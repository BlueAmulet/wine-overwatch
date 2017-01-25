/*
 * Server-side file management
 *
 * Copyright (C) 1998 Alexandre Julliard
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
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#ifdef HAVE_UTIME_H
#include <utime.h>
#endif
#ifdef HAVE_POLL_H
#include <poll.h>
#endif

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"

#include "file.h"
#include "handle.h"
#include "thread.h"
#include "request.h"
#include "process.h"
#include "security.h"

struct file
{
    struct object       obj;        /* object header */
    struct fd          *fd;         /* file descriptor for this file */
    unsigned int        access;     /* file access (FILE_READ_DATA etc.) */
    mode_t              mode;       /* file stat.st_mode */
    uid_t               uid;        /* file stat.st_uid */
};

static unsigned int generic_file_map_access( unsigned int access );

static void file_dump( struct object *obj, int verbose );
static struct object_type *file_get_type( struct object *obj );
static struct fd *file_get_fd( struct object *obj );
static struct security_descriptor *file_get_sd( struct object *obj );
static int file_set_sd( struct object *obj, const struct security_descriptor *sd, unsigned int set_info );
static struct object *file_lookup_name( struct object *obj, struct unicode_str *name, unsigned int attr );
static struct object *file_open_file( struct object *obj, unsigned int access,
                                      unsigned int sharing, unsigned int options );
static void file_destroy( struct object *obj );

static int file_get_poll_events( struct fd *fd );
static obj_handle_t file_flush( struct fd *fd, const async_data_t *async, int blocking );
static enum server_fd_type file_get_fd_type( struct fd *fd );

static const struct object_ops file_ops =
{
    sizeof(struct file),          /* size */
    file_dump,                    /* dump */
    file_get_type,                /* get_type */
    add_queue,                    /* add_queue */
    remove_queue,                 /* remove_queue */
    default_fd_signaled,          /* signaled */
    no_satisfied,                 /* satisfied */
    no_signal,                    /* signal */
    file_get_fd,                  /* get_fd */
    default_fd_map_access,        /* map_access */
    file_get_sd,                  /* get_sd */
    file_set_sd,                  /* set_sd */
    file_lookup_name,             /* lookup_name */
    no_link_name,                 /* link_name */
    NULL,                         /* unlink_name */
    file_open_file,               /* open_file */
    no_alloc_handle,              /* alloc_handle */
    fd_close_handle,              /* close_handle */
    file_destroy                  /* destroy */
};

static const struct fd_ops file_fd_ops =
{
    file_get_poll_events,         /* get_poll_events */
    default_poll_event,           /* poll_event */
    file_get_fd_type,             /* get_fd_type */
    no_fd_read,                   /* read */
    no_fd_write,                  /* write */
    file_flush,                   /* flush */
    default_fd_ioctl,             /* ioctl */
    default_fd_queue_async,       /* queue_async */
    default_fd_reselect_async     /* reselect_async */
};

static inline int is_overlapped( const struct file *file )
{
    return !(get_fd_options( file->fd ) & (FILE_SYNCHRONOUS_IO_ALERT | FILE_SYNCHRONOUS_IO_NONALERT));
}

/* create a file from a file descriptor */
/* if the function fails the fd is closed */
struct file *create_file_for_fd( int fd, unsigned int access, unsigned int sharing )
{
    struct file *file;
    struct stat st;

    if (fstat( fd, &st ) == -1)
    {
        file_set_error();
        close( fd );
        return NULL;
    }

    if (!(file = alloc_object( &file_ops )))
    {
        close( fd );
        return NULL;
    }

    file->mode = st.st_mode;
    file->access = default_fd_map_access( &file->obj, access );
    if (!(file->fd = create_anonymous_fd( &file_fd_ops, fd, &file->obj,
                                          FILE_SYNCHRONOUS_IO_NONALERT )))
    {
        release_object( file );
        return NULL;
    }
    allow_fd_caching( file->fd );
    return file;
}

/* create a file by duplicating an fd object */
struct file *create_file_for_fd_obj( struct fd *fd, unsigned int access, unsigned int sharing )
{
    struct file *file;
    struct stat st;

