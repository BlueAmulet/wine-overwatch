/*
 * Server-side change notification management
 *
 * Copyright (C) 1998 Alexandre Julliard
 * Copyright (C) 2006 Mike McCormack
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
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <dirent.h>
#include <errno.h>
#ifdef HAVE_POLL_H
# include <poll.h>
#endif

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"

#include "file.h"
#include "handle.h"
#include "thread.h"
#include "request.h"
#include "process.h"
#include "security.h"
#include "winternl.h"

/* dnotify support */

#ifdef linux
#ifndef F_NOTIFY
#define F_NOTIFY 1026
#define DN_ACCESS       0x00000001      /* File accessed */
#define DN_MODIFY       0x00000002      /* File modified */
#define DN_CREATE       0x00000004      /* File created */
#define DN_DELETE       0x00000008      /* File removed */
#define DN_RENAME       0x00000010      /* File renamed */
#define DN_ATTRIB       0x00000020      /* File changed attributes */
#define DN_MULTISHOT    0x80000000      /* Don't remove notifier */
#endif
#endif

/* inotify support */

#ifdef HAVE_SYS_INOTIFY_H
#include <sys/inotify.h>
#define USE_INOTIFY
#elif defined(__linux__) && defined(__i386__)

#define SYS_inotify_init	291
#define SYS_inotify_add_watch	292
#define SYS_inotify_rm_watch	293

struct inotify_event {
    int           wd;
    unsigned int  mask;
    unsigned int  cookie;
    unsigned int  len;
    char          name[1];
};

#define IN_ACCESS        0x00000001
#define IN_MODIFY        0x00000002
#define IN_ATTRIB        0x00000004
#define IN_CLOSE_WRITE   0x00000008
#define IN_CLOSE_NOWRITE 0x00000010
#define IN_OPEN          0x00000020
#define IN_MOVED_FROM    0x00000040
#define IN_MOVED_TO      0x00000080
#define IN_CREATE        0x00000100
#define IN_DELETE        0x00000200
#define IN_DELETE_SELF   0x00000400

#define IN_ISDIR         0x40000000

static inline int inotify_init( void )
{
    return syscall( SYS_inotify_init );
}

static inline int inotify_add_watch( int fd, const char *name, unsigned int mask )
{
    return syscall( SYS_inotify_add_watch, fd, name, mask );
}

static inline int inotify_rm_watch( int fd, int wd )
{
    return syscall( SYS_inotify_rm_watch, fd, wd );
}

#define USE_INOTIFY

#endif

struct inode;

static void free_inode( struct inode *inode );

static struct fd *inotify_fd;

struct change_record {
    struct list entry;
    unsigned int cookie;
    struct filesystem_event event;
};

struct dir
{
    struct object  obj;      /* object header */
    struct fd     *fd;       /* file descriptor to the directory */
    mode_t         mode;     /* file stat.st_mode */
    uid_t          uid;      /* file stat.st_uid */
    struct list    entry;    /* entry in global change notifications list */
    unsigned int   filter;   /* notification filter */
    int            notified; /* SIGIO counter */
    int            want_data; /* return change data */
    int            subtree;  /* do we want to watch subdirectories? */
    struct list    change_records;   /* data for the change */
    struct list    in_entry; /* entry in the inode dirs list */
    struct inode  *inode;    /* inode of the associated directory */
    struct process *client_process;  /* client process that has a cache for this directory */
    int             client_entry;    /* entry in client process cache */
};

static struct fd *dir_get_fd( struct object *obj );
static struct security_descriptor *dir_get_sd( struct object *obj );
static int dir_set_sd( struct object *obj, const struct security_descriptor *sd,
                       unsigned int set_info );
static void dir_dump( struct object *obj, int verbose );
static struct object_type *dir_get_type( struct object *obj );
static int dir_close_handle( struct object *obj, struct process *process, obj_handle_t handle );
static void dir_destroy( struct object *obj );

static const struct object_ops dir_ops =
{
    sizeof(struct dir),       /* size */
    dir_dump,                 /* dump */
    dir_get_type,             /* get_type */
    add_queue,                /* add_queue */
    remove_queue,             /* remove_queue */
    default_fd_signaled,      /* signaled */
    no_satisfied,             /* satisfied */
    no_signal,                /* signal */
    dir_get_fd,               /* get_fd */
    default_fd_map_access,    /* map_access */
    dir_get_sd,               /* get_sd */
    dir_set_sd,               /* set_sd */
    no_lookup_name,           /* lookup_name */
    no_link_name,             /* link_name */
    NULL,                     /* unlink_name */
    no_open_file,             /* open_file */
    no_alloc_handle,          /* alloc_handle */
    dir_close_handle,         /* close_handle */
    dir_destroy               /* destroy */
};

