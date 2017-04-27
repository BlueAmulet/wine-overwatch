/*
 * Copyright 2009 Henri Verbeet for CodeWeavers
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
 *
 */

#include "config.h"
#include "wine/port.h"

#include <stdarg.h>
#ifdef HAVE_COMMONCRYPTO_COMMONDIGEST_H
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>
#elif defined(SONAME_LIBGNUTLS)
#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>
#endif

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "ntsecapi.h"
#include "bcrypt.h"

#include "bcrypt_internal.h"

#include "wine/debug.h"
#include "wine/library.h"
#include "wine/unicode.h"

WINE_DEFAULT_DEBUG_CHANNEL(bcrypt);

static HINSTANCE instance;

#if defined(HAVE_GNUTLS_HASH) && !defined(HAVE_COMMONCRYPTO_COMMONDIGEST_H)
WINE_DECLARE_DEBUG_CHANNEL(winediag);

/* Not present in gnutls version < 3.0 */
static int (*pgnutls_cipher_tag)(gnutls_cipher_hd_t handle, void *tag, size_t tag_size);
static int (*pgnutls_cipher_add_auth)(gnutls_cipher_hd_t handle, const void *ptext, size_t ptext_size);

static void *libgnutls_handle;
#define MAKE_FUNCPTR(f) static typeof(f) * p##f
MAKE_FUNCPTR(gnutls_cipher_init);
MAKE_FUNCPTR(gnutls_cipher_deinit);
MAKE_FUNCPTR(gnutls_cipher_encrypt2);
MAKE_FUNCPTR(gnutls_cipher_decrypt2);
MAKE_FUNCPTR(gnutls_global_deinit);
MAKE_FUNCPTR(gnutls_global_init);
MAKE_FUNCPTR(gnutls_global_set_log_function);
MAKE_FUNCPTR(gnutls_global_set_log_level);
MAKE_FUNCPTR(gnutls_perror);
#undef MAKE_FUNCPTR

#if GNUTLS_VERSION_MAJOR < 3
#define GNUTLS_CIPHER_AES_192_CBC 92
#define GNUTLS_CIPHER_AES_128_GCM 93
#define GNUTLS_CIPHER_AES_256_GCM 94
#endif

static int compat_gnutls_cipher_tag(gnutls_cipher_hd_t handle, void *tag, size_t tag_size)
{
    return GNUTLS_E_UNKNOWN_CIPHER_TYPE;
}

static int compat_gnutls_cipher_add_auth(gnutls_cipher_hd_t handle, const void *ptext, size_t ptext_size)
{
    return GNUTLS_E_UNKNOWN_CIPHER_TYPE;
}

static void gnutls_log( int level, const char *msg )
{
    TRACE( "<%d> %s", level, msg );
}

