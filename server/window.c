/*
 * Server-side window handling
 *
 * Copyright (C) 2001 Alexandre Julliard
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

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winternl.h"

#include "object.h"
#include "request.h"
#include "thread.h"
#include "process.h"
#include "user.h"
#include "unicode.h"

/* a window property */
struct property
{
    unsigned short type;     /* property type (see below) */
    atom_t         atom;     /* property atom */
    lparam_t       data;     /* property data (user-defined storage) */
};

enum property_type
{
    PROP_TYPE_FREE,   /* free entry */
    PROP_TYPE_STRING, /* atom that was originally a string */
    PROP_TYPE_ATOM    /* plain atom */
};


struct window
{
    struct window   *parent;          /* parent window */
    user_handle_t    owner;           /* owner of this window */
    struct list      children;        /* list of children in Z-order */
    struct list      unlinked;        /* list of children not linked in the Z-order list */
    struct list      entry;           /* entry in parent's children list */
    user_handle_t    handle;          /* full handle for this window */
    struct thread   *thread;          /* thread owning the window */
    struct desktop  *desktop;         /* desktop that the window belongs to */
    struct window_class *class;       /* window class */
    atom_t           atom;            /* class atom */
    user_handle_t    last_active;     /* last active popup */
    rectangle_t      window_rect;     /* window rectangle (relative to parent client area) */
    rectangle_t      visible_rect;    /* visible part of window rect (relative to parent client area) */
    rectangle_t      client_rect;     /* client rectangle (relative to parent client area) */
    struct region   *win_region;      /* region for shaped windows (relative to window rect) */
    struct region   *layer_region;    /* region for layered windows (relative to window rect) */
    struct region   *update_region;   /* update region (relative to window rect) */
    unsigned int     style;           /* window style */
    unsigned int     ex_style;        /* window extended style */
    unsigned int     id;              /* window id */
    mod_handle_t     instance;        /* creator instance */
    unsigned int     is_unicode : 1;  /* ANSI or unicode */
    unsigned int     is_linked : 1;   /* is it linked into the parent z-order list? */
    unsigned int     is_layered : 1;  /* has layered info been set? */
    unsigned int     color_key;       /* color key for a layered window */
    unsigned int     alpha;           /* alpha value for a layered window */
    unsigned int     layered_flags;   /* flags for a layered window */
    lparam_t         user_data;       /* user-specific data */
    WCHAR           *text;            /* window caption text */
    unsigned int     paint_flags;     /* various painting flags */
    int              prop_inuse;      /* number of in-use window properties */
    int              prop_alloc;      /* number of allocated window properties */
    struct property *properties;      /* window properties array */
    int              nb_extra_bytes;  /* number of extra bytes */
    char             extra_bytes[1];  /* extra bytes storage */
};

/* flags that can be set by the client */
#define PAINT_HAS_SURFACE        SET_WINPOS_PAINT_SURFACE
#define PAINT_HAS_PIXEL_FORMAT   SET_WINPOS_PIXEL_FORMAT
#define PAINT_CLIENT_FLAGS       (PAINT_HAS_SURFACE | PAINT_HAS_PIXEL_FORMAT)
/* flags only manipulated by the server */
#define PAINT_INTERNAL           0x0010  /* internal WM_PAINT pending */
#define PAINT_ERASE              0x0020  /* needs WM_ERASEBKGND */
#define PAINT_NONCLIENT          0x0040  /* needs WM_NCPAINT */
#define PAINT_DELAYED_ERASE      0x0080  /* still needs erase after WM_ERASEBKGND */
#define PAINT_PIXEL_FORMAT_CHILD 0x0100  /* at least one child has a custom pixel format */

/* growable array of user handles */
struct user_handle_array
{
    user_handle_t *handles;
    int            count;
    int            total;
};

/* global window pointers */
static struct window *shell_window;
static struct window *shell_listview;
static struct window *progman_window;
static struct window *taskman_window;

/* magic HWND_TOP etc. pointers */
#define WINPTR_TOP       ((struct window *)1L)
#define WINPTR_BOTTOM    ((struct window *)2L)
#define WINPTR_TOPMOST   ((struct window *)3L)
#define WINPTR_NOTOPMOST ((struct window *)4L)

/* retrieve a pointer to a window from its handle */
static inline struct window *get_window( user_handle_t handle )
{
    struct window *ret = get_user_object( handle, USER_WINDOW );
    if (!ret) set_win32_error( ERROR_INVALID_WINDOW_HANDLE );
    return ret;
}

/* check if window is the desktop */
static inline int is_desktop_window( const struct window *win )
{
    return !win->parent;  /* only desktop windows have no parent */
}

/* get next window in Z-order list */
static inline struct window *get_next_window( struct window *win )
{
    struct list *ptr = list_next( &win->parent->children, &win->entry );
    return ptr ? LIST_ENTRY( ptr, struct window, entry ) : NULL;
}

/* get previous window in Z-order list */
static inline struct window *get_prev_window( struct window *win )
{
    struct list *ptr = list_prev( &win->parent->children, &win->entry );
    return ptr ? LIST_ENTRY( ptr, struct window, entry ) : NULL;
}

/* get first child in Z-order list */
static inline struct window *get_first_child( struct window *win )
{
    struct list *ptr = list_head( &win->children );
    return ptr ? LIST_ENTRY( ptr, struct window, entry ) : NULL;
}

/* get last child in Z-order list */
static inline struct window *get_last_child( struct window *win )
{
    struct list *ptr = list_tail( &win->children );
    return ptr ? LIST_ENTRY( ptr, struct window, entry ) : NULL;
}

/* set the PAINT_PIXEL_FORMAT_CHILD flag on all the parents */
/* note: we never reset the flag, it's just a heuristic */
static inline void update_pixel_format_flags( struct window *win )
{
    for (win = win->parent; win && win->parent; win = win->parent)
        win->paint_flags |= PAINT_PIXEL_FORMAT_CHILD;
}

/* link a window at the right place in the siblings list */
static void link_window( struct window *win, struct window *previous )
{
    if (previous == WINPTR_NOTOPMOST)
    {
        if (!(win->ex_style & WS_EX_TOPMOST) && win->is_linked) return;  /* nothing to do */
        win->ex_style &= ~WS_EX_TOPMOST;
        previous = WINPTR_TOP;  /* fallback to the HWND_TOP case */
    }

    list_remove( &win->entry );  /* unlink it from the previous location */

    if (previous == WINPTR_BOTTOM)
    {
        list_add_tail( &win->parent->children, &win->entry );
        win->ex_style &= ~WS_EX_TOPMOST;
    }
    else if (previous == WINPTR_TOPMOST)
    {
        list_add_head( &win->parent->children, &win->entry );
        win->ex_style |= WS_EX_TOPMOST;
    }
    else if (previous == WINPTR_TOP)
    {
        struct list *entry = win->parent->children.next;
        if (!(win->ex_style & WS_EX_TOPMOST))  /* put it above the first non-topmost window */
        {
            while (entry != &win->parent->children)
            {
                struct window *next = LIST_ENTRY( entry, struct window, entry );
                if (!(next->ex_style & WS_EX_TOPMOST)) break;
                if (next->handle == win->owner)  /* keep it above owner */
                {
                    win->ex_style |= WS_EX_TOPMOST;
                    break;
                }
                entry = entry->next;
            }
        }
        list_add_before( entry, &win->entry );
    }
    else
    {
        list_add_after( &previous->entry, &win->entry );
        if (!(previous->ex_style & WS_EX_TOPMOST)) win->ex_style &= ~WS_EX_TOPMOST;
        else
        {
            struct window *next = get_next_window( win );
            if (next && (next->ex_style & WS_EX_TOPMOST)) win->ex_style |= WS_EX_TOPMOST;
        }
    }

    win->is_linked = 1;
}

/* change the parent of a window (or unlink the window if the new parent is NULL) */
static int set_parent_window( struct window *win, struct window *parent )
{
    struct window *ptr;

    /* make sure parent is not a child of window */
    for (ptr = parent; ptr; ptr = ptr->parent)
    {
        if (ptr == win)
        {
            set_error( STATUS_INVALID_PARAMETER );
            return 0;
        }
    }

    if (parent)
    {
        win->parent = parent;
        link_window( win, WINPTR_TOP );

        /* if parent belongs to a different thread and the window isn't */
        /* top-level, attach the two threads */
        if (parent->thread && parent->thread != win->thread && !is_desktop_window(parent))
            attach_thread_input( win->thread, parent->thread );

        if (win->paint_flags & (PAINT_HAS_PIXEL_FORMAT | PAINT_PIXEL_FORMAT_CHILD))
            update_pixel_format_flags( win );
    }
    else  /* move it to parent unlinked list */
    {
        list_remove( &win->entry );  /* unlink it from the previous location */
        list_add_head( &win->parent->unlinked, &win->entry );
        win->is_linked = 0;
    }
    return 1;
}

/* append a user handle to a handle array */
static int add_handle_to_array( struct user_handle_array *array, user_handle_t handle )
{
    if (array->count >= array->total)
    {
        int new_total = max( array->total * 2, 32 );
        user_handle_t *new_array = realloc( array->handles, new_total * sizeof(*new_array) );
        if (!new_array)
        {
            free( array->handles );
            set_error( STATUS_NO_MEMORY );
            return 0;
        }
        array->handles = new_array;
        array->total = new_total;
    }
    array->handles[array->count++] = handle;
    return 1;
}

/* set a window property */
static void set_property( struct window *win, atom_t atom, lparam_t data, enum property_type type )
{
    int i, free = -1;
    struct property *new_props;

    /* check if it exists already */
    for (i = 0; i < win->prop_inuse; i++)
    {
        if (win->properties[i].type == PROP_TYPE_FREE)
        {
            free = i;
            continue;
        }
        if (win->properties[i].atom == atom)
        {
            win->properties[i].type = type;
            win->properties[i].data = data;
            return;
        }
    }

    /* need to add an entry */
    if (!grab_global_atom( NULL, atom )) return;
    if (free == -1)
    {
        /* no free entry */
        if (win->prop_inuse >= win->prop_alloc)
        {
            /* need to grow the array */
            if (!(new_props = realloc( win->properties,
                                       sizeof(*new_props) * (win->prop_alloc + 16) )))
            {
                set_error( STATUS_NO_MEMORY );
                release_global_atom( NULL, atom );
                return;
            }
            win->prop_alloc += 16;
            win->properties = new_props;
        }
        free = win->prop_inuse++;
    }
    win->properties[free].atom = atom;
    win->properties[free].type = type;
    win->properties[free].data = data;
}

/* remove a window property */
static lparam_t remove_property( struct window *win, atom_t atom )
{
    int i;

    for (i = 0; i < win->prop_inuse; i++)
    {
        if (win->properties[i].type == PROP_TYPE_FREE) continue;
        if (win->properties[i].atom == atom)
        {
            release_global_atom( NULL, atom );
            win->properties[i].type = PROP_TYPE_FREE;
            return win->properties[i].data;
        }
    }
    /* FIXME: last error? */
    return 0;
}

/* find a window property */
static lparam_t get_property( struct window *win, atom_t atom )
{
    int i;

    for (i = 0; i < win->prop_inuse; i++)
    {
        if (win->properties[i].type == PROP_TYPE_FREE) continue;
        if (win->properties[i].atom == atom) return win->properties[i].data;
    }
    /* FIXME: last error? */
    return 0;
}

/* destroy all properties of a window */
static inline void destroy_properties( struct window *win )
{
    int i;

    if (!win->properties) return;
    for (i = 0; i < win->prop_inuse; i++)
    {
        if (win->properties[i].type == PROP_TYPE_FREE) continue;
        release_global_atom( NULL, win->properties[i].atom );
    }
    free( win->properties );
}

