/*
 * Copyright 2011-2012 Maarten Lankhorst
 * Copyright 2010-2011 Maarten Lankhorst for CodeWeavers
 * Copyright 2011 Andrew Eikum for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define NONAMELESSUNION
#define COBJMACROS
#define _GNU_SOURCE

#include "config.h"
#include <poll.h>
#include <pthread.h>

#include <stdarg.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <errno.h>

#include <pulse/pulseaudio.h>

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "winreg.h"
#include "wine/debug.h"
#include "wine/unicode.h"
#include "wine/list.h"

#include "ole2.h"
#include "dshow.h"
#include "dsound.h"
#include "propsys.h"

#include "initguid.h"
#include "ks.h"
#include "ksmedia.h"
#include "propkey.h"
#include "mmdeviceapi.h"
#include "audioclient.h"
#include "endpointvolume.h"
#include "audiopolicy.h"

WINE_DEFAULT_DEBUG_CHANNEL(pulse);

#define NULL_PTR_ERR MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, RPC_X_NULL_REF_POINTER)

/* From <dlls/mmdevapi/mmdevapi.h> */
enum DriverPriority {
    Priority_Unavailable = 0,
    Priority_Low,
    Priority_Neutral,
    Priority_Preferred
};

static const REFERENCE_TIME MinimumPeriod = 30000;
static const REFERENCE_TIME DefaultPeriod = 100000;

static pa_context *pulse_ctx;
static pa_mainloop *pulse_ml;

static HANDLE pulse_thread;
static pthread_mutex_t pulse_lock;
static pthread_cond_t pulse_cond = PTHREAD_COND_INITIALIZER;
static struct list g_sessions = LIST_INIT(g_sessions);

typedef struct _PhysDevice {
    struct list entry;
    GUID guid;
    WCHAR name[0];
} PhysDevice;

static UINT g_phys_speakers_mask = 0;
static struct list g_phys_speakers = LIST_INIT(g_phys_speakers);
static struct list g_phys_sources = LIST_INIT(g_phys_sources);

/* Mixer format + period times */
static WAVEFORMATEXTENSIBLE pulse_fmt[2];
static REFERENCE_TIME pulse_min_period[2], pulse_def_period[2];

static const WCHAR drv_key_devicesW[] = {'S','o','f','t','w','a','r','e','\\',
    'W','i','n','e','\\','D','r','i','v','e','r','s','\\',
    'w','i','n','e','p','u','l','s','e','.','d','r','v','\\','d','e','v','i','c','e','s',0};
static const WCHAR guidW[] = {'g','u','i','d',0};

static GUID pulse_render_guid =
{ 0xfd47d9cc, 0x4218, 0x4135, { 0x9c, 0xe2, 0x0c, 0x19, 0x5c, 0x87, 0x40, 0x5b } };
static GUID pulse_capture_guid =
{ 0x25da76d0, 0x033c, 0x4235, { 0x90, 0x02, 0x19, 0xf4, 0x88, 0x94, 0xac, 0x6f } };

BOOL WINAPI DllMain(HINSTANCE dll, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        pthread_mutexattr_t attr;

        DisableThreadLibraryCalls(dll);

        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);

        if (pthread_mutex_init(&pulse_lock, &attr) != 0)
            pthread_mutex_init(&pulse_lock, NULL);
    } else if (reason == DLL_PROCESS_DETACH) {
        PhysDevice *dev, *dev_next;

        LIST_FOR_EACH_ENTRY_SAFE(dev, dev_next, &g_phys_speakers, PhysDevice, entry)
            HeapFree(GetProcessHeap(), 0, dev);
        LIST_FOR_EACH_ENTRY_SAFE(dev, dev_next, &g_phys_sources, PhysDevice, entry)
            HeapFree(GetProcessHeap(), 0, dev);

        if (pulse_thread)
           SetThreadPriority(pulse_thread, 0);
        if (pulse_ctx) {
            pa_context_disconnect(pulse_ctx);
            pa_context_unref(pulse_ctx);
        }
        if (pulse_ml)
            pa_mainloop_quit(pulse_ml, 0);
        if (pulse_thread) {
            WaitForSingleObject(pulse_thread, INFINITE);
            CloseHandle(pulse_thread);
        }
    }
    return TRUE;
}

typedef struct ACImpl ACImpl;

typedef struct _AudioSession {
    GUID guid;
    struct list clients;

    IMMDevice *device;

    float master_vol;
    UINT32 channel_count;
    float *channel_vols;
    BOOL mute;

    struct list entry;
} AudioSession;

typedef struct _AudioSessionWrapper {
    IAudioSessionControl2 IAudioSessionControl2_iface;
    IChannelAudioVolume IChannelAudioVolume_iface;
    ISimpleAudioVolume ISimpleAudioVolume_iface;

    LONG ref;

    ACImpl *client;
    AudioSession *session;
} AudioSessionWrapper;

typedef struct _ACPacket {
    struct list entry;
    UINT64 qpcpos;
    BYTE *data;
    UINT32 discont;
} ACPacket;

struct ACImpl {
    IAudioClient IAudioClient_iface;
    IAudioRenderClient IAudioRenderClient_iface;
    IAudioCaptureClient IAudioCaptureClient_iface;
    IAudioClock IAudioClock_iface;
    IAudioClock2 IAudioClock2_iface;
    IAudioStreamVolume IAudioStreamVolume_iface;
    IUnknown *marshal;
    IMMDevice *parent;
    struct list entry;
    float vol[PA_CHANNELS_MAX];
    char device[256];

    LONG ref;
    EDataFlow dataflow;
    DWORD flags;
    AUDCLNT_SHAREMODE share;
    HANDLE event;

    INT32 locked;
    UINT32 bufsize_frames, bufsize_bytes, capture_period, pad, started, peek_ofs, wri_offs_bytes, lcl_offs_bytes;
    UINT32 tmp_buffer_bytes, held_bytes, peek_len, peek_buffer_len;
    BYTE *local_buffer, *tmp_buffer, *peek_buffer;
    void *locked_ptr;

    pa_stream *stream;
    pa_sample_spec ss;
    pa_channel_map map;
    pa_buffer_attr attr;

    INT64 clock_lastpos, clock_written;

    AudioSession *session;
    AudioSessionWrapper *session_wrapper;
    struct list packet_free_head;
    struct list packet_filled_head;
};

static const IAudioClientVtbl AudioClient_Vtbl;
static const IAudioRenderClientVtbl AudioRenderClient_Vtbl;
static const IAudioCaptureClientVtbl AudioCaptureClient_Vtbl;
static const IAudioSessionControl2Vtbl AudioSessionControl2_Vtbl;
static const ISimpleAudioVolumeVtbl SimpleAudioVolume_Vtbl;
static const IChannelAudioVolumeVtbl ChannelAudioVolume_Vtbl;
static const IAudioClockVtbl AudioClock_Vtbl;
static const IAudioClock2Vtbl AudioClock2_Vtbl;
static const IAudioStreamVolumeVtbl AudioStreamVolume_Vtbl;

static AudioSessionWrapper *AudioSessionWrapper_Create(ACImpl *client);

static inline ACImpl *impl_from_IAudioClient(IAudioClient *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioClient_iface);
}

static inline ACImpl *impl_from_IAudioRenderClient(IAudioRenderClient *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioRenderClient_iface);
}

static inline ACImpl *impl_from_IAudioCaptureClient(IAudioCaptureClient *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioCaptureClient_iface);
}

static inline AudioSessionWrapper *impl_from_IAudioSessionControl2(IAudioSessionControl2 *iface)
{
    return CONTAINING_RECORD(iface, AudioSessionWrapper, IAudioSessionControl2_iface);
}

static inline AudioSessionWrapper *impl_from_ISimpleAudioVolume(ISimpleAudioVolume *iface)
{
    return CONTAINING_RECORD(iface, AudioSessionWrapper, ISimpleAudioVolume_iface);
}

static inline AudioSessionWrapper *impl_from_IChannelAudioVolume(IChannelAudioVolume *iface)
{
    return CONTAINING_RECORD(iface, AudioSessionWrapper, IChannelAudioVolume_iface);
}

static inline ACImpl *impl_from_IAudioClock(IAudioClock *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioClock_iface);
}

static inline ACImpl *impl_from_IAudioClock2(IAudioClock2 *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioClock2_iface);
}

static inline ACImpl *impl_from_IAudioStreamVolume(IAudioStreamVolume *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioStreamVolume_iface);
}

/* Following pulseaudio design here, mainloop has the lock taken whenever
 * it is handling something for pulse, and the lock is required whenever
 * doing any pa_* call that can affect the state in any way
 *
 * pa_cond_wait is used when waiting on results, because the mainloop needs
 * the same lock taken to affect the state
 *
 * This is basically the same as the pa_threaded_mainloop implementation,
 * but that cannot be used because it uses pthread_create directly
 *
 * pa_threaded_mainloop_(un)lock -> pthread_mutex_(un)lock
 * pa_threaded_mainloop_signal -> pthread_cond_signal
 * pa_threaded_mainloop_wait -> pthread_cond_wait
 */

static int pulse_poll_func(struct pollfd *ufds, unsigned long nfds, int timeout, void *userdata) {
    int r;
    pthread_mutex_unlock(&pulse_lock);
    r = poll(ufds, nfds, timeout);
    pthread_mutex_lock(&pulse_lock);
    return r;
}

static DWORD CALLBACK pulse_mainloop_thread(void *tmp) {
    int ret;
    pulse_ml = pa_mainloop_new();
    pa_mainloop_set_poll_func(pulse_ml, pulse_poll_func, NULL);
    pthread_mutex_lock(&pulse_lock);
    pthread_cond_signal(&pulse_cond);
    pa_mainloop_run(pulse_ml, &ret);
    pthread_mutex_unlock(&pulse_lock);
    pa_mainloop_free(pulse_ml);
    return ret;
}

static void pulse_contextcallback(pa_context *c, void *userdata)
{
    switch (pa_context_get_state(c)) {
        default:
            FIXME("Unhandled state: %i\n", pa_context_get_state(c));
            return;

        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
        case PA_CONTEXT_TERMINATED:
            TRACE("State change to %i\n", pa_context_get_state(c));
            return;

        case PA_CONTEXT_READY:
            TRACE("Ready\n");
            break;

        case PA_CONTEXT_FAILED:
            WARN("Context failed: %s\n", pa_strerror(pa_context_errno(c)));
            break;
    }
    pthread_cond_signal(&pulse_cond);
}

static void pulse_stream_state(pa_stream *s, void *user)
{
    pa_stream_state_t state = pa_stream_get_state(s);
    TRACE("Stream state changed to %i\n", state);
    pthread_cond_signal(&pulse_cond);
}

static const enum pa_channel_position pulse_pos_from_wfx[] = {
    PA_CHANNEL_POSITION_FRONT_LEFT,
    PA_CHANNEL_POSITION_FRONT_RIGHT,
    PA_CHANNEL_POSITION_FRONT_CENTER,
    PA_CHANNEL_POSITION_LFE,
    PA_CHANNEL_POSITION_REAR_LEFT,
    PA_CHANNEL_POSITION_REAR_RIGHT,
    PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,
    PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,
    PA_CHANNEL_POSITION_REAR_CENTER,
    PA_CHANNEL_POSITION_SIDE_LEFT,
    PA_CHANNEL_POSITION_SIDE_RIGHT,
    PA_CHANNEL_POSITION_TOP_CENTER,
    PA_CHANNEL_POSITION_TOP_FRONT_LEFT,
    PA_CHANNEL_POSITION_TOP_FRONT_CENTER,
    PA_CHANNEL_POSITION_TOP_FRONT_RIGHT,
    PA_CHANNEL_POSITION_TOP_REAR_LEFT,
    PA_CHANNEL_POSITION_TOP_REAR_CENTER,
    PA_CHANNEL_POSITION_TOP_REAR_RIGHT
};

static DWORD pulse_channel_map_to_channel_mask(const pa_channel_map *map) {
    int i;
    DWORD mask = 0;

    for (i = 0; i < map->channels; ++i)
        switch (map->map[i]) {
            default: FIXME("Unhandled channel %s\n", pa_channel_position_to_string(map->map[i])); break;
            case PA_CHANNEL_POSITION_FRONT_LEFT: mask |= SPEAKER_FRONT_LEFT; break;
            case PA_CHANNEL_POSITION_MONO:
            case PA_CHANNEL_POSITION_FRONT_CENTER: mask |= SPEAKER_FRONT_CENTER; break;
            case PA_CHANNEL_POSITION_FRONT_RIGHT: mask |= SPEAKER_FRONT_RIGHT; break;
            case PA_CHANNEL_POSITION_REAR_LEFT: mask |= SPEAKER_BACK_LEFT; break;
            case PA_CHANNEL_POSITION_REAR_CENTER: mask |= SPEAKER_BACK_CENTER; break;
            case PA_CHANNEL_POSITION_REAR_RIGHT: mask |= SPEAKER_BACK_RIGHT; break;
            case PA_CHANNEL_POSITION_LFE: mask |= SPEAKER_LOW_FREQUENCY; break;
            case PA_CHANNEL_POSITION_SIDE_LEFT: mask |= SPEAKER_SIDE_LEFT; break;
            case PA_CHANNEL_POSITION_SIDE_RIGHT: mask |= SPEAKER_SIDE_RIGHT; break;
            case PA_CHANNEL_POSITION_TOP_CENTER: mask |= SPEAKER_TOP_CENTER; break;
            case PA_CHANNEL_POSITION_TOP_FRONT_LEFT: mask |= SPEAKER_TOP_FRONT_LEFT; break;
            case PA_CHANNEL_POSITION_TOP_FRONT_CENTER: mask |= SPEAKER_TOP_FRONT_CENTER; break;
            case PA_CHANNEL_POSITION_TOP_FRONT_RIGHT: mask |= SPEAKER_TOP_FRONT_RIGHT; break;
            case PA_CHANNEL_POSITION_TOP_REAR_LEFT: mask |= SPEAKER_TOP_BACK_LEFT; break;
            case PA_CHANNEL_POSITION_TOP_REAR_CENTER: mask |= SPEAKER_TOP_BACK_CENTER; break;
            case PA_CHANNEL_POSITION_TOP_REAR_RIGHT: mask |= SPEAKER_TOP_BACK_RIGHT; break;
            case PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER: mask |= SPEAKER_FRONT_LEFT_OF_CENTER; break;
            case PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER: mask |= SPEAKER_FRONT_RIGHT_OF_CENTER; break;
    }

    return mask;
}

