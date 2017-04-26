/* Typedefs for extensions loading

     Copyright (c) 2000 Lionel Ulmer
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
#ifndef __DLLS_OPENGL32_OPENGL_EXT_H
#define __DLLS_OPENGL32_OPENGL_EXT_H

#include "windef.h"
#include "wine/wgl.h"

typedef struct {
  const char  *name;     /* name of the extension */
  const char  *extension; /* name of the GL/WGL extension */
  void  *func;     /* pointer to the Wine function for this extension */
} OpenGL_extension;

extern const OpenGL_extension extension_registry[] DECLSPEC_HIDDEN;
extern const int extension_registry_size DECLSPEC_HIDDEN;

extern BOOL WINAPI wglSetPixelFormatWINE( HDC hdc, int format ) DECLSPEC_HIDDEN;
extern BOOL WINAPI wglQueryCurrentRendererIntegerWINE( GLenum attribute, GLuint *value ) DECLSPEC_HIDDEN;
extern const GLchar * WINAPI wglQueryCurrentRendererStringWINE( GLenum attribute );
extern BOOL WINAPI wglQueryRendererIntegerWINE( HDC dc, GLint renderer,
        GLenum attribute, GLuint *value ) DECLSPEC_HIDDEN;
extern const GLchar * WINAPI wglQueryRendererStringWINE( HDC dc, GLint renderer, GLenum attribute ) DECLSPEC_HIDDEN;

extern void WINAPI glDebugMessageCallback( void *callback, const void *userParam ) DECLSPEC_HIDDEN;
extern void WINAPI glDebugMessageCallbackAMD( void *callback, void *userParam ) DECLSPEC_HIDDEN;
extern void WINAPI glDebugMessageCallbackARB( void *callback, const void *userParam ) DECLSPEC_HIDDEN;

#endif /* __DLLS_OPENGL32_OPENGL_EXT_H */