static int dir_get_poll_events( struct fd *fd );
static enum server_fd_type dir_get_fd_type( struct fd *fd );

static const struct fd_ops dir_fd_ops =
{
    dir_get_poll_events,         /* get_poll_events */
    default_poll_event,          /* poll_event */
    dir_get_fd_type,             /* get_fd_type */
    no_fd_read,                  /* read */
    no_fd_write,                 /* write */
    no_fd_flush,                 /* flush */
    default_fd_ioctl,            /* ioctl */
    default_fd_queue_async,      /* queue_async */
    default_fd_reselect_async    /* reselect_async */
};

static struct list change_list = LIST_INIT(change_list);

/* per-process structure to keep track of cache entries on the client size */
struct dir_cache
{
    unsigned int  size;
    unsigned int  count;
    unsigned char state[1];
};

enum dir_cache_state
{
    DIR_CACHE_STATE_FREE,
    DIR_CACHE_STATE_INUSE,
    DIR_CACHE_STATE_RELEASED
};

/* return an array of cache entries that can be freed on the client side */
static int *get_free_dir_cache_entries( struct process *process, data_size_t *size )
{
    int *ret;
    struct dir_cache *cache = process->dir_cache;
    unsigned int i, j, count;

    if (!cache) return NULL;
    for (i = count = 0; i < cache->count && count < *size / sizeof(*ret); i++)
        if (cache->state[i] == DIR_CACHE_STATE_RELEASED) count++;
    if (!count) return NULL;

    if ((ret = malloc( count * sizeof(*ret) )))
    {
        for (i = j = 0; j < count; i++)
        {
            if (cache->state[i] != DIR_CACHE_STATE_RELEASED) continue;
            cache->state[i] = DIR_CACHE_STATE_FREE;
            ret[j++] = i;
        }
        *size = count * sizeof(*ret);
    }
    return ret;
}

/* allocate a new client-side directory cache entry */
static int alloc_dir_cache_entry( struct dir *dir, struct process *process )
{
    unsigned int i = 0;
    struct dir_cache *cache = process->dir_cache;

    if (cache)
        for (i = 0; i < cache->count; i++)
            if (cache->state[i] == DIR_CACHE_STATE_FREE) goto found;

    if (!cache || cache->count == cache->size)
    {
        unsigned int size = cache ? cache->size * 2 : 256;
        if (!(cache = realloc( cache, offsetof( struct dir_cache, state[size] ))))
        {
            set_error( STATUS_NO_MEMORY );
            return -1;
        }
        process->dir_cache = cache;
        cache->size = size;
    }
    cache->count = i + 1;

found:
    cache->state[i] = DIR_CACHE_STATE_INUSE;
    return i;
}

/* release a directory cache entry; it will be freed on the client side on the next cache request */
static void release_dir_cache_entry( struct dir *dir )
{
    struct dir_cache *cache;

    if (!dir->client_process) return;
    cache = dir->client_process->dir_cache;
    cache->state[dir->client_entry] = DIR_CACHE_STATE_RELEASED;
    release_object( dir->client_process );
    dir->client_process = NULL;
}

static void dnotify_adjust_changes( struct dir *dir )
{
#if defined(F_SETSIG) && defined(F_NOTIFY)
    int fd = get_unix_fd( dir->fd );
    unsigned int filter = dir->filter;
    unsigned int val;
    if ( 0 > fcntl( fd, F_SETSIG, SIGIO) )
        return;

    val = DN_MULTISHOT;
    if (filter & FILE_NOTIFY_CHANGE_FILE_NAME)
        val |= DN_RENAME | DN_DELETE | DN_CREATE;
    if (filter & FILE_NOTIFY_CHANGE_DIR_NAME)
        val |= DN_RENAME | DN_DELETE | DN_CREATE;
    if (filter & FILE_NOTIFY_CHANGE_ATTRIBUTES)
        val |= DN_ATTRIB;
    if (filter & FILE_NOTIFY_CHANGE_SIZE)
        val |= DN_MODIFY;
    if (filter & FILE_NOTIFY_CHANGE_LAST_WRITE)
        val |= DN_MODIFY;
    if (filter & FILE_NOTIFY_CHANGE_LAST_ACCESS)
        val |= DN_ACCESS;
    if (filter & FILE_NOTIFY_CHANGE_CREATION)
        val |= DN_CREATE;
    if (filter & FILE_NOTIFY_CHANGE_SECURITY)
        val |= DN_ATTRIB;
    fcntl( fd, F_NOTIFY, val );
#endif
}