    if (fstat( get_unix_fd(fd), &st ) == -1)
    {
        file_set_error();
        return NULL;
    }

    if ((file = alloc_object( &file_ops )))
    {
        file->mode = st.st_mode;
        file->access = default_fd_map_access( &file->obj, access );
        if (!(file->fd = dup_fd_object( fd, access, sharing, FILE_SYNCHRONOUS_IO_NONALERT )))
        {
            release_object( file );
            return NULL;
        }
        set_fd_user( file->fd, &file_fd_ops, &file->obj );
    }
    return file;
}

static struct object *create_file_obj( struct fd *fd, unsigned int access, mode_t mode )
{
    struct file *file = alloc_object( &file_ops );

    if (!file) return NULL;
    file->access  = access;
    file->mode    = mode;
    file->uid     = ~(uid_t)0;
    file->fd      = fd;
    grab_object( fd );
    set_fd_user( fd, &file_fd_ops, &file->obj );
    return &file->obj;
}

static struct object *create_file( struct fd *root, const char *nameptr, data_size_t len,
                                   unsigned int access, unsigned int sharing, int create,
                                   unsigned int options, unsigned int attrs,
                                   const struct security_descriptor *sd )
{
    struct object *obj = NULL;
    struct fd *fd;
    int flags;
    char *name;
    mode_t mode;

    if (!len || ((nameptr[0] == '/') ^ !root))
    {
        set_error( STATUS_OBJECT_PATH_SYNTAX_BAD );
        return NULL;
    }
    if (!(name = mem_alloc( len + 1 ))) return NULL;
    memcpy( name, nameptr, len );
    name[len] = 0;

    switch(create)
    {
    case FILE_CREATE:       flags = O_CREAT | O_EXCL; break;
    case FILE_OVERWRITE_IF: /* FIXME: the difference is whether we trash existing attr or not */
                            access |= FILE_WRITE_ATTRIBUTES;
    case FILE_SUPERSEDE:    flags = O_CREAT | O_TRUNC; break;
    case FILE_OPEN:         flags = 0; break;
    case FILE_OPEN_IF:      flags = O_CREAT; break;
    case FILE_OVERWRITE:    flags = O_TRUNC;
                            access |= FILE_WRITE_ATTRIBUTES; break;
    default:                set_error( STATUS_INVALID_PARAMETER ); goto done;
    }

    if (sd)
    {
        const SID *owner = sd_get_owner( sd );
        if (!owner)
            owner = token_get_user( current->process->token );
        mode = sd_to_mode( sd, owner );
    }
    else if (options & FILE_DIRECTORY_FILE)
        mode = (attrs & FILE_ATTRIBUTE_READONLY) ? 0555 : 0777;
    else
        mode = (attrs & FILE_ATTRIBUTE_READONLY) ? 0444 : 0666;

    if (len >= 4 &&
        (!strcasecmp( name + len - 4, ".exe" ) || !strcasecmp( name + len - 4, ".com" )))
    {
        if (mode & S_IRUSR)
            mode |= S_IXUSR;
        if (mode & S_IRGRP)
            mode |= S_IXGRP;
        if (mode & S_IROTH)
            mode |= S_IXOTH;
    }

    access = generic_file_map_access( access );

    /* FIXME: should set error to STATUS_OBJECT_NAME_COLLISION if file existed before */
    fd = open_fd( root, name, flags | O_NONBLOCK | O_LARGEFILE, &mode, access, sharing, options );
    if (!fd) goto done;

    if (S_ISDIR(mode))
        obj = create_dir_obj( fd, access, mode );
    else if (S_ISCHR(mode) && is_serial_fd( fd ))
        obj = create_serial( fd );
    else
        obj = create_file_obj( fd, access, mode );

    release_object( fd );

done:
    free( name );
    return obj;
}

/* check if two file objects point to the same file */
int is_same_file( struct file *file1, struct file *file2 )
{
    return is_same_file_fd( file1->fd, file2->fd );
}