static void pulse_probe_settings(pa_mainloop *ml, pa_context *ctx, int render, WAVEFORMATEXTENSIBLE *fmt) {
    WAVEFORMATEX *wfx = &fmt->Format;
    pa_stream *stream;
    pa_channel_map map;
    pa_sample_spec ss;
    pa_buffer_attr attr;
    int ret;
    unsigned int length = 0;

    pa_channel_map_init_auto(&map, 2, PA_CHANNEL_MAP_ALSA);
    ss.rate = 48000;
    ss.format = PA_SAMPLE_FLOAT32LE;
    ss.channels = map.channels;

    attr.maxlength = -1;
    attr.tlength = -1;
    attr.minreq = attr.fragsize = pa_frame_size(&ss);
    attr.prebuf = 0;

    stream = pa_stream_new(ctx, "format test stream", &ss, &map);
    if (stream)
        pa_stream_set_state_callback(stream, pulse_stream_state, NULL);
    if (!stream)
        ret = -1;
    else if (render)
        ret = pa_stream_connect_playback(stream, NULL, &attr,
        PA_STREAM_START_CORKED|PA_STREAM_FIX_RATE|PA_STREAM_FIX_CHANNELS|PA_STREAM_EARLY_REQUESTS, NULL, NULL);
    else
        ret = pa_stream_connect_record(stream, NULL, &attr, PA_STREAM_START_CORKED|PA_STREAM_FIX_RATE|PA_STREAM_FIX_CHANNELS|PA_STREAM_EARLY_REQUESTS);
    if (ret >= 0) {
        while (pa_mainloop_iterate(ml, 1, &ret) >= 0 &&
                pa_stream_get_state(stream) == PA_STREAM_CREATING)
        {}
        if (pa_stream_get_state(stream) == PA_STREAM_READY) {
            ss = *pa_stream_get_sample_spec(stream);
            map = *pa_stream_get_channel_map(stream);
            if (render)
                length = pa_stream_get_buffer_attr(stream)->minreq;
            else
                length = pa_stream_get_buffer_attr(stream)->fragsize;
            pa_stream_disconnect(stream);
            while (pa_mainloop_iterate(ml, 1, &ret) >= 0 &&
                    pa_stream_get_state(stream) == PA_STREAM_READY)
            {}
        }
    }

    if (stream)
        pa_stream_unref(stream);

    if (length)
        pulse_def_period[!render] = pulse_min_period[!render] = pa_bytes_to_usec(10 * length, &ss);

    if (pulse_min_period[!render] < MinimumPeriod)
        pulse_min_period[!render] = MinimumPeriod;

    if (pulse_def_period[!render] < DefaultPeriod)
        pulse_def_period[!render] = DefaultPeriod;

    wfx->wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfx->cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wfx->nChannels = ss.channels;
    wfx->wBitsPerSample = 8 * pa_sample_size_of_format(ss.format);
    wfx->nSamplesPerSec = ss.rate;
    wfx->nBlockAlign = pa_frame_size(&ss);
    wfx->nAvgBytesPerSec = wfx->nSamplesPerSec * wfx->nBlockAlign;
    if (ss.format != PA_SAMPLE_S24_32LE)
        fmt->Samples.wValidBitsPerSample = wfx->wBitsPerSample;
    else
        fmt->Samples.wValidBitsPerSample = 24;
    if (ss.format == PA_SAMPLE_FLOAT32LE)
        fmt->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    else
        fmt->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

    fmt->dwChannelMask = pulse_channel_map_to_channel_mask(&map);
}

static HRESULT pulse_connect(void)
{
    int len;
    WCHAR path[MAX_PATH], *name;
    char *str;

    if (!pulse_thread)
    {
        if (!(pulse_thread = CreateThread(NULL, 0, pulse_mainloop_thread, NULL, 0, NULL)))
        {
            ERR("Failed to create mainloop thread.\n");
            return E_FAIL;
        }
        SetThreadPriority(pulse_thread, THREAD_PRIORITY_TIME_CRITICAL);
        pthread_cond_wait(&pulse_cond, &pulse_lock);
    }

    if (pulse_ctx && PA_CONTEXT_IS_GOOD(pa_context_get_state(pulse_ctx)))
        return S_OK;
    if (pulse_ctx)
        pa_context_unref(pulse_ctx);

    GetModuleFileNameW(NULL, path, sizeof(path)/sizeof(*path));
    name = strrchrW(path, '\\');
    if (!name)
        name = path;
    else
        name++;
    len = WideCharToMultiByte(CP_UNIXCP, 0, name, -1, NULL, 0, NULL, NULL);
    str = pa_xmalloc(len);
    WideCharToMultiByte(CP_UNIXCP, 0, name, -1, str, len, NULL, NULL);
    TRACE("Name: %s\n", str);
    pulse_ctx = pa_context_new(pa_mainloop_get_api(pulse_ml), str);
    pa_xfree(str);
    if (!pulse_ctx) {
        ERR("Failed to create context\n");
        return E_FAIL;
    }

    pa_context_set_state_callback(pulse_ctx, pulse_contextcallback, NULL);

    TRACE("libpulse protocol version: %u. API Version %u\n", pa_context_get_protocol_version(pulse_ctx), PA_API_VERSION);
    if (pa_context_connect(pulse_ctx, NULL, 0, NULL) < 0)
        goto fail;

    /* Wait for connection */
    while (pthread_cond_wait(&pulse_cond, &pulse_lock)) {
        pa_context_state_t state = pa_context_get_state(pulse_ctx);

        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED)
            goto fail;

        if (state == PA_CONTEXT_READY)
            break;
    }

    TRACE("Connected to server %s with protocol version: %i.\n",
        pa_context_get_server(pulse_ctx),
        pa_context_get_server_protocol_version(pulse_ctx));
    return S_OK;

fail:
    pa_context_unref(pulse_ctx);
    pulse_ctx = NULL;
    return E_FAIL;
}

static BOOL get_device_guid(EDataFlow flow, const char *device, GUID *guid)
{
    HKEY key, dev_key;
    DWORD type, size = sizeof(*guid);
    WCHAR key_name[258];

    key_name[0] = (flow == eCapture) ? '1' : '0';
    key_name[1] = ',';
    if (!MultiByteToWideChar(CP_UTF8, 0, device, -1, key_name + 2,
            (sizeof(key_name) / sizeof(*key_name)) - 2))
        return FALSE;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, drv_key_devicesW, 0, NULL, 0,
            KEY_WRITE|KEY_READ, NULL, &key, NULL) != ERROR_SUCCESS){
        ERR("Failed to open registry key %s\n", debugstr_w(drv_key_devicesW));
        return FALSE;
    }

    if (RegCreateKeyExW(key, key_name, 0, NULL, 0, KEY_WRITE|KEY_READ,
            NULL, &dev_key, NULL) != ERROR_SUCCESS){
        ERR("Failed to open registry key for device %s\n", debugstr_w(key_name));
        RegCloseKey(key);
        return FALSE;
    }

    if (RegQueryValueExW(dev_key, guidW, 0, &type, (BYTE *)guid,
            &size) == ERROR_SUCCESS){
        if (type == REG_BINARY && size == sizeof(*guid)){
            RegCloseKey(dev_key);
            RegCloseKey(key);
            return TRUE;
        }

        ERR("Invalid type for device %s GUID: %u; ignoring and overwriting\n",
                wine_dbgstr_w(key_name), type);
    }

    /* generate new GUID for this device */
    CoCreateGuid(guid);

    if (RegSetValueExW(dev_key, guidW, 0, REG_BINARY, (BYTE *)guid,
            sizeof(GUID)) != ERROR_SUCCESS)
        ERR("Failed to store device GUID for %s to registry\n", device);

    RegCloseKey(dev_key);
    RegCloseKey(key);
    return TRUE;
}

static void pulse_add_device(struct list *list, GUID *guid, const char *name)
{
    int len = MultiByteToWideChar(CP_UNIXCP, 0, name, -1, NULL, 0);
    if (len) {
        PhysDevice *dev = HeapAlloc(GetProcessHeap(), 0, offsetof(PhysDevice, name[len]));
        if (dev) {
            MultiByteToWideChar(CP_UNIXCP, 0, name, -1, dev->name, len);
            dev->guid = *guid;
            list_add_tail(list, &dev->entry);
        }
    }
}

static void pulse_phys_speakers_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
    GUID guid;

    if (i) {
        /* For default PulseAudio render device, OR together all of the
         * PKEY_AudioEndpoint_PhysicalSpeakers values of the sinks. */
        g_phys_speakers_mask |= pulse_channel_map_to_channel_mask(&i->channel_map);

        if (!get_device_guid(eRender, i->name, &guid))
            CoCreateGuid(&guid);
        pulse_add_device(&g_phys_speakers, &guid, i->description);
    }
}

static void pulse_phys_sources_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata)
{
    GUID guid;

    if (i) {
        if (!get_device_guid(eCapture, i->name, &guid))
            CoCreateGuid(&guid);
        pulse_add_device(&g_phys_sources, &guid, i->description);
    }
}

/* some poorly-behaved applications call audio functions during DllMain, so we
 * have to do as much as possible without creating a new thread. this function
 * sets up a synchronous connection to verify the server is running and query
 * static data. */
static HRESULT pulse_test_connect(void)
{
    int len, ret;
    WCHAR path[MAX_PATH], *name;
    char *str;
    pa_operation *o;
    pa_mainloop *ml;
    pa_context *ctx;

    /* Make sure we never run this function twice accidentially */
    if (!list_empty(&g_phys_speakers))
        return S_OK;

    ml = pa_mainloop_new();

    pa_mainloop_set_poll_func(ml, pulse_poll_func, NULL);

    GetModuleFileNameW(NULL, path, sizeof(path)/sizeof(*path));
    name = strrchrW(path, '\\');
    if (!name)
        name = path;
    else
        name++;
    len = WideCharToMultiByte(CP_UNIXCP, 0, name, -1, NULL, 0, NULL, NULL);
    str = pa_xmalloc(len);
    WideCharToMultiByte(CP_UNIXCP, 0, name, -1, str, len, NULL, NULL);
    TRACE("Name: %s\n", str);
    ctx = pa_context_new(pa_mainloop_get_api(ml), str);
    pa_xfree(str);
    if (!ctx) {
        ERR("Failed to create context\n");
        pa_mainloop_free(ml);
        return E_FAIL;
    }

    pa_context_set_state_callback(ctx, pulse_contextcallback, NULL);

    TRACE("libpulse protocol version: %u. API Version %u\n", pa_context_get_protocol_version(ctx), PA_API_VERSION);
    if (pa_context_connect(ctx, NULL, 0, NULL) < 0)
        goto fail;

    /* Wait for connection */
    while (pa_mainloop_iterate(ml, 1, &ret) >= 0) {
        pa_context_state_t state = pa_context_get_state(ctx);

        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED)
            goto fail;

        if (state == PA_CONTEXT_READY)
            break;
    }

    if (pa_context_get_state(ctx) != PA_CONTEXT_READY)
        goto fail;

    TRACE("Test-connected to server %s with protocol version: %i.\n",
        pa_context_get_server(ctx),
        pa_context_get_server_protocol_version(ctx));

    pulse_probe_settings(ml, ctx, 1, &pulse_fmt[0]);
    pulse_probe_settings(ml, ctx, 0, &pulse_fmt[1]);

    g_phys_speakers_mask = 0;
    pulse_add_device(&g_phys_speakers, &pulse_render_guid, "Pulseaudio");
    pulse_add_device(&g_phys_sources, &pulse_capture_guid, "Pulseaudio");

    o = pa_context_get_sink_info_list(ctx, &pulse_phys_speakers_cb, NULL);
    if (o) {
        while (pa_mainloop_iterate(ml, 1, &ret) >= 0 &&
                pa_operation_get_state(o) == PA_OPERATION_RUNNING)
        {}
        pa_operation_unref(o);
    }

    o = pa_context_get_source_info_list(ctx, &pulse_phys_sources_cb, NULL);
    if (o) {
        while (pa_mainloop_iterate(ml, 1, &ret) >= 0 &&
                pa_operation_get_state(o) == PA_OPERATION_RUNNING)
        {}
        pa_operation_unref(o);
    }

    pa_context_unref(ctx);
    pa_mainloop_free(ml);
    return S_OK;

fail:
    pa_context_unref(ctx);
    pa_mainloop_free(ml);
    return E_FAIL;
}

static HRESULT pulse_stream_valid(ACImpl *This) {
    if (!This->stream)
        return AUDCLNT_E_NOT_INITIALIZED;
    if (!This->stream || pa_stream_get_state(This->stream) != PA_STREAM_READY)
        return AUDCLNT_E_DEVICE_INVALIDATED;
    return S_OK;
}

static void silence_buffer(pa_sample_format_t format, BYTE *buffer, UINT32 bytes)
{
    memset(buffer, format == PA_SAMPLE_U8 ? 0x80 : 0, bytes);
}

static void dump_attr(const pa_buffer_attr *attr) {
    TRACE("maxlength: %u\n", attr->maxlength);
    TRACE("minreq: %u\n", attr->minreq);
    TRACE("fragsize: %u\n", attr->fragsize);
    TRACE("tlength: %u\n", attr->tlength);
    TRACE("prebuf: %u\n", attr->prebuf);
}

static void pulse_op_cb(pa_stream *s, int success, void *user) {
    TRACE("Success: %i\n", success);
    *(int*)user = success;
    pthread_cond_signal(&pulse_cond);
}

static void pulse_attr_update(pa_stream *s, void *user) {
    const pa_buffer_attr *attr = pa_stream_get_buffer_attr(s);
    TRACE("New attributes or device moved:\n");
    dump_attr(attr);
}

