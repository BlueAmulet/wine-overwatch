/*
 * Server-side file mapping management
 *
 * Copyright (C) 1999 Alexandre Julliard
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

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_MMAN_H
# include <sys/mman.h>
#endif
#include <unistd.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"

#include "file.h"
#include "handle.h"
#include "thread.h"
#include "process.h"
#include "request.h"
#include "security.h"

/* list of memory ranges, used to store committed info */
struct ranges
{
    unsigned int count;
    unsigned int max;
    struct range
    {
        file_pos_t  start;
        file_pos_t  end;
    } ranges[1];
};

struct mapping
{
    struct object   obj;             /* object header */
    mem_size_t      size;            /* mapping size */
    unsigned int    flags;           /* SEC_* flags */
    int             protect;         /* protection flags */
    struct fd      *fd;              /* fd for mapped file */
    enum cpu_type   cpu;             /* client CPU (for PE image mapping) */
    pe_image_info_t image;           /* image info (for PE image mapping) */
    struct ranges  *committed;       /* list of committed ranges in this mapping */
    struct file    *shared_file;     /* temp file for shared PE mapping */
    struct list     shared_entry;    /* entry in global shared PE mappings list */
};

static void mapping_dump( struct object *obj, int verbose );
static struct object_type *mapping_get_type( struct object *obj );
static struct fd *mapping_get_fd( struct object *obj );
static unsigned int mapping_map_access( struct object *obj, unsigned int access );
static void mapping_destroy( struct object *obj );
static enum server_fd_type mapping_get_fd_type( struct fd *fd );

static const struct object_ops mapping_ops =
{
    sizeof(struct mapping),      /* size */
    mapping_dump,                /* dump */
    mapping_get_type,            /* get_type */
    no_add_queue,                /* add_queue */
    NULL,                        /* remove_queue */
    NULL,                        /* signaled */
    NULL,                        /* satisfied */
    no_signal,                   /* signal */
    mapping_get_fd,              /* get_fd */
    mapping_map_access,          /* map_access */
    default_get_sd,              /* get_sd */
    default_set_sd,              /* set_sd */
    no_lookup_name,              /* lookup_name */
    directory_link_name,         /* link_name */
    default_unlink_name,         /* unlink_name */
    no_open_file,                /* open_file */
    no_alloc_handle,             /* alloc_handle */
    fd_close_handle,             /* close_handle */
    mapping_destroy              /* destroy */
};

static const struct fd_ops mapping_fd_ops =
{
    default_fd_get_poll_events,   /* get_poll_events */
    default_poll_event,           /* poll_event */
    mapping_get_fd_type,          /* get_fd_type */
    no_fd_read,                   /* read */
    no_fd_write,                  /* write */
    no_fd_flush,                  /* flush */
    no_fd_ioctl,                  /* ioctl */
    no_fd_queue_async,            /* queue_async */
    default_fd_reselect_async     /* reselect_async */
};

static struct list shared_list = LIST_INIT(shared_list);

static size_t page_mask;

#define ROUND_SIZE(size)  (((size) + page_mask) & ~page_mask)


/* extend a file beyond the current end of file */
static int grow_file( int unix_fd, file_pos_t new_size )
{
    static const char zero;
    off_t size = new_size;

    if (sizeof(new_size) > sizeof(size) && size != new_size)
    {
        set_error( STATUS_INVALID_PARAMETER );
        return 0;
    }
    /* extend the file one byte beyond the requested size and then truncate it */
    /* this should work around ftruncate implementations that can't extend files */
    if (pwrite( unix_fd, &zero, 1, size ) != -1)
    {
        ftruncate( unix_fd, size );
        return 1;
    }
    file_set_error();
    return 0;
}

/* check if the current directory allows exec mappings */
static int check_current_dir_for_exec(void)
{
    int fd;
    char tmpfn[] = "anonmap.XXXXXX";
    void *ret = MAP_FAILED;

    fd = mkstemps( tmpfn, 0 );
    if (fd == -1) return 0;
    if (grow_file( fd, 1 ))
    {
        ret = mmap( NULL, get_page_size(), PROT_READ | PROT_EXEC, MAP_PRIVATE, fd, 0 );
        if (ret != MAP_FAILED) munmap( ret, get_page_size() );
    }
    close( fd );
    unlink( tmpfn );
    return (ret != MAP_FAILED);
}