static void file_dump( struct object *obj, int verbose )
{
    struct file *file = (struct file *)obj;
    assert( obj->ops == &file_ops );
    fprintf( stderr, "File fd=%p\n", file->fd );
}

static struct object_type *file_get_type( struct object *obj )
{
    static const WCHAR name[] = {'F','i','l','e'};
    static const struct unicode_str str = { name, sizeof(name) };
    return get_object_type( &str );
}

static int file_get_poll_events( struct fd *fd )
{
    struct file *file = get_fd_user( fd );
    int events = 0;
    assert( file->obj.ops == &file_ops );
    if (file->access & FILE_UNIX_READ_ACCESS) events |= POLLIN;
    if (file->access & FILE_UNIX_WRITE_ACCESS) events |= POLLOUT;
    return events;
}

static obj_handle_t file_flush( struct fd *fd, const async_data_t *async, int blocking )
{
    int unix_fd = get_unix_fd( fd );
    if (unix_fd != -1 && fsync( unix_fd ) == -1) file_set_error();
    return 0;
}

static enum server_fd_type file_get_fd_type( struct fd *fd )
{
    struct file *file = get_fd_user( fd );

    if (S_ISREG(file->mode) || S_ISBLK(file->mode)) return FD_TYPE_FILE;
    if (S_ISDIR(file->mode)) return FD_TYPE_DIR;
    return FD_TYPE_CHAR;
}

static struct fd *file_get_fd( struct object *obj )
{
    struct file *file = (struct file *)obj;
    assert( obj->ops == &file_ops );
    return (struct fd *)grab_object( file->fd );
}

static unsigned int generic_file_map_access( unsigned int access )
{
    if (access & GENERIC_READ)    access |= FILE_GENERIC_READ;
    if (access & GENERIC_WRITE)   access |= FILE_GENERIC_WRITE;
    if (access & GENERIC_EXECUTE) access |= FILE_GENERIC_EXECUTE;
    if (access & GENERIC_ALL)     access |= FILE_ALL_ACCESS;
    return access & ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
}

struct security_descriptor *mode_to_sd( mode_t mode, const SID *user, const SID *group )
{
    struct security_descriptor *sd;
    size_t dacl_size;
    ACE_HEADER *current_ace;
    ACCESS_ALLOWED_ACE *aaa;
    ACL *dacl;
    SID *sid;
    char *ptr;
    const SID *world_sid = security_world_sid;
    const SID *local_system_sid = security_local_system_sid;