/* Here's the buffer setup:
 *
 *  vvvvvvvv sent to HW already
 *          vvvvvvvv in Pulse buffer but rewindable
 * [dddddddddddddddd] Pulse buffer
 *         [dddddddddddddddd--------] mmdevapi buffer
 *          ^^^^^^^^^^^^^^^^ pad
 *                  ^ lcl_offs_bytes
 *                  ^^^^^^^^^ held_bytes
 *                          ^ wri_offs_bytes
 *
 * GetCurrentPadding is pad
 *
 * During pulse_wr_callback, we decrement pad, fill Pulse buffer, and move
 *   lcl_offs forward
 *
 * During Stop, we flush the Pulse buffer
 */
static void pulse_wr_callback(pa_stream *s, size_t bytes, void *userdata)
{
    ACImpl *This = userdata;
    UINT32 oldpad = This->pad;

    if(This->local_buffer){
        UINT32 to_write;
        BYTE *buf = This->local_buffer + This->lcl_offs_bytes;

        if(This->pad > bytes){
            This->clock_written += bytes;
            This->pad -= bytes;
        }else{
            This->clock_written += This->pad;
            This->pad = 0;
        }

        bytes = min(bytes, This->held_bytes);

        if(This->lcl_offs_bytes + bytes > This->bufsize_bytes){
            to_write = This->bufsize_bytes - This->lcl_offs_bytes;
            TRACE("writing small chunk of %u bytes\n", to_write);
            pa_stream_write(This->stream, buf, to_write, NULL, 0, PA_SEEK_RELATIVE);
            This->held_bytes -= to_write;
            to_write = bytes - to_write;
            This->lcl_offs_bytes = 0;
            buf = This->local_buffer;
        }else
            to_write = bytes;

        TRACE("writing main chunk of %u bytes\n", to_write);
        pa_stream_write(This->stream, buf, to_write, NULL, 0, PA_SEEK_RELATIVE);
        This->lcl_offs_bytes += to_write;
        This->lcl_offs_bytes %= This->bufsize_bytes;
        This->held_bytes -= to_write;
    }else{
        if (bytes < This->bufsize_bytes)
            This->pad = This->bufsize_bytes - bytes;
        else
            This->pad = 0;

        if (oldpad == This->pad)
            return;

        assert(oldpad > This->pad);

        This->clock_written += oldpad - This->pad;
        TRACE("New pad: %zu (-%zu)\n", This->pad / pa_frame_size(&This->ss), (oldpad - This->pad) / pa_frame_size(&This->ss));
    }

    if (This->event)
        SetEvent(This->event);
}

static void pulse_underflow_callback(pa_stream *s, void *userdata)
{
    WARN("Underflow\n");
}

/* Latency is periodically updated even when nothing is played,
 * because of PA_STREAM_AUTO_TIMING_UPDATE so use it as timer
 *
 * Perfect for passing all tests :)
 */
static void pulse_latency_callback(pa_stream *s, void *userdata)
{
    ACImpl *This = userdata;
    if (!This->pad && This->event)
        SetEvent(This->event);
}

static void pulse_started_callback(pa_stream *s, void *userdata)
{
    TRACE("(Re)started playing\n");
}

static void pulse_rd_loop(ACImpl *This, size_t bytes)
{
    while (bytes >= This->capture_period) {
        ACPacket *p, *next;
        LARGE_INTEGER stamp, freq;
        BYTE *dst, *src;
        size_t src_len, copy, rem = This->capture_period;
        if (!(p = (ACPacket*)list_head(&This->packet_free_head))) {
            p = (ACPacket*)list_head(&This->packet_filled_head);
            if (!p->discont) {
                next = (ACPacket*)p->entry.next;
                next->discont = 1;
            } else
                p = (ACPacket*)list_tail(&This->packet_filled_head);
            assert(This->pad == This->bufsize_bytes);
        } else {
            assert(This->pad < This->bufsize_bytes);
            This->pad += This->capture_period;
            assert(This->pad <= This->bufsize_bytes);
        }
        QueryPerformanceCounter(&stamp);
        QueryPerformanceFrequency(&freq);
        p->qpcpos = (stamp.QuadPart * (INT64)10000000) / freq.QuadPart;
        p->discont = 0;
        list_remove(&p->entry);
        list_add_tail(&This->packet_filled_head, &p->entry);

        dst = p->data;
        while (rem) {
            if (This->peek_len) {
                copy = min(rem, This->peek_len - This->peek_ofs);

                memcpy(dst, This->peek_buffer + This->peek_ofs, copy);

                rem -= copy;
                dst += copy;
                This->peek_ofs += copy;
                if(This->peek_len == This->peek_ofs)
                    This->peek_len = 0;
            } else {
                pa_stream_peek(This->stream, (const void**)&src, &src_len);

                copy = min(rem, src_len);

                memcpy(dst, src, rem);

                dst += copy;
                rem -= copy;

                if (copy < src_len) {
                    if (src_len > This->peek_buffer_len) {
                        HeapFree(GetProcessHeap(), 0, This->peek_buffer);
                        This->peek_buffer = HeapAlloc(GetProcessHeap(), 0, src_len);
                        This->peek_buffer_len = src_len;
                    }

                    memcpy(This->peek_buffer, src + copy, src_len - copy);
                    This->peek_len = src_len - copy;
                    This->peek_ofs = 0;
                }

                pa_stream_drop(This->stream);
            }
        }

        bytes -= This->capture_period;
    }
}

static void pulse_rd_drop(ACImpl *This, size_t bytes)
{
    while (bytes >= This->capture_period) {
        size_t src_len, copy, rem = This->capture_period;
        while (rem) {
            const void *src;
            pa_stream_peek(This->stream, &src, &src_len);
            assert(src_len);
            assert(This->peek_ofs < src_len);
            src_len -= This->peek_ofs;
            assert(src_len <= bytes);

            copy = rem;
            if (copy > src_len)
                copy = src_len;

            src_len -= copy;
            rem -= copy;

            if (!src_len) {
                This->peek_ofs = 0;
                pa_stream_drop(This->stream);
            } else
                This->peek_ofs += copy;
            bytes -= copy;
        }
    }
}

static void pulse_rd_callback(pa_stream *s, size_t bytes, void *userdata)
{
    ACImpl *This = userdata;

    TRACE("Readable total: %zu, fragsize: %u\n", bytes, pa_stream_get_buffer_attr(s)->fragsize);
    assert(bytes >= This->peek_ofs);
    bytes -= This->peek_ofs;
    if (bytes < This->capture_period)
        return;

    if (This->started)
        pulse_rd_loop(This, bytes);
    else
        pulse_rd_drop(This, bytes);

    if (This->event)
        SetEvent(This->event);
}

static HRESULT pulse_stream_connect(ACImpl *This, UINT32 period_bytes) {
    int ret;
    char buffer[64];
    static LONG number;
    pa_buffer_attr attr;
    int moving = 0;
    const char *dev = NULL;

    if (This->stream) {
        pa_stream_disconnect(This->stream);
        while (pa_stream_get_state(This->stream) == PA_STREAM_READY)
            pthread_cond_wait(&pulse_cond, &pulse_lock);
        pa_stream_unref(This->stream);
    }
    ret = InterlockedIncrement(&number);
    sprintf(buffer, "audio stream #%i", ret);
    This->stream = pa_stream_new(pulse_ctx, buffer, &This->ss, &This->map);

    if (!This->stream) {
        WARN("pa_stream_new returned error %i\n", pa_context_errno(pulse_ctx));
        return AUDCLNT_E_ENDPOINT_CREATE_FAILED;
    }

    pa_stream_set_state_callback(This->stream, pulse_stream_state, This);
    pa_stream_set_buffer_attr_callback(This->stream, pulse_attr_update, This);
    pa_stream_set_moved_callback(This->stream, pulse_attr_update, This);

    /* PulseAudio will fill in correct values */
    attr.minreq = attr.fragsize = period_bytes;
    attr.maxlength = attr.tlength = This->bufsize_bytes;
    attr.prebuf = pa_frame_size(&This->ss);
    dump_attr(&attr);

    /* If device name is given use exactly the specified device */
    if (This->device[0]){
        moving = PA_STREAM_DONT_MOVE;
        dev    = This->device;
    }

    if (This->dataflow == eRender)
        ret = pa_stream_connect_playback(This->stream, dev, &attr,
                PA_STREAM_START_CORKED|PA_STREAM_START_UNMUTED|PA_STREAM_AUTO_TIMING_UPDATE|
                PA_STREAM_INTERPOLATE_TIMING|PA_STREAM_EARLY_REQUESTS|moving, NULL, NULL);
    else
        ret = pa_stream_connect_record(This->stream, dev, &attr,
                PA_STREAM_START_CORKED|PA_STREAM_START_UNMUTED|PA_STREAM_AUTO_TIMING_UPDATE|
                PA_STREAM_INTERPOLATE_TIMING|PA_STREAM_EARLY_REQUESTS|moving);
    if (ret < 0) {
        WARN("Returns %i\n", ret);
        return AUDCLNT_E_ENDPOINT_CREATE_FAILED;
    }
    while (pa_stream_get_state(This->stream) == PA_STREAM_CREATING)
        pthread_cond_wait(&pulse_cond, &pulse_lock);
    if (pa_stream_get_state(This->stream) != PA_STREAM_READY)
        return AUDCLNT_E_ENDPOINT_CREATE_FAILED;

    if (This->dataflow == eRender) {
        pa_stream_set_write_callback(This->stream, pulse_wr_callback, This);
        pa_stream_set_underflow_callback(This->stream, pulse_underflow_callback, This);
        pa_stream_set_started_callback(This->stream, pulse_started_callback, This);
    } else
        pa_stream_set_read_callback(This->stream, pulse_rd_callback, This);
    return S_OK;
}

HRESULT WINAPI AUDDRV_GetEndpointIDs(EDataFlow flow, WCHAR ***ids, GUID **keys,
        UINT *num, UINT *def_index)
{
    struct list *list = (flow == eRender) ? &g_phys_speakers : &g_phys_sources;
    PhysDevice *dev;
    DWORD count;
    WCHAR *id;

    TRACE("%d %p %p %p\n", flow, ids, num, def_index);

    *num = count = list_count(list);
    *def_index = 0;

    if (!count) {
        *ids = NULL;
        *keys = NULL;
        return E_FAIL;
    }

    *ids = HeapAlloc(GetProcessHeap(), 0, count * sizeof(**ids));
    *keys = HeapAlloc(GetProcessHeap(), 0, count * sizeof(**keys));
    if (!*ids || !*keys) {
        count = 0;
        goto err;
    }

    count = 0;
    LIST_FOR_EACH_ENTRY(dev, list, PhysDevice, entry) {
        id = HeapAlloc(GetProcessHeap(), 0, (strlenW(dev->name) + 1) * sizeof(WCHAR));
        if (!id)
            goto err;
        (*ids)[count] = id;
        (*keys)[count] = dev->guid;
        strcpyW(id, dev->name);
        count++;
    }

    return S_OK;

err:
    while (count)
        HeapFree(GetProcessHeap(), 0, (*ids)[--count]);
    HeapFree(GetProcessHeap(), 0, *keys);
    HeapFree(GetProcessHeap(), 0, *ids);
    *ids = NULL;
    *keys = NULL;
    return E_OUTOFMEMORY;
}

int WINAPI AUDDRV_GetPriority(void)
{
    HRESULT hr;
    pthread_mutex_lock(&pulse_lock);
    hr = pulse_test_connect();
    pthread_mutex_unlock(&pulse_lock);
    return SUCCEEDED(hr) ? Priority_Preferred : Priority_Unavailable;
}

static BOOL get_pulse_name_by_guid(const GUID *guid, char *name, DWORD name_size, EDataFlow *flow)
{
    HKEY key;
    DWORD index = 0;
    WCHAR key_name[258];
    DWORD key_name_size;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, drv_key_devicesW, 0, KEY_READ,
            &key) != ERROR_SUCCESS){
        ERR("No devices found in registry?\n");
        return FALSE;
    }

    while(1){
        HKEY dev_key;
        DWORD size, type;
        GUID reg_guid;

        key_name_size = sizeof(key_name)/sizeof(WCHAR);
        if(RegEnumKeyExW(key, index++, key_name, &key_name_size, NULL,
                NULL, NULL, NULL) != ERROR_SUCCESS)
            break;

        if (RegOpenKeyExW(key, key_name, 0, KEY_READ, &dev_key) != ERROR_SUCCESS){
            ERR("Couldn't open key: %s\n", wine_dbgstr_w(key_name));
            continue;
        }

        size = sizeof(reg_guid);
        if (RegQueryValueExW(dev_key, guidW, 0, &type, (BYTE *)&reg_guid, &size) == ERROR_SUCCESS){
            if (type == REG_BINARY && size == sizeof(reg_guid) && IsEqualGUID(&reg_guid, guid)){
                RegCloseKey(dev_key);
                RegCloseKey(key);

                TRACE("Found matching device key: %s\n", wine_dbgstr_w(key_name));

                if (key_name[0] == '0')
                    *flow = eRender;
                else if (key_name[0] == '1')
                    *flow = eCapture;
                else{
                    ERR("Unknown device type: %c\n", key_name[0]);
                    return FALSE;
                }

                return WideCharToMultiByte(CP_UNIXCP, 0, key_name + 2, -1, name, name_size, NULL, NULL);
            }
        }

        RegCloseKey(dev_key);
    }

    RegCloseKey(key);
    WARN("No matching device in registry for GUID %s\n", debugstr_guid(guid));
    return FALSE;
}

