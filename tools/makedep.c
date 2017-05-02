/*
 * Generate include file dependencies
 *
 * Copyright 1996, 2013 Alexandre Julliard
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
#define NO_LIBWINE_PORT
#include "wine/port.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include "wine/list.h"

enum incl_type
{
    INCL_NORMAL,           /* #include "foo.h" */
    INCL_SYSTEM,           /* #include <foo.h> */
    INCL_IMPORT,           /* idl import "foo.idl" */
    INCL_IMPORTLIB,        /* idl importlib "foo.tlb" */
    INCL_CPP_QUOTE,        /* idl cpp_quote("#include \"foo.h\"") */
    INCL_CPP_QUOTE_SYSTEM  /* idl cpp_quote("#include <foo.h>") */
};

struct dependency
{
    int                line;          /* source line where this header is included */
    enum incl_type     type;          /* type of include */
    char              *name;          /* header name */
};

struct file
{
    struct list        entry;
    char              *name;          /* full file name relative to cwd */
    void              *args;          /* custom arguments for makefile rule */
    unsigned int       flags;         /* flags (see below) */
    unsigned int       deps_count;    /* files in use */
    unsigned int       deps_size;     /* total allocated size */
    struct dependency *deps;          /* all header dependencies */
};

struct incl_file
{
    struct list        entry;
    struct file       *file;
    char              *name;
    char              *filename;
    char              *sourcename;    /* source file name for generated headers */
    struct incl_file  *included_by;   /* file that included this one */
    int                included_line; /* line where this file was included */
    enum incl_type     type;          /* type of include */
    struct incl_file  *owner;
    unsigned int       files_count;   /* files in use */
    unsigned int       files_size;    /* total allocated size */
    struct incl_file **files;
};

#define FLAG_GENERATED      0x000001  /* generated file */
#define FLAG_INSTALL        0x000002  /* file to install */
#define FLAG_IDL_PROXY      0x000100  /* generates a proxy (_p.c) file */
#define FLAG_IDL_CLIENT     0x000200  /* generates a client (_c.c) file */
#define FLAG_IDL_SERVER     0x000400  /* generates a server (_s.c) file */
#define FLAG_IDL_IDENT      0x000800  /* generates an ident (_i.c) file */
#define FLAG_IDL_REGISTER   0x001000  /* generates a registration (_r.res) file */
#define FLAG_IDL_TYPELIB    0x002000  /* generates a typelib (.tlb) file */
#define FLAG_IDL_REGTYPELIB 0x004000  /* generates a registered typelib (_t.res) file */
#define FLAG_IDL_HEADER     0x008000  /* generates a header (.h) file */
#define FLAG_RC_PO          0x010000  /* rc file contains translations */
#define FLAG_C_IMPLIB       0x020000 /* file is part of an import library */
#define FLAG_SFD_FONTS      0x040000 /* sfd file generated bitmap fonts */

static const struct
{
    unsigned int flag;
    const char *ext;
} idl_outputs[] =
{
    { FLAG_IDL_TYPELIB,    ".tlb" },
    { FLAG_IDL_REGTYPELIB, "_t.res" },
    { FLAG_IDL_CLIENT,     "_c.c" },
    { FLAG_IDL_IDENT,      "_i.c" },
    { FLAG_IDL_PROXY,      "_p.c" },
    { FLAG_IDL_SERVER,     "_s.c" },
    { FLAG_IDL_REGISTER,   "_r.res" },
    { FLAG_IDL_HEADER,     ".h" }
};

#define HASH_SIZE 997

static struct list files[HASH_SIZE];

struct strarray
{
    unsigned int count;  /* strings in use */
    unsigned int size;   /* total allocated size */
    const char **str;
};

static const struct strarray empty_strarray;

enum install_rules { INSTALL_LIB, INSTALL_DEV, NB_INSTALL_RULES };

/* variables common to all makefiles */
static struct strarray linguas;
static struct strarray dll_flags;
static struct strarray target_flags;
static struct strarray msvcrt_flags;
static struct strarray extra_cflags;
static struct strarray cpp_flags;
static struct strarray unwind_flags;
static struct strarray libs;
static struct strarray cmdline_vars;
static struct strarray disabled_dirs;
static const char *root_src_dir;
static const char *tools_dir;
static const char *tools_ext;
static const char *exe_ext;
static const char *dll_ext;
static const char *man_ext;
static const char *crosstarget;
static const char *fontforge;
static const char *convert;
static const char *rsvg;
static const char *icotool;
static const char *dlltool;
static const char *msgfmt;
static const char *ln_s;

struct makefile
{
    struct strarray vars;
    struct strarray include_paths;
    struct strarray define_args;
    struct strarray programs;
    struct strarray scripts;
    struct strarray appmode;
    struct strarray imports;
    struct strarray subdirs;
    struct strarray delayimports;
    struct strarray extradllflags;
    struct strarray install_lib;
    struct strarray install_dev;
    struct list     sources;
    struct list     includes;
    const char     *base_dir;
    const char     *src_dir;
    const char     *obj_dir;
    const char     *top_src_dir;
    const char     *top_obj_dir;
    const char     *parent_dir;
    const char     *parent_spec;
    const char     *module;
    const char     *testdll;
    const char     *resource;
    const char     *sharedlib;
    const char     *staticlib;
    const char     *staticimplib;
    const char     *importlib;
    int             disabled;
    int             use_msvcrt;
    int             is_win16;
    struct makefile **submakes;
};

static struct makefile *top_makefile;

static const char separator[] = "### Dependencies";
static const char *output_makefile_name = "Makefile";
static const char *input_file_name;
static const char *output_file_name;
static const char *temp_file_name;
static int relative_dir_mode;
static int input_line;
static int output_column;
static FILE *output_file;

static const char Usage[] =
    "Usage: makedep [options] [directories]\n"
    "Options:\n"
    "   -R from to  Compute the relative path between two directories\n"
    "   -fxxx       Store output in file 'xxx' (default: Makefile)\n";


#ifndef __GNUC__
#define __attribute__(x)
#endif

static void fatal_error( const char *msg, ... ) __attribute__ ((__format__ (__printf__, 1, 2)));
static void fatal_perror( const char *msg, ... ) __attribute__ ((__format__ (__printf__, 1, 2)));
static void output( const char *format, ... ) __attribute__ ((__format__ (__printf__, 1, 2)));
static char *strmake( const char* fmt, ... ) __attribute__ ((__format__ (__printf__, 1, 2)));

/*******************************************************************
 *         fatal_error
 */
static void fatal_error( const char *msg, ... )
{
    va_list valist;
    va_start( valist, msg );
    if (input_file_name)
    {
        fprintf( stderr, "%s:", input_file_name );
        if (input_line) fprintf( stderr, "%d:", input_line );
        fprintf( stderr, " error: " );
    }
    else fprintf( stderr, "makedep: error: " );
    vfprintf( stderr, msg, valist );
    va_end( valist );
    exit(1);
}


/*******************************************************************
 *         fatal_perror
 */
static void fatal_perror( const char *msg, ... )
{
    va_list valist;
    va_start( valist, msg );
    if (input_file_name)
    {
        fprintf( stderr, "%s:", input_file_name );
        if (input_line) fprintf( stderr, "%d:", input_line );
        fprintf( stderr, " error: " );
    }
    else fprintf( stderr, "makedep: error: " );
    vfprintf( stderr, msg, valist );
    perror( " " );
    va_end( valist );
    exit(1);
}


/*******************************************************************
 *         cleanup_files
 */
static void cleanup_files(void)
{
    if (temp_file_name) unlink( temp_file_name );
    if (output_file_name) unlink( output_file_name );
}


/*******************************************************************
 *         exit_on_signal
 */
static void exit_on_signal( int sig )
{
    exit( 1 );  /* this will call the atexit functions */
}


/*******************************************************************
 *         xmalloc
 */
static void *xmalloc( size_t size )
{
    void *res;
    if (!(res = malloc (size ? size : 1)))
        fatal_error( "Virtual memory exhausted.\n" );
    return res;
}


/*******************************************************************
 *         xrealloc
 */
static void *xrealloc (void *ptr, size_t size)
{
    void *res;
    assert( size );
    if (!(res = realloc( ptr, size )))
        fatal_error( "Virtual memory exhausted.\n" );
    return res;
}

/*******************************************************************
 *         xstrdup
 */
static char *xstrdup( const char *str )
{
    char *res = strdup( str );
    if (!res) fatal_error( "Virtual memory exhausted.\n" );
    return res;
}


/*******************************************************************
 *         strmake
 */
static char *strmake( const char* fmt, ... )
{
    int n;
    size_t size = 100;
    va_list ap;

    for (;;)
    {
        char *p = xmalloc (size);
        va_start(ap, fmt);
        n = vsnprintf (p, size, fmt, ap);
        va_end(ap);
        if (n == -1) size *= 2;
        else if ((size_t)n >= size) size = n + 1;
        else return xrealloc( p, n + 1 );
        free(p);
    }
}


/*******************************************************************
 *         strendswith
 */
static int strendswith( const char* str, const char* end )
{
    size_t l = strlen( str );
    size_t m = strlen( end );

    return l >= m && strcmp(str + l - m, end) == 0;
}


/*******************************************************************
 *         output
 */
static void output( const char *format, ... )
{
    int ret;
    va_list valist;

    va_start( valist, format );
    ret = vfprintf( output_file, format, valist );
    va_end( valist );
    if (ret < 0) fatal_perror( "output" );
    if (format[0] && format[strlen(format) - 1] == '\n') output_column = 0;
    else output_column += ret;
}


/*******************************************************************
 *         strarray_add
 */
static void strarray_add( struct strarray *array, const char *str )
{
    if (array->count == array->size)
    {
	if (array->size) array->size *= 2;
        else array->size = 16;
	array->str = xrealloc( array->str, sizeof(array->str[0]) * array->size );
    }
    array->str[array->count++] = str;
}


/*******************************************************************
 *         strarray_addall
 */
static void strarray_addall( struct strarray *array, struct strarray added )
{
    unsigned int i;

    for (i = 0; i < added.count; i++) strarray_add( array, added.str[i] );
}


/*******************************************************************
 *         strarray_exists
 */
static int strarray_exists( const struct strarray *array, const char *str )
{
    unsigned int i;

    for (i = 0; i < array->count; i++) if (!strcmp( array->str[i], str )) return 1;
    return 0;
}


/*******************************************************************
 *         strarray_add_uniq
 */
static void strarray_add_uniq( struct strarray *array, const char *str )
{
    if (!strarray_exists( array, str )) strarray_add( array, str );
}


/*******************************************************************
 *         strarray_get_value
 *
 * Find a value in a name/value pair string array.
 */
static const char *strarray_get_value( const struct strarray *array, const char *name )
{
    int pos, res, min = 0, max = array->count / 2 - 1;

    while (min <= max)
    {
        pos = (min + max) / 2;
        if (!(res = strcmp( array->str[pos * 2], name ))) return array->str[pos * 2 + 1];
        if (res < 0) min = pos + 1;
        else max = pos - 1;
    }
    return NULL;
}


/*******************************************************************
 *         strarray_set_value
 *
 * Define a value in a name/value pair string array.
 */
static void strarray_set_value( struct strarray *array, const char *name, const char *value )
{
    int i, pos, res, min = 0, max = array->count / 2 - 1;

    while (min <= max)
    {
        pos = (min + max) / 2;
        if (!(res = strcmp( array->str[pos * 2], name )))
        {
            /* redefining a variable replaces the previous value */
            array->str[pos * 2 + 1] = value;
            return;
        }
        if (res < 0) min = pos + 1;
        else max = pos - 1;
    }
    strarray_add( array, NULL );
    strarray_add( array, NULL );
    for (i = array->count - 1; i > min * 2 + 1; i--) array->str[i] = array->str[i - 2];
    array->str[min * 2] = name;
    array->str[min * 2 + 1] = value;
}


/*******************************************************************
 *         output_filename
 */
static void output_filename( const char *name )
{
    if (output_column + strlen(name) + 1 > 100)
    {
        output( " \\\n" );
        output( "  " );
    }
    else if (output_column) output( " " );
    output( "%s", name );
}


/*******************************************************************
 *         output_filenames
 */
static void output_filenames( struct strarray array )
{
    unsigned int i;

    for (i = 0; i < array.count; i++) output_filename( array.str[i] );
}


/*******************************************************************
 *         get_extension
 */
static char *get_extension( char *filename )
{
    char *ext = strrchr( filename, '.' );
    if (ext && strchr( ext, '/' )) ext = NULL;
    return ext;
}


/*******************************************************************
 *         prepend_extension
 */
static char *prepend_extension( const char *name, const char *prepend, const char *new_ext )
{
    int name_len, prepend_len = strlen(prepend);
    const char *ext;
    char *ret;

    ext = strrchr( name, '.' );
    if (ext && strchr(ext, '/')) ext = NULL;
    name_len = ext ? (ext - name) : strlen( name );

    if (!ext) ext = new_ext;
    if (!ext) ext = "";

    ret = xmalloc( name_len + prepend_len + strlen(ext) + 1 );
    memcpy( ret, name, name_len );
    memcpy( ret + name_len, prepend, prepend_len );
    strcpy( ret + name_len + prepend_len, ext );
    return ret;
}


/*******************************************************************
 *         replace_extension
 */
static char *replace_extension( const char *name, const char *old_ext, const char *new_ext )
{
    char *ret;
    size_t name_len = strlen( name );
    size_t ext_len = strlen( old_ext );

    if (name_len >= ext_len && !strcmp( name + name_len - ext_len, old_ext )) name_len -= ext_len;
    ret = xmalloc( name_len + strlen( new_ext ) + 1 );
    memcpy( ret, name, name_len );
    strcpy( ret + name_len, new_ext );
    return ret;
}


/*******************************************************************
 *         replace_filename
 */
static char *replace_filename( const char *path, const char *name )
{
    const char *p;
    char *ret;
    size_t len;

    if (!path) return xstrdup( name );
    if (!(p = strrchr( path, '/' ))) return xstrdup( name );
    len = p - path + 1;
    ret = xmalloc( len + strlen( name ) + 1 );
    memcpy( ret, path, len );
    strcpy( ret + len, name );
    return ret;
}


/*******************************************************************
 *         strarray_replace_extension
 */
static struct strarray strarray_replace_extension( const struct strarray *array,
                                                   const char *old_ext, const char *new_ext )
{
    unsigned int i;
    struct strarray ret;

    ret.count = ret.size = array->count;
    ret.str = xmalloc( sizeof(ret.str[0]) * ret.size );
    for (i = 0; i < array->count; i++) ret.str[i] = replace_extension( array->str[i], old_ext, new_ext );
    return ret;
}


/*******************************************************************
 *         replace_substr
 */
static char *replace_substr( const char *str, const char *start, size_t len, const char *replace )
{
    size_t pos = start - str;
    char *ret = xmalloc( pos + strlen(replace) + strlen(start + len) + 1 );
    memcpy( ret, str, pos );
    strcpy( ret + pos, replace );
    strcat( ret + pos, start + len );
    return ret;
}


/*******************************************************************
 *         get_relative_path
 *
 * Determine where the destination path is located relative to the 'from' path.
 */
static char *get_relative_path( const char *from, const char *dest )
{
    const char *start;
    char *ret, *p;
    unsigned int dotdots = 0;

    /* a path of "." is equivalent to an empty path */
    if (!strcmp( from, "." )) from = "";

    for (;;)
    {
        while (*from == '/') from++;
        while (*dest == '/') dest++;
        start = dest;  /* save start of next path element */
        if (!*from) break;

        while (*from && *from != '/' && *from == *dest) { from++; dest++; }
        if ((!*from || *from == '/') && (!*dest || *dest == '/')) continue;

        /* count remaining elements in 'from' */
        do
        {
            dotdots++;
            while (*from && *from != '/') from++;
            while (*from == '/') from++;
        }
        while (*from);
        break;
    }

    if (!start[0] && !dotdots) return NULL;  /* empty path */

    ret = xmalloc( 3 * dotdots + strlen( start ) + 1 );
    for (p = ret; dotdots; dotdots--, p += 3) memcpy( p, "../", 3 );

    if (start[0]) strcpy( p, start );
    else p[-1] = 0;  /* remove trailing slash */
    return ret;
}


/*******************************************************************
 *         concat_paths
 */