/* create a temp file for anonymous mappings */
static int create_temp_file( file_pos_t size )
{
    static int temp_dir_fd = -1;
    char tmpfn[] = "anonmap.XXXXXX";
    int fd;

    if (temp_dir_fd == -1)
    {
        temp_dir_fd = server_dir_fd;
        if (!check_current_dir_for_exec())
        {
            /* the server dir is noexec, try the config dir instead */
            fchdir( config_dir_fd );
            if (check_current_dir_for_exec())
                temp_dir_fd = config_dir_fd;
            else  /* neither works, fall back to server dir */
                fchdir( server_dir_fd );
        }
    }
    else if (temp_dir_fd != server_dir_fd) fchdir( temp_dir_fd );

    fd = mkstemps( tmpfn, 0 );
    if (fd != -1)
    {
        if (!grow_file( fd, size ))
        {
            close( fd );
            fd = -1;
        }
        unlink( tmpfn );
    }
    else file_set_error();

    if (temp_dir_fd != server_dir_fd) fchdir( server_dir_fd );
    return fd;
}

/* find the shared PE mapping for a given mapping */
static struct file *get_shared_file( struct mapping *mapping )
{
    struct mapping *ptr;

    LIST_FOR_EACH_ENTRY( ptr, &shared_list, struct mapping, shared_entry )
        if (is_same_file_fd( ptr->fd, mapping->fd ))
            return (struct file *)grab_object( ptr->shared_file );
    return NULL;
}

/* return the size of the memory mapping and file range of a given section */
static inline void get_section_sizes( const IMAGE_SECTION_HEADER *sec, size_t *map_size,
                                      off_t *file_start, size_t *file_size )
{
    static const unsigned int sector_align = 0x1ff;

    if (!sec->Misc.VirtualSize) *map_size = ROUND_SIZE( sec->SizeOfRawData );
    else *map_size = ROUND_SIZE( sec->Misc.VirtualSize );

    *file_start = sec->PointerToRawData & ~sector_align;
    *file_size = (sec->SizeOfRawData + (sec->PointerToRawData & sector_align) + sector_align) & ~sector_align;
    if (*file_size > *map_size) *file_size = *map_size;
}

/* add a range to the committed list */
static void add_committed_range( struct mapping *mapping, file_pos_t start, file_pos_t end )
{
    unsigned int i, j;
    struct range *ranges;

    if (!mapping->committed) return;  /* everything committed already */

    for (i = 0, ranges = mapping->committed->ranges; i < mapping->committed->count; i++)
    {
        if (ranges[i].start > end) break;
        if (ranges[i].end < start) continue;
        if (ranges[i].start > start) ranges[i].start = start;   /* extend downwards */
        if (ranges[i].end < end)  /* extend upwards and maybe merge with next */
        {
            for (j = i + 1; j < mapping->committed->count; j++)
            {
                if (ranges[j].start > end) break;
                if (ranges[j].end > end) end = ranges[j].end;
            }
            if (j > i + 1)
            {
                memmove( &ranges[i + 1], &ranges[j], (mapping->committed->count - j) * sizeof(*ranges) );
                mapping->committed->count -= j - (i + 1);
            }
            ranges[i].end = end;
        }
        return;
    }

    /* now add a new range */

    if (mapping->committed->count == mapping->committed->max)
    {
        unsigned int new_size = mapping->committed->max * 2;
        struct ranges *new_ptr = realloc( mapping->committed, offsetof( struct ranges, ranges[new_size] ));
        if (!new_ptr) return;
        new_ptr->max = new_size;
        ranges = new_ptr->ranges;
        mapping->committed = new_ptr;
    }
    memmove( &ranges[i + 1], &ranges[i], (mapping->committed->count - i) * sizeof(*ranges) );
    ranges[i].start = start;
    ranges[i].end = end;
    mapping->committed->count++;
}

/* find the range containing start and return whether it's committed */
static int find_committed_range( struct mapping *mapping, file_pos_t start, mem_size_t *size )
{
    unsigned int i;
    struct range *ranges;

    if (!mapping->committed)  /* everything is committed */
    {
        *size = mapping->size - start;
        return 1;
    }
    for (i = 0, ranges = mapping->committed->ranges; i < mapping->committed->count; i++)
    {
        if (ranges[i].start > start)
        {
            *size = ranges[i].start - start;
            return 0;
        }
        if (ranges[i].end > start)
        {
            *size = ranges[i].end - start;
            return 1;
        }
    }
    *size = mapping->size - start;
    return 0;
}

