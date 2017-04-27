/*
 * Configuration parameters shared between Wine server and clients
 *
 * Copyright 2002 Alexandre Julliard
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

#include "config.h"
#include "wine/port.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#include "wine/library.h"

static const char server_config_dir[] = "/.wine";        /* config dir relative to $HOME */
static const char server_root_prefix[] = "/tmp/.wine";   /* prefix for server root dir */
static const char server_dir_prefix[] = "/server-";      /* prefix for server dir */

static char *bindir;
static char *dlldir;
static char *datadir;
static char *config_dir;
static char *server_dir;
static char *build_dir;
static char *user_name;
static char *argv0_name;

#ifdef __GNUC__
static void fatal_error( const char *err, ... )  __attribute__((noreturn,format(printf,1,2)));
static void fatal_perror( const char *err, ... )  __attribute__((noreturn,format(printf,1,2)));
#endif

#if defined(__linux__) || defined(__FreeBSD_kernel__ )
#define EXE_LINK "/proc/self/exe"
#elif defined (__FreeBSD__) || defined(__DragonFly__)
#define EXE_LINK "/proc/curproc/file"
#endif

/* die on a fatal error */
static void fatal_error( const char *err, ... )
{
    va_list args;

    va_start( args, err );
    fprintf( stderr, "wine: " );
    vfprintf( stderr, err, args );
    va_end( args );
    exit(1);
}

/* die on a fatal error */
static void fatal_perror( const char *err, ... )
{
    va_list args;

    va_start( args, err );
    fprintf( stderr, "wine: " );
    vfprintf( stderr, err, args );
    perror( " " );
    va_end( args );
    exit(1);
}

/* malloc wrapper */
static void *xmalloc( size_t size )
{
    void *res;

    if (!size) size = 1;
    if (!(res = malloc( size ))) fatal_error( "virtual memory exhausted\n");
    return res;
}

/* strdup wrapper */
static char *xstrdup( const char *str )
{
    size_t len = strlen(str) + 1;
    char *res = xmalloc( len );
    memcpy( res, str, len );
    return res;
}

/* check if a string ends in a given substring */
static inline int strendswith( const char* str, const char* end )
{
    size_t len = strlen( str );
    size_t tail = strlen( end );
    return len >= tail && !strcmp( str + len - tail, end );
}

/* remove all trailing slashes from a path name */
static inline void remove_trailing_slashes( char *path )
{
    int len = strlen( path );
    while (len > 1 && path[len-1] == '/') path[--len] = 0;
}

/* build a path from the specified dir and name */
static char *build_path( const char *dir, const char *name )
{
    size_t len = strlen(dir);
    char *ret = xmalloc( len + strlen(name) + 2 );

    memcpy( ret, dir, len );
    if (len && ret[len-1] != '/') ret[len++] = '/';
    strcpy( ret + len, name );
    return ret;
}

/* return the directory that contains the library at run-time */
static char *get_runtime_libdir(void)
{
#ifdef HAVE_DLADDR
    Dl_info info;
    char *libdir;

    if (dladdr( get_runtime_libdir, &info ) && info.dli_fname[0] == '/')
    {
        const char *p = strrchr( info.dli_fname, '/' );
        unsigned int len = p - info.dli_fname;
        if (!len) len++;  /* include initial slash */
        libdir = xmalloc( len + 1 );
        memcpy( libdir, info.dli_fname, len );
        libdir[len] = 0;
        return libdir;
    }
#endif /* HAVE_DLADDR */
    return NULL;
}

/* return the directory that contains the main exe at run-time */
static char *get_runtime_exedir(void)
{
#ifdef EXE_LINK
    char *p, *bindir;
    int size;

    for (size = 256; ; size *= 2)
    {
        int ret;
        if (!(bindir = malloc( size ))) return NULL;
        if ((ret = readlink( EXE_LINK, bindir, size )) == -1) break;
        if (ret != size)
        {
            bindir[ret] = 0;
            if (!(p = strrchr( bindir, '/' ))) break;
            if (p == bindir) p++;
            *p = 0;
            return bindir;
        }
        free( bindir );
    }
    free( bindir );
#endif
    return NULL;
}

/* return the base directory from argv0 */
static char *get_runtime_argvdir( const char *argv0 )
{
    char *p, *bindir, *cwd;
    int len, size;

    if (!(p = strrchr( argv0, '/' ))) return NULL;

    len = p - argv0;
    if (!len) len++;  /* include leading slash */

    if (argv0[0] == '/')  /* absolute path */
    {
        bindir = xmalloc( len + 1 );
        memcpy( bindir, argv0, len );
        bindir[len] = 0;
    }
    else
    {
        /* relative path, make it absolute */
        for (size = 256 + len; ; size *= 2)
        {
            if (!(cwd = malloc( size ))) return NULL;
            if (getcwd( cwd, size - len ))
            {
                bindir = cwd;
                cwd += strlen(cwd);
                *cwd++ = '/';
                memcpy( cwd, argv0, len );
                cwd[len] = 0;
                break;
            }
            free( cwd );
            if (errno != ERANGE) return NULL;
        }
    }
    return bindir;
}

/* initialize the server directory value */
static void init_server_dir( dev_t dev, ino_t ino )
{
    char *p, *root;

#ifdef __ANDROID__  /* there's no /tmp dir on Android */
    root = build_path( config_dir, ".wineserver" );
#elif defined(HAVE_GETUID)
    root = xmalloc( sizeof(server_root_prefix) + 12 );
    sprintf( root, "%s-%u", server_root_prefix, getuid() );
#else
    root = xstrdup( server_root_prefix );
#endif

    server_dir = xmalloc( strlen(root) + sizeof(server_dir_prefix) + 2*sizeof(dev) + 2*sizeof(ino) + 2 );
    strcpy( server_dir, root );
    strcat( server_dir, server_dir_prefix );
    p = server_dir + strlen(server_dir);

    if (dev != (unsigned long)dev)
        p += sprintf( p, "%lx%08lx-", (unsigned long)((unsigned long long)dev >> 32), (unsigned long)dev );
    else
        p += sprintf( p, "%lx-", (unsigned long)dev );

    if (ino != (unsigned long)ino)
        sprintf( p, "%lx%08lx", (unsigned long)((unsigned long long)ino >> 32), (unsigned long)ino );
    else
        sprintf( p, "%lx", (unsigned long)ino );
    free( root );
}

/* retrieve the default dll dir */
const char *get_dlldir( const char **default_dlldir )
{
    *default_dlldir = DLLDIR;
    return dlldir;
}

/* initialize all the paths values */
static void init_paths(void)
{
    struct stat st;

    const char *home = getenv( "HOME" );
    const char *user = NULL;
    const char *prefix = getenv( "WINEPREFIX" );

#ifdef HAVE_GETPWUID
    char uid_str[32];
    struct passwd *pwd = getpwuid( getuid() );

    if (pwd)
    {
        user = pwd->pw_name;
        if (!home) home = pwd->pw_dir;
    }
    if (!user)
    {
        sprintf( uid_str, "%lu", (unsigned long)getuid() );
        user = uid_str;
    }
#else  /* HAVE_GETPWUID */
    if (!(user = getenv( "USER" )))
        fatal_error( "cannot determine your user name, set the USER environment variable\n" );
#endif  /* HAVE_GETPWUID */
    user_name = xstrdup( user );

    /* build config_dir */

    if (prefix)
    {
        config_dir = xstrdup( prefix );
        remove_trailing_slashes( config_dir );
        if (config_dir[0] != '/')
            fatal_error( "invalid directory %s in WINEPREFIX: not an absolute path\n", prefix );
        if (stat( config_dir, &st ) == -1)
        {
            if (errno == ENOENT) return;  /* will be created later on */
            fatal_perror( "cannot open %s as specified in WINEPREFIX", config_dir );
        }
    }
    else
    {
        if (!home) fatal_error( "could not determine your home directory\n" );
        if (home[0] != '/') fatal_error( "your home directory %s is not an absolute path\n", home );
        config_dir = xmalloc( strlen(home) + sizeof(server_config_dir) );
        strcpy( config_dir, home );
        remove_trailing_slashes( config_dir );
        strcat( config_dir, server_config_dir );
        if (stat( config_dir, &st ) == -1)
        {
            if (errno == ENOENT) return;  /* will be created later on */
            fatal_perror( "cannot open %s", config_dir );
        }
    }
    if (!S_ISDIR(st.st_mode)) fatal_error( "%s is not a directory\n", config_dir );
#ifdef HAVE_GETUID
    if (st.st_uid != getuid()) fatal_error( "%s is not owned by you\n", config_dir );
#endif

    init_server_dir( st.st_dev, st.st_ino );
}

/* check if bindir is valid by checking for wineserver */
static int is_valid_bindir( const char *bindir )
{
    struct stat st;
    char *path = build_path( bindir, "wineserver" );
    int ret = (stat( path, &st ) != -1);
    free( path );
    return ret;
}

/* check if basedir is a valid build dir by checking for wineserver and ntdll */
/* helper for running_from_build_dir */
static inline int is_valid_build_dir( char *basedir, int baselen )
{
    struct stat st;

    strcpy( basedir + baselen, "/server/wineserver" );
    if (stat( basedir, &st ) == -1) return 0;  /* no wineserver found */
    /* check for ntdll too to make sure */
    strcpy( basedir + baselen, "/dlls/ntdll/ntdll.dll.so" );
    if (stat( basedir, &st ) == -1) return 0;  /* no ntdll found */

    basedir[baselen] = 0;
    return 1;
}

/* check if we are running from the build directory */
static char *running_from_build_dir( const char *basedir )
{
    const char *p;
    char *path;

    /* remove last component from basedir */
    p = basedir + strlen(basedir) - 1;
    while (p > basedir && *p == '/') p--;
    while (p > basedir && *p != '/') p--;
    if (p == basedir) return NULL;
    path = xmalloc( p - basedir + sizeof("/dlls/ntdll/ntdll.dll.so") );
    memcpy( path, basedir, p - basedir );

    if (!is_valid_build_dir( path, p - basedir ))
    {
        /* remove another component */
        while (p > basedir && *p == '/') p--;
        while (p > basedir && *p != '/') p--;
        if (p == basedir || !is_valid_build_dir( path, p - basedir ))
        {
            free( path );
            return NULL;
        }
    }
    return path;
}

/* initialize the argv0 path */
void wine_init_argv0_path( const char *argv0 )
{
    const char *basename;
    char *libdir;

    if (!(basename = strrchr( argv0, '/' ))) basename = argv0;
    else basename++;

    bindir = get_runtime_exedir();
    if (bindir && !is_valid_bindir( bindir ))
    {
        build_dir = running_from_build_dir( bindir );
        free( bindir );
        bindir = NULL;
    }

    libdir = get_runtime_libdir();
    if (libdir && !bindir && !build_dir)
    {
        build_dir = running_from_build_dir( libdir );
        if (!build_dir) bindir = build_path( libdir, LIB_TO_BINDIR );
    }

    if (!libdir && !bindir && !build_dir)
    {
        bindir = get_runtime_argvdir( argv0 );
        if (bindir && !is_valid_bindir( bindir ))
        {
            build_dir = running_from_build_dir( bindir );
            free( bindir );
            bindir = NULL;
        }
    }

    if (build_dir)
    {
        argv0_name = build_path( "loader/", basename );
    }
    else
    {
        if (libdir) dlldir = build_path( libdir, LIB_TO_DLLDIR );
        else if (bindir) dlldir = build_path( bindir, BIN_TO_DLLDIR );

        if (bindir) datadir = build_path( bindir, BIN_TO_DATADIR );
        argv0_name = xstrdup( basename );
    }
    free( libdir );
}

/* return the configuration directory ($WINEPREFIX or $HOME/.wine) */
const char *wine_get_config_dir(void)
{
    if (!config_dir) init_paths();
    return config_dir;
}

/* retrieve the wine data dir */
const char *wine_get_data_dir(void)
{
    return datadir;
}

/* retrieve the wine build dir (if we are running from there) */
const char *wine_get_build_dir(void)
{
    return build_dir;
}

const char *wine_libs[] = {
#ifdef SONAME_LIBCAIRO
    SONAME_LIBCAIRO,
#endif
#ifdef SONAME_LIBCAPI20
    SONAME_LIBCAPI20,
#endif
#ifdef SONAME_LIBCUPS
    SONAME_LIBCUPS,
#endif
#ifdef SONAME_LIBCURSES
    SONAME_LIBCURSES,
#endif
#ifdef SONAME_LIBDBUS_1
    SONAME_LIBDBUS_1,
#endif
#ifdef SONAME_LIBFONTCONFIG
    SONAME_LIBFONTCONFIG,
#endif
#ifdef SONAME_LIBFREETYPE
    SONAME_LIBFREETYPE,
#endif
#ifdef SONAME_LIBGL
    SONAME_LIBGL,
#endif
#ifdef SONAME_LIBGNUTLS
    SONAME_LIBGNUTLS,
#endif
#ifdef SONAME_LIBGOBJECT_2_0
    SONAME_LIBGOBJECT_2_0,
#endif
#ifdef SONAME_LIBGSM
    SONAME_LIBGSM,
#endif
#ifdef SONAME_LIBGTK_3
    SONAME_LIBGTK_3,
#endif
#ifdef SONAME_LIBHAL
    SONAME_LIBHAL,
#endif
#ifdef SONAME_LIBJPEG
    SONAME_LIBJPEG,
#endif
#ifdef SONAME_LIBNCURSES
    SONAME_LIBNCURSES,
#endif
#ifdef SONAME_LIBNETAPI
    SONAME_LIBNETAPI,
#endif
#ifdef SONAME_LIBODBC
    SONAME_LIBODBC,
#endif
#ifdef SONAME_LIBOSMESA
    SONAME_LIBOSMESA,
#endif
#ifdef SONAME_LIBPCAP
    SONAME_LIBPCAP,
#endif
#ifdef SONAME_LIBPNG
    SONAME_LIBPNG,
#endif
#ifdef SONAME_LIBSANE
    SONAME_LIBSANE,
#endif
#ifdef SONAME_LIBTIFF
    SONAME_LIBTIFF,
#endif
#ifdef SONAME_LIBTXC_DXTN
    SONAME_LIBTXC_DXTN,
#endif
#ifdef SONAME_LIBV4L1
    SONAME_LIBV4L1,
#endif
#ifdef SONAME_LIBVA
    SONAME_LIBVA,
#endif
#ifdef SONAME_LIBVA_DRM
    SONAME_LIBVA_DRM,
#endif
#ifdef SONAME_LIBVA_X11
    SONAME_LIBVA_X11,
#endif
#ifdef SONAME_LIBX11
    SONAME_LIBX11,
#endif
#ifdef SONAME_LIBX11_XCB
    SONAME_LIBX11_XCB,
#endif
#ifdef SONAME_LIBXCOMPOSITE
    SONAME_LIBXCOMPOSITE,
#endif
#ifdef SONAME_LIBXCURSOR
    SONAME_LIBXCURSOR,
#endif
#ifdef SONAME_LIBXEXT
    SONAME_LIBXEXT,
#endif
#ifdef SONAME_LIBXI
    SONAME_LIBXI,
#endif
#ifdef SONAME_LIBXINERAMA
    SONAME_LIBXINERAMA,
#endif
#ifdef SONAME_LIBXRANDR
    SONAME_LIBXRANDR,
#endif
#ifdef SONAME_LIBXRENDER
    SONAME_LIBXRENDER,
#endif
#ifdef SONAME_LIBXSLT
    SONAME_LIBXSLT,
#endif
#ifdef SONAME_LIBXXF86VM
    SONAME_LIBXXF86VM,
#endif
    NULL
};

/* return the list of shared libs used by wine */
const char **wine_get_libs(void)
{
    return &wine_libs[0];
}

/* return the full name of the server directory (the one containing the socket) */
const char *wine_get_server_dir(void)
{
    if (!server_dir)
    {
        if (!config_dir) init_paths();
        else
        {
            struct stat st;

            if (stat( config_dir, &st ) == -1)
            {
                if (errno != ENOENT) fatal_error( "cannot stat %s\n", config_dir );
                return NULL;  /* will have to try again once config_dir has been created */
            }
            init_server_dir( st.st_dev, st.st_ino );
        }
    }
    return server_dir;
}