/* insert change in the global list */
static inline void insert_change( struct dir *dir )
{
    sigset_t sigset;

    sigemptyset( &sigset );
    sigaddset( &sigset, SIGIO );
    sigprocmask( SIG_BLOCK, &sigset, NULL );
    list_add_head( &change_list, &dir->entry );
    sigprocmask( SIG_UNBLOCK, &sigset, NULL );
}

/* remove change from the global list */
static inline void remove_change( struct dir *dir )
{
    sigset_t sigset;

    sigemptyset( &sigset );
    sigaddset( &sigset, SIGIO );
    sigprocmask( SIG_BLOCK, &sigset, NULL );
    list_remove( &dir->entry );
    sigprocmask( SIG_UNBLOCK, &sigset, NULL );
}

static void dir_dump( struct object *obj, int verbose )
{
    struct dir *dir = (struct dir *)obj;
    assert( obj->ops == &dir_ops );
    fprintf( stderr, "Dirfile fd=%p filter=%08x\n", dir->fd, dir->filter );
}

static struct object_type *dir_get_type( struct object *obj )
{
    static const WCHAR name[] = {'F','i','l','e'};
    static const struct unicode_str str = { name, sizeof(name) };
    return get_object_type( &str );
}

/* enter here directly from SIGIO signal handler */
void do_change_notify( int unix_fd )
{
    struct dir *dir;

    /* FIXME: this is O(n) ... probably can be improved */
    LIST_FOR_EACH_ENTRY( dir, &change_list, struct dir, entry )
    {
        if (get_unix_fd( dir->fd ) != unix_fd) continue;
        interlocked_xchg_add( &dir->notified, 1 );
        break;
    }
}

/* SIGIO callback, called synchronously with the poll loop */
void sigio_callback(void)
{
    struct dir *dir;

    LIST_FOR_EACH_ENTRY( dir, &change_list, struct dir, entry )
    {
        if (interlocked_xchg( &dir->notified, 0 ))
            fd_async_wake_up( dir->fd, ASYNC_TYPE_WAIT, STATUS_ALERTED );
    }
}

static struct fd *dir_get_fd( struct object *obj )
{
    struct dir *dir = (struct dir *)obj;
    assert( obj->ops == &dir_ops );
    return (struct fd *)grab_object( dir->fd );
}

static struct security_descriptor *dir_get_sd( struct object *obj )
{
    struct dir *dir = (struct dir *)obj;
    struct security_descriptor *sd;
    struct fd *fd;

    assert( obj->ops == &dir_ops );

    fd = dir_get_fd( obj );
    sd = get_file_sd( obj, fd, &dir->mode, &dir->uid );
    release_object( fd );
    return sd;
}

static int dir_set_sd( struct object *obj, const struct security_descriptor *sd,
                       unsigned int set_info )
{
    struct dir *dir = (struct dir *)obj;
    struct fd *fd;
    int ret;

    assert( obj->ops == &dir_ops );

    fd = dir_get_fd( obj );
    ret = set_file_sd( obj, fd, &dir->mode, &dir->uid, sd, set_info );
    release_object( fd );
    return ret;
}

static struct change_record *get_first_change_record( struct dir *dir )
{
    struct list *ptr = list_head( &dir->change_records );
    if (!ptr) return NULL;
    list_remove( ptr );
    return LIST_ENTRY( ptr, struct change_record, entry );
}

static int dir_close_handle( struct object *obj, struct process *process, obj_handle_t handle )
{
    struct dir *dir = (struct dir *)obj;

    if (!fd_close_handle( obj, process, handle )) return 0;
    if (obj->handle_count == 1) release_dir_cache_entry( dir ); /* closing last handle, release cache */
    return 1;  /* ok to close */
}

static void dir_destroy( struct object *obj )
{
    struct change_record *record;
    struct dir *dir = (struct dir *)obj;
    assert (obj->ops == &dir_ops);

    if (dir->filter)
        remove_change( dir );

    if (dir->inode)
    {
        list_remove( &dir->in_entry );
        free_inode( dir->inode );
    }

    while ((record = get_first_change_record( dir ))) free( record );

    release_dir_cache_entry( dir );
    release_object( dir->fd );

    if (inotify_fd && list_empty( &change_list ))
    {
        release_object( inotify_fd );
        inotify_fd = NULL;
    }
}