    dacl_size = sizeof(ACL) + FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) +
        security_sid_len( local_system_sid );
    if (mode & S_IRWXU)
        dacl_size += FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + security_sid_len( user );
    if ((!(mode & S_IRUSR) && (mode & (S_IRGRP|S_IROTH))) ||
        (!(mode & S_IWUSR) && (mode & (S_IWGRP|S_IWOTH))) ||
        (!(mode & S_IXUSR) && (mode & (S_IXGRP|S_IXOTH))))
        dacl_size += FIELD_OFFSET(ACCESS_DENIED_ACE, SidStart) + security_sid_len( user );
    if (mode & S_IRWXO)
        dacl_size += FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + security_sid_len( world_sid );

    sd = mem_alloc( sizeof(struct security_descriptor) +
                    security_sid_len( user ) + security_sid_len( group ) +
                    dacl_size );
    if (!sd) return sd;

    sd->control = SE_DACL_PRESENT;
    sd->owner_len = security_sid_len( user );
    sd->group_len = security_sid_len( group );
    sd->sacl_len = 0;
    sd->dacl_len = dacl_size;

    ptr = (char *)(sd + 1);
    memcpy( ptr, user, sd->owner_len );
    ptr += sd->owner_len;
    memcpy( ptr, group, sd->group_len );
    ptr += sd->group_len;

    dacl = (ACL *)ptr;
    dacl->AclRevision = ACL_REVISION;
    dacl->Sbz1 = 0;
    dacl->AclSize = dacl_size;
    dacl->AceCount = 1 + (mode & S_IRWXU ? 1 : 0) + (mode & S_IRWXO ? 1 : 0);
    if ((!(mode & S_IRUSR) && (mode & (S_IRGRP|S_IROTH))) ||
        (!(mode & S_IWUSR) && (mode & (S_IWGRP|S_IWOTH))) ||
        (!(mode & S_IXUSR) && (mode & (S_IXGRP|S_IXOTH))))
        dacl->AceCount++;
    dacl->Sbz2 = 0;

    /* always give FILE_ALL_ACCESS for Local System */
    aaa = (ACCESS_ALLOWED_ACE *)(dacl + 1);
    current_ace = &aaa->Header;
    aaa->Header.AceType = ACCESS_ALLOWED_ACE_TYPE;
    aaa->Header.AceFlags = (mode & S_IFDIR) ? OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE : 0;
    aaa->Header.AceSize = FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + security_sid_len( local_system_sid );
    aaa->Mask = FILE_ALL_ACCESS;
    sid = (SID *)&aaa->SidStart;
    memcpy( sid, local_system_sid, security_sid_len( local_system_sid ));

    if (mode & S_IRWXU)
    {
        /* appropriate access rights for the user */
        aaa = (ACCESS_ALLOWED_ACE *)ace_next( current_ace );
        current_ace = &aaa->Header;
        aaa->Header.AceType = ACCESS_ALLOWED_ACE_TYPE;
        aaa->Header.AceFlags = (mode & S_IFDIR) ? OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE : 0;
        aaa->Header.AceSize = FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + security_sid_len( user );
        aaa->Mask = WRITE_DAC | WRITE_OWNER;
        if (mode & S_IRUSR)
            aaa->Mask |= FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
        if (mode & S_IWUSR)
            aaa->Mask |= FILE_GENERIC_WRITE | DELETE | FILE_DELETE_CHILD;
        sid = (SID *)&aaa->SidStart;
        memcpy( sid, user, security_sid_len( user ));
    }
    if ((!(mode & S_IRUSR) && (mode & (S_IRGRP|S_IROTH))) ||
        (!(mode & S_IWUSR) && (mode & (S_IWGRP|S_IWOTH))) ||
        (!(mode & S_IXUSR) && (mode & (S_IXGRP|S_IXOTH))))
    {
        /* deny just in case the user is a member of the group */
        ACCESS_DENIED_ACE *ada = (ACCESS_DENIED_ACE *)ace_next( current_ace );
        current_ace = &ada->Header;
        ada->Header.AceType = ACCESS_DENIED_ACE_TYPE;
        ada->Header.AceFlags = (mode & S_IFDIR) ? OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE : 0;
        ada->Header.AceSize = FIELD_OFFSET(ACCESS_DENIED_ACE, SidStart) + security_sid_len( user );
        ada->Mask = 0;
        if (!(mode & S_IRUSR) && (mode & (S_IRGRP|S_IROTH)))
            ada->Mask |= FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
        if (!(mode & S_IWUSR) && (mode & (S_IWGRP|S_IROTH)))
            ada->Mask |= FILE_GENERIC_WRITE | DELETE | FILE_DELETE_CHILD;
        ada->Mask &= ~STANDARD_RIGHTS_ALL; /* never deny standard rights */
        sid = (SID *)&ada->SidStart;
        memcpy( sid, user, security_sid_len( user ));
    }
    if (mode & S_IRWXO)
    {
        /* appropriate access rights for Everyone */
        aaa = (ACCESS_ALLOWED_ACE *)ace_next( current_ace );
        current_ace = &aaa->Header;
        aaa->Header.AceType = ACCESS_ALLOWED_ACE_TYPE;
        aaa->Header.AceFlags = (mode & S_IFDIR) ? OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE : 0;
        aaa->Header.AceSize = FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + security_sid_len( world_sid );
        aaa->Mask = 0;
        if (mode & S_IROTH)
            aaa->Mask |= FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
        if (mode & S_IWOTH)
            aaa->Mask |= FILE_GENERIC_WRITE | DELETE | FILE_DELETE_CHILD;
        sid = (SID *)&aaa->SidStart;
        memcpy( sid, world_sid, security_sid_len( world_sid ));
    }

    return sd;
}

