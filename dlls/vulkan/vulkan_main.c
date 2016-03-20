/*
 * Vulkan API implementation
 *
 * Copyright 2016 Sebastian Lackner
 * Copyright 2016 Michael Müller
 * Copyright 2016 Erich E. Hoover
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
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "wine/debug.h"
#include "wine/library.h"

#include "vulkan_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(vulkan);

static HINSTANCE vulkan_module;

#if defined(HAVE_X11_XLIB_H)
static Display *display;
#endif

#if defined(HAVE_X11_XLIB_H) && defined(SONAME_LIBX11_XCB)
WINE_DECLARE_DEBUG_CHANNEL(winediag);

static void *libx11_xcb_handle;
static typeof(xcb_get_setup) *pxcb_get_setup;
static typeof(xcb_screen_next) *pxcb_screen_next;
static typeof(xcb_setup_roots_iterator) *pxcb_setup_roots_iterator;
static typeof(XGetXCBConnection) *pXGetXCBConnection;

static BOOL init_x11_xcb( void )
{
    if (!(libx11_xcb_handle = wine_dlopen( SONAME_LIBX11_XCB, RTLD_NOW, NULL, 0 )))
    {
        ERR_(winediag)( "failed to load %s, vulkan support might not work properly\n", SONAME_LIBX11_XCB );
        return FALSE;
    }

    pxcb_get_setup              = wine_dlsym( libx11_xcb_handle, "xcb_get_setup", NULL, 0 );
    pxcb_screen_next            = wine_dlsym( libx11_xcb_handle, "xcb_screen_next", NULL, 0 );
    pxcb_setup_roots_iterator   = wine_dlsym( libx11_xcb_handle, "xcb_setup_roots_iterator", NULL, 0 );
    pXGetXCBConnection          = wine_dlsym( libx11_xcb_handle, "XGetXCBConnection", NULL, 0 );

    return TRUE;
}

static void free_x11_xcb( void )
{
    if (!libx11_xcb_handle) return;
    wine_dlclose( libx11_xcb_handle, NULL, 0 );
    libx11_xcb_handle = NULL;
}
#else
static BOOL init_x11_xcb( void )
{
    return FALSE;
}

static void free_x11_xcb( void )
{
}
#endif

static VkInstanceCreateInfo *convert_VkInstanceCreateInfo( VkInstanceCreateInfo *out,
        const VkInstanceCreateInfo *in, const char *extension_name )
{
    int i;

    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->sType                   = in->sType;
    out->pNext                   = in->pNext;
    out->flags                   = in->flags;
    out->pApplicationInfo        = in->pApplicationInfo;
    out->enabledLayerCount       = in->enabledLayerCount;
    out->ppEnabledLayerNames     = in->ppEnabledLayerNames;
    out->enabledExtensionCount   = in->enabledExtensionCount;
    out->ppEnabledExtensionNames = NULL;

    if (in->ppEnabledExtensionNames)
    {
        out->ppEnabledExtensionNames = HeapAlloc( GetProcessHeap(), 0,
                in->enabledExtensionCount * sizeof(*out->ppEnabledExtensionNames) );
        for (i = 0; i < in->enabledExtensionCount; i++)
        {
            if (!strcmp(in->ppEnabledExtensionNames[i], "VK_KHR_win32_surface"))
                out->ppEnabledExtensionNames[i] = (char *)extension_name;
            else
                out->ppEnabledExtensionNames[i] = in->ppEnabledExtensionNames[i];
        }
    }

    return out;
}
static  void release_VkInstanceCreateInfo( VkInstanceCreateInfo *out,
        VkInstanceCreateInfo *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return;

    HeapFree( GetProcessHeap(), 0, in->ppEnabledExtensionNames );
}

BOOL WINAPI DllMain( HINSTANCE instance, DWORD reason, LPVOID reserved )
{
    TRACE( "(%p, %u, %p)\n", instance, reason, reserved );

    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
            vulkan_module = instance;
            DisableThreadLibraryCalls( instance );
            if (!init_vulkan()) return FALSE;
#if defined(HAVE_X11_XLIB_H)
            if (!(display = XOpenDisplay( NULL)))
            {
                free_vulkan();
                return FALSE;
            }
#endif
            init_x11_xcb();
            break;

        case DLL_PROCESS_DETACH:
            if (reserved) break;
            free_x11_xcb();
            free_vulkan();
            if (display) XCloseDisplay( display );
            break;
    }

    return TRUE;
}

/***********************************************************************
 *              vkGetDeviceProcAddr (VULKAN.@)
 */