struct dir *get_dir_obj( struct process *process, obj_handle_t handle, unsigned int access )
{
    return (struct dir *)get_handle_obj( process, handle, access, &dir_ops );
}

static int dir_get_poll_events( struct fd *fd )
{
    return 0;
}

static enum server_fd_type dir_get_fd_type( struct fd *fd )
{
    return FD_TYPE_DIR;
}

#ifdef USE_INOTIFY

#define HASH_SIZE 31

struct inode {
    struct list ch_entry;    /* entry in the children list */
    struct list children;    /* children of this inode */
    struct inode *parent;    /* parent of this inode */
    struct list dirs;        /* directory handles watching this inode */
    struct list ino_entry;   /* entry in the inode hash */
    struct list wd_entry;    /* entry in the watch descriptor hash */
    dev_t dev;               /* device number */
    ino_t ino;               /* device's inode number */
    int wd;                  /* inotify's watch descriptor */
    char *name;              /* basename name of the inode */
};

static struct list inode_hash[ HASH_SIZE ];
static struct list wd_hash[ HASH_SIZE ];

static int inotify_add_dir( char *path, unsigned int filter );

static struct inode *inode_from_wd( int wd )
{
    struct list *bucket = &wd_hash[ wd % HASH_SIZE ];
    struct inode *inode;

    LIST_FOR_EACH_ENTRY( inode, bucket, struct inode, wd_entry )
        if (inode->wd == wd)
            return inode;

    return NULL;
}

static inline struct list *get_hash_list( dev_t dev, ino_t ino )
{
    return &inode_hash[ (ino ^ dev) % HASH_SIZE ];
}

static struct inode *find_inode( dev_t dev, ino_t ino )
{
    struct list *bucket = get_hash_list( dev, ino );
    struct inode *inode;

    LIST_FOR_EACH_ENTRY( inode, bucket, struct inode, ino_entry )
        if (inode->ino == ino && inode->dev == dev)
             return inode;

    return NULL;
}

static struct inode *create_inode( dev_t dev, ino_t ino )
{
    struct inode *inode;

    inode = malloc( sizeof *inode );
    if (inode)
    {
        list_init( &inode->children );
        list_init( &inode->dirs );
        inode->ino = ino;
        inode->dev = dev;
        inode->wd = -1;
        inode->parent = NULL;
        inode->name = NULL;
        list_add_tail( get_hash_list( dev, ino ), &inode->ino_entry );
    }
    return inode;
}

static struct inode *get_inode( dev_t dev, ino_t ino )
{
    struct inode *inode;

    inode = find_inode( dev, ino );
    if (inode)
        return inode;
    return create_inode( dev, ino );
}

static void inode_set_wd( struct inode *inode, int wd )
{
    if (inode->wd != -1)
        list_remove( &inode->wd_entry );
    inode->wd = wd;
    list_add_tail( &wd_hash[ wd % HASH_SIZE ], &inode->wd_entry );
}

static void inode_set_name( struct inode *inode, const char *name )
{
    free (inode->name);
    inode->name = name ? strdup( name ) : NULL;
}

static void free_inode( struct inode *inode )
{
    int subtree = 0, watches = 0;
    struct inode *tmp, *next;
    struct dir *dir;

    LIST_FOR_EACH_ENTRY( dir, &inode->dirs, struct dir, in_entry )
    {
        subtree |= dir->subtree;
        watches++;
    }

    if (!subtree && !inode->parent)
    {
        LIST_FOR_EACH_ENTRY_SAFE( tmp, next, &inode->children,
                                  struct inode, ch_entry )
        {
            assert( tmp != inode );
            assert( tmp->parent == inode );
            free_inode( tmp );
        }
    }

    if (watches)
        return;

    if (inode->parent)
        list_remove( &inode->ch_entry );

    /* disconnect remaining children from the parent */
    LIST_FOR_EACH_ENTRY_SAFE( tmp, next, &inode->children, struct inode, ch_entry )
    {
        list_remove( &tmp->ch_entry );
        tmp->parent = NULL;
    }

    if (inode->wd != -1)
    {
        inotify_rm_watch( get_unix_fd( inotify_fd ), inode->wd );
        list_remove( &inode->wd_entry );
    }
    list_remove( &inode->ino_entry );

    free( inode->name );
    free( inode );
}

