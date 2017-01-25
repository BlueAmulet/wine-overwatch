/*
 * Copyright 2015 Michael Müller
 * Copyright 2015 Sebastian Lackner
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

#include "wine/unicode.h"
#include "wine/list.h"
#include <windows.h>

enum
{
    ASSEMBLY_STATUS_NONE,
    ASSEMBLY_STATUS_IN_PROGRESS,
    ASSEMBLY_STATUS_INSTALLED,
};

struct assembly_identity
{
    WCHAR                       *name;
    WCHAR                       *version;
    WCHAR                       *architecture;
    WCHAR                       *language;
    WCHAR                       *pubkey_token;
};

struct dependency_entry
{
    struct list                 entry;
    struct assembly_identity    identity;
};

struct fileop_entry
{
    struct list                 entry;
    WCHAR                       *source;
    WCHAR                       *target;
};

struct registrykv_entry
{
    struct list                 entry;
    WCHAR                      *name;
    WCHAR                      *value_type;
    WCHAR                      *value;
};

struct registryop_entry
{
    struct list                 entry;
    WCHAR                       *key;
    struct list                 keyvalues;
};

struct assembly_entry
{
    struct list                 entry;
    DWORD                       status;
    WCHAR                       *filename;
    WCHAR                       *displayname;
    struct assembly_identity    identity;
    struct list                 dependencies;

    struct list                 fileops;
    struct list                 registryops;
};

void free_assembly(struct assembly_entry *entry) DECLSPEC_HIDDEN;
void free_dependency(struct dependency_entry *entry) DECLSPEC_HIDDEN;
struct assembly_entry *load_manifest(const WCHAR *filename) DECLSPEC_HIDDEN;
BOOL load_update(const WCHAR *filename, struct list *update_list) DECLSPEC_HIDDEN;
BOOL queue_update(struct assembly_entry *assembly, struct list *update_list) DECLSPEC_HIDDEN;

static void *heap_alloc(size_t len) __WINE_ALLOC_SIZE(1);
static inline void *heap_alloc(size_t len)
{
    return HeapAlloc(GetProcessHeap(), 0, len);
}

static void *heap_alloc_zero(size_t len) __WINE_ALLOC_SIZE(1);
static inline void *heap_alloc_zero(size_t len)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
}

static void *heap_realloc(void *mem, size_t len) __WINE_ALLOC_SIZE(2);
static inline void *heap_realloc(void *mem, size_t len)
{
    return HeapReAlloc(GetProcessHeap(), 0, mem, len);
}

static void *heap_realloc_zero(void *mem, size_t len) __WINE_ALLOC_SIZE(2);
static inline void *heap_realloc_zero(void *mem, size_t len)
{
    return HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, mem, len);
}

static inline BOOL heap_free(void *mem)
{
    return HeapFree(GetProcessHeap(), 0, mem);
}

static inline char *strdupWtoA(const WCHAR *str)
{
    char *ret = NULL;
    DWORD len;

    if (!str) return ret;
    len = WideCharToMultiByte(CP_ACP, 0, str, -1, NULL, 0, NULL, NULL);
    if ((ret = heap_alloc(len)))
        WideCharToMultiByte(CP_ACP, 0, str, -1, ret, len, NULL, NULL);
    return ret;
}

static inline WCHAR *strdupAtoW(const char *str)
{
    WCHAR *ret = NULL;
    DWORD len;

    if (!str) return ret;
    len = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0);
    if ((ret = heap_alloc(len * sizeof(WCHAR))))
        MultiByteToWideChar(CP_ACP, 0, str, -1, ret, len);
    return ret;
}

static inline WCHAR *strdupW(const WCHAR *str)
{
    WCHAR *ret;
    if (!str) return NULL;
    ret = heap_alloc((strlenW(str) + 1) * sizeof(WCHAR));
    if (ret)
        strcpyW(ret, str);
    return ret;
}

static inline WCHAR *strdupWn(const WCHAR *str, DWORD len)
{
    WCHAR *ret;
    if (!str) return NULL;
    ret = heap_alloc((len + 1) * sizeof(WCHAR));
    if (ret)
    {
        memcpy(ret, str, len * sizeof(WCHAR));
        ret[len] = 0;
    }
    return ret;
}