static char *concat_paths( const char *base, const char *path )
{
    if (!base || !base[0]) return xstrdup( path && path[0] ? path : "." );
    if (!path || !path[0]) return xstrdup( base );
    if (path[0] == '/') return xstrdup( path );
    return strmake( "%s/%s", base, path );
}


/*******************************************************************
 *         base_dir_path
 */
static char *base_dir_path( const struct makefile *make, const char *path )
{
    return concat_paths( make->base_dir, path );
}


/*******************************************************************
 *         obj_dir_path
 */
static char *obj_dir_path( const struct makefile *make, const char *path )
{
    return concat_paths( make->obj_dir, path );
}


/*******************************************************************
 *         src_dir_path
 */
static char *src_dir_path( const struct makefile *make, const char *path )
{
    if (make->src_dir) return concat_paths( make->src_dir, path );
    return obj_dir_path( make, path );
}


/*******************************************************************
 *         top_obj_dir_path
 */
static char *top_obj_dir_path( const struct makefile *make, const char *path )
{
    return concat_paths( make->top_obj_dir, path );
}


/*******************************************************************
 *         top_src_dir_path
 */
static char *top_src_dir_path( const struct makefile *make, const char *path )
{
    if (make->top_src_dir) return concat_paths( make->top_src_dir, path );
    return top_obj_dir_path( make, path );
}


/*******************************************************************
 *         root_dir_path
 */
static char *root_dir_path( const char *path )
{
    return concat_paths( root_src_dir, path );
}


/*******************************************************************
 *         tools_dir_path
 */
static char *tools_dir_path( const struct makefile *make, const char *path )
{
    if (tools_dir) return top_obj_dir_path( make, strmake( "%s/tools/%s", tools_dir, path ));
    return top_obj_dir_path( make, strmake( "tools/%s", path ));
}


/*******************************************************************
 *         tools_path
 */
static char *tools_path( const struct makefile *make, const char *name )
{
    return strmake( "%s/%s%s", tools_dir_path( make, name ), name, tools_ext );
}


/*******************************************************************
 *         get_line
 */
static char *get_line( FILE *file )
{
    static char *buffer;
    static size_t size;

    if (!size)
    {
        size = 1024;
        buffer = xmalloc( size );
    }
    if (!fgets( buffer, size, file )) return NULL;
    input_line++;

    for (;;)
    {
        char *p = buffer + strlen(buffer);
        /* if line is larger than buffer, resize buffer */
        while (p == buffer + size - 1 && p[-1] != '\n')
        {
            buffer = xrealloc( buffer, size * 2 );
            if (!fgets( buffer + size - 1, size + 1, file )) break;
            p = buffer + strlen(buffer);
            size *= 2;
        }
        if (p > buffer && p[-1] == '\n')
        {
            *(--p) = 0;
            if (p > buffer && p[-1] == '\r') *(--p) = 0;
            if (p > buffer && p[-1] == '\\')
            {
                *(--p) = 0;
                /* line ends in backslash, read continuation line */
                if (!fgets( p, size - (p - buffer), file )) return buffer;
                input_line++;
                continue;
            }
        }
        return buffer;
    }
}


/*******************************************************************
 *         hash_filename
 */
static unsigned int hash_filename( const char *name )
{
    /* FNV-1 hash */
    unsigned int ret = 2166136261u;
    while (*name) ret = (ret * 16777619) ^ *name++;
    return ret % HASH_SIZE;
}


/*******************************************************************
 *         add_file
 */
static struct file *add_file( const char *name )
{
    struct file *file = xmalloc( sizeof(*file) );
    memset( file, 0, sizeof(*file) );
    file->name = xstrdup( name );
    return file;
}


/*******************************************************************
 *         add_dependency
 */
static void add_dependency( struct file *file, const char *name, enum incl_type type )
{
    /* enforce some rules for the Wine tree */

    if (!memcmp( name, "../", 3 ))
        fatal_error( "#include directive with relative path not allowed\n" );

    if (!strcmp( name, "config.h" ))
    {
        if (strendswith( file->name, ".h" ))
            fatal_error( "config.h must not be included by a header file\n" );
        if (file->deps_count)
            fatal_error( "config.h must be included before anything else\n" );
    }
    else if (!strcmp( name, "wine/port.h" ))
    {
        if (strendswith( file->name, ".h" ))
            fatal_error( "wine/port.h must not be included by a header file\n" );
        if (!file->deps_count) fatal_error( "config.h must be included before wine/port.h\n" );
        if (file->deps_count > 1)
            fatal_error( "wine/port.h must be included before everything except config.h\n" );
        if (strcmp( file->deps[0].name, "config.h" ))
            fatal_error( "config.h must be included before wine/port.h\n" );
    }

    if (file->deps_count >= file->deps_size)
    {
        file->deps_size *= 2;
        if (file->deps_size < 16) file->deps_size = 16;
        file->deps = xrealloc( file->deps, file->deps_size * sizeof(*file->deps) );
    }
    file->deps[file->deps_count].line = input_line;
    file->deps[file->deps_count].type = type;
    file->deps[file->deps_count].name = xstrdup( name );
    file->deps_count++;
}


/*******************************************************************
 *         find_src_file
 */
static struct incl_file *find_src_file( const struct makefile *make, const char *name )
{
    struct incl_file *file;

    LIST_FOR_EACH_ENTRY( file, &make->sources, struct incl_file, entry )
        if (!strcmp( name, file->name )) return file;
    return NULL;
}

/*******************************************************************
 *         find_include_file
 */
static struct incl_file *find_include_file( const struct makefile *make, const char *name )
{
    struct incl_file *file;

    LIST_FOR_EACH_ENTRY( file, &make->includes, struct incl_file, entry )
        if (!strcmp( name, file->name )) return file;
    return NULL;
}

/*******************************************************************
 *         add_include
 *
 * Add an include file if it doesn't already exists.
 */
static struct incl_file *add_include( struct makefile *make, struct incl_file *parent,
                                      const char *name, int line, enum incl_type type )
{
    struct incl_file *include;

    if (parent->files_count >= parent->files_size)
    {
        parent->files_size *= 2;
        if (parent->files_size < 16) parent->files_size = 16;
        parent->files = xrealloc( parent->files, parent->files_size * sizeof(*parent->files) );
    }

    LIST_FOR_EACH_ENTRY( include, &make->includes, struct incl_file, entry )
        if (!strcmp( name, include->name )) goto found;

    include = xmalloc( sizeof(*include) );
    memset( include, 0, sizeof(*include) );
    include->name = xstrdup(name);
    include->included_by = parent;
    include->included_line = line;
    include->type = type;
    list_add_tail( &make->includes, &include->entry );
found:
    parent->files[parent->files_count++] = include;
    return include;
}


/*******************************************************************
 *         add_generated_source
 *
 * Add a generated source file to the list.
 */
static struct incl_file *add_generated_source( struct makefile *make,
                                               const char *name, const char *filename )
{
    struct incl_file *file;

    if ((file = find_src_file( make, name ))) return file;  /* we already have it */
    file = xmalloc( sizeof(*file) );
    memset( file, 0, sizeof(*file) );
    file->file = add_file( name );
    file->name = xstrdup( name );
    file->filename = obj_dir_path( make, filename ? filename : name );
    file->file->flags = FLAG_GENERATED;
    list_add_tail( &make->sources, &file->entry );
    return file;
}


/*******************************************************************
 *         parse_include_directive
 */
static void parse_include_directive( struct file *source, char *str )
{
    char quote, *include, *p = str;

    while (*p && isspace(*p)) p++;
    if (*p != '\"' && *p != '<' ) return;
    quote = *p++;
    if (quote == '<') quote = '>';
    include = p;
    while (*p && (*p != quote)) p++;
    if (!*p) fatal_error( "malformed include directive '%s'\n", str );
    *p = 0;
    add_dependency( source, include, (quote == '>') ? INCL_SYSTEM : INCL_NORMAL );
}


/*******************************************************************
 *         parse_pragma_directive
 */
static void parse_pragma_directive( struct file *source, char *str )
{
    char *flag, *p = str;

    if (!isspace( *p )) return;
    while (*p && isspace(*p)) p++;
    p = strtok( p, " \t" );
    if (strcmp( p, "makedep" )) return;

    while ((flag = strtok( NULL, " \t" )))
    {
        if (!strcmp( flag, "depend" ))
        {
            while ((p = strtok( NULL, " \t" ))) add_dependency( source, p, INCL_NORMAL );
            return;
        }
        else if (!strcmp( flag, "install" )) source->flags |= FLAG_INSTALL;

        if (strendswith( source->name, ".idl" ))
        {
            if (!strcmp( flag, "header" )) source->flags |= FLAG_IDL_HEADER;
            else if (!strcmp( flag, "proxy" )) source->flags |= FLAG_IDL_PROXY;
            else if (!strcmp( flag, "client" )) source->flags |= FLAG_IDL_CLIENT;
            else if (!strcmp( flag, "server" )) source->flags |= FLAG_IDL_SERVER;
            else if (!strcmp( flag, "ident" )) source->flags |= FLAG_IDL_IDENT;
            else if (!strcmp( flag, "typelib" )) source->flags |= FLAG_IDL_TYPELIB;
            else if (!strcmp( flag, "register" )) source->flags |= FLAG_IDL_REGISTER;
            else if (!strcmp( flag, "regtypelib" )) source->flags |= FLAG_IDL_REGTYPELIB;
        }
        else if (strendswith( source->name, ".rc" ))
        {
            if (!strcmp( flag, "po" )) source->flags |= FLAG_RC_PO;
        }
        else if (strendswith( source->name, ".sfd" ))
        {
            if (!strcmp( flag, "font" ))
            {
                struct strarray *array = source->args;

                if (!array)
                {
                    source->args = array = xmalloc( sizeof(*array) );
                    *array = empty_strarray;
                    source->flags |= FLAG_SFD_FONTS;
                }
                strarray_add( array, xstrdup( strtok( NULL, "" )));
                return;
            }
        }
        else if (!strcmp( flag, "implib" )) source->flags |= FLAG_C_IMPLIB;
    }
}


/*******************************************************************
 *         parse_cpp_directive
 */
static void parse_cpp_directive( struct file *source, char *str )
{
    while (*str && isspace(*str)) str++;
    if (*str++ != '#') return;
    while (*str && isspace(*str)) str++;

    if (!strncmp( str, "include", 7 ))
        parse_include_directive( source, str + 7 );
    else if (!strncmp( str, "import", 6 ) && strendswith( source->name, ".m" ))
        parse_include_directive( source, str + 6 );
    else if (!strncmp( str, "pragma", 6 ))
        parse_pragma_directive( source, str + 6 );
}


/*******************************************************************
 *         parse_idl_file
 */
static void parse_idl_file( struct file *source, FILE *file )
{
    char *buffer, *include;

    input_line = 0;

    while ((buffer = get_line( file )))
    {
        char quote;
        char *p = buffer;
        while (*p && isspace(*p)) p++;

        if (!strncmp( p, "importlib", 9 ))
        {
            p += 9;
            while (*p && isspace(*p)) p++;
            if (*p++ != '(') continue;
            while (*p && isspace(*p)) p++;
            if (*p++ != '"') continue;
            include = p;
            while (*p && (*p != '"')) p++;
            if (!*p) fatal_error( "malformed importlib directive\n" );
            *p = 0;
            add_dependency( source, include, INCL_IMPORTLIB );
            continue;
        }

        if (!strncmp( p, "import", 6 ))
        {
            p += 6;
            while (*p && isspace(*p)) p++;
            if (*p != '"') continue;
            include = ++p;
            while (*p && (*p != '"')) p++;
            if (!*p) fatal_error( "malformed import directive\n" );
            *p = 0;
            add_dependency( source, include, INCL_IMPORT );
            continue;
        }

        /* check for #include inside cpp_quote */
        if (!strncmp( p, "cpp_quote", 9 ))
        {
            p += 9;
            while (*p && isspace(*p)) p++;
            if (*p++ != '(') continue;
            while (*p && isspace(*p)) p++;
            if (*p++ != '"') continue;
            if (*p++ != '#') continue;
            while (*p && isspace(*p)) p++;
            if (strncmp( p, "include", 7 )) continue;
            p += 7;
            while (*p && isspace(*p)) p++;
            if (*p == '\\' && p[1] == '"')
            {
                p += 2;
                quote = '"';
            }
            else
            {
                if (*p++ != '<' ) continue;
                quote = '>';
            }
            include = p;
            while (*p && (*p != quote)) p++;
            if (!*p || (quote == '"' && p[-1] != '\\'))
                fatal_error( "malformed #include directive inside cpp_quote\n" );
            if (quote == '"') p--;  /* remove backslash */
            *p = 0;
            add_dependency( source, include, (quote == '>') ? INCL_CPP_QUOTE_SYSTEM : INCL_CPP_QUOTE );
            continue;
        }

        parse_cpp_directive( source, p );
    }
}

/*******************************************************************
 *         parse_c_file
 */
static void parse_c_file( struct file *source, FILE *file )
{
    char *buffer;

    input_line = 0;
    while ((buffer = get_line( file )))
    {
        parse_cpp_directive( source, buffer );
    }
}


/*******************************************************************
 *         parse_rc_file
 */
static void parse_rc_file( struct file *source, FILE *file )
{
    char *buffer, *include;

    input_line = 0;
    while ((buffer = get_line( file )))
    {
        char quote;
        char *p = buffer;
        while (*p && isspace(*p)) p++;

        if (p[0] == '/' && p[1] == '*')  /* check for magic makedep comment */
        {
            p += 2;
            while (*p && isspace(*p)) p++;
            if (strncmp( p, "@makedep:", 9 )) continue;
            p += 9;
            while (*p && isspace(*p)) p++;
            quote = '"';
            if (*p == quote)
            {
                include = ++p;
                while (*p && *p != quote) p++;
            }
            else
            {
                include = p;
                while (*p && !isspace(*p) && *p != '*') p++;
            }
            if (!*p)
                fatal_error( "malformed makedep comment\n" );
            *p = 0;
            add_dependency( source, include, (quote == '>') ? INCL_SYSTEM : INCL_NORMAL );
            continue;
        }

        parse_cpp_directive( source, buffer );
    }
}


/*******************************************************************
 *         parse_in_file
 */
static void parse_in_file( struct file *source, FILE *file )
{
    char *p, *buffer;

    /* make sure it gets rebuilt when the version changes */
    add_dependency( source, "config.h", INCL_SYSTEM );

    if (!strendswith( source->name, ".man.in" )) return;  /* not a man page */

    input_line = 0;
    while ((buffer = get_line( file )))
    {
        if (strncmp( buffer, ".TH", 3 )) continue;
        if (!(p = strtok( buffer, " \t" ))) continue;  /* .TH */
        if (!(p = strtok( NULL, " \t" ))) continue;  /* program name */
        if (!(p = strtok( NULL, " \t" ))) continue;  /* man section */
        source->args = xstrdup( p );
        return;
    }
}


/*******************************************************************
 *         parse_sfd_file
 */
static void parse_sfd_file( struct file *source, FILE *file )
{
    char *p, *eol, *buffer;

    input_line = 0;
    while ((buffer = get_line( file )))
    {
        if (strncmp( buffer, "UComments:", 10 )) continue;
        p = buffer + 10;
        while (*p == ' ') p++;
        if (p[0] == '"' && p[1] && buffer[strlen(buffer) - 1] == '"')
        {
            p++;
            buffer[strlen(buffer) - 1] = 0;
        }
        while ((eol = strstr( p, "+AAoA" )))
        {
            *eol = 0;
            while (*p && isspace(*p)) p++;
            if (*p++ == '#')
            {
                while (*p && isspace(*p)) p++;
                if (!strncmp( p, "pragma", 6 )) parse_pragma_directive( source, p + 6 );
            }
            p = eol + 5;
        }
        while (*p && isspace(*p)) p++;
        if (*p++ != '#') return;
        while (*p && isspace(*p)) p++;
        if (!strncmp( p, "pragma", 6 )) parse_pragma_directive( source, p + 6 );
        return;
    }
}


static const struct
{
    const char *ext;
    void (*parse)( struct file *file, FILE *f );
} parse_functions[] =
{
    { ".c",   parse_c_file },
    { ".h",   parse_c_file },
    { ".inl", parse_c_file },
    { ".l",   parse_c_file },
    { ".m",   parse_c_file },
    { ".rh",  parse_c_file },
    { ".x",   parse_c_file },
    { ".y",   parse_c_file },
    { ".idl", parse_idl_file },
    { ".rc",  parse_rc_file },
    { ".in",  parse_in_file },
    { ".sfd", parse_sfd_file }
};