/* detach a window from its owner thread but keep the window around */
static void detach_window_thread( struct window *win )
{
    struct thread *thread = win->thread;

    if (!thread) return;
    if (thread->queue)
    {
        if (win->update_region) inc_queue_paint_count( thread, -1 );
        if (win->paint_flags & PAINT_INTERNAL) inc_queue_paint_count( thread, -1 );
        queue_cleanup_window( thread, win->handle );
    }
    assert( thread->desktop_users > 0 );
    thread->desktop_users--;
    release_class( win->class );
    win->class = NULL;

    /* don't hold a reference to the desktop so that the desktop window can be */
    /* destroyed when the desktop ref count reaches zero */
    release_object( win->desktop );
    win->thread = NULL;
}

/* get the process owning the top window of a given desktop */
struct process *get_top_window_owner( struct desktop *desktop )
{
    struct window *win = desktop->top_window;
    if (!win || !win->thread) return NULL;
    return win->thread->process;
}

/* get the top window size of a given desktop */
void get_top_window_rectangle( struct desktop *desktop, rectangle_t *rect )
{
    struct window *win = desktop->top_window;
    if (!win) rect->left = rect->top = rect->right = rect->bottom = 0;
    else *rect = win->window_rect;
}

/* post a message to the desktop window */
void post_desktop_message( struct desktop *desktop, unsigned int message,
                           lparam_t wparam, lparam_t lparam )
{
    struct window *win = desktop->top_window;
    if (win && win->thread) post_message( win->handle, message, wparam, lparam );
}

/* create a new window structure (note: the window is not linked in the window tree) */
static struct window *create_window( struct window *parent, struct window *owner,
                                     atom_t atom, mod_handle_t instance )
{
    static const rectangle_t empty_rect;
    int extra_bytes;
    struct window *win = NULL;
    struct desktop *desktop;
    struct window_class *class;

    if (!(desktop = get_thread_desktop( current, DESKTOP_CREATEWINDOW ))) return NULL;

    if (!(class = grab_class( current->process, atom, instance, &extra_bytes )))
    {
        release_object( desktop );
        return NULL;
    }

    if (!parent)  /* null parent is only allowed for desktop or HWND_MESSAGE top window */
    {
        if (is_desktop_class( class ))
            parent = desktop->top_window;  /* use existing desktop if any */
        else if (is_hwnd_message_class( class ))
            /* use desktop window if message window is already created */
            parent = desktop->msg_window ? desktop->top_window : NULL;
        else if (!(parent = desktop->top_window))  /* must already have a desktop then */
        {
            set_error( STATUS_ACCESS_DENIED );
            goto failed;
        }
    }

    /* parent must be on the same desktop */
    if (parent && parent->desktop != desktop)
    {
        set_error( STATUS_ACCESS_DENIED );
        goto failed;
    }

    if (!(win = mem_alloc( sizeof(*win) + extra_bytes - 1 ))) goto failed;
    if (!(win->handle = alloc_user_handle( win, USER_WINDOW ))) goto failed;

    win->parent         = parent;
    win->owner          = owner ? owner->handle : 0;
    win->thread         = current;
    win->desktop        = desktop;
    win->class          = class;
    win->atom           = atom;
    win->last_active    = win->handle;
    win->win_region     = NULL;
    win->layer_region   = NULL;
    win->update_region  = NULL;
    win->style          = 0;
    win->ex_style       = 0;
    win->id             = 0;
    win->instance       = 0;
    win->is_unicode     = 1;
    win->is_linked      = 0;
    win->is_layered     = 0;
    win->user_data      = 0;
    win->text           = NULL;
    win->paint_flags    = 0;
    win->prop_inuse     = 0;
    win->prop_alloc     = 0;
    win->properties     = NULL;
    win->nb_extra_bytes = extra_bytes;
    win->window_rect = win->visible_rect = win->client_rect = empty_rect;
    memset( win->extra_bytes, 0, extra_bytes );
    list_init( &win->children );
    list_init( &win->unlinked );

    /* if parent belongs to a different thread and the window isn't */
    /* top-level, attach the two threads */
    if (parent && parent->thread && parent->thread != current && !is_desktop_window(parent))
    {
        if (!attach_thread_input( current, parent->thread )) goto failed;
    }
    else  /* otherwise just make sure that the thread has a message queue */
    {
        if (!current->queue && !init_thread_queue( current )) goto failed;
    }

    /* put it on parent unlinked list */
    if (parent) list_add_head( &parent->unlinked, &win->entry );
    else
    {
        list_init( &win->entry );
        if (is_desktop_class( class ))
        {
            assert( !desktop->top_window );
            desktop->top_window = win;
            set_process_default_desktop( current->process, desktop, current->desktop );
        }
        else
        {
            assert( !desktop->msg_window );
            desktop->msg_window = win;
        }
    }

    current->desktop_users++;
    return win;

failed:
    if (win)
    {
        if (win->handle) free_user_handle( win->handle );
        free( win );
    }
    release_object( desktop );
    release_class( class );
    return NULL;
}

/* destroy all windows belonging to a given thread */
void destroy_thread_windows( struct thread *thread )
{
    user_handle_t handle = 0;
    struct window *win;

    while ((win = next_user_handle( &handle, USER_WINDOW )))
    {
        if (win->thread != thread) continue;
        if (is_desktop_window( win )) detach_window_thread( win );
        else destroy_window( win );
    }
}

/* get the desktop window */
static struct window *get_desktop_window( struct thread *thread )
{
    struct window *top_window;
    struct desktop *desktop = get_thread_desktop( thread, 0 );

    if (!desktop) return NULL;
    top_window = desktop->top_window;
    release_object( desktop );
    return top_window;
}

/* check whether child is a descendant of parent */
int is_child_window( user_handle_t parent, user_handle_t child )
{
    struct window *child_ptr = get_user_object( child, USER_WINDOW );
    struct window *parent_ptr = get_user_object( parent, USER_WINDOW );

    if (!child_ptr || !parent_ptr) return 0;
    while (child_ptr->parent)
    {
        if (child_ptr->parent == parent_ptr) return 1;
        child_ptr = child_ptr->parent;
    }
    return 0;
}

/* check if window can be set as foreground window */
int is_valid_foreground_window( user_handle_t window )
{
    struct window *win = get_user_object( window, USER_WINDOW );
    return win && (win->style & (WS_POPUP|WS_CHILD)) != WS_CHILD;
}

/* make a window active if possible */
int make_window_active( user_handle_t window )
{
    struct window *owner, *win = get_window( window );

    if (!win) return 0;

    /* set last active for window and its owners */
    owner = win;
    while (owner)
    {
        owner->last_active = win->handle;
        owner = get_user_object( owner->owner, USER_WINDOW );
    }
    return 1;
}

/* increment (or decrement) the window paint count */
static inline void inc_window_paint_count( struct window *win, int incr )
{
    if (win->thread) inc_queue_paint_count( win->thread, incr );
}

/* check if window and all its ancestors are visible */
static int is_visible( const struct window *win )
{
    while (win)
    {
        if (!(win->style & WS_VISIBLE)) return 0;
        win = win->parent;
        /* if parent is minimized children are not visible */
        if (win && (win->style & WS_MINIMIZE)) return 0;
    }
    return 1;
}

/* same as is_visible but takes a window handle */
int is_window_visible( user_handle_t window )
{
    struct window *win = get_user_object( window, USER_WINDOW );
    if (!win) return 0;
    return is_visible( win );
}

int is_window_transparent( user_handle_t window )
{
    struct window *win = get_user_object( window, USER_WINDOW );
    if (!win) return 0;
    return (win->ex_style & (WS_EX_LAYERED|WS_EX_TRANSPARENT)) == (WS_EX_LAYERED|WS_EX_TRANSPARENT);
}

/* check if point is inside the window */
static inline int is_point_in_window( struct window *win, int x, int y )
{
    if (!(win->style & WS_VISIBLE)) return 0; /* not visible */
    if ((win->style & (WS_POPUP|WS_CHILD|WS_DISABLED)) == (WS_CHILD|WS_DISABLED))
        return 0;  /* disabled child */
    if ((win->ex_style & (WS_EX_LAYERED|WS_EX_TRANSPARENT)) == (WS_EX_LAYERED|WS_EX_TRANSPARENT))
        return 0;  /* transparent */
    if (x < win->visible_rect.left || x >= win->visible_rect.right ||
        y < win->visible_rect.top || y >= win->visible_rect.bottom)
        return 0;  /* not in window */
    if (win->win_region &&
        !point_in_region( win->win_region, x - win->window_rect.left, y - win->window_rect.top ))
        return 0;  /* not in window region */
    if (win->layer_region &&
        !point_in_region( win->layer_region, x - win->window_rect.left, y - win->window_rect.top ))
        return 0;  /* not in layer mask region */
    return 1;
}

/* fill an array with the handles of the children of a specified window */
static unsigned int get_children_windows( struct window *parent, atom_t atom, thread_id_t tid,
                                          user_handle_t *handles, unsigned int max_count )
{
    struct window *ptr;
    unsigned int count = 0;

    if (!parent) return 0;

    LIST_FOR_EACH_ENTRY( ptr, &parent->children, struct window, entry )
    {
        if (atom && get_class_atom(ptr->class) != atom) continue;
        if (tid && get_thread_id(ptr->thread) != tid) continue;
        if (handles)
        {
            if (count >= max_count) break;
            handles[count] = ptr->handle;
        }
        count++;
    }
    return count;
}

/* find child of 'parent' that contains the given point (in parent-relative coords) */
static struct window *child_window_from_point( struct window *parent, int x, int y )
{
    struct window *ptr;

    LIST_FOR_EACH_ENTRY( ptr, &parent->children, struct window, entry )
    {
        if (!is_point_in_window( ptr, x, y )) continue;  /* skip it */

        /* if window is minimized or disabled, return at once */
        if (ptr->style & (WS_MINIMIZE|WS_DISABLED)) return ptr;

        /* if point is not in client area, return at once */
        if (x < ptr->client_rect.left || x >= ptr->client_rect.right ||
            y < ptr->client_rect.top || y >= ptr->client_rect.bottom)
            return ptr;

        return child_window_from_point( ptr, x - ptr->client_rect.left, y - ptr->client_rect.top );
    }
    return parent;  /* not found any child */
}

/* find all children of 'parent' that contain the given point */
static int get_window_children_from_point( struct window *parent, int x, int y,
                                           struct user_handle_array *array )
{
    struct window *ptr;

    LIST_FOR_EACH_ENTRY( ptr, &parent->children, struct window, entry )
    {
        if (!is_point_in_window( ptr, x, y )) continue;  /* skip it */

        /* if point is in client area, and window is not minimized or disabled, check children */
        if (!(ptr->style & (WS_MINIMIZE|WS_DISABLED)) &&
            x >= ptr->client_rect.left && x < ptr->client_rect.right &&
            y >= ptr->client_rect.top && y < ptr->client_rect.bottom)
        {
            if (!get_window_children_from_point( ptr, x - ptr->client_rect.left,
                                                 y - ptr->client_rect.top, array ))
                return 0;
        }

        /* now add window to the array */
        if (!add_handle_to_array( array, ptr->handle )) return 0;
    }
    return 1;
}

/* get handle of root of top-most window containing point */
user_handle_t shallow_window_from_point( struct desktop *desktop, int x, int y )
{
    struct window *ptr;

    if (!desktop->top_window) return 0;

    LIST_FOR_EACH_ENTRY( ptr, &desktop->top_window->children, struct window, entry )
    {
        if (!is_point_in_window( ptr, x, y )) continue;  /* skip it */
        return ptr->handle;
    }
    return desktop->top_window->handle;
}

