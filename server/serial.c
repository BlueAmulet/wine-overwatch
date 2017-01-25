/*
 * Server-side serial port communications management
 *
 * Copyright (C) 1998 Alexandre Julliard
 * Copyright (C) 2000,2001 Mike McCormack
 *
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
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#ifdef HAVE_UTIME_H
#include <utime.h>
#endif
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
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

static void serial_dump( struct object *obj, int verbose );
static struct fd *serial_get_fd( struct object *obj );
static void serial_destroy(struct object *obj);

static enum server_fd_type serial_get_fd_type( struct fd *fd );
static void serial_queue_async( struct fd *fd, const async_data_t *data, int type, int count );
static void serial_reselect_async( struct fd *fd, struct async_queue *queue );

struct serial
{
    struct object       obj;
    struct fd          *fd;

    struct timeout_user *read_timer;

    /* timeout values */
    unsigned int        readinterval;
    unsigned int        readconst;
    unsigned int        readmult;
    unsigned int        writeconst;
    unsigned int        writemult;

    unsigned int        eventmask;
    unsigned int        generation; /* event mask change counter */
    unsigned int        pending_write : 1;
    unsigned int        pending_wait  : 1;

    struct termios      original;

    /* FIXME: add dcb, comm status, handler module, sharing */
};

static const struct object_ops serial_ops =
{
    sizeof(struct serial),        /* size */
    serial_dump,                  /* dump */
    no_get_type,                  /* get_type */
    add_queue,                    /* add_queue */
    remove_queue,                 /* remove_queue */
    default_fd_signaled,          /* signaled */
    no_satisfied,                 /* satisfied */
    no_signal,                    /* signal */
    serial_get_fd,                /* get_fd */
    default_fd_map_access,        /* map_access */
    default_get_sd,               /* get_sd */
    default_set_sd,               /* set_sd */
    no_lookup_name,               /* lookup_name */
    no_link_name,                 /* link_name */
    NULL,                         /* unlink_name */
    no_open_file,                 /* open_file */
    no_alloc_handle,              /* alloc_handle */
    fd_close_handle,              /* close_handle */
    serial_destroy                /* destroy */
};

static const struct fd_ops serial_fd_ops =
{
    default_fd_get_poll_events,   /* get_poll_events */
    default_poll_event,           /* poll_event */
    serial_get_fd_type,           /* get_fd_type */
    no_fd_read,                   /* read */
    no_fd_write,                  /* write */
    no_fd_flush,                  /* flush */
    default_fd_ioctl,             /* ioctl */
    serial_queue_async,           /* queue_async */
    serial_reselect_async         /* reselect_async */
};

/* check if the given fd is a serial port */
int is_serial_fd( struct fd *fd )
{
    struct termios tios;

    return !tcgetattr( get_unix_fd(fd), &tios );
}

/* create a serial object for a given fd */
struct object *create_serial( struct fd *fd )
{
    struct serial *serial;

    if (!(serial = alloc_object( &serial_ops ))) return NULL;

    serial->read_timer   = NULL;
    serial->readinterval = 0;
    serial->readmult     = 0;
    serial->readconst    = 0;
    serial->writemult    = 0;
    serial->writeconst   = 0;
    serial->eventmask    = 0;
    serial->generation   = 0;
    serial->pending_write = 0;
    serial->pending_wait = 0;
    serial->fd = (struct fd *)grab_object( fd );
    set_fd_user( fd, &serial_fd_ops, &serial->obj );
    return &serial->obj;
}

static struct fd *serial_get_fd( struct object *obj )
{
    struct serial *serial = (struct serial *)obj;
    return (struct fd *)grab_object( serial->fd );
}

static void serial_destroy( struct object *obj)
{
    struct serial *serial = (struct serial *)obj;
    if (serial->read_timer) remove_timeout_user( serial->read_timer );
    release_object( serial->fd );
}

static void serial_dump( struct object *obj, int verbose )
{
    struct serial *serial = (struct serial *)obj;
    assert( obj->ops == &serial_ops );
    fprintf( stderr, "Port fd=%p mask=%x\n", serial->fd, serial->eventmask );
}

