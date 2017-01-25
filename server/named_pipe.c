/*
 * Server-side pipe management
 *
 * Copyright (C) 1998 Alexandre Julliard
 * Copyright (C) 2001 Mike McCormack
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
 * TODO:
 *   message mode
 */

#include "config.h"
#include "wine/port.h"

#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#include <time.h>
#include <unistd.h>
#ifdef HAVE_POLL_H
#include <poll.h>
#endif

#ifndef SO_PEEK_OFF
#define SO_PEEK_OFF 42
#endif

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "winioctl.h"

#include "file.h"
#include "sock.h"
#include "handle.h"
#include "thread.h"
#include "request.h"
#include "security.h"

enum pipe_state
{
    ps_idle_server,
    ps_wait_open,
    ps_connected_server,
    ps_wait_disconnect,
    ps_wait_connect
};

struct named_pipe;

struct pipe_server
{
    struct object        obj;        /* object header */
    struct fd           *fd;         /* pipe file descriptor */
    struct fd           *ioctl_fd;   /* file descriptor for ioctls when not connected */
    struct list          entry;      /* entry in named pipe servers list */
    enum pipe_state      state;      /* server state */
    struct pipe_client  *client;     /* client that this server is connected to */
    struct named_pipe   *pipe;
    struct timeout_user *flush_poll;
    unsigned int         options;    /* pipe options */
    unsigned int         pipe_flags;
};

struct pipe_client
{
    struct object        obj;        /* object header */
    struct fd           *fd;         /* pipe file descriptor */
    struct pipe_server  *server;     /* server that this client is connected to */
    unsigned int         flags;      /* file flags */
    unsigned int         pipe_flags;
};

struct named_pipe
{
    struct object       obj;         /* object header */
    unsigned int        flags;
    unsigned int        sharing;
    unsigned int        maxinstances;
    unsigned int        outsize;
    unsigned int        insize;
    unsigned int        instances;
    timeout_t           timeout;
    struct list         servers;     /* list of servers using this pipe */
    struct async_queue *waiters;     /* list of clients waiting to connect */
};

struct named_pipe_device
{
    struct object       obj;         /* object header */
    struct fd          *fd;          /* pseudo-fd for ioctls */
    struct namespace   *pipes;       /* named pipe namespace */
};

static void named_pipe_dump( struct object *obj, int verbose );
static unsigned int named_pipe_map_access( struct object *obj, unsigned int access );
static int named_pipe_link_name( struct object *obj, struct object_name *name, struct object *parent );
static struct object *named_pipe_open_file( struct object *obj, unsigned int access,
                                            unsigned int sharing, unsigned int options );
static void named_pipe_destroy( struct object *obj );

static const struct object_ops named_pipe_ops =
{
    sizeof(struct named_pipe),    /* size */
    named_pipe_dump,              /* dump */
    no_get_type,                  /* get_type */
    no_add_queue,                 /* add_queue */
    NULL,                         /* remove_queue */
    NULL,                         /* signaled */
    NULL,                         /* satisfied */
    no_signal,                    /* signal */
    no_get_fd,                    /* get_fd */
    named_pipe_map_access,        /* map_access */
    default_get_sd,               /* get_sd */
    default_set_sd,               /* set_sd */
    no_lookup_name,               /* lookup_name */
    named_pipe_link_name,         /* link_name */
    default_unlink_name,          /* unlink_name */
    named_pipe_open_file,         /* open_file */
    no_alloc_handle,              /* alloc_handle */
    no_close_handle,              /* close_handle */
    named_pipe_destroy            /* destroy */
};

/* server end functions */
static void pipe_server_dump( struct object *obj, int verbose );
static struct fd *pipe_server_get_fd( struct object *obj );
static int pipe_server_link_name( struct object *obj, struct object_name *name, struct object *parent );
static void pipe_server_unlink_name( struct object *obj, struct object_name *name );
static int pipe_server_close_handle( struct object *obj, struct process *process, obj_handle_t handle );
static void pipe_server_destroy( struct object *obj);
static obj_handle_t pipe_server_flush( struct fd *fd, const async_data_t *async, int blocking );
static enum server_fd_type pipe_server_get_fd_type( struct fd *fd );
static obj_handle_t pipe_server_ioctl( struct fd *fd, ioctl_code_t code, const async_data_t *async,
                                       int blocking );
static void pipe_server_queue_async( struct fd *fd, const async_data_t *data, int type, int count );

static const struct object_ops pipe_server_ops =
{
    sizeof(struct pipe_server),   /* size */
    pipe_server_dump,             /* dump */
    no_get_type,                  /* get_type */
    add_queue,                    /* add_queue */
    remove_queue,                 /* remove_queue */
    default_fd_signaled,          /* signaled */
    no_satisfied,                 /* satisfied */
    no_signal,                    /* signal */
    pipe_server_get_fd,           /* get_fd */
    default_fd_map_access,        /* map_access */
    default_get_sd,               /* get_sd */
    default_set_sd,               /* set_sd */
    no_lookup_name,               /* lookup_name */
    pipe_server_link_name,        /* link_name */
    pipe_server_unlink_name,      /* unlink_name */
    no_open_file,                 /* open_file */
    no_alloc_handle,              /* alloc_handle */
    pipe_server_close_handle,     /* close_handle */
    pipe_server_destroy           /* destroy */
};