/*******************************************************************
 *         load_file
 */
static struct file *load_file( const char *name )
{
    struct file *file;
    FILE *f;
    unsigned int i, hash = hash_filename( name );

    LIST_FOR_EACH_ENTRY( file, &files[hash], struct file, entry )
        if (!strcmp( name, file->name )) return file;

    if (!(f = fopen( name, "r" ))) return NULL;

    file = add_file( name );
    list_add_tail( &files[hash], &file->entry );
    input_file_name = file->name;
    input_line = 0;

    for (i = 0; i < sizeof(parse_functions) / sizeof(parse_functions[0]); i++)
    {
        if (!strendswith( name, parse_functions[i].ext )) continue;
        parse_functions[i].parse( file, f );
        break;
    }

    fclose( f );
    input_file_name = NULL;

    return file;
}


/*******************************************************************
 *         open_include_path_file
 *
 * Open a file from a directory on the include path.
 */
static struct file *open_include_path_file( const struct makefile *make, const char *dir,
                                            const char *name, char **filename )
{
    char *src_path = base_dir_path( make, concat_paths( dir, name ));
    struct file *ret = load_file( src_path );

    if (ret) *filename = src_dir_path( make, concat_paths( dir, name ));
    return ret;
}


/*******************************************************************
 *         open_file_same_dir
 *
 * Open a file in the same directory as the parent.
 */
static struct file *open_file_same_dir( const struct incl_file *parent, const char *name, char **filename )
{
    char *src_path = replace_filename( parent->file->name, name );
    struct file *ret = load_file( src_path );

    if (ret) *filename = replace_filename( parent->filename, name );
    free( src_path );
    return ret;
}


/*******************************************************************
 *         open_local_file
 *
 * Open a file in the source directory of the makefile.
 */
static struct file *open_local_file( const struct makefile *make, const char *path, char **filename )
{
    char *src_path = root_dir_path( base_dir_path( make, path ));
    struct file *ret = load_file( src_path );

    /* if not found, try parent dir */
    if (!ret && make->parent_dir)
    {
        free( src_path );
        path = strmake( "%s/%s", make->parent_dir, path );
        src_path = root_dir_path( base_dir_path( make, path ));
        ret = load_file( src_path );
    }

    if (ret) *filename = src_dir_path( make, path );
    free( src_path );
    return ret;
}


/*******************************************************************
 *         open_global_file
 *
 * Open a file in the top-level source directory.
 */
static struct file *open_global_file( const struct makefile *make, const char *path, char **filename )
{
    char *src_path = root_dir_path( path );
    struct file *ret = load_file( src_path );

    if (ret) *filename = top_src_dir_path( make, path );
    free( src_path );
    return ret;
}


/*******************************************************************
 *         open_global_header
 *
 * Open a file in the global include source directory.
 */
static struct file *open_global_header( const struct makefile *make, const char *path, char **filename )
{
    return open_global_file( make, strmake( "include/%s", path ), filename );
}


/*******************************************************************
 *         open_src_file
 */
static struct file *open_src_file( const struct makefile *make, struct incl_file *pFile )
{
    struct file *file = open_local_file( make, pFile->name, &pFile->filename );

    if (!file) fatal_perror( "open %s", pFile->name );
    return file;
}


/*******************************************************************
 *         open_include_file
 */
static struct file *open_include_file( const struct makefile *make, struct incl_file *pFile )
{
    struct file *file = NULL;
    char *filename;
    unsigned int i, len;

    errno = ENOENT;

    /* check for generated bison header */

    if (strendswith( pFile->name, ".tab.h" ) &&
        (file = open_local_file( make, replace_extension( pFile->name, ".tab.h", ".y" ), &filename )))
    {
        pFile->sourcename = filename;
        pFile->filename = obj_dir_path( make, pFile->name );
        return file;
    }

    /* check for corresponding idl file in source dir */

    if (strendswith( pFile->name, ".h" ) &&
        (file = open_local_file( make, replace_extension( pFile->name, ".h", ".idl" ), &filename )))
    {
        pFile->sourcename = filename;
        pFile->filename = obj_dir_path( make, pFile->name );
        return file;
    }

    /* check for corresponding tlb file in source dir */

    if (strendswith( pFile->name, ".tlb" ) &&
        (file = open_local_file( make, replace_extension( pFile->name, ".tlb", ".idl" ), &filename )))
    {
        pFile->sourcename = filename;
        pFile->filename = obj_dir_path( make, pFile->name );
        return file;
    }

    /* now try in source dir */
    if ((file = open_local_file( make, pFile->name, &pFile->filename ))) return file;

    /* check for corresponding idl file in global includes */

    if (strendswith( pFile->name, ".h" ) &&
        (file = open_global_header( make, replace_extension( pFile->name, ".h", ".idl" ), &filename )))
    {
        pFile->sourcename = filename;
        pFile->filename = top_obj_dir_path( make, strmake( "include/%s", pFile->name ));
        return file;
    }

    /* check for corresponding .in file in global includes (for config.h.in) */

    if (strendswith( pFile->name, ".h" ) &&
        (file = open_global_header( make, replace_extension( pFile->name, ".h", ".h.in" ), &filename )))
    {
        pFile->sourcename = filename;
        pFile->filename = top_obj_dir_path( make, strmake( "include/%s", pFile->name ));
        return file;
    }

    /* check for corresponding .x file in global includes */

    if (strendswith( pFile->name, "tmpl.h" ) &&
        (file = open_global_header( make, replace_extension( pFile->name, ".h", ".x" ), &filename )))
    {
        pFile->sourcename = filename;
        pFile->filename = top_obj_dir_path( make, strmake( "include/%s", pFile->name ));
        return file;
    }

    /* check for corresponding .tlb file in global includes */

    if (strendswith( pFile->name, ".tlb" ) &&
        (file = open_global_header( make, replace_extension( pFile->name, ".tlb", ".idl" ), &filename )))
    {
        pFile->sourcename = filename;
        pFile->filename = top_obj_dir_path( make, strmake( "include/%s", pFile->name ));
        return file;
    }

    /* check in global includes source dir */

    if ((file = open_global_header( make, pFile->name, &pFile->filename ))) return file;

    /* check in global msvcrt includes */
    if (make->use_msvcrt &&
        (file = open_global_header( make, strmake( "msvcrt/%s", pFile->name ), &pFile->filename )))
        return file;

    /* now search in include paths */
    for (i = 0; i < make->include_paths.count; i++)
    {
        const char *dir = make->include_paths.str[i];
        const char *prefix = make->top_src_dir ? make->top_src_dir : make->top_obj_dir;

        if (prefix)
        {
            len = strlen( prefix );
            if (!strncmp( dir, prefix, len ) && (!dir[len] || dir[len] == '/'))
            {
                while (dir[len] == '/') len++;
                file = open_global_file( make, concat_paths( dir + len, pFile->name ), &pFile->filename );
                if (file) return file;
            }
            if (make->top_src_dir) continue;  /* ignore paths that don't point to the top source dir */
        }
        if (*dir != '/')
        {
            if ((file = open_include_path_file( make, dir, pFile->name, &pFile->filename )))
                return file;
        }
    }
    if (pFile->type == INCL_SYSTEM) return NULL;  /* ignore system files we cannot find */

    /* try in src file directory */
    if ((file = open_file_same_dir( pFile->included_by, pFile->name, &pFile->filename ))) return file;

    fprintf( stderr, "%s:%d: error: ", pFile->included_by->file->name, pFile->included_line );
    perror( pFile->name );
    pFile = pFile->included_by;
    while (pFile && pFile->included_by)
    {
        const char *parent = pFile->included_by->sourcename;
        if (!parent) parent = pFile->included_by->file->name;
        fprintf( stderr, "%s:%d: note: %s was first included here\n",
                 parent, pFile->included_line, pFile->name );
        pFile = pFile->included_by;
    }
    exit(1);
}


/*******************************************************************
 *         add_all_includes
 */
static void add_all_includes( struct makefile *make, struct incl_file *parent, struct file *file )
{
    unsigned int i;

    parent->files_count = 0;
    parent->files_size = file->deps_count;
    parent->files = xmalloc( parent->files_size * sizeof(*parent->files) );
    for (i = 0; i < file->deps_count; i++)
    {
        switch (file->deps[i].type)
        {
        case INCL_NORMAL:
        case INCL_IMPORT:
            add_include( make, parent, file->deps[i].name, file->deps[i].line, INCL_NORMAL );
            break;
        case INCL_IMPORTLIB:
            add_include( make, parent, file->deps[i].name, file->deps[i].line, INCL_IMPORTLIB );
            break;
        case INCL_SYSTEM:
            add_include( make, parent, file->deps[i].name, file->deps[i].line, INCL_SYSTEM );
            break;
        case INCL_CPP_QUOTE:
        case INCL_CPP_QUOTE_SYSTEM:
            break;
        }
    }
}


/*******************************************************************
 *         parse_file
 */
static void parse_file( struct makefile *make, struct incl_file *source, int src )
{
    struct file *file = src ? open_src_file( make, source ) : open_include_file( make, source );

    if (!file) return;

    source->file = file;
    source->files_count = 0;
    source->files_size = file->deps_count;
    source->files = xmalloc( source->files_size * sizeof(*source->files) );

    if (source->sourcename)
    {
        if (strendswith( source->sourcename, ".idl" ))
        {
            unsigned int i;

            if (strendswith( source->name, ".tlb" )) return;  /* typelibs don't include anything */

            /* generated .h file always includes these */
            add_include( make, source, "rpc.h", 0, INCL_NORMAL );
            add_include( make, source, "rpcndr.h", 0, INCL_NORMAL );
            for (i = 0; i < file->deps_count; i++)
            {
                switch (file->deps[i].type)
                {
                case INCL_IMPORT:
                    if (strendswith( file->deps[i].name, ".idl" ))
                        add_include( make, source, replace_extension( file->deps[i].name, ".idl", ".h" ),
                                     file->deps[i].line, INCL_NORMAL );
                    else
                        add_include( make, source, file->deps[i].name, file->deps[i].line, INCL_NORMAL );
                    break;
                case INCL_CPP_QUOTE:
                    add_include( make, source, file->deps[i].name, file->deps[i].line, INCL_NORMAL );
                    break;
                case INCL_CPP_QUOTE_SYSTEM:
                    add_include( make, source, file->deps[i].name, file->deps[i].line, INCL_SYSTEM );
                    break;
                case INCL_NORMAL:
                case INCL_SYSTEM:
                case INCL_IMPORTLIB:
                    break;
                }
            }
            return;
        }
        if (strendswith( source->sourcename, ".y" ))
            return;  /* generated .tab.h doesn't include anything */
    }

    add_all_includes( make, source, file );
}


/*******************************************************************
 *         add_src_file
 *
 * Add a source file to the list.
 */
static struct incl_file *add_src_file( struct makefile *make, const char *name )
{
    struct incl_file *file;

    if ((file = find_src_file( make, name ))) return file;  /* we already have it */
    file = xmalloc( sizeof(*file) );
    memset( file, 0, sizeof(*file) );
    file->name = xstrdup(name);
    list_add_tail( &make->sources, &file->entry );
    parse_file( make, file, 1 );
    return file;
}


/*******************************************************************
 *         open_input_makefile
 */
static FILE *open_input_makefile( const struct makefile *make )
{
    FILE *ret;

    if (make->base_dir)
        input_file_name = root_dir_path( base_dir_path( make, strmake( "%s.in", output_makefile_name )));
    else
        input_file_name = output_makefile_name;  /* always use output name for main Makefile */

    input_line = 0;
    if (!(ret = fopen( input_file_name, "r" ))) fatal_perror( "open" );
    return ret;
}


/*******************************************************************
 *         get_make_variable
 */
static const char *get_make_variable( const struct makefile *make, const char *name )
{
    const char *ret;

    if ((ret = strarray_get_value( &cmdline_vars, name ))) return ret;
    if ((ret = strarray_get_value( &make->vars, name ))) return ret;
    if (top_makefile && (ret = strarray_get_value( &top_makefile->vars, name ))) return ret;
    return NULL;
}


/*******************************************************************
 *         get_expanded_make_variable
 */
static char *get_expanded_make_variable( const struct makefile *make, const char *name )
{
    const char *var;
    char *p, *end, *expand, *tmp = NULL;

    var = get_make_variable( make, name );
    if (!var) return NULL;

    p = expand = xstrdup( var );
    while ((p = strchr( p, '$' )))
    {
        if (p[1] == '(')
        {
            if (!(end = strchr( p + 2, ')' ))) fatal_error( "syntax error in '%s'\n", expand );
            *end++ = 0;
            if (strchr( p + 2, ':' )) fatal_error( "pattern replacement not supported for '%s'\n", p + 2 );
            var = get_make_variable( make, p + 2 );
            tmp = replace_substr( expand, p, end - p, var ? var : "" );
            /* switch to the new string */
            p = tmp + (p - expand);
            free( expand );
            expand = tmp;
        }
        else if (p[1] == '{')  /* don't expand ${} variables */
        {
            if (!(end = strchr( p + 2, '}' ))) fatal_error( "syntax error in '%s'\n", expand );
            p = end + 1;
        }
        else if (p[1] == '$')
        {
            p += 2;
        }
        else fatal_error( "syntax error in '%s'\n", expand );
    }

    /* consider empty variables undefined */
    p = expand;
    while (*p && isspace(*p)) p++;
    if (*p) return expand;
    free( expand );
    return NULL;
}


/*******************************************************************
 *         get_expanded_make_var_array
 */
static struct strarray get_expanded_make_var_array( const struct makefile *make, const char *name )
{
    struct strarray ret = empty_strarray;
    char *value, *token;

    if ((value = get_expanded_make_variable( make, name )))
        for (token = strtok( value, " \t" ); token; token = strtok( NULL, " \t" ))
            strarray_add( &ret, token );
    return ret;
}


/*******************************************************************
 *         get_expanded_file_local_var
 */
static struct strarray get_expanded_file_local_var( const struct makefile *make, const char *file,
                                                    const char *name )
{
    char *p, *var = strmake( "%s_%s", file, name );

    for (p = var; *p; p++) if (!isalnum( *p )) *p = '_';
    return get_expanded_make_var_array( make, var );
}


/*******************************************************************
 *         set_make_variable
 */
static int set_make_variable( struct strarray *array, const char *assignment )
{
    char *p, *name;

    p = name = xstrdup( assignment );
    while (isalnum(*p) || *p == '_') p++;
    if (name == p) return 0;  /* not a variable */
    if (isspace(*p))
    {
        *p++ = 0;
        while (isspace(*p)) p++;
    }
    if (*p != '=') return 0;  /* not an assignment */
    *p++ = 0;
    while (isspace(*p)) p++;

    strarray_set_value( array, name, p );
    return 1;
}


/*******************************************************************
 *         parse_makefile
 */
static struct makefile *parse_makefile( const char *path )
{
    char *buffer;
    FILE *file;
    struct makefile *make = xmalloc( sizeof(*make) );

    memset( make, 0, sizeof(*make) );
    if (path)
    {
        make->top_obj_dir = get_relative_path( path, "" );
        make->base_dir = path;
        if (!strcmp( make->base_dir, "." )) make->base_dir = NULL;
    }

    file = open_input_makefile( make );
    while ((buffer = get_line( file )))
    {
        if (!strncmp( buffer, separator, strlen(separator) )) break;
        if (*buffer == '\t') continue;  /* command */
        while (isspace( *buffer )) buffer++;
        if (*buffer == '#') continue;  /* comment */
        set_make_variable( &make->vars, buffer );
    }
    fclose( file );
    input_file_name = NULL;
    return make;
}


/*******************************************************************
 *         add_generated_sources
 */