/* return thread of top-most window containing point (in absolute coords) */
struct thread *window_thread_from_point( user_handle_t scope, int x, int y )
{
    struct window *win = get_user_object( scope, USER_WINDOW );
    struct window *ptr;

    if (!win) return NULL;

    for (ptr = win; ptr && !is_desktop_window(ptr); ptr = ptr->parent)
    {
        x -= ptr->client_rect.left;
        y -= ptr->client_rect.top;
    }

    ptr =  child_window_from_point( win, x, y );
    if (!ptr->thread) return NULL;
    return (struct thread *)grab_object( ptr->thread );
}

/* return list of all windows containing point (in absolute coords) */
static int all_windows_from_point( struct window *top, int x, int y, struct user_handle_array *array )
{
    struct window *ptr;

    /* make point relative to top window */
    for (ptr = top->parent; ptr && !is_desktop_window(ptr); ptr = ptr->parent)
    {
        x -= ptr->client_rect.left;
        y -= ptr->client_rect.top;
    }

    if (!is_point_in_window( top, x, y )) return 1;

    /* if point is in client area, and window is not minimized or disabled, check children */
    if (!(top->style & (WS_MINIMIZE|WS_DISABLED)) &&
        x >= top->client_rect.left && x < top->client_rect.right &&
        y >= top->client_rect.top && y < top->client_rect.bottom)
    {
        if (!is_desktop_window(top))
        {
            x -= top->client_rect.left;
            y -= top->client_rect.top;
        }
        if (!get_window_children_from_point( top, x, y, array )) return 0;
    }
    /* now add window to the array */
    if (!add_handle_to_array( array, top->handle )) return 0;
    return 1;
}


/* return the thread owning a window */
struct thread *get_window_thread( user_handle_t handle )
{
    struct window *win = get_user_object( handle, USER_WINDOW );
    if (!win || !win->thread) return NULL;
    return (struct thread *)grab_object( win->thread );
}


/* check if any area of a window needs repainting */
static inline int win_needs_repaint( struct window *win )
{
    return win->update_region || (win->paint_flags & PAINT_INTERNAL);
}


/* find a child of the specified window that needs repainting */
static struct window *find_child_to_repaint( struct window *parent, struct thread *thread )
{
    struct window *ptr, *ret = NULL;

    LIST_FOR_EACH_ENTRY( ptr, &parent->children, struct window, entry )
    {
        if (!(ptr->style & WS_VISIBLE)) continue;
        if (ptr->thread == thread && win_needs_repaint( ptr ))
            ret = ptr;
        else if (!(ptr->style & WS_MINIMIZE)) /* explore its children */
            ret = find_child_to_repaint( ptr, thread );
        if (ret) break;
    }

    if (ret && (ret->ex_style & WS_EX_TRANSPARENT))
    {
        /* transparent window, check for non-transparent sibling to paint first */
        for (ptr = get_next_window(ret); ptr; ptr = get_next_window(ptr))
        {
            if (!(ptr->style & WS_VISIBLE)) continue;
            if (ptr->ex_style & WS_EX_TRANSPARENT) continue;
            if (ptr->thread != thread) continue;
            if (win_needs_repaint( ptr )) return ptr;
        }
    }
    return ret;
}


/* find a window that needs to receive a WM_PAINT; also clear its internal paint flag */
user_handle_t find_window_to_repaint( user_handle_t parent, struct thread *thread )
{
    struct window *ptr, *win, *top_window = get_desktop_window( thread );

    if (!top_window) return 0;

    if (top_window->thread == thread && win_needs_repaint( top_window )) win = top_window;
    else win = find_child_to_repaint( top_window, thread );

    if (win && parent)
    {
        /* check that it is a child of the specified parent */
        for (ptr = win; ptr; ptr = ptr->parent)
            if (ptr->handle == parent) break;
        /* otherwise don't return any window, we don't repaint a child before its parent */
        if (!ptr) win = NULL;
    }
    if (!win) return 0;
    win->paint_flags &= ~PAINT_INTERNAL;
    return win->handle;
}


/* intersect the window region with the specified region, relative to the window parent */
static struct region *intersect_window_region( struct region *region, struct window *win )
{
    /* make region relative to window rect */
    offset_region( region, -win->window_rect.left, -win->window_rect.top );
    if (!intersect_region( region, region, win->win_region )) return NULL;
    /* make region relative to parent again */
    offset_region( region, win->window_rect.left, win->window_rect.top );
    return region;
}


/* convert coordinates from client to screen coords */
static inline void client_to_screen( struct window *win, int *x, int *y )
{
    for ( ; win && !is_desktop_window(win); win = win->parent)
    {
        *x += win->client_rect.left;
        *y += win->client_rect.top;
    }
}

/* convert coordinates from client to screen coords */
static inline void client_to_screen_rect( struct window *win, rectangle_t *rect )
{
    for ( ; win && !is_desktop_window(win); win = win->parent)
    {
        rect->left   += win->client_rect.left;
        rect->right  += win->client_rect.left;
        rect->top    += win->client_rect.top;
        rect->bottom += win->client_rect.top;
    }
}

/* map the region from window to screen coordinates */
static inline void map_win_region_to_screen( struct window *win, struct region *region )
{
    if (!is_desktop_window(win))
    {
        int x = win->window_rect.left;
        int y = win->window_rect.top;
        client_to_screen( win->parent, &x, &y );
        offset_region( region, x, y );
    }
}


/* clip all children of a given window out of the visible region */
static struct region *clip_children( struct window *parent, struct window *last,
                                     struct region *region, int offset_x, int offset_y )
{
    struct window *ptr;
    struct region *tmp = create_empty_region();

    if (!tmp) return NULL;
    LIST_FOR_EACH_ENTRY( ptr, &parent->children, struct window, entry )
    {
        if (ptr == last) break;
        if (!(ptr->style & WS_VISIBLE)) continue;
        if (ptr->ex_style & WS_EX_TRANSPARENT) continue;
        set_region_rect( tmp, &ptr->visible_rect );
        if (ptr->win_region && !intersect_window_region( tmp, ptr ))
        {
            free_region( tmp );
            return NULL;
        }
        offset_region( tmp, offset_x, offset_y );
        if (!(region = subtract_region( region, region, tmp ))) break;
        if (is_region_empty( region )) break;
    }
    free_region( tmp );
    return region;
}


/* offset the coordinates of a rectangle */
static inline void offset_rect( rectangle_t *rect, int offset_x, int offset_y )
{
    rect->left   += offset_x;
    rect->top    += offset_y;
    rect->right  += offset_x;
    rect->bottom += offset_y;
}


/* set the region to the client rect clipped by the window rect, in parent-relative coordinates */
static void set_region_client_rect( struct region *region, struct window *win )
{
    rectangle_t rect;

    intersect_rect( &rect, &win->window_rect, &win->client_rect );
    set_region_rect( region, &rect );
}


/* get the top-level window to clip against for a given window */
static inline struct window *get_top_clipping_window( struct window *win )
{
    while (!(win->paint_flags & PAINT_HAS_SURFACE) && win->parent && !is_desktop_window(win->parent))
        win = win->parent;
    return win;
}


/* compute the visible region of a window, in window coordinates */
static struct region *get_visible_region( struct window *win, unsigned int flags )
{
    struct region *tmp = NULL, *region;
    int offset_x, offset_y;

    if (!(region = create_empty_region())) return NULL;

    /* first check if all ancestors are visible */

    if (!is_visible( win )) return region;  /* empty region */

    /* create a region relative to the window itself */

    if ((flags & DCX_PARENTCLIP) && win->parent && !is_desktop_window(win->parent))
    {
        set_region_client_rect( region, win->parent );
        offset_region( region, -win->parent->client_rect.left, -win->parent->client_rect.top );
    }
    else if (flags & DCX_WINDOW)
    {
        set_region_rect( region, &win->visible_rect );
        if (win->win_region && !intersect_window_region( region, win )) goto error;
    }
    else
    {
        set_region_client_rect( region, win );
        if (win->win_region && !intersect_window_region( region, win )) goto error;
    }

    /* clip children */

    if (flags & DCX_CLIPCHILDREN)
    {
        if (is_desktop_window(win)) offset_x = offset_y = 0;
        else
        {
            offset_x = win->client_rect.left;
            offset_y = win->client_rect.top;
        }
        if (!clip_children( win, NULL, region, offset_x, offset_y )) goto error;
    }

    /* clip siblings of ancestors */

    if (is_desktop_window(win)) offset_x = offset_y = 0;
    else
    {
        offset_x = win->window_rect.left;
        offset_y = win->window_rect.top;
    }

    if ((tmp = create_empty_region()) != NULL)
    {
        while (win->parent)
        {
            /* we don't clip out top-level siblings as that's up to the native windowing system */
            if ((win->style & WS_CLIPSIBLINGS) && !is_desktop_window( win->parent ))
            {
                if (!clip_children( win->parent, win, region, 0, 0 )) goto error;
                if (is_region_empty( region )) break;
            }
            /* clip to parent client area */
            win = win->parent;
            if (!is_desktop_window(win))
            {
                offset_x += win->client_rect.left;
                offset_y += win->client_rect.top;
                offset_region( region, win->client_rect.left, win->client_rect.top );
            }
            set_region_client_rect( tmp, win );
            if (win->win_region && !intersect_window_region( tmp, win )) goto error;
            if (!intersect_region( region, region, tmp )) goto error;
            if (is_region_empty( region )) break;
        }
        free_region( tmp );
    }
    offset_region( region, -offset_x, -offset_y );  /* make it relative to target window */
    return region;

error:
    if (tmp) free_region( tmp );
    free_region( region );
    return NULL;
}


/* clip all children with a custom pixel format out of the visible region */
static struct region *clip_pixel_format_children( struct window *parent, struct region *parent_clip,
                                                  struct region *region, int offset_x, int offset_y )
{
    struct window *ptr;
    struct region *clip = create_empty_region();

    if (!clip) return NULL;

    LIST_FOR_EACH_ENTRY_REV( ptr, &parent->children, struct window, entry )
    {
        if (!(ptr->style & WS_VISIBLE)) continue;
        if (ptr->ex_style & WS_EX_TRANSPARENT) continue;

        /* add the visible rect */
        set_region_rect( clip, &ptr->visible_rect );
        if (ptr->win_region && !intersect_window_region( clip, ptr )) break;
        offset_region( clip, offset_x, offset_y );
        if (!intersect_region( clip, clip, parent_clip )) break;
        if (!union_region( region, region, clip )) break;
        if (!(ptr->paint_flags & (PAINT_HAS_PIXEL_FORMAT | PAINT_PIXEL_FORMAT_CHILD))) continue;

        /* subtract the client rect if it uses a custom pixel format */
        set_region_rect( clip, &ptr->client_rect );
        if (ptr->win_region && !intersect_window_region( clip, ptr )) break;
        offset_region( clip, offset_x, offset_y );
        if (!intersect_region( clip, clip, parent_clip )) break;
        if ((ptr->paint_flags & PAINT_HAS_PIXEL_FORMAT) && !subtract_region( region, region, clip ))
            break;

        if (!clip_pixel_format_children( ptr, clip, region, offset_x + ptr->client_rect.left,
                                         offset_y + ptr->client_rect.top ))
            break;
    }
    free_region( clip );
    return region;
}