static const struct fd_ops pipe_server_fd_ops =
{
    default_fd_get_poll_events,   /* get_poll_events */
    default_poll_event,           /* poll_event */
    pipe_server_get_fd_type,      /* get_fd_type */
    no_fd_read,                   /* read */
    no_fd_write,                  /* write */
    pipe_server_flush,            /* flush */
    pipe_server_ioctl,            /* ioctl */
    pipe_server_queue_async,      /* queue_async */
    default_fd_reselect_async    /* reselect_async */
};

/* client end functions */
static void pipe_client_dump( struct object *obj, int verbose );
static int pipe_client_signaled( struct object *obj, struct wait_queue_entry *entry );
static struct fd *pipe_client_get_fd( struct object *obj );
static int pipe_client_link_name( struct object *obj, struct object_name *name, struct object *parent );
static void pipe_client_unlink_name( struct object *obj, struct object_name *name );
static int pipe_client_close_handle( struct object *obj, struct process *process, obj_handle_t handle );
static void pipe_client_destroy( struct object *obj );
static obj_handle_t pipe_client_flush( struct fd *fd, const async_data_t *async, int blocking );
static enum server_fd_type pipe_client_get_fd_type( struct fd *fd );
static void pipe_client_queue_async( struct fd *fd, const async_data_t *data, int type, int count );

static const struct object_ops pipe_client_ops =
{
    sizeof(struct pipe_client),   /* size */
    pipe_client_dump,             /* dump */
    no_get_type,                  /* get_type */
    add_queue,                    /* add_queue */
    remove_queue,                 /* remove_queue */
    pipe_client_signaled,         /* signaled */
    no_satisfied,                 /* satisfied */
    no_signal,                    /* signal */
    pipe_client_get_fd,           /* get_fd */
    default_fd_map_access,        /* map_access */
    default_get_sd,               /* get_sd */
    default_set_sd,               /* set_sd */
    no_lookup_name,               /* lookup_name */
    pipe_client_link_name,        /* link_name */
    pipe_client_unlink_name,      /* unlink_name */
    no_open_file,                 /* open_file */
    no_alloc_handle,              /* alloc_handle */
    pipe_client_close_handle,     /* close_handle */
    pipe_client_destroy           /* destroy */
};

static const struct fd_ops pipe_client_fd_ops =
{
    default_fd_get_poll_events,   /* get_poll_events */
    default_poll_event,           /* poll_event */
    pipe_client_get_fd_type,      /* get_fd_type */
    no_fd_read,                   /* read */
    no_fd_write,                  /* write */
    pipe_client_flush,            /* flush */
    default_fd_ioctl,             /* ioctl */
    pipe_client_queue_async,      /* queue_async */
    default_fd_reselect_async     /* reselect_async */
};

static void named_pipe_device_dump( struct object *obj, int verbose );
static struct object_type *named_pipe_device_get_type( struct object *obj );
static struct fd *named_pipe_device_get_fd( struct object *obj );
static struct object *named_pipe_device_lookup_name( struct object *obj,
    struct unicode_str *name, unsigned int attr );
static struct object *named_pipe_device_open_file( struct object *obj, unsigned int access,
                                                   unsigned int sharing, unsigned int options );
static void named_pipe_device_destroy( struct object *obj );
static enum server_fd_type named_pipe_device_get_fd_type( struct fd *fd );
static obj_handle_t named_pipe_device_ioctl( struct fd *fd, ioctl_code_t code,
                                             const async_data_t *async_data, int blocking );

static const struct object_ops named_pipe_device_ops =
{
    sizeof(struct named_pipe_device), /* size */
    named_pipe_device_dump,           /* dump */
    named_pipe_device_get_type,       /* get_type */
    no_add_queue,                     /* add_queue */
    NULL,                             /* remove_queue */
    NULL,                             /* signaled */
    no_satisfied,                     /* satisfied */
    no_signal,                        /* signal */
    named_pipe_device_get_fd,         /* get_fd */
    no_map_access,                    /* map_access */
    default_get_sd,                   /* get_sd */
    default_set_sd,                   /* set_sd */
    named_pipe_device_lookup_name,    /* lookup_name */
    directory_link_name,              /* link_name */
    default_unlink_name,              /* unlink_name */
    named_pipe_device_open_file,      /* open_file */
    no_alloc_handle,                  /* alloc_handle */
    fd_close_handle,                  /* close_handle */
    named_pipe_device_destroy         /* destroy */
};