struct security_descriptor *get_file_sd( struct object *obj, struct fd *fd, mode_t *mode,
                                         uid_t *uid )
{
    int unix_fd = get_unix_fd( fd );
    struct stat st;
    struct security_descriptor *sd;

    if (unix_fd == -1 || fstat( unix_fd, &st ) == -1)
        return obj->sd;

    /* mode and uid the same? if so, no need to re-generate security descriptor */
    if (obj->sd && (st.st_mode & (S_IRWXU|S_IRWXO)) == (*mode & (S_IRWXU|S_IRWXO)) &&
        (st.st_uid == *uid))
        return obj->sd;

    sd = mode_to_sd( st.st_mode,
                     security_unix_uid_to_sid( st.st_uid ),
                     token_get_primary_group( current->process->token ));
    if (!sd) return obj->sd;

    *mode = st.st_mode;
    *uid = st.st_uid;
    free( obj->sd );
    obj->sd = sd;
    return sd;
}

static struct security_descriptor *file_get_sd( struct object *obj )
{
    struct file *file = (struct file *)obj;
    struct security_descriptor *sd;
    struct fd *fd;

    assert( obj->ops == &file_ops );

    fd = file_get_fd( obj );
    sd = get_file_sd( obj, fd, &file->mode, &file->uid );
    release_object( fd );
    return sd;
}

static mode_t file_access_to_mode( unsigned int access )
{
    mode_t mode = 0;

    access = generic_file_map_access( access );
    if (access & FILE_READ_DATA)  mode |= 4;
    if (access & (FILE_WRITE_DATA|FILE_APPEND_DATA)) mode |= 2;
    if (access & FILE_EXECUTE)    mode |= 1;
    return mode;
}

mode_t sd_to_mode( const struct security_descriptor *sd, const SID *owner )
{
    mode_t new_mode = 0;
    mode_t bits_to_set = ~0;
    mode_t mode;
    int present;
    const ACL *dacl = sd_get_dacl( sd, &present );
    if (present && dacl)
    {
        const ACE_HEADER *ace = (const ACE_HEADER *)(dacl + 1);
        ULONG i;
        for (i = 0; i < dacl->AceCount; i++, ace = ace_next( ace ))
        {
            const ACCESS_ALLOWED_ACE *aa_ace;
            const ACCESS_DENIED_ACE *ad_ace;
            const SID *sid;

            if (ace->AceFlags & INHERIT_ONLY_ACE) continue;

            switch (ace->AceType)
            {
                case ACCESS_DENIED_ACE_TYPE:
                    ad_ace = (const ACCESS_DENIED_ACE *)ace;
                    sid = (const SID *)&ad_ace->SidStart;
                    mode = file_access_to_mode( ad_ace->Mask );
                    if (security_equal_sid( sid, security_world_sid ))
                    {
                        bits_to_set &= ~(mode << 0); /* all */
                    }
                    if (token_sid_present( current->process->token, sid, TRUE ))
                    {
                        bits_to_set &= ~(mode << 3);  /* group */
                    }
                    if (security_equal_sid( sid, owner ))
                    {
                        bits_to_set &= ~(mode << 6);  /* user */
                    }
                    break;
                case ACCESS_ALLOWED_ACE_TYPE:
                    aa_ace = (const ACCESS_ALLOWED_ACE *)ace;
                    sid = (const SID *)&aa_ace->SidStart;
                    mode = file_access_to_mode( aa_ace->Mask );
                    if (security_equal_sid( sid, security_world_sid ))
                    {
                        new_mode |= (mode << 0) & bits_to_set; /* all */
                        bits_to_set &= ~(mode << 0);
                    }
                    if (token_sid_present( current->process->token, sid, FALSE ))
                    {
                        new_mode |= (mode << 3) & bits_to_set; /* group */
                        bits_to_set &= ~(mode << 3);
                    }
                    if (security_equal_sid( sid, owner ))
                    {
                        new_mode |= (mode << 6) & bits_to_set; /* user */
                        bits_to_set &= ~(mode << 6);
                    }
                    break;
            }
        }
        new_mode |= (new_mode & S_IRWXO) << 3;
        new_mode |= (new_mode & S_IRWXG) << 3;
    }
    else
        /* no ACL means full access rights to anyone */
        new_mode = S_IRWXU | S_IRWXG | S_IRWXO;

    return new_mode;
}