HRESULT WINAPI AUDDRV_GetAudioEndpoint(GUID *guid, IMMDevice *dev, IAudioClient **out)
{
    char pulse_name[256] = {0};
    ACImpl *This;
    int i;
    EDataFlow dataflow;
    HRESULT hr;

    TRACE("%s %p %p\n", debugstr_guid(guid), dev, out);

    if (IsEqualGUID(guid, &pulse_render_guid))
        dataflow = eRender;
    else if (IsEqualGUID(guid, &pulse_capture_guid))
        dataflow = eCapture;
    else if(!get_pulse_name_by_guid(guid, pulse_name, sizeof(pulse_name), &dataflow))
        return AUDCLNT_E_DEVICE_INVALIDATED;

    *out = NULL;

    This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This));
    if (!This)
        return E_OUTOFMEMORY;

    This->IAudioClient_iface.lpVtbl = &AudioClient_Vtbl;
    This->IAudioRenderClient_iface.lpVtbl = &AudioRenderClient_Vtbl;
    This->IAudioCaptureClient_iface.lpVtbl = &AudioCaptureClient_Vtbl;
    This->IAudioClock_iface.lpVtbl = &AudioClock_Vtbl;
    This->IAudioClock2_iface.lpVtbl = &AudioClock2_Vtbl;
    This->IAudioStreamVolume_iface.lpVtbl = &AudioStreamVolume_Vtbl;
    This->dataflow = dataflow;
    This->parent = dev;
    for (i = 0; i < PA_CHANNELS_MAX; ++i)
        This->vol[i] = 1.f;
    strcpy(This->device, pulse_name);

    hr = CoCreateFreeThreadedMarshaler((IUnknown*)&This->IAudioClient_iface, &This->marshal);
    if (hr) {
        HeapFree(GetProcessHeap(), 0, This);
        return hr;
    }
    IMMDevice_AddRef(This->parent);

    *out = &This->IAudioClient_iface;
    IAudioClient_AddRef(&This->IAudioClient_iface);

    return S_OK;
}

static HRESULT WINAPI AudioClient_QueryInterface(IAudioClient *iface,
        REFIID riid, void **ppv)
{
    ACImpl *This = impl_from_IAudioClient(iface);

    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;

    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IAudioClient))
        *ppv = iface;
    if (*ppv) {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    if (IsEqualIID(riid, &IID_IMarshal))
        return IUnknown_QueryInterface(This->marshal, riid, ppv);

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioClient_AddRef(IAudioClient *iface)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    ULONG ref;
    ref = InterlockedIncrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    return ref;
}

static ULONG WINAPI AudioClient_Release(IAudioClient *iface)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    ULONG ref;
    ref = InterlockedDecrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    if (!ref) {
        if (This->stream) {
            pthread_mutex_lock(&pulse_lock);
            if (PA_STREAM_IS_GOOD(pa_stream_get_state(This->stream))) {
                pa_stream_disconnect(This->stream);
                while (PA_STREAM_IS_GOOD(pa_stream_get_state(This->stream)))
                    pthread_cond_wait(&pulse_cond, &pulse_lock);
            }
            pa_stream_unref(This->stream);
            This->stream = NULL;
            list_remove(&This->entry);
            pthread_mutex_unlock(&pulse_lock);
        }
        IUnknown_Release(This->marshal);
        IMMDevice_Release(This->parent);
        HeapFree(GetProcessHeap(), 0, This->tmp_buffer);
        HeapFree(GetProcessHeap(), 0, This->peek_buffer);
        HeapFree(GetProcessHeap(), 0, This->local_buffer);
        HeapFree(GetProcessHeap(), 0, This);
    }
    return ref;
}

static void dump_fmt(const WAVEFORMATEX *fmt)
{
    TRACE("wFormatTag: 0x%x (", fmt->wFormatTag);
    switch(fmt->wFormatTag) {
    case WAVE_FORMAT_PCM:
        TRACE("WAVE_FORMAT_PCM");
        break;
    case WAVE_FORMAT_IEEE_FLOAT:
        TRACE("WAVE_FORMAT_IEEE_FLOAT");
        break;
    case WAVE_FORMAT_EXTENSIBLE:
        TRACE("WAVE_FORMAT_EXTENSIBLE");
        break;
    default:
        TRACE("Unknown");
        break;
    }
    TRACE(")\n");

    TRACE("nChannels: %u\n", fmt->nChannels);
    TRACE("nSamplesPerSec: %u\n", fmt->nSamplesPerSec);
    TRACE("nAvgBytesPerSec: %u\n", fmt->nAvgBytesPerSec);
    TRACE("nBlockAlign: %u\n", fmt->nBlockAlign);
    TRACE("wBitsPerSample: %u\n", fmt->wBitsPerSample);
    TRACE("cbSize: %u\n", fmt->cbSize);

    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE *fmtex = (void*)fmt;
        TRACE("dwChannelMask: %08x\n", fmtex->dwChannelMask);
        TRACE("Samples: %04x\n", fmtex->Samples.wReserved);
        TRACE("SubFormat: %s\n", wine_dbgstr_guid(&fmtex->SubFormat));
    }
}

static WAVEFORMATEX *clone_format(const WAVEFORMATEX *fmt)
{
    WAVEFORMATEX *ret;
    size_t size;

    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        size = sizeof(WAVEFORMATEXTENSIBLE);
    else
        size = sizeof(WAVEFORMATEX);

    ret = CoTaskMemAlloc(size);
    if (!ret)
        return NULL;

    memcpy(ret, fmt, size);

    ret->cbSize = size - sizeof(WAVEFORMATEX);

    return ret;
}

static DWORD get_channel_mask(unsigned int channels)
{
    switch(channels) {
    case 0:
        return 0;
    case 1:
        return KSAUDIO_SPEAKER_MONO;
    case 2:
        return KSAUDIO_SPEAKER_STEREO;
    case 3:
        return KSAUDIO_SPEAKER_STEREO | SPEAKER_LOW_FREQUENCY;
    case 4:
        return KSAUDIO_SPEAKER_QUAD;    /* not _SURROUND */
    case 5:
        return KSAUDIO_SPEAKER_QUAD | SPEAKER_LOW_FREQUENCY;
    case 6:
        return KSAUDIO_SPEAKER_5POINT1; /* not 5POINT1_SURROUND */
    case 7:
        return KSAUDIO_SPEAKER_5POINT1 | SPEAKER_BACK_CENTER;
    case 8:
        return KSAUDIO_SPEAKER_7POINT1_SURROUND; /* Vista deprecates 7POINT1 */
    }
    FIXME("Unknown speaker configuration: %u\n", channels);
    return 0;
}

static void session_init_vols(AudioSession *session, UINT channels)
{
    if (session->channel_count < channels) {
        UINT i;

        if (session->channel_vols)
            session->channel_vols = HeapReAlloc(GetProcessHeap(), 0,
                    session->channel_vols, sizeof(float) * channels);
        else
            session->channel_vols = HeapAlloc(GetProcessHeap(), 0,
                    sizeof(float) * channels);
        if (!session->channel_vols)
            return;

        for(i = session->channel_count; i < channels; ++i)
            session->channel_vols[i] = 1.f;

        session->channel_count = channels;
    }
}

static AudioSession *create_session(const GUID *guid, IMMDevice *device,
        UINT num_channels)
{
    AudioSession *ret;

    ret = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(AudioSession));
    if (!ret)
        return NULL;

    memcpy(&ret->guid, guid, sizeof(GUID));

    ret->device = device;

    list_init(&ret->clients);

    list_add_head(&g_sessions, &ret->entry);

    session_init_vols(ret, num_channels);

    ret->master_vol = 1.f;

    return ret;
}

/* if channels == 0, then this will return or create a session with
 * matching dataflow and GUID. otherwise, channels must also match */
static HRESULT get_audio_session(const GUID *sessionguid,
        IMMDevice *device, UINT channels, AudioSession **out)
{
    AudioSession *session;

    if (!sessionguid || IsEqualGUID(sessionguid, &GUID_NULL)) {
        *out = create_session(&GUID_NULL, device, channels);
        if (!*out)
            return E_OUTOFMEMORY;

        return S_OK;
    }

    *out = NULL;
    LIST_FOR_EACH_ENTRY(session, &g_sessions, AudioSession, entry) {
        if (session->device == device &&
            IsEqualGUID(sessionguid, &session->guid)) {
            session_init_vols(session, channels);
            *out = session;
            break;
        }
    }

    if (!*out) {
        *out = create_session(sessionguid, device, channels);
        if (!*out)
            return E_OUTOFMEMORY;
    }

    return S_OK;
}

static HRESULT pulse_spec_from_waveformat(ACImpl *This, const WAVEFORMATEX *fmt)
{
    pa_channel_map_init(&This->map);
    This->ss.rate = fmt->nSamplesPerSec;
    This->ss.format = PA_SAMPLE_INVALID;

    switch(fmt->wFormatTag) {
    case WAVE_FORMAT_IEEE_FLOAT:
        if (!fmt->nChannels || fmt->nChannels > 2 || fmt->wBitsPerSample != 32)
            break;
        This->ss.format = PA_SAMPLE_FLOAT32LE;
        pa_channel_map_init_auto(&This->map, fmt->nChannels, PA_CHANNEL_MAP_ALSA);
        break;
    case WAVE_FORMAT_PCM:
        if (!fmt->nChannels || fmt->nChannels > 2)
            break;
        if (fmt->wBitsPerSample == 8)
            This->ss.format = PA_SAMPLE_U8;
        else if (fmt->wBitsPerSample == 16)
            This->ss.format = PA_SAMPLE_S16LE;
        else
            return AUDCLNT_E_UNSUPPORTED_FORMAT;
        pa_channel_map_init_auto(&This->map, fmt->nChannels, PA_CHANNEL_MAP_ALSA);
        break;
    case WAVE_FORMAT_EXTENSIBLE: {
        WAVEFORMATEXTENSIBLE *wfe = (WAVEFORMATEXTENSIBLE*)fmt;
        DWORD mask = wfe->dwChannelMask;
        DWORD i = 0, j;
        if (fmt->cbSize != (sizeof(*wfe) - sizeof(*fmt)) && fmt->cbSize != sizeof(*wfe))
            break;
        if (IsEqualGUID(&wfe->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) &&
            (!wfe->Samples.wValidBitsPerSample || wfe->Samples.wValidBitsPerSample == 32) &&
            fmt->wBitsPerSample == 32)
            This->ss.format = PA_SAMPLE_FLOAT32LE;
        else if (IsEqualGUID(&wfe->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM)) {
            DWORD valid = wfe->Samples.wValidBitsPerSample;
            if (!valid)
                valid = fmt->wBitsPerSample;
            if (!valid || valid > fmt->wBitsPerSample)
                break;
            switch (fmt->wBitsPerSample) {
                case 8:
                    if (valid == 8)
                        This->ss.format = PA_SAMPLE_U8;
                    break;
                case 16:
                    if (valid == 16)
                        This->ss.format = PA_SAMPLE_S16LE;
                    break;
                case 24:
                    if (valid == 24)
                        This->ss.format = PA_SAMPLE_S24LE;
                    break;
                case 32:
                    if (valid == 24)
                        This->ss.format = PA_SAMPLE_S24_32LE;
                    else if (valid == 32)
                        This->ss.format = PA_SAMPLE_S32LE;
                    break;
                default:
                    return AUDCLNT_E_UNSUPPORTED_FORMAT;
            }
        }
        This->map.channels = fmt->nChannels;
        if (!mask || (mask & (SPEAKER_ALL|SPEAKER_RESERVED)))
            mask = get_channel_mask(fmt->nChannels);
        for (j = 0; j < sizeof(pulse_pos_from_wfx)/sizeof(*pulse_pos_from_wfx) && i < fmt->nChannels; ++j) {
            if (mask & (1 << j))
                This->map.map[i++] = pulse_pos_from_wfx[j];
        }

        /* Special case for mono since pulse appears to map it differently */
        if (mask == SPEAKER_FRONT_CENTER)
            This->map.map[0] = PA_CHANNEL_POSITION_MONO;

        if (i < fmt->nChannels || (mask & SPEAKER_RESERVED)) {
            This->map.channels = 0;
            ERR("Invalid channel mask: %i/%i and %x(%x)\n", i, fmt->nChannels, mask, wfe->dwChannelMask);
            break;
        }
        break;
        }
    case WAVE_FORMAT_ALAW:
    case WAVE_FORMAT_MULAW:
        if (fmt->wBitsPerSample != 8) {
            FIXME("Unsupported bpp %u for LAW\n", fmt->wBitsPerSample);
            return AUDCLNT_E_UNSUPPORTED_FORMAT;
        }
        if (fmt->nChannels != 1 && fmt->nChannels != 2) {
            FIXME("Unsupported channels %u for LAW\n", fmt->nChannels);
            return AUDCLNT_E_UNSUPPORTED_FORMAT;
        }
        This->ss.format = fmt->wFormatTag == WAVE_FORMAT_MULAW ? PA_SAMPLE_ULAW : PA_SAMPLE_ALAW;
        pa_channel_map_init_auto(&This->map, fmt->nChannels, PA_CHANNEL_MAP_ALSA);
        break;
    default:
        WARN("Unhandled tag %x\n", fmt->wFormatTag);
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }
    This->ss.channels = This->map.channels;
    if (!pa_channel_map_valid(&This->map) || This->ss.format == PA_SAMPLE_INVALID) {
        ERR("Invalid format! Channel spec valid: %i, format: %i\n", pa_channel_map_valid(&This->map), This->ss.format);
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }
    return S_OK;
}