PFN_vkVoidFunction WINAPI vkGetDeviceProcAddr( VkDevice device, const char *pName )
{
    void *function;

    TRACE( "(%p, %s)\n", device, debugstr_a(pName) );

    if (is_null_func( pName ))
    {
        FIXME( "%s not supported\n", debugstr_a(pName) );
        return NULL;
    }

    function = GetProcAddress( vulkan_module, pName );
    if (!function) FIXME( "missing function %s\n", debugstr_a(pName) );
    return function;
}

/***********************************************************************
 *              vkGetInstanceProcAddr (VULKAN.@)
 */
PFN_vkVoidFunction WINAPI vkGetInstanceProcAddr( VkInstance instance, const char *pName )
{
    void *function;

    TRACE( "(%p, %s)\n", instance, debugstr_a(pName) );

    if (is_null_func( pName ))
    {
        FIXME( "%s not supported\n", debugstr_a(pName) );
        return NULL;
    }

    function = GetProcAddress( vulkan_module, pName );
    if (!function) FIXME( "missing function %s\n", debugstr_a(pName) );
    return function;
}

/***********************************************************************
 *              vkCreateWin32SurfaceKHR (VULKAN.@)
 */
VkResult WINAPI vkCreateWin32SurfaceKHR( VkInstance instance,
        const VkWin32SurfaceCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator,
        VkSurfaceKHR *pSurface )
{
    VkResult res = VK_ERROR_INCOMPATIBLE_DRIVER;

    TRACE( "(%p, %p, %p, %p)\n", instance, pCreateInfo, pAllocator, pSurface );

#if defined(HAVE_X11_XLIB_H) && defined(SONAME_LIBX11_XCB)
    if (pXGetXCBConnection && res == VK_ERROR_INCOMPATIBLE_DRIVER)
    {
        VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
        VkXcbSurfaceCreateInfoKHR_host tmp_pCreateInfo;

        tmp_pCreateInfo.sType   = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        tmp_pCreateInfo.pNext   = NULL;
        tmp_pCreateInfo.flags   = pCreateInfo->flags;
        tmp_pCreateInfo.connection = pXGetXCBConnection( display );
        tmp_pCreateInfo.window  = (xcb_window_t)(DWORD_PTR)GetPropA( pCreateInfo->hwnd, "__wine_x11_whole_window" );

        ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
        res = p_vkCreateXcbSurfaceKHR( instance, &tmp_pCreateInfo, ptr_pAllocator, pSurface );
        release_VkAllocationCallbacks( NULL, ptr_pAllocator );
    }
#endif

#if defined(HAVE_X11_XLIB_H)
    if (res == VK_ERROR_INCOMPATIBLE_DRIVER)
    {
        VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
        VkXlibSurfaceCreateInfoKHR_host tmp_pCreateInfo;

        tmp_pCreateInfo.sType   = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        tmp_pCreateInfo.pNext   = NULL;
        tmp_pCreateInfo.flags   = pCreateInfo->flags;
        tmp_pCreateInfo.dpy     = display;
        tmp_pCreateInfo.window  = (Window)(DWORD_PTR)GetPropA( pCreateInfo->hwnd, "__wine_x11_whole_window" );

        ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
        res = p_vkCreateXlibSurfaceKHR( instance, &tmp_pCreateInfo, ptr_pAllocator, pSurface );
        release_VkAllocationCallbacks( NULL, ptr_pAllocator );
    }
#endif

    if (res != VK_SUCCESS)
        ERR( "failed to create Win32Surface: %08x\n", res );

    return res;
}

#if defined(HAVE_X11_XLIB_H) && defined(SONAME_LIBX11_XCB)
static xcb_screen_t *get_xcb_screen( xcb_connection_t *connection, int screen )
{
    xcb_screen_iterator_t iter = pxcb_setup_roots_iterator( pxcb_get_setup(connection) );
    for (; iter.rem; screen--)
    {
        if (!screen) return iter.data;
        pxcb_screen_next( &iter );
    }
    return NULL;
}
#endif

/***********************************************************************
 *              vkGetPhysicalDeviceWin32PresentationSupportKHR (VULKAN.@)
 */
