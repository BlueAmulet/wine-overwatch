/*
 * Server-side objects
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
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#ifdef HAVE_VALGRIND_MEMCHECK_H
#include <valgrind/memcheck.h>
#endif

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "winternl.h"

#include "file.h"
#include "process.h"
#include "thread.h"
#include "unicode.h"
#include "security.h"


struct namespace
{
    unsigned int        hash_size;       /* size of hash table */
    struct list         names[1];        /* array of hash entry lists */
};


#ifdef DEBUG_OBJECTS
static struct list object_list = LIST_INIT(object_list);
static struct list static_object_list = LIST_INIT(static_object_list);

void dump_objects(void)
{
    struct list *p;

    LIST_FOR_EACH( p, &static_object_list )
    {
        struct object *ptr = LIST_ENTRY( p, struct object, obj_list );
        fprintf( stderr, "%p:%d: ", ptr, ptr->refcount );
        dump_object_name( ptr );
        ptr->ops->dump( ptr, 1 );
    }
    LIST_FOR_EACH( p, &object_list )
    {
        struct object *ptr = LIST_ENTRY( p, struct object, obj_list );
        fprintf( stderr, "%p:%d: ", ptr, ptr->refcount );
        dump_object_name( ptr );
        ptr->ops->dump( ptr, 1 );
    }
}

void close_objects(void)
{
    struct list *ptr;

    /* release the static objects */
    while ((ptr = list_head( &static_object_list )))
    {
        struct object *obj = LIST_ENTRY( ptr, struct object, obj_list );
        /* move it back to the standard list before freeing */
        list_remove( &obj->obj_list );
        list_add_head( &object_list, &obj->obj_list );
        release_object( obj );
    }

    dump_objects();  /* dump any remaining objects */
}

#endif  /* DEBUG_OBJECTS */

/*****************************************************************/

/* mark a block of memory as uninitialized for debugging purposes */
static inline void mark_block_uninitialized( void *ptr, size_t size )
{
    memset( ptr, 0x55, size );
#if defined(VALGRIND_MAKE_MEM_UNDEFINED)
    VALGRIND_DISCARD( VALGRIND_MAKE_MEM_UNDEFINED( ptr, size ));
#elif defined(VALGRIND_MAKE_WRITABLE)
    VALGRIND_DISCARD( VALGRIND_MAKE_WRITABLE( ptr, size ));
#endif
}

/* malloc replacement */
void *mem_alloc( size_t size )
{
    void *ptr = malloc( size );
    if (ptr) mark_block_uninitialized( ptr, size );
    else set_error( STATUS_NO_MEMORY );
    return ptr;
}

/* duplicate a block of memory */
void *memdup( const void *data, size_t len )
{
    void *ptr = malloc( len );
    if (ptr) memcpy( ptr, data, len );
    else set_error( STATUS_NO_MEMORY );
    return ptr;
}


/*****************************************************************/

static int get_name_hash( const struct namespace *namespace, const WCHAR *name, data_size_t len )
{
    WCHAR hash = 0;
    len /= sizeof(WCHAR);
    while (len--) hash ^= tolowerW(*name++);
    return hash % namespace->hash_size;
}

void namespace_add( struct namespace *namespace, struct object_name *ptr )
{
    int hash = get_name_hash( namespace, ptr->name, ptr->len );

    list_add_head( &namespace->names[hash], &ptr->entry );
}

/* allocate a name for an object */
static struct object_name *alloc_name( const struct unicode_str *name )
{
    struct object_name *ptr;

    if ((ptr = mem_alloc( sizeof(*ptr) + name->len - sizeof(ptr->name) )))
    {
        ptr->len = name->len;
        ptr->parent = NULL;
        memcpy( ptr->name, name->str, name->len );
    }
    return ptr;
}

/* get the name of an existing object */
const WCHAR *get_object_name( struct object *obj, data_size_t *len )
{
    struct object_name *ptr = obj->name;
    if (!ptr) return NULL;
    *len = ptr->len;
    return ptr->name;
}