/* compute the visible surface region of a window, in parent coordinates */
static struct region *get_surface_region( struct window *win )
{
    struct region *region, *clip;
    int offset_x, offset_y;

    /* create a region relative to the window itself */

    if (!(region = create_empty_region())) return NULL;
    if (!(clip = create_empty_region())) goto error;
    set_region_rect( region, &win->visible_rect );
    if (win->win_region && !intersect_window_region( region, win )) goto error;
    set_region_rect( clip, &win->client_rect );
    if (win->win_region && !intersect_window_region( clip, win )) goto error;

    if ((win->paint_flags & PAINT_HAS_PIXEL_FORMAT) && !subtract_region( region, region, clip ))
        goto error;

    /* clip children */

    if (!is_desktop_window(win))
    {
        offset_x = win->client_rect.left;
        offset_y = win->client_rect.top;
    }
    else offset_x = offset_y = 0;

    if (!clip_pixel_format_children( win, clip, region, offset_x, offset_y )) goto error;

    free_region( clip );
    return region;

error:
    if (clip) free_region( clip );
    free_region( region );
    return NULL;
}


/* get the window class of a window */
struct window_class* get_window_class( user_handle_t window )
{
    struct window *win;
    if (!(win = get_window( window ))) return NULL;
    if (!win->class) set_error( STATUS_ACCESS_DENIED );
    return win->class;
}

/* determine the window visible rectangle, i.e. window or client rect cropped by parent rects */
/* the returned rectangle is in window coordinates; return 0 if rectangle is empty */
static int get_window_visible_rect( struct window *win, rectangle_t *rect, int frame )
{
    int offset_x = 0, offset_y = 0;

    if (!(win->style & WS_VISIBLE)) return 0;

    *rect = frame ? win->window_rect : win->client_rect;
    if (!is_desktop_window(win))
    {
        offset_x = win->window_rect.left;
        offset_y = win->window_rect.top;
    }

    while (win->parent)
    {
        win = win->parent;
        if (!(win->style & WS_VISIBLE) || win->style & WS_MINIMIZE) return 0;
        if (!is_desktop_window(win))
        {
            offset_x += win->client_rect.left;
            offset_y += win->client_rect.top;
            offset_rect( rect, win->client_rect.left, win->client_rect.top );
        }
        if (!intersect_rect( rect, rect, &win->client_rect )) return 0;
        if (!intersect_rect( rect, rect, &win->window_rect )) return 0;
    }
    offset_rect( rect, -offset_x, -offset_y );
    return 1;
}

/* return a copy of the specified region cropped to the window client or frame rectangle, */
/* and converted from client to window coordinates. Helper for (in)validate_window. */
static struct region *crop_region_to_win_rect( struct window *win, struct region *region, int frame )
{
    rectangle_t rect;
    struct region *tmp;

    if (!get_window_visible_rect( win, &rect, frame )) return NULL;
    if (!(tmp = create_empty_region())) return NULL;
    set_region_rect( tmp, &rect );

    if (region)
    {
        /* map it to client coords */
        offset_region( tmp, win->window_rect.left - win->client_rect.left,
                       win->window_rect.top - win->client_rect.top );

        /* intersect specified region with bounding rect */
        if (!intersect_region( tmp, region, tmp )) goto done;
        if (is_region_empty( tmp )) goto done;

        /* map it back to window coords */
        offset_region( tmp, win->client_rect.left - win->window_rect.left,
                       win->client_rect.top - win->window_rect.top );
    }
    return tmp;

done:
    free_region( tmp );
    return NULL;
}


/* set a region as new update region for the window */
static void set_update_region( struct window *win, struct region *region )
{
    if (region && !is_region_empty( region ))
    {
        if (!win->update_region) inc_window_paint_count( win, 1 );
        else free_region( win->update_region );
        win->update_region = region;
    }
    else
    {
        if (win->update_region)
        {
            inc_window_paint_count( win, -1 );
            free_region( win->update_region );
        }
        win->paint_flags &= ~(PAINT_ERASE | PAINT_DELAYED_ERASE | PAINT_NONCLIENT);
        win->update_region = NULL;
        if (region) free_region( region );
    }
}


/* add a region to the update region; the passed region is freed or reused */
static int add_update_region( struct window *win, struct region *region )
{
    if (win->update_region && !union_region( region, win->update_region, region ))
    {
        free_region( region );
        return 0;
    }
    set_update_region( win, region );
    return 1;
}


/* crop the update region of children to the specified rectangle, in client coords */
static void crop_children_update_region( struct window *win, rectangle_t *rect )
{
    struct window *child;
    struct region *tmp;
    rectangle_t child_rect;

    LIST_FOR_EACH_ENTRY( child, &win->children, struct window, entry )
    {
        if (!(child->style & WS_VISIBLE)) continue;
        if (!rect)  /* crop everything out */
        {
            crop_children_update_region( child, NULL );
            set_update_region( child, NULL );
            continue;
        }

        /* nothing to do if child is completely inside rect */
        if (child->window_rect.left >= rect->left &&
            child->window_rect.top >= rect->top &&
            child->window_rect.right <= rect->right &&
            child->window_rect.bottom <= rect->bottom) continue;

        /* map to child client coords and crop grand-children */
        child_rect = *rect;
        offset_rect( &child_rect, -child->client_rect.left, -child->client_rect.top );
        crop_children_update_region( child, &child_rect );

        /* now crop the child itself */
        if (!child->update_region) continue;
        if (!(tmp = create_empty_region())) continue;
        set_region_rect( tmp, rect );
        offset_region( tmp, -child->window_rect.left, -child->window_rect.top );
        if (intersect_region( tmp, child->update_region, tmp )) set_update_region( child, tmp );
        else free_region( tmp );
    }
}


/* validate the non client area of a window */
static void validate_non_client( struct window *win )
{
    struct region *tmp;
    rectangle_t rect;

    if (!win->update_region) return;  /* nothing to do */

    /* get client rect in window coords */
    rect.left   = win->client_rect.left - win->window_rect.left;
    rect.top    = win->client_rect.top - win->window_rect.top;
    rect.right  = win->client_rect.right - win->window_rect.left;
    rect.bottom = win->client_rect.bottom - win->window_rect.top;

    if ((tmp = create_empty_region()))
    {
        set_region_rect( tmp, &rect );
        if (intersect_region( tmp, win->update_region, tmp ))
            set_update_region( win, tmp );
        else
            free_region( tmp );
    }
    win->paint_flags &= ~PAINT_NONCLIENT;
}


/* validate a window completely so that we don't get any further paint messages for it */
static void validate_whole_window( struct window *win )
{
    set_update_region( win, NULL );

    if (win->paint_flags & PAINT_INTERNAL)
    {
        win->paint_flags &= ~PAINT_INTERNAL;
        inc_window_paint_count( win, -1 );
    }
}


/* validate a window's children so that we don't get any further paint messages for it */
static void validate_children( struct window *win )
{
    struct window *child;

    LIST_FOR_EACH_ENTRY( child, &win->children, struct window, entry )
    {
        if (!(child->style & WS_VISIBLE)) continue;
        validate_children(child);
        validate_whole_window(child);
    }
}


/* validate the update region of a window on all parents; helper for get_update_region */
static void validate_parents( struct window *child )
{
    int offset_x = 0, offset_y = 0;
    struct window *win = child;
    struct region *tmp = NULL;

    if (!child->update_region) return;

    while (win->parent)
    {
        /* map to parent client coords */
        offset_x += win->window_rect.left;
        offset_y += win->window_rect.top;

        win = win->parent;

        /* and now map to window coords */
        offset_x += win->client_rect.left - win->window_rect.left;
        offset_y += win->client_rect.top - win->window_rect.top;

        if (win->update_region && !(win->style & WS_CLIPCHILDREN))
        {
            if (!tmp && !(tmp = create_empty_region())) return;
            offset_region( child->update_region, offset_x, offset_y );
            if (subtract_region( tmp, win->update_region, child->update_region ))
            {
                set_update_region( win, tmp );
                tmp = NULL;
            }
            /* restore child coords */
            offset_region( child->update_region, -offset_x, -offset_y );
        }
    }
    if (tmp) free_region( tmp );
}


/* add/subtract a region (in client coordinates) to the update region of the window */
static void redraw_window( struct window *win, struct region *region, int frame, unsigned int flags )
{
    struct region *tmp;
    struct window *child;

    if (flags & RDW_INVALIDATE)
    {
        if (!(tmp = crop_region_to_win_rect( win, region, frame ))) return;

        if (!add_update_region( win, tmp )) return;

        if (flags & RDW_FRAME) win->paint_flags |= PAINT_NONCLIENT;
        if (flags & RDW_ERASE) win->paint_flags |= PAINT_ERASE;
    }
    else if (flags & RDW_VALIDATE)
    {
        if (!region && (flags & RDW_NOFRAME))  /* shortcut: validate everything */
        {
            set_update_region( win, NULL );
        }
        else if (win->update_region)
        {
            if ((tmp = crop_region_to_win_rect( win, region, frame )))
            {
                if (!subtract_region( tmp, win->update_region, tmp ))
                {
                    free_region( tmp );
                    return;
                }
                set_update_region( win, tmp );
            }
            if (flags & RDW_NOFRAME) validate_non_client( win );
            if (flags & RDW_NOERASE) win->paint_flags &= ~(PAINT_ERASE | PAINT_DELAYED_ERASE);
        }
    }

    if ((flags & RDW_INTERNALPAINT) && !(win->paint_flags & PAINT_INTERNAL))
    {
        win->paint_flags |= PAINT_INTERNAL;
        inc_window_paint_count( win, 1 );
    }
    else if ((flags & RDW_NOINTERNALPAINT) && (win->paint_flags & PAINT_INTERNAL))
    {
        win->paint_flags &= ~PAINT_INTERNAL;
        inc_window_paint_count( win, -1 );
    }

    /* now process children recursively */

    if (flags & RDW_NOCHILDREN) return;
    if (win->style & WS_MINIMIZE) return;
    if ((win->style & WS_CLIPCHILDREN) && !(flags & RDW_ALLCHILDREN)) return;

    if (!(tmp = crop_region_to_win_rect( win, region, 0 ))) return;

    /* map to client coordinates */
    offset_region( tmp, win->window_rect.left - win->client_rect.left,
                   win->window_rect.top - win->client_rect.top );

    if (flags & RDW_INVALIDATE) flags |= RDW_FRAME | RDW_ERASE;

    LIST_FOR_EACH_ENTRY( child, &win->children, struct window, entry )
    {
        if (!(child->style & WS_VISIBLE)) continue;
        if (!rect_in_region( tmp, &child->window_rect )) continue;
        offset_region( tmp, -child->client_rect.left, -child->client_rect.top );
        redraw_window( child, tmp, 1, flags );
        offset_region( tmp, child->client_rect.left, child->client_rect.top );
    }
    free_region( tmp );
}


/* retrieve the update flags for a window depending on the state of the update region */
static unsigned int get_update_flags( struct window *win, unsigned int flags )
{
    unsigned int ret = 0;

    if (flags & UPDATE_NONCLIENT)
    {
        if ((win->paint_flags & PAINT_NONCLIENT) && win->update_region) ret |= UPDATE_NONCLIENT;
    }
    if (flags & UPDATE_ERASE)
    {
        if ((win->paint_flags & PAINT_ERASE) && win->update_region) ret |= UPDATE_ERASE;
    }
    if (flags & UPDATE_PAINT)
    {
        if (win->update_region)
        {
            if (win->paint_flags & PAINT_DELAYED_ERASE) ret |= UPDATE_DELAYED_ERASE;
            ret |= UPDATE_PAINT;
        }
    }
    if (flags & UPDATE_INTERNALPAINT)
    {
        if (win->paint_flags & PAINT_INTERNAL)
        {
            ret |= UPDATE_INTERNALPAINT;
            if (win->paint_flags & PAINT_DELAYED_ERASE) ret |= UPDATE_DELAYED_ERASE;
        }
    }
    return ret;
}