VkBool32 WINAPI vkGetPhysicalDeviceWin32PresentationSupportKHR( VkPhysicalDevice physicalDevice,
        uint32_t queueFamilyIndex )
{
    VkResult res = VK_ERROR_INCOMPATIBLE_DRIVER;

    TRACE( "(%p, %u)\n", physicalDevice, queueFamilyIndex );

#if defined(HAVE_X11_XLIB_H) && defined(SONAME_LIBX11_XCB)
    if (pxcb_get_setup && pxcb_screen_next && pxcb_setup_roots_iterator &&
        pXGetXCBConnection && res == VK_ERROR_INCOMPATIBLE_DRIVER)
    {
        xcb_connection_t *connection = pXGetXCBConnection( display );
        xcb_screen_t *screen = get_xcb_screen( connection, XDefaultScreen(display) );
        if (screen)
        {
            res = p_vkGetPhysicalDeviceXcbPresentationSupportKHR( physicalDevice, queueFamilyIndex,
                    connection, screen->root_visual );
        }
        else
            ERR( "failed to find default screen\n" );
    }
#endif

#if defined(HAVE_X11_XLIB_H)
    if (res == VK_ERROR_INCOMPATIBLE_DRIVER)
    {
        Visual *visual = XDefaultVisual( display, XDefaultScreen(display) );
        VisualID visual_id = XVisualIDFromVisual( visual );
        res = p_vkGetPhysicalDeviceXlibPresentationSupportKHR( physicalDevice, queueFamilyIndex,
                display, visual_id );
    }
#endif

    return res;
}

/***********************************************************************
 *              vkCreateInstance (VULKAN.@)
 */
VkResult WINAPI vkCreateInstance( const VkInstanceCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator, VkInstance *pInstance )
{
    VkResult res = VK_ERROR_INCOMPATIBLE_DRIVER;

    TRACE( "(%p, %p, %p)\n", pCreateInfo, pAllocator, pInstance );

#if defined(HAVE_X11_XLIB_H) && defined(SONAME_LIBX11_XCB)
    if (pXGetXCBConnection && (res == VK_ERROR_INCOMPATIBLE_DRIVER ||
                               res == VK_ERROR_EXTENSION_NOT_PRESENT))
    {
        VkInstanceCreateInfo tmp_pCreateInfo, *ptr_pCreateInfo;
        VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

        ptr_pCreateInfo = convert_VkInstanceCreateInfo( &tmp_pCreateInfo, pCreateInfo, "VK_KHR_xcb_surface" );
        ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
        res = p_vkCreateInstance( &tmp_pCreateInfo, ptr_pAllocator, pInstance );
        release_VkAllocationCallbacks( NULL, ptr_pAllocator );
        release_VkInstanceCreateInfo( NULL, ptr_pCreateInfo );
    }
#endif

#if defined(HAVE_X11_XLIB_H)
    if (res == VK_ERROR_INCOMPATIBLE_DRIVER ||
        res == VK_ERROR_EXTENSION_NOT_PRESENT)
    {
        VkInstanceCreateInfo tmp_pCreateInfo, *ptr_pCreateInfo;
        VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

        ptr_pCreateInfo = convert_VkInstanceCreateInfo( &tmp_pCreateInfo, pCreateInfo, "VK_KHR_xlib_surface" );
        ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
        res = p_vkCreateInstance( &tmp_pCreateInfo, ptr_pAllocator, pInstance );
        release_VkAllocationCallbacks( NULL, ptr_pAllocator );
        release_VkInstanceCreateInfo( NULL, ptr_pCreateInfo );
    }
#endif

    if (res != VK_SUCCESS)
        ERR( "failed to create instance: %08x\n", res );

    return res;
}

/***********************************************************************
 *              vkEnumerateInstanceExtensionProperties (VULKAN.@)
 */
VkResult WINAPI vkEnumerateInstanceExtensionProperties( const char *pLayerName,
        uint32_t *pPropertyCount, VkExtensionProperties *pProperties )
{
    VkResult res;
    int i;

    TRACE( "(%p, %p, %p)\n", pLayerName, pPropertyCount, pProperties );

    res = p_vkEnumerateInstanceExtensionProperties( pLayerName, pPropertyCount, pProperties );
    if ((res == VK_SUCCESS || res == VK_INCOMPLETE) && pProperties)
    {
        for (i = 0; i < *pPropertyCount; i++)
        {
            if (!strcmp( pProperties[i].extensionName, "VK_KHR_xcb_surface" ) ||
                !strcmp( pProperties[i].extensionName, "VK_KHR_xlib_surface" ))
            {
                TRACE( "replacing %s -> VK_KHR_win32_surface\n", debugstr_a(pProperties[i].extensionName) );
                strcpy( pProperties[i].extensionName, "VK_KHR_win32_surface" );
                pProperties[i].specVersion = 6;
            }
        }
    }

    return res;
}