static const struct fd_ops named_pipe_device_fd_ops =
{
    default_fd_get_poll_events,       /* get_poll_events */
    default_poll_event,               /* poll_event */
    named_pipe_device_get_fd_type,    /* get_fd_type */
    no_fd_read,                       /* read */
    no_fd_write,                      /* write */
    no_fd_flush,                      /* flush */
    named_pipe_device_ioctl,          /* ioctl */
    default_fd_queue_async,           /* queue_async */
    default_fd_reselect_async         /* reselect_async */
};

static inline int messagemode_flags( int flags );

static void named_pipe_dump( struct object *obj, int verbose )
{
    fputs( "Named pipe\n", stderr );
}

static unsigned int named_pipe_map_access( struct object *obj, unsigned int access )
{
    if (access & GENERIC_READ)    access |= STANDARD_RIGHTS_READ;
    if (access & GENERIC_WRITE)   access |= STANDARD_RIGHTS_WRITE | FILE_CREATE_PIPE_INSTANCE;
    if (access & GENERIC_EXECUTE) access |= STANDARD_RIGHTS_EXECUTE;
    if (access & GENERIC_ALL)     access |= STANDARD_RIGHTS_ALL;
    return access & ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
}

static void pipe_server_dump( struct object *obj, int verbose )
{
    struct pipe_server *server = (struct pipe_server *) obj;
    assert( obj->ops == &pipe_server_ops );
    fprintf( stderr, "Named pipe server pipe=%p state=%d\n", server->pipe, server->state );
}

static void pipe_client_dump( struct object *obj, int verbose )
{
    struct pipe_client *client = (struct pipe_client *) obj;
    assert( obj->ops == &pipe_client_ops );
    fprintf( stderr, "Named pipe client server=%p\n", client->server );
}

static int pipe_client_signaled( struct object *obj, struct wait_queue_entry *entry )
{
    struct pipe_client *client = (struct pipe_client *) obj;

    return client->fd && is_fd_signaled(client->fd);
}

static void named_pipe_destroy( struct object *obj)
{
    struct named_pipe *pipe = (struct named_pipe *) obj;

    assert( list_empty( &pipe->servers ) );
    assert( !pipe->instances );
    free_async_queue( pipe->waiters );
}

static struct fd *pipe_client_get_fd( struct object *obj )
{
    struct pipe_client *client = (struct pipe_client *) obj;
    if (client->fd)
        return (struct fd *) grab_object( client->fd );
    set_error( STATUS_PIPE_DISCONNECTED );
    return NULL;
}

static void set_server_state( struct pipe_server *server, enum pipe_state state )
{
    server->state = state;

    switch(state)
    {
    case ps_connected_server:
    case ps_wait_disconnect:
        assert( server->fd );
        break;
    case ps_wait_open:
    case ps_idle_server:
        assert( !server->fd );
        set_no_fd_status( server->ioctl_fd, STATUS_PIPE_LISTENING );
        break;
    case ps_wait_connect:
        assert( !server->fd );
        set_no_fd_status( server->ioctl_fd, STATUS_PIPE_DISCONNECTED );
        break;
    }
}

static struct fd *pipe_server_get_fd( struct object *obj )
{
    struct pipe_server *server = (struct pipe_server *) obj;

    return (struct fd *)grab_object( server->fd ? server->fd : server->ioctl_fd );
}


static void notify_empty( struct pipe_server *server )
{
    if (!server->flush_poll)
        return;
    assert( server->state == ps_connected_server );
    remove_timeout_user( server->flush_poll );
    server->flush_poll = NULL;
    fd_async_wake_up( server->fd, ASYNC_TYPE_WAIT, STATUS_SUCCESS );
}

static void do_disconnect( struct pipe_server *server )
{
    /* we may only have a server fd, if the client disconnected */
    if (server->client)
    {
        assert( server->client->server == server );
        assert( server->client->fd );
        release_object( server->client->fd );
        server->client->fd = NULL;
    }
    assert( server->fd );
    shutdown( get_unix_fd( server->fd ), SHUT_RDWR );
    release_object( server->fd );
    server->fd = NULL;
}

static int pipe_server_link_name( struct object *obj, struct object_name *name, struct object *parent )
{
    assert( parent->ops == &named_pipe_ops );
    name->parent = grab_object( parent );
    return 1;
}

static void pipe_server_unlink_name( struct object *obj, struct object_name *name )
{
}

static int pipe_server_close_handle( struct object *obj, struct process *process, obj_handle_t handle )
{
#ifdef __linux__
    struct pipe_server *server = (struct pipe_server *)obj;
    struct pipe_client *client = server->client;
    int unix_fd;

    assert( obj->ops == &pipe_server_ops );
    if (obj->handle_count == 1 && client && client->fd && (unix_fd = get_unix_fd( client->fd )) != -1)
    {
        /* set the NAMED_PIPE_CLOSED_HANDLE flag, to distinguish disconnect / closing pipe */
        fcntl( unix_fd, F_SETSIG, messagemode_flags( client->pipe_flags ) | NAMED_PIPE_CLOSED_HANDLE );
    }
#endif
    return 1;
}