/* return the current user name */
const char *wine_get_user_name(void)
{
    if (!user_name) init_paths();
    return user_name;
}

/* return the standard version string */
const char *wine_get_version(void)
{
    return PACKAGE_VERSION;
}

static const struct
{
    const char *author;
    const char *subject;
    int revision;
}
wine_patch_data[] =
{
    { "Alexander E. Patrakov", "dsound: Add a linear resampler for use with a large number of mixing buffers.", 2 },
    { "Alexander Morozov", "ntoskrnl.exe: Add stub for ObReferenceObjectByPointer.", 1 },
    { "Alexander Morozov", "ntoskrnl.exe: Add stubs for ExAcquireFastMutexUnsafe and ExReleaseFastMutexUnsafe.", 1 },
    { "Alexander Morozov", "ntoskrnl.exe: Implement KeInitializeMutex.", 1 },
    { "Alexander Morozov", "ntoskrnl.exe: Improve KeInitializeSemaphore stub.", 1 },
    { "Alexander Morozov", "ntoskrnl.exe: Improve KeInitializeTimerEx stub.", 1 },
    { "Alexander Morozov", "ntoskrnl.exe: Improve KeReleaseMutex stub.", 1 },
    { "Alex Henrie", "ntdll: Do a device check before returning a default serial port name.", 1 },
    { "Alex Henrie", "winex11: Send SC_KEYMENU on managed windows.", 1 },
    { "Alex Villacís Lasso", "user.exe16: Fix handling of DDL_DRIVES flag in DlgDirList.", 1 },
    { "Alistair Leslie-Hughes", "comctl32/tooltip: Protect TTM_ADDTOOLW from invalid text pointers.", 1 },
    { "Alistair Leslie-Hughes", "d3dx9_36: ID3DXFont_DrawText calc_rect can be null.", 1 },
    { "Alistair Leslie-Hughes", "d3dx9_36: Implement D3DXGetShaderOutputSemantics.", 2 },
    { "Alistair Leslie-Hughes", "d3dx9_36: Return a mesh in D3DXCreateTeapot.", 1 },
    { "Alistair Leslie-Hughes", "d3dx9_36: Support NULL terminated strings in ID3DXFont_DrawText.", 1 },
    { "Alistair Leslie-Hughes", "d3dx9: Implement D3DXComputeTangent.", 1 },
    { "Alistair Leslie-Hughes", "oleaut32: Implement semi-stub for CreateTypeLib.", 1 },
    { "André Hentschel", "sfnt2fon: Don't leak output name if specified multiple times (Coverity).", 1 },
    { "André Hentschel", "winedump: Free debug string in case it was not freed in for-loop (Coverity).", 1 },
    { "André Hentschel", "wpcap: Load libpcap dynamically.", 1 },
    { "Andrey Gusev", "d3dx9_36: Add D3DXFrameFind stub.", 1 },
    { "Austin English", "wininet: Add ParseX509EncodedCertificateForListBoxEntry stub.", 2 },
    { "Bernhard Reiter", "imagehlp: Implement parts of BindImageEx to make freezing Python scripts work.", 1 },
    { "Charles Davis", "crypt32: Skip unknown item when decoding a CMS certificate.", 1 },
    { "Christian Costa", "crypt32: Print CryptUnprotectMemory FIXME only once.", 1 },
    { "Christian Costa", "d3d9/tests: Avoid crash when surface and texture creation fails.", 1 },
    { "Christian Costa", "d3dx9_36: Add dxtn support.", 1 },
    { "Christian Costa", "d3dx9_36: Add format description for X8L8V8U8 for format conversions.", 1 },
    { "Christian Costa", "d3dx9_36: Add stub for D3DXComputeNormalMap.", 1 },
    { "Christian Costa", "d3dx9_36: Add support for FOURCC surface to save_dds_surface_to_memory.", 1 },
    { "Christian Costa", "d3dx9_36: Align texture dimensions to block size for compressed textures in D3DXCheckTextureRequirements.", 1 },
    { "Christian Costa", "d3dx9_36: Filter out D3DCompile warning messages that are not present with D3DCompileShader.", 4 },
    { "Christian Costa", "d3dx9_36: Fix horizontal centering in ID3DXFont_DrawText.", 1 },
    { "Christian Costa", "d3dx9_36: Implement D3DXDisassembleShader.", 2 },
    { "Christian Costa", "d3dx9_36: Implement D3DXGetShaderInputSemantics + tests.", 3 },
    { "Christian Costa", "d3dx9_36: Implement ID3DXEffect_FindNextValidTechnique + add tests.", 1 },
    { "Christian Costa", "d3dx9_36: Implement ID3DXFontImpl_DrawText.", 1 },
    { "Christian Costa", "d3dx9_36: Implement ID3DXSkinInfoImpl_UpdateSkinnedMesh.", 1 },
    { "Christian Costa", "d3dx9_36: Improve D3DXSaveTextureToFile to save simple texture to dds file.", 1 },
    { "Christian Costa", "d3dx9_36: No need to fail if we don't support vertices reordering in D3DXMESHOPT_ATTRSORT.", 1 },
    { "Christian Costa", "d3dx9_36: Recognize bump luminance X8L8V8U8 when loading dds file.", 1 },
    { "Christian Costa", "d3dx9_36/tests: Add additional tests for special cases.", 1 },
    { "Christian Costa", "ddraw: Silence noisy FIXME about unimplemented D3DPROCESSVERTICES_UPDATEEXTENTS.", 1 },
    { "Christian Costa", "ntdll: Add dll override default rule for purist mode.", 1 },
    { "Christian Costa", "ntoskrnl.exe: Fix IoReleaseCancelSpinLock argument.", 1 },
    { "Christian Costa", "ntoskrnl.exe: Implement MmMapLockedPages and MmUnmapLockedPages.", 1 },
    { "Christian Costa", "quartz: Don't print FIXME for IAMFilterMiscFlags in parsers.", 1 },
    { "Christian Costa", "shdocvw: Check precisely ParseURLFromOutsideSourceX returned values in tests and make code clearer about that.", 3 },
    { "Christian Costa", "wined3d: Display FIXME for cmp function 0 only once.", 1 },
    { "Christian Costa", "wined3d: Display FIXME only once when blen op is 0.", 1 },
    { "Christian Costa", "wined3d: Improve DXTn support and export conversion functions for d3dx9_36.", 1 },
    { "Christian Costa", "wined3d: Print FIXME only once in surface_cpu_blt.", 1 },
    { "Christian Costa", "wined3d: Recognize GTX 560M card with NVIDIA driver.", 1 },
    { "Christian Costa", "wined3d: Silence noisy fixme Unrecognized stencil op 0.", 1 },
    { "Claudio Fontana", "kernel32: Allow empty profile section and key name strings.", 1 },
    { "Daniel Jelinski", "wine.inf: Add registry keys for Windows Performance Library.", 1 },
    { "Dmitry Timoshkov", "advapi32: SDDL assigns the \"AC\" abbreviation to WinBuiltinAnyPackageSid.", 1 },
    { "Dmitry Timoshkov", "advapi32/tests: Add a test that compares a well-known SID to a SID created from a SDDL abbreviation.", 1 },
    { "Dmitry Timoshkov", "avifil32: Add support for AVIFile interface proxies.", 2 },
    { "Dmitry Timoshkov", "comdlg32: Postpone setting ofn->lpstrFileTitle to work around an application bug.", 1 },
    { "Dmitry Timoshkov", "gdi32: ExtTextOut on a path with bitmap font selected shouldn't fail.", 1 },
    { "Dmitry Timoshkov", "gdi32: Improve detection of symbol charset for old truetype fonts.", 1 },
    { "Dmitry Timoshkov", "gdi32/tests: Add a simple test for printing to a PostScript device.", 1 },
    { "Dmitry Timoshkov", "gdi32/tests: Add some additional tests for ExtExtOut on a path for an EMF DC.", 2 },
    { "Dmitry Timoshkov", "gdi32/tests: Explicitly test BeginPath() return value in the metafile path tests.", 1 },
    { "Dmitry Timoshkov", "gdi32: Trace full contents of DOCINFO in StartDoc.", 1 },
    { "Dmitry Timoshkov", "gdiplus: Add support for more image color formats.", 1 },
    { "Dmitry Timoshkov", "gdiplus: Change multiplications by additions in the x/y scaler loops.", 1 },
    { "Dmitry Timoshkov", "gdiplus: Change the order of x/y loops in the scaler.", 1 },
    { "Dmitry Timoshkov", "gdiplus: Fix a typo in GIF container format passed to encode_image_wic().", 1 },
    { "Dmitry Timoshkov", "gdiplus: Force conversion of 8 bpp grayscale PNG images to 32 bpp BGRA.", 1 },
    { "Dmitry Timoshkov", "gdiplus: Ignore an externally set DC clipping region.", 1 },
    { "Dmitry Timoshkov", "gdiplus: Ignore an externally set DC origin.", 1 },
    { "Dmitry Timoshkov", "gdiplus: Implement GdipInitializePalette.", 2 },
    { "Dmitry Timoshkov", "gdiplus: Prefer using pre-multiplied ARGB data in the scaler.", 1 },
    { "Dmitry Timoshkov", "gdiplus: Remove ceilf/floorf calls from bilinear scaler.", 2 },
    { "Dmitry Timoshkov", "gdiplus: Set correct color space flags for grayscale images.", 1 },
    { "Dmitry Timoshkov", "gdiplus/tests: Add a test for image flags to PNG grayscale image tests.", 1 },
    { "Dmitry Timoshkov", "gdiplus/tests: Add a test for loading PNG grayscale images.", 1 },
    { "Dmitry Timoshkov", "gdiplus/tests: Add some tests for GdipInitializePalette.", 2 },
    { "Dmitry Timoshkov", "gdiplus/tests: Add some tests for loading TIFF images in various color formats.", 1 },
    { "Dmitry Timoshkov", "include: Fix the GUID_WICPixelFormat32bppCMYK definition.", 1 },
    { "Dmitry Timoshkov", "include: Make stdole32.idl a public component.", 1 },
    { "Dmitry Timoshkov", "kernel32: Add a bunch of kernel32 stubs.", 1 },
    { "Dmitry Timoshkov", "kernel32: GetLongPathName should fail when called with a wildcard.", 1 },
    { "Dmitry Timoshkov", "kernel32: GetShortPathName should fail when called with a wildcard.", 1 },
    { "Dmitry Timoshkov", "kernel32: Implement K32GetMappedFileName.", 2 },
    { "Dmitry Timoshkov", "kernel32: MoveFile should make sure that it has the delete access to the source file.", 1 },
    { "Dmitry Timoshkov", "kernel32: MoveFile(source, source) should succeed.", 1 },
    { "Dmitry Timoshkov", "kernel32: On process entry store PEB address in %ebx.", 1 },
    { "Dmitry Timoshkov", "kernel32/tests: Add a PE test image that resembles format of some of 8k demos.", 1 },
    { "Dmitry Timoshkov", "kernel32/tests: Add some tests for GetLongPathName/GetShortPathName called with a wildcard.", 1 },
    { "Dmitry Timoshkov", "kernel32/tests: Add tests for NtQuerySection.", 2 },
    { "Dmitry Timoshkov", "kernel32/tests: Fix a bunch of access denied errors by using a dedicated TEMP directory.", 1 },
    { "Dmitry Timoshkov", "kernel32/tests: Fix a module reference leak leading to an undeletable temporary file.", 1 },
    { "Dmitry Timoshkov", "kernel32/tests: Fix compilation with PSDK.", 1 },
    { "Dmitry Timoshkov", "kernel32/tests: Move the MoveFile test where it belongs.", 1 },
    { "Dmitry Timoshkov", "libs: Fix most problems with CompareString.", 1 },
    { "Dmitry Timoshkov", "libs/wine: Allow to modify reserved LDT entries.", 1 },
    { "Dmitry Timoshkov", "ntdll: Avoid race-conditions with write watches in NtReadFile.", 1 },
    { "Dmitry Timoshkov", "ntdll: If PE image size is larger than the backed file size then treat file as removable.", 1 },
    { "Dmitry Timoshkov", "ntdll: Implement NtQueryVirtualMemory(MemorySectionName).", 2 },
    { "Dmitry Timoshkov", "ntdll: Implement NtSetLdtEntries.", 1 },
    { "Dmitry Timoshkov", "ntdll: Reserve some more stack space in call_thread_func_wrapper.", 1 },
    { "Dmitry Timoshkov", "ole32: Add a check for hglobal pointer to GetHGlobalFromStream.", 1 },
    { "Dmitry Timoshkov", "ole32: Add a wrapper for memory block managed by HGLOBAL based IStream.", 1 },
    { "Dmitry Timoshkov", "ole32: Allow moving a being reallocated block of memory managed by HGLOBAL based IStream.", 1 },
    { "Dmitry Timoshkov", "ole32/tests: Add a bunch of tests for HGLOBAL based IStream::Clone.", 1 },
    { "Dmitry Timoshkov", "oleaut32: Accept DISP_E_PARAMNOTFOUND for missing optional parameters in ITypeInfo::Invoke implementation.", 3 },
    { "Dmitry Timoshkov", "oleaut32: Add an arguments check to LoadTypeLibEx.", 1 },
    { "Dmitry Timoshkov", "oleaut32: Add support for decoding SLTG function help strings.", 1 },
    { "Dmitry Timoshkov", "oleaut32: Add support for decoding SLTG variable help strings.", 1 },
    { "Dmitry Timoshkov", "oleaut32: Add support for loading and saving EMF to IPicture interface.", 1 },
    { "Dmitry Timoshkov", "oleaut32: Do not reimplement OleLoadPicture in OleLoadPicturePath.", 1 },
    { "Dmitry Timoshkov", "oleaut32: Extend a vtable offset before calling 64-bit DispCallFunc() for a 32-bit typelib.", 1 },
    { "Dmitry Timoshkov", "oleaut32: Factor out stream creation from OleLoadPicturePath.", 1 },
    { "Dmitry Timoshkov", "oleaut32: Fix calling function with instance and VARIANT return type.", 2 },
    { "Dmitry Timoshkov", "oleaut32: Fix logic for deciding whether type description follows the name.", 2 },
    { "Dmitry Timoshkov", "oleaut32: Implement a better stub for IPicture::SaveAsFile.", 1 },
    { "Dmitry Timoshkov", "oleaut32: Implement decoding of SLTG help strings.", 1 },
    { "Dmitry Timoshkov", "oleaut32: Implement OleLoadPictureFile.", 2 },
    { "Dmitry Timoshkov", "oleaut32: Make OleLoadPicture load DIBs using WIC decoder.", 1 },
    { "Dmitry Timoshkov", "oleaut32: OleLoadPicture should create a DIB section for a being loaded bitmap.", 3 },
    { "Dmitry Timoshkov", "oleaut32/tests: Add some tests for loading and saving EMF using IPicture interface.", 1 },
    { "Dmitry Timoshkov", "riched20/tests: Add a test to see what richedit class flavours should be available.", 1 },
    { "Dmitry Timoshkov", "server: All fields up to CheckSum are mandatory regardless of SizeOfOptionalHeader value.", 1 },
    { "Dmitry Timoshkov", "server: CreateFileMapping should not fail without SEC_COMMIT for a named file section.", 1 },
    { "Dmitry Timoshkov", "shell32: Add more Tango icons to the IE toolbar.", 1 },
    { "Dmitry Timoshkov", "shell32: Add toolbar bitmaps compatible with IE6.", 1 },
    { "Dmitry Timoshkov", "shell32: Implement SHCreateSessionKey.", 1 },
    { "Dmitry Timoshkov", "tools/wrc: Fix generation of custom dialog control data.", 1 },
    { "Dmitry Timoshkov", "user32: Add a check if the menu text is a valid string.", 1 },
    { "Dmitry Timoshkov", "user32: Add a workaround for Windows 3.1 apps which call LoadImage(LR_LOADFROMFILE) with a resource id.", 2 },
    { "Dmitry Timoshkov", "user32: Add support for navigating a group of radio buttons using a keyboard.", 1 },
    { "Dmitry Timoshkov", "user32: Add support for PNG icons.", 5 },
    { "Dmitry Timoshkov", "user32: Always restore previously selected font in the button painting helpers.", 1 },
    { "Dmitry Timoshkov", "user32: Before asking a WM to activate a window make sure that the window is in foreground and not minimized.", 1 },
    { "Dmitry Timoshkov", "user32: DialogBoxParam should return -1 when dialog control creation fails.", 1 },
    { "Dmitry Timoshkov", "user32: Do not initialize dialog info for every window passed to DefDlgProc.", 1 },
    { "Dmitry Timoshkov", "user32: Don't force a combobox repaint on WM_SIZE.", 1 },
    { "Dmitry Timoshkov", "user32: Fix order of items passed in WM_COMPAREITEM data.", 1 },
    { "Dmitry Timoshkov", "user32: Fix return value of ScrollWindowEx for invisible windows.", 1 },
    { "Dmitry Timoshkov", "user32: Fix the listbox sorting algorithm.", 1 },
    { "Dmitry Timoshkov", "user32: Fix the pointer to custom dialog control data.", 1 },
    { "Dmitry Timoshkov", "user32: For an owner-drawn listbox without strings WM_MEASUREITEM still needs correct itemData.", 1 },
    { "Dmitry Timoshkov", "user32: If there is no dialog controls to set focus to then set focus to dialog itself.", 1 },
    { "Dmitry Timoshkov", "user32: MessageBox should be topmost when MB_SYSTEMMODAL style is set.", 1 },
    { "Dmitry Timoshkov", "user32: Move the auto radio button group logic from BM_SETCHECK to WM_LBUTTONUP handler.", 1 },
    { "Dmitry Timoshkov", "user32: Pass correct itemData to WM_MEASUREITEM when inserting an item to an owner-drawn listbox.", 1 },
    { "Dmitry Timoshkov", "user32/tests: Add a bunch of tests for DM_SETDEFID/DM_GETDEFID handling by a DefDlgProc.", 1 },
    { "Dmitry Timoshkov", "user32/tests: Add a focus test for an empty dialog that returns TRUE in WM_INITDIALOG.", 1 },
    { "Dmitry Timoshkov", "user32/tests: Add a message test for an owner-drawn sorted listbox.", 1 },
    { "Dmitry Timoshkov", "user32/tests: Add a message test for group of radio buttons.", 1 },
    { "Dmitry Timoshkov", "user32/tests: Add a test for custom dialog control data.", 1 },
    { "Dmitry Timoshkov", "user32/tests: Add a test for navigating a group of buttons using keyboard events.", 1 },
    { "Dmitry Timoshkov", "user32/tests: Add a test for WM_MEASUREITEM when inserting an item to an owner-drawn listbox.", 2 },
    { "Dmitry Timoshkov", "user32/tests: Add some tests to see when MessageBox gains WS_EX_TOPMOST style.", 1 },
    { "Dmitry Timoshkov", "user32/tests: Simplify the test for BM_CLICK on autoradio button by using a dialog.", 1 },
    { "Dmitry Timoshkov", "user32/tests: Test DialogBoxParam using a dialog template with invalid control class.", 1 },
    { "Dmitry Timoshkov", "user32: Try harder to find a target for mouse messages.", 1 },
    { "Dmitry Timoshkov", "user32: Use root dialog for DM_SETDEFID/DM_GETDEFID in DefDlgProc.", 1 },
    { "Dmitry Timoshkov", "uxtheme: Protect CloseThemeData() from invalid input.", 1 },
    { "Dmitry Timoshkov", "widl: Add initial implementation of SLTG typelib generator.", 1 },
    { "Dmitry Timoshkov", "widl: Add support for function parameter flags to SLTG typelib generator.", 1 },
    { "Dmitry Timoshkov", "widl: Add support for inherited interfaces to SLTG typelib generator.", 1 },
    { "Dmitry Timoshkov", "widl: Add support for interfaces to SLTG typelib generator.", 1 },
    { "Dmitry Timoshkov", "widl: Add support for recursive type references to SLTG typelib generator.", 1 },
    { "Dmitry Timoshkov", "widl: Add support for structures.", 1 },
    { "Dmitry Timoshkov", "widl: Add support for VT_USERDEFINED to SLTG typelib generator.", 1 },
    { "Dmitry Timoshkov", "widl: Add support for VT_VOID and VT_VARIANT to SLTG typelib generator.", 1 },
    { "Dmitry Timoshkov", "widl: Avoid relying on side effects when marking function index as the last one.", 1 },
    { "Dmitry Timoshkov", "widl: Calculate size of instance for structures.", 1 },
    { "Dmitry Timoshkov", "widl: Create library block index right after the CompObj one.", 1 },
    { "Dmitry Timoshkov", "widl: Factor out SLTG tail initialization.", 1 },
    { "Dmitry Timoshkov", "widl: Fix generation of resources containing an old typelib.", 1 },
    { "Dmitry Timoshkov", "widl: Make automatic dispid generation scheme better match what midl does.", 1 },
    { "Dmitry Timoshkov", "widl: Minor/cosmetic clean up.", 1 },
    { "Dmitry Timoshkov", "widl: More accurately report variable descriptions data size.", 1 },
    { "Dmitry Timoshkov", "widl: Properly align name table entries.", 1 },
    { "Dmitry Timoshkov", "widl: Set the lowest bit in the param name to indicate whether type description follows the name.", 1 },
    { "Dmitry Timoshkov", "widl: Write correct syskind by SLTG typelib generator.", 1 },
    { "Dmitry Timoshkov", "widl: Write correct typekind to the SLTG typeinfo block.", 1 },
    { "Dmitry Timoshkov", "widl: Write SLTG blocks according to the index order.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Add initial implementation of the GIF encoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Add registration for GUID_WICPixelFormat32bppGrayFloat pixel format.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Add registration of the GIF encoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Add some tests for various TIFF color formats.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Add support for 128bppRGBAFloat format to TIFF decoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Add support for 12bpp RGB format to TIFF decoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Add support for 16bppGray and 32bppGrayFloat formats to TIFF decoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Add support for 16bpp RGBA format to TIFF decoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Add support for 32bppCMYK and 64bppCMYK formats to TIFF decoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Add support for 32bppRGB, 32bppRGBA and 32bppPRGBA to format converter.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Add support for 3bps RGB format to TIFF decoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Add support for 4bpp RGBA format to TIFF decoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Add support for converting to 8bppIndexed format to IWICFormatConverter.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Add support for IMILBitmapScaler interface.", 2 },
    { "Dmitry Timoshkov", "windowscodecs: Add support for palette image formats to BMP encoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Add support for palette image formats to PNG encoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Add support for palette image formats to TIFF encoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Avoid crashing if no IPropertyBag2 was passed to IWICBitmapEncoder::CreateNewFrame in JPEG encoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Avoid crashing if no IPropertyBag2 was passed to IWICBitmapEncoder::CreateNewFrame in TIFF encoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Avoid redundant checks when reading a TIFF tile.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Better follow the GIF spec and don't specify the local color table size if there is no local palette.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Correctly indicate that the global info was written even without the global palette.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Decode PNG images with a tRNS chunk in their native formats.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Fail earlier in TIFF decoder's Initialize method for unsupported pixel formats.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Find_decoder() should return an error it received from the decoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Fix 32bppGrayFloat to 8bppGray conversion.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Fix 32bppRGB to 32bppRGBA conversion.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Fix behaviour of format converter for indexed formats when NULL or empty palette has been provided.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Fix IWICBitmapDecoder::CopyPalette for a not initialized case in the GIF decoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Fix IWICBitmapEncoder::SetPalette for a not initialized case in BMP encoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Fix IWICBitmapEncoder::SetPalette for a not initialized case in PNG encoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Fix IWICBitmapEncoder::SetPalette for a not initialized case in TIFF encoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Fix the buffer size check in the TIFF decoder's IWICBitmapFrameDecode::CopyPixels implementation.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Fix the SupportsTransparency flag value for various pixel formats.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Implement IWICBitmapDecoder::CopyPalette in PNG decoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Implement IWICBitmapDecoder::CopyPalette in TIFF decoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Implement IWICBitmapDecoder::GetMetadataQueryReader in the TIFF decoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Implement IWICBitmapEncoder::GetEncoderInfo in BMP encoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Implement IWICBitmapEncoder::GetEncoderInfo in JPEG encoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Implement IWICBitmapEncoder::GetEncoderInfo in the JPEG encoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Implement IWICBitmapEncoder::GetEncoderInfo in the PNG encoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Implement IWICBitmapEncoder::GetEncoderInfo in TIFF encoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Implement IWICBitmapEncoderInfo::GetFileExtensions.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Implement IWICBitmapFrameEncode::SetPalette in BMP encoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Implement IWICBitmapFrameEncode::SetPalette in JPEG encoder,.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Implement IWICBitmapFrameEncode::SetPalette in the TIFF encoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Implement IWICPalette::InitializeFromBitmap.", 5 },
    { "Dmitry Timoshkov", "windowscodecs: Implement WICCreateBitmapFromSection(Ex).", 2 },
    { "Dmitry Timoshkov", "windowscodecs: Improve compatibility of IMILBitmapSource interface.", 3 },
    { "Dmitry Timoshkov", "windowscodecs: Improve stub for IWICBitmapDecoder::GetMetadataQueryReader in the PNG decoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Improve stub for IWICMetadataQueryReader::GetMetadataByName.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Initialize empty property bag in GIF encoder's CreateNewFrame implementation.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Limit number of colors in a palette in BMP decoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: PNG decoder should return WINCODEC_ERR_UNKNOWNIMAGEFORMAT when image loading fails.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: PNG decoder should use indexed formats for grayscale images when a PLTE chunk exists.", 1 },
    { "Dmitry Timoshkov", "windowscodecs/tests: Add a bunch of new tests for indexed format conversions.", 1 },
    { "Dmitry Timoshkov", "windowscodecs/tests: Add a missing check for IWICBitmapFrameDecode::GetPixelFormat return value.", 1 },
    { "Dmitry Timoshkov", "windowscodecs/tests: Add a test for 8bpp indexed TIFF format.", 1 },
    { "Dmitry Timoshkov", "windowscodecs/tests: Add a test for loading PNG grayscale images.", 1 },
    { "Dmitry Timoshkov", "windowscodecs/tests: Add more tests for loading PNG images in various color formats.", 1 },
    { "Dmitry Timoshkov", "windowscodecs/tests: Add some tests for converting 24bppBGR to 8bppIndexed format.", 1 },
    { "Dmitry Timoshkov", "windowscodecs/tests: Add some tests for encoding 1bpp/8bpp images with a palette.", 1 },
    { "Dmitry Timoshkov", "windowscodecs/tests: Add some tests for IWICPalette::InitializeFromBitmap.", 2 },
    { "Dmitry Timoshkov", "windowscodecs/tests: Add tests for encoding 2bpp/4bpp images with a palette.", 1 },
    { "Dmitry Timoshkov", "windowscodecs/tests: Add the tests for GIF encoder and decoder.", 1 },
    { "Dmitry Timoshkov", "windowscodecs/tests: Make create_decoder() return an error code in PNG tests.", 1 },
    { "Dmitry Timoshkov", "windowscodecs/tests: Make the test for 8bpp indexed TIFF format run under XP.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Tolerate partial reads in the IFD metadata loader.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Use V_UI1() instead of V_UNION() to assign a VT_UI1 variant member.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: WICConvertBitmapSource should ask IWICFormatConverter::Initialize to use an optimized palette.", 1 },
    { "Dmitry Timoshkov", "windowscodecs: Write the image bits as a bottom-top array in BMP encoder.", 1 },
    { "Dmitry Timoshkov", "wine.inf: Add \"ProfileImagePath\" with the user's profile directory under ProfileList key.", 1 },
    { "Dmitry Timoshkov", "wineps.drv: Add stubs for escapes required by Adobe PageMaker.", 1 },
    { "Dmitry Timoshkov", "wineps.drv: Add support for GETFACENAME and DOWNLOADFACE escapes.", 1 },
    { "Dmitry Timoshkov", "wineps.drv: PostScript header should be written by StartDoc instead of StartPage.", 1 },
    { "Dmitry Timoshkov", "winex11.drv: Add support for _NET_ACTIVE_WINDOW.", 2 },
    { "Dmitry Timoshkov", "winex11.drv: Don't add MWM_DECOR_BORDER to windows without a caption.", 1 },
    { "Dmitry Timoshkov", "winex11.drv: Don't use MWM_DECOR_RESIZEH, window resizing is controlled by MWM_FUNC_RESIZE.", 1 },
    { "Dmitry Timoshkov", "winex11.drv: Send WM_WINDOWPOSCHANGING/WM_WINDOWPOSCHANGED messages to a being deactivated topmost window.", 1 },
    { "Dmitry Timoshkov", "winex11: Fix handling of window attributes for WS_EX_LAYERED | WS_EX_COMPOSITED.", 1 },
    { "Erich E. Hoover", "advapi32: Fix the initialization of combined DACLs when the new DACL is empty.", 1 },
    { "Erich E. Hoover", "advapi32: Move the DACL combining code into a separate routine.", 1 },
    { "Erich E. Hoover", "Appease the blessed version of gcc (4.5) when -Werror is enabled.", 1 },
    { "Erich E. Hoover", "dsound: Add stub support for DSPROPSETID_EAX20_BufferProperties.", 1 },
    { "Erich E. Hoover", "dsound: Add stub support for DSPROPSETID_EAX20_ListenerProperties.", 1 },
    { "Erich E. Hoover", "fonts: Add WenQuanYi Micro Hei as a Microsoft Yahei replacement.", 1 },
    { "Erich E. Hoover", "iphlpapi: Implement AllocateAndGetTcpExTableFromStack.", 2 },
    { "Erich E. Hoover", "kernel32: Add SearchPath test demonstrating the priority of the working directory.", 1 },
    { "Erich E. Hoover", "kernel32: Advertise junction point support.", 1 },
    { "Erich E. Hoover", "kernel32: Consider the working directory first when launching executables with CreateProcess.", 1 },
    { "Erich E. Hoover", "kernel32: NeedCurrentDirectoryForExePath does not use the registry.", 1 },
    { "Erich E. Hoover", "kernel32,ntdll: Add support for deleting junction points with RemoveDirectory.", 1 },
    { "Erich E. Hoover", "libport: Add support for FreeBSD style extended attributes.", 1 },
    { "Erich E. Hoover", "libport: Add support for Mac OS X style extended attributes.", 1 },
    { "Erich E. Hoover", "msi: Add support for deleting streams from an MSI database.", 1 },
    { "Erich E. Hoover", "msi: Add support for exporting binary streams (Binary/Icon tables).", 1 },
    { "Erich E. Hoover", "msi: Add support for exporting the _SummaryInformation table.", 1 },
    { "Erich E. Hoover", "msi: Break out field exporting into a separate routine.", 1 },
    { "Erich E. Hoover", "msidb: Add stub tool for manipulating MSI databases.", 1 },
    { "Erich E. Hoover", "msidb: Add support for adding stream/cabinet files to MSI databases.", 1 },
    { "Erich E. Hoover", "msidb: Add support for exporting database tables.", 1 },
    { "Erich E. Hoover", "msidb: Add support for exporting with short (DOS) filenames.", 1 },
    { "Erich E. Hoover", "msidb: Add support for extracting stream/cabinet files from MSI databases.", 1 },
    { "Erich E. Hoover", "msidb: Add support for importing database tables.", 1 },
    { "Erich E. Hoover", "msidb: Add support for removing stream/cabinet files from MSI databases.", 1 },
    { "Erich E. Hoover", "msidb: Add support for wildcard (full database) export.", 1 },
    { "Erich E. Hoover", "msi: Return an error when MsiDatabaseImport is passed an invalid pathname.", 1 },
    { "Erich E. Hoover", "ntdll: Add a test for junction point advertisement.", 1 },
    { "Erich E. Hoover", "ntdll: Add support for deleting junction points.", 1 },
    { "Erich E. Hoover", "ntdll: Add support for junction point creation.", 1 },
    { "Erich E. Hoover", "ntdll: Add support for reading junction points.", 1 },
    { "Erich E. Hoover", "ntdll: Implement retrieving DOS attributes in NtQuery[Full]AttributesFile and NtQueryDirectoryFile.", 1 },
    { "Erich E. Hoover", "ntdll: Implement retrieving DOS attributes in NtQueryInformationFile.", 1 },
    { "Erich E. Hoover", "ntdll: Implement storing DOS attributes in NtCreateFile.", 1 },
    { "Erich E. Hoover", "ntdll: Implement storing DOS attributes in NtSetInformationFile.", 1 },
    { "Erich E. Hoover", "ntdll: Perform the Unix-style hidden file check within the unified file info grabbing routine.", 1 },
    { "Erich E. Hoover", "ntdll/tests: Add test for deleting junction point target.", 1 },
    { "Erich E. Hoover", "quartz: Implement MediaSeeking_GetCurrentPosition on top of MediaSeeking_GetPositions.", 1 },
    { "Erich E. Hoover", "quartz: Implement MediaSeeking_GetStopPosition on top of MediaSeeking_GetPositions.", 1 },
    { "Erich E. Hoover", "quartz: Include the stream position in addition to the reference clock offset in the time returned by MediaSeeking_GetPositions.", 1 },
    { "Erich E. Hoover", "quartz: Remove unused cache of MediaSeeking stop position.", 1 },
    { "Erich E. Hoover", "server: Add default security descriptor DACL for processes.", 1 },
    { "Erich E. Hoover", "server: Add default security descriptor ownership for processes.", 1 },
    { "Erich E. Hoover", "server: Convert return of file security masks with generic access mappings.", 7 },
    { "Erich E. Hoover", "server: Inherit security attributes from parent directories on creation.", 7 },
    { "Erich E. Hoover", "server: Retrieve file security attributes with extended file attributes.", 7 },
    { "Erich E. Hoover", "server: Store file security attributes with extended file attributes.", 8 },
    { "Erich E. Hoover", "server: Unify the retrieval of security attributes for files and directories.", 7 },
    { "Erich E. Hoover", "server: Unify the storage of security attributes for files and directories.", 7 },
    { "Erich E. Hoover", "strmbase: Fix MediaSeekingPassThru_GetPositions return when the pins are unconnected.", 1 },
    { "Erich E. Hoover", "wined3d: Silence repeated resource_check_usage FIXME.", 2 },
    { "Erich E. Hoover", "ws2_32: Add support for TF_DISCONNECT to TransmitFile.", 1 },
    { "Erich E. Hoover", "ws2_32: Add support for TF_REUSE_SOCKET to TransmitFile.", 1 },
    { "Felix Yan", "winex11.drv: Update a candidate window's position with over-the-spot style.", 2 },
    { "Hans Leidekker", "bcrypt: Add AES provider.", 1 },
    { "Hao Peng", "winecfg: Double click in dlls list to edit item's overides.", 3 },
    { "Henri Verbeet", "ddraw: Implement ddraw7_FlipToGDISurface.", 2 },
    { "Hermes Belusca-Maito", "setupapi/tests: Determine path to system32 directory at runtime.", 1 },
    { "Hermès BÉLUSCA-MAÏTO", "shlwapi: Fix the return value of SHAddDataBlock.", 1 },
    { "Ivan Akulinchev", "uxthemegtk: Initial implementation.", 1 },
    { "Jactry Zeng", "riched20: Fix ME_RunOfsFromCharOfs() when nCharOfs > strlen().", 1 },
    { "Jactry Zeng", "riched20: Implement ITextRange::GetStoryLength.", 1 },
    { "Jactry Zeng", "riched20: Implement ITextRange::GetText.", 1 },
    { "Jactry Zeng", "riched20: Implement ITextRange::IsEqual.", 1 },
    { "Jactry Zeng", "riched20: Implement ITextRange::SetRange.", 1 },
    { "Jactry Zeng", "riched20: Implement ITextSelection::GetStoryLength.", 1 },
    { "Jactry Zeng", "riched20: Implement IText{Selection, Range}::Set{Start, End}.", 1 },
    { "Jactry Zeng", "riched20: Stub for ITextFont interface and implement ITextRange::GetFont and ITextSelection::GetFont.", 1 },
    { "Jactry Zeng", "riched20: Stub for ITextPara interface and implement ITextRange::GetPara.", 1 },
    { "Jarkko Korpi", "kernel32: Silence repeated LocaleNameToLCID unsupported flags message.", 1 },
    { "Jarkko Korpi", "ntdll: Silence FIXME in RtlCaptureStackBackTrace stub function.", 1 },
    { "Jarkko Korpi", "ntoskrnl.exe: Add IoGetDeviceAttachmentBaseRef stub.", 1 },
    { "Jarkko Korpi", "wininet: Silence wininet no support on this platform message.", 1 },
    { "Jianqiu Zhang", "ntdll: Add support for FileFsFullSizeInformation class in NtQueryVolumeInformationFile.", 2 },
    { "Jianqiu Zhang", "wpcap: Implement pcap_dump_open and pcap_dump.", 1 },
    { "Joakim Hernberg", "wineserver: Draft to implement priority levels through POSIX scheduling policies on linux.", 1 },
    { "Joris van der Wel", "advapi32/tests: Add additional tests for passing a thread sd to CreateProcess.", 1 },
    { "Ken Thomases", "gdi32: Also accept \"\\\\.\\DISPLAY<n>\" devices names with <n> other than 1 as display devices.", 1 },
    { "Ken Thomases", "user32: Implement EnumDisplayDevicesW() based on EnumDisplayMonitors() and GetMonitorInfoW().", 1 },
    { "Ken Thomases", "winemac: Make GetMonitorInfo() give a different device name (\\\\.\\DISPLAY<n>) to each monitor.", 1 },
    { "Ken Thomases", "winex11: Make GetMonitorInfo() give a different device name (\\.\\DISPLAY<n>) to each monitor.", 1 },
    { "Kimmo Myllyvirta", "d3d11: Add stub deferred rendering context.", 1 },
    { "Louis Lenders", "include: Add headerfile virtdisk.h.", 1 },
    { "Louis Lenders", "ntdll: Improve stub for NtAllocateUuids.", 1 },
    { "Mark Harmstone", "dsound: Add delay line EAX functions.", 1 },
    { "Mark Harmstone", "dsound: Add EAX init and free stubs.", 1 },
    { "Mark Harmstone", "dsound: Add EAX presets.", 1 },
    { "Mark Harmstone", "dsound: Add EAX propset stubs.", 1 },
    { "Mark Harmstone", "dsound: Add EAX v1 constants and structs.", 1 },
    { "Mark Harmstone", "dsound: Add EAX VerbPass stub.", 1 },
    { "Mark Harmstone", "dsound: Allocate EAX delay lines.", 1 },
    { "Mark Harmstone", "dsound: Feed data through EAX function.", 1 },
    { "Mark Harmstone", "dsound: Implement EAX decorrelator.", 1 },
    { "Mark Harmstone", "dsound: Implement EAX early reflections.", 1 },
    { "Mark Harmstone", "dsound: Implement EAX late all-pass filter.", 1 },
    { "Mark Harmstone", "dsound: Implement EAX late reverb.", 1 },
    { "Mark Harmstone", "dsound: Implement EAX lowpass filter.", 1 },
    { "Mark Harmstone", "dsound: Report that we support EAX.", 1 },
    { "Mark Harmstone", "dsound: Support getting and setting EAX buffer properties.", 1 },
    { "Mark Harmstone", "dsound: Support getting and setting EAX properties.", 1 },
    { "Mark Harmstone", "winecfg: Add checkbox to enable/disable EAX support.", 1 },
    { "Mark Harmstone", "winepulse: Expose audio devices directly to programs.", 1 },
    { "Mark Harmstone", "winepulse: Fetch actual program name if possible.", 1 },
    { "Mark Harmstone", "winepulse: Fix segfault in pulse_rd_loop.", 1 },
    { "Mark Harmstone", "winepulse: Implement exclusive mode.", 1 },
    { "Mark Harmstone", "winepulse: Implement GetPropValue.", 1 },
    { "Mark Harmstone", "winepulse: Return PKEY_AudioEndpoint_PhysicalSpeakers device prop.", 1 },
    { "Mark Jansen", "imagehlp/tests: Add tests for ImageLoad, ImageUnload, GetImageUnusedHeaderBytes.", 1 },
    { "Mark Jansen", "imagehlp/tests: Msvc compatibility fixes.", 1 },
    { "Mark Jansen", "msi: Do not sign extend after multiplying.", 1 },
    { "Mark Jansen", "ntdll/tests: Add tests for RtlIpv6AddressToString and RtlIpv6AddressToStringEx.", 1 },
    { "Mark Jansen", "ntdll/tests: Tests for RtlIpv4StringToAddressEx (try 5, resend).", 1 },
    { "Mark Jansen", "ntdll/tests: Tests for RtlIpv6StringToAddress.", 6 },
    { "Mark Jansen", "ntdll/tests: Tests for RtlIpv6StringToAddressEx.", 6 },
    { "Mark Jansen", "shlwapi: Implement AssocGetPerceivedType.", 2 },
    { "Mark Jansen", "shlwapi/tests: Add tests for AssocGetPerceivedType.", 1 },
    { "Mark Jansen", "version: Test for VerQueryValueA.", 2 },
    { "Mark Jansen", "wintrust/tests: Add tests for WinVerifyTrust.", 2 },
    { "Mark Jansen", "wintrust: Verify image hash in WinVerifyTrust.", 2 },
    { "Matt Durgavich", "ws2_32: Proper WSACleanup implementation using wineserver function.", 2 },
    { "Michael Müller", "Add licenses for fonts as separate files.", 1 },
    { "Michael Müller", "advapi32: Add stub for LsaLookupPrivilegeDisplayName.", 1 },
    { "Michael Müller", "advapi32: Fix error code when calling LsaOpenPolicy for non existing remote machine.", 1 },
    { "Michael Müller", "advapi32: Implement GetExplicitEntriesFromAclW.", 1 },
    { "Michael Müller", "advapi32: Implement LsaLookupPrivilegeName.", 1 },
    { "Michael Müller", "advapi32/tests: Add basic tests for token security descriptors.", 1 },
    { "Michael Müller", "advapi32/tests: Show that tokens do not inherit dacls while creating child processes.", 1 },
    { "Michael Müller", "advapi32/tests: Show that tokens do not inherit sacls / mandatory labels while creating child processes.", 1 },
    { "Michael Müller", "advapi32/tests: Show that tokens do not inherit security descriptors during duplication.", 1 },
    { "Michael Müller", "advapi32: Use TRACE for LsaOpenPolicy/LsaClose.", 1 },
    { "Michael Müller", "api-ms-win-core-heap-l2-1-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-core-shlwapi-obsolete-l1-2-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-rtcore-ntuser-draw-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-rtcore-ntuser-window-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-shcore-obsolete-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-shcore-stream-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-shcore-thread-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "avifil32: Correctly handle compressed frames when desired format is specified.", 1 },
    { "Michael Müller", "avifile.dll16: Convert between AVISTREAMINFO (16 bit) and AVISTREAMINFOA.", 1 },
    { "Michael Müller", "avifile.dll16: Correctly convert result of AVIStreamGetFrame to a segptr.", 1 },
    { "Michael Müller", "bcrypt: Add support for auth data in AES GCM mode.", 1 },
    { "Michael Müller", "bcrypt: Add support for computing/comparing cipher tag.", 1 },
    { "Michael Müller", "bcrypt: Allow to call BCryptSetProperty on key objects.", 1 },
    { "Michael Müller", "bcrypt: Fix handling of padding when input size equals block size for AES.", 1 },
    { "Michael Müller", "bcrypt: Fix use-after-free in key_init.", 1 },
    { "Michael Müller", "bcrypt: Implement BCryptDecrypt for AES GCM mode.", 1 },
    { "Michael Müller", "bcrypt: Implement BCryptDuplicateKey.", 1 },
    { "Michael Müller", "bcrypt: Implement BCryptEncrypt for AES GCM mode.", 1 },
    { "Michael Müller", "bcrypt: Implement BCryptGetProperty for BCRYPT_AUTH_TAG_LENGTH.", 1 },
    { "Michael Müller", "bcrypt: Implement BCryptGetProperty for BCRYPT_CHAINING_MODE.", 1 },
    { "Michael Müller", "bcrypt: Implement BCryptSetProperty for algorithms.", 1 },
    { "Michael Müller", "bcrypt: Properly handle padding in AES decryption.", 1 },
    { "Michael Müller", "bcrypt/tests: Add tests for AES GCM mode.", 1 },
    { "Michael Müller", "browseui: Implement IProgressDialog::SetAnimation.", 1 },
    { "Michael Müller", "browseui: Implement PROGDLG_AUTOTIME flag for IProgressDialog.", 1 },
    { "Michael Müller", "combase: Add stub for CleanupTlsOleState.", 1 },
    { "Michael Müller", "combase: Add stub for RoGetApartmentIdentifier.", 1 },
    { "Michael Müller", "combase: Add stub for RoGetServerActivatableClasses.", 1 },
    { "Michael Müller", "combase: Add stub for RoRegisterActivationFactories.", 1 },
    { "Michael Müller", "combase: Add stub for RoRegisterForApartmentShutdown.", 1 },
    { "Michael Müller", "combase: Implement RoActivateInstance.", 1 },
    { "Michael Müller", "combase: Implement RoGetActivationFactory.", 1 },
    { "Michael Müller", "comctl32: Preserve custom colors between subitems.", 2 },
    { "Michael Müller", "crypt32: Add MS root CA 2010/2011.", 1 },
    { "Michael Müller", "crypt32: Properly check root certificate in CERT_CHAIN_REVOCATION_CHECK_CHAIN.", 1 },
    { "Michael Müller", "crypt32: Recognize sha256/384/512 KEY OIDs.", 1 },
    { "Michael Müller", "d3d11: Add a hack to prevent creation of 1d cube textures.", 1 },
    { "Michael Müller", "d3d11: Add d3d11_texture1d_SetPrivateDataInterface.", 1 },
    { "Michael Müller", "d3d11: Add dummy support for D3D11_QUERY_PIPELINE_STATISTICS query.", 1 },
    { "Michael Müller", "d3d11: Add stub ID3D11Texture2D and ID3D10Texture2D interfaces.", 1 },
    { "Michael Müller", "d3d11: Add support for 1d textures in normalize_rtv_desc.", 1 },
    { "Michael Müller", "d3d11: Add support for 1d textures in normalize_srv_desc.", 1 },
    { "Michael Müller", "d3d11: Create a private store in d3d_texture1d_init.", 1 },
    { "Michael Müller", "d3d11: Create a texture in d3d_texture1d_init.", 1 },
    { "Michael Müller", "d3d11: Generate dxgi surface in d3d_texture1d_init.", 1 },
    { "Michael Müller", "d3d11: Implement ClearDepthStencilView for deferred contexts.", 1 },
    { "Michael Müller", "d3d11: Implement ClearRenderTargetView for deferred contexts.", 1 },
    { "Michael Müller", "d3d11: Implement CSSetConstantBuffers for deferred contexts.", 1 },
    { "Michael Müller", "d3d11: Implement CSSetShader for deferred contexts.", 1 },
    { "Michael Müller", "d3d11: Implement CSSetUnorderedAccessViews for deferred contexts.", 1 },
    { "Michael Müller", "d3d11: Implement d3d10_device_ResolveSubresource in the same way as for d3d11.", 1 },
    { "Michael Müller", "d3d11: Implement d3d10_texture1d_GetDesc.", 1 },
    { "Michael Müller", "d3d11: Implement d3d10_texture1d_(Un)map.", 1 },
    { "Michael Müller", "d3d11: Implement d3d11_texture1d_{G,S}etPrivateData.", 1 },
    { "Michael Müller", "d3d11: Implement Dispatch for deferred contexts.", 1 },
    { "Michael Müller", "d3d11: Implement Draw for deferred contexts.", 1 },
    { "Michael Müller", "d3d11: Implement ID3D10Device_CheckFormatSupport.", 1 },
    { "Michael Müller", "d3d11: Implement ID3D11Device_CheckFormatSupport.", 1 },
    { "Michael Müller", "d3d11: Implement ResolveSubresource by copying sub resource (there is no multisample texture support yet).", 1 },
    { "Michael Müller", "d3d11: Improve d3d11_texture1d_GetDesc by obtaining the current width and format from wined3d.", 1 },
    { "Michael Müller", "d3d11: Initial implementation for deferred contexts.", 1 },
    { "Michael Müller", "d3d11/tests: Add some basic 1d texture tests in test_texture.", 1 },
    { "Michael Müller", "d3d11/tests: Add support for 1d textures in check_rtv_desc_.", 1 },
    { "Michael Müller", "d3d11/tests: Add support for 1d textures in check_srv_desc_.", 1 },
    { "Michael Müller", "d3d11/tests: Add test for creating 1d textures.", 1 },
    { "Michael Müller", "d3d11/tests: Prepare test_texture for 1d textures.", 1 },
    { "Michael Müller", "d3d11/tests: Prepare test_texture for non 2d textures.", 1 },
    { "Michael Müller", "d3d11/tests: Test 1d texture interfaces.", 1 },
    { "Michael Müller", "d3d11/tests: Test the creation of 1d render buffers in test_create_rendertarget_view.", 1 },
    { "Michael Müller", "d3d11/tests: Test the creation of 1d shader resource views in test_create_shader_resource_view.", 1 },
    { "Michael Müller", "d3dx9_36: Return dummy skininfo interface in D3DXLoadSkinMeshFromXof when skin information is unavailable.", 1 },
    { "Michael Müller", "d3dx9_36/tests: Add initial tests for dummy skininfo interface.", 1 },
    { "Michael Müller", "ddraw: Accept d3d1 and d3d2 versions of D3DFINDDEVICERESULT in IDirect3D_FindDevice.", 1 },
    { "Michael Müller", "ddraw: Allow size and format conversions in IDirect3DTexture2::Load.", 1 },
    { "Michael Müller", "ddraw: Create rendering targets in video memory if possible.", 1 },
    { "Michael Müller", "ddraw: Don't set HWTRANSFORMANDLIGHT flag on d3d7 RGB device.", 1 },
    { "Michael Müller", "ddraw: Implement DDENUMSURFACES_CANBECREATED flag in ddraw7_EnumSurfaces.", 1 },
    { "Michael Müller", "ddraw: Remove const from ddraw1_vtbl and ddraw_surface1_vtbl.", 1 },
    { "Michael Müller", "ddraw: Set ddsOldCaps correctly in ddraw7_GetCaps.", 1 },
    { "Michael Müller", "ddraw: Set dwZBufferBitDepth in ddraw7_GetCaps.", 1 },
    { "Michael Müller", "ddraw/tests: Add more tests for IDirect3DTexture2::Load.", 1 },
    { "Michael Müller", "ddraw/tests: Add more tests for IDirectDraw7::EnumSurfaces.", 1 },
    { "Michael Müller", "devenum: Populate AudioCompressorCategory.", 1 },
    { "Michael Müller", "dmusic: Implement IDirectMusicBuffer::PackStructured.", 1 },
    { "Michael Müller", "dxdiagn: Add several more fields for DisplayDevices.", 1 },
    { "Michael Müller", "dxdiagn: Calling GetChildContainer with an empty string on a leaf container returns the object itself.", 1 },
    { "Michael Müller", "dxdiagn: Enumerate DirectSound devices and add some basic properties.", 1 },
    { "Michael Müller", "dxgi: Implement setting and querying the gamma value of an output.", 1 },
    { "Michael Müller", "dxgi: Improve stubs for MakeWindowAssociation and GetWindowAssociation.", 1 },
    { "Michael Müller", "dxva2: Add DRM mode for vaapi.", 1 },
    { "Michael Müller", "dxva2: Always destroy buffers when calling vaRenderPicture.", 1 },
    { "Michael Müller", "dxva2: Fill h264 luma and chroma weights / offsets with default values in case they are not specified.", 1 },
    { "Michael Müller", "dxva2: Implement h264 decoder.", 1 },
    { "Michael Müller", "dxva2: Implement stubbed DirectX Software VideoProcessor interface.", 1 },
    { "Michael Müller", "dxva2: Implement stubbed interfaces for IDirectXVideo{Acceleration,Decoder,Processor}Service.", 1 },
    { "Michael Müller", "dxva2: Initial implementation of MPEG2 decoder using vaapi backend.", 1 },
    { "Michael Müller", "dxva2: Only declare debug channels when they are actually used.", 1 },
    { "Michael Müller", "dxva2/tests: Add tests for dxva2 decoder.", 1 },
    { "Michael Müller", "explorer: Create CurrentControlSet\\Control\\Video registry key as non-volatile.", 1 },
    { "Michael Müller", "ext-ms-win-appmodel-usercontext-l1-1-0: Add dll and add stub for UserContextExtInitialize.", 1 },
    { "Michael Müller", "ext-ms-win-ntuser-mouse-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "ext-ms-win-rtcore-ntuser-syscolors-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "ext-ms-win-uxtheme-themes-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "ext-ms-win-xaml-pal-l1-1-0: Add dll and add stub for XamlBehaviorEnabled.", 1 },
    { "Michael Müller", "ext-ms-win-xaml-pal-l1-1-0: Add stub for GetThemeServices.", 1 },
    { "Michael Müller", "feclient: Add stub dll.", 1 },
    { "Michael Müller", "fsutil: Add fsutil program with support for creating hard links.", 1 },
    { "Michael Müller", "gdi32: Treat lpResults as optional in GetCharacterPlacement.", 1 },
    { "Michael Müller", "hal: Implement KeQueryPerformanceCounter.", 1 },
    { "Michael Müller", "hnetcfg: Improve INetFwAuthorizedApplication::get_ProcessImageFileName stub.", 1 },
    { "Michael Müller", "iccvid: Fix calculation of stride and size.", 1 },
    { "Michael Müller", "ieframe: Return S_OK in IViewObject::Draw stub.", 1 },
    { "Michael Müller", "iertutil: Add dll and add stub for ordinal 811.", 1 },
    { "Michael Müller", "imagehlp: Catch invalid memory access in CheckSumMappedFile and add tests.", 1 },
    { "Michael Müller", "imagehlp: Fix checksum calculation for odd sizes.", 1 },
    { "Michael Müller", "imagehlp: Implement ImageLoad and cleanup ImageUnload.", 1 },
    { "Michael Müller", "imm32: Add stub for ImmDisableLegacyIME.", 1 },
    { "Michael Müller", "include: Add activation.idl with IActivationFactory interface.", 1 },
    { "Michael Müller", "include: Add dxva.h header file.", 1 },
    { "Michael Müller", "include: Add IApplicationActivationManager interface declaration.", 1 },
    { "Michael Müller", "include: Add more constants to SYSTEM_INFORMATION_CLASS.", 1 },
    { "Michael Müller", "include: Add run level information enum and structure to winnt.h.", 1 },
    { "Michael Müller", "include: Declare a couple more file information class structures.", 1 },
    { "Michael Müller", "include: Fix an invalid UUID in dxva2api.idl.", 1 },
    { "Michael Müller", "include/mfidl.idl: Add IMFMediaSession interface and dependencies.", 1 },
    { "Michael Müller", "include/objidl.idl: Add IApartmentShutdown interface.", 1 },
    { "Michael Müller", "include/roapi.h: Add further typedefs.", 1 },
    { "Michael Müller", "include/rpcndr.h: Fix definition of EXTERN_GUID.", 1 },
    { "Michael Müller", "include: Update LDR_MODULE to more recent windows versions.", 1 },
    { "Michael Müller", "inseng: Implement CIF reader and download functions.", 1 },
    { "Michael Müller", "kernel32: Add sr-Latn-RS locale definition.", 1 },
    { "Michael Müller", "kernel32: Add stub for GetPackageFullName.", 1 },
    { "Michael Müller", "kernel32: Add stub for SetThreadIdealProcessorEx.", 1 },
    { "Michael Müller", "kernel32: Add support for progress callback in CopyFileEx.", 1 },
    { "Michael Müller", "kernel32: Add support for reading scsi devices from sysfs.", 1 },
    { "Michael Müller", "kernel32: Convert scsi device type in SCSI_getprocentry.", 1 },
    { "Michael Müller", "kernel32: Implement some processor group functions.", 1 },
    { "Michael Müller", "kernel32: Make K32GetPerformanceInfo faster.", 1 },
    { "Michael Müller", "kernel32: Strip invalid characters from mask in FindFirstFileExW.", 1 },
    { "Michael Müller", "kernel32/tests: Add tests for FindFirstFileA with invalid characters.", 1 },
    { "Michael Müller", "kernel32: Update sr-Latn locale definition.", 1 },
    { "Michael Müller", "kernelbase: Add dll and add stub for QuirkIsEnabled.", 1 },
    { "Michael Müller", "krnl386.exe16: Do not reassign default handles after they got closed.", 1 },
    { "Michael Müller", "krnl386.exe16: Emulate GDT and LDT access.", 1 },
    { "Michael Müller", "krnl386.exe16: Really translate all invalid console handles into usable DOS handles.", 1 },
    { "Michael Müller", "libwine: Add process specific debug channels.", 1 },
    { "Michael Müller", "loader: Add commandline option --check-libs.", 1 },
    { "Michael Müller", "loader: Print library paths for --check-libs on Mac OS X.", 1 },
    { "Michael Müller", "mf: Add stub for MFCreateMediaSession.", 1 },
    { "Michael Müller", "mf: Implement IMFMediaSession stub interface.", 1 },
    { "Michael Müller", "mfplat: Add stub for MFCreateSourceResolver.", 1 },
    { "Michael Müller", "mfplat: Implement MFTEnum.", 1 },
    { "Michael Müller", "mfplat: Implement MFTRegister.", 2 },
    { "Michael Müller", "mfplat: Implement MFTUnregister.", 1 },
    { "Michael Müller", "mfplat/tests: Add tests.", 1 },
    { "Michael Müller", "mmsystem.dll16: Refcount midihdr to work around buggy application which unprepares buffer during a callback.", 1 },
    { "Michael Müller", "mmsystem.dll16: Translate MidiIn messages.", 1 },
    { "Michael Müller", "mountmgr.sys: Write usable device paths into HKLM\\SYSTEM\\MountedDevices.", 1 },
    { "Michael Müller", "mpr: Return correct error code for non network paths and REMOTE_NAME_INFO_LEVEL in WNetGetUniversalName.", 1 },
    { "Michael Müller", "mscoree: Implement semi-stub for _CorValidateImage.", 1 },
    { "Michael Müller", "msvcr120: Add stub for _SetWinRTOutOfMemoryExceptionCallback.", 1 },
    { "Michael Müller", "msvcrt: Add stub for Concurrency::details::_CurrentScheduler::_Id.", 1 },
    { "Michael Müller", "msvfw32: Add test for negative width/height values passed to ICGetDisplayFormat.", 1 },
    { "Michael Müller", "msvfw32: Set biSizeImage correctly in ICGetDisplayFormat.", 1 },
    { "Michael Müller", "msvfw32: Take stride into account and ask for palette in ICGetDisplayFormat.", 1 },
    { "Michael Müller", "msvfw32: Try different formarts in ICGetDisplayFormat.", 1 },
    { "Michael Müller", "msvidc32: Add support for converting 16 bit depth to 24 bit.", 1 },
    { "Michael Müller", "msvidc32: Fix calculation of stride and size.", 1 },
    { "Michael Müller", "ntdll: Add dummy apiset to PEB.", 1 },
    { "Michael Müller", "ntdll: Add stub for ApiSetQueryApiSetPresence.", 1 },
    { "Michael Müller", "ntdll: Add stub for JobObjectBasicAccountingInformation and JobObjectBasicProcessIdList in NtQueryInformationJobObject.", 1 },
    { "Michael Müller", "ntdll: Add stub for RtlGetUnloadEventTraceEx.", 1 },
    { "Michael Müller", "ntdll: Add stub for RtlIpv6StringToAddressExW.", 1 },
    { "Michael Müller", "ntdll: Add stub for RtlQueryPackageIdentity.", 1 },
    { "Michael Müller", "ntdll: Allow special characters in pipe names.", 1 },
    { "Michael Müller", "ntdll: Catch windows int 0x2e syscall on i386.", 1 },
    { "Michael Müller", "ntdll: Check architecture before loading module.", 1 },
    { "Michael Müller", "ntdll: Fill out thread times in process enumeration.", 1 },
    { "Michael Müller", "ntdll: Fill process kernel and user time.", 1 },
    { "Michael Müller", "ntdll: Fill process virtual memory counters in NtQuerySystemInformation.", 1 },
    { "Michael Müller", "ntdll: Fix error code when querying too large memory address.", 1 },
    { "Michael Müller", "ntdll: Fix parameters for RtlIpv4StringToAddressExW stub.", 1 },
    { "Michael Müller", "ntdll: Implement basic IO stats for SystemPerformanceInformation in NtQuerySystemInformation.", 1 },
    { "Michael Müller", "ntdll: Implement get_redirect function.", 1 },
    { "Michael Müller", "ntdll: Implement HashLinks field in LDR module data.", 1 },
    { "Michael Müller", "ntdll: Implement LdrEnumerateLoadedModules.", 1 },
    { "Michael Müller", "ntdll: Implement loader redirection scheme.", 1 },
    { "Michael Müller", "ntdll: Implement NtSuspendProcess and NtResumeProcess.", 1 },
    { "Michael Müller", "ntdll: Implement ObjectTypesInformation in NtQueryObject.", 1 },
    { "Michael Müller", "ntdll: Implement ProcessImageFileNameWin32 in NtQueryInformationProcess.", 1 },
    { "Michael Müller", "ntdll: Implement ProcessPriorityClass in NtQueryInformationProcess.", 1 },
    { "Michael Müller", "ntdll: Implement querying for FileNameInformation of named pipes in NtQueryInformationFile.", 1 },
    { "Michael Müller", "ntdll: Implement RunlevelInformationInActivationContext in RtlQueryInformationActivationContext.", 1 },
    { "Michael Müller", "ntdll: Implement SystemExtendedHandleInformation in NtQuerySystemInformation.", 1 },
    { "Michael Müller", "ntdll: Improve speed of LdrGetDllHandle when searching for the basename of modules that are not loaded.", 1 },
    { "Michael Müller", "ntdll: Load CLI/.NET images in the same way as Windows XP and above.", 1 },
    { "Michael Müller", "ntdll: Mark LDR data as initialized.", 1 },
    { "Michael Müller", "ntdll: Mimic object type behavior for different windows versions.", 1 },
    { "Michael Müller", "ntdll: Move code to determine module basename into separate function.", 1 },
    { "Michael Müller", "ntdll: Move logic to determine loadorder HKCU/app key into separate functions.", 1 },
    { "Michael Müller", "ntdll: Move logic to read loadorder registry values into separate function.", 1 },
    { "Michael Müller", "ntdll: Move NtProtectVirtualMemory and NtCreateSection to separate pages on x86.", 2 },
    { "Michael Müller", "ntdll: Parse execution level information in manifest data.", 1 },
    { "Michael Müller", "ntdll: Properly handle PAGE_WRITECOPY protection.", 5 },
    { "Michael Müller", "ntdll: Report system information SystemPerformanceInformation info class.", 2 },
    { "Michael Müller", "ntdll: Return success for TokenSessionId in NtSetInformationToken.", 1 },
    { "Michael Müller", "ntdll: Set correct thread creation time for SystemProcessInformation in NtQuerySystemInformation.", 1 },
    { "Michael Müller", "ntdll: Set object type for System(Extended)HandleInformation in NtQuerySystemInformation.", 1 },
    { "Michael Müller", "ntdll: Set process start time.", 1 },
    { "Michael Müller", "ntdll: Set TypeIndex for ObjectTypeInformation in NtQueryObject.", 1 },
    { "Michael Müller", "ntdll: Setup a temporary signal handler during process startup to handle page faults.", 2 },
    { "Michael Müller", "ntdll/tests: Add basic tests for RtlQueryPackageIdentity.", 1 },
    { "Michael Müller", "ntdll: Use HashLinks when searching for a dll using the basename.", 1 },
    { "Michael Müller", "ntoskrnl.exe/tests: Add kernel compliant test functions.", 1 },
    { "Michael Müller", "nvapi: Add NvAPI_GetPhysicalGPUsFromLogicalGPU.", 1 },
    { "Michael Müller", "nvapi: Add stub for EnumNvidiaDisplayHandle.", 1 },
    { "Michael Müller", "nvapi: Add stub for NvAPI_D3D9_RegisterResource.", 1 },
    { "Michael Müller", "nvapi: Add stub for NvAPI_D3D_GetCurrentSLIState.", 1 },
    { "Michael Müller", "nvapi: Add stub for NvAPI_D3D_GetObjectHandleForResource.", 1 },
    { "Michael Müller", "nvapi: Add stub for NvAPI_DISP_GetGDIPrimaryDisplayId.", 1 },
    { "Michael Müller", "nvapi: Add stub for NvAPI_EnumPhysicalGPUs.", 1 },
    { "Michael Müller", "nvapi: Add stub for NvAPI_GetLogicalGPUFromDisplay.", 1 },
    { "Michael Müller", "nvapi: Add stub for NvAPI_SYS_GetDriverAndBranchVersion.", 1 },
    { "Michael Müller", "nvapi: Add stub for NvAPI_Unload.", 1 },
    { "Michael Müller", "nvapi: Add stubs for NvAPI_EnumLogicalGPUs and undocumented equivalent.", 1 },
    { "Michael Müller", "nvapi: Add stubs for NvAPI_GPU_GetFullName.", 1 },
    { "Michael Müller", "nvapi: Explicity return NULL for 0x33c7358c and 0x593e8644.", 1 },
    { "Michael Müller", "nvapi: First implementation.", 1 },
    { "Michael Müller", "nvapi: Print fixme message for NvAPI_D3D9_StretchRectEx.", 1 },
    { "Michael Müller", "nvapi/tests: Use structure to list imports.", 1 },
    { "Michael Müller", "nvcuda: Emulate two d3d9 initialization functions.", 1 },
    { "Michael Müller", "nvcuda: First implementation.", 2 },
    { "Michael Müller", "nvcuda: Properly wrap undocumented 'ContextStorage' interface and add tests.", 1 },
    { "Michael Müller", "nvcuda: Search for dylib library on Mac OS X.", 1 },
    { "Michael Müller", "nvcuvid: First implementation.", 2 },
    { "Michael Müller", "nvencodeapi: Add debian specific paths to native library.", 1 },
    { "Michael Müller", "nvencodeapi: Add support for version 6.0.", 1 },
    { "Michael Müller", "nvencodeapi: First implementation.", 1 },
    { "Michael Müller", "ole32: Correctly parse unicode property storage dictionaries.", 1 },
    { "Michael Müller", "ole32: Implement returning a name in IEnumSTATPROPSTG.", 1 },
    { "Michael Müller", "ole32: Support reading VT_BOOL, VT_R8 and VT_I8 into propery storage.", 1 },
    { "Michael Müller", "programs/winedbg: Print process arguments in info threads.", 1 },
    { "Michael Müller", "secur32: Set output buffer size to zero during handshake when no data needs to be sent.", 1 },
    { "Michael Müller", "server: Add type Process.", 1 },
    { "Michael Müller", "server: Add type Thread.", 1 },
    { "Michael Müller", "server: Add type Token.", 1 },
    { "Michael Müller", "server: Assign a default label (high) to all tokens.", 1 },
    { "Michael Müller", "server: Compatibility with Wine Staging format for high precision registry timestamps.", 1 },
    { "Michael Müller", "server: Do not set SE_{D,S}ACL_PRESENT if no {D,S}ACL was set.", 1 },
    { "Michael Müller", "server: Implement changing the label of a security descriptor.", 1 },
    { "Michael Müller", "server: Implement querying the security label of a security descriptor.", 1 },
    { "Michael Müller", "server: Implement setting a security descriptor when duplicating tokens.", 1 },
    { "Michael Müller", "server: Implement support for global and local shared memory blocks based on memfd.", 1 },
    { "Michael Müller", "server: Register types during startup.", 1 },
    { "Michael Müller", "server: Rename ObjectType to Type.", 1 },
    { "Michael Müller", "setupapi: Add spec file stub entry for SetupDiInstallDeviceInterfaces and SetupDiRegisterCoDeviceInstallers.", 1 },
    { "Michael Müller", "setupapi: Add support for IDF_CHECKFIRST flag in SetupPromptForDiskW.", 1 },
    { "Michael Müller", "setupapi: Check handle type for HSPFILEQ handles.", 1 },
    { "Michael Müller", "setupapi: Create registry keys for display devices and display drivers.", 1 },
    { "Michael Müller", "setupapi: Handle the case that a full driver path is passed to SetupDiGetClassDevs.", 1 },
    { "Michael Müller", "setupapi: Ignore deletion of added files in SetupAddToDiskSpaceList.", 1 },
    { "Michael Müller", "setupapi: Implement SetupAddInstallSectionToDiskSpaceList.", 1 },
    { "Michael Müller", "setupapi: ImplementSetupAddSectionToDiskSpaceList.", 1 },
    { "Michael Müller", "setupapi: Implement SetupAddToDiskSpaceList.", 1 },
    { "Michael Müller", "setupapi: Implement SetupQueryDrivesInDiskSpaceList.", 1 },
    { "Michael Müller", "setupapi: Implement SP_COPY_IN_USE_NEEDS_REBOOT.", 1 },
    { "Michael Müller", "setupapi: Rewrite DiskSpaceList logic using lists.", 1 },
    { "Michael Müller", "setupapi: SetupDiGetDeviceInterfaceDetail should fill out DeviceInfoData even if the buffer for DeviceInterfaceData is too small.", 1 },
    { "Michael Müller", "setupapi/tests: Add test for IDF_CHECKFIRST and SetupPromptForDiskA/W.", 1 },
    { "Michael Müller", "sfc_os: Set an error code in SfcGetNextProtectedFile stub.", 1 },
    { "Michael Müller", "shell32: Add general tab in file property dialog.", 1 },
    { "Michael Müller", "shell32: Add parameter to ISFHelper::DeleteItems to allow deleting files without confirmation.", 1 },
    { "Michael Müller", "shell32: Add placeholder icons to match icon offset with XP.", 1 },
    { "Michael Müller", "shell32: Add support for extra large and jumbo icon lists.", 2 },
    { "Michael Müller", "shell32: Add support for setting/getting PREFERREDDROPEFFECT in IDataObject.", 1 },
    { "Michael Müller", "shell32: Choose return value for SHFileOperationW depending on windows version.", 1 },
    { "Michael Müller", "shell32: Correct indentation in shfileop.c.", 1 },
    { "Michael Müller", "shell32: Do not use unixfs for devices without mountpoint.", 1 },
    { "Michael Müller", "shell32: Fix copying of files when using a context menu.", 1 },
    { "Michael Müller", "shell32: Implement file operation progress dialog.", 1 },
    { "Michael Müller", "shell32: Implement insert/paste for item context menus.", 1 },
    { "Michael Müller", "shell32: Implement NewMenu with new folder item.", 1 },
    { "Michael Müller", "shell32: Improve FolderItemsImpl_get_Count stub.", 1 },
    { "Michael Müller", "shell32: Pass FILE_INFORMATION into SHNotify* functions.", 1 },
    { "Michael Müller", "shell32: Recognize cut/copy/paste string verbs in item menu context menu.", 1 },
    { "Michael Müller", "shell32: Remove source files when using cut in the context menu.", 1 },
    { "Michael Müller", "shell32: Set return value correctly in DoPaste.", 1 },
    { "Michael Müller", "shell32: Set SFGAO_HASSUBFOLDER correctly for normal shellfolders.", 1 },
    { "Michael Müller", "shell32: Set SFGAO_HASSUBFOLDER correctly for unixfs.", 1 },
    { "Michael Müller", "shell32: Show animation during SHFileOperation.", 1 },
    { "Michael Müller", "shell32: Use manual redirection for RunDLL_CallEntry16.", 1 },
    { "Michael Müller", "taskmgr: Use different units depending on memory usage.", 1 },
    { "Michael Müller", "taskmgr: Use system font instead of special bitmap font.", 1 },
    { "Michael Müller", "uiautomationcore: Add dll and stub some functions.", 1 },
    { "Michael Müller", "user32: Add semi-stub for GetAutoRotationState.", 1 },
    { "Michael Müller", "user32: Allow changing the tablet / media center status via wine registry key.", 1 },
    { "Michael Müller", "user32: Decrease minimum SetTimer interval to 5 ms.", 2 },
    { "Michael Müller", "user32: Fix calculation of listbox size when horizontal scrollbar is present.", 1 },
    { "Michael Müller", "user32: Get rid of wineserver call for GetLastInputInfo.", 1 },
    { "Michael Müller", "uxthemegtk: Add configure check and stub dll.", 1 },
    { "Michael Müller", "uxthemegtk: Implement enumeration of themes, color and sizes.", 1 },
    { "Michael Müller", "uxthemegtk: Print class name before calling vtable functions.", 1 },
    { "Michael Müller", "uxthemegtk: Reset FPU flags before calling GTK3 functions.", 1 },
    { "Michael Müller", "version: Correctly return VFF_CURNEDEST in VerFindFileA.", 1 },
    { "Michael Müller", "version: Return ERROR_FILE_NOT_FOUND for Win 9X when a file was not found in GetFileVersionInfoSizeEx.", 1 },
    { "Michael Müller", "virtdisk: Add GetStorageDependencyInformation stub.", 1 },
    { "Michael Müller", "vulkan: Enumerate VK_KHR_win32_surface only one time in vkEnumerateInstanceExtensionProperties.", 1 },
    { "Michael Müller", "vulkan: Implement vkGetPhysicalDeviceWin32PresentationSupportKHR.", 1 },
    { "Michael Müller", "vulkan: Improve vkGetPhysicalDeviceWin32PresentationSupportKHR and vkCreateWin32SurfaceKHR.", 1 },
    { "Michael Müller", "vulkan: Try to load libvulkan.so.1.", 1 },
    { "Michael Müller", "vulkan: Update to spec version 1.0.30 (no VK_EXT_debug_marker support yet).", 1 },
    { "Michael Müller", "wbemdisp: Add ISWbemSecurity stub interface.", 1 },
    { "Michael Müller", "wbemprox: Add ConfigManagerErrorCode for Win32_VideoController.", 1 },
    { "Michael Müller", "wbemprox: Add DriverDate for Win32_VideoController.", 1 },
    { "Michael Müller", "wbemprox: Add InstalledDisplayDrivers for Win32_VideoController.", 1 },
    { "Michael Müller", "wbemprox: Add Status for Win32_VideoController.", 1 },
    { "Michael Müller", "wbemprox: Add Vendor field in Win32_ComputerSystemProduct.", 1 },
    { "Michael Müller", "wbemprox: Provide DeviceID, Location and PortName for printers.", 1 },
    { "Michael Müller", "wevtapi: Add stub for EvtNext.", 1 },
    { "Michael Müller", "wineboot: Add some generic hardware in HKEY_DYN_DATA\\Config Manager\\Enum.", 1 },
    { "Michael Müller", "wineboot: Initialize proxy settings registry key.", 1 },
    { "Michael Müller", "winecfg: Add option to enable/disable GTK3 theming.", 1 },
    { "Michael Müller", "winecfg: Add staging tab for CSMT.", 1 },
    { "Michael Müller", "winecfg: Show unmounted devices and allow changing the device value.", 1 },
    { "Michael Müller", "wined3d: Add 1d texture resource type.", 1 },
    { "Michael Müller", "wined3d: Add DXT1 to B4G4R4A4, DXT1 to B5G5R5A1 and DXT3 to B4G4R4A4 conversion.", 1 },
    { "Michael Müller", "wined3d: Add hack for WINED3DFMT_R24_UNORM_X8_TYPELESS.", 1 },
    { "Michael Müller", "wined3d: Add is_power_of_two helper function.", 1 },
    { "Michael Müller", "wined3d: Add stubs for QUERY_TYPE_SO_STATISTICS and QUERY_TYPE_SO_OVERFLOW.", 1 },
    { "Michael Müller", "wined3d: Add support for 1d textures in context_attach_gl_texture_fbo.", 1 },
    { "Michael Müller", "wined3d: Add support for DXTn software decoding through libtxc_dxtn.", 3 },
    { "Michael Müller", "wined3d: Add wined3d_check_device_format_support.", 1 },
    { "Michael Müller", "wined3d: Add wined3d_resource_map_info function.", 1 },
    { "Michael Müller", "wined3d: Allow changing strict drawing through an exported function.", 1 },
    { "Michael Müller", "wined3d: Allow creation of 1d shader views.", 1 },
    { "Michael Müller", "wined3d: Check for 1d textures in wined3d_texture_update_desc.", 1 },
    { "Michael Müller", "wined3d: Check if 1d teture is still in use before releasing.", 1 },
    { "Michael Müller", "wined3d: Create dummy 1d textures.", 1 },
    { "Michael Müller", "wined3d: Create dummy 1d textures and surfaces.", 1 },
    { "Michael Müller", "wined3d: Discard buffer during upload when replacing complete content.", 1 },
    { "Michael Müller", "wined3d: Do not pin large buffers.", 1 },
    { "Michael Müller", "wined3d: Don't wait for events that have not been issued yet.", 1 },
    { "Michael Müller", "wined3d: Generate glsl samplers for 1d texture arrays.", 1 },
    { "Michael Müller", "wined3d: Handle 1d textures in texture_activate_dimensions.", 1 },
    { "Michael Müller", "wined3d: Implement converting between (s)rgb 1d textures.", 1 },
    { "Michael Müller", "wined3d: Implement downloading from (s)rgb 1d textures to system memory.", 1 },
    { "Michael Müller", "wined3d: Implement loading from system memory and buffers to (s)rgb 1d textures.", 1 },
    { "Michael Müller", "wined3d: Implement preparation for 1d textures.", 1 },
    { "Michael Müller", "wined3d: Implement uploading for 1d textures.", 1 },
    { "Michael Müller", "wined3d: Improve wined3d_cs_emit_update_sub_resource.", 1 },
    { "Michael Müller", "wined3d: Limit the vram memory to LONG_MAX only on 32 bit.", 1 },
    { "Michael Müller", "wined3d: Load dxtn dylib library on Mac OS X.", 1 },
    { "Michael Müller", "wined3d: Use real values for memory accounting on NVIDIA cards.", 1 },
    { "Michael Müller", "wined3d: Use spin lock for cs list critical sections.", 1 },
    { "Michael Müller", "wine.inf: Add 'New' context menu handler entry for directories.", 1 },
    { "Michael Müller", "wine.inf: Add WMP 12 as installed windows feature.", 1 },
    { "Michael Müller", "wine.inf.in: Add invalid dummy certificate to CA certificate store.", 1 },
    { "Michael Müller", "winemp3.acm: Check input format in MPEG3_StreamOpen.", 1 },
    { "Michael Müller", "winex11.drv: Allow changing the opengl pixel format on the desktop window.", 1 },
    { "Michael Müller", "winex11.drv: Allow to select default display frequency in registry key.", 1 },
    { "Michael Müller", "winex11.drv: Indicate direct rendering through OpenGL extension.", 1 },
    { "Michael Müller", "winex11.drv: Only warn about used contexts in wglShareLists.", 1 },
    { "Michael Müller", "winex11: Prevent window managers from grouping all wine programs together.", 1 },
    { "Michael Müller", "wininet: Add support for INTERNET_OPTION_SETTINGS_CHANGED in InternetSetOption.", 1 },
    { "Michael Müller", "wininet: Allow INTERNET_OPTION_SETTINGS_CHANGED on connections.", 1 },
    { "Michael Müller", "wininet: Allow to set INTERNET_OPTION_HTTP_DECODING on sessions and connections.", 1 },
    { "Michael Müller", "wininet: Replacing header fields should fail if they do not exist yet.", 1 },
    { "Michael Müller", "wininet: Resize buffer when call to InternetCanonicalizeUrlW fails in InternetCrackUrlW.", 1 },
    { "Michael Müller", "wininet: Return failure when handling http redirect without hostname.", 1 },
    { "Michael Müller", "wininet: Strip filename if no path is set in cookie.", 1 },
    { "Michael Müller", "wininet/tests: Add more tests for cookies.", 1 },
    { "Michael Müller", "wininet/tests: Check cookie behaviour when overriding host.", 1 },
    { "Michael Müller", "wininet/tests: Test auth credential reusage with host override.", 1 },
    { "Michael Müller", "winmm: Delay import ole32 msacm32 to workaround bug when loading multiple winmm versions.", 1 },
    { "Michael Müller", "winmm: Do not crash in Win 9X mode when an invalid device ptr is passed to MCI_OPEN.", 1 },
    { "Michael Müller", "wuauserv: Add dummy service.", 1 },
    { "Michael Müller", "wusa: Ignore systemProtection subkey of registry key.", 1 },
    { "Michael Müller", "wusa: Implement basic installation logic.", 1 },
    { "Michael Müller", "wusa: Implement WOW64 support.", 1 },
    { "Michael Müller", "wusa: Print warning when encountering msdelta compressed files.", 1 },
    { "Michael Müller", "wusa: Treat empty update list as error.", 1 },
    { "Michael Müller", "xinput9_1_0: Build independent xinput9_1_0.dll instead of using forwards.", 1 },
    { "Nikolay Sivov", "dwrite: Added missing max bitmap size initialization.", 1 },
    { "Nils Kuhnhenn", "wined3d: Fix context_acquire not being called from the command thread in wined3d_texture_add_dirty_region.", 1 },
    { "Qian Hong", "advapi32: Fallback to Sid string when LookupAccountSid fails.", 1 },
    { "Qian Hong", "advapi32: Fix name and use of DOMAIN_GROUP_RID_USERS.", 1 },
    { "Qian Hong", "advapi32: Initialize buffer length to zero in LsaLookupSids to prevent crash.", 2 },
    { "Qian Hong", "advapi32: Prepend a hidden LSA_TRUST_INFORMATION in LsaLookupNames2 to avoid crash when Domains[-1] incorrectly accessed by application.", 2 },
    { "Qian Hong", "advapi32: Prepend a hidden LSA_TRUST_INFORMATION in LsaLookupSids to avoid crash when Domains[-1] incorrectly accessed by application.", 2 },
    { "Qian Hong", "advapi32/tests: Test prefix and use of TokenPrimaryGroup Sid.", 1 },
    { "Qian Hong", "kernel32: Fallback to default comspec when %COMSPEC% is not set.", 1 },
    { "Qian Hong", "kernel32: Init TimezoneInformation registry.", 1 },
    { "Qian Hong", "ntdll: Add fake data implementation for ProcessQuotaLimits class.", 1 },
    { "Qian Hong", "ntdll: Improve invalid paramater handling in NtAccessCheck.", 1 },
    { "Qian Hong", "ntdll: Initialize mod_name to zero.", 1 },
    { "Qian Hong", "ntdll: Set EOF on file which has a memory mapping should fail.", 1 },
    { "Qian Hong", "ntdll/tests: Added tests for open behaviour on readonly files.", 1 },
    { "Qian Hong", "ntdll/tests: Added tests to set disposition on file which is mapped to memory.", 1 },
    { "Qian Hong", "server: Create primary group using DOMAIN_GROUP_RID_USERS.", 1 },
    { "Qian Hong", "server: Do not allow to set disposition on file which has a file mapping.", 1 },
    { "Qian Hong", "server: Map EXDEV to STATUS_NOT_SAME_DEVICE.", 1 },
    { "Samuel Kim", "comctl32: Fix buttons becoming unthemed when pressed/released.", 1 },
    { "Sebastian Lackner", "advapi32/tests: Add ACL inheritance tests for creating subdirectories with NtCreateFile.", 1 },
    { "Sebastian Lackner", "advapi32/tests: Add tests for ACL inheritance in CreateDirectoryA.", 1 },
    { "Sebastian Lackner", "advapi: Trigger write watches before passing userdata pointer to read syscall.", 1 },
    { "Sebastian Lackner", "amstream: Avoid implicit cast of interface pointer.", 1 },
    { "Sebastian Lackner", "bcrypt: Avoid crash in tests when compiling without gnutls support.", 1 },
    { "Sebastian Lackner", "bcrypt: Fix string comparison in set_alg_property.", 1 },
    { "Sebastian Lackner", "bcrypt: Implement support for ECB chain mode.", 1 },
    { "Sebastian Lackner", "bcrypt: Pass object to get_{alg,hash}_property instead of alg_id.", 1 },
    { "Sebastian Lackner", "bcrypt/tests: Add test for bugs in BCryptGetProperty.", 1 },
    { "Sebastian Lackner", "bcrypt/tests: Add tests for auth data in AES GCM mode.", 1 },
    { "Sebastian Lackner", "bcrypt/tests: Add tests for BCryptDuplicateKey.", 1 },
    { "Sebastian Lackner", "configure: Also add the absolute RPATH when linking against libwine.", 1 },
    { "Sebastian Lackner", "d2d1: Avoid implicit cast of interface pointer.", 1 },
    { "Sebastian Lackner", "d3d10_1: Add missing forwards to d3d10.", 1 },
    { "Sebastian Lackner", "d3d11: Avoid implicit cast of interface pointer.", 1 },
    { "Sebastian Lackner", "d3d11: Simplify d3d11_deferred_context_IASetVertexBuffers.", 1 },
    { "Sebastian Lackner", "d3d11/tests: Avoid test failures.", 1 },
    { "Sebastian Lackner", "d3d8: Avoid implicit cast of interface pointer.", 1 },
    { "Sebastian Lackner", "d3d8: Improve ValidatePixelShader stub.", 1 },
    { "Sebastian Lackner", "d3d8: Improve ValidateVertexShader stub.", 1 },
    { "Sebastian Lackner", "d3d9: Avoid implicit cast of interface pointer.", 1 },
    { "Sebastian Lackner", "d3d9/tests: Avoid test failures on specific Nvidia graphic cards.", 1 },
    { "Sebastian Lackner", "d3dx9_36: Improve stub for ID3DXEffectImpl_CloneEffect.", 1 },
    { "Sebastian Lackner", "d3dx9_36/tests: Add initial tests for D3DXDisassembleShader.", 1 },
    { "Sebastian Lackner", "d3dx9_*: Adjust ID3DXEffect interface based on DLL version.", 1 },
    { "Sebastian Lackner", "dbghelp: Always check for debug symbols in BINDIR.", 1 },
    { "Sebastian Lackner", "ddraw: Avoid implicit cast of interface pointer.", 1 },
    { "Sebastian Lackner", "ddraw/tests: Fix function name in multiple ok() messages.", 1 },
    { "Sebastian Lackner", "dinput: Do not wait for hook thread startup in IDirectInput8::Initialize.", 1 },
    { "Sebastian Lackner", "dsound: Allow disabling of EAX support in the registry.", 1 },
    { "Sebastian Lackner", "dsound: Apply filters before sound is multiplied to speakers.", 1 },
    { "Sebastian Lackner", "dsound: Avoid implicit cast of interface pointer.", 1 },
    { "Sebastian Lackner", "dsound: Various improvements to EAX support.", 1 },
    { "Sebastian Lackner", "dwrite: Avoid implicit cast of interface pointer.", 1 },
    { "Sebastian Lackner", "dxva2: Implement semi-stub for Direct3DDeviceManager9 interface.", 1 },
    { "Sebastian Lackner", "fonts: Add Liberation Mono as an Courier New replacement.", 1 },
    { "Sebastian Lackner", "fonts: Add Liberation Serif as an Times New Roman replacement.", 1 },
    { "Sebastian Lackner", "gdi32: Perform lazy initialization of fonts to improve startup performance.", 1 },
    { "Sebastian Lackner", "include: Add cuda.h.", 1 },
    { "Sebastian Lackner", "include: Check element type in CONTAINING_RECORD and similar macros.", 1 },
    { "Sebastian Lackner", "iphlpapi: Fallback to system ping when ICMP permissions are not present.", 1 },
    { "Sebastian Lackner", "kernel32: Add winediag message to show warning, that this isn't vanilla wine.", 1 },
    { "Sebastian Lackner", "kernel32: Allow non-nullterminated string as working directory in create_startup_info.", 1 },
    { "Sebastian Lackner", "kernel32: Always start debugger on WinSta0.", 1 },
    { "Sebastian Lackner", "kernel32: Fill stack with meaningful values in call_process_entry mis-align workaround.", 1 },
    { "Sebastian Lackner", "kernel32: Implement passing security descriptors from CreateProcess to the wineserver.", 2 },
    { "Sebastian Lackner", "kernel32/tests: Add additional tests for condition mask of VerifyVersionInfoA.", 1 },
    { "Sebastian Lackner", "loader: Add commandline option --patches to show the patch list.", 1 },
    { "Sebastian Lackner", "makedep: Add support for PARENTSPEC Makefile variable.", 1 },
    { "Sebastian Lackner", "msvcrt: Calculate sinh/cosh/exp/pow with higher precision.", 2 },
    { "Sebastian Lackner", "msxml3: Avoid implicit cast of interface pointer.", 1 },
    { "Sebastian Lackner", "ntdll: Add semi-stub for FileFsVolumeInformation information class.", 1 },
    { "Sebastian Lackner", "ntdll: Add special handling for \\SystemRoot to satisfy MSYS2 case-insensitive system check.", 1 },
    { "Sebastian Lackner", "ntdll: Add support for hiding wine version information from applications.", 1 },
    { "Sebastian Lackner", "ntdll: Allow to query file IO completion notification mode.", 1 },
    { "Sebastian Lackner", "ntdll: Always store SAMBA_XATTR_DOS_ATTRIB when path could be interpreted as hidden.", 1 },
    { "Sebastian Lackner", "ntdll: Always use 64-bit registry view on WOW64 setups.", 1 },
    { "Sebastian Lackner", "ntdll: Block signals while executing system APCs.", 2 },
    { "Sebastian Lackner", "ntdll: Do not allow to deallocate thread stack for current thread.", 1 },
    { "Sebastian Lackner", "ntdll: Do not require unix fd for FileIoCompletionNotificationInformation.", 1 },
    { "Sebastian Lackner", "ntdll: Ensure process dlls are not attached too early.", 2 },
    { "Sebastian Lackner", "ntdll: Expose wine_uninterrupted_[read|write]_memory as exports.", 1 },
    { "Sebastian Lackner", "ntdll: Fix a bug when exception handling triggers stack guard page.", 1 },
    { "Sebastian Lackner", "ntdll: Fix condition mask handling in RtlVerifyVersionInfo.", 1 },
    { "Sebastian Lackner", "ntdll: Fix race-condition when threads are killed during shutdown.", 1 },
    { "Sebastian Lackner", "ntdll: Fix return value for missing ACTIVATION_CONTEXT_SECTION_ASSEMBLY_INFORMATION key.", 1 },
    { "Sebastian Lackner", "ntdll: Implement _alldvrm/_aulldvrm and add tests.", 1 },
    { "Sebastian Lackner", "ntdll: Implement FileIoCompletionNotificationInformation info class.", 2 },
    { "Sebastian Lackner", "ntdll: Implement virtual_map_shared_memory.", 1 },
    { "Sebastian Lackner", "ntdll: Improve stub of NtQueryEaFile.", 1 },
    { "Sebastian Lackner", "ntdll: Move code to update user shared data into a separate function.", 1 },
    { "Sebastian Lackner", "ntdll: Only enable wineserver shared memory communication when a special environment variable is set.", 1 },
    { "Sebastian Lackner", "ntdll: OutputDebugString should throw the exception a second time, if a debugger is attached.", 1 },
    { "Sebastian Lackner", "ntdll: Process APC calls before starting process.", 1 },
    { "Sebastian Lackner", "ntdll: Remove memory limitation to 32GB on 64-bit by growing heap dynamically.", 2 },
    { "Sebastian Lackner", "ntdll: Return buffer filled with random values from SystemInterruptInformation.", 1 },
    { "Sebastian Lackner", "ntdll: Return correct values in GetThreadTimes() for all threads.", 1 },
    { "Sebastian Lackner", "ntdll: Return fake device type when systemroot is located on virtual disk.", 1 },
    { "Sebastian Lackner", "ntdll: Return STATUS_INVALID_DEVICE_REQUEST when trying to call NtReadFile on directory.", 1 },
    { "Sebastian Lackner", "ntdll: Reuse old async fileio structures if possible.", 1 },
    { "Sebastian Lackner", "ntdll/tests: Add more tests for FileIoCompletionNotificationInformation.", 1 },
    { "Sebastian Lackner", "ntdll: Throw exception if invalid handle is passed to NtClose and debugger enabled.", 1 },
    { "Sebastian Lackner", "ntdll: Trigger stack overflow exception earlier on x86_64.", 1 },
    { "Sebastian Lackner", "ntdll: Trigger write watches before passing userdata pointer to wait_reply.", 1 },
    { "Sebastian Lackner", "ntoskrnl.exe/tests: Add initial driver testing framework and corresponding changes to Makefile system.", 2 },
    { "Sebastian Lackner", "ntoskrnl: Update USER_SHARED_DATA before accessing memory.", 1 },
    { "Sebastian Lackner", "nvcuda: Add stub dll.", 1 },
    { "Sebastian Lackner", "nvcuda: Add support for CUDA 7.0.", 1 },
    { "Sebastian Lackner", "nvcuda: Implement cuModuleLoad wrapper function.", 1 },
    { "Sebastian Lackner", "nvcuda: Implement new functions added in CUDA 6.5.", 1 },
    { "Sebastian Lackner", "nvcuda: Properly wrap stream callbacks by forwarding them to a worker thread.", 1 },
    { "Sebastian Lackner", "ole32: Improve thread-safety of HGLOBALStreamImpl_Read.", 1 },
    { "Sebastian Lackner", "ole32: Improve thread-safety of HGLOBALStreamImpl_Write.", 1 },
    { "Sebastian Lackner", "ole32: Set DebugInfo->Spare[0] for handle_wrapper lock.", 1 },
    { "Sebastian Lackner", "oleaut32: Avoid implicit cast of interface pointer.", 1 },
    { "Sebastian Lackner", "oleaut32: Handle TKIND_COCLASS in proxy/stub marshalling.", 1 },
    { "Sebastian Lackner", "oleaut32: Implement asm proxys for x86_64.", 1 },
    { "Sebastian Lackner", "oleaut32: Implement ITypeInfo_fnInvoke for TKIND_COCLASS in arguments.", 1 },
    { "Sebastian Lackner", "oleaut32: Implement SaveAsFile for PICTYPE_ENHMETAFILE.", 1 },
    { "Sebastian Lackner", "oleaut32: Implement TMStubImpl_Invoke on x86_64.", 1 },
    { "Sebastian Lackner", "oleaut32: Initial preparation to make marshalling compatible with x86_64.", 1 },
    { "Sebastian Lackner", "oleaut32: Pass a HREFTYPE to get_iface_guid.", 1 },
    { "Sebastian Lackner", "oleaut32/tests: Add a test for TKIND_COCLASS in proxy/stub marshalling.", 1 },
    { "Sebastian Lackner", "opengl32: Add wrappers for glDebugMessageCallback to handle calling convention differences.", 1 },
    { "Sebastian Lackner", "qcap: Fix compile error when O_CLOEXEC is not defined.", 1 },
    { "Sebastian Lackner", "Revert \"dsound: Simplify error handling when creating a sound buffer.\".", 1 },
    { "Sebastian Lackner", "Revert \"dsound: Use a better name for IDirectSoundBufferImpl_Create().\".", 1 },
    { "Sebastian Lackner", "Revert \"iexplore: Sync registry and program resource values.\".", 1 },
    { "Sebastian Lackner", "Revert \"opengl32: Return a NULL pointer for functions requiring unsupported or disabled extensions.\".", 1 },
    { "Sebastian Lackner", "Revert \"wined3d: Implement wined3d_buffer_upload_data() on top of wined3d_buffer_upload_ranges().\".", 1 },
    { "Sebastian Lackner", "Revert \"wined3d: Slightly offset the viewport in the ARB_clip_control case.\".", 1 },
    { "Sebastian Lackner", "riched20: Silence repeated FIXMEs triggered by Adobe Reader.", 1 },
    { "Sebastian Lackner", "rpcrt4: Avoid implicit cast of interface pointer.", 1 },
    { "Sebastian Lackner", "server: Add a helper function set_sd_from_token_internal to merge two security descriptors.", 1 },
    { "Sebastian Lackner", "server: Allow multiple registry notifications for the same key.", 1 },
    { "Sebastian Lackner", "server: Allow to open files without any permission bits.", 2 },
    { "Sebastian Lackner", "server: Assign random name when no name was passed to create_winstation.", 1 },
    { "Sebastian Lackner", "server: Do not signal thread until it is really gone.", 1 },
    { "Sebastian Lackner", "server: FILE_WRITE_ATTRIBUTES should succeed for readonly files.", 1 },
    { "Sebastian Lackner", "server: Fix handling of GetMessage after previous PeekMessage call.", 3 },
    { "Sebastian Lackner", "server: Growing files which are mapped to memory should still work.", 1 },
    { "Sebastian Lackner", "server: Implement locking and synchronization of keystate buffer.", 3 },
    { "Sebastian Lackner", "server: Improve mapping of DACL to file permissions.", 1 },
    { "Sebastian Lackner", "server: Improve STATUS_CANNOT_DELETE checks for directory case.", 1 },
    { "Sebastian Lackner", "server: Introduce a helper function to update the thread_input key state.", 1 },
    { "Sebastian Lackner", "server: Introduce a new alloc_handle object callback.", 2 },
    { "Sebastian Lackner", "server: Introduce refcounting for registry notifications.", 1 },
    { "Sebastian Lackner", "server: Skip async completion when possible.", 1 },
    { "Sebastian Lackner", "server: Store a list of associated queues for each thread input.", 1 },
    { "Sebastian Lackner", "server: Store a reference to the parent object for pipe servers.", 2 },
    { "Sebastian Lackner", "server: Support for thread and process security descriptors in new_process wineserver call.", 2 },
    { "Sebastian Lackner", "server: Temporarily store the full security descriptor for file objects.", 1 },
    { "Sebastian Lackner", "server: Track desktop handle count more correctly.", 1 },
    { "Sebastian Lackner", "server: When combining root and name, make sure there is only one slash.", 2 },
    { "Sebastian Lackner", "server: When creating new directories temporarily give read-permissions until they are opened.", 1 },
    { "Sebastian Lackner", "setupapi: Also create HardwareId registry key for display devices.", 1 },
    { "Sebastian Lackner", "setupapi: DelReg should recursively delete registry keys.", 1 },
    { "Sebastian Lackner", "shcore: Add dll.", 1 },
    { "Sebastian Lackner", "shcore: Add SetProcessDpiAwareness stub.", 1 },
    { "Sebastian Lackner", "shcore: Add stub for GetProcessDpiAwareness.", 1 },
    { "Sebastian Lackner", "shcore: Implement stub for GetDpiForMonitor.", 1 },
    { "Sebastian Lackner", "shell32: Create Microsoft\\Windows\\Themes directory during Wineprefix creation.", 1 },
    { "Sebastian Lackner", "shlwapi: SHMapHandle should not set error when NULL is passed as hShared.", 1 },
    { "Sebastian Lackner", "shlwapi/tests: Add additional tests for UrlCombine and UrlCanonicalize.", 1 },
    { "Sebastian Lackner", "shlwapi: UrlCombineW workaround for relative paths.", 1 },
    { "Sebastian Lackner", "stdole32.tlb: Compile typelib with --oldtlb.", 1 },
    { "Sebastian Lackner", "user32: Avoid unnecessary wineserver calls in PeekMessage/GetMessage.", 1 },
    { "Sebastian Lackner", "user32: Cache the result of GetForegroundWindow.", 1 },
    { "Sebastian Lackner", "user32: Call UpdateWindow() during DIALOG_CreateIndirect.", 1 },
    { "Sebastian Lackner", "user32: Fix handling of invert_y in DrawTextExW.", 1 },
    { "Sebastian Lackner", "user32: Get rid of wineserver call for GetActiveWindow, GetFocus, GetCapture.", 1 },
    { "Sebastian Lackner", "user32: Get rid of wineserver call for GetInputState.", 1 },
    { "Sebastian Lackner", "user32: Globally invalidate key state on changes in other threads.", 1 },
    { "Sebastian Lackner", "user32: Implement CWF_CREATE_ONLY flag for CreateWindowStation.", 1 },
    { "Sebastian Lackner", "user32: Refresh MDI menus when DefMDIChildProc(WM_SETTEXT) is called.", 1 },
    { "Sebastian Lackner", "user.exe16: Don't open code CONTAINING_RECORD.", 1 },
    { "Sebastian Lackner", "uxthemegtk: Add export for OpenThemeDataEx.", 1 },
    { "Sebastian Lackner", "uxthemegtk: Correctly render buttons with GTK >= 3.14.0.", 1 },
    { "Sebastian Lackner", "uxthemegtk: Fix some incorrect error codes.", 1 },
    { "Sebastian Lackner", "uxthemegtk: Validate theme handles before accessing private data.", 1 },
    { "Sebastian Lackner", "vbscript: Avoid implicit cast of interface pointer.", 1 },
    { "Sebastian Lackner", "vcomp: Handle begin == NULL in _vcomp_for_static_init.", 1 },
    { "Sebastian Lackner", "vulkan: Initial implementation.", 2 },
    { "Sebastian Lackner", "vulkan: Only convert VkDescriptor{Image,Buffer}Info when type is valid.", 1 },
    { "Sebastian Lackner", "vulkan: Use binary search to lookup function in is_null_func.", 1 },
    { "Sebastian Lackner", "widl: Add --oldtlb switch in usage message.", 1 },
    { "Sebastian Lackner", "wineboot: Assign a drive serial number during prefix creation/update.", 1 },
    { "Sebastian Lackner", "wineboot: Init system32/drivers/etc/{host,networks,protocol,services}.", 1 },
    { "Sebastian Lackner", "winecfg: Add checkbox to enable/disable HideWineExports registry key.", 1 },
    { "Sebastian Lackner", "winecfg: Add checkbox to enable/disable vaapi GPU decoder.", 1 },
    { "Sebastian Lackner", "wined3d: Add additional synchronization CS ops.", 1 },
    { "Sebastian Lackner", "wined3d: Add second dll with STAGING_CSMT definition set.", 1 },
    { "Sebastian Lackner", "wined3d: Do not immediately submit stateblock updates.", 1 },
    { "Sebastian Lackner", "wined3d: Enforce a memory limit of about 16 MB for CSMT blocks.", 1 },
    { "Sebastian Lackner", "wined3d: Introduce a separate priority queue.", 1 },
    { "Sebastian Lackner", "wined3d: Reset context before destruction.", 1 },
    { "Sebastian Lackner", "wined3d: Run the cs asynchronously.", 1 },
    { "Sebastian Lackner", "wined3d: Send push_constants through the CS.", 1 },
    { "Sebastian Lackner", "wined3d: Send query_poll through the command stream.", 1 },
    { "Sebastian Lackner", "wined3d: Silence extremely noisy FIXME in wined3d_texture_add_dirty_region.", 1 },
    { "Sebastian Lackner", "wined3d: Silence repeated 'Unhandled blend factor 0' messages.", 1 },
    { "Sebastian Lackner", "wined3d: Silence repeated wined3d_swapchain_present FIXME.", 1 },
    { "Sebastian Lackner", "wined3d: Synchronize before resizing swapchain context array.", 1 },
    { "Sebastian Lackner", "wined3d: Use priority queue for maps/unmaps.", 1 },
    { "Sebastian Lackner", "wined3d: Use priority queue for query polls.", 1 },
    { "Sebastian Lackner", "wined3d: Use priority queue for update_sub_resource.", 1 },
    { "Sebastian Lackner", "winedevice: Avoid invalid memory access when relocation block addresses memory outside of the current page.", 1 },
    { "Sebastian Lackner", "wine.inf: Add a ProfileList\\<UserSID> registry subkey.", 1 },
    { "Sebastian Lackner", "winelib: Append '(Staging)' at the end of the version string.", 1 },
    { "Sebastian Lackner", "winemenubuilder: Create desktop shortcuts with absolute wine path.", 1 },
    { "Sebastian Lackner", "winepulse.drv: Use a separate mainloop and ctx for pulse_test_connect.", 1 },
    { "Sebastian Lackner", "winex11: Enable/disable windows when they are (un)mapped by foreign applications.", 1 },
    { "Sebastian Lackner", "winex11: Fix alpha blending in X11DRV_UpdateLayeredWindow.", 1 },
    { "Sebastian Lackner", "winex11: Forward all clipping requests to the right thread (including fullscreen clipping).", 1 },
    { "Sebastian Lackner", "winex11: Implement X11DRV_FLUSH_GDI_DISPLAY ExtEscape command.", 1 },
    { "Sebastian Lackner", "winhlp32: Workaround a bug in Flex.", 1 },
    { "Sebastian Lackner", "winhttp: Fix handling of Accept headers.", 1 },
    { "Sebastian Lackner", "wintrust/tests: Add some additional tests.", 1 },
    { "Sebastian Lackner", "ws2_32: Avoid race-conditions of async WSARecv() operations with write watches.", 2 },
    { "Sebastian Lackner", "ws2_32: Divide values returned by SO_RCVBUF and SO_SNDBUF getsockopt options by two.", 1 },
    { "Sebastian Lackner", "ws2_32: Implement returning the proper time with SO_CONNECT_TIME.", 1 },
    { "Sebastian Lackner", "ws2_32: Invalidate client-side file descriptor cache in WSACleanup.", 1 },
    { "Sebastian Lackner", "ws2_32: Reuse old async ws2_async_io structures if possible.", 1 },
    { "Sebastian Lackner", "ws2_32/tests: Add test for completion notification flags.", 1 },
    { "Sebastian Lackner", "ws2_32/tests: Hide random test failures in socket tests.", 1 },
    { "Sebastian Lackner", "wtsapi32: Improve WTSQueryUserToken stub.", 2 },
    { "Sebastian Lackner", "wtsapi32: Partial implementation of WTSEnumerateProcessesW.", 1 },
    { "Sebastian Lackner", "wusa: Add workaround to be compatible with Vista packages.", 1 },
    { "Sebastian Lackner", "wusa: Improve tracing of installation process.", 1 },
    { "Steaphan Greene", "ntdll: Improve heap allocation performance by using more fine-grained free lists.", 1 },
    { "Stefan Dösinger", "wined3d: Add a separate variable to check if queries are started.", 1 },
    { "Stefan Dösinger", "wined3d: Add swapchain waits.", 1 },
    { "Stefan Dösinger", "wined3d: Don't call glFinish before swapping.", 1 },
    { "Stefan Dösinger", "wined3d: Get rid of the end_scene flush and finish.", 1 },
    { "Stefan Dösinger", "wined3d: Hackily introduce a multithreaded command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Prevent the command stream from running ahead too far.", 1 },
    { "Stefan Dösinger", "wined3d: Send blits through the command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Send render target view clears through the command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Send update_texture calls through the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Update the swap interval through the CS in reset.", 1 },
    { "Stefan Dösinger", "wined3d: Use an event to block the worker thread when it is idle.", 1 },
    { "Stefan Dösinger", "wined3d: Wait for the cs to finish before destroying the device.", 1 },
    { "Stefan Dösinger", "wined3d: Wrap GL BOs in a structure.", 1 },
    { "Torsten Kurbad", "fonts: Add Liberation Sans as an Arial replacement.", 2 },
    { "Zhenbo Li", "mshtml: Add IHTMLLocation::hash property's getter implementation.", 1 },
    { "Zhenbo Li", "ole32: Add stub for OleGetIconOfFile.", 1 },
    { "Zhenbo Li", "shell32: Fix SHFileOperation(FO_MOVE) for creating subdirectories.", 1 },
    { NULL, NULL, 0 }
};

/* return the applied non-standard patches */
const void *wine_get_patches(void)
{
    return &wine_patch_data[0];
}

/* return the build id string */
const char *wine_get_build_id(void)
{
    extern const char wine_build[];
    return wine_build;
}

/* exec a binary using the preloader if requested; helper for wine_exec_wine_binary */
static void preloader_exec( char **argv, int use_preloader )
{
    if (use_preloader)
    {
        static const char preloader[] = "wine-preloader";
        static const char preloader64[] = "wine64-preloader";
        char *p, *full_name;
        char **last_arg = argv, **new_argv;

        if (!(p = strrchr( argv[0], '/' ))) p = argv[0];
        else p++;

        full_name = xmalloc( p - argv[0] + sizeof(preloader64) );
        memcpy( full_name, argv[0], p - argv[0] );
        if (strendswith( p, "64" ))
            memcpy( full_name + (p - argv[0]), preloader64, sizeof(preloader64) );
        else
            memcpy( full_name + (p - argv[0]), preloader, sizeof(preloader) );

        /* make a copy of argv */
        while (*last_arg) last_arg++;
        new_argv = xmalloc( (last_arg - argv + 2) * sizeof(*argv) );
        memcpy( new_argv + 1, argv, (last_arg - argv + 1) * sizeof(*argv) );
        new_argv[0] = full_name;
        execv( full_name, new_argv );
        free( new_argv );
        free( full_name );
    }
    execv( argv[0], argv );
}

/* exec a wine internal binary (either the wine loader or the wine server) */
void wine_exec_wine_binary( const char *name, char **argv, const char *env_var )
{
    const char *path, *pos, *ptr;
    int use_preloader;

    if (!name) name = argv0_name;  /* no name means default loader */

#ifdef linux
    use_preloader = !strendswith( name, "wineserver" );
#else
    use_preloader = 0;
#endif

    if ((ptr = strrchr( name, '/' )))
    {
        /* if we are in build dir and name contains a path, try that */
        if (build_dir)
        {
            argv[0] = build_path( build_dir, name );
            preloader_exec( argv, use_preloader );
            free( argv[0] );
        }
        name = ptr + 1;  /* get rid of path */
    }

    /* first, bin directory from the current libdir or argv0 */
    if (bindir)
    {
        argv[0] = build_path( bindir, name );
        preloader_exec( argv, use_preloader );
        free( argv[0] );
    }

    /* then specified environment variable */
    if (env_var)
    {
        argv[0] = (char *)env_var;
        preloader_exec( argv, use_preloader );
    }

    /* now search in the Unix path */
    if ((path = getenv( "PATH" )))
    {
        argv[0] = xmalloc( strlen(path) + strlen(name) + 2 );
        pos = path;
        for (;;)
        {
            while (*pos == ':') pos++;
            if (!*pos) break;
            if (!(ptr = strchr( pos, ':' ))) ptr = pos + strlen(pos);
            memcpy( argv[0], pos, ptr - pos );
            strcpy( argv[0] + (ptr - pos), "/" );
            strcat( argv[0] + (ptr - pos), name );
            preloader_exec( argv, use_preloader );
            pos = ptr;
        }
        free( argv[0] );
    }

    /* and finally try BINDIR */
    argv[0] = build_path( BINDIR, name );
    preloader_exec( argv, use_preloader );
    free( argv[0] );
}