/* get the full path name of an existing object */
WCHAR *get_object_full_name( struct object *obj, data_size_t *ret_len )
{
    static const WCHAR backslash = '\\';
    struct object *ptr = obj;
    data_size_t len = 0;
    char *ret;

    while (ptr && ptr->name)
    {
        struct object_name *name = ptr->name;
        if (name->len) len += name->len + sizeof(WCHAR);
        ptr = name->parent;
    }
    if (!len) return NULL;
    if (!(ret = malloc( len ))) return NULL;

    *ret_len = len;
    while (obj && obj->name)
    {
        struct object_name *name = obj->name;
        if (name->len)
        {
            memcpy( ret + len - name->len, name->name, name->len );
            len -= name->len + sizeof(WCHAR);
            memcpy( ret + len, &backslash, sizeof(WCHAR) );
        }
        obj = name->parent;
    }
    return (WCHAR *)ret;
}

/* allocate and initialize an object */
void *alloc_object( const struct object_ops *ops )
{
    struct object *obj = mem_alloc( ops->size );
    if (obj)
    {
        obj->refcount     = 1;
        obj->handle_count = 0;
        obj->ops          = ops;
        obj->name         = NULL;
        obj->sd           = NULL;
        list_init( &obj->wait_queue );
#ifdef DEBUG_OBJECTS
        list_add_head( &object_list, &obj->obj_list );
#endif
        return obj;
    }
    return NULL;
}

/* free an object once it has been destroyed */
void free_object( struct object *obj )
{
    free( obj->sd );
#ifdef DEBUG_OBJECTS
    list_remove( &obj->obj_list );
    memset( obj, 0xaa, obj->ops->size );
#endif
    free( obj );
}

/* find an object by name starting from the specified root */
/* if it doesn't exist, its parent is returned, and name_left contains the remaining name */
struct object *lookup_named_object( struct object *root, const struct unicode_str *name,
                                    unsigned int attr, struct unicode_str *name_left )
{
    struct object *obj, *parent;
    struct unicode_str name_tmp = *name, *ptr = &name_tmp;

    if (root)
    {
        /* if root is specified path shouldn't start with backslash */
        if (name_tmp.len && name_tmp.str[0] == '\\')
        {
            set_error( STATUS_OBJECT_PATH_SYNTAX_BAD );
            return NULL;
        }
        parent = grab_object( root );
    }
    else
    {
        if (!name_tmp.len || name_tmp.str[0] != '\\')
        {
            set_error( STATUS_OBJECT_PATH_SYNTAX_BAD );
            return NULL;
        }
        /* skip leading backslash */
        name_tmp.str++;
        name_tmp.len -= sizeof(WCHAR);
        parent = get_root_directory();
    }

    if (!name_tmp.len) ptr = NULL;  /* special case for empty path */

    clear_error();

    while ((obj = parent->ops->lookup_name( parent, ptr, attr )))
    {
        /* move to the next element */
        release_object ( parent );
        parent = obj;
    }
    if (get_error())
    {
        release_object( parent );
        return NULL;
    }

    if (name_left) *name_left = name_tmp;
    return parent;
}

void *create_object( struct object *parent, const struct object_ops *ops,
                     const struct unicode_str *name, const struct security_descriptor *sd )
{
    struct object *obj;
    struct object_name *name_ptr;

    if (!(name_ptr = alloc_name( name ))) return NULL;
    if (!(obj = alloc_object( ops ))) goto failed;
    if (sd && !default_set_sd( obj, sd, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                               DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION ))
        goto failed;
    if (!obj->ops->link_name( obj, name_ptr, parent )) goto failed;

    name_ptr->obj = obj;
    obj->name = name_ptr;
    return obj;

failed:
    if (obj) free_object( obj );
    free( name_ptr );
    return NULL;
}

/* create an object as named child under the specified parent */
void *create_named_object( struct object *parent, const struct object_ops *ops,
                           const struct unicode_str *name, unsigned int attributes,
                           const struct security_descriptor *sd )
{
    struct object *obj, *new_obj;
    struct unicode_str new_name;

    clear_error();

    if (!name || !name->len)
    {
        if (!(new_obj = alloc_object( ops ))) return NULL;
        if (sd && !default_set_sd( new_obj, sd, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                                   DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION ))
        {
            free_object( new_obj );
            return NULL;
        }
        return new_obj;
    }

    if (!(obj = lookup_named_object( parent, name, attributes, &new_name ))) return NULL;

    if (!new_name.len)
    {
        if (attributes & OBJ_OPENIF && obj->ops == ops)
            set_error( STATUS_OBJECT_NAME_EXISTS );
        else
        {
            release_object( obj );
            obj = NULL;
            if (attributes & OBJ_OPENIF)
                set_error( STATUS_OBJECT_TYPE_MISMATCH );
            else
                set_error( STATUS_OBJECT_NAME_COLLISION );
        }
        return obj;
    }

    new_obj = create_object( obj, ops, &new_name, sd );
    release_object( obj );
    return new_obj;
}