static void add_generated_sources( struct makefile *make )
{
    struct incl_file *source, *next, *file;

    LIST_FOR_EACH_ENTRY_SAFE( source, next, &make->sources, struct incl_file, entry )
    {
        if (source->file->flags & FLAG_IDL_CLIENT)
        {
            file = add_generated_source( make, replace_extension( source->name, ".idl", "_c.c" ), NULL );
            add_dependency( file->file, replace_extension( source->name, ".idl", ".h" ), INCL_NORMAL );
            add_all_includes( make, file, file->file );
        }
        if (source->file->flags & FLAG_IDL_SERVER)
        {
            file = add_generated_source( make, replace_extension( source->name, ".idl", "_s.c" ), NULL );
            add_dependency( file->file, "wine/exception.h", INCL_NORMAL );
            add_dependency( file->file, replace_extension( source->name, ".idl", ".h" ), INCL_NORMAL );
            add_all_includes( make, file, file->file );
        }
        if (source->file->flags & FLAG_IDL_IDENT)
        {
            file = add_generated_source( make, replace_extension( source->name, ".idl", "_i.c" ), NULL );
            add_dependency( file->file, "rpc.h", INCL_NORMAL );
            add_dependency( file->file, "rpcndr.h", INCL_NORMAL );
            add_dependency( file->file, "guiddef.h", INCL_NORMAL );
            add_all_includes( make, file, file->file );
        }
        if (source->file->flags & FLAG_IDL_PROXY)
        {
            file = add_generated_source( make, "dlldata.o", "dlldata.c" );
            add_dependency( file->file, "objbase.h", INCL_NORMAL );
            add_dependency( file->file, "rpcproxy.h", INCL_NORMAL );
            add_all_includes( make, file, file->file );
            file = add_generated_source( make, replace_extension( source->name, ".idl", "_p.c" ), NULL );
            add_dependency( file->file, "objbase.h", INCL_NORMAL );
            add_dependency( file->file, "rpcproxy.h", INCL_NORMAL );
            add_dependency( file->file, "wine/exception.h", INCL_NORMAL );
            add_dependency( file->file, replace_extension( source->name, ".idl", ".h" ), INCL_NORMAL );
            add_all_includes( make, file, file->file );
        }
        if (source->file->flags & FLAG_IDL_TYPELIB)
        {
            add_generated_source( make, replace_extension( source->name, ".idl", ".tlb" ), NULL );
        }
        if (source->file->flags & FLAG_IDL_REGTYPELIB)
        {
            add_generated_source( make, replace_extension( source->name, ".idl", "_t.res" ), NULL );
        }
        if (source->file->flags & FLAG_IDL_REGISTER)
        {
            add_generated_source( make, replace_extension( source->name, ".idl", "_r.res" ), NULL );
        }
        if (source->file->flags & FLAG_IDL_HEADER)
        {
            add_generated_source( make, replace_extension( source->name, ".idl", ".h" ), NULL );
        }
        if (!source->file->flags && strendswith( source->name, ".idl" ))
        {
            add_generated_source( make, replace_extension( source->name, ".idl", ".h" ), NULL );
        }
        if (strendswith( source->name, ".x" ))
        {
            add_generated_source( make, replace_extension( source->name, ".x", ".h" ), NULL );
        }
        if (strendswith( source->name, ".y" ))
        {
            file = add_generated_source( make, replace_extension( source->name, ".y", ".tab.c" ), NULL );
            /* steal the includes list from the source file */
            file->files_count = source->files_count;
            file->files_size = source->files_size;
            file->files = source->files;
            source->files_count = source->files_size = 0;
            source->files = NULL;
        }
        if (strendswith( source->name, ".l" ))
        {
            file = add_generated_source( make, replace_extension( source->name, ".l", ".yy.c" ), NULL );
            /* steal the includes list from the source file */
            file->files_count = source->files_count;
            file->files_size = source->files_size;
            file->files = source->files;
            source->files_count = source->files_size = 0;
            source->files = NULL;
        }
        if (source->file->flags & FLAG_C_IMPLIB)
        {
            if (!make->staticimplib && make->importlib && *dll_ext)
                make->staticimplib = strmake( "lib%s.a", make->importlib );
        }
        if (strendswith( source->name, ".po" ))
        {
            if (!make->disabled)
                strarray_add_uniq( &linguas, replace_extension( source->name, ".po", "" ));
        }
    }
    if (make->testdll)
    {
        file = add_generated_source( make, "testlist.o", "testlist.c" );
        add_dependency( file->file, "wine/test.h", INCL_NORMAL );
        add_all_includes( make, file, file->file );
    }
}


/*******************************************************************
 *         create_dir
 */
static void create_dir( const char *dir )
{
    char *p, *path;

    p = path = xstrdup( dir );
    while ((p = strchr( p, '/' )))
    {
        *p = 0;
        if (mkdir( path, 0755 ) == -1 && errno != EEXIST) fatal_perror( "mkdir %s", path );
        *p++ = '/';
        while (*p == '/') p++;
    }
    if (mkdir( path, 0755 ) == -1 && errno != EEXIST) fatal_perror( "mkdir %s", path );
    free( path );
}


/*******************************************************************
 *         create_file_directories
 *
 * Create the base directories of all the files.
 */
static void create_file_directories( const struct makefile *make, struct strarray files )
{
    struct strarray subdirs = empty_strarray;
    unsigned int i;
    char *dir;

    for (i = 0; i < files.count; i++)
    {
        if (!strchr( files.str[i], '/' )) continue;
        dir = base_dir_path( make, files.str[i] );
        *strrchr( dir, '/' ) = 0;
        strarray_add_uniq( &subdirs, dir );
    }

    for (i = 0; i < subdirs.count; i++) create_dir( subdirs.str[i] );
}


/*******************************************************************
 *         output_filenames_obj_dir
 */
static void output_filenames_obj_dir( const struct makefile *make, struct strarray array )
{
    unsigned int i;

    for (i = 0; i < array.count; i++) output_filename( obj_dir_path( make, array.str[i] ));
}


/*******************************************************************
 *         get_dependencies
 */
static void get_dependencies( struct strarray *deps, struct incl_file *file, struct incl_file *source )
{
    unsigned int i;

    if (!file->filename) return;

    if (file != source)
    {
        if (file->owner == source) return;  /* already processed */
        if (file->type == INCL_IMPORTLIB &&
            !(source->file->flags & (FLAG_IDL_TYPELIB | FLAG_IDL_REGTYPELIB)))
            return;  /* library is imported only when building a typelib */
        file->owner = source;
        strarray_add( deps, file->filename );
    }
    for (i = 0; i < file->files_count; i++) get_dependencies( deps, file->files[i], source );
}


/*******************************************************************
 *         get_local_dependencies
 *
 * Get the local dependencies of a given target.
 */
static struct strarray get_local_dependencies( const struct makefile *make, const char *name,
                                               struct strarray targets )
{
    unsigned int i;
    struct strarray deps = get_expanded_file_local_var( make, name, "DEPS" );

    for (i = 0; i < deps.count; i++)
    {
        if (strarray_exists( &targets, deps.str[i] ))
            deps.str[i] = obj_dir_path( make, deps.str[i] );
        else
            deps.str[i] = src_dir_path( make, deps.str[i] );
    }
    return deps;
}


/*******************************************************************
 *         get_static_lib
 *
 * Check if makefile builds the named static library and return the full lib path.
 */
static const char *get_static_lib( const struct makefile *make, const char *name )
{
    if (!make->staticlib) return NULL;
    if (strncmp( make->staticlib, "lib", 3 )) return NULL;
    if (strncmp( make->staticlib + 3, name, strlen(name) )) return NULL;
    if (strcmp( make->staticlib + 3 + strlen(name), ".a" )) return NULL;
    return base_dir_path( make, make->staticlib );
}


/*******************************************************************
 *         add_default_libraries
 */
static struct strarray add_default_libraries( const struct makefile *make, struct strarray *deps )
{
    struct strarray ret = empty_strarray;
    struct strarray all_libs = empty_strarray;
    unsigned int i, j;

    strarray_add( &all_libs, "-lwine_port" );
    strarray_addall( &all_libs, get_expanded_make_var_array( make, "EXTRALIBS" ));
    strarray_addall( &all_libs, libs );

    for (i = 0; i < all_libs.count; i++)
    {
        const char *lib = NULL;

        if (!strncmp( all_libs.str[i], "-l", 2 ))
        {
            const char *name = all_libs.str[i] + 2;

            for (j = 0; j < top_makefile->subdirs.count; j++)
            {
                const struct makefile *submake = top_makefile->submakes[j];

                if ((lib = get_static_lib( submake, name ))) break;
            }
        }

        if (lib)
        {
            lib = top_obj_dir_path( make, lib );
            strarray_add( deps, lib );
            strarray_add( &ret, lib );
        }
        else strarray_add( &ret, all_libs.str[i] );
    }
    return ret;
}


/*******************************************************************
 *         add_import_libs
 */
static struct strarray add_import_libs( const struct makefile *make, struct strarray *deps,
                                        struct strarray imports, int cross )
{
    struct strarray ret = empty_strarray;
    unsigned int i, j;

    for (i = 0; i < imports.count; i++)
    {
        const char *name = imports.str[i];
        const char *lib = NULL;

        for (j = 0; j < top_makefile->subdirs.count; j++)
        {
            const struct makefile *submake = top_makefile->submakes[j];

            if (submake->importlib && !strcmp( submake->importlib, name ))
            {
                if (cross || !*dll_ext || submake->staticimplib)
                    lib = base_dir_path( submake, strmake( "lib%s.a", name ));
                else
                    strarray_add( deps, top_obj_dir_path( make,
                                            strmake( "%s/lib%s.def", submake->base_dir, name )));
                break;
            }

            if ((lib = get_static_lib( submake, name ))) break;
        }

        if (lib)
        {
            if (cross) lib = replace_extension( lib, ".a", ".cross.a" );
            lib = top_obj_dir_path( make, lib );
            strarray_add( deps, lib );
            strarray_add( &ret, lib );
        }
        else strarray_add( &ret, strmake( "-l%s", name ));
    }
    return ret;
}


/*******************************************************************
 *         get_default_imports
 */
static struct strarray get_default_imports( const struct makefile *make )
{
    struct strarray ret = empty_strarray;

    if (strarray_exists( &make->extradllflags, "-nodefaultlibs" )) return ret;
    if (strarray_exists( &make->appmode, "-mno-cygwin" )) strarray_add( &ret, "msvcrt" );
    if (make->is_win16) strarray_add( &ret, "kernel" );
    strarray_add( &ret, "kernel32" );
    strarray_add( &ret, "ntdll" );
    strarray_add( &ret, "winecrt0" );
    return ret;
}


/*******************************************************************
 *         add_install_rule
 */
static void add_install_rule( const struct makefile *make, struct strarray *install_rules,
                              const char *target, const char *file, const char *dest )
{
    if (strarray_exists( &make->install_lib, target ))
    {
        strarray_add( &install_rules[INSTALL_LIB], file );
        strarray_add( &install_rules[INSTALL_LIB], dest );
    }
    else if (strarray_exists( &make->install_dev, target ))
    {
        strarray_add( &install_rules[INSTALL_DEV], file );
        strarray_add( &install_rules[INSTALL_DEV], dest );
    }
}


/*******************************************************************
 *         get_include_install_path
 *
 * Determine the installation path for a given include file.
 */
static const char *get_include_install_path( const char *name )
{
    if (!strncmp( name, "wine/", 5 )) return name + 5;
    if (!strncmp( name, "msvcrt/", 7 )) return name;
    return strmake( "windows/%s", name );
}


/*******************************************************************
 *         get_shared_library_name
 *
 * Determine possible names for a shared library with a version number.
 */
static struct strarray get_shared_lib_names( const char *libname )
{
    struct strarray ret = empty_strarray;
    const char *ext, *p;
    char *name, *first, *second;
    size_t len = 0;

    strarray_add( &ret, libname );

    for (p = libname; (p = strchr( p, '.' )); p++)
        if ((len = strspn( p + 1, "0123456789." ))) break;

    if (!len) return ret;
    ext = p + 1 + len;
    if (*ext && ext[-1] == '.') ext--;

    /* keep only the first group of digits */
    name = xstrdup( libname );
    first = name + (p - libname);
    if ((second = strchr( first + 1, '.' )))
    {
        strcpy( second, ext );
        strarray_add( &ret, xstrdup( name ));
    }
    /* now remove all digits */
    strcpy( first, ext );
    strarray_add( &ret, name );
    return ret;
}


/*******************************************************************
 *         output_symlink_rule
 *
 * Output a rule to create a symlink.
 */
static void output_symlink_rule( const char *src_name, const char *link_name )
{
    const char *name;

    output( "\trm -f %s && ", link_name );

    /* dest path with a directory needs special handling if ln -s isn't supported */
    if (strcmp( ln_s, "ln -s" ) && ((name = strrchr( link_name, '/' ))))
    {
        char *dir = xstrdup( link_name );
        dir[name - link_name] = 0;
        output( "cd %s && %s %s %s\n", *dir ? dir : "/", ln_s, src_name, name + 1 );
        free( dir );
    }
    else
    {
        output( "%s %s %s\n", ln_s, src_name, link_name );
    }
}


/*******************************************************************
 *         output_install_rules
 *
 * Rules are stored as a (file,dest) pair of values.
 * The first char of dest indicates the type of install.
 */
static struct strarray output_install_rules( const struct makefile *make, struct strarray files,
                                             const char *target, struct strarray *phony_targets )
{
    unsigned int i;
    char *install_sh;
    struct strarray uninstall = empty_strarray;
    struct strarray targets = empty_strarray;

    if (!files.count) return uninstall;

    for (i = 0; i < files.count; i += 2)
    {
        const char *file = files.str[i];
        switch (*files.str[i + 1])
        {
        case 'd':  /* data file */
        case 'p':  /* program file */
        case 's':  /* script */
            strarray_add_uniq( &targets, obj_dir_path( make, file ));
            break;
        case 't':  /* script in tools dir */
            strarray_add_uniq( &targets, tools_dir_path( make, file ));
            break;
        }
    }

    output( "install %s::", target );
    output_filenames( targets );
    output( "\n" );

    install_sh = top_src_dir_path( make, "tools/install-sh" );
    for (i = 0; i < files.count; i += 2)
    {
        const char *file = files.str[i];
        const char *dest = strmake( "$(DESTDIR)%s", files.str[i + 1] + 1 );

        switch (*files.str[i + 1])
        {
        case 'd':  /* data file */
            output( "\t%s -m 644 $(INSTALL_DATA_FLAGS) %s %s\n",
                    install_sh, obj_dir_path( make, file ), dest );
            break;
        case 'D':  /* data file in source dir */
            output( "\t%s -m 644 $(INSTALL_DATA_FLAGS) %s %s\n",
                    install_sh, src_dir_path( make, file ), dest );
            break;
        case 'p':  /* program file */
            output( "\tSTRIPPROG=\"$(STRIP)\" %s $(INSTALL_PROGRAM_FLAGS) %s %s\n",
                    install_sh, obj_dir_path( make, file ), dest );
            break;
        case 's':  /* script */
            output( "\t%s $(INSTALL_SCRIPT_FLAGS) %s %s\n",
                    install_sh, obj_dir_path( make, file ), dest );
            break;
        case 'S':  /* script in source dir */
            output( "\t%s $(INSTALL_SCRIPT_FLAGS) %s %s\n",
                    install_sh, src_dir_path( make, file ), dest );
            break;
        case 't':  /* script in tools dir */
            output( "\t%s $(INSTALL_SCRIPT_FLAGS) %s %s\n",
                    install_sh, tools_dir_path( make, file ), dest );
            break;
        case 'y':  /* symlink */
            output_symlink_rule( file, dest );
            break;
        default:
            assert(0);
        }
        strarray_add( &uninstall, dest );
    }

    strarray_add_uniq( phony_targets, "install" );
    strarray_add_uniq( phony_targets, target );
    return uninstall;
}


/*******************************************************************
 *         output_importlib_symlinks
 */
static struct strarray output_importlib_symlinks( const struct makefile *parent,
                                                  const struct makefile *make )
{
    struct strarray ret = empty_strarray;
    const char *lib, *dst;

    if (!make->module) return ret;
    if (!make->importlib) return ret;
    if (make->is_win16 && make->disabled) return ret;
    if (strncmp( make->base_dir, "dlls/", 5 )) return ret;
    if (!strcmp( make->module, make->importlib )) return ret;
    if (!strchr( make->importlib, '.' ) &&
        !strncmp( make->module, make->importlib, strlen( make->importlib )) &&
        !strcmp( make->module + strlen( make->importlib ), ".dll" ))
        return ret;

    lib = strmake( "lib%s.%s", make->importlib, *dll_ext ? "def" : "a" );
    dst = concat_paths( obj_dir_path( parent, "dlls" ), lib );
    output( "%s: %s\n", dst, base_dir_path( make, lib ));
    output_symlink_rule( concat_paths( make->base_dir + strlen("dlls/"), lib ), dst );
    strarray_add( &ret, dst );

    if (crosstarget && !make->is_win16)
    {
        lib = strmake( "lib%s.cross.a", make->importlib );
        dst = concat_paths( obj_dir_path( parent, "dlls" ), lib );
        output( "%s: %s\n", dst, base_dir_path( make, lib ));
        output_symlink_rule( concat_paths( make->base_dir + strlen("dlls/"), lib ), dst );
        strarray_add( &ret, dst );
    }
    return ret;
}