static HRESULT WINAPI AudioClient_Initialize(IAudioClient *iface,
        AUDCLNT_SHAREMODE mode, DWORD flags, REFERENCE_TIME duration,
        REFERENCE_TIME period, const WAVEFORMATEX *fmt,
        const GUID *sessionguid)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    HRESULT hr = S_OK;
    UINT period_bytes;

    TRACE("(%p)->(%x, %x, %s, %s, %p, %s)\n", This, mode, flags,
          wine_dbgstr_longlong(duration), wine_dbgstr_longlong(period), fmt, debugstr_guid(sessionguid));

    if (!fmt)
        return E_POINTER;

    if (mode != AUDCLNT_SHAREMODE_SHARED && mode != AUDCLNT_SHAREMODE_EXCLUSIVE)
        return AUDCLNT_E_NOT_INITIALIZED;
    if (mode == AUDCLNT_SHAREMODE_EXCLUSIVE)
        return AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED;

    if (flags & ~(AUDCLNT_STREAMFLAGS_CROSSPROCESS |
                AUDCLNT_STREAMFLAGS_LOOPBACK |
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                AUDCLNT_STREAMFLAGS_NOPERSIST |
                AUDCLNT_STREAMFLAGS_RATEADJUST |
                AUDCLNT_SESSIONFLAGS_EXPIREWHENUNOWNED |
                AUDCLNT_SESSIONFLAGS_DISPLAY_HIDE |
                AUDCLNT_SESSIONFLAGS_DISPLAY_HIDEWHENEXPIRED)) {
        TRACE("Unknown flags: %08x\n", flags);
        return E_INVALIDARG;
    }

    pthread_mutex_lock(&pulse_lock);

    hr = pulse_connect();
    if (FAILED(hr)) {
        pthread_mutex_unlock(&pulse_lock);
        return hr;
    }

    if (This->stream) {
        pthread_mutex_unlock(&pulse_lock);
        return AUDCLNT_E_ALREADY_INITIALIZED;
    }

    hr = pulse_spec_from_waveformat(This, fmt);
    TRACE("Obtaining format returns %08x\n", hr);
    dump_fmt(fmt);

    if (FAILED(hr))
        goto exit;

    if (mode == AUDCLNT_SHAREMODE_SHARED) {
        REFERENCE_TIME def = pulse_def_period[This->dataflow == eCapture];
        REFERENCE_TIME min = pulse_min_period[This->dataflow == eCapture];

        /* Switch to low latency mode if below 2 default periods,
         * which is 20 ms by default, this will increase the amount
         * of interrupts but allows very low latency. In dsound I
         * managed to get a total latency of ~8ms, which is well below
         * default
         */
        if (duration < 2 * def)
            period = min;
        else
            period = def;
        if (duration < 2 * period)
            duration = 2 * period;

        /* Uh oh, really low latency requested.. */
        if (duration <= 2 * period)
            period /= 2;
    }
    period_bytes = pa_frame_size(&This->ss) * MulDiv(period, This->ss.rate, 10000000);

    if (duration < 20000000)
        This->bufsize_frames = ceil((duration / 10000000.) * fmt->nSamplesPerSec);
    else
        This->bufsize_frames = 2 * fmt->nSamplesPerSec;
    This->bufsize_bytes = This->bufsize_frames * pa_frame_size(&This->ss);

    This->share = mode;
    This->flags = flags;
    hr = pulse_stream_connect(This, period_bytes);
    if (SUCCEEDED(hr)) {
        UINT32 unalign;
        const pa_buffer_attr *attr = pa_stream_get_buffer_attr(This->stream);
        This->attr = *attr;
        /* Update frames according to new size */
        dump_attr(attr);
        if (This->dataflow == eRender) {
            if (attr->tlength < This->bufsize_bytes) {
                TRACE("PulseAudio buffer too small (%u < %u), using tmp buffer\n", attr->tlength, This->bufsize_bytes);

                This->local_buffer = HeapAlloc(GetProcessHeap(), 0, This->bufsize_bytes);
                if(!This->local_buffer)
                    hr = E_OUTOFMEMORY;
            }
        } else {
            UINT32 i, capture_packets;

            This->capture_period = period_bytes = attr->fragsize;
            if ((unalign = This->bufsize_bytes % period_bytes))
                This->bufsize_bytes += period_bytes - unalign;
            This->bufsize_frames = This->bufsize_bytes / pa_frame_size(&This->ss);

            capture_packets = This->bufsize_bytes / This->capture_period;

            This->local_buffer = HeapAlloc(GetProcessHeap(), 0, This->bufsize_bytes + capture_packets * sizeof(ACPacket));
            if (!This->local_buffer)
                hr = E_OUTOFMEMORY;
            else {
                ACPacket *cur_packet = (ACPacket*)((char*)This->local_buffer + This->bufsize_bytes);
                BYTE *data = This->local_buffer;
                silence_buffer(This->ss.format, This->local_buffer, This->bufsize_bytes);
                list_init(&This->packet_free_head);
                list_init(&This->packet_filled_head);
                for (i = 0; i < capture_packets; ++i, ++cur_packet) {
                    list_add_tail(&This->packet_free_head, &cur_packet->entry);
                    cur_packet->data = data;
                    data += This->capture_period;
                }
                assert(!This->capture_period || This->bufsize_bytes == This->capture_period * capture_packets);
                assert(!capture_packets || data - This->bufsize_bytes == This->local_buffer);
            }
        }
    }
    if (SUCCEEDED(hr))
        hr = get_audio_session(sessionguid, This->parent, fmt->nChannels, &This->session);
    if (SUCCEEDED(hr))
        list_add_tail(&This->session->clients, &This->entry);

exit:
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, This->local_buffer);
        This->local_buffer = NULL;
        if (This->stream) {
            pa_stream_disconnect(This->stream);
            pa_stream_unref(This->stream);
            This->stream = NULL;
        }
    }
    pthread_mutex_unlock(&pulse_lock);
    return hr;
}

static HRESULT WINAPI AudioClient_GetBufferSize(IAudioClient *iface,
        UINT32 *out)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, out);

    if (!out)
        return E_POINTER;

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (SUCCEEDED(hr))
        *out = This->bufsize_frames;
    pthread_mutex_unlock(&pulse_lock);

    return hr;
}

static HRESULT WINAPI AudioClient_GetStreamLatency(IAudioClient *iface,
        REFERENCE_TIME *latency)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    const pa_buffer_attr *attr;
    REFERENCE_TIME lat;
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, latency);

    if (!latency)
        return E_POINTER;

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (FAILED(hr)) {
        pthread_mutex_unlock(&pulse_lock);
        return hr;
    }
    attr = pa_stream_get_buffer_attr(This->stream);
    if (This->dataflow == eRender){
        lat = attr->minreq / pa_frame_size(&This->ss);
        lat += pulse_def_period[0];
    }else
        lat = attr->fragsize / pa_frame_size(&This->ss);
    *latency = 10000000;
    *latency *= lat;
    *latency /= This->ss.rate;
    pthread_mutex_unlock(&pulse_lock);
    TRACE("Latency: %u ms\n", (DWORD)(*latency / 10000));
    return S_OK;
}

static void ACImpl_GetRenderPad(ACImpl *This, UINT32 *out)
{
    *out = This->pad / pa_frame_size(&This->ss);
}

static void ACImpl_GetCapturePad(ACImpl *This, UINT32 *out)
{
    ACPacket *packet = This->locked_ptr;
    if (!packet && !list_empty(&This->packet_filled_head)) {
        packet = (ACPacket*)list_head(&This->packet_filled_head);
        This->locked_ptr = packet;
        list_remove(&packet->entry);
    }
    if (out)
        *out = This->pad / pa_frame_size(&This->ss);
}

static HRESULT WINAPI AudioClient_GetCurrentPadding(IAudioClient *iface,
        UINT32 *out)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, out);

    if (!out)
        return E_POINTER;

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (FAILED(hr)) {
        pthread_mutex_unlock(&pulse_lock);
        return hr;
    }

    if (This->dataflow == eRender)
        ACImpl_GetRenderPad(This, out);
    else
        ACImpl_GetCapturePad(This, out);
    pthread_mutex_unlock(&pulse_lock);

    TRACE("%p Pad: %u ms (%u)\n", This, MulDiv(*out, 1000, This->ss.rate), *out);
    return S_OK;
}

static HRESULT WINAPI AudioClient_IsFormatSupported(IAudioClient *iface,
        AUDCLNT_SHAREMODE mode, const WAVEFORMATEX *fmt,
        WAVEFORMATEX **out)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    HRESULT hr = S_OK;
    WAVEFORMATEX *closest = NULL;
    BOOL exclusive;

    TRACE("(%p)->(%x, %p, %p)\n", This, mode, fmt, out);

    if (!fmt)
        return E_POINTER;

    if (out)
        *out = NULL;

    if (mode == AUDCLNT_SHAREMODE_EXCLUSIVE) {
        exclusive = 1;
        out = NULL;
    } else if (mode == AUDCLNT_SHAREMODE_SHARED) {
        exclusive = 0;
        if (!out)
            return E_POINTER;
    } else
        return E_INVALIDARG;

    if (fmt->nChannels == 0)
        return AUDCLNT_E_UNSUPPORTED_FORMAT;

    closest = clone_format(fmt);
    if (!closest)
        return E_OUTOFMEMORY;

    dump_fmt(fmt);

    switch (fmt->wFormatTag) {
    case WAVE_FORMAT_EXTENSIBLE: {
        WAVEFORMATEXTENSIBLE *ext = (WAVEFORMATEXTENSIBLE*)closest;

        if ((fmt->cbSize != sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX) &&
             fmt->cbSize != sizeof(WAVEFORMATEXTENSIBLE)) ||
            fmt->nBlockAlign != fmt->wBitsPerSample / 8 * fmt->nChannels ||
            ext->Samples.wValidBitsPerSample > fmt->wBitsPerSample ||
            fmt->nAvgBytesPerSec != fmt->nBlockAlign * fmt->nSamplesPerSec) {
            hr = E_INVALIDARG;
            break;
        }

        if (exclusive) {
            UINT32 mask = 0, i, channels = 0;

            if (!(ext->dwChannelMask & (SPEAKER_ALL | SPEAKER_RESERVED))) {
                for (i = 1; !(i & SPEAKER_RESERVED); i <<= 1) {
                    if (i & ext->dwChannelMask) {
                        mask |= i;
                        channels++;
                    }
                }

                if (channels != fmt->nChannels || (ext->dwChannelMask & ~mask)) {
                    hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
                    break;
                }
            } else {
                hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
                break;
            }
        }

        if (IsEqualGUID(&ext->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            if (fmt->wBitsPerSample != 32) {
                hr = E_INVALIDARG;
                break;
            }

            if (ext->Samples.wValidBitsPerSample != fmt->wBitsPerSample) {
                hr = S_FALSE;
                ext->Samples.wValidBitsPerSample = fmt->wBitsPerSample;
            }
        } else if (IsEqualGUID(&ext->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM)) {
            if (!fmt->wBitsPerSample || fmt->wBitsPerSample > 32 || fmt->wBitsPerSample % 8) {
                hr = E_INVALIDARG;
                break;
            }

            if (ext->Samples.wValidBitsPerSample != fmt->wBitsPerSample &&
                !(fmt->wBitsPerSample == 32 &&
                  ext->Samples.wValidBitsPerSample == 24)) {
                hr = S_FALSE;
                ext->Samples.wValidBitsPerSample = fmt->wBitsPerSample;
                break;
            }
        } else {
            hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
            break;
        }

        break;
    }

    case WAVE_FORMAT_ALAW:
    case WAVE_FORMAT_MULAW:
        if (fmt->wBitsPerSample != 8) {
            hr = E_INVALIDARG;
            break;
        }
        /* Fall-through */
    case WAVE_FORMAT_IEEE_FLOAT:
        if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && fmt->wBitsPerSample != 32) {
            hr = E_INVALIDARG;
            break;
        }
        /* Fall-through */
    case WAVE_FORMAT_PCM:
        if (fmt->wFormatTag == WAVE_FORMAT_PCM &&
            (!fmt->wBitsPerSample || fmt->wBitsPerSample > 32 || fmt->wBitsPerSample % 8)) {
            hr = E_INVALIDARG;
            break;
        }

        if (fmt->nChannels > 2) {
            hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
            break;
        }
        /*
         * fmt->cbSize, fmt->nBlockAlign and fmt->nAvgBytesPerSec seem to be
         * ignored, invalid values are happily accepted.
         */
        break;
    default:
        hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
        break;
    }

    if (exclusive && hr != S_OK) {
        hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
        CoTaskMemFree(closest);
    } else if (hr != S_FALSE)
        CoTaskMemFree(closest);
    else
        *out = closest;

    /* Winepulse does not currently support exclusive mode, if you know of an
     * application that uses it, I will correct this..
     */
    if (hr == S_OK && exclusive)
        return This->dataflow == eCapture ? AUDCLNT_E_UNSUPPORTED_FORMAT : AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED;

    TRACE("returning: %08x %p\n", hr, out ? *out : NULL);
    return hr;
}

static HRESULT WINAPI AudioClient_GetMixFormat(IAudioClient *iface,
        WAVEFORMATEX **pwfx)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    WAVEFORMATEXTENSIBLE *fmt = &pulse_fmt[This->dataflow == eCapture];

    TRACE("(%p)->(%p)\n", This, pwfx);

    if (!pwfx)
        return E_POINTER;

    *pwfx = clone_format(&fmt->Format);
    if (!*pwfx)
        return E_OUTOFMEMORY;
    dump_fmt(*pwfx);
    return S_OK;
}

static HRESULT WINAPI AudioClient_GetDevicePeriod(IAudioClient *iface,
        REFERENCE_TIME *defperiod, REFERENCE_TIME *minperiod)
{
    ACImpl *This = impl_from_IAudioClient(iface);

    TRACE("(%p)->(%p, %p)\n", This, defperiod, minperiod);

    if (!defperiod && !minperiod)
        return E_POINTER;

    if (defperiod)
        *defperiod = pulse_def_period[This->dataflow == eCapture];
    if (minperiod)
        *minperiod = pulse_min_period[This->dataflow == eCapture];

    return S_OK;
}

static HRESULT WINAPI AudioClient_Start(IAudioClient *iface)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    HRESULT hr = S_OK;
    int success;
    pa_operation *o;

    TRACE("(%p)\n", This);

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (FAILED(hr)) {
        pthread_mutex_unlock(&pulse_lock);
        return hr;
    }

    if ((This->flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK) && !This->event) {
        pthread_mutex_unlock(&pulse_lock);
        return AUDCLNT_E_EVENTHANDLE_NOT_SET;
    }

    if (This->started) {
        pthread_mutex_unlock(&pulse_lock);
        return AUDCLNT_E_NOT_STOPPED;
    }

    if (pa_stream_is_corked(This->stream)) {
        o = pa_stream_cork(This->stream, 0, pulse_op_cb, &success);
        if (o) {
            while(pa_operation_get_state(o) == PA_OPERATION_RUNNING)
                pthread_cond_wait(&pulse_cond, &pulse_lock);
            pa_operation_unref(o);
        } else
            success = 0;
        if (!success)
            hr = E_FAIL;
    }

    if (SUCCEEDED(hr)) {
        This->started = TRUE;
        if (This->dataflow == eRender && This->event)
            pa_stream_set_latency_update_callback(This->stream, pulse_latency_callback, This);
    }
    pthread_mutex_unlock(&pulse_lock);
    return hr;
}