/* open a object by name under the specified parent */
void *open_named_object( struct object *parent, const struct object_ops *ops,
                         const struct unicode_str *name, unsigned int attributes )
{
    struct unicode_str name_left;
    struct object *obj;

    if ((obj = lookup_named_object( parent, name, attributes, &name_left )))
    {
        if (name_left.len) /* not fully parsed */
            set_error( STATUS_OBJECT_NAME_NOT_FOUND );
        else if (ops && obj->ops != ops)
            set_error( STATUS_OBJECT_TYPE_MISMATCH );
        else
            return obj;

        release_object( obj );
    }
    return NULL;
}

/* recursive helper for dump_object_name */
static void dump_name( struct object *obj )
{
    struct object_name *name = obj->name;

    if (!name) return;
    if (name->parent) dump_name( name->parent );
    if (name->len)
    {
        fputs( "\\\\", stderr );
        dump_strW( name->name, name->len / sizeof(WCHAR), stderr, "[]" );
    }
}

/* dump the name of an object to stderr */
void dump_object_name( struct object *obj )
{
    if (!obj->name) return;
    fputc( '[', stderr );
    dump_name( obj );
    fputs( "] ", stderr );
}

/* unlink a named object from its namespace, without freeing the object itself */
void unlink_named_object( struct object *obj )
{
    struct object_name *name_ptr = obj->name;

    if (!name_ptr) return;
    obj->name = NULL;
    obj->ops->unlink_name( obj, name_ptr );
    if (name_ptr->parent) release_object( name_ptr->parent );
    free( name_ptr );
}

/* mark an object as being stored statically, i.e. only released at shutdown */
void make_object_static( struct object *obj )
{
#ifdef DEBUG_OBJECTS
    list_remove( &obj->obj_list );
    list_add_head( &static_object_list, &obj->obj_list );
#endif
}

/* grab an object (i.e. increment its refcount) and return the object */
struct object *grab_object( void *ptr )
{
    struct object *obj = (struct object *)ptr;
    assert( obj->refcount < INT_MAX );
    obj->refcount++;
    return obj;
}

/* release an object (i.e. decrement its refcount) */
void release_object( void *ptr )
{
    struct object *obj = (struct object *)ptr;
    assert( obj->refcount );
    if (!--obj->refcount)
    {
        assert( !obj->handle_count );
        /* if the refcount is 0, nobody can be in the wait queue */
        assert( list_empty( &obj->wait_queue ));
        unlink_named_object( obj );
        obj->ops->destroy( obj );
        free_object( obj );
    }
}

/* find an object by its name; the refcount is incremented */
struct object *find_object( const struct namespace *namespace, const struct unicode_str *name,
                            unsigned int attributes )
{
    const struct list *list;
    struct list *p;

    if (!name || !name->len) return NULL;

    list = &namespace->names[ get_name_hash( namespace, name->str, name->len ) ];
    LIST_FOR_EACH( p, list )
    {
        const struct object_name *ptr = LIST_ENTRY( p, struct object_name, entry );
        if (ptr->len != name->len) continue;
        if (attributes & OBJ_CASE_INSENSITIVE)
        {
            if (!strncmpiW( ptr->name, name->str, name->len/sizeof(WCHAR) ))
                return grab_object( ptr->obj );
        }
        else
        {
            if (!memcmp( ptr->name, name->str, name->len ))
                return grab_object( ptr->obj );
        }
    }
    return NULL;
}

/* find an object by its index; the refcount is incremented */
struct object *find_object_index( const struct namespace *namespace, unsigned int index )
{
    unsigned int i;

    /* FIXME: not efficient at all */
    for (i = 0; i < namespace->hash_size; i++)
    {
        const struct object_name *ptr;
        LIST_FOR_EACH_ENTRY( ptr, &namespace->names[i], const struct object_name, entry )
        {
            if (!index--) return grab_object( ptr->obj );
        }
    }
    set_error( STATUS_NO_MORE_ENTRIES );
    return NULL;
}

/* allocate a namespace */
struct namespace *create_namespace( unsigned int hash_size )
{
    struct namespace *namespace;
    unsigned int i;