static void pipe_server_destroy( struct object *obj)
{
    struct pipe_server *server = (struct pipe_server *)obj;

    assert( obj->ops == &pipe_server_ops );

    if (server->fd)
    {
        notify_empty( server );
        do_disconnect( server );
    }

    if (server->client)
    {
        server->client->server = NULL;
        server->client = NULL;
    }

    assert( server->pipe->instances );
    server->pipe->instances--;

    if (server->ioctl_fd) release_object( server->ioctl_fd );
    list_remove( &server->entry );
    release_object( server->pipe );
}

static int pipe_client_link_name( struct object *obj, struct object_name *name, struct object *parent )
{
    assert( parent->ops == &named_pipe_ops );
    name->parent = grab_object( parent );
    return 1;
}

static void pipe_client_unlink_name( struct object *obj, struct object_name *name )
{
}

static int pipe_client_close_handle( struct object *obj, struct process *process, obj_handle_t handle )
{
#ifdef __linux__
    struct pipe_client *client = (struct pipe_client *)obj;
    struct pipe_server *server = client->server;
    int unix_fd;

    assert( obj->ops == &pipe_client_ops );
    if (obj->handle_count == 1 && server && server->fd &&
        server->state != ps_wait_connect && (unix_fd = get_unix_fd( server->fd )) != -1)
    {
        /* set the NAMED_PIPE_CLOSED_HANDLE flag, to distinguish disconnect / closing pipe */
        fcntl( unix_fd, F_SETSIG, messagemode_flags( server->pipe_flags ) | NAMED_PIPE_CLOSED_HANDLE );
    }
#endif
    return 1;
}

static void pipe_client_destroy( struct object *obj)
{
    struct pipe_client *client = (struct pipe_client *)obj;
    struct pipe_server *server = client->server;

    assert( obj->ops == &pipe_client_ops );

    if (server)
    {
        notify_empty( server );

        switch(server->state)
        {
        case ps_connected_server:
            /* Don't destroy the server's fd here as we can't
               do a successful flush without it. */
            set_server_state( server, ps_wait_disconnect );
            break;
        case ps_idle_server:
        case ps_wait_open:
        case ps_wait_disconnect:
        case ps_wait_connect:
            assert( 0 );
        }
        assert( server->client );
        server->client = NULL;
        client->server = NULL;
    }
    if (client->fd) release_object( client->fd );
}

static void named_pipe_device_dump( struct object *obj, int verbose )
{
    fputs( "Named pipe device\n", stderr );
}

static struct object_type *named_pipe_device_get_type( struct object *obj )
{
    static const WCHAR name[] = {'D','e','v','i','c','e'};
    static const struct unicode_str str = { name, sizeof(name) };
    return get_object_type( &str );
}

static struct fd *named_pipe_device_get_fd( struct object *obj )
{
    struct named_pipe_device *device = (struct named_pipe_device *)obj;
    return (struct fd *)grab_object( device->fd );
}

static struct object *named_pipe_device_lookup_name( struct object *obj, struct unicode_str *name,
                                                     unsigned int attr )
{
    struct named_pipe_device *device = (struct named_pipe_device*)obj;
    struct object *found;

    assert( obj->ops == &named_pipe_device_ops );
    assert( device->pipes );

    if (!name) return NULL;  /* open the device itself */

    if ((found = find_object( device->pipes, name, attr | OBJ_CASE_INSENSITIVE )))
        name->len = 0;

    return found;
}

static struct object *named_pipe_device_open_file( struct object *obj, unsigned int access,
                                                   unsigned int sharing, unsigned int options )
{
    return grab_object( obj );
}

static void named_pipe_device_destroy( struct object *obj )
{
    struct named_pipe_device *device = (struct named_pipe_device*)obj;
    assert( obj->ops == &named_pipe_device_ops );
    if (device->fd) release_object( device->fd );
    free( device->pipes );
}

static enum server_fd_type named_pipe_device_get_fd_type( struct fd *fd )
{
    return FD_TYPE_DEVICE;
}

struct object *create_named_pipe_device( struct object *root, const struct unicode_str *name )
{
    struct named_pipe_device *dev;

    if ((dev = create_named_object( root, &named_pipe_device_ops, name, 0, NULL )) &&
        get_error() != STATUS_OBJECT_NAME_EXISTS)
    {
        dev->pipes = NULL;
        if (!(dev->fd = alloc_pseudo_fd( &named_pipe_device_fd_ops, &dev->obj, 0 )) ||
            !(dev->pipes = create_namespace( 7 )))
        {
            release_object( dev );
            dev = NULL;
        }
    }
    return &dev->obj;
}

static int pipe_data_remaining( struct pipe_server *server )
{
    struct pollfd pfd;
    int fd;

    assert( server->client );

    fd = get_unix_fd( server->client->fd );
    if (fd < 0)
        return 0;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    if (0 > poll( &pfd, 1, 0 ))
        return 0;
 
    return pfd.revents&POLLIN;
}