/* iterate through the children of the given window until we find one with some update flags */
static unsigned int get_child_update_flags( struct window *win, struct window *from_child,
                                            unsigned int flags, struct window **child )
{
    struct window *ptr;
    unsigned int ret = 0;

    /* first make sure we want to iterate children at all */

    if (win->style & WS_MINIMIZE) return 0;

    /* note: the WS_CLIPCHILDREN test is the opposite of the invalidation case,
     * here we only want to repaint children of windows that clip them, others
     * need to wait for WM_PAINT to be done in the parent first.
     */
    if (!(flags & UPDATE_ALLCHILDREN) && !(win->style & WS_CLIPCHILDREN)) return 0;

    LIST_FOR_EACH_ENTRY( ptr, &win->children, struct window, entry )
    {
        if (from_child)  /* skip all children until from_child is found */
        {
            if (ptr == from_child) from_child = NULL;
            continue;
        }
        if (!(ptr->style & WS_VISIBLE)) continue;
        if ((ret = get_update_flags( ptr, flags )) != 0)
        {
            *child = ptr;
            break;
        }
        if ((ret = get_child_update_flags( ptr, NULL, flags, child ))) break;
    }
    return ret;
}

/* iterate through children and siblings of the given window until we find one with some update flags */
static unsigned int get_window_update_flags( struct window *win, struct window *from_child,
                                             unsigned int flags, struct window **child )
{
    unsigned int ret;
    struct window *ptr, *from_sibling = NULL;

    /* if some parent is not visible start from the next sibling */

    if (!is_visible( win )) return 0;
    for (ptr = from_child; ptr; ptr = ptr->parent)
    {
        if (!(ptr->style & WS_VISIBLE) || (ptr->style & WS_MINIMIZE)) from_sibling = ptr;
        if (ptr == win) break;
    }

    /* non-client painting must be delayed if one of the parents is going to
     * be repainted and doesn't clip children */

    if ((flags & UPDATE_NONCLIENT) && !(flags & (UPDATE_PAINT|UPDATE_INTERNALPAINT)))
    {
        for (ptr = win->parent; ptr; ptr = ptr->parent)
        {
            if (!(ptr->style & WS_CLIPCHILDREN) && win_needs_repaint( ptr ))
                return 0;
        }
        if (from_child && !(flags & UPDATE_ALLCHILDREN))
        {
            for (ptr = from_sibling ? from_sibling : from_child; ptr; ptr = ptr->parent)
            {
                if (!(ptr->style & WS_CLIPCHILDREN) && win_needs_repaint( ptr )) from_sibling = ptr;
                if (ptr == win) break;
            }
        }
    }


    /* check window itself (only if not restarting from a child) */

    if (!from_child)
    {
        if ((ret = get_update_flags( win, flags )))
        {
            *child = win;
            return ret;
        }
        from_child = win;
    }

    /* now check children */

    if (flags & UPDATE_NOCHILDREN) return 0;
    if (!from_sibling)
    {
        if ((ret = get_child_update_flags( from_child, NULL, flags, child ))) return ret;
        from_sibling = from_child;
    }

    /* then check siblings and parent siblings */

    while (from_sibling->parent && from_sibling != win)
    {
        if ((ret = get_child_update_flags( from_sibling->parent, from_sibling, flags, child )))
            return ret;
        from_sibling = from_sibling->parent;
    }
    return 0;
}


/* expose the areas revealed by a vis region change on the window parent */
/* returns the region exposed on the window itself (in client coordinates) */
static struct region *expose_window( struct window *win, const rectangle_t *old_window_rect,
                                     struct region *old_vis_rgn )
{
    struct region *new_vis_rgn, *exposed_rgn;

    if (!(new_vis_rgn = get_visible_region( win, DCX_WINDOW ))) return NULL;

    if ((exposed_rgn = create_empty_region()))
    {
        if (subtract_region( exposed_rgn, new_vis_rgn, old_vis_rgn ) && !is_region_empty( exposed_rgn ))
        {
            /* make it relative to the new client area */
            offset_region( exposed_rgn, win->window_rect.left - win->client_rect.left,
                           win->window_rect.top - win->client_rect.top );
        }
        else
        {
            free_region( exposed_rgn );
            exposed_rgn = NULL;
        }
    }

    if (win->parent && !is_desktop_window( win->parent ))
    {
        /* make it relative to the old window pos for subtracting */
        offset_region( new_vis_rgn, win->window_rect.left - old_window_rect->left,
                       win->window_rect.top - old_window_rect->top  );

        if ((win->parent->style & WS_CLIPCHILDREN) ?
            subtract_region( new_vis_rgn, old_vis_rgn, new_vis_rgn ) :
            xor_region( new_vis_rgn, old_vis_rgn, new_vis_rgn ))
        {
            if (!is_region_empty( new_vis_rgn ))
            {
                /* make it relative to parent */
                offset_region( new_vis_rgn, old_window_rect->left, old_window_rect->top );
                redraw_window( win->parent, new_vis_rgn, 0, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN );
            }
        }
    }
    free_region( new_vis_rgn );
    return exposed_rgn;
}


/* set the window and client rectangles, updating the update region if necessary */
static void set_window_pos( struct window *win, struct window *previous,
                            unsigned int swp_flags, const rectangle_t *window_rect,
                            const rectangle_t *client_rect, const rectangle_t *visible_rect,
                            const rectangle_t *valid_rects )
{
    struct region *old_vis_rgn = NULL, *exposed_rgn = NULL;
    const rectangle_t old_window_rect = win->window_rect;
    const rectangle_t old_visible_rect = win->visible_rect;
    const rectangle_t old_client_rect = win->client_rect;
    rectangle_t rect;
    int client_changed, frame_changed;
    int visible = (win->style & WS_VISIBLE) || (swp_flags & SWP_SHOWWINDOW);

    if (win->parent && !is_visible( win->parent )) visible = 0;

    if (visible && !(old_vis_rgn = get_visible_region( win, DCX_WINDOW ))) return;

    /* set the new window info before invalidating anything */

    win->window_rect  = *window_rect;
    win->visible_rect = *visible_rect;
    win->client_rect  = *client_rect;
    if (!(swp_flags & SWP_NOZORDER) && win->parent) link_window( win, previous );
    if (swp_flags & SWP_SHOWWINDOW) win->style |= WS_VISIBLE;
    else if (swp_flags & SWP_HIDEWINDOW) win->style &= ~WS_VISIBLE;

    /* keep children at the same position relative to top right corner when the parent is mirrored */
    if (win->ex_style & WS_EX_LAYOUTRTL)
    {
        struct window *child;
        int old_size = old_client_rect.right - old_client_rect.left;
        int new_size = win->client_rect.right - win->client_rect.left;

        if (old_size != new_size) LIST_FOR_EACH_ENTRY( child, &win->children, struct window, entry )
        {
            offset_rect( &child->window_rect, new_size - old_size, 0 );
            offset_rect( &child->visible_rect, new_size - old_size, 0 );
            offset_rect( &child->client_rect, new_size - old_size, 0 );
        }
    }

    /* reset cursor clip rectangle when the desktop changes size */
    if (win == win->desktop->top_window) win->desktop->cursor.clip = *window_rect;

    /* if the window is not visible, everything is easy */
    if (!visible) return;

    /* expose anything revealed by the change */

    if (!(swp_flags & SWP_NOREDRAW))
        exposed_rgn = expose_window( win, &old_window_rect, old_vis_rgn );

    if (!(win->style & WS_VISIBLE))
    {
        /* clear the update region since the window is no longer visible */
        validate_whole_window( win );
        validate_children( win );
        goto done;
    }

    /* crop update region to the new window rect */

    if (win->update_region)
    {
        if (get_window_visible_rect( win, &rect, 1 ))
        {
            struct region *tmp = create_empty_region();
            if (tmp)
            {
                set_region_rect( tmp, &rect );
                if (intersect_region( tmp, win->update_region, tmp ))
                    set_update_region( win, tmp );
                else
                    free_region( tmp );
            }
        }
        else set_update_region( win, NULL ); /* visible rect is empty */
    }

    /* crop children regions to the new window rect */

    if (get_window_visible_rect( win, &rect, 0 ))
    {
        /* map to client coords */
        offset_rect( &rect, win->window_rect.left - win->client_rect.left,
                     win->window_rect.top - win->client_rect.top );
        crop_children_update_region( win, &rect );
    }
    else crop_children_update_region( win, NULL );

    if (swp_flags & SWP_NOREDRAW) goto done;  /* do not repaint anything */

    /* expose the whole non-client area if it changed in any way */

    if (swp_flags & SWP_NOCOPYBITS)
    {
        frame_changed = ((swp_flags & SWP_FRAMECHANGED) ||
                         memcmp( window_rect, &old_window_rect, sizeof(old_window_rect) ) ||
                         memcmp( visible_rect, &old_visible_rect, sizeof(old_visible_rect) ));
        client_changed = memcmp( client_rect, &old_client_rect, sizeof(old_client_rect) );
    }
    else
    {
        /* assume the bits have been moved to follow the window rect */
        int x_offset = window_rect->left - old_window_rect.left;
        int y_offset = window_rect->top - old_window_rect.top;
        frame_changed = ((swp_flags & SWP_FRAMECHANGED) ||
                         window_rect->right  - old_window_rect.right != x_offset ||
                         window_rect->bottom - old_window_rect.bottom != y_offset ||
                         visible_rect->left   - old_visible_rect.left   != x_offset ||
                         visible_rect->right  - old_visible_rect.right  != x_offset ||
                         visible_rect->top    - old_visible_rect.top    != y_offset ||
                         visible_rect->bottom - old_visible_rect.bottom != y_offset);
        client_changed = (client_rect->left   - old_client_rect.left   != x_offset ||
                          client_rect->right  - old_client_rect.right  != x_offset ||
                          client_rect->top    - old_client_rect.top    != y_offset ||
                          client_rect->bottom - old_client_rect.bottom != y_offset ||
                          !valid_rects ||
                          memcmp( &valid_rects[0], client_rect, sizeof(*client_rect) ));
    }

    if (frame_changed || client_changed)
    {
        struct region *win_rgn = old_vis_rgn;  /* reuse previous region */

        set_region_rect( win_rgn, window_rect );
        if (valid_rects)
        {
            /* subtract the valid portion of client rect from the total region */
            struct region *tmp = create_empty_region();
            if (tmp)
            {
                set_region_rect( tmp, &valid_rects[0] );
                /* subtract update region since invalid parts of the valid rect won't be copied */
                if (win->update_region)
                {
                    offset_region( tmp, -window_rect->left, -window_rect->top );
                    subtract_region( tmp, tmp, win->update_region );
                    offset_region( tmp, window_rect->left, window_rect->top );
                }
                if (subtract_region( tmp, win_rgn, tmp )) win_rgn = tmp;
                else free_region( tmp );
            }
        }
        if (!is_desktop_window(win))
            offset_region( win_rgn, -client_rect->left, -client_rect->top );
        if (exposed_rgn)
        {
            union_region( exposed_rgn, exposed_rgn, win_rgn );
            if (win_rgn != old_vis_rgn) free_region( win_rgn );
        }
        else
        {
            exposed_rgn = win_rgn;
            if (win_rgn == old_vis_rgn) old_vis_rgn = NULL;
        }
    }

    if (exposed_rgn)
        redraw_window( win, exposed_rgn, 1, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN );

done:
    if (old_vis_rgn) free_region( old_vis_rgn );
    if (exposed_rgn) free_region( exposed_rgn );
    clear_error();  /* we ignore out of memory errors once the new rects have been set */
}