    namespace = mem_alloc( sizeof(*namespace) + (hash_size - 1) * sizeof(namespace->names[0]) );
    if (namespace)
    {
        namespace->hash_size      = hash_size;
        for (i = 0; i < hash_size; i++) list_init( &namespace->names[i] );
    }
    return namespace;
}

/* functions for unimplemented/default object operations */

struct object_type *no_get_type( struct object *obj )
{
    return NULL;
}

int no_add_queue( struct object *obj, struct wait_queue_entry *entry )
{
    set_error( STATUS_OBJECT_TYPE_MISMATCH );
    return 0;
}

void no_satisfied( struct object *obj, struct wait_queue_entry *entry )
{
}

int no_signal( struct object *obj, unsigned int access )
{
    set_error( STATUS_OBJECT_TYPE_MISMATCH );
    return 0;
}

struct fd *no_get_fd( struct object *obj )
{
    set_error( STATUS_OBJECT_TYPE_MISMATCH );
    return NULL;
}

unsigned int no_map_access( struct object *obj, unsigned int access )
{
    if (access & GENERIC_READ)    access |= STANDARD_RIGHTS_READ;
    if (access & GENERIC_WRITE)   access |= STANDARD_RIGHTS_WRITE;
    if (access & GENERIC_EXECUTE) access |= STANDARD_RIGHTS_EXECUTE;
    if (access & GENERIC_ALL)     access |= STANDARD_RIGHTS_ALL;
    return access & ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
}

struct security_descriptor *default_get_sd( struct object *obj )
{
    return obj->sd;
}

struct security_descriptor *set_sd_from_token_internal( const struct security_descriptor *sd,
                                                        const struct security_descriptor *old_sd,
                                                        unsigned int set_info, struct token *token )
{
    struct security_descriptor new_sd, *new_sd_ptr;
    int present;
    const SID *owner = NULL, *group = NULL;
    const ACL *sacl, *dacl;
    ACL *replaced_sacl = NULL;
    char *ptr;

    new_sd.control = sd->control & ~SE_SELF_RELATIVE;

    if (set_info & OWNER_SECURITY_INFORMATION && sd->owner_len)
    {
        owner = sd_get_owner( sd );
        new_sd.owner_len = sd->owner_len;
    }
    else if (old_sd && old_sd->owner_len)
    {
        owner = sd_get_owner( old_sd );
        new_sd.owner_len = old_sd->owner_len;
    }
    else if (token)
    {
        owner = token_get_user( token );
        new_sd.owner_len = security_sid_len( owner );
    }
    else new_sd.owner_len = 0;

    if (set_info & GROUP_SECURITY_INFORMATION && sd->group_len)
    {
        group = sd_get_group( sd );
        new_sd.group_len = sd->group_len;
    }
    else if (old_sd && old_sd->group_len)
    {
        group = sd_get_group( old_sd );
        new_sd.group_len = old_sd->group_len;
    }
    else if (token)
    {
        group = token_get_primary_group( token );
        new_sd.group_len = security_sid_len( group );
    }
    else new_sd.group_len = 0;

    sacl = sd_get_sacl( sd, &present );
    if (set_info & SACL_SECURITY_INFORMATION && present)
    {
        new_sd.control |= SE_SACL_PRESENT;
        new_sd.sacl_len = sd->sacl_len;
    }
    else if (set_info & LABEL_SECURITY_INFORMATION && present)
    {
        const ACL *old_sacl = NULL;
        if (old_sd && old_sd->control & SE_SACL_PRESENT) old_sacl = sd_get_sacl( old_sd, &present );
        if (!(replaced_sacl = replace_security_labels( old_sacl, sacl ))) return NULL;
        new_sd.control |= SE_SACL_PRESENT;
        new_sd.sacl_len = replaced_sacl->AclSize;
        sacl = replaced_sacl;
    }
    else
    {
        if (old_sd) sacl = sd_get_sacl( old_sd, &present );

        if (old_sd && present)
        {
            new_sd.control |= SE_SACL_PRESENT;
            new_sd.sacl_len = old_sd->sacl_len;
        }
        else
            new_sd.sacl_len = 0;
    }

    dacl = sd_get_dacl( sd, &present );
    if (set_info & DACL_SECURITY_INFORMATION && present)
    {
        new_sd.control |= SE_DACL_PRESENT;
        new_sd.dacl_len = sd->dacl_len;
    }
    else
    {
        if (old_sd) dacl = sd_get_dacl( old_sd, &present );

        if (old_sd && present)
        {
            new_sd.control |= SE_DACL_PRESENT;
            new_sd.dacl_len = old_sd->dacl_len;
        }
        else if (token)
        {
            dacl = token_get_default_dacl( token );
            new_sd.control |= SE_DACL_PRESENT;
            new_sd.dacl_len = dacl->AclSize;
        }
        else new_sd.dacl_len = 0;
    }

    ptr = mem_alloc( sizeof(new_sd) + new_sd.owner_len + new_sd.group_len +
                     new_sd.sacl_len + new_sd.dacl_len );
    if (!ptr)
    {
        free( replaced_sacl );
        return NULL;
    }
    new_sd_ptr = (struct security_descriptor*)ptr;

    memcpy( ptr, &new_sd, sizeof(new_sd) );
    ptr += sizeof(new_sd);
    memcpy( ptr, owner, new_sd.owner_len );
    ptr += new_sd.owner_len;
    memcpy( ptr, group, new_sd.group_len );
    ptr += new_sd.group_len;
    memcpy( ptr, sacl, new_sd.sacl_len );
    ptr += new_sd.sacl_len;
    memcpy( ptr, dacl, new_sd.dacl_len );

    free( replaced_sacl );
    return new_sd_ptr;
}

