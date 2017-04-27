/*
 * Server-side symbolic link object management
 *
 * Copyright (C) 2005 Vitaliy Margolen
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

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "winternl.h"
#include "ddk/wdm.h"

#include "handle.h"
#include "request.h"
#include "object.h"
#include "unicode.h"

struct symlink
{
    struct object    obj;       /* object header */
    WCHAR           *target;    /* target of the symlink */
    data_size_t      len;       /* target len in bytes */
};

static void symlink_dump( struct object *obj, int verbose );
static struct object_type *symlink_get_type( struct object *obj );
static unsigned int symlink_map_access( struct object *obj, unsigned int access );
static struct object *symlink_lookup_name( struct object *obj, struct unicode_str *name,
                                           unsigned int attr );
static void symlink_destroy( struct object *obj );

static const struct object_ops symlink_ops =
{
    sizeof(struct symlink),       /* size */
    symlink_dump,                 /* dump */
    symlink_get_type,             /* get_type */
    no_add_queue,                 /* add_queue */
    NULL,                         /* remove_queue */
    NULL,                         /* signaled */
    NULL,                         /* satisfied */
    no_signal,                    /* signal */
    no_get_fd,                    /* get_fd */
    symlink_map_access,           /* map_access */
    default_get_sd,               /* get_sd */
    default_set_sd,               /* set_sd */
    symlink_lookup_name,          /* lookup_name */
    directory_link_name,          /* link_name */
    default_unlink_name,          /* unlink_name */
    no_open_file,                 /* open_file */
    no_alloc_handle,              /* alloc_handle */
    no_close_handle,              /* close_handle */
    symlink_destroy               /* destroy */
};

static void symlink_dump( struct object *obj, int verbose )
{
    struct symlink *symlink = (struct symlink *)obj;
    assert( obj->ops == &symlink_ops );

    fputs( "Symlink target=\"", stderr );
    dump_strW( symlink->target, symlink->len / sizeof(WCHAR), stderr, "\"\"" );
    fputs( "\"\n", stderr );
}

static struct object_type *symlink_get_type( struct object *obj )
{
    static const WCHAR name[] = {'S','y','m','b','o','l','i','c','L','i','n','k'};
    static const struct unicode_str str = { name, sizeof(name) };
    return get_object_type( &str );
}

static struct object *symlink_lookup_name( struct object *obj, struct unicode_str *name,
                                           unsigned int attr )
{
    struct symlink *symlink = (struct symlink *)obj;
    struct unicode_str target_str, name_left;
    struct object *target;

    assert( obj->ops == &symlink_ops );

    if (!name) return NULL;
    if (!name->len && (attr & OBJ_OPENLINK)) return NULL;

    target_str.str = symlink->target;
    target_str.len = symlink->len;
    if ((target = lookup_named_object( NULL, &target_str, attr, &name_left )))
    {
        if (name_left.len)
        {
            release_object( target );
            target = NULL;
            set_error( STATUS_OBJECT_PATH_NOT_FOUND );
        }
    }
    return target;
}

static unsigned int symlink_map_access( struct object *obj, unsigned int access )
{
    if (access & GENERIC_READ)    access |= STANDARD_RIGHTS_READ | SYMBOLIC_LINK_QUERY;
    if (access & GENERIC_WRITE)   access |= STANDARD_RIGHTS_WRITE;
    if (access & GENERIC_EXECUTE) access |= STANDARD_RIGHTS_EXECUTE;
    if (access & GENERIC_ALL)     access |= SYMBOLIC_LINK_ALL_ACCESS;
    return access & ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
}

static void symlink_destroy( struct object *obj )
{
    struct symlink *symlink = (struct symlink *)obj;
    assert( obj->ops == &symlink_ops );
    free( symlink->target );
}

static struct symlink *create_symlink( struct object *root, const struct unicode_str *name,
                                       unsigned int attr, const struct unicode_str *target,
                                       const struct security_descriptor *sd )
{
    struct symlink *symlink;

    if (!target->len)
    {
        set_error( STATUS_INVALID_PARAMETER );
        return NULL;
    }
    if ((symlink = create_named_object( root, &symlink_ops, name, attr, sd )) &&
        (get_error() != STATUS_OBJECT_NAME_EXISTS))
    {
        if ((symlink->target = memdup( target->str, target->len )))
        {
            symlink->len = target->len;
        }
        else
        {
            release_object( symlink );
            symlink = NULL;
        }
    }
    return symlink;
}

/* create a symlink pointing to an existing object */
struct object *create_obj_symlink( struct object *root, const struct unicode_str *name,
                                    unsigned int attr, struct object *target,
                                    const struct security_descriptor *sd )
{
    struct symlink *symlink;
    data_size_t len;
    WCHAR *target_name;

    if (!(target_name = get_object_full_name( target, &len )))
    {
        set_error( STATUS_INVALID_PARAMETER );
        return NULL;
    }
    if ((symlink = create_named_object( root, &symlink_ops, name, attr, sd )) &&
        (get_error() != STATUS_OBJECT_NAME_EXISTS))
    {
        symlink->target = target_name;
        symlink->len = len;
    }
    else free( target_name );

    return &symlink->obj;
}


/* create a symbolic link object */
DECL_HANDLER(create_symlink)
{
    struct symlink *symlink;
    struct unicode_str name, target;
    struct object *root;
    const struct security_descriptor *sd;
    const struct object_attributes *objattr = get_req_object_attributes( &sd, &name, &root );

    if (!objattr) return;

    target.str = get_req_data_after_objattr( objattr, &target.len );
    target.len = (target.len / sizeof(WCHAR)) * sizeof(WCHAR);

    if ((symlink = create_symlink( root, &name, objattr->attributes, &target, sd )))
    {
        reply->handle = alloc_handle( current->process, symlink, req->access, objattr->attributes );
        release_object( symlink );
    }

    if (root) release_object( root );
}

/* open a symbolic link object */
DECL_HANDLER(open_symlink)
{
    struct unicode_str name = get_req_unicode_str();

    reply->handle = open_object( current->process, req->rootdir, req->access,
                                 &symlink_ops, &name, req->attributes | OBJ_OPENLINK );
}

/* query a symbolic link object */
DECL_HANDLER(query_symlink)
{
    struct symlink *symlink;

    symlink = (struct symlink *)get_handle_obj( current->process, req->handle,
                                                SYMBOLIC_LINK_QUERY, &symlink_ops );
    if (!symlink) return;

    reply->total = symlink->len;
    if (get_reply_max_size() < symlink->len)
        set_error( STATUS_BUFFER_TOO_SMALL );
    else
        set_reply_data( symlink->target, symlink->len );
    release_object( symlink );
}