/* set the window region, updating the update region if necessary */
static void set_window_region( struct window *win, struct region *region, int redraw )
{
    struct region *old_vis_rgn = NULL, *exposed_rgn;

    /* no need to redraw if window is not visible */
    if (redraw && !is_visible( win )) redraw = 0;

    if (redraw) old_vis_rgn = get_visible_region( win, DCX_WINDOW );

    if (win->win_region) free_region( win->win_region );
    win->win_region = region;

    /* expose anything revealed by the change */
    if (old_vis_rgn && ((exposed_rgn = expose_window( win, &win->window_rect, old_vis_rgn ))))
    {
        redraw_window( win, exposed_rgn, 1, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN );
        free_region( exposed_rgn );
    }

    if (old_vis_rgn) free_region( old_vis_rgn );
    clear_error();  /* we ignore out of memory errors since the region has been set */
}


/* set the layer region */
static void set_layer_region( struct window *win, struct region *region )
{
    if (win->layer_region) free_region( win->layer_region );
    win->layer_region = region;
}


/* destroy a window */
void destroy_window( struct window *win )
{
    /* hide the window */
    if (is_visible(win))
    {
        struct region *vis_rgn = get_visible_region( win, DCX_WINDOW );
        win->style &= ~WS_VISIBLE;
        if (vis_rgn)
        {
            struct region *exposed_rgn = expose_window( win, &win->window_rect, vis_rgn );
            if (exposed_rgn) free_region( exposed_rgn );
            free_region( vis_rgn );
        }
        validate_whole_window( win );
        validate_children( win );
    }

    /* destroy all children */
    while (!list_empty(&win->children))
        destroy_window( LIST_ENTRY( list_head(&win->children), struct window, entry ));
    while (!list_empty(&win->unlinked))
        destroy_window( LIST_ENTRY( list_head(&win->unlinked), struct window, entry ));

    /* reset global window pointers, if the corresponding window is destroyed */
    if (win == shell_window) shell_window = NULL;
    if (win == shell_listview) shell_listview = NULL;
    if (win == progman_window) progman_window = NULL;
    if (win == taskman_window) taskman_window = NULL;
    free_hotkeys( win->desktop, win->handle );
    cleanup_clipboard_window( win->desktop, win->handle );
    free_user_handle( win->handle );
    destroy_properties( win );
    list_remove( &win->entry );
    if (is_desktop_window(win))
    {
        struct desktop *desktop = win->desktop;
        assert( desktop->top_window == win || desktop->msg_window == win );
        if (desktop->top_window == win) desktop->top_window = NULL;
        else desktop->msg_window = NULL;
    }
    else if (is_desktop_window( win->parent ))
    {
        post_message( win->parent->handle, WM_PARENTNOTIFY, WM_DESTROY, win->handle );
    }

    detach_window_thread( win );
    if (win->win_region) free_region( win->win_region );
    if (win->layer_region) free_region( win->layer_region );
    if (win->update_region) free_region( win->update_region );
    if (win->class) release_class( win->class );
    free( win->text );
    memset( win, 0x55, sizeof(*win) + win->nb_extra_bytes - 1 );
    free( win );
}


/* create a window */
DECL_HANDLER(create_window)
{
    struct window *win, *parent = NULL, *owner = NULL;
    struct unicode_str cls_name = get_req_unicode_str();
    atom_t atom;

    reply->handle = 0;
    if (req->parent && !(parent = get_window( req->parent ))) return;

    if (req->owner)
    {
        if (!(owner = get_window( req->owner ))) return;
        if (is_desktop_window(owner)) owner = NULL;
        else if (parent && !is_desktop_window(parent))
        {
            /* an owned window must be created as top-level */
            set_error( STATUS_ACCESS_DENIED );
            return;
        }
        else /* owner must be a top-level window */
            while ((owner->style & (WS_POPUP|WS_CHILD)) == WS_CHILD && !is_desktop_window(owner->parent))
                owner = owner->parent;
    }

    atom = cls_name.len ? find_global_atom( NULL, &cls_name ) : req->atom;

    if (!(win = create_window( parent, owner, atom, req->instance ))) return;

    reply->handle    = win->handle;
    reply->parent    = win->parent ? win->parent->handle : 0;
    reply->owner     = win->owner;
    reply->extra     = win->nb_extra_bytes;
    reply->class_ptr = get_class_client_ptr( win->class );
}


/* set the parent of a window */
DECL_HANDLER(set_parent)
{
    struct window *win, *parent = NULL;

    if (!(win = get_window( req->handle ))) return;
    if (req->parent && !(parent = get_window( req->parent ))) return;

    if (is_desktop_window(win))
    {
        set_error( STATUS_INVALID_PARAMETER );
        return;
    }
    reply->old_parent  = win->parent->handle;
    reply->full_parent = parent ? parent->handle : 0;
    set_parent_window( win, parent );
}


/* destroy a window */
DECL_HANDLER(destroy_window)
{
    struct window *win = get_window( req->handle );
    if (win)
    {
        if (!is_desktop_window(win)) destroy_window( win );
        else if (win->thread == current) detach_window_thread( win );
        else set_error( STATUS_ACCESS_DENIED );
    }
}


/* retrieve the desktop window for the current thread */
DECL_HANDLER(get_desktop_window)
{
    struct desktop *desktop = get_thread_desktop( current, 0 );
    int force;

    if (!desktop) return;

    /* if winstation is invisible, then avoid roundtrip */
    force = req->force || !(desktop->winstation->flags & WSF_VISIBLE);

    if (!desktop->top_window && force)  /* create it */
    {
        if ((desktop->top_window = create_window( NULL, NULL, DESKTOP_ATOM, 0 )))
        {
            detach_window_thread( desktop->top_window );
            desktop->top_window->style  = WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
        }
    }

    if (!desktop->msg_window && force)  /* create it */
    {
        static const WCHAR messageW[] = {'M','e','s','s','a','g','e'};
        static const struct unicode_str name = { messageW, sizeof(messageW) };
        atom_t atom = add_global_atom( NULL, &name );
        if (atom && (desktop->msg_window = create_window( NULL, NULL, atom, 0 )))
        {
            detach_window_thread( desktop->msg_window );
            desktop->msg_window->style = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
        }
    }

    reply->top_window = desktop->top_window ? desktop->top_window->handle : 0;
    reply->msg_window = desktop->msg_window ? desktop->msg_window->handle : 0;
    release_object( desktop );
}


/* set a window owner */
DECL_HANDLER(set_window_owner)
{
    struct window *win = get_window( req->handle );
    struct window *owner = NULL, *ptr;

    if (!win) return;
    if (req->owner && !(owner = get_window( req->owner ))) return;
    if (is_desktop_window(win))
    {
        set_error( STATUS_ACCESS_DENIED );
        return;
    }

    /* make sure owner is not a successor of window */
    for (ptr = owner; ptr; ptr = ptr->owner ? get_window( ptr->owner ) : NULL)
    {
        if (ptr == win)
        {
            set_error( STATUS_INVALID_PARAMETER );
            return;
        }
    }

    reply->prev_owner = win->owner;
    reply->full_owner = win->owner = owner ? owner->handle : 0;
}


/* get information from a window handle */
DECL_HANDLER(get_window_info)
{
    struct window *win = get_window( req->handle );

    reply->full_handle = 0;
    reply->tid = reply->pid = 0;
    if (win)
    {
        reply->full_handle = win->handle;
        reply->last_active = win->handle;
        reply->is_unicode  = win->is_unicode;
        if (get_user_object( win->last_active, USER_WINDOW )) reply->last_active = win->last_active;
        if (win->thread)
        {
            reply->tid  = get_thread_id( win->thread );
            reply->pid  = get_process_id( win->thread->process );
            reply->atom = win->class ? get_class_atom( win->class ) : DESKTOP_ATOM;
        }
    }
}


/* set some information in a window */
DECL_HANDLER(set_window_info)
{
    struct window *win = get_window( req->handle );

    if (!win) return;
    if (req->flags && is_desktop_window(win) && win->thread != current)
    {
        set_error( STATUS_ACCESS_DENIED );
        return;
    }
    if (req->extra_size > sizeof(req->extra_value) ||
        req->extra_offset < -1 ||
        req->extra_offset > win->nb_extra_bytes - (int)req->extra_size)
    {
        set_win32_error( ERROR_INVALID_INDEX );
        return;
    }
    if (req->extra_offset != -1)
    {
        memcpy( &reply->old_extra_value, win->extra_bytes + req->extra_offset, req->extra_size );
    }
    else if (req->flags & SET_WIN_EXTRA)
    {
        set_win32_error( ERROR_INVALID_INDEX );
        return;
    }
    reply->old_style     = win->style;
    reply->old_ex_style  = win->ex_style;
    reply->old_id        = win->id;
    reply->old_instance  = win->instance;
    reply->old_user_data = win->user_data;
    if (req->flags & SET_WIN_STYLE) win->style = req->style;
    if (req->flags & SET_WIN_EXSTYLE)
    {
        /* WS_EX_TOPMOST can only be changed for unlinked windows */
        if (!win->is_linked) win->ex_style = req->ex_style;
        else win->ex_style = (req->ex_style & ~WS_EX_TOPMOST) | (win->ex_style & WS_EX_TOPMOST);
        if (!(win->ex_style & WS_EX_LAYERED)) win->is_layered = 0;
    }
    if (req->flags & SET_WIN_ID) win->id = req->id;
    if (req->flags & SET_WIN_INSTANCE) win->instance = req->instance;
    if (req->flags & SET_WIN_UNICODE) win->is_unicode = req->is_unicode;
    if (req->flags & SET_WIN_USERDATA) win->user_data = req->user_data;
    if (req->flags & SET_WIN_EXTRA) memcpy( win->extra_bytes + req->extra_offset,
                                            &req->extra_value, req->extra_size );

    /* changing window style triggers a non-client paint */
    if (req->flags & SET_WIN_STYLE) win->paint_flags |= PAINT_NONCLIENT;
}


/* get a list of the window parents, up to the root of the tree */
DECL_HANDLER(get_window_parents)
{
    struct window *ptr, *win = get_window( req->handle );
    int total = 0;
    user_handle_t *data;
    data_size_t len;

    if (win) for (ptr = win->parent; ptr; ptr = ptr->parent) total++;

    reply->count = total;
    len = min( get_reply_max_size(), total * sizeof(user_handle_t) );
    if (len && ((data = set_reply_data_size( len ))))
    {
        for (ptr = win->parent; ptr && len; ptr = ptr->parent, len -= sizeof(*data))
            *data++ = ptr->handle;
    }
}