static void check_flushed( void *arg )
{
    struct pipe_server *server = (struct pipe_server*) arg;

    if (pipe_data_remaining( server ))
    {
        server->flush_poll = add_timeout_user( -TICKS_PER_SEC / 10, check_flushed, server );
    }
    else
    {
        server->flush_poll = NULL;
        fd_async_wake_up( server->fd, ASYNC_TYPE_WAIT, STATUS_SUCCESS );
    }
}

static obj_handle_t pipe_server_flush( struct fd *fd, const async_data_t *async_data, int blocking )
{
    struct pipe_server *server = get_fd_user( fd );
    obj_handle_t handle = 0;
    struct async *async;

    if (!server || server->state != ps_connected_server) return 0;

    if (!pipe_data_remaining( server )) return 0;

    if ((async = fd_queue_async( server->fd, async_data, NULL, ASYNC_TYPE_WAIT )))
    {
        /* there's no unix way to be alerted when a pipe becomes empty, so resort to polling */
        if (!server->flush_poll)
            server->flush_poll = add_timeout_user( -TICKS_PER_SEC / 10, check_flushed, server );
        if (blocking) handle = alloc_handle( current->process, async, SYNCHRONIZE, 0 );
        release_object( async );
        set_error( STATUS_PENDING );
    }
    return handle;
}

static obj_handle_t pipe_client_flush( struct fd *fd, const async_data_t *async, int blocking )
{
    /* FIXME: what do we have to do for this? */
    return 0;
}

static void pipe_client_queue_async( struct fd *fd, const async_data_t *data, int type, int count )
{
    struct pipe_client *client = get_fd_user( fd );
    struct pipe_server *server = client->server;

    if (!server || !server->fd)
    {
        set_error( STATUS_PIPE_BROKEN );
        return;
    }

    default_fd_queue_async( fd, data, type, count );
}

static inline int is_overlapped( unsigned int options )
{
    return !(options & (FILE_SYNCHRONOUS_IO_ALERT | FILE_SYNCHRONOUS_IO_NONALERT));
}

static enum server_fd_type pipe_server_get_fd_type( struct fd *fd )
{
    return FD_TYPE_PIPE;
}

static enum server_fd_type pipe_client_get_fd_type( struct fd *fd )
{
    return FD_TYPE_PIPE;
}

static obj_handle_t pipe_server_ioctl( struct fd *fd, ioctl_code_t code, const async_data_t *async_data,
                                       int blocking )
{
    struct pipe_server *server = get_fd_user( fd );
    struct async *async;
    obj_handle_t wait_handle = 0;

    switch(code)
    {
    case FSCTL_PIPE_LISTEN:
        switch(server->state)
        {
        case ps_idle_server:
        case ps_wait_connect:
            if ((async = fd_queue_async( server->ioctl_fd, async_data, NULL, ASYNC_TYPE_WAIT )))
            {
                if (blocking) wait_handle = alloc_handle( current->process, async, SYNCHRONIZE, 0 );
                set_server_state( server, ps_wait_open );
                if (server->pipe->waiters) async_wake_up( server->pipe->waiters, STATUS_SUCCESS );
                release_object( async );
                set_error( STATUS_PENDING );
                return wait_handle;
            }
            break;
        case ps_connected_server:
            set_error( STATUS_PIPE_CONNECTED );
            break;
        case ps_wait_disconnect:
            set_error( STATUS_NO_DATA_DETECTED );
            break;
        case ps_wait_open:
            set_error( STATUS_INVALID_HANDLE );
            break;
        }
        return 0;

    case FSCTL_PIPE_DISCONNECT:
        switch(server->state)
        {
        case ps_connected_server:
            assert( server->client );
            assert( server->client->fd );

            notify_empty( server );

            /* dump the client and server fds - client loses all waiting data */
            do_disconnect( server );
            server->client->server = NULL;
            server->client = NULL;
            set_server_state( server, ps_wait_connect );
            break;
        case ps_wait_disconnect:
            assert( !server->client );
            do_disconnect( server );
            set_server_state( server, ps_wait_connect );
            break;
        case ps_idle_server:
        case ps_wait_open:
            set_error( STATUS_PIPE_LISTENING );
            break;
        case ps_wait_connect:
            set_error( STATUS_PIPE_DISCONNECTED );
            break;
        }
        return 0;

    default:
        return default_fd_ioctl( fd, code, async_data, blocking );
    }
}

static void pipe_server_queue_async( struct fd *fd, const async_data_t *data, int type, int count )
{
    struct pipe_server *server = get_fd_user( fd );
    struct pipe_client *client = server->client;

    if (!client || !client->fd)
    {
        set_error( STATUS_PIPE_BROKEN );
        return;
    }

    default_fd_queue_async( fd, data, type, count );
}

static struct pipe_server *get_pipe_server_obj( struct process *process,
                                obj_handle_t handle, unsigned int access )
{
    struct object *obj;
    obj = get_handle_obj( process, handle, access, &pipe_server_ops );
    return (struct pipe_server *) obj;
}

