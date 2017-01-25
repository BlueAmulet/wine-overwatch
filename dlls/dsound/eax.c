/*
 * Copyright (c) 2008-2009 Christopher Fitzgerald
 * Copyright (c) 2015 Mark Harmstone
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

/* Taken in large part from OpenAL's Alc/alcReverb.c. */

#include <stdarg.h>
#include <math.h>

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "mmsystem.h"
#include "winternl.h"
#include "vfwmsgs.h"
#include "wine/debug.h"
#include "dsound.h"
#include "dsound_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(eax);

static const EAX_REVERBPROPERTIES presets[] = {
    { EAX_ENVIRONMENT_GENERIC, 0.5f, 1.493f, 0.5f },
    { EAX_ENVIRONMENT_PADDEDCELL, 0.25f, 0.1f, 0.0f },
    { EAX_ENVIRONMENT_ROOM, 0.417f, 0.4f, 0.666f },
    { EAX_ENVIRONMENT_BATHROOM, 0.653f, 1.499f, 0.166f },
    { EAX_ENVIRONMENT_LIVINGROOM, 0.208f, 0.478f, 0.0f },
    { EAX_ENVIRONMENT_STONEROOM, 0.5f, 2.309f, 0.888f },
    { EAX_ENVIRONMENT_AUDITORIUM, 0.403f, 4.279f, 0.5f },
    { EAX_ENVIRONMENT_CONCERTHALL, 0.5f, 3.961f, 0.5f },
    { EAX_ENVIRONMENT_CAVE, 0.5f, 2.886f, 1.304f },
    { EAX_ENVIRONMENT_ARENA, 0.361f, 7.284f, 0.332f },
    { EAX_ENVIRONMENT_HANGAR, 0.5f, 10.0f, 0.3f },
    { EAX_ENVIRONMENT_CARPETEDHALLWAY, 0.153f, 0.259f, 2.0f },
    { EAX_ENVIRONMENT_HALLWAY, 0.361f, 1.493f, 0.0f },
    { EAX_ENVIRONMENT_STONECORRIDOR, 0.444f, 2.697f, 0.638f },
    { EAX_ENVIRONMENT_ALLEY, 0.25f, 1.752f, 0.776f },
    { EAX_ENVIRONMENT_FOREST, 0.111f, 3.145f, 0.472f },
    { EAX_ENVIRONMENT_CITY, 0.111f, 2.767f, 0.224f },
    { EAX_ENVIRONMENT_MOUNTAINS, 0.194f, 7.841f, 0.472f },
    { EAX_ENVIRONMENT_QUARRY, 1.0f, 1.499f, 0.5f },
    { EAX_ENVIRONMENT_PLAIN, 0.097f, 2.767f, 0.224f },
    { EAX_ENVIRONMENT_PARKINGLOT, 0.208f, 1.652f, 1.5f },
    { EAX_ENVIRONMENT_SEWERPIPE, 0.652f, 2.886f, 0.25f },
    { EAX_ENVIRONMENT_UNDERWATER, 1.0f, 1.499f, 0.0f },
    { EAX_ENVIRONMENT_DRUGGED, 0.875f, 8.392f, 1.388f },
    { EAX_ENVIRONMENT_DIZZY, 0.139f, 17.234f, 0.666f },
    { EAX_ENVIRONMENT_PSYCHOTIC, 0.486f, 7.563f, 0.806f }
};