static BOOL gnutls_initialize(void)
{
    int ret;

    if (!(libgnutls_handle = wine_dlopen( SONAME_LIBGNUTLS, RTLD_NOW, NULL, 0 )))
    {
        ERR_(winediag)( "failed to load libgnutls, no support for crypto hashes\n" );
        return FALSE;
    }

#define LOAD_FUNCPTR(f) \
    if (!(p##f = wine_dlsym( libgnutls_handle, #f, NULL, 0 ))) \
    { \
        ERR( "failed to load %s\n", #f ); \
        goto fail; \
    }

    LOAD_FUNCPTR(gnutls_cipher_init)
    LOAD_FUNCPTR(gnutls_cipher_deinit)
    LOAD_FUNCPTR(gnutls_cipher_encrypt2)
    LOAD_FUNCPTR(gnutls_cipher_decrypt2)
    LOAD_FUNCPTR(gnutls_global_deinit)
    LOAD_FUNCPTR(gnutls_global_init)
    LOAD_FUNCPTR(gnutls_global_set_log_function)
    LOAD_FUNCPTR(gnutls_global_set_log_level)
    LOAD_FUNCPTR(gnutls_perror)
#undef LOAD_FUNCPTR

    if (!(pgnutls_cipher_tag = wine_dlsym( libgnutls_handle, "gnutls_cipher_tag", NULL, 0 )))
    {
        WARN("gnutls_cipher_tag not found\n");
        pgnutls_cipher_tag = compat_gnutls_cipher_tag;
    }
    if (!(pgnutls_cipher_add_auth = wine_dlsym( libgnutls_handle, "gnutls_cipher_add_auth", NULL, 0 )))
    {
        WARN("gnutls_cipher_add_auth not found\n");
        pgnutls_cipher_add_auth = compat_gnutls_cipher_add_auth;
    }

    if ((ret = pgnutls_global_init()) != GNUTLS_E_SUCCESS)
    {
        pgnutls_perror( ret );
        goto fail;
    }

    if (TRACE_ON( bcrypt ))
    {
        pgnutls_global_set_log_level( 4 );
        pgnutls_global_set_log_function( gnutls_log );
    }

    return TRUE;

fail:
    wine_dlclose( libgnutls_handle, NULL, 0 );
    libgnutls_handle = NULL;
    return FALSE;
}

static void gnutls_uninitialize(void)
{
    pgnutls_global_deinit();
    wine_dlclose( libgnutls_handle, NULL, 0 );
    libgnutls_handle = NULL;
}
#endif /* HAVE_GNUTLS_HASH && !HAVE_COMMONCRYPTO_COMMONDIGEST_H */

NTSTATUS WINAPI BCryptEnumAlgorithms(ULONG dwAlgOperations, ULONG *pAlgCount,
                                     BCRYPT_ALGORITHM_IDENTIFIER **ppAlgList, ULONG dwFlags)
{
    FIXME("%08x, %p, %p, %08x - stub\n", dwAlgOperations, pAlgCount, ppAlgList, dwFlags);

    *ppAlgList=NULL;
    *pAlgCount=0;

    return STATUS_NOT_IMPLEMENTED;
}

#define MAGIC_ALG  (('A' << 24) | ('L' << 16) | ('G' << 8) | '0')
#define MAGIC_HASH (('H' << 24) | ('A' << 16) | ('S' << 8) | 'H')
#define MAGIC_KEY  (('K' << 24) | ('E' << 16) | ('Y' << 8) | '0')
struct object
{
    ULONG magic;
};

enum alg_id
{
    ALG_ID_AES,
    ALG_ID_MD5,
    ALG_ID_RNG,
    ALG_ID_SHA1,
    ALG_ID_SHA256,
    ALG_ID_SHA384,
    ALG_ID_SHA512
};

enum mode_id
{
    MODE_ID_ECB,
    MODE_ID_CBC,
    MODE_ID_GCM
};

#define MAX_HASH_OUTPUT_BYTES 64

static const struct {
    ULONG hash_length;
    const WCHAR *alg_name;
} alg_props[] = {
    /* ALG_ID_AES    */ {  0, BCRYPT_AES_ALGORITHM },
    /* ALG_ID_MD5    */ { 16, BCRYPT_MD5_ALGORITHM },
    /* ALG_ID_RNG    */ {  0, BCRYPT_RNG_ALGORITHM },
    /* ALG_ID_SHA1   */ { 20, BCRYPT_SHA1_ALGORITHM },
    /* ALG_ID_SHA256 */ { 32, BCRYPT_SHA256_ALGORITHM },
    /* ALG_ID_SHA384 */ { 48, BCRYPT_SHA384_ALGORITHM },
    /* ALG_ID_SHA512 */ { 64, BCRYPT_SHA512_ALGORITHM }
};

struct algorithm
{
    struct object hdr;
    enum alg_id   id;
    enum mode_id  mode;
    BOOL hmac;
};

#define MAX_HASH_BLOCK_BITS 1024

int alg_block_bits[] =
{
    /* ALG_ID_AES    */    0,
    /* ALG_ID_MD5    */  512,
    /* ALG_ID_RNG    */    0,
    /* ALG_ID_SHA1   */  512,
    /* ALG_ID_SHA256 */  512,
    /* ALG_ID_SHA384 */ 1024,
    /* ALG_ID_SHA512 */ 1024
};

struct key;
static NTSTATUS set_key_property( struct key *key, const WCHAR *prop, UCHAR *value, ULONG size, ULONG flags );

NTSTATUS WINAPI BCryptGenRandom(BCRYPT_ALG_HANDLE handle, UCHAR *buffer, ULONG count, ULONG flags)
{
    const DWORD supported_flags = BCRYPT_USE_SYSTEM_PREFERRED_RNG;
    struct algorithm *algorithm = handle;

    TRACE("%p, %p, %u, %08x - semi-stub\n", handle, buffer, count, flags);

    if (!algorithm)
    {
        /* It's valid to call without an algorithm if BCRYPT_USE_SYSTEM_PREFERRED_RNG
         * is set. In this case the preferred system RNG is used.
         */
        if (!(flags & BCRYPT_USE_SYSTEM_PREFERRED_RNG))
            return STATUS_INVALID_HANDLE;
    }
    else if (algorithm->hdr.magic != MAGIC_ALG || algorithm->id != ALG_ID_RNG)
        return STATUS_INVALID_HANDLE;

    if (!buffer)
        return STATUS_INVALID_PARAMETER;

    if (flags & ~supported_flags)
        FIXME("unsupported flags %08x\n", flags & ~supported_flags);

    if (algorithm)
        FIXME("ignoring selected algorithm\n");

    /* When zero bytes are requested the function returns success too. */
    if (!count)
        return STATUS_SUCCESS;

    if (algorithm || (flags & BCRYPT_USE_SYSTEM_PREFERRED_RNG))
    {
        if (RtlGenRandom(buffer, count))
            return STATUS_SUCCESS;
    }

    FIXME("called with unsupported parameters, returning error\n");
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS WINAPI BCryptOpenAlgorithmProvider( BCRYPT_ALG_HANDLE *handle, LPCWSTR id, LPCWSTR implementation, DWORD flags )
{
    const DWORD supported_flags = BCRYPT_ALG_HANDLE_HMAC_FLAG;
    struct algorithm *alg;
    enum alg_id alg_id;

    TRACE( "%p, %s, %s, %08x\n", handle, wine_dbgstr_w(id), wine_dbgstr_w(implementation), flags );

    if (!handle || !id) return STATUS_INVALID_PARAMETER;
    if (flags & ~supported_flags)
    {
        FIXME( "unsupported flags %08x\n", flags & ~supported_flags);
        return STATUS_NOT_IMPLEMENTED;
    }

    if (!strcmpW( id, BCRYPT_AES_ALGORITHM )) alg_id = ALG_ID_AES;
    else if (!strcmpW( id, BCRYPT_MD5_ALGORITHM )) alg_id = ALG_ID_MD5;
    else if (!strcmpW( id, BCRYPT_RNG_ALGORITHM )) alg_id = ALG_ID_RNG;
    else if (!strcmpW( id, BCRYPT_SHA1_ALGORITHM )) alg_id = ALG_ID_SHA1;
    else if (!strcmpW( id, BCRYPT_SHA256_ALGORITHM )) alg_id = ALG_ID_SHA256;
    else if (!strcmpW( id, BCRYPT_SHA384_ALGORITHM )) alg_id = ALG_ID_SHA384;
    else if (!strcmpW( id, BCRYPT_SHA512_ALGORITHM )) alg_id = ALG_ID_SHA512;
    else
    {
        FIXME( "algorithm %s not supported\n", debugstr_w(id) );
        return STATUS_NOT_IMPLEMENTED;
    }
    if (implementation && strcmpW( implementation, MS_PRIMITIVE_PROVIDER ))
    {
        FIXME( "implementation %s not supported\n", debugstr_w(implementation) );
        return STATUS_NOT_IMPLEMENTED;
    }

    if (!(alg = HeapAlloc( GetProcessHeap(), 0, sizeof(*alg) ))) return STATUS_NO_MEMORY;
    alg->hdr.magic = MAGIC_ALG;
    alg->id        = alg_id;
    alg->mode      = MODE_ID_CBC;
    alg->hmac      = flags & BCRYPT_ALG_HANDLE_HMAC_FLAG;

    *handle = alg;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptCloseAlgorithmProvider( BCRYPT_ALG_HANDLE handle, DWORD flags )
{
    struct algorithm *alg = handle;

    TRACE( "%p, %08x\n", handle, flags );

    if (!alg || alg->hdr.magic != MAGIC_ALG) return STATUS_INVALID_HANDLE;
    HeapFree( GetProcessHeap(), 0, alg );
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptGetFipsAlgorithmMode(BOOLEAN *enabled)
{
    FIXME("%p - semi-stub\n", enabled);

    if (!enabled)
        return STATUS_INVALID_PARAMETER;

    *enabled = FALSE;
    return STATUS_SUCCESS;
}

struct hash_impl
{
    union
    {
        MD5_CTX md5;
        SHA_CTX sha1;
        SHA256_CTX sha256;
        SHA512_CTX sha512;
    } u;
};

static NTSTATUS hash_init( struct hash_impl *hash, enum alg_id alg_id )
{
    switch (alg_id)
    {
    case ALG_ID_MD5:
        MD5Init(&hash->u.md5);
        break;

    case ALG_ID_SHA1:
        A_SHAInit(&hash->u.sha1);
        break;

    case ALG_ID_SHA256:
        sha256_init(&hash->u.sha256);
        break;

    case ALG_ID_SHA384:
        sha384_init(&hash->u.sha512);
        break;

    case ALG_ID_SHA512:
        sha512_init(&hash->u.sha512);
        break;

    default:
        ERR( "unhandled id %u\n", alg_id );
        return STATUS_NOT_IMPLEMENTED;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS hash_update( struct hash_impl *hash, enum alg_id alg_id,
                             UCHAR *input, ULONG size )
{
    switch (alg_id)
    {
    case ALG_ID_MD5:
        MD5Update(&hash->u.md5, input, size);
        break;

    case ALG_ID_SHA1:
        A_SHAUpdate(&hash->u.sha1, input, size);
        break;

    case ALG_ID_SHA256:
        sha256_update(&hash->u.sha256, input, size);
        break;

    case ALG_ID_SHA384:
        sha384_update(&hash->u.sha512, input, size);
        break;

    case ALG_ID_SHA512:
        sha512_update(&hash->u.sha512, input, size);
        break;

    default:
        ERR( "unhandled id %u\n", alg_id );
        return STATUS_NOT_IMPLEMENTED;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS hash_finish( struct hash_impl *hash, enum alg_id alg_id,
                             UCHAR *output, ULONG size )
{
    switch (alg_id)
    {
    case ALG_ID_MD5:
        MD5Final(&hash->u.md5);
        memcpy(output, hash->u.md5.digest, 16);
        break;

    case ALG_ID_SHA1:
        A_SHAFinal(&hash->u.sha1, (ULONG*)output);
        break;

    case ALG_ID_SHA256:
        sha256_finalize(&hash->u.sha256, output);
        break;

    case ALG_ID_SHA384:
        sha384_finalize(&hash->u.sha512, output);
        break;

    case ALG_ID_SHA512:
        sha512_finalize(&hash->u.sha512, output);
        break;

    default:
        ERR( "unhandled id %u\n", alg_id );
        return STATUS_NOT_IMPLEMENTED;
    }

    return STATUS_SUCCESS;
}

struct hash
{
    struct object    hdr;
    enum alg_id      alg_id;
    BOOL hmac;
    struct hash_impl outer;
    struct hash_impl inner;
};

#ifdef _WIN64
#define OBJECT_LENGTH_AES       654
#else
#define OBJECT_LENGTH_AES       618
#endif
#define OBJECT_LENGTH_MD5       274
#define OBJECT_LENGTH_SHA1      278
#define OBJECT_LENGTH_SHA256    286
#define OBJECT_LENGTH_SHA384    382
#define OBJECT_LENGTH_SHA512    382

#define BLOCK_LENGTH_AES        16

static NTSTATUS generic_alg_property( enum alg_id id, const WCHAR *prop, UCHAR *buf, ULONG size, ULONG *ret_size )
{
    if (!strcmpW( prop, BCRYPT_HASH_LENGTH ))
    {
        *ret_size = sizeof(ULONG);
        if (size < sizeof(ULONG))
            return STATUS_BUFFER_TOO_SMALL;
        if(buf)
            *(ULONG*)buf = alg_props[id].hash_length;
        return STATUS_SUCCESS;
    }

    if (!strcmpW( prop, BCRYPT_ALGORITHM_NAME ))
    {
        *ret_size = (strlenW(alg_props[id].alg_name)+1)*sizeof(WCHAR);
        if (size < *ret_size)
            return STATUS_BUFFER_TOO_SMALL;
        if(buf)
            memcpy(buf, alg_props[id].alg_name, *ret_size);
        return STATUS_SUCCESS;
    }

    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS get_alg_property( const struct algorithm *alg, const WCHAR *prop, UCHAR *buf, ULONG size, ULONG *ret_size )
{
    NTSTATUS status;
    ULONG value;

    status = generic_alg_property( alg->id, prop, buf, size, ret_size );
    if (status != STATUS_NOT_IMPLEMENTED)
        return status;

    switch (alg->id)
    {
    case ALG_ID_AES:
        if (!strcmpW( prop, BCRYPT_BLOCK_LENGTH ))
        {
            value = BLOCK_LENGTH_AES;
            break;
        }
        if (!strcmpW( prop, BCRYPT_OBJECT_LENGTH ))
        {
            value = OBJECT_LENGTH_AES;
            break;
        }
        if (!strcmpW( prop, BCRYPT_CHAINING_MODE ))
        {
            const WCHAR *mode;
            switch (alg->mode)
            {
                case MODE_ID_ECB: mode = BCRYPT_CHAIN_MODE_ECB; break;
                case MODE_ID_CBC: mode = BCRYPT_CHAIN_MODE_CBC; break;
                case MODE_ID_GCM: mode = BCRYPT_CHAIN_MODE_GCM; break;
                default: return STATUS_NOT_IMPLEMENTED;
            }

            *ret_size = 64;
            if (size < *ret_size) return STATUS_BUFFER_TOO_SMALL;
            memcpy( buf, mode, (strlenW(mode) + 1) * sizeof(WCHAR) );
            return STATUS_SUCCESS;
        }
        if (!strcmpW( prop, BCRYPT_AUTH_TAG_LENGTH ))
        {
            BCRYPT_AUTH_TAG_LENGTHS_STRUCT *tag_length = (void *)buf;
            if (alg->mode != MODE_ID_GCM) return STATUS_NOT_SUPPORTED;
            *ret_size = sizeof(*tag_length);
            if (tag_length && size < *ret_size) return STATUS_BUFFER_TOO_SMALL;
            if (tag_length)
            {
                tag_length->dwMinLength = 12;
                tag_length->dwMaxLength = 16;
                tag_length->dwIncrement =  1;
            }
            return STATUS_SUCCESS;
        }
        FIXME( "unsupported aes algorithm property %s\n", debugstr_w(prop) );
        return STATUS_NOT_IMPLEMENTED;

    case ALG_ID_MD5:
        if (!strcmpW( prop, BCRYPT_OBJECT_LENGTH ))
        {
            value = OBJECT_LENGTH_MD5;
            break;
        }
        FIXME( "unsupported md5 algorithm property %s\n", debugstr_w(prop) );
        return STATUS_NOT_IMPLEMENTED;

    case ALG_ID_RNG:
        if (!strcmpW( prop, BCRYPT_OBJECT_LENGTH )) return STATUS_NOT_SUPPORTED;
        FIXME( "unsupported rng algorithm property %s\n", debugstr_w(prop) );
        return STATUS_NOT_IMPLEMENTED;

    case ALG_ID_SHA1:
        if (!strcmpW( prop, BCRYPT_OBJECT_LENGTH ))
        {
            value = OBJECT_LENGTH_SHA1;
            break;
        }
        FIXME( "unsupported sha1 algorithm property %s\n", debugstr_w(prop) );
        return STATUS_NOT_IMPLEMENTED;

    case ALG_ID_SHA256:
        if (!strcmpW( prop, BCRYPT_OBJECT_LENGTH ))
        {
            value = OBJECT_LENGTH_SHA256;
            break;
        }
        FIXME( "unsupported sha256 algorithm property %s\n", debugstr_w(prop) );
        return STATUS_NOT_IMPLEMENTED;

    case ALG_ID_SHA384:
        if (!strcmpW( prop, BCRYPT_OBJECT_LENGTH ))
        {
            value = OBJECT_LENGTH_SHA384;
            break;
        }
        FIXME( "unsupported sha384 algorithm property %s\n", debugstr_w(prop) );
        return STATUS_NOT_IMPLEMENTED;

    case ALG_ID_SHA512:
        if (!strcmpW( prop, BCRYPT_OBJECT_LENGTH ))
        {
            value = OBJECT_LENGTH_SHA512;
            break;
        }
        FIXME( "unsupported sha512 algorithm property %s\n", debugstr_w(prop) );
        return STATUS_NOT_IMPLEMENTED;

    default:
        FIXME( "unsupported algorithm %u\n", alg->id );
        return STATUS_NOT_IMPLEMENTED;
    }

    if (size < sizeof(ULONG))
    {
        *ret_size = sizeof(ULONG);
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (buf) *(ULONG *)buf = value;
    *ret_size = sizeof(ULONG);

    return STATUS_SUCCESS;
}

static NTSTATUS set_alg_property( struct algorithm *alg, const WCHAR *prop, UCHAR *value, ULONG size, ULONG flags )
{
    switch (alg->id)
    {
    case ALG_ID_AES:
        if (!strcmpW( prop, BCRYPT_CHAINING_MODE ))
        {
            if (!strncmpW( (WCHAR *)value, BCRYPT_CHAIN_MODE_ECB, size ))
            {
                alg->mode = MODE_ID_ECB;
                return STATUS_SUCCESS;
            }
            else if (!strncmpW( (WCHAR *)value, BCRYPT_CHAIN_MODE_CBC, size ))
            {
                alg->mode = MODE_ID_CBC;
                return STATUS_SUCCESS;
            }
            else if (!strncmpW( (WCHAR *)value, BCRYPT_CHAIN_MODE_GCM, size ))
            {
                alg->mode = MODE_ID_GCM;
                return STATUS_SUCCESS;
            }
            else
            {
                FIXME( "unsupported mode %s\n", debugstr_wn( (WCHAR *)value, size ) );
                return STATUS_NOT_IMPLEMENTED;
            }
        }
        FIXME( "unsupported aes algorithm property %s\n", debugstr_w(prop) );
        return STATUS_NOT_IMPLEMENTED;

    default:
        FIXME( "unsupported algorithm %u\n", alg->id );
        return STATUS_NOT_IMPLEMENTED;
    }
}

static NTSTATUS get_hash_property( const struct hash *hash, const WCHAR *prop, UCHAR *buf, ULONG size, ULONG *ret_size )
{
    NTSTATUS status;

    status = generic_alg_property( hash->alg_id, prop, buf, size, ret_size );
    if (status == STATUS_NOT_IMPLEMENTED)
        FIXME( "unsupported property %s\n", debugstr_w(prop) );
    return status;
}

NTSTATUS WINAPI BCryptGetProperty( BCRYPT_HANDLE handle, LPCWSTR prop, UCHAR *buffer, ULONG count, ULONG *res, ULONG flags )
{
    struct object *object = handle;

    TRACE( "%p, %s, %p, %u, %p, %08x\n", handle, wine_dbgstr_w(prop), buffer, count, res, flags );

    if (!object) return STATUS_INVALID_HANDLE;
    if (!prop || !res) return STATUS_INVALID_PARAMETER;

    switch (object->magic)
    {
    case MAGIC_ALG:
    {
        const struct algorithm *alg = (const struct algorithm *)object;
        return get_alg_property( alg, prop, buffer, count, res );
    }
    case MAGIC_HASH:
    {
        const struct hash *hash = (const struct hash *)object;
        return get_hash_property( hash, prop, buffer, count, res );
    }
    default:
        WARN( "unknown magic %08x\n", object->magic );
        return STATUS_INVALID_HANDLE;
    }
}

NTSTATUS WINAPI BCryptSetProperty( BCRYPT_HANDLE handle, const WCHAR *prop, UCHAR *value,
                                   ULONG size, ULONG flags )
{
    struct object *object = handle;

    TRACE( "%p, %s, %p, %u, %08x\n", handle, debugstr_w(prop), value, size, flags );

    if (!object) return STATUS_INVALID_HANDLE;

    switch (object->magic)
    {
    case MAGIC_ALG:
    {
        struct algorithm *alg = (struct algorithm *)object;
        return set_alg_property( alg, prop, value, size, flags );
    }
    case MAGIC_KEY:
    {
        struct key *key = (struct key *)object;
        return set_key_property( key, prop, value, size, flags );
    }
    default:
        WARN( "unknown magic %08x\n", object->magic );
        return STATUS_INVALID_HANDLE;
    }
}

NTSTATUS WINAPI BCryptCreateHash( BCRYPT_ALG_HANDLE algorithm, BCRYPT_HASH_HANDLE *handle, UCHAR *object, ULONG objectlen,
                                  UCHAR *secret, ULONG secretlen, ULONG flags )
{
    struct algorithm *alg = algorithm;
    UCHAR buffer[MAX_HASH_BLOCK_BITS / 8];
    struct hash *hash;
    int block_bytes;
    NTSTATUS status;
    int i;

    TRACE( "%p, %p, %p, %u, %p, %u, %08x - stub\n", algorithm, handle, object, objectlen,
           secret, secretlen, flags );
    if (flags)
    {
        FIXME( "unimplemented flags %08x\n", flags );
        return STATUS_NOT_IMPLEMENTED;
    }

    if (!alg || alg->hdr.magic != MAGIC_ALG) return STATUS_INVALID_HANDLE;
    if (object) FIXME( "ignoring object buffer\n" );

    if (!(hash = HeapAlloc( GetProcessHeap(), 0, sizeof(*hash) ))) return STATUS_NO_MEMORY;
    hash->hdr.magic = MAGIC_HASH;
    hash->alg_id    = alg->id;
    hash->hmac      = alg->hmac;

    status = hash_init( &hash->inner, hash->alg_id );
    if (status || !hash->hmac) goto end;
    status = hash_init( &hash->outer, hash->alg_id );
    if (status) goto end;

    /* reduce key size if too big */
    block_bytes = alg_block_bits[hash->alg_id] / 8;
    if (secretlen > block_bytes)
    {
        struct hash_impl temp;
        status = hash_init( &temp, hash->alg_id );
        if (status) goto end;
        status = hash_update( &temp, hash->alg_id, secret, secretlen );
        if (status) goto end;
        memset( buffer, 0, block_bytes );
        status = hash_finish( &temp, hash->alg_id, buffer, alg_props[hash->alg_id].hash_length );
        if (status) goto end;
    }
    else
    {
        memset( buffer, 0, block_bytes );
        memcpy( buffer, secret, secretlen );
    }

    /* initialize outer hash */
    for (i = 0; i < block_bytes; i++)
        buffer[i] ^= 0x5c;
    status = hash_update( &hash->outer, hash->alg_id, buffer, block_bytes );
    if (status) goto end;

    /* initialize inner hash */
    for (i = 0; i < block_bytes; i++)
        buffer[i] ^= (0x5c ^ 0x36);
    status = hash_update( &hash->inner, hash->alg_id, buffer, block_bytes );

end:
    if (status != STATUS_SUCCESS)
    {
        /* FIXME: call hash_finish to release resources */
        HeapFree( GetProcessHeap(), 0, hash );
        return status;
    }

    *handle = hash;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptDuplicateHash( BCRYPT_HASH_HANDLE handle, BCRYPT_HASH_HANDLE *handle_copy,
                                     UCHAR *object, ULONG object_count, ULONG flags )
{
    struct hash *hash_orig = handle;
    struct hash *hash_copy;

    TRACE( "%p, %p, %p, %u, %u\n", handle, handle_copy, object, object_count, flags );

    if (!hash_orig || hash_orig->hdr.magic != MAGIC_HASH) return STATUS_INVALID_HANDLE;
    if (!handle_copy) return STATUS_INVALID_PARAMETER;
    if (!(hash_copy = HeapAlloc( GetProcessHeap(), 0, sizeof(*hash_copy) )))
        return STATUS_NO_MEMORY;

    memcpy( hash_copy, hash_orig, sizeof(*hash_orig) );

    *handle_copy = hash_copy;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptDestroyHash( BCRYPT_HASH_HANDLE handle )
{
    struct hash *hash = handle;

    TRACE( "%p\n", handle );

    if (!hash || hash->hdr.magic != MAGIC_HASH) return STATUS_INVALID_HANDLE;
    HeapFree( GetProcessHeap(), 0, hash );
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptHashData( BCRYPT_HASH_HANDLE handle, UCHAR *input, ULONG size, ULONG flags )
{
    struct hash *hash = handle;

    TRACE( "%p, %p, %u, %08x\n", handle, input, size, flags );

    if (!hash || hash->hdr.magic != MAGIC_HASH) return STATUS_INVALID_HANDLE;
    if (!input) return STATUS_SUCCESS;

    return hash_update( &hash->inner, hash->alg_id, input, size );
}

NTSTATUS WINAPI BCryptFinishHash( BCRYPT_HASH_HANDLE handle, UCHAR *output, ULONG size, ULONG flags )
{
    UCHAR buffer[MAX_HASH_OUTPUT_BYTES];
    struct hash *hash = handle;
    NTSTATUS status;
    int hash_size;

    TRACE( "%p, %p, %u, %08x\n", handle, output, size, flags );

    if (!hash || hash->hdr.magic != MAGIC_HASH) return STATUS_INVALID_HANDLE;
    if (!output) return STATUS_INVALID_PARAMETER;

    if (!hash->hmac)
        return hash_finish( &hash->inner, hash->alg_id, output, size );

    hash_size = alg_props[hash->alg_id].hash_length;

    status = hash_finish( &hash->inner, hash->alg_id, buffer, hash_size);
    if (status) return status;

    status = hash_update( &hash->outer, hash->alg_id, buffer, hash_size);
    if (status) return status;

    return hash_finish( &hash->outer, hash->alg_id, output, size);
}

NTSTATUS WINAPI BCryptHash( BCRYPT_ALG_HANDLE algorithm, UCHAR *secret, ULONG secretlen,
                            UCHAR *input, ULONG inputlen, UCHAR *output, ULONG outputlen )
{
    NTSTATUS status;
    BCRYPT_HASH_HANDLE handle;

    TRACE( "%p, %p, %u, %p, %u, %p, %u\n", algorithm, secret, secretlen,
           input, inputlen, output, outputlen );

    status = BCryptCreateHash( algorithm, &handle, NULL, 0, secret, secretlen, 0);
    if (status != STATUS_SUCCESS)
    {
        return status;
    }

    status = BCryptHashData( handle, input, inputlen, 0 );
    if (status != STATUS_SUCCESS)
    {
        BCryptDestroyHash( handle );
        return status;
    }

    status = BCryptFinishHash( handle, output, outputlen, 0 );
    if (status != STATUS_SUCCESS)
    {
        BCryptDestroyHash( handle );
        return status;
    }

    return BCryptDestroyHash( handle );
}

#if defined(HAVE_GNUTLS_HASH) && !defined(HAVE_COMMONCRYPTO_COMMONDIGEST_H)
struct key
{
    struct object      hdr;
    enum alg_id        alg_id;
    enum mode_id       mode;
    ULONG              block_size;
    gnutls_cipher_hd_t handle;
    UCHAR             *secret;
    ULONG              secret_len;
};

static ULONG get_block_size( struct algorithm *alg )
{
    ULONG ret = 0, size = sizeof(ret);
    get_alg_property( alg, BCRYPT_BLOCK_LENGTH, (UCHAR *)&ret, sizeof(ret), &size );
    return ret;
}

static NTSTATUS key_init( struct key *key, struct algorithm *alg, UCHAR *secret, ULONG secret_len )
{
    UCHAR *buffer;

    if (!libgnutls_handle) return STATUS_INTERNAL_ERROR;

    switch (alg->id)
    {
    case ALG_ID_AES:
        break;

    default:
        FIXME( "algorithm %u not supported\n", alg->id );
        return STATUS_NOT_SUPPORTED;
    }

    if (!(key->block_size = get_block_size( alg ))) return STATUS_INVALID_PARAMETER;
    if (!(buffer = HeapAlloc( GetProcessHeap(), 0, secret_len ))) return STATUS_NO_MEMORY;
    memcpy( buffer, secret, secret_len );

    key->alg_id     = alg->id;
    key->mode       = alg->mode;
    key->handle     = 0;        /* initialized on first use */
    key->secret     = buffer;
    key->secret_len = secret_len;

    return STATUS_SUCCESS;
}

static NTSTATUS key_duplicate( struct key *key_orig, struct key *key_copy )
{
    UCHAR *buffer;

    if (!(buffer = HeapAlloc( GetProcessHeap(), 0, key_orig->secret_len ))) return STATUS_NO_MEMORY;
    memcpy( buffer, key_orig->secret, key_orig->secret_len );

    key_copy->hdr           = key_orig->hdr;
    key_copy->alg_id        = key_orig->alg_id;
    key_copy->mode          = key_orig->mode;
    key_copy->block_size    = key_orig->block_size;
    key_copy->handle        = NULL;
    key_copy->secret        = buffer;
    key_copy->secret_len    = key_orig->secret_len;

    return STATUS_SUCCESS;
}

static NTSTATUS set_key_property( struct key *key, const WCHAR *prop, UCHAR *value, ULONG size, ULONG flags )
{
    if (!strcmpW( prop, BCRYPT_CHAINING_MODE ))
    {
        if (!strncmpW( (WCHAR *)value, BCRYPT_CHAIN_MODE_ECB, size ))
        {
            key->mode = MODE_ID_ECB;
            return STATUS_SUCCESS;
        }
        else if (!strncmpW( (WCHAR *)value, BCRYPT_CHAIN_MODE_CBC, size ))
        {
            key->mode = MODE_ID_CBC;
            return STATUS_SUCCESS;
        }
        else if (!strncmpW( (WCHAR *)value, BCRYPT_CHAIN_MODE_GCM, size ))
        {
            key->mode = MODE_ID_GCM;
            return STATUS_SUCCESS;
        }
        else
        {
            FIXME( "unsupported mode %s\n", debugstr_wn( (WCHAR *)value, size ) );
            return STATUS_NOT_IMPLEMENTED;
        }
    }

    FIXME( "unsupported key property %s\n", debugstr_w(prop) );
    return STATUS_NOT_IMPLEMENTED;
}

static gnutls_cipher_algorithm_t get_gnutls_cipher( const struct key *key )
{
    switch (key->alg_id)
    {
    case ALG_ID_AES:
        WARN( "handle block size\n" );
        switch (key->mode)
        {
            case MODE_ID_GCM: return GNUTLS_CIPHER_AES_128_GCM;
            case MODE_ID_ECB: /* can be emulated with CBC + empty IV */
            case MODE_ID_CBC:
            default:          return GNUTLS_CIPHER_AES_128_CBC;
        }
    default:
        FIXME( "algorithm %u not supported\n", key->alg_id );
        return GNUTLS_CIPHER_UNKNOWN;
    }
}

static NTSTATUS key_set_params( struct key *key, UCHAR *iv, ULONG iv_len )
{
    static const UCHAR zero_iv[16];
    gnutls_cipher_algorithm_t cipher;
    gnutls_datum_t secret, vector;
    int ret;

    if (key->handle)
    {
        pgnutls_cipher_deinit( key->handle );
        key->handle = NULL;
    }

    if ((cipher = get_gnutls_cipher( key )) == GNUTLS_CIPHER_UNKNOWN)
        return STATUS_NOT_SUPPORTED;

    if (!iv)
    {
        iv      = (UCHAR *)zero_iv;
        iv_len  = sizeof(zero_iv);
    }

    secret.data = key->secret;
    secret.size = key->secret_len;
    vector.data = iv;
    vector.size = iv_len;

    if ((ret = pgnutls_cipher_init( &key->handle, cipher, &secret, &vector )))
    {
        pgnutls_perror( ret );
        return STATUS_INTERNAL_ERROR;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS key_set_auth_data( struct key *key, UCHAR *auth_data, ULONG len )
{
    int ret;

    if ((ret = pgnutls_cipher_add_auth( key->handle, auth_data, len )))
    {
        pgnutls_perror( ret );
        return STATUS_INTERNAL_ERROR;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS key_encrypt( struct key *key, const UCHAR *input, ULONG input_len, UCHAR *output,
                             ULONG output_len )
{
    int ret;

    if ((ret = pgnutls_cipher_encrypt2( key->handle, input, input_len, output, output_len )))
    {
        pgnutls_perror( ret );
        return STATUS_INTERNAL_ERROR;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS key_decrypt( struct key *key, const UCHAR *input, ULONG input_len, UCHAR *output,
                             ULONG output_len  )
{
    int ret;

    if ((ret = pgnutls_cipher_decrypt2( key->handle, input, input_len, output, output_len )))
    {
        pgnutls_perror( ret );
        return STATUS_INTERNAL_ERROR;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS key_get_tag( struct key *key, UCHAR *tag, ULONG len )
{
    int ret;

    if ((ret = pgnutls_cipher_tag( key->handle, tag, len )))
    {
        pgnutls_perror( ret );
        return STATUS_INTERNAL_ERROR;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS key_destroy( struct key *key )
{
    if (key->handle) pgnutls_cipher_deinit( key->handle );
    HeapFree( GetProcessHeap(), 0, key->secret );
    HeapFree( GetProcessHeap(), 0, key );
    return STATUS_SUCCESS;
}
#else
struct key
{
    struct object hdr;
    enum mode_id  mode;
    ULONG         block_size;
};

static NTSTATUS key_init( struct key *key, struct algorithm *alg, UCHAR *secret, ULONG secret_len )
{
    ERR( "support for keys not available at build time\n" );
    key->mode = MODE_ID_CBC;
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS key_duplicate( struct key *key_orig, struct key *key_copy )
{
    ERR( "support for keys not available at build time\n" );
    key_copy->mode = MODE_ID_CBC;
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS set_key_property( struct key *key, const WCHAR *prop, UCHAR *value, ULONG size, ULONG flags )
{
    ERR( "support for keys not available at build time\n" );
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS key_set_params( struct key *key, UCHAR *iv, ULONG iv_len )
{
    ERR( "support for keys not available at build time\n" );
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS key_set_auth_data( struct key *key, UCHAR *auth_data, ULONG len )
{
    ERR( "support for keys not available at build time\n" );
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS key_encrypt( struct key *key, const UCHAR *input, ULONG input_len, UCHAR *output,
                             ULONG output_len  )
{
    ERR( "support for keys not available at build time\n" );
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS key_decrypt( struct key *key, const UCHAR *input, ULONG input_len, UCHAR *output,
                             ULONG output_len )
{
    ERR( "support for keys not available at build time\n" );
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS key_get_tag( struct key *key, UCHAR *tag, ULONG len )
{
    ERR( "support for keys not available at build time\n" );
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS key_destroy( struct key *key )
{
    ERR( "support for keys not available at build time\n" );
    return STATUS_NOT_IMPLEMENTED;
}
#endif

NTSTATUS WINAPI BCryptGenerateSymmetricKey( BCRYPT_ALG_HANDLE algorithm, BCRYPT_KEY_HANDLE *handle,
                                            UCHAR *object, ULONG object_len, UCHAR *secret, ULONG secret_len,
                                            ULONG flags )
{
    struct algorithm *alg = algorithm;
    struct key *key;
    NTSTATUS status;

    TRACE( "%p, %p, %p, %u, %p, %u, %08x\n", alg, handle, object, object_len, secret, secret_len, flags );

    if (!alg || alg->hdr.magic != MAGIC_ALG) return STATUS_INVALID_HANDLE;
    if (object) FIXME( "ignoring object buffer\n" );

    if (!(key = HeapAlloc( GetProcessHeap(), 0, sizeof(*key) )))
    {
        *handle = NULL;
        return STATUS_NO_MEMORY;
    }
    key->hdr.magic = MAGIC_KEY;

    if ((status = key_init( key, alg, secret, secret_len )))
    {
        HeapFree( GetProcessHeap(), 0, key );
        *handle = NULL;
        return status;
    }

    *handle = key;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptDuplicateKey( BCRYPT_KEY_HANDLE handle, BCRYPT_KEY_HANDLE *handle_copy,
                                    UCHAR *object, ULONG object_len, ULONG flags )
{
    struct key *key_orig = handle;
    struct key *key_copy;
    NTSTATUS status;

    TRACE( "%p, %p, %p, %u, %08x\n", handle, handle_copy, object, object_len, flags );

    if (!key_orig || key_orig->hdr.magic != MAGIC_KEY) return STATUS_INVALID_HANDLE;
    if (!handle_copy) return STATUS_INVALID_PARAMETER;
    if (!(key_copy = HeapAlloc( GetProcessHeap(), 0, sizeof(*key_copy) )))
    {
        *handle_copy = NULL;
        return STATUS_NO_MEMORY;
    }

    if ((status = key_duplicate( key_orig, key_copy )))
    {
        HeapFree( GetProcessHeap(), 0, key_copy );
        *handle_copy = NULL;
        return status;
    }

    *handle_copy = key_copy;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptDestroyKey( BCRYPT_KEY_HANDLE handle )
{
    struct key *key = handle;

    TRACE( "%p\n", handle );

    if (!key || key->hdr.magic != MAGIC_KEY) return STATUS_INVALID_HANDLE;
    return key_destroy( key );
}

NTSTATUS WINAPI BCryptEncrypt( BCRYPT_KEY_HANDLE handle, UCHAR *input, ULONG input_len,
                               void *padding, UCHAR *iv, ULONG iv_len, UCHAR *output,
                               ULONG output_len, ULONG *ret_len, ULONG flags )
{
    struct key *key = handle;
    ULONG bytes_left = input_len;
    UCHAR *buf, *src, *dst;
    NTSTATUS status;

    TRACE( "%p, %p, %u, %p, %p, %u, %p, %u, %p, %08x\n", handle, input, input_len,
           padding, iv, iv_len, output, output_len, ret_len, flags );

    if (!key || key->hdr.magic != MAGIC_KEY) return STATUS_INVALID_HANDLE;
    if (flags & ~BCRYPT_BLOCK_PADDING)
    {
        FIXME( "flags %08x not implemented\n", flags );
        return STATUS_NOT_IMPLEMENTED;
    }

    if (key->mode == MODE_ID_GCM)
    {
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO *auth_info = padding;

        if (!auth_info) return STATUS_INVALID_PARAMETER;
        if (!auth_info->pbNonce) return STATUS_INVALID_PARAMETER;
        if (!auth_info->pbTag) return STATUS_INVALID_PARAMETER;
        if (auth_info->cbTag < 12 || auth_info->cbTag > 16) return STATUS_INVALID_PARAMETER;
        if (auth_info->dwFlags & BCRYPT_AUTH_MODE_CHAIN_CALLS_FLAG)
            FIXME( "call chaining not implemented\n" );

        if ((status = key_set_params( key, auth_info->pbNonce, auth_info->cbNonce )))
            return status;

        *ret_len = input_len;
        if (flags & BCRYPT_BLOCK_PADDING) return STATUS_INVALID_PARAMETER;
        if (!output) return STATUS_SUCCESS;
        if (output_len < *ret_len) return STATUS_BUFFER_TOO_SMALL;

        if (auth_info->pbAuthData && (status = key_set_auth_data( key, auth_info->pbAuthData, auth_info->cbAuthData )))
            return status;
        if ((status = key_encrypt( key, input, input_len, output, output_len )))
            return status;

        return key_get_tag( key, auth_info->pbTag, auth_info->cbTag );
    }

    if ((status = key_set_params( key, iv, iv_len ))) return status;

    *ret_len = input_len;

    if (flags & BCRYPT_BLOCK_PADDING)
        *ret_len = (input_len + key->block_size) & ~(key->block_size - 1);
    else if (input_len & (key->block_size - 1))
        return STATUS_INVALID_BUFFER_SIZE;

    if (!output) return STATUS_SUCCESS;
    if (output_len < *ret_len) return STATUS_BUFFER_TOO_SMALL;

    if (key->mode == MODE_ID_ECB && iv)
        return STATUS_INVALID_PARAMETER;

    src = input;
    dst = output;
    while (bytes_left >= key->block_size)
    {
        if ((status = key_encrypt( key, src, key->block_size, dst, key->block_size ))) return status;
        if (key->mode == MODE_ID_ECB && (status = key_set_params( key, iv, iv_len ))) return status;
        bytes_left -= key->block_size;
        src += key->block_size;
        dst += key->block_size;
    }

    if (flags & BCRYPT_BLOCK_PADDING)
    {
        if (!(buf = HeapAlloc( GetProcessHeap(), 0, key->block_size ))) return STATUS_NO_MEMORY;
        memcpy( buf, src, bytes_left );
        memset( buf + bytes_left, key->block_size - bytes_left, key->block_size - bytes_left );
        status = key_encrypt( key, buf, key->block_size, dst, key->block_size );
        HeapFree( GetProcessHeap(), 0, buf );
    }

    return status;
}

NTSTATUS WINAPI BCryptDecrypt( BCRYPT_KEY_HANDLE handle, UCHAR *input, ULONG input_len,
                               void *padding, UCHAR *iv, ULONG iv_len, UCHAR *output,
                               ULONG output_len, ULONG *ret_len, ULONG flags )
{
    struct key *key = handle;
    ULONG bytes_left = input_len;
    UCHAR *buf, *src, *dst;
    NTSTATUS status;

    TRACE( "%p, %p, %u, %p, %p, %u, %p, %u, %p, %08x\n", handle, input, input_len,
           padding, iv, iv_len, output, output_len, ret_len, flags );

    if (!key || key->hdr.magic != MAGIC_KEY) return STATUS_INVALID_HANDLE;
    if (flags & ~BCRYPT_BLOCK_PADDING)
    {
        FIXME( "flags %08x not supported\n", flags );
        return STATUS_NOT_IMPLEMENTED;
    }

    if (key->mode == MODE_ID_GCM)
    {
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO *auth_info = padding;
        UCHAR tag[16];

        if (!auth_info) return STATUS_INVALID_PARAMETER;
        if (!auth_info->pbNonce) return STATUS_INVALID_PARAMETER;
        if (!auth_info->pbTag) return STATUS_INVALID_PARAMETER;
        if (auth_info->cbTag < 12 || auth_info->cbTag > 16) return STATUS_INVALID_PARAMETER;

        if ((status = key_set_params( key, auth_info->pbNonce, auth_info->cbNonce )))
            return status;

        *ret_len = input_len;
        if (flags & BCRYPT_BLOCK_PADDING) return STATUS_INVALID_PARAMETER;
        if (!output) return STATUS_SUCCESS;
        if (output_len < *ret_len) return STATUS_BUFFER_TOO_SMALL;

        if (auth_info->pbAuthData && (status = key_set_auth_data( key, auth_info->pbAuthData, auth_info->cbAuthData )))
            return status;
        if ((status = key_decrypt( key, input, input_len, output, output_len )))
            return status;

        if ((status = key_get_tag( key, tag, sizeof(tag) )))
            return status;
        if (memcmp( tag, auth_info->pbTag, auth_info->cbTag ))
            return STATUS_AUTH_TAG_MISMATCH;

        return STATUS_SUCCESS;
    }

    if ((status = key_set_params( key, iv, iv_len ))) return status;

    *ret_len = input_len;

    if (input_len & (key->block_size - 1)) return STATUS_INVALID_BUFFER_SIZE;
    if (!output) return STATUS_SUCCESS;
    if (flags & BCRYPT_BLOCK_PADDING)
    {
        if (output_len + key->block_size < *ret_len) return STATUS_BUFFER_TOO_SMALL;
        if (input_len < key->block_size) return STATUS_BUFFER_TOO_SMALL;
        bytes_left -= key->block_size;
    }
    else if (output_len < *ret_len)
        return STATUS_BUFFER_TOO_SMALL;

    if (key->mode == MODE_ID_ECB && iv)
        return STATUS_INVALID_PARAMETER;

    src = input;
    dst = output;
    while (bytes_left >= key->block_size)
    {
        if ((status = key_decrypt( key, src, key->block_size, dst, key->block_size ))) return status;
        if (key->mode == MODE_ID_ECB && (status = key_set_params( key, iv, iv_len ))) return status;
        bytes_left -= key->block_size;
        src += key->block_size;
        dst += key->block_size;
    }

    if (flags & BCRYPT_BLOCK_PADDING)
    {
        if (!(buf = HeapAlloc( GetProcessHeap(), 0, key->block_size ))) return STATUS_NO_MEMORY;
        status = key_decrypt( key, src, key->block_size, buf, key->block_size );
        if (!status && buf[ key->block_size - 1 ] <= key->block_size)
        {
            *ret_len -= buf[ key->block_size - 1 ];
            if (output_len < *ret_len) status = STATUS_BUFFER_TOO_SMALL;
            else memcpy( dst, buf, key->block_size - buf[ key->block_size - 1 ] );
        }
        else
            status = STATUS_UNSUCCESSFUL; /* FIXME: invalid padding */
        HeapFree( GetProcessHeap(), 0, buf );
    }

    return status;
}

BOOL WINAPI DllMain( HINSTANCE hinst, DWORD reason, LPVOID reserved )
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        instance = hinst;
        DisableThreadLibraryCalls( hinst );
#if defined(HAVE_GNUTLS_HASH) && !defined(HAVE_COMMONCRYPTO_COMMONDIGEST_H)
        gnutls_initialize();
#endif
        break;

    case DLL_PROCESS_DETACH:
        if (reserved) break;
#if defined(HAVE_GNUTLS_HASH) && !defined(HAVE_COMMONCRYPTO_COMMONDIGEST_H)
        gnutls_uninitialize();
#endif
        break;
    }
    return TRUE;
}