static struct serial *get_serial_obj( struct process *process, obj_handle_t handle, unsigned int access )
{
    return (struct serial *)get_handle_obj( process, handle, access, &serial_ops );
}

static enum server_fd_type serial_get_fd_type( struct fd *fd )
{
    return FD_TYPE_SERIAL;
}

static void serial_queue_async( struct fd *fd, const async_data_t *data, int type, int count )
{
    struct serial *serial = get_fd_user( fd );
    timeout_t timeout = 0;
    struct async *async;

    assert(serial->obj.ops == &serial_ops);

    switch (type)
    {
    case ASYNC_TYPE_READ:
        timeout = serial->readconst + (timeout_t)serial->readmult*count;
        break;
    case ASYNC_TYPE_WRITE:
        timeout = serial->writeconst + (timeout_t)serial->writemult*count;
        break;
    }

    if ((async = fd_queue_async( fd, data, NULL, type )))
    {
        if (timeout) async_set_timeout( async, timeout * -10000, STATUS_TIMEOUT );
        release_object( async );
        set_error( STATUS_PENDING );
    }
}

static void serial_read_timeout( void *arg )
{
    struct serial *serial = arg;

    serial->read_timer = NULL;
    fd_async_wake_up( serial->fd, ASYNC_TYPE_READ, STATUS_TIMEOUT );
}

static void serial_reselect_async( struct fd *fd, struct async_queue *queue )
{
    struct serial *serial = get_fd_user( fd );

    if (serial->read_timer)
    {
        if (!(default_fd_get_poll_events( fd ) & POLLIN))
        {
            remove_timeout_user( serial->read_timer );
            serial->read_timer = NULL;
        }
    }
    else if (serial->readinterval && (default_fd_get_poll_events( fd ) & POLLIN))
    {
        serial->read_timer = add_timeout_user( (timeout_t)serial->readinterval * -10000,
                                               serial_read_timeout, serial );
    }
    default_fd_reselect_async( fd, queue );
}

DECL_HANDLER(get_serial_info)
{
    struct serial *serial;

    if ((serial = get_serial_obj( current->process, req->handle, 0 )))
    {
        if (req->flags & SERIALINFO_PENDING_WAIT)
        {
            if (serial->pending_wait)
            {
                release_object( serial );
                set_error( STATUS_INVALID_PARAMETER );
                return;
            }
            serial->pending_wait = 1;
        }

        /* timeouts */
        reply->readinterval = serial->readinterval;
        reply->readconst    = serial->readconst;
        reply->readmult     = serial->readmult;
        reply->writeconst   = serial->writeconst;
        reply->writemult    = serial->writemult;

        /* event mask */
        reply->eventmask    = serial->eventmask;
        reply->cookie       = serial->generation;

        /* pending write */
        reply->pending_write = serial->pending_write;
        if (req->flags & SERIALINFO_PENDING_WRITE)
            serial->pending_write = 0;

        release_object( serial );
    }
}

DECL_HANDLER(set_serial_info)
{
    struct serial *serial;

    if ((serial = get_serial_obj( current->process, req->handle, 0 )))
    {
        if (req->flags & SERIALINFO_PENDING_WAIT)
        {
            if (!serial->pending_wait)
            {
                release_object( serial );
                set_error( STATUS_INVALID_PARAMETER );
                return;
            }
            serial->pending_wait = 0;
        }

        /* timeouts */
        if (req->flags & SERIALINFO_SET_TIMEOUTS)
        {
            serial->readinterval = req->readinterval;
            serial->readconst    = req->readconst;
            serial->readmult     = req->readmult;
            serial->writeconst   = req->writeconst;
            serial->writemult    = req->writemult;
        }

        /* pending write */
        if (req->flags & SERIALINFO_PENDING_WRITE)
            serial->pending_write = 1;

        /* event mask */
        if (req->flags & SERIALINFO_SET_MASK)
        {
            serial->eventmask = req->eventmask;
            serial->generation++;
            fd_async_wake_up( serial->fd, ASYNC_TYPE_WAIT, STATUS_SUCCESS );
        }

        release_object( serial );
    }
}