/* allocate and fill the temp file for a shared PE image mapping */
static int build_shared_mapping( struct mapping *mapping, int fd,
                                 IMAGE_SECTION_HEADER *sec, unsigned int nb_sec )
{
    unsigned int i;
    mem_size_t total_size;
    size_t file_size, map_size, max_size;
    off_t shared_pos, read_pos, write_pos;
    char *buffer = NULL;
    int shared_fd;
    long toread;

    /* compute the total size of the shared mapping */

    total_size = max_size = 0;
    for (i = 0; i < nb_sec; i++)
    {
        if ((sec[i].Characteristics & IMAGE_SCN_MEM_SHARED) &&
            (sec[i].Characteristics & IMAGE_SCN_MEM_WRITE))
        {
            get_section_sizes( &sec[i], &map_size, &read_pos, &file_size );
            if (file_size > max_size) max_size = file_size;
            total_size += map_size;
        }
    }
    if (!total_size) return 1;  /* nothing to do */

    if ((mapping->shared_file = get_shared_file( mapping ))) return 1;

    /* create a temp file for the mapping */

    if ((shared_fd = create_temp_file( total_size )) == -1) return 0;
    if (!(mapping->shared_file = create_file_for_fd( shared_fd, FILE_GENERIC_READ|FILE_GENERIC_WRITE, 0 )))
        return 0;

    if (!(buffer = malloc( max_size ))) goto error;

    /* copy the shared sections data into the temp file */

    shared_pos = 0;
    for (i = 0; i < nb_sec; i++)
    {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_SHARED)) continue;
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
        get_section_sizes( &sec[i], &map_size, &read_pos, &file_size );
        write_pos = shared_pos;
        shared_pos += map_size;
        if (!sec[i].PointerToRawData || !file_size) continue;
        toread = file_size;
        while (toread)
        {
            long res = pread( fd, buffer + file_size - toread, toread, read_pos );
            if (!res && toread < 0x200)  /* partial sector at EOF is not an error */
            {
                file_size -= toread;
                break;
            }
            if (res <= 0) goto error;
            toread -= res;
            read_pos += res;
        }
        if (pwrite( shared_fd, buffer, file_size, write_pos ) != file_size) goto error;
    }
    free( buffer );
    return 1;

 error:
    release_object( mapping->shared_file );
    mapping->shared_file = NULL;
    free( buffer );
    return 0;
}