static struct pipe_server *create_pipe_server( struct named_pipe *pipe, unsigned int options,
                                               unsigned int pipe_flags )
{
    static const struct unicode_str str = { NULL, 0 };
    struct pipe_server *server;

    server = create_object( &pipe->obj, &pipe_server_ops, &str, NULL );
    if (!server)
        return NULL;

    server->fd = NULL;
    server->pipe = pipe;
    server->client = NULL;
    server->flush_poll = NULL;
    server->options = options;
    server->pipe_flags = pipe_flags;

    list_add_head( &pipe->servers, &server->entry );
    grab_object( pipe );
    if (!(server->ioctl_fd = alloc_pseudo_fd( &pipe_server_fd_ops, &server->obj, options )))
    {
        release_object( server );
        return NULL;
    }
    set_fd_signaled( server->ioctl_fd, 1 );
    set_server_state( server, ps_idle_server );
    return server;
}

static struct pipe_client *create_pipe_client( struct named_pipe *pipe, unsigned int flags,
                                               unsigned int pipe_flags )
{
    static const struct unicode_str str = { NULL, 0 };
    struct pipe_client *client;

    client = create_object( &pipe->obj, &pipe_client_ops, &str, NULL );
    if (!client)
        return NULL;

    client->fd = NULL;
    client->server = NULL;
    client->flags = flags;
    client->pipe_flags = pipe_flags;

    return client;
}

static struct pipe_server *find_available_server( struct named_pipe *pipe )
{
    struct pipe_server *server;

    /* look for pipe servers that are listening */
    LIST_FOR_EACH_ENTRY( server, &pipe->servers, struct pipe_server, entry )
    {
        if (server->state == ps_wait_open)
            return (struct pipe_server *)grab_object( server );
    }

    /* fall back to pipe servers that are idle */
    LIST_FOR_EACH_ENTRY( server, &pipe->servers, struct pipe_server, entry )
    {
        if (server->state == ps_idle_server)
            return (struct pipe_server *)grab_object( server );
    }

    return NULL;
}

static int named_pipe_link_name( struct object *obj, struct object_name *name, struct object *parent )
{
    struct named_pipe_device *dev = (struct named_pipe_device *)parent;

    if (parent->ops != &named_pipe_device_ops)
    {
        set_error( STATUS_OBJECT_NAME_INVALID );
        return 0;
    }
    namespace_add( dev->pipes, name );
    name->parent = grab_object( parent );
    return 1;
}

/* check if message mode named pipes are supported */
static int is_messagemode_supported(void)
{
#ifdef __linux__
    static const int zero = 0;
    static int messagemode = -1;
    int fd;

    if (messagemode < 0)
    {
        fd = socket( PF_UNIX, SOCK_SEQPACKET, 0 );
        messagemode = (fd != -1) && (setsockopt( fd, SOL_SOCKET, SO_PEEK_OFF, &zero, sizeof(zero) ) != -1);
        if (fd != -1) close( fd );
    }

    return messagemode;
#else
    return 0;
#endif
}

static inline int messagemode_flags( int flags )
{
    if (!is_messagemode_supported())
        flags &= ~(NAMED_PIPE_MESSAGE_STREAM_WRITE | NAMED_PIPE_MESSAGE_STREAM_READ);
    return flags;
}