int set_file_sd( struct object *obj, struct fd *fd, mode_t *mode, uid_t *uid,
                 const struct security_descriptor *sd, unsigned int set_info )
{
    int unix_fd = get_unix_fd( fd );
    const SID *owner;
    struct stat st;
    mode_t new_mode;

    if (unix_fd == -1 || fstat( unix_fd, &st ) == -1) return 1;

    if (set_info & OWNER_SECURITY_INFORMATION)
    {
        owner = sd_get_owner( sd );
        if (!owner)
        {
            set_error( STATUS_INVALID_SECURITY_DESCR );
            return 0;
        }
        if (!obj->sd || !security_equal_sid( owner, sd_get_owner( obj->sd ) ))
        {
            /* FIXME: get Unix uid and call fchown */
        }
    }
    else if (obj->sd)
        owner = sd_get_owner( obj->sd );
    else
        owner = token_get_user( current->process->token );

    /* group and sacl not supported */

    if (set_info & DACL_SECURITY_INFORMATION)
    {
        /* keep the bits that we don't map to access rights in the ACL */
        new_mode = st.st_mode & (S_ISUID|S_ISGID|S_ISVTX);
        new_mode |= sd_to_mode( sd, owner );

        if (((st.st_mode ^ new_mode) & (S_IRWXU|S_IRWXG|S_IRWXO)) && fchmod( unix_fd, new_mode ) == -1)
        {
            file_set_error();
            return 0;
        }
    }
    return 1;
}

static struct object *file_lookup_name( struct object *obj, struct unicode_str *name, unsigned int attr )
{
    if (!name || !name->len) return NULL;  /* open the file itself */

    set_error( STATUS_OBJECT_PATH_NOT_FOUND );
    return NULL;
}

static struct object *file_open_file( struct object *obj, unsigned int access,
                                      unsigned int sharing, unsigned int options )
{
    struct file *file = (struct file *)obj;
    struct object *new_file = NULL;
    char *unix_name;

    assert( obj->ops == &file_ops );

    if ((unix_name = dup_fd_name( file->fd, "" )))
    {
        new_file = create_file( NULL, unix_name, strlen(unix_name), access,
                                sharing, FILE_OPEN, options, 0, NULL );
        free( unix_name );
    }
    else set_error( STATUS_OBJECT_TYPE_MISMATCH );
    return new_file;
}

static int file_set_sd( struct object *obj, const struct security_descriptor *sd,
                        unsigned int set_info )
{
    struct file *file = (struct file *)obj;
    struct fd *fd;
    int ret;

    assert( obj->ops == &file_ops );

    fd = file_get_fd( obj );
    ret = set_file_sd( obj, fd, &file->mode, &file->uid, sd, set_info );
    release_object( fd );
    return ret;
}

static void file_destroy( struct object *obj )
{
    struct file *file = (struct file *)obj;
    assert( obj->ops == &file_ops );

    if (file->fd) release_object( file->fd );
}