static const EFXEAXREVERBPROPERTIES efx_presets[] = {
    { 1.0000f, 1.0000f, 0.3162f, 0.8913f, 1.0000f, 1.4900f, 0.8300f, 1.0000f, 0.0500f, 0.0070f, { 0.0000f, 0.0000f, 0.0000f }, 1.2589f, 0.0110f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* generic */
    { 0.1715f, 1.0000f, 0.3162f, 0.0010f, 1.0000f, 0.1700f, 0.1000f, 1.0000f, 0.2500f, 0.0010f, { 0.0000f, 0.0000f, 0.0000f }, 1.2691f, 0.0020f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* padded cell */
    { 0.4287f, 1.0000f, 0.3162f, 0.5929f, 1.0000f, 0.4000f, 0.8300f, 1.0000f, 0.1503f, 0.0020f, { 0.0000f, 0.0000f, 0.0000f }, 1.0629f, 0.0030f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* room */
    { 0.1715f, 1.0000f, 0.3162f, 0.2512f, 1.0000f, 1.4900f, 0.5400f, 1.0000f, 0.6531f, 0.0070f, { 0.0000f, 0.0000f, 0.0000f }, 3.2734f, 0.0110f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* bathroom */
    { 0.9766f, 1.0000f, 0.3162f, 0.0010f, 1.0000f, 0.5000f, 0.1000f, 1.0000f, 0.2051f, 0.0030f, { 0.0000f, 0.0000f, 0.0000f }, 0.2805f, 0.0040f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* living room */
    { 1.0000f, 1.0000f, 0.3162f, 0.7079f, 1.0000f, 2.3100f, 0.6400f, 1.0000f, 0.4411f, 0.0120f, { 0.0000f, 0.0000f, 0.0000f }, 1.1003f, 0.0170f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* stone room */
    { 1.0000f, 1.0000f, 0.3162f, 0.5781f, 1.0000f, 4.3200f, 0.5900f, 1.0000f, 0.4032f, 0.0200f, { 0.0000f, 0.0000f, 0.0000f }, 0.7170f, 0.0300f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* auditorium */
    { 1.0000f, 1.0000f, 0.3162f, 0.5623f, 1.0000f, 3.9200f, 0.7000f, 1.0000f, 0.2427f, 0.0200f, { 0.0000f, 0.0000f, 0.0000f }, 0.9977f, 0.0290f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* concert hall */
    { 1.0000f, 1.0000f, 0.3162f, 1.0000f, 1.0000f, 2.9100f, 1.3000f, 1.0000f, 0.5000f, 0.0150f, { 0.0000f, 0.0000f, 0.0000f }, 0.7063f, 0.0220f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x0 }, /* cave */
    { 1.0000f, 1.0000f, 0.3162f, 0.4477f, 1.0000f, 7.2400f, 0.3300f, 1.0000f, 0.2612f, 0.0200f, { 0.0000f, 0.0000f, 0.0000f }, 1.0186f, 0.0300f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* arena */
    { 1.0000f, 1.0000f, 0.3162f, 0.3162f, 1.0000f, 10.0500f, 0.2300f, 1.0000f, 0.5000f, 0.0200f, { 0.0000f, 0.0000f, 0.0000f }, 1.2560f, 0.0300f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* hangar */
    { 0.4287f, 1.0000f, 0.3162f, 0.0100f, 1.0000f, 0.3000f, 0.1000f, 1.0000f, 0.1215f, 0.0020f, { 0.0000f, 0.0000f, 0.0000f }, 0.1531f, 0.0300f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* carpeted hallway */
    { 0.3645f, 1.0000f, 0.3162f, 0.7079f, 1.0000f, 1.4900f, 0.5900f, 1.0000f, 0.2458f, 0.0070f, { 0.0000f, 0.0000f, 0.0000f }, 1.6615f, 0.0110f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* hallway */
    { 1.0000f, 1.0000f, 0.3162f, 0.7612f, 1.0000f, 2.7000f, 0.7900f, 1.0000f, 0.2472f, 0.0130f, { 0.0000f, 0.0000f, 0.0000f }, 1.5758f, 0.0200f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* stone corridor */
    { 1.0000f, 0.3000f, 0.3162f, 0.7328f, 1.0000f, 1.4900f, 0.8600f, 1.0000f, 0.2500f, 0.0070f, { 0.0000f, 0.0000f, 0.0000f }, 0.9954f, 0.0110f, { 0.0000f, 0.0000f, 0.0000f }, 0.1250f, 0.9500f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* alley */
    { 1.0000f, 0.3000f, 0.3162f, 0.0224f, 1.0000f, 1.4900f, 0.5400f, 1.0000f, 0.0525f, 0.1620f, { 0.0000f, 0.0000f, 0.0000f }, 0.7682f, 0.0880f, { 0.0000f, 0.0000f, 0.0000f }, 0.1250f, 1.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* forest */
    { 1.0000f, 0.5000f, 0.3162f, 0.3981f, 1.0000f, 1.4900f, 0.6700f, 1.0000f, 0.0730f, 0.0070f, { 0.0000f, 0.0000f, 0.0000f }, 0.1427f, 0.0110f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* city */
    { 1.0000f, 0.2700f, 0.3162f, 0.0562f, 1.0000f, 1.4900f, 0.2100f, 1.0000f, 0.0407f, 0.3000f, { 0.0000f, 0.0000f, 0.0000f }, 0.1919f, 0.1000f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 1.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x0 }, /* mountains */
    { 1.0000f, 1.0000f, 0.3162f, 0.3162f, 1.0000f, 1.4900f, 0.8300f, 1.0000f, 0.0000f, 0.0610f, { 0.0000f, 0.0000f, 0.0000f }, 1.7783f, 0.0250f, { 0.0000f, 0.0000f, 0.0000f }, 0.1250f, 0.7000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* quarry */
    { 1.0000f, 0.2100f, 0.3162f, 0.1000f, 1.0000f, 1.4900f, 0.5000f, 1.0000f, 0.0585f, 0.1790f, { 0.0000f, 0.0000f, 0.0000f }, 0.1089f, 0.1000f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 1.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* plain */
    { 1.0000f, 1.0000f, 0.3162f, 1.0000f, 1.0000f, 1.6500f, 1.5000f, 1.0000f, 0.2082f, 0.0080f, { 0.0000f, 0.0000f, 0.0000f }, 0.2652f, 0.0120f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x0 }, /* parking lot */
    { 0.3071f, 0.8000f, 0.3162f, 0.3162f, 1.0000f, 2.8100f, 0.1400f, 1.0000f, 1.6387f, 0.0140f, { 0.0000f, 0.0000f, 0.0000f }, 3.2471f, 0.0210f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* sewer pipe */
    { 0.3645f, 1.0000f, 0.3162f, 0.0100f, 1.0000f, 1.4900f, 0.1000f, 1.0000f, 0.5963f, 0.0070f, { 0.0000f, 0.0000f, 0.0000f }, 7.0795f, 0.0110f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 1.1800f, 0.3480f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x1 }, /* underwater */
    { 0.4287f, 0.5000f, 0.3162f, 1.0000f, 1.0000f, 8.3900f, 1.3900f, 1.0000f, 0.8760f, 0.0020f, { 0.0000f, 0.0000f, 0.0000f }, 3.1081f, 0.0300f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 1.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x0 }, /* drugged */
    { 0.3645f, 0.6000f, 0.3162f, 0.6310f, 1.0000f, 17.2300f, 0.5600f, 1.0000f, 0.1392f, 0.0200f, { 0.0000f, 0.0000f, 0.0000f }, 0.4937f, 0.0300f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 1.0000f, 0.8100f, 0.3100f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x0 }, /* dizzy */
    { 0.0625f, 0.5000f, 0.3162f, 0.8404f, 1.0000f, 7.5600f, 0.9100f, 1.0000f, 0.4864f, 0.0200f, { 0.0000f, 0.0000f, 0.0000f }, 2.4378f, 0.0300f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 4.0000f, 1.0000f, 0.9943f, 5000.0000f, 250.0000f, 0.0000f, 0x0 } /* psychotic */
};

static float lpFilter2P(FILTER *iir, unsigned int offset, float input)
{
    float *history = &iir->history[offset*2];
    float a = iir->coeff;
    float output = input;

    output = output + (history[0]-output)*a;
    history[0] = output;
    output = output + (history[1]-output)*a;
    history[1] = output;

    return output;
}

static void DelayLineIn(DelayLine *Delay, unsigned int offset, float in)
{
    Delay->Line[offset&Delay->Mask] = in;
}

static float DelayLineOut(DelayLine *Delay, unsigned int offset)
{
    return Delay->Line[offset&Delay->Mask];
}

static void VerbPass(IDirectSoundBufferImpl* dsb, float in, float* out)
{
    /* Low-pass filter the incoming sample. */
    in = lpFilter2P(&dsb->eax.LpFilter, 0, in);

    /* Feed the initial delay line. */
    DelayLineIn(&dsb->eax.Delay, dsb->eax.Offset, in);

    in = DelayLineOut(&dsb->eax.Delay, dsb->eax.Offset - dsb->eax.DelayTap[0]);

    /* Step all delays forward one sample. */
    dsb->eax.Offset++;
}

static unsigned int fastf2u(float f)
{
    return (unsigned int)f;
}

void process_eax_buffer(IDirectSoundBufferImpl *dsb, float *buf, DWORD count)
{
    int i;
    float* out;
    float gain;

    if (dsb->device->eax.volume == 0.0f)
        return;

    if (dsb->mix_channels > 1) {
        WARN("EAX not yet supported for non-mono sources\n");
        return;
    }

    out = HeapAlloc(GetProcessHeap(), 0, sizeof(float)*count*4);

    for (i = 0; i < count; i++) {
        VerbPass(dsb, buf[i], &out[i*4]);
    }

    if (dsb->eax.reverb_mix == EAX_REVERBMIX_USEDISTANCE)
        gain = 1.0f; /* FIXME - should be calculated from distance */
    else
        gain = dsb->eax.reverb_mix;

    for (i = 0; i < count; i++) {
        buf[i] += gain * out[i*4];
    }

    HeapFree(GetProcessHeap(), 0, out);
}

static void UpdateDelayLine(float earlyDelay, float lateDelay, unsigned int frequency, eax_buffer_info *State)
{
    State->DelayTap[0] = fastf2u(earlyDelay * frequency);
    State->DelayTap[1] = fastf2u((earlyDelay + lateDelay) * frequency);
}

static float lpCoeffCalc(float g, float cw)
{
    float a = 0.0f;

    if (g < 0.9999f) /* 1-epsilon */
    {
        /* Be careful with gains < 0.001, as that causes the coefficient head
         * towards 1, which will flatten the signal */
        if (g < 0.001f) g = 0.001f;
        a = (1 - g*cw - sqrtf(2*g*(1-cw) - g*g*(1 - cw*cw))) /
            (1 - g);
    }

    return a;
}

static float CalcI3DL2HFreq(float hfRef, unsigned int frequency)
{
    return cosf(M_PI*2.0f * hfRef / frequency);
}

static unsigned int NextPowerOf2(unsigned int value)
{
    if (value > 0)
    {
        value--;
        value |= value>>1;
        value |= value>>2;
        value |= value>>4;
        value |= value>>8;
        value |= value>>16;
    }
    return value+1;
}

static unsigned int CalcLineLength(float length, unsigned int offset, unsigned int frequency, DelayLine *Delay)
{
    unsigned int samples;

    /* All line lengths are powers of 2, calculated from their lengths, with
     * an additional sample in case of rounding errors. */
    samples = NextPowerOf2(fastf2u(length * frequency) + 1);
    /* All lines share a single sample buffer. */
    Delay->Mask = samples - 1;
    Delay->Line = (float*)offset;
    /* Return the sample count for accumulation. */
    return samples;
}

static void RealizeLineOffset(float *sampleBuffer, DelayLine *Delay)
{
    Delay->Line = &sampleBuffer[(unsigned int)Delay->Line];
}

static BOOL AllocLines(unsigned int frequency, IDirectSoundBufferImpl *dsb)
{
    unsigned int totalSamples, index;
    float length;
    float *newBuffer = NULL;

    /* All delay line lengths are calculated to accomodate the full range of
     * lengths given their respective paramters. */
    totalSamples = 0;

    /* The initial delay is the sum of the reflections and late reverb
     * delays. */
    length = AL_EAXREVERB_MAX_REFLECTIONS_DELAY +
             AL_EAXREVERB_MAX_LATE_REVERB_DELAY;
    totalSamples += CalcLineLength(length, totalSamples, frequency,
                                   &dsb->eax.Delay);

    if (totalSamples != dsb->eax.TotalSamples)
    {
        TRACE("New reverb buffer length: %u samples (%f sec)\n", totalSamples, totalSamples/(float)frequency);

        if (dsb->eax.SampleBuffer)
            newBuffer = HeapReAlloc(GetProcessHeap(), 0, dsb->eax.SampleBuffer, sizeof(float) * totalSamples);
        else
            newBuffer = HeapAlloc(GetProcessHeap(), 0, sizeof(float) * totalSamples);

        if (newBuffer == NULL)
            return FALSE;
        dsb->eax.SampleBuffer = newBuffer;
        dsb->eax.TotalSamples = totalSamples;
    }

    /* Update all delays to reflect the new sample buffer. */
    RealizeLineOffset(dsb->eax.SampleBuffer, &dsb->eax.Delay);

    /* Clear the sample buffer. */
    for (index = 0; index < dsb->eax.TotalSamples; index++)
        dsb->eax.SampleBuffer[index] = 0.0f;

    return TRUE;
}

static void ReverbUpdate(IDirectSoundBufferImpl *dsb)
{
    float cw;

    /* avoid segfaults in mixing thread when we recalculate the line offsets */
    EnterCriticalSection(&dsb->device->mixlock);

    AllocLines(dsb->device->pwfx->nSamplesPerSec, dsb);

    LeaveCriticalSection(&dsb->device->mixlock);

    cw = CalcI3DL2HFreq(dsb->device->eax.eax_props.flHFReference, dsb->device->pwfx->nSamplesPerSec);

    dsb->eax.LpFilter.coeff = lpCoeffCalc(dsb->device->eax.eax_props.flGainHF, cw);

    UpdateDelayLine(dsb->device->eax.eax_props.flReflectionsDelay,
                    dsb->device->eax.eax_props.flLateReverbDelay,
                    dsb->device->pwfx->nSamplesPerSec, &dsb->eax);
}

static BOOL ReverbDeviceUpdate(DirectSoundDevice *dev)
{
    int i;

    for (i = 0; i < dev->nrofbuffers; i++) {
        ReverbUpdate(dev->buffers[i]);
    }

    return TRUE;
}

void init_eax_buffer(IDirectSoundBufferImpl *dsb)
{
    dsb->eax.TotalSamples = 0;
    dsb->eax.SampleBuffer = NULL;

    dsb->eax.LpFilter.coeff = 0.0f;
    dsb->eax.LpFilter.history[0] = 0.0f;
    dsb->eax.LpFilter.history[1] = 0.0f;

    dsb->eax.Delay.Mask = 0;
    dsb->eax.Delay.Line = NULL;
    dsb->eax.DelayTap[0] = 0;
    dsb->eax.DelayTap[1] = 0;

    dsb->eax.Offset = 0;

    ReverbUpdate(dsb);
}

static void init_eax(DirectSoundDevice *dev)
{
    int i;

    dev->eax.using_eax = TRUE;
    dev->eax.environment = presets[0].environment;
    dev->eax.volume = presets[0].fVolume;
    dev->eax.damping = presets[0].fDamping;
    memcpy(&dev->eax.eax_props, &efx_presets[0], sizeof(dev->eax.eax_props));
    dev->eax.eax_props.flDecayTime = presets[0].fDecayTime_sec;

    for (i = 0; i < dev->nrofbuffers; i++) {
        init_eax_buffer(dev->buffers[i]);
    }
}

void free_eax_buffer(IDirectSoundBufferImpl *dsb)
{
    if (dsb->eax.SampleBuffer)
        HeapFree(GetProcessHeap(), 0, dsb->eax.SampleBuffer);
}

HRESULT WINAPI EAX_Get(IDirectSoundBufferImpl *buf, REFGUID guidPropSet,
        ULONG dwPropID, void *pInstanceData, ULONG cbInstanceData, void *pPropData,
        ULONG cbPropData, ULONG *pcbReturned)
{
    TRACE("(buf=%p,guidPropSet=%s,dwPropID=%d,pInstanceData=%p,cbInstanceData=%d,pPropData=%p,cbPropData=%d,pcbReturned=%p)\n",
        buf, debugstr_guid(guidPropSet), dwPropID, pInstanceData, cbInstanceData, pPropData, cbPropData, pcbReturned);

    *pcbReturned = 0;

    if (IsEqualGUID(&DSPROPSETID_EAX_ReverbProperties, guidPropSet)) {
        EAX_REVERBPROPERTIES *props;

        if (!buf->device->eax.using_eax)
            init_eax(buf->device);

        switch (dwPropID) {
            case DSPROPERTY_EAX_ALL:
                if (cbPropData < sizeof(EAX_REVERBPROPERTIES))
                    return E_FAIL;

                props = pPropData;

                props->environment = buf->device->eax.environment;
                props->fVolume = buf->device->eax.volume;
                props->fDecayTime_sec = buf->device->eax.eax_props.flDecayTime;
                props->fDamping = buf->device->eax.damping;

                *pcbReturned = sizeof(EAX_REVERBPROPERTIES);
            break;

            case DSPROPERTY_EAX_ENVIRONMENT:
                if (cbPropData < sizeof(unsigned long))
                    return E_FAIL;

                *(unsigned long*)pPropData = buf->device->eax.environment;

                *pcbReturned = sizeof(unsigned long);
            break;

            case DSPROPERTY_EAX_VOLUME:
                if (cbPropData < sizeof(float))
                    return E_FAIL;

                *(float*)pPropData = buf->device->eax.volume;

                *pcbReturned = sizeof(float);
            break;

            case DSPROPERTY_EAX_DECAYTIME:
                if (cbPropData < sizeof(float))
                    return E_FAIL;

                *(float*)pPropData = buf->device->eax.eax_props.flDecayTime;

                *pcbReturned = sizeof(float);
            break;

            case DSPROPERTY_EAX_DAMPING:
                if (cbPropData < sizeof(float))
                    return E_FAIL;

                *(float*)pPropData = buf->device->eax.damping;

                *pcbReturned = sizeof(float);
            break;

            default:
                return E_PROP_ID_UNSUPPORTED;
        }

        return S_OK;
    } else if (IsEqualGUID(&DSPROPSETID_EAXBUFFER_ReverbProperties, guidPropSet)) {
        EAXBUFFER_REVERBPROPERTIES *props;

        if (!buf->device->eax.using_eax)
            init_eax(buf->device);

        switch (dwPropID) {
            case DSPROPERTY_EAXBUFFER_ALL:
                if (cbPropData < sizeof(EAXBUFFER_REVERBPROPERTIES))
                    return E_FAIL;

                props = pPropData;

                props->fMix = buf->eax.reverb_mix;

                *pcbReturned = sizeof(EAXBUFFER_REVERBPROPERTIES);
            break;

            case DSPROPERTY_EAXBUFFER_REVERBMIX:
                if (cbPropData < sizeof(float))
                    return E_FAIL;

                *(float*)pPropData = buf->eax.reverb_mix;

                *pcbReturned = sizeof(float);
            break;

            default:
                return E_PROP_ID_UNSUPPORTED;
        }

        return S_OK;
    }

    return E_PROP_ID_UNSUPPORTED;
}

HRESULT WINAPI EAX_Set(IDirectSoundBufferImpl *buf, REFGUID guidPropSet,
        ULONG dwPropID, void *pInstanceData, ULONG cbInstanceData, void *pPropData,
        ULONG cbPropData)
{
    EAX_REVERBPROPERTIES *props;

    TRACE("(%p,%s,%d,%p,%d,%p,%d)\n",
        buf, debugstr_guid(guidPropSet), dwPropID, pInstanceData, cbInstanceData, pPropData, cbPropData);

    if (IsEqualGUID(&DSPROPSETID_EAX_ReverbProperties, guidPropSet)) {
        if (!buf->device->eax.using_eax)
            init_eax(buf->device);

        switch (dwPropID) {
            case DSPROPERTY_EAX_ALL:
                if (cbPropData != sizeof(EAX_REVERBPROPERTIES))
                    return E_FAIL;

                props = pPropData;

                TRACE("setting environment = %lu, fVolume = %f, fDecayTime_sec = %f, fDamping = %f\n",
                      props->environment, props->fVolume, props->fDecayTime_sec,
                      props->fDamping);

                buf->device->eax.environment = props->environment;

                if (buf->device->eax.environment < EAX_ENVIRONMENT_COUNT)
                    memcpy(&buf->device->eax.eax_props, &efx_presets[buf->device->eax.environment], sizeof(buf->device->eax.eax_props));

                buf->device->eax.volume = props->fVolume;
                buf->device->eax.eax_props.flDecayTime = props->fDecayTime_sec;
                buf->device->eax.damping = props->fDamping;

                ReverbDeviceUpdate(buf->device);
            break;

            case DSPROPERTY_EAX_ENVIRONMENT:
                if (cbPropData != sizeof(unsigned long))
                    return E_FAIL;

                TRACE("setting environment to %lu\n", *(unsigned long*)pPropData);

                buf->device->eax.environment = *(unsigned long*)pPropData;

                if (buf->device->eax.environment < EAX_ENVIRONMENT_COUNT) {
                    memcpy(&buf->device->eax.eax_props, &efx_presets[buf->device->eax.environment], sizeof(buf->device->eax.eax_props));
                    buf->device->eax.volume = presets[buf->device->eax.environment].fVolume;
                    buf->device->eax.eax_props.flDecayTime = presets[buf->device->eax.environment].fDecayTime_sec;
                    buf->device->eax.damping = presets[buf->device->eax.environment].fDamping;
                }

                ReverbDeviceUpdate(buf->device);
            break;

            case DSPROPERTY_EAX_VOLUME:
                if (cbPropData != sizeof(float))
                    return E_FAIL;

                TRACE("setting volume to %f\n", *(float*)pPropData);

                buf->device->eax.volume = *(float*)pPropData;

                ReverbDeviceUpdate(buf->device);
            break;

            case DSPROPERTY_EAX_DECAYTIME:
                if (cbPropData != sizeof(float))
                    return E_FAIL;

                TRACE("setting decay time to %f\n", *(float*)pPropData);

                buf->device->eax.eax_props.flDecayTime = *(float*)pPropData;

                ReverbDeviceUpdate(buf->device);
            break;

            case DSPROPERTY_EAX_DAMPING:
                if (cbPropData != sizeof(float))
                    return E_FAIL;

                TRACE("setting damping to %f\n", *(float*)pPropData);

                buf->device->eax.damping = *(float*)pPropData;

                ReverbDeviceUpdate(buf->device);
            break;

            default:
                return E_PROP_ID_UNSUPPORTED;
        }

        return S_OK;
    } else if (IsEqualGUID(&DSPROPSETID_EAXBUFFER_ReverbProperties, guidPropSet)) {
        EAXBUFFER_REVERBPROPERTIES *props;

        if (!buf->device->eax.using_eax)
            init_eax(buf->device);

        switch (dwPropID) {
            case DSPROPERTY_EAXBUFFER_ALL:
                if (cbPropData != sizeof(EAXBUFFER_REVERBPROPERTIES))
                    return E_FAIL;

                props = pPropData;

                TRACE("setting reverb mix to %f\n", props->fMix);

                buf->eax.reverb_mix = props->fMix;
            break;

            case DSPROPERTY_EAXBUFFER_REVERBMIX:
                if (cbPropData != sizeof(float))
                    return E_FAIL;

                TRACE("setting reverb mix to %f\n", *(float*)pPropData);

                buf->eax.reverb_mix = *(float*)pPropData;
            break;

            default:
                return E_PROP_ID_UNSUPPORTED;
        }

        return S_OK;
    }

    return E_PROP_ID_UNSUPPORTED;
}