static struct object *named_pipe_open_file( struct object *obj, unsigned int access,
                                            unsigned int sharing, unsigned int options )
{
    static const int zero = 0;
    struct named_pipe *pipe = (struct named_pipe *)obj;
    struct pipe_server *server;
    struct pipe_client *client;
    unsigned int pipe_sharing;
    int fds[2], type;

    if (!(server = find_available_server( pipe )))
    {
        set_error( STATUS_PIPE_NOT_AVAILABLE );
        return NULL;
    }

    pipe_sharing = server->pipe->sharing;
    if (((access & GENERIC_READ) && !(pipe_sharing & FILE_SHARE_READ)) ||
        ((access & GENERIC_WRITE) && !(pipe_sharing & FILE_SHARE_WRITE)))
    {
        set_error( STATUS_ACCESS_DENIED );
        release_object( server );
        return NULL;
    }

    if ((client = create_pipe_client( pipe, options, pipe->flags )))
    {
        type = ((pipe->flags & NAMED_PIPE_MESSAGE_STREAM_WRITE) && is_messagemode_supported()) ?
               SOCK_SEQPACKET : SOCK_STREAM;

        if (!socketpair( PF_UNIX, type, 0, fds ))
        {
            assert( !server->fd );

            /* for performance reasons, only set nonblocking mode when using
             * overlapped I/O. Otherwise, we will be doing too much busy
             * looping */
            if (is_overlapped( options )) fcntl( fds[1], F_SETFL, O_NONBLOCK );
            if (is_overlapped( server->options )) fcntl( fds[0], F_SETFL, O_NONBLOCK );

            /* FIXME: For message mode we don't pay attention to the provided buffer size.
             * Linux pipes cannot dynamically adjust size, so we leave the size to the system
             * instead of using the application provided value. Please note that this will
             * have the effect that the application doesn't block when sending very large
             * messages. */
            if (type != SOCK_SEQPACKET)
            {
                if (pipe->insize)
                {
                    setsockopt( fds[0], SOL_SOCKET, SO_RCVBUF, &pipe->insize, sizeof(pipe->insize) );
                    setsockopt( fds[1], SOL_SOCKET, SO_RCVBUF, &pipe->insize, sizeof(pipe->insize) );
                }
                if (pipe->outsize)
                {
                    setsockopt( fds[0], SOL_SOCKET, SO_SNDBUF, &pipe->outsize, sizeof(pipe->outsize) );
                    setsockopt( fds[1], SOL_SOCKET, SO_SNDBUF, &pipe->outsize, sizeof(pipe->outsize) );
                }
            }

            if (type != SOCK_SEQPACKET || (setsockopt( fds[0], SOL_SOCKET, SO_PEEK_OFF, &zero, sizeof(zero) ) != -1 &&
                                           setsockopt( fds[1], SOL_SOCKET, SO_PEEK_OFF, &zero, sizeof(zero) ) != -1))
            {
            #ifdef __linux__
                fcntl( fds[0], F_SETSIG, messagemode_flags( server->pipe_flags ) );
                fcntl( fds[1], F_SETSIG, messagemode_flags( client->pipe_flags ) );
            #endif

                client->fd = create_anonymous_fd( &pipe_client_fd_ops, fds[1], &client->obj, options );
                server->fd = create_anonymous_fd( &pipe_server_fd_ops, fds[0], &server->obj, server->options );
                if (client->fd && server->fd)
                {
                    allow_fd_caching( client->fd );
                    allow_fd_caching( server->fd );
                    fd_copy_completion( server->ioctl_fd, server->fd );
                    if (server->state == ps_wait_open)
                        fd_async_wake_up( server->ioctl_fd, ASYNC_TYPE_WAIT, STATUS_SUCCESS );
                    set_server_state( server, ps_connected_server );
                    server->client = client;
                    client->server = server;
                }
                else
                {
                    release_object( client );
                    client = NULL;
                }
            }
            else
            {
                sock_set_error();
                release_object( client );
                client = NULL;
            }
        }
        else
        {
            file_set_error();
            release_object( client );
            client = NULL;
        }
    }
    release_object( server );
    return &client->obj;
}

static obj_handle_t named_pipe_device_ioctl( struct fd *fd, ioctl_code_t code,
                                             const async_data_t *async_data, int blocking )
{
    struct named_pipe_device *device = get_fd_user( fd );

    switch(code)
    {
    case FSCTL_PIPE_WAIT:
        {
            const FILE_PIPE_WAIT_FOR_BUFFER *buffer = get_req_data();
            data_size_t size = get_req_data_size();
            obj_handle_t wait_handle = 0;
            struct named_pipe *pipe;
            struct pipe_server *server;
            struct unicode_str name;

            if (size < sizeof(*buffer) ||
                size < FIELD_OFFSET(FILE_PIPE_WAIT_FOR_BUFFER, Name[buffer->NameLength/sizeof(WCHAR)]))
            {
                set_error( STATUS_INVALID_PARAMETER );
                return 0;
            }
            name.str = buffer->Name;
            name.len = (buffer->NameLength / sizeof(WCHAR)) * sizeof(WCHAR);
            if (!(pipe = open_named_object( &device->obj, &named_pipe_ops, &name, 0 ))) return 0;

            if (!(server = find_available_server( pipe )))
            {
                struct async *async;

                if (!pipe->waiters && !(pipe->waiters = create_async_queue( NULL ))) goto done;

                if ((async = create_async( current, pipe->waiters, async_data, NULL )))
                {
                    timeout_t when = buffer->TimeoutSpecified ? buffer->Timeout.QuadPart : pipe->timeout;
                    async_set_timeout( async, when, STATUS_IO_TIMEOUT );
                    if (blocking) wait_handle = alloc_handle( current->process, async, SYNCHRONIZE, 0 );
                    release_object( async );
                    set_error( STATUS_PENDING );
                }
            }
            else release_object( server );

        done:
            release_object( pipe );
            return wait_handle;
        }

    default:
        return default_fd_ioctl( fd, code, async_data, blocking );
    }
}