/* retrieve the mapping parameters for an executable (PE) image */
static unsigned int get_image_params( struct mapping *mapping, file_pos_t file_size, int unix_fd )
{
    IMAGE_DOS_HEADER dos;
    IMAGE_SECTION_HEADER *sec = NULL;
    struct
    {
        DWORD Signature;
        IMAGE_FILE_HEADER FileHeader;
        union
        {
            IMAGE_OPTIONAL_HEADER32 hdr32;
            IMAGE_OPTIONAL_HEADER64 hdr64;
        } opt;
    } nt;
    off_t pos;
    int size;

    /* load the headers */

    if (!file_size) return STATUS_INVALID_FILE_FOR_SECTION;
    if (pread( unix_fd, &dos, sizeof(dos), 0 ) != sizeof(dos)) return STATUS_INVALID_IMAGE_NOT_MZ;
    if (dos.e_magic != IMAGE_DOS_SIGNATURE) return STATUS_INVALID_IMAGE_NOT_MZ;
    pos = dos.e_lfanew;

    /* zero out header in the case it's not present or partial */
    memset( &nt, 0, sizeof(nt) );
    size = pread( unix_fd, &nt, sizeof(nt), pos );
    if (size < sizeof(nt.Signature) + sizeof(nt.FileHeader)) return STATUS_INVALID_IMAGE_FORMAT;
    if (nt.Signature != IMAGE_NT_SIGNATURE)
    {
        if (*(WORD *)&nt.Signature == IMAGE_OS2_SIGNATURE) return STATUS_INVALID_IMAGE_NE_FORMAT;
        return STATUS_INVALID_IMAGE_PROTECT;
    }

    mapping->cpu = current->process->cpu;
    switch (mapping->cpu)
    {
    case CPU_x86:
        if (nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386) return STATUS_INVALID_IMAGE_FORMAT;
        if (nt.opt.hdr32.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return STATUS_INVALID_IMAGE_FORMAT;
        break;
    case CPU_x86_64:
        if (nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return STATUS_INVALID_IMAGE_FORMAT;
        if (nt.opt.hdr64.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return STATUS_INVALID_IMAGE_FORMAT;
        break;
    case CPU_POWERPC:
        if (nt.FileHeader.Machine != IMAGE_FILE_MACHINE_POWERPC) return STATUS_INVALID_IMAGE_FORMAT;
        if (nt.opt.hdr32.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return STATUS_INVALID_IMAGE_FORMAT;
        break;
    case CPU_ARM:
        if (nt.FileHeader.Machine != IMAGE_FILE_MACHINE_ARM &&
            nt.FileHeader.Machine != IMAGE_FILE_MACHINE_THUMB &&
            nt.FileHeader.Machine != IMAGE_FILE_MACHINE_ARMNT) return STATUS_INVALID_IMAGE_FORMAT;
        if (nt.opt.hdr32.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return STATUS_INVALID_IMAGE_FORMAT;
        break;
    case CPU_ARM64:
        if (nt.FileHeader.Machine != IMAGE_FILE_MACHINE_ARM64) return STATUS_INVALID_IMAGE_FORMAT;
        if (nt.opt.hdr64.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return STATUS_INVALID_IMAGE_FORMAT;
        break;
    default:
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    switch (nt.opt.hdr32.Magic)
    {
    case IMAGE_NT_OPTIONAL_HDR32_MAGIC:
        /* All fields up to CheckSum are mandatory regardless of SizeOfOptionalHeader value */
        size = max( nt.FileHeader.SizeOfOptionalHeader, offsetof(IMAGE_OPTIONAL_HEADER32, CheckSum) );
        if (size < sizeof(nt.opt.hdr32)) memset( (char *)&nt.opt.hdr32 + size, 0, sizeof(nt.opt.hdr32) - size );

        mapping->image.base           = nt.opt.hdr32.ImageBase;
        mapping->image.entry_point    = nt.opt.hdr32.ImageBase + nt.opt.hdr32.AddressOfEntryPoint;
        mapping->image.map_size       = ROUND_SIZE( nt.opt.hdr32.SizeOfImage );
        mapping->image.stack_size     = nt.opt.hdr32.SizeOfStackReserve;
        mapping->image.stack_commit   = nt.opt.hdr32.SizeOfStackCommit;
        mapping->image.subsystem      = nt.opt.hdr32.Subsystem;
        mapping->image.subsystem_low  = nt.opt.hdr32.MinorSubsystemVersion;
        mapping->image.subsystem_high = nt.opt.hdr32.MajorSubsystemVersion;
        mapping->image.dll_charact    = nt.opt.hdr32.DllCharacteristics;
        mapping->image.loader_flags   = nt.opt.hdr32.LoaderFlags;
        mapping->image.header_size    = nt.opt.hdr32.SizeOfHeaders;
        mapping->image.checksum       = nt.opt.hdr32.CheckSum;
        break;
    case IMAGE_NT_OPTIONAL_HDR64_MAGIC:
        /* All fields up to CheckSum are mandatory regardless of SizeOfOptionalHeader value */
        size = max( nt.FileHeader.SizeOfOptionalHeader, offsetof(IMAGE_OPTIONAL_HEADER64, CheckSum) );
        if (size < sizeof(nt.opt.hdr64)) memset( (char *)&nt.opt.hdr64 + size, 0, sizeof(nt.opt.hdr64) - size );

        mapping->image.base           = nt.opt.hdr64.ImageBase;
        mapping->image.entry_point    = nt.opt.hdr64.ImageBase + nt.opt.hdr64.AddressOfEntryPoint;
        mapping->image.map_size       = ROUND_SIZE( nt.opt.hdr64.SizeOfImage );
        mapping->image.stack_size     = nt.opt.hdr64.SizeOfStackReserve;
        mapping->image.stack_commit   = nt.opt.hdr64.SizeOfStackCommit;
        mapping->image.subsystem      = nt.opt.hdr64.Subsystem;
        mapping->image.subsystem_low  = nt.opt.hdr64.MinorSubsystemVersion;
        mapping->image.subsystem_high = nt.opt.hdr64.MajorSubsystemVersion;
        mapping->image.dll_charact    = nt.opt.hdr64.DllCharacteristics;
        mapping->image.loader_flags   = nt.opt.hdr64.LoaderFlags;
        mapping->image.header_size    = nt.opt.hdr64.SizeOfHeaders;
        mapping->image.checksum       = nt.opt.hdr64.CheckSum;
        break;
    }
    mapping->image.image_charact = nt.FileHeader.Characteristics;
    mapping->image.machine       = nt.FileHeader.Machine;
    mapping->image.zerobits      = 0; /* FIXME */
    mapping->image.gp            = 0; /* FIXME */
    mapping->image.contains_code = 0; /* FIXME */
    mapping->image.image_flags   = 0; /* FIXME */
    mapping->image.file_size     = file_size;

    /* load the section headers */

    pos += sizeof(nt.Signature) + sizeof(nt.FileHeader) + nt.FileHeader.SizeOfOptionalHeader;
    size = sizeof(*sec) * nt.FileHeader.NumberOfSections;
    if (!mapping->size) mapping->size = mapping->image.map_size;
    else if (mapping->size > mapping->image.map_size) return STATUS_SECTION_TOO_BIG;
    if (pos + size > mapping->image.map_size) return STATUS_INVALID_FILE_FOR_SECTION;
    if (pos + size > mapping->image.header_size) mapping->image.header_size = pos + size;
    if (!(sec = malloc( size ))) goto error;
    if (pread( unix_fd, sec, size, pos ) != size) goto error;

    if (!build_shared_mapping( mapping, unix_fd, sec, nt.FileHeader.NumberOfSections )) goto error;

    if (mapping->shared_file) list_add_head( &shared_list, &mapping->shared_entry );

    free( sec );
    return 0;

 error:
    free( sec );
    return STATUS_INVALID_FILE_FOR_SECTION;
}

static struct object *create_mapping( struct object *root, const struct unicode_str *name,
                                      unsigned int attr, mem_size_t size, unsigned int flags, int protect,
                                      obj_handle_t handle, const struct security_descriptor *sd )
{
    struct mapping *mapping;
    struct file *file;
    struct fd *fd;
    int access = 0;
    int unix_fd;
    struct stat st;

    if (!page_mask) page_mask = sysconf( _SC_PAGESIZE ) - 1;

    if (!(mapping = create_named_object( root, &mapping_ops, name, attr, sd )))
        return NULL;
    if (get_error() == STATUS_OBJECT_NAME_EXISTS)
        return &mapping->obj;  /* Nothing else to do */

    mapping->size        = size;
    mapping->flags       = flags & (SEC_IMAGE | SEC_NOCACHE | SEC_WRITECOMBINE | SEC_LARGE_PAGES);
    mapping->protect     = protect;
    mapping->fd          = NULL;
    mapping->shared_file = NULL;
    mapping->committed   = NULL;

    if (protect & VPROT_READ) access |= FILE_READ_DATA;
    if (protect & VPROT_WRITE) access |= FILE_WRITE_DATA;

    if (handle)
    {
        const unsigned int sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
        unsigned int mapping_access = FILE_MAPPING_ACCESS;

        if (flags & SEC_RESERVE)
        {
            set_error( STATUS_INVALID_PARAMETER );
            goto error;
        }
        if (!(file = get_file_obj( current->process, handle, access ))) goto error;
        fd = get_obj_fd( (struct object *)file );

        /* file sharing rules for mappings are different so we use magic the access rights */
        if (flags & SEC_IMAGE) mapping_access |= FILE_MAPPING_IMAGE;
        else if (protect & VPROT_WRITE) mapping_access |= FILE_MAPPING_WRITE;

        mapping->flags |= SEC_FILE;
        if (!(mapping->fd = get_fd_object_for_mapping( fd, mapping_access, sharing )))
        {
            mapping->fd = dup_fd_object( fd, mapping_access, sharing, FILE_SYNCHRONOUS_IO_NONALERT );
            if (mapping->fd) set_fd_user( mapping->fd, &mapping_fd_ops, NULL );
        }
        release_object( file );
        release_object( fd );
        if (!mapping->fd) goto error;

        if ((unix_fd = get_unix_fd( mapping->fd )) == -1) goto error;
        if (fstat( unix_fd, &st ) == -1)
        {
            file_set_error();
            goto error;
        }
        if (flags & SEC_IMAGE)
        {
            unsigned int err = get_image_params( mapping, st.st_size, unix_fd );
            if (!err) return &mapping->obj;
            set_error( err );
            goto error;
        }
        if (!mapping->size)
        {
            if (!(mapping->size = st.st_size))
            {
                set_error( STATUS_MAPPED_FILE_SIZE_ZERO );
                goto error;
            }
        }
        else if (st.st_size < mapping->size)
        {
            if (!(access & FILE_WRITE_DATA))
            {
                set_error( STATUS_SECTION_TOO_BIG );
                goto error;
            }
            if (!grow_file( unix_fd, mapping->size )) goto error;
        }
    }
    else  /* Anonymous mapping (no associated file) */
    {
        if (!mapping->size || (flags & SEC_IMAGE))
        {
            set_error( STATUS_INVALID_PARAMETER );
            goto error;
        }
        mapping->flags |= flags & (SEC_COMMIT | SEC_RESERVE);
        if (flags & SEC_RESERVE)
        {
            if (!(mapping->committed = mem_alloc( offsetof(struct ranges, ranges[8]) ))) goto error;
            mapping->committed->count = 0;
            mapping->committed->max   = 8;
        }
        mapping->size = (mapping->size + page_mask) & ~((mem_size_t)page_mask);
        if ((unix_fd = create_temp_file( mapping->size )) == -1) goto error;
        if (!(mapping->fd = create_anonymous_fd( &mapping_fd_ops, unix_fd, &mapping->obj,
                                                 FILE_SYNCHRONOUS_IO_NONALERT ))) goto error;
        allow_fd_caching( mapping->fd );
    }
    return &mapping->obj;

 error:
    release_object( mapping );
    return NULL;
}

struct mapping *get_mapping_obj( struct process *process, obj_handle_t handle, unsigned int access )
{
    return (struct mapping *)get_handle_obj( process, handle, access, &mapping_ops );
}

/* open a new file handle to the file backing the mapping */
obj_handle_t open_mapping_file( struct process *process, struct mapping *mapping,
                                unsigned int access, unsigned int sharing )
{
    obj_handle_t handle;
    struct file *file = create_file_for_fd_obj( mapping->fd, access, sharing );

    if (!file) return 0;
    handle = alloc_handle( process, file, access, 0 );
    release_object( file );
    return handle;
}

struct mapping *grab_mapping_unless_removable( struct mapping *mapping )
{
    if (is_fd_removable( mapping->fd )) return NULL;
    return (struct mapping *)grab_object( mapping );
}

static void mapping_dump( struct object *obj, int verbose )
{
    struct mapping *mapping = (struct mapping *)obj;
    assert( obj->ops == &mapping_ops );
    fprintf( stderr, "Mapping size=%08x%08x flags=%08x prot=%08x fd=%p shared_file=%p\n",
             (unsigned int)(mapping->size >> 32), (unsigned int)mapping->size,
             mapping->flags, mapping->protect, mapping->fd, mapping->shared_file );
}

static struct object_type *mapping_get_type( struct object *obj )
{
    static const WCHAR name[] = {'S','e','c','t','i','o','n'};
    static const struct unicode_str str = { name, sizeof(name) };
    return get_object_type( &str );
}

static struct fd *mapping_get_fd( struct object *obj )
{
    struct mapping *mapping = (struct mapping *)obj;
    return (struct fd *)grab_object( mapping->fd );
}

static unsigned int mapping_map_access( struct object *obj, unsigned int access )
{
    if (access & GENERIC_READ)    access |= STANDARD_RIGHTS_READ | SECTION_QUERY | SECTION_MAP_READ;
    if (access & GENERIC_WRITE)   access |= STANDARD_RIGHTS_WRITE | SECTION_MAP_WRITE;
    if (access & GENERIC_EXECUTE) access |= STANDARD_RIGHTS_EXECUTE | SECTION_MAP_EXECUTE;
    if (access & GENERIC_ALL)     access |= SECTION_ALL_ACCESS;
    return access & ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
}

static void mapping_destroy( struct object *obj )
{
    struct mapping *mapping = (struct mapping *)obj;
    assert( obj->ops == &mapping_ops );
    if (mapping->fd) release_object( mapping->fd );
    if (mapping->shared_file)
    {
        release_object( mapping->shared_file );
        list_remove( &mapping->shared_entry );
    }
    free( mapping->committed );
}

static enum server_fd_type mapping_get_fd_type( struct fd *fd )
{
    return FD_TYPE_FILE;
}

int get_page_size(void)
{
    if (!page_mask) page_mask = sysconf( _SC_PAGESIZE ) - 1;
    return page_mask + 1;
}

/* create a file mapping */
DECL_HANDLER(create_mapping)
{
    struct object *root, *obj;
    struct unicode_str name;
    const struct security_descriptor *sd;
    const struct object_attributes *objattr = get_req_object_attributes( &sd, &name, &root );

    if (!objattr) return;

    if ((obj = create_mapping( root, &name, objattr->attributes,
                               req->size, req->flags, req->protect, req->file_handle, sd )))
    {
        if (get_error() == STATUS_OBJECT_NAME_EXISTS)
            reply->handle = alloc_handle( current->process, obj, req->access, objattr->attributes );
        else
            reply->handle = alloc_handle_no_access_check( current->process, obj,
                                                          req->access, objattr->attributes );
        release_object( obj );
    }

    if (root) release_object( root );
}

/* open a handle to a mapping */
DECL_HANDLER(open_mapping)
{
    struct unicode_str name = get_req_unicode_str();

    reply->handle = open_object( current->process, req->rootdir, req->access,
                                 &mapping_ops, &name, req->attributes );
}

/* get a mapping information */
DECL_HANDLER(get_mapping_info)
{
    struct mapping *mapping;
    struct fd *fd;

    if (!(mapping = get_mapping_obj( current->process, req->handle, req->access ))) return;

    reply->size    = mapping->size;
    reply->flags   = mapping->flags;
    reply->protect = mapping->protect;

    if (mapping->flags & SEC_IMAGE)
        set_reply_data( &mapping->image, min( sizeof(mapping->image), get_reply_max_size() ));

    if (!(req->access & (SECTION_MAP_READ | SECTION_MAP_WRITE)))  /* query only */
    {
        release_object( mapping );
        return;
    }

    if ((mapping->flags & SEC_IMAGE) && mapping->cpu != current->process->cpu)
    {
        set_error( STATUS_INVALID_IMAGE_FORMAT );
        release_object( mapping );
        return;
    }

    if ((fd = get_obj_fd( &mapping->obj )))
    {
        if (!is_fd_removable(fd)) reply->mapping = alloc_handle( current->process, mapping, 0, 0 );
        release_object( fd );
    }
    if (mapping->shared_file)
    {
        if (!(reply->shared_file = alloc_handle( current->process, mapping->shared_file,
                                                 GENERIC_READ|GENERIC_WRITE, 0 )))
        {
            if (reply->mapping) close_handle( current->process, reply->mapping );
        }
    }
    release_object( mapping );
}

/* get a range of committed pages in a file mapping */
DECL_HANDLER(get_mapping_committed_range)
{
    struct mapping *mapping;

    if ((mapping = get_mapping_obj( current->process, req->handle, 0 )))
    {
        if (!(req->offset & page_mask) && req->offset < mapping->size)
            reply->committed = find_committed_range( mapping, req->offset, &reply->size );
        else
            set_error( STATUS_INVALID_PARAMETER );

        release_object( mapping );
    }
}

/* add a range to the committed pages in a file mapping */
DECL_HANDLER(add_mapping_committed_range)
{
    struct mapping *mapping;

    if ((mapping = get_mapping_obj( current->process, req->handle, 0 )))
    {
        if (!(req->size & page_mask) &&
            !(req->offset & page_mask) &&
            req->offset < mapping->size &&
            req->size > 0 &&
            req->size <= mapping->size - req->offset)
            add_committed_range( mapping, req->offset, req->offset + req->size );
        else
            set_error( STATUS_INVALID_PARAMETER );

        release_object( mapping );
    }
}
