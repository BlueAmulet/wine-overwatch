/*
 * GTK uxtheme implementation
 *
 * Copyright (C) 2015 Michael Müller
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
#include "wine/library.h"

#include "wine/debug.h"
#include "uxthemegtk.h"

WINE_DEFAULT_DEBUG_CHANNEL(uxthemegtk);

static void *libgtk3 = NULL;
static void *libcairo = NULL;
static void *libgobject2 = NULL;

#define MAKE_FUNCPTR(f) typeof(f) * p##f = NULL
MAKE_FUNCPTR(cairo_create);
MAKE_FUNCPTR(cairo_destroy);
MAKE_FUNCPTR(cairo_image_surface_create);
MAKE_FUNCPTR(cairo_image_surface_get_data);
MAKE_FUNCPTR(cairo_image_surface_get_stride);
MAKE_FUNCPTR(cairo_surface_destroy);
MAKE_FUNCPTR(cairo_surface_flush);
MAKE_FUNCPTR(g_type_check_instance_is_a);
MAKE_FUNCPTR(gtk_bin_get_child);
MAKE_FUNCPTR(gtk_button_new);
MAKE_FUNCPTR(gtk_check_button_new);
MAKE_FUNCPTR(gtk_combo_box_new_with_entry);
MAKE_FUNCPTR(gtk_container_add);
MAKE_FUNCPTR(gtk_container_forall);
MAKE_FUNCPTR(gtk_entry_new);
MAKE_FUNCPTR(gtk_fixed_new);
MAKE_FUNCPTR(gtk_frame_new);
MAKE_FUNCPTR(gtk_init);
MAKE_FUNCPTR(gtk_label_new);
MAKE_FUNCPTR(gtk_menu_bar_new);
MAKE_FUNCPTR(gtk_menu_item_new);
MAKE_FUNCPTR(gtk_menu_item_set_submenu);
MAKE_FUNCPTR(gtk_menu_new);
MAKE_FUNCPTR(gtk_menu_shell_append);
MAKE_FUNCPTR(gtk_notebook_new);
MAKE_FUNCPTR(gtk_radio_button_new);
MAKE_FUNCPTR(gtk_render_arrow);
MAKE_FUNCPTR(gtk_render_background);
MAKE_FUNCPTR(gtk_render_check);
MAKE_FUNCPTR(gtk_render_frame);
MAKE_FUNCPTR(gtk_render_handle);
MAKE_FUNCPTR(gtk_render_line);
MAKE_FUNCPTR(gtk_render_option);
MAKE_FUNCPTR(gtk_render_slider);
MAKE_FUNCPTR(gtk_scale_new);
MAKE_FUNCPTR(gtk_scrolled_window_new);
MAKE_FUNCPTR(gtk_separator_tool_item_new);
MAKE_FUNCPTR(gtk_style_context_add_class);
MAKE_FUNCPTR(gtk_style_context_add_region);
MAKE_FUNCPTR(gtk_style_context_get_background_color);
MAKE_FUNCPTR(gtk_style_context_get_border_color);
MAKE_FUNCPTR(gtk_style_context_get_color);
MAKE_FUNCPTR(gtk_style_context_remove_class);
MAKE_FUNCPTR(gtk_style_context_restore);
MAKE_FUNCPTR(gtk_style_context_save);
MAKE_FUNCPTR(gtk_style_context_set_junction_sides);
MAKE_FUNCPTR(gtk_style_context_set_state);
MAKE_FUNCPTR(gtk_toggle_button_get_type);
MAKE_FUNCPTR(gtk_toolbar_new);
MAKE_FUNCPTR(gtk_tree_view_append_column);
MAKE_FUNCPTR(gtk_tree_view_column_get_button);
MAKE_FUNCPTR(gtk_tree_view_column_new);
MAKE_FUNCPTR(gtk_tree_view_get_column);
MAKE_FUNCPTR(gtk_tree_view_new);
MAKE_FUNCPTR(gtk_widget_destroy);
MAKE_FUNCPTR(gtk_widget_get_style_context);
MAKE_FUNCPTR(gtk_widget_style_get);
MAKE_FUNCPTR(gtk_window_new);
#undef MAKE_FUNCPTR

static void free_gtk3_libs(void)
{
    if (libgtk3) wine_dlclose(libgtk3, NULL, 0);
    if (libcairo) wine_dlclose(libcairo, NULL, 0);
    if (libgobject2) wine_dlclose(libgobject2, NULL, 0);
    libgtk3 = libcairo = libgobject2 = NULL;
}

#define LOAD_FUNCPTR(lib, f) \
    if(!(p##f = wine_dlsym(lib, #f, NULL, 0))) \
    { \
        WARN("Can't find symbol %s.\n", #f); \
        goto error; \
    }

static BOOL load_gtk3_libs(void)
{
    libgtk3 = wine_dlopen(SONAME_LIBGTK_3, RTLD_NOW, NULL, 0);
    if (!libgtk3)
    {
        FIXME("Wine cannot find the %s library.\n", SONAME_LIBGTK_3);
        goto error;
    }

    LOAD_FUNCPTR(libgtk3, gtk_bin_get_child)
    LOAD_FUNCPTR(libgtk3, gtk_button_new)
    LOAD_FUNCPTR(libgtk3, gtk_check_button_new)
    LOAD_FUNCPTR(libgtk3, gtk_combo_box_new_with_entry)
    LOAD_FUNCPTR(libgtk3, gtk_container_add)
    LOAD_FUNCPTR(libgtk3, gtk_container_forall)
    LOAD_FUNCPTR(libgtk3, gtk_entry_new)
    LOAD_FUNCPTR(libgtk3, gtk_fixed_new)
    LOAD_FUNCPTR(libgtk3, gtk_frame_new)
    LOAD_FUNCPTR(libgtk3, gtk_init)
    LOAD_FUNCPTR(libgtk3, gtk_label_new)
    LOAD_FUNCPTR(libgtk3, gtk_menu_bar_new)
    LOAD_FUNCPTR(libgtk3, gtk_menu_item_new)
    LOAD_FUNCPTR(libgtk3, gtk_menu_item_set_submenu)
    LOAD_FUNCPTR(libgtk3, gtk_menu_new)
    LOAD_FUNCPTR(libgtk3, gtk_menu_shell_append)
    LOAD_FUNCPTR(libgtk3, gtk_notebook_new)
    LOAD_FUNCPTR(libgtk3, gtk_radio_button_new)
    LOAD_FUNCPTR(libgtk3, gtk_render_arrow)
    LOAD_FUNCPTR(libgtk3, gtk_render_background)
    LOAD_FUNCPTR(libgtk3, gtk_render_check)
    LOAD_FUNCPTR(libgtk3, gtk_render_frame)
    LOAD_FUNCPTR(libgtk3, gtk_render_handle)
    LOAD_FUNCPTR(libgtk3, gtk_render_line)
    LOAD_FUNCPTR(libgtk3, gtk_render_option)
    LOAD_FUNCPTR(libgtk3, gtk_render_slider)
    LOAD_FUNCPTR(libgtk3, gtk_scale_new)
    LOAD_FUNCPTR(libgtk3, gtk_scrolled_window_new)
    LOAD_FUNCPTR(libgtk3, gtk_separator_tool_item_new)
    LOAD_FUNCPTR(libgtk3, gtk_style_context_add_class)
    LOAD_FUNCPTR(libgtk3, gtk_style_context_add_region)
    LOAD_FUNCPTR(libgtk3, gtk_style_context_get_background_color)
    LOAD_FUNCPTR(libgtk3, gtk_style_context_get_border_color)
    LOAD_FUNCPTR(libgtk3, gtk_style_context_get_color)
    LOAD_FUNCPTR(libgtk3, gtk_style_context_remove_class)
    LOAD_FUNCPTR(libgtk3, gtk_style_context_restore)
    LOAD_FUNCPTR(libgtk3, gtk_style_context_save)
    LOAD_FUNCPTR(libgtk3, gtk_style_context_set_junction_sides)
    LOAD_FUNCPTR(libgtk3, gtk_style_context_set_state)
    LOAD_FUNCPTR(libgtk3, gtk_toggle_button_get_type)
    LOAD_FUNCPTR(libgtk3, gtk_toolbar_new)
    LOAD_FUNCPTR(libgtk3, gtk_tree_view_append_column)
    LOAD_FUNCPTR(libgtk3, gtk_tree_view_column_get_button)
    LOAD_FUNCPTR(libgtk3, gtk_tree_view_column_new)
    LOAD_FUNCPTR(libgtk3, gtk_tree_view_get_column)
    LOAD_FUNCPTR(libgtk3, gtk_tree_view_new)
    LOAD_FUNCPTR(libgtk3, gtk_widget_destroy)
    LOAD_FUNCPTR(libgtk3, gtk_widget_get_style_context)
    LOAD_FUNCPTR(libgtk3, gtk_widget_style_get)
    LOAD_FUNCPTR(libgtk3, gtk_window_new)

    libcairo = wine_dlopen(SONAME_LIBCAIRO, RTLD_NOW, NULL, 0);
    if (!libcairo)
    {
        FIXME("Wine cannot find the %s library.\n", SONAME_LIBCAIRO);
        goto error;
    }

    LOAD_FUNCPTR(libcairo, cairo_create)
    LOAD_FUNCPTR(libcairo, cairo_destroy)
    LOAD_FUNCPTR(libcairo, cairo_image_surface_create)
    LOAD_FUNCPTR(libcairo, cairo_image_surface_get_data)
    LOAD_FUNCPTR(libcairo, cairo_image_surface_get_stride)
    LOAD_FUNCPTR(libcairo, cairo_surface_destroy)
    LOAD_FUNCPTR(libcairo, cairo_surface_flush)

    libgobject2 = wine_dlopen(SONAME_LIBGOBJECT_2_0, RTLD_NOW, NULL, 0);
    if (!libgobject2)
    {
        FIXME("Wine cannot find the %s library.\n", SONAME_LIBGOBJECT_2_0);
        goto error;
    }

    LOAD_FUNCPTR(libgobject2, g_type_check_instance_is_a)
    return TRUE;

error:
    free_gtk3_libs();
    return FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    TRACE("(%p, %d, %p)\n", instance, reason, reserved);

    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(instance);
            if (!load_gtk3_libs()) return FALSE;
            break;

        case DLL_PROCESS_DETACH:
            if (reserved) break;
            free_gtk3_libs();
            break;
    }

    return TRUE;
}