DECL_HANDLER(create_named_pipe)
{
    struct named_pipe *pipe;
    struct pipe_server *server;
    struct unicode_str name;
    struct object *root;
    const struct security_descriptor *sd;
    const struct object_attributes *objattr = get_req_object_attributes( &sd, &name, &root );

    if (!objattr) return;

    if (!req->sharing || (req->sharing & ~(FILE_SHARE_READ | FILE_SHARE_WRITE)) ||
        (!(req->flags & NAMED_PIPE_MESSAGE_STREAM_WRITE) && (req->flags & NAMED_PIPE_MESSAGE_STREAM_READ)))
    {
        if (root) release_object( root );
        set_error( STATUS_INVALID_PARAMETER );
        return;
    }

    reply->flags = messagemode_flags(req->flags);

    if (!name.len)  /* pipes need a root directory even without a name */
    {
        if (!objattr->rootdir)
        {
            set_error( STATUS_OBJECT_PATH_SYNTAX_BAD );
            return;
        }
        if (!(root = get_directory_obj( current->process, objattr->rootdir ))) return;
    }

    pipe = create_named_object( root, &named_pipe_ops, &name, objattr->attributes | OBJ_OPENIF, NULL );

    if (root) release_object( root );
    if (!pipe) return;

    if (get_error() != STATUS_OBJECT_NAME_EXISTS)
    {
        /* initialize it if it didn't already exist */
        pipe->instances = 0;
        pipe->waiters = NULL;
        list_init( &pipe->servers );
        pipe->insize = req->insize;
        pipe->outsize = req->outsize;
        pipe->maxinstances = req->maxinstances;
        pipe->timeout = req->timeout;
        pipe->flags = req->flags & NAMED_PIPE_MESSAGE_STREAM_WRITE;
        pipe->sharing = req->sharing;
    }
    else
    {
        if (pipe->maxinstances <= pipe->instances)
        {
            set_error( STATUS_INSTANCE_NOT_AVAILABLE );
            release_object( pipe );
            return;
        }
        if (pipe->sharing != req->sharing)
        {
            set_error( STATUS_ACCESS_DENIED );
            release_object( pipe );
            return;
        }
        clear_error(); /* clear the name collision */
    }

    server = create_pipe_server( pipe, req->options, req->flags );
    if (server)
    {
        reply->handle = alloc_handle( current->process, server, req->access, objattr->attributes );
        server->pipe->instances++;
        if (sd) default_set_sd( &server->obj, sd, OWNER_SECURITY_INFORMATION |
                                                  GROUP_SECURITY_INFORMATION |
                                                  DACL_SECURITY_INFORMATION |
                                                  SACL_SECURITY_INFORMATION );
        release_object( server );
    }

    release_object( pipe );
}

DECL_HANDLER(get_named_pipe_info)
{
    struct pipe_server *server;
    struct pipe_client *client = NULL;

    server = get_pipe_server_obj( current->process, req->handle, FILE_READ_ATTRIBUTES );
    if (!server)
    {
        if (get_error() != STATUS_OBJECT_TYPE_MISMATCH)
            return;

        clear_error();
        client = (struct pipe_client *)get_handle_obj( current->process, req->handle,
                                                       0, &pipe_client_ops );
        if (!client) return;
        server = client->server;
    }

    reply->flags        = client ? client->pipe_flags : server->pipe_flags;
    if (server)
    {
        reply->sharing      = server->pipe->sharing;
        reply->maxinstances = server->pipe->maxinstances;
        reply->instances    = server->pipe->instances;
        reply->insize       = server->pipe->insize;
        reply->outsize      = server->pipe->outsize;
    }

    if (client)
        release_object(client);
    else
    {
        reply->flags |= NAMED_PIPE_SERVER_END;
        release_object(server);
    }
}

DECL_HANDLER(set_named_pipe_info)
{
    struct pipe_server *server;
    struct pipe_client *client = NULL;
#ifdef __linux__
    int unix_fd;
#endif

    server = get_pipe_server_obj( current->process, req->handle, FILE_WRITE_ATTRIBUTES );
    if (!server)
    {
        if (get_error() != STATUS_OBJECT_TYPE_MISMATCH)
            return;

        clear_error();
        client = (struct pipe_client *)get_handle_obj( current->process, req->handle,
                                                       0, &pipe_client_ops );
        if (!client) return;
        if (!(server = client->server))
        {
            release_object( client );
            return;
        }
    }

    if ((req->flags & ~(NAMED_PIPE_MESSAGE_STREAM_READ | NAMED_PIPE_NONBLOCKING_MODE)) ||
            ((req->flags & NAMED_PIPE_MESSAGE_STREAM_READ) && !(server->pipe->flags & NAMED_PIPE_MESSAGE_STREAM_WRITE)))
    {
        set_error( STATUS_INVALID_PARAMETER );
    }
    else if (client)
    {
        client->pipe_flags = server->pipe->flags | req->flags;
    #ifdef __linux__
        if (client->fd && (unix_fd = get_unix_fd( client->fd )) != -1)
            fcntl( unix_fd, F_SETSIG, messagemode_flags( client->pipe_flags ) );
        clear_error();
    #endif
    }
    else
    {
        server->pipe_flags = server->pipe->flags | req->flags;
    #ifdef __linux__
        if (server->fd && (unix_fd = get_unix_fd( server->fd )) != -1)
            fcntl( unix_fd, F_SETSIG, messagemode_flags( server->pipe_flags ) );
        clear_error();
    #endif
    }

    if (client)
        release_object(client);
    else
        release_object(server);
}