/*******************************************************************
 *         output_po_files
 */
static void output_po_files( const struct makefile *make )
{
    const char *po_dir = src_dir_path( make, "po" );
    struct strarray pot_files = empty_strarray;
    struct incl_file *source;
    unsigned int i;

    for (i = 0; i < make->subdirs.count; i++)
    {
        struct makefile *submake = make->submakes[i];

        LIST_FOR_EACH_ENTRY( source, &submake->sources, struct incl_file, entry )
        {
            if (strendswith( source->name, ".rc" ) && (source->file->flags & FLAG_RC_PO))
            {
                char *pot_file = replace_extension( source->name, ".rc", ".pot" );
                char *pot_path = base_dir_path( submake, pot_file );
                output( "%s: tools/wrc include dummy\n", pot_path );
                output( "\t@cd %s && $(MAKE) %s\n", base_dir_path( submake, "" ), pot_file );
                strarray_add( &pot_files, pot_path );
            }
            else if (strendswith( source->name, ".mc" ))
            {
                char *pot_file = replace_extension( source->name, ".mc", ".pot" );
                char *pot_path = base_dir_path( submake, pot_file );
                output( "%s: tools/wmc include dummy\n", pot_path );
                output( "\t@cd %s && $(MAKE) %s\n", base_dir_path( submake, "" ), pot_file );
                strarray_add( &pot_files, pot_path );
            }
        }
    }
    if (linguas.count)
    {
        for (i = 0; i < linguas.count; i++)
            output_filename( strmake( "%s/%s.po", po_dir, linguas.str[i] ));
        output( ": %s/wine.pot\n", po_dir );
        output( "\tmsgmerge --previous -q $@ %s/wine.pot | msgattrib --no-obsolete -o $@.new && mv $@.new $@\n",
                po_dir );
        output( "po:" );
        for (i = 0; i < linguas.count; i++)
            output_filename( strmake( "%s/%s.po", po_dir, linguas.str[i] ));
        output( "\n" );
    }
    output( "%s/wine.pot:", po_dir );
    output_filenames( pot_files );
    output( "\n" );
    output( "\tmsgcat -o $@" );
    output_filenames( pot_files );
    output( "\n" );
}


/*******************************************************************
 *         output_sources
 */
static struct strarray output_sources( const struct makefile *make )
{
    struct incl_file *source;
    unsigned int i, j;
    struct strarray object_files = empty_strarray;
    struct strarray crossobj_files = empty_strarray;
    struct strarray res_files = empty_strarray;
    struct strarray clean_files = empty_strarray;
    struct strarray uninstall_files = empty_strarray;
    struct strarray mo_files = empty_strarray;
    struct strarray ok_files = empty_strarray;
    struct strarray in_files = empty_strarray;
    struct strarray dlldata_files = empty_strarray;
    struct strarray c2man_files = empty_strarray;
    struct strarray implib_objs = empty_strarray;
    struct strarray includes = empty_strarray;
    struct strarray phony_targets = empty_strarray;
    struct strarray all_targets = empty_strarray;
    struct strarray resource_dlls = get_expanded_make_var_array( make, "RC_DLLS" );
    struct strarray install_rules[NB_INSTALL_RULES];
    char *ldrpath_local   = get_expanded_make_variable( make, "LDRPATH_LOCAL" );
    char *ldrpath_install = get_expanded_make_variable( make, "LDRPATH_INSTALL" );

    for (i = 0; i < NB_INSTALL_RULES; i++) install_rules[i] = empty_strarray;

    for (i = 0; i < linguas.count; i++)
        strarray_add( &mo_files, strmake( "%s/%s.mo", top_obj_dir_path( make, "po" ), linguas.str[i] ));

    strarray_add( &phony_targets, "all" );
    strarray_add( &includes, strmake( "-I%s", obj_dir_path( make, "" )));
    if (make->src_dir) strarray_add( &includes, strmake( "-I%s", make->src_dir ));
    if (make->parent_dir) strarray_add( &includes, strmake( "-I%s", src_dir_path( make, make->parent_dir )));
    strarray_add( &includes, strmake( "-I%s", top_obj_dir_path( make, "include" )));
    if (make->top_src_dir)
        strarray_add( &includes, strmake( "-I%s", top_src_dir_path( make, "include" )));
    if (make->use_msvcrt)
        strarray_add( &includes, strmake( "-I%s", top_src_dir_path( make, "include/msvcrt" )));
    for (i = 0; i < make->include_paths.count; i++)
        strarray_add( &includes, strmake( "-I%s", obj_dir_path( make, make->include_paths.str[i] )));

    LIST_FOR_EACH_ENTRY( source, &make->sources, struct incl_file, entry )
    {
        struct strarray dependencies = empty_strarray;
        struct strarray extradefs;
        char *obj = xstrdup( source->name );
        char *ext = get_extension( obj );

        if (!ext) fatal_error( "unsupported file type %s\n", source->name );
        *ext++ = 0;

        extradefs = get_expanded_file_local_var( make, obj, "EXTRADEFS" );
        get_dependencies( &dependencies, source, source );

        if (!strcmp( ext, "y" ))  /* yacc file */
        {
            /* add source file dependency for parallel makes */
            char *header = strmake( "%s.tab.h", obj );

            if (find_include_file( make, header ))
            {
                output( "%s: %s\n", obj_dir_path( make, header ), source->filename );
                output( "\t$(BISON) -p %s_ -o %s.tab.c -d %s\n",
                        obj, obj_dir_path( make, obj ), source->filename );
                output( "%s.tab.c: %s %s\n", obj_dir_path( make, obj ),
                        source->filename, obj_dir_path( make, header ));
                strarray_add( &clean_files, header );
            }
            else output( "%s.tab.c: %s\n", obj, source->filename );

            output( "\t$(BISON) -p %s_ -o $@ %s\n", obj, source->filename );
        }
        else if (!strcmp( ext, "x" ))  /* template file */
        {
            output( "%s.h: %s%s %s\n", obj_dir_path( make, obj ),
                    tools_dir_path( make, "make_xftmpl" ), tools_ext, source->filename );
            output( "\t%s%s -H -o $@ %s\n",
                    tools_dir_path( make, "make_xftmpl" ), tools_ext, source->filename );
            if (source->file->flags & FLAG_INSTALL)
            {
                strarray_add( &install_rules[INSTALL_DEV], source->name );
                strarray_add( &install_rules[INSTALL_DEV],
                              strmake( "D$(includedir)/%s", get_include_install_path( source->name ) ));
                strarray_add( &install_rules[INSTALL_DEV], strmake( "%s.h", obj ));
                strarray_add( &install_rules[INSTALL_DEV],
                              strmake( "d$(includedir)/%s.h", get_include_install_path( obj ) ));
            }
        }
        else if (!strcmp( ext, "l" ))  /* lex file */
        {
            output( "%s.yy.c: %s\n", obj_dir_path( make, obj ), source->filename );
            output( "\t$(FLEX) -o$@ %s\n", source->filename );
        }
        else if (!strcmp( ext, "rc" ))  /* resource file */
        {
            strarray_add( &res_files, strmake( "%s.res", obj ));
            output( "%s.res: %s\n", obj_dir_path( make, obj ), source->filename );
            output( "\t%s -o $@", tools_path( make, "wrc" ) );
            if (make->is_win16) output_filename( "-m16" );
            else output_filenames( target_flags );
            output_filename( "--nostdinc" );
            output_filenames( includes );
            output_filenames( make->define_args );
            output_filenames( extradefs );
            if (mo_files.count && (source->file->flags & FLAG_RC_PO))
            {
                output_filename( strmake( "--po-dir=%s", top_obj_dir_path( make, "po" )));
                output_filename( source->filename );
                output( "\n" );
                output( "%s.res:", obj_dir_path( make, obj ));
                output_filenames( mo_files );
                output( "\n" );
            }
            else
            {
                output_filename( source->filename );
                output( "\n" );
            }
            if (source->file->flags & FLAG_RC_PO)
            {
                strarray_add( &clean_files, strmake( "%s.pot", obj ));
                output( "%s.pot: %s\n", obj_dir_path( make, obj ), source->filename );
                output( "\t%s -O pot -o $@", tools_path( make, "wrc" ) );
                if (make->is_win16) output_filename( "-m16" );
                else output_filenames( target_flags );
                output_filename( "--nostdinc" );
                output_filenames( includes );
                output_filenames( make->define_args );
                output_filenames( extradefs );
                output_filename( source->filename );
                output( "\n" );
                output( "%s.pot ", obj_dir_path( make, obj ));
            }
            output( "%s.res:", obj_dir_path( make, obj ));
            output_filename( tools_path( make, "wrc" ));
            output_filenames( dependencies );
            output( "\n" );
        }
        else if (!strcmp( ext, "mc" ))  /* message file */
        {
            strarray_add( &res_files, strmake( "%s.res", obj ));
            strarray_add( &clean_files, strmake( "%s.pot", obj ));
            output( "%s.res: %s\n", obj_dir_path( make, obj ), source->filename );
            output( "\t%s -U -O res -o $@ %s", tools_path( make, "wmc" ), source->filename );
            if (mo_files.count)
            {
                output_filename( strmake( "--po-dir=%s", top_obj_dir_path( make, "po" )));
                output( "\n" );
                output( "%s.res:", obj_dir_path( make, obj ));
                output_filenames( mo_files );
            }
            output( "\n" );
            output( "%s.pot: %s\n", obj_dir_path( make, obj ), source->filename );
            output( "\t%s -O pot -o $@ %s", tools_path( make, "wmc" ), source->filename );
            output( "\n" );
            output( "%s.pot %s.res:", obj_dir_path( make, obj ), obj_dir_path( make, obj ));
            output_filename( tools_path( make, "wmc" ));
            output_filenames( dependencies );
            output( "\n" );
        }
        else if (!strcmp( ext, "idl" ))  /* IDL file */
        {
            struct strarray targets = empty_strarray;
            char *dest;

            if (!source->file->flags) source->file->flags |= FLAG_IDL_HEADER | FLAG_INSTALL;
            if (find_include_file( make, strmake( "%s.h", obj ))) source->file->flags |= FLAG_IDL_HEADER;

            for (i = 0; i < sizeof(idl_outputs) / sizeof(idl_outputs[0]); i++)
            {
                if (!(source->file->flags & idl_outputs[i].flag)) continue;
                dest = strmake( "%s%s", obj, idl_outputs[i].ext );
                if (!find_src_file( make, dest )) strarray_add( &clean_files, dest );
                strarray_add( &targets, dest );
            }
            if (source->file->flags & FLAG_IDL_PROXY) strarray_add( &dlldata_files, source->name );
            if (source->file->flags & FLAG_INSTALL)
            {
                strarray_add( &install_rules[INSTALL_DEV], xstrdup( source->name ));
                strarray_add( &install_rules[INSTALL_DEV],
                              strmake( "D$(includedir)/%s.idl", get_include_install_path( obj ) ));
                if (source->file->flags & FLAG_IDL_HEADER)
                {
                    strarray_add( &install_rules[INSTALL_DEV], strmake( "%s.h", obj ));
                    strarray_add( &install_rules[INSTALL_DEV],
                                  strmake( "d$(includedir)/%s.h", get_include_install_path( obj ) ));
                }
            }
            if (!targets.count) continue;
            output_filenames_obj_dir( make, targets );
            output( ": %s\n", tools_path( make, "widl" ));
            output( "\t%s -o $@", tools_path( make, "widl" ) );
            output_filenames( target_flags );
            output_filenames( includes );
            output_filenames( make->define_args );
            output_filenames( extradefs );
            output_filenames( get_expanded_make_var_array( make, "EXTRAIDLFLAGS" ));
            output_filename( source->filename );
            output( "\n" );
            output_filenames_obj_dir( make, targets );
            output( ": %s", source->filename );
            output_filenames( dependencies );
            output( "\n" );
        }
        else if (!strcmp( ext, "in" ))  /* .in file or man page */
        {
            if (strendswith( obj, ".man" ) && source->file->args)
            {
                struct strarray symlinks;
                char *dir, *dest = replace_extension( obj, ".man", "" );
                char *lang = strchr( dest, '.' );
                char *section = source->file->args;
                if (lang)
                {
                    *lang++ = 0;
                    dir = strmake( "$(mandir)/%s/man%s", lang, section );
                }
                else dir = strmake( "$(mandir)/man%s", section );
                add_install_rule( make, install_rules, dest, xstrdup(obj),
                                  strmake( "d%s/%s.%s", dir, dest, section ));
                symlinks = get_expanded_file_local_var( make, dest, "SYMLINKS" );
                for (i = 0; i < symlinks.count; i++)
                    add_install_rule( make, install_rules, symlinks.str[i],
                                      strmake( "%s.%s", dest, section ),
                                      strmake( "y%s/%s.%s", dir, symlinks.str[i], section ));
                free( dest );
                free( dir );
            }
            strarray_add( &in_files, xstrdup(obj) );
            strarray_add( &all_targets, xstrdup(obj) );
            output( "%s: %s\n", obj_dir_path( make, obj ), source->filename );
            output( "\t$(SED_CMD) %s >$@ || (rm -f $@ && false)\n", source->filename );
            output( "%s:", obj_dir_path( make, obj ));
            output_filenames( dependencies );
            output( "\n" );
            add_install_rule( make, install_rules, obj, xstrdup( obj ),
                              strmake( "d$(datadir)/wine/%s", obj ));
        }
        else if (!strcmp( ext, "sfd" ))  /* font file */
        {
            char *ttf_file = src_dir_path( make, strmake( "%s.ttf", obj ));
            if (fontforge && !make->src_dir)
            {
                output( "%s: %s\n", ttf_file, source->filename );
                output( "\t%s -script %s %s $@\n",
                        fontforge, top_src_dir_path( make, "fonts/genttf.ff" ), source->filename );
                if (!(source->file->flags & FLAG_SFD_FONTS)) output( "all: %s\n", ttf_file );
            }
            if (source->file->flags & FLAG_INSTALL)
            {
                strarray_add( &install_rules[INSTALL_LIB], strmake( "%s.ttf", obj ));
                strarray_add( &install_rules[INSTALL_LIB], strmake( "D$(fontdir)/%s.ttf", obj ));
            }
            if (source->file->flags & FLAG_SFD_FONTS)
            {
                struct strarray *array = source->file->args;

                for (i = 0; i < array->count; i++)
                {
                    char *font = strtok( xstrdup(array->str[i]), " \t" );
                    char *args = strtok( NULL, "" );

                    strarray_add( &all_targets, xstrdup( font ));
                    output( "%s: %s %s\n", obj_dir_path( make, font ),
                            tools_path( make, "sfnt2fon" ), ttf_file );
                    output( "\t%s -o $@ %s %s\n", tools_path( make, "sfnt2fon" ), ttf_file, args );
                    strarray_add( &install_rules[INSTALL_LIB], xstrdup(font) );
                    strarray_add( &install_rules[INSTALL_LIB], strmake( "d$(fontdir)/%s", font ));
                }
            }
        }
        else if (!strcmp( ext, "svg" ))  /* svg file */
        {
            if (convert && rsvg && icotool && !make->src_dir)
            {
                output( "%s.ico %s.bmp: %s\n",
                        src_dir_path( make, obj ), src_dir_path( make, obj ), source->filename );
                output( "\tCONVERT=\"%s\" ICOTOOL=\"%s\" RSVG=\"%s\" %s %s $@\n", convert, icotool, rsvg,
                        top_src_dir_path( make, "tools/buildimage" ), source->filename );
            }
        }
        else if (!strcmp( ext, "po" ))  /* po file */
        {
            output( "%s.mo: %s\n", obj_dir_path( make, obj ), source->filename );
            output( "\t%s -o $@ %s\n", msgfmt, source->filename );
            strarray_add( &all_targets, strmake( "%s.mo", obj ));
        }
        else if (!strcmp( ext, "res" ))
        {
            strarray_add( &res_files, source->name );
        }
        else if (!strcmp( ext, "tlb" ))
        {
            strarray_add( &all_targets, source->name );
        }
        else if (!strcmp( ext, "h" ) || !strcmp( ext, "rh" ) || !strcmp( ext, "inl" ))  /* header file */
        {
            if (source->file->flags & FLAG_GENERATED)
            {
                strarray_add( &all_targets, source->name );
            }
            else
            {
                strarray_add( &install_rules[INSTALL_DEV], source->name );
                strarray_add( &install_rules[INSTALL_DEV],
                              strmake( "D$(includedir)/%s", get_include_install_path( source->name ) ));
            }
        }
        else
        {
            int need_cross = make->testdll || make->resource ||
                (source->file->flags & FLAG_C_IMPLIB) ||
                (make->module && make->staticlib);

            if ((source->file->flags & FLAG_GENERATED) &&
                (!make->testdll || !strendswith( source->filename, "testlist.c" )))
                strarray_add( &clean_files, source->filename );
            if (source->file->flags & FLAG_C_IMPLIB) strarray_add( &implib_objs, strmake( "%s.o", obj ));
            strarray_add( &object_files, strmake( "%s.o", obj ));
            output( "%s.o: %s\n", obj_dir_path( make, obj ), source->filename );
            output( "\t$(CC) -c -o $@ %s", source->filename );
            output_filenames( includes );
            output_filenames( make->define_args );
            output_filenames( extradefs );
            if (make->module || make->staticlib || make->sharedlib || make->testdll || make->resource)
            {
                output_filenames( dll_flags );
                if (make->use_msvcrt) output_filenames( msvcrt_flags );
            }
            output_filenames( extra_cflags );
            output_filenames( cpp_flags );
            output_filename( "$(CFLAGS)" );
            output( "\n" );
            if (crosstarget && need_cross)
            {
                strarray_add( &crossobj_files, strmake( "%s.cross.o", obj ));
                output( "%s.cross.o: %s\n", obj_dir_path( make, obj ), source->filename );
                output( "\t$(CROSSCC) -c -o $@ %s", source->filename );
                output_filenames( includes );
                output_filenames( make->define_args );
                output_filenames( extradefs );
                if (make->use_msvcrt) output_filenames( msvcrt_flags );
                output_filename( "-DWINE_CROSSTEST" );
                output_filenames( cpp_flags );
                output_filename( "$(CFLAGS)" );
                output( "\n" );
            }
            if (make->testdll && !strcmp( ext, "c" ) && !(source->file->flags & FLAG_GENERATED))
            {
                strarray_add( &ok_files, strmake( "%s.ok", obj ));
                output( "%s.ok:\n", obj_dir_path( make, obj ));
                output( "\t%s $(RUNTESTFLAGS) -T %s -M %s -p %s%s %s && touch $@\n",
                        top_src_dir_path( make, "tools/runtest" ), top_obj_dir_path( make, "" ),
                        make->testdll, replace_extension( make->testdll, ".dll", "_test.exe" ),
                        dll_ext, obj );
            }
            if (!strcmp( ext, "c" ) && !(source->file->flags & FLAG_GENERATED))
                strarray_add( &c2man_files, source->filename );
            output( "%s.o", obj_dir_path( make, obj ));
            if (crosstarget && need_cross) output( " %s.cross.o", obj_dir_path( make, obj ));
            output( ":" );
            output_filenames( dependencies );
            output( "\n" );
        }
        free( obj );
    }