static struct inode *inode_add( struct inode *parent,
                                dev_t dev, ino_t ino, const char *name )
{
    struct inode *inode;
 
    inode = get_inode( dev, ino );
    if (!inode)
        return NULL;
 
    if (!inode->parent)
    {
        list_add_tail( &parent->children, &inode->ch_entry );
        inode->parent = parent;
        assert( inode != parent );
    }
    inode_set_name( inode, name );

    return inode;
}

static struct inode *inode_from_name( struct inode *inode, const char *name )
{
    struct inode *i;

    LIST_FOR_EACH_ENTRY( i, &inode->children, struct inode, ch_entry )
        if (i->name && !strcmp( i->name, name ))
            return i;
    return NULL;
}

static int inotify_get_poll_events( struct fd *fd );
static void inotify_poll_event( struct fd *fd, int event );

static const struct fd_ops inotify_fd_ops =
{
    inotify_get_poll_events,     /* get_poll_events */
    inotify_poll_event,          /* poll_event */
    NULL,                        /* flush */
    NULL,                        /* get_fd_type */
    NULL,                        /* ioctl */
    NULL,                        /* queue_async */
    NULL                         /* reselect_async */
};

static int inotify_get_poll_events( struct fd *fd )
{
    return POLLIN;
}

static void inotify_do_change_notify( struct dir *dir, unsigned int action,
                                      unsigned int cookie, const char *relpath )
{
    struct change_record *record;

    assert( dir->obj.ops == &dir_ops );

    if (dir->want_data)
    {
        size_t len = strlen(relpath);
        record = malloc( offsetof(struct change_record, event.name[len]) );
        if (!record)
            return;

        record->cookie = cookie;
        record->event.action = action;
        memcpy( record->event.name, relpath, len );
        record->event.len = len;

        list_add_tail( &dir->change_records, &record->entry );
    }

    fd_async_wake_up( dir->fd, ASYNC_TYPE_WAIT, STATUS_ALERTED );
}

static unsigned int filter_from_event( struct inotify_event *ie )
{
    unsigned int filter = 0;

    if (ie->mask & (IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE | IN_CREATE))
        filter |= FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME;
    if (ie->mask & IN_MODIFY)
        filter |= FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
    if (ie->mask & IN_ATTRIB)
        filter |= FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SECURITY;
    if (ie->mask & IN_ACCESS)
        filter |= FILE_NOTIFY_CHANGE_LAST_ACCESS;
    if (ie->mask & IN_CREATE)
        filter |= FILE_NOTIFY_CHANGE_CREATION;

    if (ie->mask & IN_ISDIR)
        filter &= ~FILE_NOTIFY_CHANGE_FILE_NAME;
    else
        filter &= ~FILE_NOTIFY_CHANGE_DIR_NAME;

    return filter;
}

/* scan up the parent directories for watches */
static unsigned int filter_from_inode( struct inode *inode, int is_parent )
{
    unsigned int filter = 0;
    struct dir *dir;

    /* combine filters from parents watching subtrees */
    while (inode)
    {
        LIST_FOR_EACH_ENTRY( dir, &inode->dirs, struct dir, in_entry )
            if (dir->subtree || !is_parent)
                filter |= dir->filter;
        is_parent = 1;
        inode = inode->parent;
    }

    return filter;
}

static char *inode_get_path( struct inode *inode, int sz )
{
    struct list *head;
    char *path;
    int len;

    if (!inode)
        return NULL;

    head = list_head( &inode->dirs );
    if (head)
    {
        int unix_fd = get_unix_fd( LIST_ENTRY( head, struct dir, in_entry )->fd );
        path = malloc ( 32 + sz );
        if (path)
            sprintf( path, "/proc/self/fd/%u/", unix_fd );
        return path;
    }

    if (!inode->name)
        return NULL;

    len = strlen( inode->name );
    path = inode_get_path( inode->parent, sz + len + 1 );
    if (!path)
        return NULL;
    
    strcat( path, inode->name );
    strcat( path, "/" );

    return path;
}

static void inode_check_dir( struct inode *parent, const char *name )
{
    char *path;
    unsigned int filter;
    struct inode *inode;
    struct stat st;
    int wd = -1;

    path = inode_get_path( parent, strlen(name) );
    if (!path)
        return;

    strcat( path, name );

    if (stat( path, &st ) < 0)
        goto end;

    filter = filter_from_inode( parent, 1 );
    if (!filter)
        goto end;

    inode = inode_add( parent, st.st_dev, st.st_ino, name );
    if (!inode || inode->wd != -1)
        goto end;

    wd = inotify_add_dir( path, filter );
    if (wd != -1)
        inode_set_wd( inode, wd );
    else
        free_inode( inode );

end:
    free( path );
}