/* get a list of the window children */
DECL_HANDLER(get_window_children)
{
    struct window *parent = NULL;
    unsigned int total;
    user_handle_t *data;
    data_size_t len;
    struct unicode_str cls_name = get_req_unicode_str();
    atom_t atom = req->atom;
    struct desktop *desktop = NULL;

    if (cls_name.len && !(atom = find_global_atom( NULL, &cls_name ))) return;

    if (req->desktop)
    {
        if (!(desktop = get_desktop_obj( current->process, req->desktop, DESKTOP_ENUMERATE ))) return;
        parent = desktop->top_window;
    }
    else
    {
        if (req->parent && !(parent = get_window( req->parent ))) return;
        if (!parent && !(desktop = get_thread_desktop( current, 0 ))) return;
    }

    if (parent)
        total = get_children_windows( parent, atom, req->tid, NULL, 0 );
    else
        total = get_children_windows( desktop->top_window, atom, req->tid, NULL, 0 ) +
                get_children_windows( desktop->msg_window, atom, req->tid, NULL, 0 );

    reply->count = total;
    len = min( get_reply_max_size(), total * sizeof(user_handle_t) );
    if (len && ((data = set_reply_data_size( len ))))
    {
        if (parent) get_children_windows( parent, atom, req->tid, data, len / sizeof(user_handle_t) );
        else
        {
            total = get_children_windows( desktop->top_window, atom, req->tid,
                                          data, len / sizeof(user_handle_t) );
            data += total;
            len -= total * sizeof(user_handle_t);
            if (len >= sizeof(user_handle_t))
                get_children_windows( desktop->msg_window, atom, req->tid,
                                      data, len / sizeof(user_handle_t) );
        }
    }
    if (desktop) release_object( desktop );
}


/* get a list of the window children that contain a given point */
DECL_HANDLER(get_window_children_from_point)
{
    struct user_handle_array array;
    struct window *parent = get_window( req->parent );
    data_size_t len;

    if (!parent) return;

    array.handles = NULL;
    array.count = 0;
    array.total = 0;
    if (!all_windows_from_point( parent, req->x, req->y, &array )) return;

    reply->count = array.count;
    len = min( get_reply_max_size(), array.count * sizeof(user_handle_t) );
    if (len) set_reply_data_ptr( array.handles, len );
    else free( array.handles );
}


/* get window tree information from a window handle */
DECL_HANDLER(get_window_tree)
{
    struct window *ptr, *win = get_window( req->handle );

    if (!win) return;

    reply->parent        = 0;
    reply->owner         = 0;
    reply->next_sibling  = 0;
    reply->prev_sibling  = 0;
    reply->first_sibling = 0;
    reply->last_sibling  = 0;
    reply->first_child   = 0;
    reply->last_child    = 0;

    if (win->parent)
    {
        struct window *parent = win->parent;
        reply->parent = parent->handle;
        reply->owner  = win->owner;
        if (win->is_linked)
        {
            if ((ptr = get_next_window( win ))) reply->next_sibling = ptr->handle;
            if ((ptr = get_prev_window( win ))) reply->prev_sibling = ptr->handle;
        }
        if ((ptr = get_first_child( parent ))) reply->first_sibling = ptr->handle;
        if ((ptr = get_last_child( parent ))) reply->last_sibling = ptr->handle;
    }
    if ((ptr = get_first_child( win ))) reply->first_child = ptr->handle;
    if ((ptr = get_last_child( win ))) reply->last_child = ptr->handle;
}


/* set the position and Z order of a window */
DECL_HANDLER(set_window_pos)
{
    rectangle_t window_rect, client_rect, visible_rect;
    struct window *previous = NULL;
    struct window *top, *win = get_window( req->handle );
    unsigned int flags = req->swp_flags;

    if (!win) return;
    if (!win->parent) flags |= SWP_NOZORDER;  /* no Z order for the desktop */

    if (!(flags & SWP_NOZORDER))
    {
        switch ((int)req->previous)
        {
        case 0:   /* HWND_TOP */
            previous = WINPTR_TOP;
            break;
        case 1:   /* HWND_BOTTOM */
            previous = WINPTR_BOTTOM;
            break;
        case -1:  /* HWND_TOPMOST */
            previous = WINPTR_TOPMOST;
            break;
        case -2:  /* HWND_NOTOPMOST */
            previous = WINPTR_NOTOPMOST;
            break;
        default:
            if (!(previous = get_window( req->previous ))) return;
            /* previous must be a sibling */
            if (previous->parent != win->parent)
            {
                set_error( STATUS_INVALID_PARAMETER );
                return;
            }
            break;
        }
        if (previous == win) flags |= SWP_NOZORDER;  /* nothing to do */
    }

    /* windows that use UpdateLayeredWindow don't trigger repaints */
    if ((win->ex_style & WS_EX_LAYERED) && !win->is_layered) flags |= SWP_NOREDRAW;

    /* window rectangle must be ordered properly */
    if (req->window.right < req->window.left || req->window.bottom < req->window.top)
    {
        set_error( STATUS_INVALID_PARAMETER );
        return;
    }

    window_rect = visible_rect = req->window;
    client_rect = req->client;
    if (get_req_data_size() >= sizeof(rectangle_t))
        memcpy( &visible_rect, get_req_data(), sizeof(rectangle_t) );
    if (win->parent && win->parent->ex_style & WS_EX_LAYOUTRTL)
    {
        mirror_rect( &win->parent->client_rect, &window_rect );
        mirror_rect( &win->parent->client_rect, &visible_rect );
        mirror_rect( &win->parent->client_rect, &client_rect );
    }

    win->paint_flags = (win->paint_flags & ~PAINT_CLIENT_FLAGS) | (req->paint_flags & PAINT_CLIENT_FLAGS);
    if (win->paint_flags & PAINT_HAS_PIXEL_FORMAT) update_pixel_format_flags( win );

    if (get_req_data_size() >= 3 * sizeof(rectangle_t))
    {
        rectangle_t valid_rects[2];
        memcpy( valid_rects, (const rectangle_t *)get_req_data() + 1, 2 * sizeof(rectangle_t) );
        if (win->parent && win->parent->ex_style & WS_EX_LAYOUTRTL)
        {
            mirror_rect( &win->parent->client_rect, &valid_rects[0] );
            mirror_rect( &win->parent->client_rect, &valid_rects[1] );
        }
        set_window_pos( win, previous, flags, &window_rect, &client_rect, &visible_rect, valid_rects );
    }
    else set_window_pos( win, previous, flags, &window_rect, &client_rect, &visible_rect, NULL );

    reply->new_style = win->style;
    reply->new_ex_style = win->ex_style;

    top = get_top_clipping_window( win );
    if (is_visible( top ) && (top->paint_flags & PAINT_HAS_SURFACE))
    {
        reply->surface_win = top->handle;
        reply->needs_update = !!(top->paint_flags & (PAINT_HAS_PIXEL_FORMAT | PAINT_PIXEL_FORMAT_CHILD));
    }
}


/* get the window and client rectangles of a window */
DECL_HANDLER(get_window_rectangles)
{
    struct window *win = get_window( req->handle );

    if (!win) return;

    reply->window  = win->window_rect;
    reply->visible = win->visible_rect;
    reply->client  = win->client_rect;

    switch (req->relative)
    {
    case COORDS_CLIENT:
        offset_rect( &reply->window, -win->client_rect.left, -win->client_rect.top );
        offset_rect( &reply->visible, -win->client_rect.left, -win->client_rect.top );
        offset_rect( &reply->client, -win->client_rect.left, -win->client_rect.top );
        if (win->ex_style & WS_EX_LAYOUTRTL)
        {
            mirror_rect( &win->client_rect, &reply->window );
            mirror_rect( &win->client_rect, &reply->visible );
        }
        break;
    case COORDS_WINDOW:
        offset_rect( &reply->window, -win->window_rect.left, -win->window_rect.top );
        offset_rect( &reply->visible, -win->window_rect.left, -win->window_rect.top );
        offset_rect( &reply->client, -win->window_rect.left, -win->window_rect.top );
        if (win->ex_style & WS_EX_LAYOUTRTL)
        {
            mirror_rect( &win->window_rect, &reply->visible );
            mirror_rect( &win->window_rect, &reply->client );
        }
        break;
    case COORDS_PARENT:
        if (win->parent && win->parent->ex_style & WS_EX_LAYOUTRTL)
        {
            mirror_rect( &win->parent->client_rect, &reply->window );
            mirror_rect( &win->parent->client_rect, &reply->visible );
            mirror_rect( &win->parent->client_rect, &reply->client );
        }
        break;
    case COORDS_SCREEN:
        client_to_screen_rect( win->parent, &reply->window );
        client_to_screen_rect( win->parent, &reply->visible );
        client_to_screen_rect( win->parent, &reply->client );
        break;
    default:
        set_error( STATUS_INVALID_PARAMETER );
        break;
    }
}


/* get the window text */
DECL_HANDLER(get_window_text)
{
    struct window *win = get_window( req->handle );

    if (win && win->text)
    {
        data_size_t len = strlenW( win->text ) * sizeof(WCHAR);
        if (len > get_reply_max_size()) len = get_reply_max_size();
        set_reply_data( win->text, len );
    }
}


/* set the window text */
DECL_HANDLER(set_window_text)
{
    struct window *win = get_window( req->handle );

    if (win)
    {
        WCHAR *text = NULL;
        data_size_t len = get_req_data_size() / sizeof(WCHAR);
        if (len)
        {
            if (!(text = mem_alloc( (len+1) * sizeof(WCHAR) ))) return;
            memcpy( text, get_req_data(), len * sizeof(WCHAR) );
            text[len] = 0;
        }
        free( win->text );
        win->text = text;
    }
}


/* get the coordinates offset between two windows */
DECL_HANDLER(get_windows_offset)
{
    struct window *win;
    int mirror_from = 0, mirror_to = 0;

    reply->x = reply->y = 0;
    if (req->from)
    {
        if (!(win = get_window( req->from ))) return;
        if (win->ex_style & WS_EX_LAYOUTRTL)
        {
            mirror_from = 1;
            reply->x += win->client_rect.right - win->client_rect.left;
        }
        while (win && !is_desktop_window(win))
        {
            reply->x += win->client_rect.left;
            reply->y += win->client_rect.top;
            win = win->parent;
        }
    }
    if (req->to)
    {
        if (!(win = get_window( req->to ))) return;
        if (win->ex_style & WS_EX_LAYOUTRTL)
        {
            mirror_to = 1;
            reply->x -= win->client_rect.right - win->client_rect.left;
        }
        while (win && !is_desktop_window(win))
        {
            reply->x -= win->client_rect.left;
            reply->y -= win->client_rect.top;
            win = win->parent;
        }
    }
    if (mirror_from) reply->x = -reply->x;
    reply->mirror = mirror_from ^ mirror_to;
}


/* get the visible region of a window */
DECL_HANDLER(get_visible_region)
{
    struct region *region;
    struct window *top, *win = get_window( req->window );

    if (!win) return;

    top = get_top_clipping_window( win );
    if ((region = get_visible_region( win, req->flags )))
    {
        rectangle_t *data;
        map_win_region_to_screen( win, region );
        data = get_region_data_and_free( region, get_reply_max_size(), &reply->total_size );
        if (data) set_reply_data_ptr( data, reply->total_size );
    }
    reply->top_win  = top->handle;
    reply->top_rect = top->visible_rect;

    if (!is_desktop_window(win))
    {
        reply->win_rect = (req->flags & DCX_WINDOW) ? win->window_rect : win->client_rect;
        client_to_screen_rect( top->parent, &reply->top_rect );
        client_to_screen_rect( win->parent, &reply->win_rect );
    }
    else
    {
        reply->win_rect.left   = 0;
        reply->win_rect.top    = 0;
        reply->win_rect.right  = win->client_rect.right - win->client_rect.left;
        reply->win_rect.bottom = win->client_rect.bottom - win->client_rect.top;
    }
    reply->paint_flags = win->paint_flags & PAINT_CLIENT_FLAGS;
}