    /* rules for files that depend on multiple sources */

    if (dlldata_files.count)
    {
        output( "%s: %s %s\n", obj_dir_path( make, "dlldata.c" ),
                tools_path( make, "widl" ), src_dir_path( make, "Makefile.in" ));
        output( "\t%s --dlldata-only -o $@", tools_path( make, "widl" ));
        output_filenames( dlldata_files );
        output( "\n" );
    }

    /* rules for resource dlls, call Makefile in subdirectory */

    if (resource_dlls.count)
    {
        for (i = 0; i < resource_dlls.count; i++)
        {
            output( "%s/%s%s: %s/Makefile\n", resource_dlls.str[i],
                    resource_dlls.str[i], dll_ext, resource_dlls.str[i] );
            output( "\t@cd %s && $(MAKE) %s%s\n", resource_dlls.str[i],
                    resource_dlls.str[i], dll_ext );
        }

        output( "%s:", obj_dir_path( make, "resource_dlls.o" ));
        for (i = 0; i < resource_dlls.count; i++)
            output( " %s/%s%s", resource_dlls.str[i], resource_dlls.str[i], dll_ext );
        output( "\n" );
        output( "\t(" );
        for (i = 0; i < resource_dlls.count; i++)
        {
            if (i != 0) output( "; \\\n  " );
            output( "echo \"%s RCDATA %s/%s%s\"", resource_dlls.str[i],
                    resource_dlls.str[i], resource_dlls.str[i], dll_ext );
        }
        output( ") | %s -o $@\n", tools_path( make, "wrc" ) );
        strarray_add( &clean_files, "resource_dlls.o" );
        strarray_add( &object_files, "resource_dlls.o" );

        if (crosstarget && (make->testdll || (make->module && make->staticlib)))
        {
            for (i = 0; i < resource_dlls.count; i++)
            {
                char *name = prepend_extension( resource_dlls.str[i], "_crossres", ".dll" );
                output( "%s/%s: %s/Makefile\n", resource_dlls.str[i], name, resource_dlls.str[i] );
                output( "\t@cd %s && $(MAKE) %s\n", resource_dlls.str[i], name );
                free( name );
            }

            output( "%s:", obj_dir_path( make, "resource_dlls.cross.o" ));
            for (i = 0; i < resource_dlls.count; i++)
            {
                char *name = prepend_extension( resource_dlls.str[i], "_crossres", ".dll" );
                output( " %s/%s", resource_dlls.str[i], name );
                free( name );
            }
            output( "\n" );
            output( "\t(" );
            for (i = 0; i < resource_dlls.count; i++)
            {
                char *name = prepend_extension( resource_dlls.str[i], "_crossres", ".dll" );
                if (i != 0) output( "; \\\n  " );
                output( "echo \"%s RCDATA %s/%s\"", resource_dlls.str[i], resource_dlls.str[i], name );
                free( name );
            }
            output( ") | %s -o $@\n", tools_path( make, "wrc" ) );
            strarray_add( &clean_files, "resource_dlls.cross.o" );
            strarray_add( &crossobj_files, "resource_dlls.cross.o" );
        }
    }

    if (make->module && !make->staticlib)
    {
        struct strarray all_libs = empty_strarray;
        struct strarray dep_libs = empty_strarray;
        char *module_path = obj_dir_path( make, make->module );
        char *spec_file = NULL;

        if (!make->appmode.count)
        {
            if (!make->parent_spec)
                spec_file = src_dir_path( make, replace_extension( make->module, ".dll", ".spec" ));
            else
                spec_file = src_dir_path( make, make->parent_spec );
        }
        strarray_addall( &all_libs, add_import_libs( make, &dep_libs, make->delayimports, 0 ));
        strarray_addall( &all_libs, add_import_libs( make, &dep_libs, make->imports, 0 ));
        add_import_libs( make, &dep_libs, get_default_imports( make ), 0 );  /* dependencies only */
        strarray_addall( &all_libs, add_default_libraries( make, &dep_libs ));

        if (*dll_ext)
        {
            for (i = 0; i < make->delayimports.count; i++)
                strarray_add( &all_libs, strmake( "-Wb,-d%s", make->delayimports.str[i] ));
            strarray_add( &all_targets, strmake( "%s%s", make->module, dll_ext ));
            strarray_add( &all_targets, strmake( "%s.fake", make->module ));
            add_install_rule( make, install_rules, make->module, strmake( "%s%s", make->module, dll_ext ),
                              strmake( "p$(dlldir)/%s%s", make->module, dll_ext ));
            add_install_rule( make, install_rules, make->module, strmake( "%s.fake", make->module ),
                              strmake( "d$(fakedlldir)/%s", make->module ));
            output( "%s%s %s.fake:", module_path, dll_ext, module_path );
        }
        else
        {
            strarray_add( &all_libs, "-lwine" );
            strarray_add( &all_targets, make->module );
            add_install_rule( make, install_rules, make->module, make->module,
                              strmake( "p$(%s)/%s", spec_file ? "dlldir" : "bindir", make->module ));
            output( "%s:", module_path );
        }
        if (spec_file) output_filename( spec_file );
        output_filenames_obj_dir( make, object_files );
        output_filenames_obj_dir( make, res_files );
        output_filenames( dep_libs );
        output_filename( tools_path( make, "winebuild" ));
        output_filename( tools_path( make, "winegcc" ));
        output( "\n" );
        output( "\t%s -o $@", tools_path( make, "winegcc" ));
        output_filename( strmake( "-B%s", tools_dir_path( make, "winebuild" )));
        if (tools_dir) output_filename( strmake( "--sysroot=%s", top_obj_dir_path( make, "" )));
        output_filenames( target_flags );
        output_filenames( unwind_flags );
        if (spec_file)
        {
            output( " -shared %s", spec_file );
            output_filenames( make->extradllflags );
        }
        else output_filenames( make->appmode );
        output_filenames_obj_dir( make, object_files );
        output_filenames_obj_dir( make, res_files );
        output_filenames( all_libs );
        output_filename( "$(LDFLAGS)" );
        output( "\n" );

        if (spec_file && make->importlib)
        {
            char *importlib_path = obj_dir_path( make, strmake( "lib%s", make->importlib ));
            if (*dll_ext && !implib_objs.count)
            {
                strarray_add( &clean_files, strmake( "lib%s.def", make->importlib ));
                output( "%s.def: %s %s\n", importlib_path, tools_path( make, "winebuild" ), spec_file );
                output( "\t%s -w --def -o $@ --export %s", tools_path( make, "winebuild" ), spec_file );
                output_filenames( target_flags );
                if (make->is_win16) output_filename( "-m16" );
                output( "\n" );
                add_install_rule( make, install_rules, make->importlib,
                                  strmake( "lib%s.def", make->importlib ),
                                  strmake( "d$(dlldir)/lib%s.def", make->importlib ));
            }
            else
            {
                strarray_add( &clean_files, strmake( "lib%s.a", make->importlib ));
                output( "%s.a: %s %s", importlib_path, tools_path( make, "winebuild" ), spec_file );
                output_filenames_obj_dir( make, implib_objs );
                output( "\n" );
                output( "\t%s -w --implib -o $@ --export %s", tools_path( make, "winebuild" ), spec_file );
                output_filenames( target_flags );
                output_filenames_obj_dir( make, implib_objs );
                output( "\n" );
                add_install_rule( make, install_rules, make->importlib,
                                  strmake( "lib%s.a", make->importlib ),
                                  strmake( "d$(dlldir)/lib%s.a", make->importlib ));
            }
            if (crosstarget && !make->is_win16)
            {
                struct strarray cross_files = strarray_replace_extension( &implib_objs, ".o", ".cross.o" );
                strarray_add( &clean_files, strmake( "lib%s.cross.a", make->importlib ));
                output( "%s.cross.a: %s %s", importlib_path, tools_path( make, "winebuild" ), spec_file );
                output_filenames_obj_dir( make, cross_files );
                output( "\n" );
                output( "\t%s -b %s -w --implib -o $@ --export %s",
                        tools_path( make, "winebuild" ), crosstarget, spec_file );
                output_filenames_obj_dir( make, cross_files );
                output( "\n" );
            }
        }

        if (spec_file)
        {
            if (c2man_files.count)
            {
                output( "manpages::\n" );
                output( "\t%s -w %s", top_src_dir_path( make, "tools/c2man.pl" ), spec_file );
                output_filename( strmake( "-R%s", top_src_dir_path( make, "" )));
                output_filename( strmake( "-I%s", top_src_dir_path( make, "include" )));
                output_filename( strmake( "-o %s/man%s",
                                          top_obj_dir_path( make, "documentation" ), man_ext ));
                output_filenames( c2man_files );
                output( "\n" );
                output( "htmlpages::\n" );
                output( "\t%s -Th -w %s", top_src_dir_path( make, "tools/c2man.pl" ), spec_file );
                output_filename( strmake( "-R%s", top_src_dir_path( make, "" )));
                output_filename( strmake( "-I%s", top_src_dir_path( make, "include" )));
                output_filename( strmake( "-o %s",
                                          top_obj_dir_path( make, "documentation/html" )));
                output_filenames( c2man_files );
                output( "\n" );
                output( "sgmlpages::\n" );
                output( "\t%s -Ts -w %s", top_src_dir_path( make, "tools/c2man.pl" ), spec_file );
                output_filename( strmake( "-R%s", top_src_dir_path( make, "" )));
                output_filename( strmake( "-I%s", top_src_dir_path( make, "include" )));
                output_filename( strmake( "-o %s",
                                          top_obj_dir_path( make, "documentation/api-guide" )));
                output_filenames( c2man_files );
                output( "\n" );
                output( "xmlpages::\n" );
                output( "\t%s -Tx -w %s", top_src_dir_path( make, "tools/c2man.pl" ), spec_file );
                output_filename( strmake( "-R%s", top_src_dir_path( make, "" )));
                output_filename( strmake( "-I%s", top_src_dir_path( make, "include" )));
                output_filename( strmake( "-o %s",
                                          top_obj_dir_path( make, "documentation/api-guide-xml" )));
                output_filenames( c2man_files );
                output( "\n" );
                strarray_add( &phony_targets, "manpages" );
                strarray_add( &phony_targets, "htmlpages" );
                strarray_add( &phony_targets, "sgmlpages" );
                strarray_add( &phony_targets, "xmlpages" );
            }
            else output( "manpages htmlpages sgmlpages xmlpages::\n" );
        }
        else if (*dll_ext)
        {
            char *binary = replace_extension( make->module, ".exe", "" );
            add_install_rule( make, install_rules, binary, "wineapploader",
                              strmake( "t$(bindir)/%s", binary ));
        }
    }

    if (make->staticlib)
    {
        strarray_add( &all_targets, make->staticlib );
        output( "%s:", obj_dir_path( make, make->staticlib ));
        output_filenames_obj_dir( make, object_files );
        output( "\n\trm -f $@\n" );
        output( "\t$(AR) $(ARFLAGS) $@" );
        output_filenames_obj_dir( make, object_files );
        output( "\n\t$(RANLIB) $@\n" );
        add_install_rule( make, install_rules, make->staticlib, make->staticlib,
                          strmake( "d$(dlldir)/%s", make->staticlib ));
        if (crosstarget && make->module)
        {
            char *name = replace_extension( make->staticlib, ".a", ".cross.a" );

            strarray_add( &all_targets, name );
            output( "%s:", obj_dir_path( make, name ));
            output_filenames_obj_dir( make, crossobj_files );
            output( "\n\trm -f $@\n" );
            output( "\t%s-ar $(ARFLAGS) $@", crosstarget );
            output_filenames_obj_dir( make, crossobj_files );
            output( "\n\t%s-ranlib $@\n", crosstarget );
        }
    }

    if (make->sharedlib)
    {
        char *basename, *p;
        struct strarray names = get_shared_lib_names( make->sharedlib );
        struct strarray all_libs = empty_strarray;
        struct strarray dep_libs = empty_strarray;

        basename = xstrdup( make->sharedlib );
        if ((p = strchr( basename, '.' ))) *p = 0;

        strarray_addall( &dep_libs, get_local_dependencies( make, basename, in_files ));
        strarray_addall( &all_libs, get_expanded_file_local_var( make, basename, "LDFLAGS" ));
        strarray_addall( &all_libs, add_default_libraries( make, &dep_libs ));

        output( "%s:", obj_dir_path( make, make->sharedlib ));
        output_filenames_obj_dir( make, object_files );
        output_filenames( dep_libs );
        output( "\n" );
        output( "\t$(CC) -o $@" );
        output_filenames_obj_dir( make, object_files );
        output_filenames( all_libs );
        output_filename( "$(LDFLAGS)" );
        output( "\n" );
        add_install_rule( make, install_rules, make->sharedlib, make->sharedlib,
                          strmake( "p$(libdir)/%s", make->sharedlib ));
        for (i = 1; i < names.count; i++)
        {
            output( "%s: %s\n", obj_dir_path( make, names.str[i] ), obj_dir_path( make, names.str[i-1] ));
            output_symlink_rule( obj_dir_path( make, names.str[i-1] ), obj_dir_path( make, names.str[i] ));
            add_install_rule( make, install_rules, names.str[i], names.str[i-1],
                              strmake( "y$(libdir)/%s", names.str[i] ));
        }
        strarray_addall( &all_targets, names );
    }