static HRESULT WINAPI AudioClient_Stop(IAudioClient *iface)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    HRESULT hr = S_OK;
    pa_operation *o;
    int success;

    TRACE("(%p)\n", This);

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (FAILED(hr)) {
        pthread_mutex_unlock(&pulse_lock);
        return hr;
    }

    if (!This->started) {
        pthread_mutex_unlock(&pulse_lock);
        return S_FALSE;
    }

    if (This->dataflow == eRender) {
        o = pa_stream_cork(This->stream, 1, pulse_op_cb, &success);
        if (o) {
            while(pa_operation_get_state(o) == PA_OPERATION_RUNNING)
                pthread_cond_wait(&pulse_cond, &pulse_lock);
            pa_operation_unref(o);
        } else
            success = 0;
        if (!success)
            hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) {
        This->started = FALSE;
    }
    pthread_mutex_unlock(&pulse_lock);
    return hr;
}

static HRESULT WINAPI AudioClient_Reset(IAudioClient *iface)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)\n", This);

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (FAILED(hr)) {
        pthread_mutex_unlock(&pulse_lock);
        return hr;
    }

    if (This->started) {
        pthread_mutex_unlock(&pulse_lock);
        return AUDCLNT_E_NOT_STOPPED;
    }

    if (This->locked) {
        pthread_mutex_unlock(&pulse_lock);
        return AUDCLNT_E_BUFFER_OPERATION_PENDING;
    }

    if (This->dataflow == eRender) {
        /* If there is still data in the render buffer it needs to be removed from the server */
        int success = 0;
        if (This->pad) {
            pa_operation *o = pa_stream_flush(This->stream, pulse_op_cb, &success);
            if (o) {
                while(pa_operation_get_state(o) == PA_OPERATION_RUNNING)
                    pthread_cond_wait(&pulse_cond, &pulse_lock);
                pa_operation_unref(o);
            }
        }
        if (success || !This->pad){
            This->clock_lastpos = This->clock_written = This->pad = 0;
            This->wri_offs_bytes = This->lcl_offs_bytes = This->held_bytes = 0;
        }
    } else {
        ACPacket *p;
        This->clock_written += This->pad;
        This->pad = 0;

        if ((p = This->locked_ptr)) {
            This->locked_ptr = NULL;
            list_add_tail(&This->packet_free_head, &p->entry);
        }
        list_move_tail(&This->packet_free_head, &This->packet_filled_head);
    }
    pthread_mutex_unlock(&pulse_lock);

    return hr;
}

static HRESULT WINAPI AudioClient_SetEventHandle(IAudioClient *iface,
        HANDLE event)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, event);

    if (!event)
        return E_INVALIDARG;

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (FAILED(hr)) {
        pthread_mutex_unlock(&pulse_lock);
        return hr;
    }

    if (!(This->flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK))
        hr = AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED;
    else if (This->event)
        hr = HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
    else
        This->event = event;
    pthread_mutex_unlock(&pulse_lock);
    return hr;
}

static HRESULT WINAPI AudioClient_GetService(IAudioClient *iface, REFIID riid,
        void **ppv)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    HRESULT hr;

    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    pthread_mutex_unlock(&pulse_lock);
    if (FAILED(hr))
        return hr;

    if (IsEqualIID(riid, &IID_IAudioRenderClient)) {
        if (This->dataflow != eRender)
            return AUDCLNT_E_WRONG_ENDPOINT_TYPE;
        *ppv = &This->IAudioRenderClient_iface;
    } else if (IsEqualIID(riid, &IID_IAudioCaptureClient)) {
        if (This->dataflow != eCapture)
            return AUDCLNT_E_WRONG_ENDPOINT_TYPE;
        *ppv = &This->IAudioCaptureClient_iface;
    } else if (IsEqualIID(riid, &IID_IAudioClock)) {
        *ppv = &This->IAudioClock_iface;
    } else if (IsEqualIID(riid, &IID_IAudioStreamVolume)) {
        *ppv = &This->IAudioStreamVolume_iface;
    } else if (IsEqualIID(riid, &IID_IAudioSessionControl) ||
               IsEqualIID(riid, &IID_IChannelAudioVolume) ||
               IsEqualIID(riid, &IID_ISimpleAudioVolume)) {
        if (!This->session_wrapper) {
            This->session_wrapper = AudioSessionWrapper_Create(This);
            if (!This->session_wrapper)
                return E_OUTOFMEMORY;
        }
        if (IsEqualIID(riid, &IID_IAudioSessionControl))
            *ppv = &This->session_wrapper->IAudioSessionControl2_iface;
        else if (IsEqualIID(riid, &IID_IChannelAudioVolume))
            *ppv = &This->session_wrapper->IChannelAudioVolume_iface;
        else if (IsEqualIID(riid, &IID_ISimpleAudioVolume))
            *ppv = &This->session_wrapper->ISimpleAudioVolume_iface;
    }

    if (*ppv) {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    FIXME("stub %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static const IAudioClientVtbl AudioClient_Vtbl =
{
    AudioClient_QueryInterface,
    AudioClient_AddRef,
    AudioClient_Release,
    AudioClient_Initialize,
    AudioClient_GetBufferSize,
    AudioClient_GetStreamLatency,
    AudioClient_GetCurrentPadding,
    AudioClient_IsFormatSupported,
    AudioClient_GetMixFormat,
    AudioClient_GetDevicePeriod,
    AudioClient_Start,
    AudioClient_Stop,
    AudioClient_Reset,
    AudioClient_SetEventHandle,
    AudioClient_GetService
};

static HRESULT WINAPI AudioRenderClient_QueryInterface(
        IAudioRenderClient *iface, REFIID riid, void **ppv)
{
    ACImpl *This = impl_from_IAudioRenderClient(iface);
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IAudioRenderClient))
        *ppv = iface;
    if (*ppv) {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    if (IsEqualIID(riid, &IID_IMarshal))
        return IUnknown_QueryInterface(This->marshal, riid, ppv);

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioRenderClient_AddRef(IAudioRenderClient *iface)
{
    ACImpl *This = impl_from_IAudioRenderClient(iface);
    return AudioClient_AddRef(&This->IAudioClient_iface);
}

static ULONG WINAPI AudioRenderClient_Release(IAudioRenderClient *iface)
{
    ACImpl *This = impl_from_IAudioRenderClient(iface);
    return AudioClient_Release(&This->IAudioClient_iface);
}

static void alloc_tmp_buffer(ACImpl *This, UINT32 bytes)
{
    if(This->tmp_buffer_bytes >= bytes)
        return;

    HeapFree(GetProcessHeap(), 0, This->tmp_buffer);
    This->tmp_buffer = HeapAlloc(GetProcessHeap(), 0, bytes);
    This->tmp_buffer_bytes = bytes;
}

static HRESULT WINAPI AudioRenderClient_GetBuffer(IAudioRenderClient *iface,
        UINT32 frames, BYTE **data)
{
    ACImpl *This = impl_from_IAudioRenderClient(iface);
    size_t avail, req, bytes = frames * pa_frame_size(&This->ss);
    UINT32 pad;
    HRESULT hr = S_OK;
    int ret = -1;

    TRACE("(%p)->(%u, %p)\n", This, frames, data);

    if (!data)
        return E_POINTER;
    *data = NULL;

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (FAILED(hr) || This->locked) {
        pthread_mutex_unlock(&pulse_lock);
        return FAILED(hr) ? hr : AUDCLNT_E_OUT_OF_ORDER;
    }
    if (!frames) {
        pthread_mutex_unlock(&pulse_lock);
        return S_OK;
    }

    ACImpl_GetRenderPad(This, &pad);
    avail = This->bufsize_frames - pad;
    if (avail < frames || bytes > This->bufsize_bytes) {
        pthread_mutex_unlock(&pulse_lock);
        WARN("Wanted to write %u, but only %zu available\n", frames, avail);
        return AUDCLNT_E_BUFFER_TOO_LARGE;
    }

    if(This->local_buffer){
        if(This->wri_offs_bytes + bytes > This->bufsize_bytes){
            alloc_tmp_buffer(This, bytes);
            *data = This->tmp_buffer;
            This->locked = -frames;
        }else{
            *data = This->local_buffer + This->wri_offs_bytes;
            This->locked = frames;
        }
    }else{
        req = bytes;
        ret = pa_stream_begin_write(This->stream, &This->locked_ptr, &req);
        if (ret < 0 || req < bytes) {
            FIXME("%p Not using pulse locked data: %i %zu/%u %u/%u\n", This, ret, req/pa_frame_size(&This->ss), frames, pad, This->bufsize_frames);
            if (ret >= 0)
                pa_stream_cancel_write(This->stream);
            alloc_tmp_buffer(This, bytes);
            *data = This->tmp_buffer;
            This->locked_ptr = NULL;
        } else
            *data = This->locked_ptr;

        This->locked = frames;
    }

    silence_buffer(This->ss.format, *data, bytes);

    pthread_mutex_unlock(&pulse_lock);

    return hr;
}

static void pulse_wrap_buffer(ACImpl *This, BYTE *buffer, UINT32 written_bytes)
{
    UINT32 chunk_bytes = This->bufsize_bytes - This->wri_offs_bytes;

    if(written_bytes <= chunk_bytes){
        memcpy(This->local_buffer + This->wri_offs_bytes, buffer, written_bytes);
    }else{
        memcpy(This->local_buffer + This->wri_offs_bytes, buffer, chunk_bytes);
        memcpy(This->local_buffer, buffer + chunk_bytes,
                written_bytes - chunk_bytes);
    }
}

static void pulse_free_noop(void *buf)
{
}

static HRESULT WINAPI AudioRenderClient_ReleaseBuffer(
        IAudioRenderClient *iface, UINT32 written_frames, DWORD flags)
{
    ACImpl *This = impl_from_IAudioRenderClient(iface);
    UINT32 written_bytes = written_frames * pa_frame_size(&This->ss);

    TRACE("(%p)->(%u, %x)\n", This, written_frames, flags);

    pthread_mutex_lock(&pulse_lock);
    if (!This->locked || !written_frames) {
        if (This->locked_ptr)
            pa_stream_cancel_write(This->stream);
        This->locked = 0;
        This->locked_ptr = NULL;
        pthread_mutex_unlock(&pulse_lock);
        return written_frames ? AUDCLNT_E_OUT_OF_ORDER : S_OK;
    }

    if (This->locked < written_frames) {
        pthread_mutex_unlock(&pulse_lock);
        return AUDCLNT_E_INVALID_SIZE;
    }

    if(This->local_buffer){
        BYTE *buffer;

        if(This->locked >= 0)
            buffer = This->local_buffer + This->wri_offs_bytes;
        else
            buffer = This->tmp_buffer;

        if(flags & AUDCLNT_BUFFERFLAGS_SILENT)
            silence_buffer(This->ss.format, buffer, written_bytes);

        if(This->locked < 0)
            pulse_wrap_buffer(This, buffer, written_bytes);

        This->wri_offs_bytes += written_bytes;
        This->wri_offs_bytes %= This->bufsize_bytes;

        This->pad += written_bytes;
        This->held_bytes += written_bytes;

        if(This->held_bytes == This->pad){
            int e;
            UINT32 to_write = min(This->attr.tlength, written_bytes);

            /* nothing in PA, so send data immediately */

            TRACE("pre-writing %u bytes\n", to_write);

            e = pa_stream_write(This->stream, buffer, to_write, NULL, 0, PA_SEEK_RELATIVE);
            if(e)
                ERR("pa_stream_write failed: 0x%x\n", e);

            This->lcl_offs_bytes += to_write;
            This->lcl_offs_bytes %= This->bufsize_bytes;
            This->held_bytes -= to_write;
        }

    }else{
        if (This->locked_ptr) {
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                silence_buffer(This->ss.format, This->locked_ptr, written_bytes);
            pa_stream_write(This->stream, This->locked_ptr, written_bytes, NULL, 0, PA_SEEK_RELATIVE);
        } else {
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                silence_buffer(This->ss.format, This->tmp_buffer, written_bytes);
            pa_stream_write(This->stream, This->tmp_buffer, written_bytes, pulse_free_noop, 0, PA_SEEK_RELATIVE);
        }
        This->pad += written_bytes;
    }

    if (!pa_stream_is_corked(This->stream)) {
        int success;
        pa_operation *o;
        o = pa_stream_trigger(This->stream, pulse_op_cb, &success);
        if (o) {
            while(pa_operation_get_state(o) == PA_OPERATION_RUNNING)
                pthread_cond_wait(&pulse_cond, &pulse_lock);
            pa_operation_unref(o);
        }
    }

    This->locked = 0;
    This->locked_ptr = NULL;
    TRACE("Released %u, pad %zu\n", written_frames, This->pad / pa_frame_size(&This->ss));
    assert(This->pad <= This->bufsize_bytes);

    pthread_mutex_unlock(&pulse_lock);
    return S_OK;
}

static const IAudioRenderClientVtbl AudioRenderClient_Vtbl = {
    AudioRenderClient_QueryInterface,
    AudioRenderClient_AddRef,
    AudioRenderClient_Release,
    AudioRenderClient_GetBuffer,
    AudioRenderClient_ReleaseBuffer
};

static HRESULT WINAPI AudioCaptureClient_QueryInterface(
        IAudioCaptureClient *iface, REFIID riid, void **ppv)
{
    ACImpl *This = impl_from_IAudioCaptureClient(iface);
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IAudioCaptureClient))
        *ppv = iface;
    if (*ppv) {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    if (IsEqualIID(riid, &IID_IMarshal))
        return IUnknown_QueryInterface(This->marshal, riid, ppv);

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioCaptureClient_AddRef(IAudioCaptureClient *iface)
{
    ACImpl *This = impl_from_IAudioCaptureClient(iface);
    return IAudioClient_AddRef(&This->IAudioClient_iface);
}

static ULONG WINAPI AudioCaptureClient_Release(IAudioCaptureClient *iface)
{
    ACImpl *This = impl_from_IAudioCaptureClient(iface);
    return IAudioClient_Release(&This->IAudioClient_iface);
}

static HRESULT WINAPI AudioCaptureClient_GetBuffer(IAudioCaptureClient *iface,
        BYTE **data, UINT32 *frames, DWORD *flags, UINT64 *devpos,
        UINT64 *qpcpos)
{
    ACImpl *This = impl_from_IAudioCaptureClient(iface);
    HRESULT hr;
    ACPacket *packet;

    TRACE("(%p)->(%p, %p, %p, %p, %p)\n", This, data, frames, flags,
            devpos, qpcpos);

    if (!data || !frames || !flags)
        return E_POINTER;

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (FAILED(hr) || This->locked) {
        pthread_mutex_unlock(&pulse_lock);
        return FAILED(hr) ? hr : AUDCLNT_E_OUT_OF_ORDER;
    }

    ACImpl_GetCapturePad(This, NULL);
    if ((packet = This->locked_ptr)) {
        *frames = This->capture_period / pa_frame_size(&This->ss);
        *flags = 0;
        if (packet->discont)
            *flags |= AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY;
        if (devpos) {
            if (packet->discont)
                *devpos = (This->clock_written + This->capture_period) / pa_frame_size(&This->ss);
            else
                *devpos = This->clock_written / pa_frame_size(&This->ss);
        }
        if (qpcpos)
            *qpcpos = packet->qpcpos;
        *data = packet->data;
    }
    else
        *frames = 0;
    This->locked = *frames;
    pthread_mutex_unlock(&pulse_lock);
    return *frames ? S_OK : AUDCLNT_S_BUFFER_EMPTY;
}

static HRESULT WINAPI AudioCaptureClient_ReleaseBuffer(
        IAudioCaptureClient *iface, UINT32 done)
{
    ACImpl *This = impl_from_IAudioCaptureClient(iface);

    TRACE("(%p)->(%u)\n", This, done);

    pthread_mutex_lock(&pulse_lock);
    if (!This->locked && done) {
        pthread_mutex_unlock(&pulse_lock);
        return AUDCLNT_E_OUT_OF_ORDER;
    }
    if (done && This->locked != done) {
        pthread_mutex_unlock(&pulse_lock);
        return AUDCLNT_E_INVALID_SIZE;
    }
    if (done) {
        ACPacket *packet = This->locked_ptr;
        This->locked_ptr = NULL;
        This->pad -= This->capture_period;
        if (packet->discont)
            This->clock_written += 2 * This->capture_period;
        else
            This->clock_written += This->capture_period;
        list_add_tail(&This->packet_free_head, &packet->entry);
    }
    This->locked = 0;
    pthread_mutex_unlock(&pulse_lock);
    return S_OK;
}

static HRESULT WINAPI AudioCaptureClient_GetNextPacketSize(
        IAudioCaptureClient *iface, UINT32 *frames)
{
    ACImpl *This = impl_from_IAudioCaptureClient(iface);

    TRACE("(%p)->(%p)\n", This, frames);
    if (!frames)
        return E_POINTER;

    pthread_mutex_lock(&pulse_lock);
    ACImpl_GetCapturePad(This, NULL);
    if (This->locked_ptr)
        *frames = This->capture_period / pa_frame_size(&This->ss);
    else
        *frames = 0;
    pthread_mutex_unlock(&pulse_lock);
    return S_OK;
}

static const IAudioCaptureClientVtbl AudioCaptureClient_Vtbl =
{
    AudioCaptureClient_QueryInterface,
    AudioCaptureClient_AddRef,
    AudioCaptureClient_Release,
    AudioCaptureClient_GetBuffer,
    AudioCaptureClient_ReleaseBuffer,
    AudioCaptureClient_GetNextPacketSize
};

static HRESULT WINAPI AudioClock_QueryInterface(IAudioClock *iface,
        REFIID riid, void **ppv)
{
    ACImpl *This = impl_from_IAudioClock(iface);

    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IAudioClock))
        *ppv = iface;
    else if (IsEqualIID(riid, &IID_IAudioClock2))
        *ppv = &This->IAudioClock2_iface;
    if (*ppv) {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    if (IsEqualIID(riid, &IID_IMarshal))
        return IUnknown_QueryInterface(This->marshal, riid, ppv);

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioClock_AddRef(IAudioClock *iface)
{
    ACImpl *This = impl_from_IAudioClock(iface);
    return IAudioClient_AddRef(&This->IAudioClient_iface);
}

static ULONG WINAPI AudioClock_Release(IAudioClock *iface)
{
    ACImpl *This = impl_from_IAudioClock(iface);
    return IAudioClient_Release(&This->IAudioClient_iface);
}

static HRESULT WINAPI AudioClock_GetFrequency(IAudioClock *iface, UINT64 *freq)
{
    ACImpl *This = impl_from_IAudioClock(iface);
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, freq);

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (SUCCEEDED(hr)) {
        *freq = This->ss.rate;
        if (This->share == AUDCLNT_SHAREMODE_SHARED)
            *freq *= pa_frame_size(&This->ss);
    }
    pthread_mutex_unlock(&pulse_lock);
    return hr;
}

static HRESULT WINAPI AudioClock_GetPosition(IAudioClock *iface, UINT64 *pos,
        UINT64 *qpctime)
{
    ACImpl *This = impl_from_IAudioClock(iface);
    HRESULT hr;

    TRACE("(%p)->(%p, %p)\n", This, pos, qpctime);

    if (!pos)
        return E_POINTER;

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (FAILED(hr)) {
        pthread_mutex_unlock(&pulse_lock);
        return hr;
    }

    *pos = This->clock_written;

    if (This->share == AUDCLNT_SHAREMODE_EXCLUSIVE)
        *pos /= pa_frame_size(&This->ss);

    /* Make time never go backwards */
    if (*pos < This->clock_lastpos)
        *pos = This->clock_lastpos;
    else
        This->clock_lastpos = *pos;
    pthread_mutex_unlock(&pulse_lock);

    TRACE("%p Position: %u\n", This, (unsigned)*pos);

    if (qpctime) {
        LARGE_INTEGER stamp, freq;
        QueryPerformanceCounter(&stamp);
        QueryPerformanceFrequency(&freq);
        *qpctime = (stamp.QuadPart * (INT64)10000000) / freq.QuadPart;
    }

    return S_OK;
}

static HRESULT WINAPI AudioClock_GetCharacteristics(IAudioClock *iface,
        DWORD *chars)
{
    ACImpl *This = impl_from_IAudioClock(iface);

    TRACE("(%p)->(%p)\n", This, chars);

    if (!chars)
        return E_POINTER;

    *chars = AUDIOCLOCK_CHARACTERISTIC_FIXED_FREQ;

    return S_OK;
}

static const IAudioClockVtbl AudioClock_Vtbl =
{
    AudioClock_QueryInterface,
    AudioClock_AddRef,
    AudioClock_Release,
    AudioClock_GetFrequency,
    AudioClock_GetPosition,
    AudioClock_GetCharacteristics
};

static HRESULT WINAPI AudioClock2_QueryInterface(IAudioClock2 *iface,
        REFIID riid, void **ppv)
{
    ACImpl *This = impl_from_IAudioClock2(iface);
    return IAudioClock_QueryInterface(&This->IAudioClock_iface, riid, ppv);
}

static ULONG WINAPI AudioClock2_AddRef(IAudioClock2 *iface)
{
    ACImpl *This = impl_from_IAudioClock2(iface);
    return IAudioClient_AddRef(&This->IAudioClient_iface);
}

static ULONG WINAPI AudioClock2_Release(IAudioClock2 *iface)
{
    ACImpl *This = impl_from_IAudioClock2(iface);
    return IAudioClient_Release(&This->IAudioClient_iface);
}

static HRESULT WINAPI AudioClock2_GetDevicePosition(IAudioClock2 *iface,
        UINT64 *pos, UINT64 *qpctime)
{
    ACImpl *This = impl_from_IAudioClock2(iface);
    HRESULT hr = AudioClock_GetPosition(&This->IAudioClock_iface, pos, qpctime);
    if (SUCCEEDED(hr) && This->share == AUDCLNT_SHAREMODE_SHARED)
        *pos /= pa_frame_size(&This->ss);
    return hr;
}

static const IAudioClock2Vtbl AudioClock2_Vtbl =
{
    AudioClock2_QueryInterface,
    AudioClock2_AddRef,
    AudioClock2_Release,
    AudioClock2_GetDevicePosition
};

static HRESULT WINAPI AudioStreamVolume_QueryInterface(
        IAudioStreamVolume *iface, REFIID riid, void **ppv)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);

    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IAudioStreamVolume))
        *ppv = iface;
    if (*ppv) {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    if (IsEqualIID(riid, &IID_IMarshal))
        return IUnknown_QueryInterface(This->marshal, riid, ppv);

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioStreamVolume_AddRef(IAudioStreamVolume *iface)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);
    return IAudioClient_AddRef(&This->IAudioClient_iface);
}