/* get the surface visible region of a window */
DECL_HANDLER(get_surface_region)
{
    struct region *region;
    struct window *win = get_window( req->window );

    if (!win || !is_visible( win )) return;

    if ((region = get_surface_region( win )))
    {
        rectangle_t *data;
        if (win->parent) map_win_region_to_screen( win->parent, region );
        data = get_region_data_and_free( region, get_reply_max_size(), &reply->total_size );
        if (data) set_reply_data_ptr( data, reply->total_size );
    }
    reply->visible_rect = win->visible_rect;
    if (win->parent) client_to_screen_rect( win->parent, &reply->visible_rect );
}


/* get the window region */
DECL_HANDLER(get_window_region)
{
    rectangle_t *data;
    struct window *win = get_window( req->window );

    if (!win) return;
    if (!win->win_region) return;

    if (win->ex_style & WS_EX_LAYOUTRTL)
    {
        struct region *region = create_empty_region();

        if (!region) return;
        if (!copy_region( region, win->win_region ))
        {
            free_region( region );
            return;
        }
        mirror_region( &win->window_rect, region );
        data = get_region_data_and_free( region, get_reply_max_size(), &reply->total_size );
    }
    else data = get_region_data( win->win_region, get_reply_max_size(), &reply->total_size );

    if (data) set_reply_data_ptr( data, reply->total_size );
}


/* set the window region */
DECL_HANDLER(set_window_region)
{
    struct region *region = NULL;
    struct window *win = get_window( req->window );

    if (!win) return;

    if (get_req_data_size())  /* no data means remove the region completely */
    {
        if (!(region = create_region_from_req_data( get_req_data(), get_req_data_size() )))
            return;
        if (win->ex_style & WS_EX_LAYOUTRTL) mirror_region( &win->window_rect, region );
    }
    set_window_region( win, region, req->redraw );
}


/* set the layer region */
DECL_HANDLER(set_layer_region)
{
    struct region *region = NULL;
    struct window *win = get_window( req->window );

    if (!win) return;

    if (get_req_data_size())  /* no data means remove the region completely */
    {
        if (!(region = create_region_from_req_data( get_req_data(), get_req_data_size() )))
            return;
        if (win->ex_style & WS_EX_LAYOUTRTL) mirror_region( &win->window_rect, region );
    }
    set_layer_region( win, region );
}


/* get a window update region */
DECL_HANDLER(get_update_region)
{
    rectangle_t *data;
    unsigned int flags = req->flags;
    struct window *from_child = NULL;
    struct window *win = get_window( req->window );

    reply->flags = 0;
    if (!win) return;

    if (req->from_child)
    {
        struct window *ptr;

        if (!(from_child = get_window( req->from_child ))) return;

        /* make sure from_child is a child of win */
        ptr = from_child;
        while (ptr && ptr != win) ptr = ptr->parent;
        if (!ptr)
        {
            set_error( STATUS_INVALID_PARAMETER );
            return;
        }
    }

    if (flags & UPDATE_DELAYED_ERASE)  /* this means that the previous call didn't erase */
    {
        if (from_child) from_child->paint_flags |= PAINT_DELAYED_ERASE;
        else win->paint_flags |= PAINT_DELAYED_ERASE;
    }

    reply->flags = get_window_update_flags( win, from_child, flags, &win );
    reply->child = win->handle;

    if (flags & UPDATE_NOREGION) return;

    if (win->update_region)
    {
        /* convert update region to screen coordinates */
        struct region *region = create_empty_region();

        if (!region) return;
        if (!copy_region( region, win->update_region ))
        {
            free_region( region );
            return;
        }
        if ((flags & UPDATE_CLIPCHILDREN) && (win->style & WS_CLIPCHILDREN))
            clip_children( win, NULL, region, win->client_rect.left - win->window_rect.left,
                           win->client_rect.top - win->window_rect.top );
        map_win_region_to_screen( win, region );
        if (!(data = get_region_data_and_free( region, get_reply_max_size(),
                                               &reply->total_size ))) return;
        set_reply_data_ptr( data, reply->total_size );
    }

    if (reply->flags & (UPDATE_PAINT|UPDATE_INTERNALPAINT)) /* validate everything */
    {
        validate_parents( win );
        validate_whole_window( win );
    }
    else
    {
        if (reply->flags & UPDATE_NONCLIENT) validate_non_client( win );
        if (reply->flags & UPDATE_ERASE)
        {
            win->paint_flags &= ~(PAINT_ERASE | PAINT_DELAYED_ERASE);
            /* desktop window only gets erased, not repainted */
            if (is_desktop_window(win)) validate_whole_window( win );
        }
    }
}


/* update the z order of a window so that a given rectangle is fully visible */
DECL_HANDLER(update_window_zorder)
{
    rectangle_t tmp, rect = req->rect;
    struct window *ptr, *win = get_window( req->window );

    if (!win || !win->parent || !is_visible( win )) return;  /* nothing to do */
    if (win->ex_style & WS_EX_LAYOUTRTL) mirror_rect( &win->client_rect, &rect );
    offset_rect( &rect, win->client_rect.left, win->client_rect.top );

    LIST_FOR_EACH_ENTRY( ptr, &win->parent->children, struct window, entry )
    {
        if (ptr == win) break;
        if (!(ptr->style & WS_VISIBLE)) continue;
        if (ptr->ex_style & WS_EX_TRANSPARENT) continue;
        if (ptr->is_layered && (ptr->layered_flags & LWA_COLORKEY)) continue;
        if (!intersect_rect( &tmp, &ptr->visible_rect, &rect )) continue;
        if (ptr->win_region)
        {
            tmp = rect;
            offset_rect( &tmp, -ptr->window_rect.left, -ptr->window_rect.top );
            if (!rect_in_region( ptr->win_region, &tmp )) continue;
        }
        /* found a window obscuring the rectangle, now move win above this one */
        /* making sure to not violate the topmost rule */
        if (!(ptr->ex_style & WS_EX_TOPMOST) || (win->ex_style & WS_EX_TOPMOST))
        {
            list_remove( &win->entry );
            list_add_before( &ptr->entry, &win->entry );
        }
        break;
    }
}


/* mark parts of a window as needing a redraw */
DECL_HANDLER(redraw_window)
{
    struct region *region = NULL;
    struct window *win = get_window( req->window );

    if (!win) return;
    if (!is_visible( win )) return;  /* nothing to do */

    if (req->flags & (RDW_VALIDATE|RDW_INVALIDATE))
    {
        if (get_req_data_size())  /* no data means whole rectangle */
        {
            if (!(region = create_region_from_req_data( get_req_data(), get_req_data_size() )))
                return;
            if (win->ex_style & WS_EX_LAYOUTRTL) mirror_region( &win->client_rect, region );
        }
    }

    redraw_window( win, region, (req->flags & RDW_INVALIDATE) && (req->flags & RDW_FRAME),
                   req->flags );
    if (region) free_region( region );
}


/* set a window property */
DECL_HANDLER(set_window_property)
{
    struct unicode_str name = get_req_unicode_str();
    struct window *win = get_window( req->window );

    if (!win) return;

    if (name.len)
    {
        atom_t atom = add_global_atom( NULL, &name );
        if (atom)
        {
            set_property( win, atom, req->data, PROP_TYPE_STRING );
            release_global_atom( NULL, atom );
        }
    }
    else set_property( win, req->atom, req->data, PROP_TYPE_ATOM );
}


/* remove a window property */
DECL_HANDLER(remove_window_property)
{
    struct unicode_str name = get_req_unicode_str();
    struct window *win = get_window( req->window );

    if (win)
    {
        atom_t atom = name.len ? find_global_atom( NULL, &name ) : req->atom;
        if (atom) reply->data = remove_property( win, atom );
    }
}


/* get a window property */
DECL_HANDLER(get_window_property)
{
    struct unicode_str name = get_req_unicode_str();
    struct window *win = get_window( req->window );

    if (win)
    {
        atom_t atom = name.len ? find_global_atom( NULL, &name ) : req->atom;
        if (atom) reply->data = get_property( win, atom );
    }
}


/* get the list of properties of a window */
DECL_HANDLER(get_window_properties)
{
    property_data_t *data;
    int i, count, max = get_reply_max_size() / sizeof(*data);
    struct window *win = get_window( req->window );

    reply->total = 0;
    if (!win) return;

    for (i = count = 0; i < win->prop_inuse; i++)
        if (win->properties[i].type != PROP_TYPE_FREE) count++;
    reply->total = count;

    if (count > max) count = max;
    if (!count || !(data = set_reply_data_size( count * sizeof(*data) ))) return;

    for (i = 0; i < win->prop_inuse && count; i++)
    {
        if (win->properties[i].type == PROP_TYPE_FREE) continue;
        data->atom   = win->properties[i].atom;
        data->string = (win->properties[i].type == PROP_TYPE_STRING);
        data->data   = win->properties[i].data;
        data++;
        count--;
    }
}


/* get the new window pointer for a global window, checking permissions */
/* helper for set_global_windows request */
static int get_new_global_window( struct window **win, user_handle_t handle )
{
    if (!handle)
    {
        *win = NULL;
        return 1;
    }
    else if (*win)
    {
        set_error( STATUS_ACCESS_DENIED );
        return 0;
    }
    *win = get_window( handle );
    return (*win != NULL);
}

/* Set/get the global windows */
DECL_HANDLER(set_global_windows)
{
    struct window *new_shell_window   = shell_window;
    struct window *new_shell_listview = shell_listview;
    struct window *new_progman_window = progman_window;
    struct window *new_taskman_window = taskman_window;

    reply->old_shell_window   = shell_window ? shell_window->handle : 0;
    reply->old_shell_listview = shell_listview ? shell_listview->handle : 0;
    reply->old_progman_window = progman_window ? progman_window->handle : 0;
    reply->old_taskman_window = taskman_window ? taskman_window->handle : 0;

    if (req->flags & SET_GLOBAL_SHELL_WINDOWS)
    {
        if (!get_new_global_window( &new_shell_window, req->shell_window )) return;
        if (!get_new_global_window( &new_shell_listview, req->shell_listview )) return;
    }
    if (req->flags & SET_GLOBAL_PROGMAN_WINDOW)
    {
        if (!get_new_global_window( &new_progman_window, req->progman_window )) return;
    }
    if (req->flags & SET_GLOBAL_TASKMAN_WINDOW)
    {
        if (!get_new_global_window( &new_taskman_window, req->taskman_window )) return;
    }
    shell_window   = new_shell_window;
    shell_listview = new_shell_listview;
    progman_window = new_progman_window;
    taskman_window = new_taskman_window;
}

/* retrieve layered info for a window */
DECL_HANDLER(get_window_layered_info)
{
    struct window *win = get_window( req->handle );

    if (!win) return;

    if (win->is_layered)
    {
        reply->color_key = win->color_key;
        reply->alpha     = win->alpha;
        reply->flags     = win->layered_flags;
    }
    else set_win32_error( ERROR_INVALID_WINDOW_HANDLE );
}


/* set layered info for a window */
DECL_HANDLER(set_window_layered_info)
{
    struct window *win = get_window( req->handle );

    if (!win) return;

    if (win->ex_style & WS_EX_LAYERED)
    {
        int was_layered = win->is_layered;

        if (req->flags & LWA_ALPHA) win->alpha = req->alpha;
        else if (!win->is_layered) win->alpha = 0;  /* alpha init value is 0 */

        win->color_key     = req->color_key;
        win->layered_flags = req->flags;
        win->is_layered    = 1;
        /* repaint since we know now it's not going to use UpdateLayeredWindow */
        if (!was_layered) redraw_window( win, 0, 1, RDW_ALLCHILDREN | RDW_INVALIDATE | RDW_ERASE | RDW_FRAME );
    }
    else set_win32_error( ERROR_INVALID_WINDOW_HANDLE );
}