    if (make->importlib && !make->module)  /* stand-alone import lib (for libwine) */
    {
        char *def_file = replace_extension( make->importlib, ".a", ".def" );

        if (!strncmp( def_file, "lib", 3 )) def_file += 3;
        output( "%s: %s\n", obj_dir_path( make, make->importlib ), src_dir_path( make, def_file ));
        output( "\t%s -l $@ -d %s\n", dlltool, src_dir_path( make, def_file ));
        add_install_rule( make, install_rules, make->importlib, make->importlib,
                          strmake( "d$(libdir)/%s", make->importlib ));
        strarray_add( &all_targets, make->importlib );
    }

    if (make->testdll)
    {
        char *testmodule = replace_extension( make->testdll, ".dll", "_test.exe" );
        char *stripped = replace_extension( make->testdll, ".dll", "_test-stripped.exe" );
        char *testres = replace_extension( make->testdll, ".dll", "_test.res" );
        struct strarray dep_libs = empty_strarray;
        struct strarray all_libs = add_import_libs( make, &dep_libs, make->imports, 0 );

        add_import_libs( make, &dep_libs, get_default_imports( make ), 0 );  /* dependencies only */
        strarray_addall( &all_libs, libs );
        strarray_add( &all_targets, strmake( "%s%s", testmodule, dll_ext ));
        strarray_add( &clean_files, strmake( "%s%s", stripped, dll_ext ));
        output( "%s%s:\n", obj_dir_path( make, testmodule ), dll_ext );
        output( "\t%s -o $@", tools_path( make, "winegcc" ));
        output_filename( strmake( "-B%s", tools_dir_path( make, "winebuild" )));
        if (tools_dir) output_filename( strmake( "--sysroot=%s", top_obj_dir_path( make, "" )));
        output_filenames( target_flags );
        output_filenames( unwind_flags );
        output_filenames( make->appmode );
        output_filenames_obj_dir( make, object_files );
        output_filenames_obj_dir( make, res_files );
        output_filenames( all_libs );
        output_filename( "$(LDFLAGS)" );
        output( "\n" );
        output( "%s%s:\n", obj_dir_path( make, stripped ), dll_ext );
        output( "\t%s -o $@", tools_path( make, "winegcc" ));
        output_filename( strmake( "-B%s", tools_dir_path( make, "winebuild" )));
        if (tools_dir) output_filename( strmake( "--sysroot=%s", top_obj_dir_path( make, "" )));
        output_filenames( target_flags );
        output_filenames( unwind_flags );
        output_filename( strmake( "-Wb,-F,%s", testmodule ));
        output_filenames( make->appmode );
        output_filenames_obj_dir( make, object_files );
        output_filenames_obj_dir( make, res_files );
        output_filenames( all_libs );
        output_filename( "$(LDFLAGS)" );
        output( "\n" );
        output( "%s%s %s%s:", obj_dir_path( make, testmodule ), dll_ext,
                obj_dir_path( make, stripped ), dll_ext );
        output_filenames_obj_dir( make, object_files );
        output_filenames_obj_dir( make, res_files );
        output_filenames( dep_libs );
        output_filename( tools_path( make, "winebuild" ));
        output_filename( tools_path( make, "winegcc" ));
        output( "\n" );

        if (!make->disabled)
            output( "all: %s/%s\n", top_obj_dir_path( make, "programs/winetest" ), testres );
        output( "%s/%s: %s%s\n", top_obj_dir_path( make, "programs/winetest" ), testres,
                obj_dir_path( make, stripped ), dll_ext );
        output( "\techo \"%s TESTRES \\\"%s%s\\\"\" | %s -o $@\n",
                testmodule, obj_dir_path( make, stripped ), dll_ext, tools_path( make, "wrc" ));

        if (crosstarget)
        {
            char *crosstest = replace_extension( make->testdll, ".dll", "_crosstest.exe" );

            dep_libs = empty_strarray;
            all_libs = add_import_libs( make, &dep_libs, make->imports, 1 );
            add_import_libs( make, &dep_libs, get_default_imports( make ), 1 );  /* dependencies only */
            strarray_addall( &all_libs, libs );
            strarray_add( &clean_files, crosstest );
            output( "%s:", obj_dir_path( make, crosstest ));
            output_filenames_obj_dir( make, crossobj_files );
            output_filenames_obj_dir( make, res_files );
            output_filenames( dep_libs );
            output_filename( tools_path( make, "winebuild" ));
            output_filename( tools_path( make, "winegcc" ));
            output( "\n" );
            output( "\t%s -o $@ -b %s", tools_path( make, "winegcc" ), crosstarget );
            output_filename( strmake( "-B%s", tools_dir_path( make, "winebuild" )));
            if (tools_dir) output_filename( strmake( "--sysroot=%s", top_obj_dir_path( make, "" )));
            output_filename( "--lib-suffix=.cross.a" );
            output_filenames_obj_dir( make, crossobj_files );
            output_filenames_obj_dir( make, res_files );
            output_filenames( all_libs );
            output_filename( "$(LDFLAGS)" );
            output( "\n" );
            if (!make->disabled)
            {
                output( "%s: %s\n", obj_dir_path( make, "crosstest" ), obj_dir_path( make, crosstest ));
                strarray_add( &phony_targets, obj_dir_path( make, "crosstest" ));
                if (make->obj_dir) output( "crosstest: %s\n", obj_dir_path( make, "crosstest" ));
            }
        }

        output_filenames_obj_dir( make, ok_files );
        output( ": %s%s ../%s%s\n", testmodule, dll_ext, make->testdll, dll_ext );
        if (!make->disabled)
        {
            output( "check test:" );
            output_filenames_obj_dir( make, ok_files );
            output( "\n" );
            strarray_add( &phony_targets, "check" );
            strarray_add( &phony_targets, "test" );
        }
        output( "testclean::\n" );
        output( "\trm -f" );
        output_filenames_obj_dir( make, ok_files );
        output( "\n" );
        strarray_addall( &clean_files, ok_files );
        strarray_add( &phony_targets, "testclean" );
    }

    for (i = 0; i < make->programs.count; i++)
    {
        char *program_installed = NULL;
        char *program = strmake( "%s%s", make->programs.str[i], exe_ext );
        struct strarray deps = get_local_dependencies( make, make->programs.str[i], in_files );
        struct strarray all_libs = get_expanded_file_local_var( make, make->programs.str[i], "LDFLAGS" );
        struct strarray objs     = get_expanded_file_local_var( make, make->programs.str[i], "OBJS" );
        struct strarray symlinks = get_expanded_file_local_var( make, make->programs.str[i], "SYMLINKS" );

        if (!objs.count) objs = object_files;
        strarray_addall( &all_libs, add_default_libraries( make, &deps ));

        output( "%s:", obj_dir_path( make, program ) );
        output_filenames_obj_dir( make, objs );
        output_filenames( deps );
        output( "\n" );
        output( "\t$(CC) -o $@" );
        output_filenames_obj_dir( make, objs );

        if (strarray_exists( &all_libs, "-lwine" ))
        {
            strarray_add( &all_libs, strmake( "-L%s", top_obj_dir_path( make, "libs/wine" )));
            if (ldrpath_local && ldrpath_install)
            {
                program_installed = strmake( "%s-installed%s", make->programs.str[i], exe_ext );
                output_filename( ldrpath_local );
                output_filenames( all_libs );
                output_filename( "$(LDFLAGS)" );
                output( "\n" );
                output( "%s:", obj_dir_path( make, program_installed ) );
                output_filenames_obj_dir( make, objs );
                output_filenames( deps );
                output( "\n" );
                output( "\t$(CC) -o $@" );
                output_filenames_obj_dir( make, objs );
                output_filename( ldrpath_install );
                strarray_add( &all_targets, program_installed );
            }
        }

        output_filenames( all_libs );
        output_filename( "$(LDFLAGS)" );
        output( "\n" );
        strarray_add( &all_targets, program );

        for (j = 0; j < symlinks.count; j++)
        {
            output( "%s: %s\n", obj_dir_path( make, symlinks.str[j] ), obj_dir_path( make, program ));
            output_symlink_rule( obj_dir_path( make, program ), obj_dir_path( make, symlinks.str[j] ));
        }
        strarray_addall( &all_targets, symlinks );

        add_install_rule( make, install_rules, program, program_installed ? program_installed : program,
                          strmake( "p$(bindir)/%s", program ));
        for (j = 0; j < symlinks.count; j++)
            add_install_rule( make, install_rules, symlinks.str[j], program,
                              strmake( "y$(bindir)/%s%s", symlinks.str[j], exe_ext ));
    }

    for (i = 0; i < make->scripts.count; i++)
        add_install_rule( make, install_rules, make->scripts.str[i], make->scripts.str[i],
                          strmake( "S$(bindir)/%s", make->scripts.str[i] ));

    if (make->resource)
    {
        char *extra_wine_flags = get_expanded_make_variable( make, "WINEFLAGS" );
        char *extra_cross_flags = get_expanded_make_variable( make, "CROSSFLAGS" );
        struct strarray all_libs = empty_strarray;
        char *module_path = obj_dir_path( make, make->resource );
        char *spec_file = NULL;

        if (!make->appmode.count)
            spec_file = src_dir_path( make, replace_extension( make->resource, ".dll", ".spec" ));
        for (i = 0; i < make->imports.count; i++)
            strarray_add( &all_libs, strmake( "-l%s", make->imports.str[i] ));
        strarray_addall( &all_libs, get_expanded_make_var_array( make, "LIBS" ));

        if (*dll_ext)
        {
            strarray_add( &all_targets, strmake( "%s%s", make->resource, dll_ext ));
            strarray_add( &all_targets, strmake( "%s.fake", make->resource ));
            output( "%s%s %s.fake:", module_path, dll_ext, module_path );
        }
        else
        {
            strarray_add( &all_targets, make->resource );
            output( "%s:", module_path );
        }
        if (spec_file) output_filename( spec_file );
        output_filenames_obj_dir( make, object_files );
        output_filenames_obj_dir( make, res_files );
        output( "\n" );
        output( "\t%s -o $@", tools_path( make, "winegcc" ));
        output_filename( strmake( "-B%s", tools_dir_path( make, "winebuild" )));
        if (tools_dir) output_filename( strmake( "--sysroot=%s", top_obj_dir_path( make, "" )));
        output_filenames( target_flags );
        output_filenames( unwind_flags );
        if (spec_file)
        {
            output( " -shared %s", spec_file );
            output_filenames( make->extradllflags );
        }
        else output_filenames( make->appmode );
        if (extra_wine_flags) output( " %s", extra_wine_flags );
        output_filenames_obj_dir( make, object_files );
        output_filenames_obj_dir( make, res_files );
        output_filenames( all_libs );
        output_filename( "$(LDFLAGS)" );
        output( "\n" );

        if (crosstarget)
        {
            char *crossres = prepend_extension( make->resource, "_crossres", ".dll" );

            strarray_add( &clean_files, crossres );
            output( "%s:", obj_dir_path( make, crossres ));
            output_filenames_obj_dir( make, crossobj_files );
            output_filenames_obj_dir( make, res_files );
            output( "\n" );
            output( "\t%s -o $@ -b %s", tools_path( make, "winegcc" ), crosstarget );
            output_filename( strmake( "-B%s", tools_dir_path( make, "winebuild" )));
            if (tools_dir) output_filename( strmake( "--sysroot=%s", top_obj_dir_path( make, "" )));
            output_filename( "--lib-suffix=.cross.a" );
            if (spec_file)
            {
                output( " -shared %s", spec_file );
                output_filenames( make->extradllflags );
            }
            else output_filenames( make->appmode );
            if (extra_cross_flags) output( " %s", extra_cross_flags );
            output_filenames_obj_dir( make, crossobj_files );
            output_filenames_obj_dir( make, res_files );
            output_filenames( all_libs );
            output_filename( "$(LDFLAGS)" );
            output( "\n" );
        }
    }

    if (!make->disabled)
    {
        if (all_targets.count)
        {
            output( "all:" );
            output_filenames_obj_dir( make, all_targets );
            output( "\n" );
        }
        strarray_addall( &uninstall_files, output_install_rules( make, install_rules[INSTALL_LIB],
                                                                 "install-lib", &phony_targets ));
        strarray_addall( &uninstall_files, output_install_rules( make, install_rules[INSTALL_DEV],
                                                                 "install-dev", &phony_targets ));
        if (uninstall_files.count)
        {
            output( "uninstall::\n" );
            output( "\trm -f" );
            output_filenames( uninstall_files );
            output( "\n" );
            strarray_add_uniq( &phony_targets, "uninstall" );
        }
    }

    strarray_addall( &clean_files, object_files );
    strarray_addall( &clean_files, crossobj_files );
    strarray_addall( &clean_files, res_files );
    strarray_addall( &clean_files, all_targets );
    strarray_addall( &clean_files, get_expanded_make_var_array( make, "EXTRA_TARGETS" ));

    if (make->subdirs.count)
    {
        struct strarray build_deps = empty_strarray;
        struct strarray makefile_deps = empty_strarray;
        struct strarray distclean_files = get_expanded_make_var_array( make, "CONFIGURE_TARGETS" );

        strarray_add( &distclean_files, obj_dir_path( make, output_makefile_name ));
        if (!make->src_dir) strarray_add( &distclean_files, obj_dir_path( make, ".gitignore" ));
        for (i = 0; i < make->subdirs.count; i++)
        {
            const struct makefile *submake = make->submakes[i];

            strarray_add( &makefile_deps, top_src_dir_path( make, base_dir_path( submake,
                                                            strmake ( "%s.in", output_makefile_name ))));
            strarray_add( &distclean_files, base_dir_path( submake, output_makefile_name ));
            if (!make->src_dir) strarray_add( &distclean_files, base_dir_path( submake, ".gitignore" ));
            if (submake->testdll) strarray_add( &distclean_files, base_dir_path( submake, "testlist.c" ));
            strarray_addall( &build_deps, output_importlib_symlinks( make, submake ));
        }
        output( "Makefile:" );
        output_filenames( makefile_deps );
        output( "\n" );
        output( "distclean::\n");
        output( "\trm -f" );
        output_filenames( distclean_files );
        output( "\n" );
        strarray_add( &phony_targets, "distclean" );

        if (build_deps.count)
        {
            output( "__builddeps__:" );
            output_filenames( build_deps );
            output( "\n" );
            strarray_addall( &clean_files, build_deps );
        }
        if (get_expanded_make_variable( make, "GETTEXTPO_LIBS" )) output_po_files( make );
    }

    if (clean_files.count)
    {
        output( "%s::\n", obj_dir_path( make, "clean" ));
        output( "\trm -f" );
        output_filenames_obj_dir( make, clean_files );
        output( "\n" );
        if (make->obj_dir) output( "__clean__: %s\n", obj_dir_path( make, "clean" ));
        strarray_add( &phony_targets, obj_dir_path( make, "clean" ));
    }

    if (phony_targets.count)
    {
        output( ".PHONY:" );
        output_filenames( phony_targets );
        output( "\n" );
    }

    if (!make->base_dir)
        strarray_addall( &clean_files, get_expanded_make_var_array( make, "CONFIGURE_TARGETS" ));
    return clean_files;
}


/*******************************************************************
 *         create_temp_file
 */
static FILE *create_temp_file( const char *orig )
{
    char *name = xmalloc( strlen(orig) + 13 );
    unsigned int i, id = getpid();
    int fd;
    FILE *ret = NULL;

    for (i = 0; i < 100; i++)
    {
        sprintf( name, "%s.tmp%08x", orig, id );
        if ((fd = open( name, O_RDWR | O_CREAT | O_EXCL, 0666 )) != -1)
        {
            ret = fdopen( fd, "w" );
            break;
        }
        if (errno != EEXIST) break;
        id += 7777;
    }
    if (!ret) fatal_error( "failed to create output file for '%s'\n", orig );
    temp_file_name = name;
    return ret;
}


/*******************************************************************
 *         rename_temp_file
 */