int set_sd_defaults_from_token( struct object *obj, const struct security_descriptor *sd,
                                unsigned int set_info, struct token *token )
{
    struct security_descriptor *new_sd;

    if (!set_info) return 1;

    new_sd = set_sd_from_token_internal( sd, obj->sd, set_info, token );
    if (new_sd)
    {
        free( obj->sd );
        obj->sd = new_sd;
        return 1;
    }

    return 0;
}

/** Set the security descriptor using the current primary token for defaults. */
int default_set_sd( struct object *obj, const struct security_descriptor *sd,
                    unsigned int set_info )
{
    return set_sd_defaults_from_token( obj, sd, set_info, current->process->token );
}

struct object *no_lookup_name( struct object *obj, struct unicode_str *name,
                               unsigned int attr )
{
    if (!name) set_error( STATUS_OBJECT_TYPE_MISMATCH );
    return NULL;
}

int no_link_name( struct object *obj, struct object_name *name, struct object *parent )
{
    set_error( STATUS_OBJECT_TYPE_MISMATCH );
    return 0;
}

void default_unlink_name( struct object *obj, struct object_name *name )
{
    list_remove( &name->entry );
}

struct object *no_open_file( struct object *obj, unsigned int access, unsigned int sharing,
                             unsigned int options )
{
    set_error( STATUS_OBJECT_TYPE_MISMATCH );
    return NULL;
}

void no_alloc_handle( struct object *obj, struct process *process, obj_handle_t handle )
{
}

int no_close_handle( struct object *obj, struct process *process, obj_handle_t handle )
{
    return 1;  /* ok to close */
}

void no_destroy( struct object *obj )
{
}

static const struct unicode_str type_array[] =
{
    {type_Type,          sizeof(type_Type)},
    {type_Directory,     sizeof(type_Directory)},
    {type_SymbolicLink,  sizeof(type_SymbolicLink)},
    {type_Token,         sizeof(type_Token)},
    {type_Job,           sizeof(type_Job)},
    {type_Process,       sizeof(type_Process)},
    {type_Thread,        sizeof(type_Thread)},
    {type_Event,         sizeof(type_Event)},
    {type_Mutant,        sizeof(type_Mutant)},
    {type_Semaphore,     sizeof(type_Semaphore)},
    {type_Timer,         sizeof(type_Timer)},
    {type_KeyedEvent,    sizeof(type_KeyedEvent)},
    {type_WindowStation, sizeof(type_WindowStation)},
    {type_Desktop,       sizeof(type_Desktop)},
    {type_Device,        sizeof(type_Device)},
    /* Driver */
    {type_IoCompletion,  sizeof(type_IoCompletion)},
    {type_File,          sizeof(type_File)},
    {type_Section,       sizeof(type_Section)},
    {type_Key,           sizeof(type_Key)},
};

void init_types(void)
{
    struct object_type *type;
    unsigned int i;

    for (i = 0; i < sizeof(type_array) / sizeof(type_array[0]); i++)
    {
        type = get_object_type(&type_array[i]);
        if (type) release_object(type);
    }
}