static ULONG WINAPI AudioStreamVolume_Release(IAudioStreamVolume *iface)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);
    return IAudioClient_Release(&This->IAudioClient_iface);
}

static HRESULT WINAPI AudioStreamVolume_GetChannelCount(
        IAudioStreamVolume *iface, UINT32 *out)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);

    TRACE("(%p)->(%p)\n", This, out);

    if (!out)
        return E_POINTER;

    *out = This->ss.channels;

    return S_OK;
}

struct pulse_info_cb_data {
    UINT32 n;
    float *levels;
};

static HRESULT WINAPI AudioStreamVolume_SetAllVolumes(
        IAudioStreamVolume *iface, UINT32 count, const float *levels)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);
    HRESULT hr;
    int i;

    TRACE("(%p)->(%d, %p)\n", This, count, levels);

    if (!levels)
        return E_POINTER;

    if (count != This->ss.channels)
        return E_INVALIDARG;

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (FAILED(hr))
        goto out;

    for (i = 0; i < count; ++i)
        This->vol[i] = levels[i];

out:
    pthread_mutex_unlock(&pulse_lock);
    return hr;
}

static HRESULT WINAPI AudioStreamVolume_GetAllVolumes(
        IAudioStreamVolume *iface, UINT32 count, float *levels)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);
    HRESULT hr;
    int i;

    TRACE("(%p)->(%d, %p)\n", This, count, levels);

    if (!levels)
        return E_POINTER;

    if (count != This->ss.channels)
        return E_INVALIDARG;

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (FAILED(hr))
        goto out;

    for (i = 0; i < count; ++i)
        levels[i] = This->vol[i];

out:
    pthread_mutex_unlock(&pulse_lock);
    return hr;
}

static HRESULT WINAPI AudioStreamVolume_SetChannelVolume(
        IAudioStreamVolume *iface, UINT32 index, float level)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);
    HRESULT hr;
    float volumes[PA_CHANNELS_MAX];

    TRACE("(%p)->(%d, %f)\n", This, index, level);

    if (level < 0.f || level > 1.f)
        return E_INVALIDARG;

    if (index >= This->ss.channels)
        return E_INVALIDARG;

    hr = AudioStreamVolume_GetAllVolumes(iface, This->ss.channels, volumes);
    volumes[index] = level;
    if (SUCCEEDED(hr))
        hr = AudioStreamVolume_SetAllVolumes(iface, This->ss.channels, volumes);
    return hr;
}

static HRESULT WINAPI AudioStreamVolume_GetChannelVolume(
        IAudioStreamVolume *iface, UINT32 index, float *level)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);
    float volumes[PA_CHANNELS_MAX];
    HRESULT hr;

    TRACE("(%p)->(%d, %p)\n", This, index, level);

    if (!level)
        return E_POINTER;

    if (index >= This->ss.channels)
        return E_INVALIDARG;

    hr = AudioStreamVolume_GetAllVolumes(iface, This->ss.channels, volumes);
    if (SUCCEEDED(hr))
        *level = volumes[index];
    return hr;
}

static const IAudioStreamVolumeVtbl AudioStreamVolume_Vtbl =
{
    AudioStreamVolume_QueryInterface,
    AudioStreamVolume_AddRef,
    AudioStreamVolume_Release,
    AudioStreamVolume_GetChannelCount,
    AudioStreamVolume_SetChannelVolume,
    AudioStreamVolume_GetChannelVolume,
    AudioStreamVolume_SetAllVolumes,
    AudioStreamVolume_GetAllVolumes
};

static AudioSessionWrapper *AudioSessionWrapper_Create(ACImpl *client)
{
    AudioSessionWrapper *ret;

    ret = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(AudioSessionWrapper));
    if (!ret)
        return NULL;

    ret->IAudioSessionControl2_iface.lpVtbl = &AudioSessionControl2_Vtbl;
    ret->ISimpleAudioVolume_iface.lpVtbl = &SimpleAudioVolume_Vtbl;
    ret->IChannelAudioVolume_iface.lpVtbl = &ChannelAudioVolume_Vtbl;

    ret->ref = !client;

    ret->client = client;
    if (client) {
        ret->session = client->session;
        AudioClient_AddRef(&client->IAudioClient_iface);
    }

    return ret;
}

static HRESULT WINAPI AudioSessionControl_QueryInterface(
        IAudioSessionControl2 *iface, REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IAudioSessionControl) ||
        IsEqualIID(riid, &IID_IAudioSessionControl2))
        *ppv = iface;
    if (*ppv) {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioSessionControl_AddRef(IAudioSessionControl2 *iface)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);
    ULONG ref;
    ref = InterlockedIncrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    return ref;
}

static ULONG WINAPI AudioSessionControl_Release(IAudioSessionControl2 *iface)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);
    ULONG ref;
    ref = InterlockedDecrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    if (!ref) {
        if (This->client) {
            This->client->session_wrapper = NULL;
            AudioClient_Release(&This->client->IAudioClient_iface);
        }
        HeapFree(GetProcessHeap(), 0, This);
    }
    return ref;
}

static HRESULT WINAPI AudioSessionControl_GetState(IAudioSessionControl2 *iface,
        AudioSessionState *state)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);
    ACImpl *client;

    TRACE("(%p)->(%p)\n", This, state);

    if (!state)
        return NULL_PTR_ERR;

    pthread_mutex_lock(&pulse_lock);
    if (list_empty(&This->session->clients)) {
        *state = AudioSessionStateExpired;
        goto out;
    }
    LIST_FOR_EACH_ENTRY(client, &This->session->clients, ACImpl, entry) {
        if (client->started) {
            *state = AudioSessionStateActive;
            goto out;
        }
    }
    *state = AudioSessionStateInactive;