static void rename_temp_file( const char *dest )
{
    int ret = rename( temp_file_name, dest );
    if (ret == -1 && errno == EEXIST)
    {
        /* rename doesn't overwrite on windows */
        unlink( dest );
        ret = rename( temp_file_name, dest );
    }
    if (ret == -1) fatal_error( "failed to rename output file to '%s'\n", dest );
    temp_file_name = NULL;
}


/*******************************************************************
 *         are_files_identical
 */
static int are_files_identical( FILE *file1, FILE *file2 )
{
    for (;;)
    {
        char buffer1[8192], buffer2[8192];
        int size1 = fread( buffer1, 1, sizeof(buffer1), file1 );
        int size2 = fread( buffer2, 1, sizeof(buffer2), file2 );
        if (size1 != size2) return 0;
        if (!size1) return feof( file1 ) && feof( file2 );
        if (memcmp( buffer1, buffer2, size1 )) return 0;
    }
}


/*******************************************************************
 *         rename_temp_file_if_changed
 */
static void rename_temp_file_if_changed( const char *dest )
{
    FILE *file1, *file2;
    int do_rename = 1;

    if ((file1 = fopen( dest, "r" )))
    {
        if ((file2 = fopen( temp_file_name, "r" )))
        {
            do_rename = !are_files_identical( file1, file2 );
            fclose( file2 );
        }
        fclose( file1 );
    }
    if (!do_rename)
    {
        unlink( temp_file_name );
        temp_file_name = NULL;
    }
    else rename_temp_file( dest );
}


/*******************************************************************
 *         output_linguas
 */
static void output_linguas( const struct makefile *make )
{
    const char *dest = base_dir_path( make, "LINGUAS" );
    struct incl_file *source;

    output_file = create_temp_file( dest );

    output( "# Automatically generated by make depend; DO NOT EDIT!!\n" );
    LIST_FOR_EACH_ENTRY( source, &make->sources, struct incl_file, entry )
        if (strendswith( source->name, ".po" ))
            output( "%s\n", replace_extension( source->name, ".po", "" ));

    if (fclose( output_file )) fatal_perror( "write" );
    output_file = NULL;
    rename_temp_file_if_changed( dest );
}


/*******************************************************************
 *         output_testlist
 */
static void output_testlist( const struct makefile *make )
{
    const char *dest = base_dir_path( make, "testlist.c" );
    struct strarray files = empty_strarray;
    struct incl_file *source;
    unsigned int i;

    LIST_FOR_EACH_ENTRY( source, &make->sources, struct incl_file, entry )
    {
        if (source->file->flags & FLAG_GENERATED) continue;
        if (!strendswith( source->name, ".c" )) continue;
        strarray_add( &files, replace_extension( source->name, ".c", "" ));
    }

    output_file = create_temp_file( dest );

    output( "/* Automatically generated by make depend; DO NOT EDIT!! */\n\n" );
    output( "#define WIN32_LEAN_AND_MEAN\n" );
    output( "#include <windows.h>\n\n" );
    output( "#define STANDALONE\n" );
    output( "#include \"wine/test.h\"\n\n" );

    for (i = 0; i < files.count; i++) output( "extern void func_%s(void);\n", files.str[i] );
    output( "\n" );
    output( "const struct test winetest_testlist[] =\n" );
    output( "{\n" );
    for (i = 0; i < files.count; i++) output( "    { \"%s\", func_%s },\n", files.str[i], files.str[i] );
    output( "    { 0, 0 }\n" );
    output( "};\n" );

    if (fclose( output_file )) fatal_perror( "write" );
    output_file = NULL;
    rename_temp_file_if_changed( dest );
}


/*******************************************************************
 *         output_gitignore
 */
static void output_gitignore( const char *dest, struct strarray files )
{
    int i;

    output_file = create_temp_file( dest );

    output( "# Automatically generated by make depend; DO NOT EDIT!!\n" );
    for (i = 0; i < files.count; i++)
    {
        if (!strchr( files.str[i], '/' )) output( "/" );
        output( "%s\n", files.str[i] );
    }

    if (fclose( output_file )) fatal_perror( "write" );
    output_file = NULL;
    rename_temp_file( dest );
}


/*******************************************************************
 *         output_top_variables
 */
static void output_top_variables( const struct makefile *make )
{
    unsigned int i;
    struct strarray *vars = &top_makefile->vars;

    if (!make->base_dir) return;  /* don't output variables in the top makefile */

    output( "# Automatically generated by make depend; DO NOT EDIT!!\n\n" );
    output( "all:\n\n" );
    for (i = 0; i < vars->count; i += 2)
    {
        if (!strcmp( vars->str[i], "SUBDIRS" )) continue;  /* not inherited */
        output( "%s = %s\n", vars->str[i], get_make_variable( make, vars->str[i] ));
    }
    output( "\n" );
}


/*******************************************************************
 *         output_dependencies
 */
static void output_dependencies( const struct makefile *make )
{
    struct strarray targets, ignore_files = empty_strarray;
    char buffer[1024];
    FILE *src_file;
    int found = 0;

    if (make->base_dir) create_dir( make->base_dir );

    output_file_name = base_dir_path( make, output_makefile_name );
    output_file = create_temp_file( output_file_name );
    output_top_variables( make );

    /* copy the contents of the source makefile */
    src_file = open_input_makefile( make );
    while (fgets( buffer, sizeof(buffer), src_file ) && !found)
    {
        if (fwrite( buffer, 1, strlen(buffer), output_file ) != strlen(buffer)) fatal_perror( "write" );
        found = !strncmp( buffer, separator, strlen(separator) );
    }
    if (fclose( src_file )) fatal_perror( "close" );
    input_file_name = NULL;

    if (!found) output( "\n%s (everything below this line is auto-generated; DO NOT EDIT!!)\n", separator );
    targets = output_sources( make );

    fclose( output_file );
    output_file = NULL;
    rename_temp_file( output_file_name );

    strarray_add( &ignore_files, ".gitignore" );
    strarray_add( &ignore_files, "Makefile" );
    if (make->testdll)
    {
        output_testlist( make );
        strarray_add( &ignore_files, "testlist.c" );
    }
    if (make->base_dir && !strcmp( make->base_dir, "po" ))
    {
        output_linguas( make );
        strarray_add( &ignore_files, "LINGUAS" );
    }
    strarray_addall( &ignore_files, targets );
    if (!make->src_dir) output_gitignore( base_dir_path( make, ".gitignore" ), ignore_files );

    create_file_directories( make, targets );

    output_file_name = NULL;
}


/*******************************************************************
 *         load_sources
 */
static void load_sources( struct makefile *make )
{
    static const char *source_vars[] =
    {
        "C_SRCS",
        "OBJC_SRCS",
        "RC_SRCS",
        "MC_SRCS",
        "IDL_SRCS",
        "BISON_SRCS",
        "LEX_SRCS",
        "HEADER_SRCS",
        "XTEMPLATE_SRCS",
        "SVG_SRCS",
        "FONT_SRCS",
        "IN_SRCS",
        "PO_SRCS",
        "MANPAGES",
        NULL
    };
    const char **var;
    unsigned int i;
    struct strarray value;
    struct incl_file *file;

    if (root_src_dir)
    {
        make->top_src_dir = concat_paths( make->top_obj_dir, root_src_dir );
        make->src_dir = concat_paths( make->top_src_dir, make->base_dir );
    }
    strarray_set_value( &make->vars, "top_builddir", top_obj_dir_path( make, "" ));
    strarray_set_value( &make->vars, "top_srcdir", top_src_dir_path( make, "" ));
    strarray_set_value( &make->vars, "srcdir", src_dir_path( make, "" ));

    make->parent_dir    = get_expanded_make_variable( make, "PARENTSRC" );
    make->parent_spec   = get_expanded_make_variable( make, "PARENTSPEC" );
    make->module        = get_expanded_make_variable( make, "MODULE" );
    make->testdll       = get_expanded_make_variable( make, "TESTDLL" );
    make->resource      = get_expanded_make_variable( make, "RESOURCE" );
    make->sharedlib     = get_expanded_make_variable( make, "SHAREDLIB" );
    make->staticlib     = get_expanded_make_variable( make, "STATICLIB" );
    make->importlib     = get_expanded_make_variable( make, "IMPORTLIB" );

    make->programs      = get_expanded_make_var_array( make, "PROGRAMS" );
    make->scripts       = get_expanded_make_var_array( make, "SCRIPTS" );
    make->appmode       = get_expanded_make_var_array( make, "APPMODE" );
    make->imports       = get_expanded_make_var_array( make, "IMPORTS" );
    make->delayimports  = get_expanded_make_var_array( make, "DELAYIMPORTS" );
    make->extradllflags = get_expanded_make_var_array( make, "EXTRADLLFLAGS" );
    make->install_lib   = get_expanded_make_var_array( make, "INSTALL_LIB" );
    make->install_dev   = get_expanded_make_var_array( make, "INSTALL_DEV" );

    if (make->module && strendswith( make->module, ".a" )) make->staticlib = make->module;

    make->disabled   = make->base_dir && strarray_exists( &disabled_dirs, make->base_dir );
    make->is_win16   = strarray_exists( &make->extradllflags, "-m16" );
    make->use_msvcrt = strarray_exists( &make->appmode, "-mno-cygwin" );

    for (i = 0; i < make->imports.count && !make->use_msvcrt; i++)
        make->use_msvcrt = !strncmp( make->imports.str[i], "msvcr", 5 ) ||
                           !strcmp( make->imports.str[i], "ucrtbase" );

    if (make->module && !make->install_lib.count && !make->install_dev.count)
    {
        if (make->importlib) strarray_add( &make->install_dev, make->importlib );
        if (make->staticlib) strarray_add( &make->install_dev, make->staticlib );
        else strarray_add( &make->install_lib, make->module );
    }

    make->include_paths = empty_strarray;
    make->define_args = empty_strarray;
    strarray_add( &make->define_args, "-D__WINESRC__" );

    value = get_expanded_make_var_array( make, "EXTRAINCL" );
    for (i = 0; i < value.count; i++)
        if (!strncmp( value.str[i], "-I", 2 ))
            strarray_add_uniq( &make->include_paths, value.str[i] + 2 );
        else
            strarray_add_uniq( &make->define_args, value.str[i] );
    strarray_addall( &make->define_args, get_expanded_make_var_array( make, "EXTRADEFS" ));

    list_init( &make->sources );
    list_init( &make->includes );

    for (var = source_vars; *var; var++)
    {
        value = get_expanded_make_var_array( make, *var );
        for (i = 0; i < value.count; i++) add_src_file( make, value.str[i] );
    }

    add_generated_sources( make );

    value = get_expanded_make_var_array( make, "EXTRA_OBJS" );
    for (i = 0; i < value.count; i++)
    {
        /* default to .c for unknown extra object files */
        if (strendswith( value.str[i], ".o" ))
            add_generated_source( make, value.str[i], replace_extension( value.str[i], ".o", ".c" ) );
        else
            add_generated_source( make, value.str[i], NULL );
    }

    LIST_FOR_EACH_ENTRY( file, &make->includes, struct incl_file, entry ) parse_file( make, file, 0 );
}


/*******************************************************************
 *         parse_makeflags
 */
static void parse_makeflags( const char *flags )
{
    const char *p = flags;
    char *var, *buffer = xmalloc( strlen(flags) + 1 );

    while (*p)
    {
        while (isspace(*p)) p++;
        var = buffer;
        while (*p && !isspace(*p))
        {
            if (*p == '\\' && p[1]) p++;
            *var++ = *p++;
        }
        *var = 0;
        if (var > buffer) set_make_variable( &cmdline_vars, buffer );
    }
}


/*******************************************************************
 *         parse_option
 */
static int parse_option( const char *opt )
{
    if (opt[0] != '-')
    {
        if (strchr( opt, '=' )) return set_make_variable( &cmdline_vars, opt );
        return 0;
    }
    switch(opt[1])
    {
    case 'f':
        if (opt[2]) output_makefile_name = opt + 2;
        break;
    case 'R':
        relative_dir_mode = 1;
        break;
    default:
        fprintf( stderr, "Unknown option '%s'\n%s", opt, Usage );
        exit(1);
    }
    return 1;
}


/*******************************************************************
 *         main
 */
int main( int argc, char *argv[] )
{
    const char *makeflags = getenv( "MAKEFLAGS" );
    int i, j;

    if (makeflags) parse_makeflags( makeflags );

    i = 1;
    while (i < argc)
    {
        if (parse_option( argv[i] ))
        {
            for (j = i; j < argc; j++) argv[j] = argv[j+1];
            argc--;
        }
        else i++;
    }

    if (relative_dir_mode)
    {
        char *relpath;

        if (argc != 3)
        {
            fprintf( stderr, "Option -R needs two directories\n%s", Usage );
            exit( 1 );
        }
        relpath = get_relative_path( argv[1], argv[2] );
        printf( "%s\n", relpath ? relpath : "." );
        exit( 0 );
    }

    atexit( cleanup_files );
    signal( SIGTERM, exit_on_signal );
    signal( SIGINT, exit_on_signal );
#ifdef SIGHUP
    signal( SIGHUP, exit_on_signal );
#endif

    for (i = 0; i < HASH_SIZE; i++) list_init( &files[i] );

    top_makefile = parse_makefile( NULL );

    target_flags = get_expanded_make_var_array( top_makefile, "TARGETFLAGS" );
    msvcrt_flags = get_expanded_make_var_array( top_makefile, "MSVCRTFLAGS" );
    dll_flags    = get_expanded_make_var_array( top_makefile, "DLLFLAGS" );
    extra_cflags = get_expanded_make_var_array( top_makefile, "EXTRACFLAGS" );
    cpp_flags    = get_expanded_make_var_array( top_makefile, "CPPFLAGS" );
    unwind_flags = get_expanded_make_var_array( top_makefile, "UNWINDFLAGS" );
    libs         = get_expanded_make_var_array( top_makefile, "LIBS" );

    root_src_dir = get_expanded_make_variable( top_makefile, "srcdir" );
    tools_dir    = get_expanded_make_variable( top_makefile, "TOOLSDIR" );
    tools_ext    = get_expanded_make_variable( top_makefile, "TOOLSEXT" );
    exe_ext      = get_expanded_make_variable( top_makefile, "EXEEXT" );
    man_ext      = get_expanded_make_variable( top_makefile, "api_manext" );
    dll_ext      = (exe_ext && !strcmp( exe_ext, ".exe" )) ? "" : ".so";
    crosstarget  = get_expanded_make_variable( top_makefile, "CROSSTARGET" );
    fontforge    = get_expanded_make_variable( top_makefile, "FONTFORGE" );
    convert      = get_expanded_make_variable( top_makefile, "CONVERT" );
    rsvg         = get_expanded_make_variable( top_makefile, "RSVG" );
    icotool      = get_expanded_make_variable( top_makefile, "ICOTOOL" );
    dlltool      = get_expanded_make_variable( top_makefile, "DLLTOOL" );
    msgfmt       = get_expanded_make_variable( top_makefile, "MSGFMT" );
    ln_s         = get_expanded_make_variable( top_makefile, "LN_S" );

    if (root_src_dir && !strcmp( root_src_dir, "." )) root_src_dir = NULL;
    if (tools_dir && !strcmp( tools_dir, "." )) tools_dir = NULL;
    if (!exe_ext) exe_ext = "";
    if (!tools_ext) tools_ext = "";
    if (!man_ext) man_ext = "3w";

    if (argc == 1)
    {
        disabled_dirs = get_expanded_make_var_array( top_makefile, "DISABLED_SUBDIRS" );
        top_makefile->subdirs = get_expanded_make_var_array( top_makefile, "SUBDIRS" );
        top_makefile->submakes = xmalloc( top_makefile->subdirs.count * sizeof(*top_makefile->submakes) );

        for (i = 0; i < top_makefile->subdirs.count; i++)
            top_makefile->submakes[i] = parse_makefile( top_makefile->subdirs.str[i] );

        load_sources( top_makefile );
        for (i = 0; i < top_makefile->subdirs.count; i++)
            load_sources( top_makefile->submakes[i] );

        for (i = 0; i < top_makefile->subdirs.count; i++)
            output_dependencies( top_makefile->submakes[i] );

        output_dependencies( top_makefile );
        return 0;
    }

    for (i = 1; i < argc; i++)
    {
        struct makefile *make = parse_makefile( argv[i] );
        load_sources( make );
        output_dependencies( make );
    }
    return 0;
}