static int prepend( char **path, const char *segment )
{
    int extra;
    char *p;

    extra = strlen( segment ) + 1;
    if (*path)
    {
        int len = strlen( *path ) + 1;
        p = realloc( *path, len + extra );
        if (!p) return 0;
        memmove( &p[ extra ], p, len );
        p[ extra - 1 ] = '/';
        memcpy( p, segment, extra - 1 );
    }
    else
    {
        p = malloc( extra );
        if (!p) return 0;
        memcpy( p, segment, extra );
    }

    *path = p;

    return 1;
}

static void inotify_notify_all( struct inotify_event *ie )
{
    unsigned int filter, action;
    struct inode *inode, *i;
    char *path = NULL;
    struct dir *dir;

    inode = inode_from_wd( ie->wd );
    if (!inode)
    {
        fprintf( stderr, "no inode matches %d\n", ie->wd);
        return;
    }

    filter = filter_from_event( ie );
    
    if (ie->mask & IN_CREATE)
    {
        if (ie->mask & IN_ISDIR)
            inode_check_dir( inode, ie->name );

        action = FILE_ACTION_ADDED;
    }
    else if (ie->mask & IN_DELETE)
        action = FILE_ACTION_REMOVED;
    else if (ie->mask & IN_MOVED_FROM)
        action = FILE_ACTION_RENAMED_OLD_NAME;
    else if (ie->mask & IN_MOVED_TO)
        action = FILE_ACTION_RENAMED_NEW_NAME;
    else
        action = FILE_ACTION_MODIFIED;

    /*
     * Work our way up the inode hierarchy
     *  extending the relative path as we go
     *  and notifying all recursive watches.
     */
    if (!prepend( &path, ie->name ))
        return;

    for (i = inode; i; i = i->parent)
    {
        LIST_FOR_EACH_ENTRY( dir, &i->dirs, struct dir, in_entry )
            if ((filter & dir->filter) && (i==inode || dir->subtree))
                inotify_do_change_notify( dir, action, ie->cookie, path );

        if (!i->name || !prepend( &path, i->name ))
            break;
    }

    free( path );

    if (ie->mask & IN_DELETE)
    {
        i = inode_from_name( inode, ie->name );
        if (i)
            free_inode( i );
    }
}

static void inotify_poll_event( struct fd *fd, int event )
{
    int r, ofs, unix_fd;
    char buffer[0x1000];
    struct inotify_event *ie;

    unix_fd = get_unix_fd( fd );
    r = read( unix_fd, buffer, sizeof buffer );
    if (r < 0)
    {
        fprintf(stderr,"inotify_poll_event(): inotify read failed!\n");
        return;
    }

    for( ofs = 0; ofs < r - offsetof(struct inotify_event, name); )
    {
        ie = (struct inotify_event*) &buffer[ofs];
        if (!ie->len)
            break;
        ofs += offsetof( struct inotify_event, name[ie->len] );
        if (ofs > r) break;
        inotify_notify_all( ie );
    }
}

static inline struct fd *create_inotify_fd( void )
{
    int unix_fd;

    unix_fd = inotify_init();
    if (unix_fd<0)
        return NULL;
    return create_anonymous_fd( &inotify_fd_ops, unix_fd, NULL, 0 );
}

static int map_flags( unsigned int filter )
{
    unsigned int mask;

    /* always watch these so we can track subdirectories in recursive watches */
    mask = (IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE | IN_CREATE | IN_DELETE_SELF);

    if (filter & FILE_NOTIFY_CHANGE_ATTRIBUTES)
        mask |= IN_ATTRIB;
    if (filter & FILE_NOTIFY_CHANGE_SIZE)
        mask |= IN_MODIFY;
    if (filter & FILE_NOTIFY_CHANGE_LAST_WRITE)
        mask |= IN_MODIFY;
    if (filter & FILE_NOTIFY_CHANGE_LAST_ACCESS)
        mask |= IN_ACCESS;
    if (filter & FILE_NOTIFY_CHANGE_SECURITY)
        mask |= IN_ATTRIB;

    return mask;
}

static int inotify_add_dir( char *path, unsigned int filter )
{
    int wd = inotify_add_watch( get_unix_fd( inotify_fd ),
                                path, map_flags( filter ) );
    if (wd != -1)
        set_fd_events( inotify_fd, POLLIN );
    return wd;
}