out:
    pthread_mutex_unlock(&pulse_lock);
    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_GetDisplayName(
        IAudioSessionControl2 *iface, WCHAR **name)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, name);

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_SetDisplayName(
        IAudioSessionControl2 *iface, const WCHAR *name, const GUID *session)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p, %s) - stub\n", This, name, debugstr_guid(session));

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_GetIconPath(
        IAudioSessionControl2 *iface, WCHAR **path)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, path);

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_SetIconPath(
        IAudioSessionControl2 *iface, const WCHAR *path, const GUID *session)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p, %s) - stub\n", This, path, debugstr_guid(session));

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_GetGroupingParam(
        IAudioSessionControl2 *iface, GUID *group)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, group);

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_SetGroupingParam(
        IAudioSessionControl2 *iface, const GUID *group, const GUID *session)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%s, %s) - stub\n", This, debugstr_guid(group),
            debugstr_guid(session));

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_RegisterAudioSessionNotification(
        IAudioSessionControl2 *iface, IAudioSessionEvents *events)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, events);

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_UnregisterAudioSessionNotification(
        IAudioSessionControl2 *iface, IAudioSessionEvents *events)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, events);

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_GetSessionIdentifier(
        IAudioSessionControl2 *iface, WCHAR **id)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, id);

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_GetSessionInstanceIdentifier(
        IAudioSessionControl2 *iface, WCHAR **id)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, id);

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_GetProcessId(
        IAudioSessionControl2 *iface, DWORD *pid)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    TRACE("(%p)->(%p)\n", This, pid);

    if (!pid)
        return E_POINTER;

    *pid = GetCurrentProcessId();

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_IsSystemSoundsSession(
        IAudioSessionControl2 *iface)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    TRACE("(%p)\n", This);

    return S_FALSE;
}

static HRESULT WINAPI AudioSessionControl_SetDuckingPreference(
        IAudioSessionControl2 *iface, BOOL optout)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    TRACE("(%p)->(%d)\n", This, optout);

    return S_OK;
}

static const IAudioSessionControl2Vtbl AudioSessionControl2_Vtbl =
{
    AudioSessionControl_QueryInterface,
    AudioSessionControl_AddRef,
    AudioSessionControl_Release,
    AudioSessionControl_GetState,
    AudioSessionControl_GetDisplayName,
    AudioSessionControl_SetDisplayName,
    AudioSessionControl_GetIconPath,
    AudioSessionControl_SetIconPath,
    AudioSessionControl_GetGroupingParam,
    AudioSessionControl_SetGroupingParam,
    AudioSessionControl_RegisterAudioSessionNotification,
    AudioSessionControl_UnregisterAudioSessionNotification,
    AudioSessionControl_GetSessionIdentifier,
    AudioSessionControl_GetSessionInstanceIdentifier,
    AudioSessionControl_GetProcessId,
    AudioSessionControl_IsSystemSoundsSession,
    AudioSessionControl_SetDuckingPreference
};

typedef struct _SessionMgr {
    IAudioSessionManager2 IAudioSessionManager2_iface;

    LONG ref;

    IMMDevice *device;
} SessionMgr;

static HRESULT WINAPI AudioSessionManager_QueryInterface(IAudioSessionManager2 *iface,
        REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IAudioSessionManager) ||
        IsEqualIID(riid, &IID_IAudioSessionManager2))
        *ppv = iface;
    if (*ppv) {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static inline SessionMgr *impl_from_IAudioSessionManager2(IAudioSessionManager2 *iface)
{
    return CONTAINING_RECORD(iface, SessionMgr, IAudioSessionManager2_iface);
}

static ULONG WINAPI AudioSessionManager_AddRef(IAudioSessionManager2 *iface)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    ULONG ref;
    ref = InterlockedIncrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    return ref;
}

static ULONG WINAPI AudioSessionManager_Release(IAudioSessionManager2 *iface)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    ULONG ref;
    ref = InterlockedDecrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    if (!ref)
        HeapFree(GetProcessHeap(), 0, This);
    return ref;
}

static HRESULT WINAPI AudioSessionManager_GetAudioSessionControl(
        IAudioSessionManager2 *iface, const GUID *session_guid, DWORD flags,
        IAudioSessionControl **out)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    AudioSession *session;
    AudioSessionWrapper *wrapper;
    HRESULT hr;

    TRACE("(%p)->(%s, %x, %p)\n", This, debugstr_guid(session_guid),
            flags, out);

    hr = get_audio_session(session_guid, This->device, 0, &session);
    if (FAILED(hr))
        return hr;

    wrapper = AudioSessionWrapper_Create(NULL);
    if (!wrapper)
        return E_OUTOFMEMORY;

    wrapper->session = session;

    *out = (IAudioSessionControl*)&wrapper->IAudioSessionControl2_iface;

    return S_OK;
}

static HRESULT WINAPI AudioSessionManager_GetSimpleAudioVolume(
        IAudioSessionManager2 *iface, const GUID *session_guid, DWORD flags,
        ISimpleAudioVolume **out)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    AudioSession *session;
    AudioSessionWrapper *wrapper;
    HRESULT hr;

    TRACE("(%p)->(%s, %x, %p)\n", This, debugstr_guid(session_guid),
            flags, out);

    hr = get_audio_session(session_guid, This->device, 0, &session);
    if (FAILED(hr))
        return hr;

    wrapper = AudioSessionWrapper_Create(NULL);
    if (!wrapper)
        return E_OUTOFMEMORY;

    wrapper->session = session;

    *out = &wrapper->ISimpleAudioVolume_iface;

    return S_OK;
}

static HRESULT WINAPI AudioSessionManager_GetSessionEnumerator(
        IAudioSessionManager2 *iface, IAudioSessionEnumerator **out)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, out);
    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionManager_RegisterSessionNotification(
        IAudioSessionManager2 *iface, IAudioSessionNotification *notification)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, notification);
    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionManager_UnregisterSessionNotification(
        IAudioSessionManager2 *iface, IAudioSessionNotification *notification)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, notification);
    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionManager_RegisterDuckNotification(
        IAudioSessionManager2 *iface, const WCHAR *session_id,
        IAudioVolumeDuckNotification *notification)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, notification);
    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionManager_UnregisterDuckNotification(
        IAudioSessionManager2 *iface,
        IAudioVolumeDuckNotification *notification)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, notification);
    return E_NOTIMPL;
}

static const IAudioSessionManager2Vtbl AudioSessionManager2_Vtbl =
{
    AudioSessionManager_QueryInterface,
    AudioSessionManager_AddRef,
    AudioSessionManager_Release,
    AudioSessionManager_GetAudioSessionControl,
    AudioSessionManager_GetSimpleAudioVolume,
    AudioSessionManager_GetSessionEnumerator,
    AudioSessionManager_RegisterSessionNotification,
    AudioSessionManager_UnregisterSessionNotification,
    AudioSessionManager_RegisterDuckNotification,
    AudioSessionManager_UnregisterDuckNotification
};

static HRESULT WINAPI SimpleAudioVolume_QueryInterface(
        ISimpleAudioVolume *iface, REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_ISimpleAudioVolume))
        *ppv = iface;
    if (*ppv) {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI SimpleAudioVolume_AddRef(ISimpleAudioVolume *iface)
{
    AudioSessionWrapper *This = impl_from_ISimpleAudioVolume(iface);
    return AudioSessionControl_AddRef(&This->IAudioSessionControl2_iface);
}

static ULONG WINAPI SimpleAudioVolume_Release(ISimpleAudioVolume *iface)
{
    AudioSessionWrapper *This = impl_from_ISimpleAudioVolume(iface);
    return AudioSessionControl_Release(&This->IAudioSessionControl2_iface);
}

static HRESULT WINAPI SimpleAudioVolume_SetMasterVolume(
        ISimpleAudioVolume *iface, float level, const GUID *context)
{
    AudioSessionWrapper *This = impl_from_ISimpleAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%f, %s)\n", session, level, wine_dbgstr_guid(context));

    if (level < 0.f || level > 1.f)
        return E_INVALIDARG;

    if (context)
        FIXME("Notifications not supported yet\n");

    TRACE("PulseAudio does not support session volume control\n");

    pthread_mutex_lock(&pulse_lock);
    session->master_vol = level;
    pthread_mutex_unlock(&pulse_lock);

    return S_OK;
}

static HRESULT WINAPI SimpleAudioVolume_GetMasterVolume(
        ISimpleAudioVolume *iface, float *level)
{
    AudioSessionWrapper *This = impl_from_ISimpleAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%p)\n", session, level);

    if (!level)
        return NULL_PTR_ERR;

    *level = session->master_vol;

    return S_OK;
}

static HRESULT WINAPI SimpleAudioVolume_SetMute(ISimpleAudioVolume *iface,
        BOOL mute, const GUID *context)
{
    AudioSessionWrapper *This = impl_from_ISimpleAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%u, %s)\n", session, mute, debugstr_guid(context));

    if (context)
        FIXME("Notifications not supported yet\n");

    session->mute = mute;

    return S_OK;
}

static HRESULT WINAPI SimpleAudioVolume_GetMute(ISimpleAudioVolume *iface,
        BOOL *mute)
{
    AudioSessionWrapper *This = impl_from_ISimpleAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%p)\n", session, mute);

    if (!mute)
        return NULL_PTR_ERR;

    *mute = session->mute;

    return S_OK;
}

static const ISimpleAudioVolumeVtbl SimpleAudioVolume_Vtbl  =
{
    SimpleAudioVolume_QueryInterface,
    SimpleAudioVolume_AddRef,
    SimpleAudioVolume_Release,
    SimpleAudioVolume_SetMasterVolume,
    SimpleAudioVolume_GetMasterVolume,
    SimpleAudioVolume_SetMute,
    SimpleAudioVolume_GetMute
};

static HRESULT WINAPI ChannelAudioVolume_QueryInterface(
        IChannelAudioVolume *iface, REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IChannelAudioVolume))
        *ppv = iface;
    if (*ppv) {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI ChannelAudioVolume_AddRef(IChannelAudioVolume *iface)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    return AudioSessionControl_AddRef(&This->IAudioSessionControl2_iface);
}

static ULONG WINAPI ChannelAudioVolume_Release(IChannelAudioVolume *iface)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    return AudioSessionControl_Release(&This->IAudioSessionControl2_iface);
}

static HRESULT WINAPI ChannelAudioVolume_GetChannelCount(
        IChannelAudioVolume *iface, UINT32 *out)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%p)\n", session, out);

    if (!out)
        return NULL_PTR_ERR;

    *out = session->channel_count;

    return S_OK;
}

static HRESULT WINAPI ChannelAudioVolume_SetChannelVolume(
        IChannelAudioVolume *iface, UINT32 index, float level,
        const GUID *context)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%d, %f, %s)\n", session, index, level,
            wine_dbgstr_guid(context));

    if (level < 0.f || level > 1.f)
        return E_INVALIDARG;

    if (index >= session->channel_count)
        return E_INVALIDARG;

    if (context)
        FIXME("Notifications not supported yet\n");

    TRACE("PulseAudio does not support session volume control\n");

    pthread_mutex_lock(&pulse_lock);
    session->channel_vols[index] = level;
    pthread_mutex_unlock(&pulse_lock);

    return S_OK;
}

static HRESULT WINAPI ChannelAudioVolume_GetChannelVolume(
        IChannelAudioVolume *iface, UINT32 index, float *level)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%d, %p)\n", session, index, level);

    if (!level)
        return NULL_PTR_ERR;

    if (index >= session->channel_count)
        return E_INVALIDARG;

    *level = session->channel_vols[index];

    return S_OK;
}

static HRESULT WINAPI ChannelAudioVolume_SetAllVolumes(
        IChannelAudioVolume *iface, UINT32 count, const float *levels,
        const GUID *context)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    AudioSession *session = This->session;
    int i;

    TRACE("(%p)->(%d, %p, %s)\n", session, count, levels,
            wine_dbgstr_guid(context));

    if (!levels)
        return NULL_PTR_ERR;

    if (count != session->channel_count)
        return E_INVALIDARG;

    if (context)
        FIXME("Notifications not supported yet\n");

    TRACE("PulseAudio does not support session volume control\n");

    pthread_mutex_lock(&pulse_lock);
    for(i = 0; i < count; ++i)
        session->channel_vols[i] = levels[i];
    pthread_mutex_unlock(&pulse_lock);
    return S_OK;
}

static HRESULT WINAPI ChannelAudioVolume_GetAllVolumes(
        IChannelAudioVolume *iface, UINT32 count, float *levels)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    AudioSession *session = This->session;
    int i;

    TRACE("(%p)->(%d, %p)\n", session, count, levels);

    if (!levels)
        return NULL_PTR_ERR;

    if (count != session->channel_count)
        return E_INVALIDARG;

    for(i = 0; i < count; ++i)
        levels[i] = session->channel_vols[i];

    return S_OK;
}

static const IChannelAudioVolumeVtbl ChannelAudioVolume_Vtbl =
{
    ChannelAudioVolume_QueryInterface,
    ChannelAudioVolume_AddRef,
    ChannelAudioVolume_Release,
    ChannelAudioVolume_GetChannelCount,
    ChannelAudioVolume_SetChannelVolume,
    ChannelAudioVolume_GetChannelVolume,
    ChannelAudioVolume_SetAllVolumes,
    ChannelAudioVolume_GetAllVolumes
};

HRESULT WINAPI AUDDRV_GetAudioSessionManager(IMMDevice *device,
        IAudioSessionManager2 **out)
{
    SessionMgr *This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SessionMgr));
    *out = NULL;
    if (!This)
        return E_OUTOFMEMORY;
    This->IAudioSessionManager2_iface.lpVtbl = &AudioSessionManager2_Vtbl;
    This->device = device;
    This->ref = 1;
    *out = &This->IAudioSessionManager2_iface;
    return S_OK;
}

HRESULT WINAPI AUDDRV_GetPropValue(GUID *guid, const PROPERTYKEY *prop, PROPVARIANT *out)
{
    TRACE("%s, (%s,%u), %p\n", wine_dbgstr_guid(guid), wine_dbgstr_guid(&prop->fmtid), prop->pid, out);

    if (IsEqualGUID(guid, &pulse_render_guid) && IsEqualPropertyKey(*prop, PKEY_AudioEndpoint_PhysicalSpeakers)) {
        out->vt = VT_UI4;
        out->u.ulVal = g_phys_speakers_mask;

        return out->u.ulVal ? S_OK : E_FAIL;
    }

    return E_NOTIMPL;
}