/* set the last error depending on errno */
void file_set_error(void)
{
    switch (errno)
    {
    case ETXTBSY:
    case EAGAIN:    set_error( STATUS_SHARING_VIOLATION ); break;
    case EBADF:     set_error( STATUS_INVALID_HANDLE ); break;
    case ENOSPC:    set_error( STATUS_DISK_FULL ); break;
    case EACCES:
    case ESRCH:
    case EROFS:
    case EPERM:     set_error( STATUS_ACCESS_DENIED ); break;
    case EBUSY:     set_error( STATUS_FILE_LOCK_CONFLICT ); break;
    case ENOENT:    set_error( STATUS_NO_SUCH_FILE ); break;
    case EISDIR:    set_error( STATUS_FILE_IS_A_DIRECTORY ); break;
    case ENFILE:
    case EMFILE:    set_error( STATUS_TOO_MANY_OPENED_FILES ); break;
    case EEXIST:    set_error( STATUS_OBJECT_NAME_COLLISION ); break;
    case EINVAL:    set_error( STATUS_INVALID_PARAMETER ); break;
    case ESPIPE:    set_error( STATUS_ILLEGAL_FUNCTION ); break;
    case ENOTEMPTY: set_error( STATUS_DIRECTORY_NOT_EMPTY ); break;
    case EIO:       set_error( STATUS_ACCESS_VIOLATION ); break;
    case ENOTDIR:   set_error( STATUS_NOT_A_DIRECTORY ); break;
    case EFBIG:     set_error( STATUS_SECTION_TOO_BIG ); break;
    case ENODEV:    set_error( STATUS_NO_SUCH_DEVICE ); break;
    case ENXIO:     set_error( STATUS_NO_SUCH_DEVICE ); break;
#ifdef EOVERFLOW
    case EOVERFLOW: set_error( STATUS_INVALID_PARAMETER ); break;
#endif
    default:
        perror("wineserver: file_set_error() can't map error");
        set_error( STATUS_UNSUCCESSFUL );
        break;
    }
}

struct file *get_file_obj( struct process *process, obj_handle_t handle, unsigned int access )
{
    return (struct file *)get_handle_obj( process, handle, access, &file_ops );
}

int get_file_unix_fd( struct file *file )
{
    return get_unix_fd( file->fd );
}

/* create a file */
DECL_HANDLER(create_file)
{
    struct object *file;
    struct fd *root_fd = NULL;
    struct unicode_str unicode_name;
    const struct security_descriptor *sd;
    const struct object_attributes *objattr = get_req_object_attributes( &sd, &unicode_name, NULL );
    const char *name;
    data_size_t name_len;

    if (!objattr) return;

    /* name is transferred in the unix codepage outside of the objattr structure */
    if (unicode_name.len)
    {
        set_error( STATUS_INVALID_PARAMETER );
        return;
    }

    if (objattr->rootdir)
    {
        struct dir *root;

        if (!(root = get_dir_obj( current->process, objattr->rootdir, 0 ))) return;
        root_fd = get_obj_fd( (struct object *)root );
        release_object( root );
        if (!root_fd) return;
    }

    name = get_req_data_after_objattr( objattr, &name_len );

    reply->handle = 0;
    if ((file = create_file( root_fd, name, name_len, req->access, req->sharing,
                             req->create, req->options, req->attrs, sd )))
    {
        reply->handle = alloc_handle( current->process, file, req->access, objattr->attributes );
        release_object( file );
    }
    if (root_fd) release_object( root_fd );
}

/* allocate a file handle for a Unix fd */
DECL_HANDLER(alloc_file_handle)
{
    struct file *file;
    int fd;

    reply->handle = 0;
    if ((fd = thread_get_inflight_fd( current, req->fd )) == -1)
    {
        set_error( STATUS_INVALID_HANDLE );
        return;
    }
    if ((file = create_file_for_fd( fd, req->access, FILE_SHARE_READ | FILE_SHARE_WRITE )))
    {
        reply->handle = alloc_handle( current->process, file, req->access, req->attributes );
        release_object( file );
    }
}

/* lock a region of a file */
DECL_HANDLER(lock_file)
{
    struct file *file;

    if ((file = get_file_obj( current->process, req->handle, 0 )))
    {
        reply->handle = lock_fd( file->fd, req->offset, req->count, req->shared, req->wait );
        reply->overlapped = is_overlapped( file );
        release_object( file );
    }
}

/* unlock a region of a file */
DECL_HANDLER(unlock_file)
{
    struct file *file;

    if ((file = get_file_obj( current->process, req->handle, 0 )))
    {
        unlock_fd( file->fd, req->offset, req->count );
        release_object( file );
    }
}