static int init_inotify( void )
{
    int i;

    if (inotify_fd)
        return 1;

    inotify_fd = create_inotify_fd();
    if (!inotify_fd)
        return 0;

    for (i=0; i<HASH_SIZE; i++)
    {
        list_init( &inode_hash[i] );
        list_init( &wd_hash[i] );
    }

    return 1;
}

static int inotify_adjust_changes( struct dir *dir )
{
    unsigned int filter;
    struct inode *inode;
    struct stat st;
    char path[32];
    int wd, unix_fd;

    if (!inotify_fd)
        return 0;

    unix_fd = get_unix_fd( dir->fd );

    inode = dir->inode;
    if (!inode)
    {
        /* check if this fd is already being watched */
        if (-1 == fstat( unix_fd, &st ))
            return 0;

        inode = get_inode( st.st_dev, st.st_ino );
        if (!inode)
            inode = create_inode( st.st_dev, st.st_ino );
        if (!inode)
            return 0;
        list_add_tail( &inode->dirs, &dir->in_entry );
        dir->inode = inode;
    }

    filter = filter_from_inode( inode, 0 );

    sprintf( path, "/proc/self/fd/%u", unix_fd );
    wd = inotify_add_dir( path, filter );
    if (wd == -1) return 0;

    inode_set_wd( inode, wd );

    return 1;
}

static char *get_basename( const char *link )
{
    char *buffer, *name = NULL;
    int r, n = 0x100;

    while (1)
    {
        buffer = malloc( n );
        if (!buffer) return NULL;

        r = readlink( link, buffer, n );
        if (r < 0)
            break;

        if (r < n)
        {
            name = buffer;
            break;
        }
        free( buffer );
        n *= 2;
    }

    if (name)
    {
        while (r > 0 && name[ r - 1 ] == '/' )
            r--;
        name[ r ] = 0;

        name = strrchr( name, '/' );
        if (name)
            name = strdup( &name[1] );
    }

    free( buffer );
    return name;
}

static int dir_add_to_existing_notify( struct dir *dir )
{
    struct inode *inode, *parent;
    unsigned int filter = 0;
    struct stat st, st_new;
    char link[35], *name;
    int wd, unix_fd;

    if (!inotify_fd)
        return 0;

    unix_fd = get_unix_fd( dir->fd );

    /* check if it's in the list of inodes we want to watch */
    if (-1 == fstat( unix_fd, &st_new ))
        return 0;
    inode = find_inode( st_new.st_dev, st_new.st_ino );
    if (inode)
        return 0;

    /* lookup the parent */
    sprintf( link, "/proc/self/fd/%u/..", unix_fd );
    if (-1 == stat( link, &st ))
        return 0;

    /*
     * If there's no parent, stop.  We could keep going adding
     *  ../ to the path until we hit the root of the tree or
     *  find a recursively watched ancestor.
     * Assume it's too expensive to search up the tree for now.
     */
    parent = find_inode( st.st_dev, st.st_ino );
    if (!parent)
        return 0;

    if (parent->wd == -1)
        return 0;

    filter = filter_from_inode( parent, 1 );
    if (!filter)
        return 0;

    sprintf( link, "/proc/self/fd/%u", unix_fd );
    name = get_basename( link );
    if (!name)
        return 0;
    inode = inode_add( parent, st_new.st_dev, st_new.st_ino, name );
    free( name );
    if (!inode)
        return 0;

    /* Couldn't find this inode at the start of the function, must be new */
    assert( inode->wd == -1 );

    wd = inotify_add_dir( link, filter );
    if (wd != -1)
        inode_set_wd( inode, wd );

    return 1;
}

#else

static int init_inotify( void )
{
    return 0;
}

static int inotify_adjust_changes( struct dir *dir )
{
    return 0;
}

static void free_inode( struct inode *inode )
{
    assert( 0 );
}

static int dir_add_to_existing_notify( struct dir *dir )
{
    return 0;
}

#endif  /* USE_INOTIFY */

struct object *create_dir_obj( struct fd *fd, unsigned int access, mode_t mode,
                               const struct security_descriptor *sd )
{
    struct dir *dir;

    dir = alloc_object( &dir_ops );
    if (!dir)
        return NULL;

    list_init( &dir->change_records );
    dir->filter = 0;
    dir->notified = 0;
    dir->want_data = 0;
    dir->inode = NULL;
    grab_object( fd );
    dir->fd = fd;
    dir->mode = mode;
    dir->uid  = ~(uid_t)0;
    dir->client_process = NULL;
    set_fd_user( fd, &dir_fd_ops, &dir->obj );

    if (sd) dir_set_sd( &dir->obj, sd, OWNER_SECURITY_INFORMATION |
                                       GROUP_SECURITY_INFORMATION |
                                       DACL_SECURITY_INFORMATION |
                                       SACL_SECURITY_INFORMATION );

    dir_add_to_existing_notify( dir );

    return &dir->obj;
}

/* retrieve (or allocate) the client-side directory cache entry */
DECL_HANDLER(get_directory_cache_entry)
{
    struct dir *dir;
    int *free_entries;
    data_size_t free_size;

    if (!(dir = get_dir_obj( current->process, req->handle, 0 ))) return;

    if (!dir->client_process)
    {
        if ((dir->client_entry = alloc_dir_cache_entry( dir, current->process )) == -1) goto done;
        dir->client_process = (struct process *)grab_object( current->process );
    }

    if (dir->client_process == current->process) reply->entry = dir->client_entry;
    else set_error( STATUS_SHARING_VIOLATION );

done:  /* allow freeing entries even on failure */
    free_size = get_reply_max_size();
    free_entries = get_free_dir_cache_entries( current->process, &free_size );
    if (free_entries) set_reply_data_ptr( free_entries, free_size );

    release_object( dir );
}

/* enable change notifications for a directory */
DECL_HANDLER(read_directory_changes)
{
    struct dir *dir;
    struct async *async;

    if (!req->filter)
    {
        set_error(STATUS_INVALID_PARAMETER);
        return;
    }

    dir = get_dir_obj( current->process, req->async.handle, 0 );
    if (!dir)
        return;

    /* requests don't timeout */
    if (!(async = fd_queue_async( dir->fd, &req->async, NULL, ASYNC_TYPE_WAIT ))) goto end;

    /* assign it once */
    if (!dir->filter)
    {
        init_inotify();
        insert_change( dir );
        dir->filter = req->filter;
        dir->subtree = req->subtree;
        dir->want_data = req->want_data;
    }

    /* if there's already a change in the queue, send it */
    if (!list_empty( &dir->change_records ))
        fd_async_wake_up( dir->fd, ASYNC_TYPE_WAIT, STATUS_ALERTED );

    /* setup the real notification */
    if (!inotify_adjust_changes( dir ))
        dnotify_adjust_changes( dir );

    release_object( async );
    set_error(STATUS_PENDING);

end:
    release_object( dir );
}

DECL_HANDLER(read_change)
{
    struct change_record *record, *next;
    struct dir *dir;
    struct list events;
    char *data, *event;
    int size = 0;

    dir = get_dir_obj( current->process, req->handle, 0 );
    if (!dir)
        return;

    list_init( &events );
    list_move_tail( &events, &dir->change_records );
    release_object( dir );

    if (list_empty( &events ))
    {
        set_error( STATUS_NO_DATA_DETECTED );
        return;
    }

    LIST_FOR_EACH_ENTRY( record, &events, struct change_record, entry )
    {
        size += (offsetof(struct filesystem_event, name[record->event.len])
                + sizeof(int)-1) / sizeof(int) * sizeof(int);
    }

    if (size > get_reply_max_size())
        set_error( STATUS_BUFFER_TOO_SMALL );
    else if ((data = mem_alloc( size )) != NULL)
    {
        event = data;
        LIST_FOR_EACH_ENTRY( record, &events, struct change_record, entry )
        {
            data_size_t len = offsetof( struct filesystem_event, name[record->event.len] );

            /* FIXME: rename events are sometimes reported as delete/create */
            if (record->event.action == FILE_ACTION_RENAMED_OLD_NAME)
            {
                struct list *elem = list_next( &events, &record->entry );
                if (elem)
                    next = LIST_ENTRY(elem, struct change_record, entry);

                if (elem && next->cookie == record->cookie)
                    next->cookie = 0;
                else
                    record->event.action = FILE_ACTION_REMOVED;
            }
            else if (record->event.action == FILE_ACTION_RENAMED_NEW_NAME && record->cookie)
                record->event.action = FILE_ACTION_ADDED;

            memcpy( event, &record->event, len );
            event += len;
            if (len % sizeof(int))
            {
                memset( event, 0, sizeof(int) - len % sizeof(int) );
                event += sizeof(int) - len % sizeof(int);
            }
        }
        set_reply_data_ptr( data, size );
    }

    LIST_FOR_EACH_ENTRY_SAFE( record, next, &events, struct change_record, entry )
    {
        list_remove( &record->entry );
        free( record );
    }
}
