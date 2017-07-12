/*
 * Definitions for Wine C unit tests.
 *
 * Copyright (C) 2002 Alexandre Julliard
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

#ifndef __WINE_WINE_TEST_H
#define __WINE_WINE_TEST_H

#include <stdarg.h>
#include <stdlib.h>
#include <windef.h>
#include <winbase.h>

#ifdef __WINE_CONFIG_H
#error config.h should not be used in Wine tests
#endif
#ifdef __WINE_WINE_LIBRARY_H
#error wine/library.h should not be used in Wine tests
#endif
#ifdef __WINE_WINE_UNICODE_H
#error wine/unicode.h should not be used in Wine tests
#endif
#ifdef __WINE_WINE_DEBUG_H
#error wine/debug.h should not be used in Wine tests
#endif

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES  (~0u)
#endif
#ifndef INVALID_SET_FILE_POINTER
#define INVALID_SET_FILE_POINTER (~0u)
#endif

/* debug level */
extern int winetest_debug;

/* running in interactive mode? */
extern int winetest_interactive;

/* current platform */
extern const char *winetest_platform;

extern void winetest_set_location( const char* file, int line );
extern void winetest_start_todo( int is_todo );
extern int winetest_loop_todo(void);
extern void winetest_end_todo(void);
extern int winetest_get_mainargs( char*** pargv );
extern LONG winetest_get_failures(void);
extern int winetest_get_report_success(void);
extern int winetest_get_debug(void);
extern void winetest_add_failures( LONG new_failures );
extern void winetest_add_successes( LONG new_successes );
extern void winetest_add_todo_failures( LONG new_todo_failures );
extern void winetest_add_todo_successes( LONG new_todo_successes );
extern void winetest_add_skipped( LONG new_skipped );
extern void winetest_wait_child_process( HANDLE process );

extern const char *wine_dbgstr_wn( const WCHAR *str, int n );
extern const char *wine_dbgstr_guid( const GUID *guid );
extern const char *wine_dbgstr_rect( const RECT *rect );
static inline const char *wine_dbgstr_w( const WCHAR *s ) { return wine_dbgstr_wn( s, -1 ); }
extern const char *wine_dbgstr_longlong( ULONGLONG ll );

/* strcmpW is available for tests compiled under Wine, but not in standalone
 * builds under Windows, so we reimplement it under a different name. */
static inline int winetest_strcmpW( const WCHAR *str1, const WCHAR *str2 )
{
    while (*str1 && (*str1 == *str2)) { str1++; str2++; }
    return *str1 - *str2;
}

#ifdef STANDALONE
#define START_TEST(name) \
  static void func_##name(void); \
  const struct test winetest_testlist[] = { { #name, func_##name }, { 0, 0 } }; \
  static void func_##name(void)
#else
#define START_TEST(name) void func_##name(void)
#endif

#if defined(__x86_64__) && defined(__GNUC__) && defined(__WINE_USE_MSVCRT)
#define __winetest_cdecl __cdecl
#define __winetest_va_list __builtin_ms_va_list
#else
#define __winetest_cdecl
#define __winetest_va_list va_list
#endif

extern int broken( int condition );
extern int winetest_vok( int condition, const char *msg, __winetest_va_list ap );
extern void winetest_vskip( const char *msg, __winetest_va_list ap );

#ifdef __GNUC__
# define WINETEST_PRINTF_ATTR(fmt,args) __attribute__((format (printf,fmt,args)))
#else
# define WINETEST_PRINTF_ATTR(fmt,args)
#endif

extern void __winetest_cdecl winetest_ok( int condition, const char *msg, ... ) WINETEST_PRINTF_ATTR(2,3);
extern void __winetest_cdecl winetest_skip( const char *msg, ... ) WINETEST_PRINTF_ATTR(1,2);
extern void __winetest_cdecl winetest_win_skip( const char *msg, ... ) WINETEST_PRINTF_ATTR(1,2);
extern void __winetest_cdecl winetest_trace( const char *msg, ... ) WINETEST_PRINTF_ATTR(1,2);

#define ok_(file, line)       (winetest_set_location(file, line), 0) ? (void)0 : winetest_ok
#define skip_(file, line)     (winetest_set_location(file, line), 0) ? (void)0 : winetest_skip
#define win_skip_(file, line) (winetest_set_location(file, line), 0) ? (void)0 : winetest_win_skip
#define trace_(file, line)    (winetest_set_location(file, line), 0) ? (void)0 : winetest_trace

#define ok       ok_(__FILE__, __LINE__)
#define skip     skip_(__FILE__, __LINE__)
#define win_skip win_skip_(__FILE__, __LINE__)
#define trace    trace_(__FILE__, __LINE__)

#define todo_if(is_todo) for (winetest_start_todo(is_todo); \
                              winetest_loop_todo(); \
                              winetest_end_todo())
#define todo_wine               todo_if(!strcmp(winetest_platform, "wine"))
#define todo_wine_if(is_todo)   todo_if((is_todo) && !strcmp(winetest_platform, "wine"))


#ifdef NONAMELESSUNION
# define U(x)  (x).u
# define U1(x) (x).u1
# define U2(x) (x).u2
# define U3(x) (x).u3
# define U4(x) (x).u4
# define U5(x) (x).u5
# define U6(x) (x).u6
# define U7(x) (x).u7
# define U8(x) (x).u8
#else
# define U(x)  (x)
# define U1(x) (x)
# define U2(x) (x)
# define U3(x) (x)
# define U4(x) (x)
# define U5(x) (x)
# define U6(x) (x)
# define U7(x) (x)
# define U8(x) (x)
#endif

#ifdef NONAMELESSSTRUCT
# define S(x)  (x).s
# define S1(x) (x).s1
# define S2(x) (x).s2
# define S3(x) (x).s3
# define S4(x) (x).s4
# define S5(x) (x).s5
#else
# define S(x)  (x)
# define S1(x) (x)
# define S2(x) (x)
# define S3(x) (x)
# define S4(x) (x)
# define S5(x) (x)
#endif


/************************************************************************/
/* Below is the implementation of the various functions, to be included
 * directly into the generated testlist.c file.
 * It is done that way so that the dlls can build the test routines with
 * different includes or flags if needed.
 */

#ifdef STANDALONE

#include <stdio.h>
#include <excpt.h>

#if defined(__x86_64__) && defined(__GNUC__) && defined(__WINE_USE_MSVCRT)
# define __winetest_va_start(list,arg) __builtin_ms_va_start(list,arg)
# define __winetest_va_end(list) __builtin_ms_va_end(list)
#else
# define __winetest_va_start(list,arg) va_start(list,arg)
# define __winetest_va_end(list) va_end(list)
#endif

struct test
{
    const char *name;
    void (*func)(void);
};

extern const struct test winetest_testlist[];

/* debug level */
int winetest_debug = 1;

/* interactive mode? */
int winetest_interactive = 0;

/* current platform */
const char *winetest_platform = "windows";

/* report successful tests (BOOL) */
static int report_success = 0;

/* passing arguments around */
static int winetest_argc;
static char** winetest_argv;

static const struct test *current_test; /* test currently being run */

static LONG successes;       /* number of successful tests */
static LONG failures;        /* number of failures */
static LONG skipped;         /* number of skipped test chunks */
static LONG todo_successes;  /* number of successful tests inside todo block */
static LONG todo_failures;   /* number of failures inside todo block */

/* The following data must be kept track of on a per-thread basis */
struct tls_data
{
    const char* current_file;        /* file of current check */
    int current_line;                /* line of current check */
    unsigned int todo_level;         /* current todo nesting level */
    int todo_do_loop;
    char *str_pos;                   /* position in debug buffer */
    char strings[2000];              /* buffer for debug strings */
};
static DWORD tls_index;

static struct tls_data *get_tls_data(void)
{
    struct tls_data *data;
    DWORD last_error;

    last_error=GetLastError();
    data=TlsGetValue(tls_index);
    if (!data)
    {
        data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*data));
        data->str_pos = data->strings;
        TlsSetValue(tls_index,data);
    }
    SetLastError(last_error);
    return data;
}

/* allocate some tmp space for a string */
static char *get_temp_buffer( size_t n )
{
    struct tls_data *data = get_tls_data();
    char *res = data->str_pos;

    if (res + n >= &data->strings[sizeof(data->strings)]) res = data->strings;
    data->str_pos = res + n;
    return res;
}

/* release extra space that we requested in get_temp_buffer() */
static void release_temp_buffer( char *ptr, size_t size )
{
    struct tls_data *data = get_tls_data();
    data->str_pos = ptr + size;
}

static void exit_process( int code )
{
    fflush( stdout );
    ExitProcess( code );
}


void winetest_set_location( const char* file, int line )
{
    struct tls_data *data = get_tls_data();
    data->current_file=strrchr(file,'/');
    if (data->current_file==NULL)
        data->current_file=strrchr(file,'\\');
    if (data->current_file==NULL)
        data->current_file=file;
    else
        data->current_file++;
    data->current_line=line;
}

int broken( int condition )
{
    return (strcmp(winetest_platform, "windows") == 0) && condition;
}

/*
 * Checks condition.
 * Parameters:
 *   - condition - condition to check;
 *   - msg test description;
 *   - file - test application source code file name of the check
 *   - line - test application source code file line number of the check
 * Return:
 *   0 if condition does not have the expected value, 1 otherwise
 */
int winetest_vok( int condition, const char *msg, __winetest_va_list args )
{
    struct tls_data *data = get_tls_data();

    if (data->todo_level)
    {
        if (condition)
        {
            printf( "%s:%d: Test succeeded inside todo block: ",
                    data->current_file, data->current_line );
            vprintf(msg, args);
            InterlockedIncrement(&todo_failures);
            return 0;
        }
        else
        {
            if (winetest_debug > 0)
            {
                printf( "%s:%d: Test marked todo: ",
                        data->current_file, data->current_line );
                vprintf(msg, args);
            }
            InterlockedIncrement(&todo_successes);
            return 1;
        }
    }
    else
    {
        if (!condition)
        {
            printf( "%s:%d: Test failed: ",
                    data->current_file, data->current_line );
            vprintf(msg, args);
            InterlockedIncrement(&failures);
            return 0;
        }
        else
        {
            if (report_success)
                printf( "%s:%d: Test succeeded\n",
                        data->current_file, data->current_line);
            InterlockedIncrement(&successes);
            return 1;
        }
    }
}

void __winetest_cdecl winetest_ok( int condition, const char *msg, ... )
{
    __winetest_va_list valist;

    __winetest_va_start(valist, msg);
    winetest_vok(condition, msg, valist);
    __winetest_va_end(valist);
}

void __winetest_cdecl winetest_trace( const char *msg, ... )
{
    __winetest_va_list valist;
    struct tls_data *data = get_tls_data();

    if (winetest_debug > 0)
    {
        printf( "%s:%d: ", data->current_file, data->current_line );
        __winetest_va_start(valist, msg);
        vprintf(msg, valist);
        __winetest_va_end(valist);
    }
}

void winetest_vskip( const char *msg, __winetest_va_list args )
{
    struct tls_data *data = get_tls_data();

    printf( "%s:%d: Tests skipped: ", data->current_file, data->current_line );
    vprintf(msg, args);
    skipped++;
}

void __winetest_cdecl winetest_skip( const char *msg, ... )
{
    __winetest_va_list valist;
    __winetest_va_start(valist, msg);
    winetest_vskip(msg, valist);
    __winetest_va_end(valist);
}

void __winetest_cdecl winetest_win_skip( const char *msg, ... )
{
    __winetest_va_list valist;
    __winetest_va_start(valist, msg);
    if (strcmp(winetest_platform, "windows") == 0)
        winetest_vskip(msg, valist);
    else
        winetest_vok(0, msg, valist);
    __winetest_va_end(valist);
}

void winetest_start_todo( int is_todo )
{
    struct tls_data *data = get_tls_data();
    data->todo_level = (data->todo_level << 1) | (is_todo != 0);
    data->todo_do_loop=1;
}

int winetest_loop_todo(void)
{
    struct tls_data *data = get_tls_data();
    int do_loop=data->todo_do_loop;
    data->todo_do_loop=0;
    return do_loop;
}

void winetest_end_todo(void)
{
    struct tls_data *data = get_tls_data();
    data->todo_level >>= 1;
}

int winetest_get_mainargs( char*** pargv )
{
    *pargv = winetest_argv;
    return winetest_argc;
}

LONG winetest_get_failures(void)
{
    return failures;
}

int winetest_get_report_success(void)
{
    return report_success;
}

int winetest_get_debug(void)
{
    return winetest_debug;
}

void winetest_add_failures( LONG new_failures )
{
    InterlockedExchangeAdd( &failures, new_failures );
}

void winetest_add_successes( LONG new_successes )
{
    InterlockedExchangeAdd( &successes, new_successes );
}

void winetest_add_todo_failures( LONG new_todo_failures )
{
    InterlockedExchangeAdd( &todo_failures, new_todo_failures );
}

void winetest_add_todo_successes( LONG new_todo_successes )
{
    InterlockedExchangeAdd( &todo_successes, new_todo_successes );
}

void winetest_add_skipped( LONG new_skipped )
{
    InterlockedExchangeAdd( &skipped, new_skipped );
}

void winetest_wait_child_process( HANDLE process )
{
    DWORD exit_code = 1;

    if (WaitForSingleObject( process, 30000 ))
        printf( "%s: child process wait failed\n", current_test->name );
    else
        GetExitCodeProcess( process, &exit_code );

    if (exit_code)
    {
        if (exit_code > 255)
        {
            printf( "%s: exception 0x%08x in child process\n", current_test->name, exit_code );
            InterlockedIncrement( &failures );
        }
        else
        {
            printf( "%s: %u failures in child process\n",
                    current_test->name, exit_code );
            while (exit_code-- > 0)
                InterlockedIncrement(&failures);
        }
    }
}

const char *wine_dbgstr_wn( const WCHAR *str, int n )
{
    char *dst, *res;
    size_t size;

    if (!((ULONG_PTR)str >> 16))
    {
        if (!str) return "(null)";
        res = get_temp_buffer( 6 );
        sprintf( res, "#%04x", LOWORD(str) );
        return res;
    }
    if (n == -1)
    {
        const WCHAR *end = str;
        while (*end) end++;
        n = end - str;
    }
    if (n < 0) n = 0;
    size = 12 + min( 300, n * 5 );
    dst = res = get_temp_buffer( size );
    *dst++ = 'L';
    *dst++ = '"';
    while (n-- > 0 && dst <= res + size - 10)
    {
        WCHAR c = *str++;
        switch (c)
        {
        case '\n': *dst++ = '\\'; *dst++ = 'n'; break;
        case '\r': *dst++ = '\\'; *dst++ = 'r'; break;
        case '\t': *dst++ = '\\'; *dst++ = 't'; break;
        case '"':  *dst++ = '\\'; *dst++ = '"'; break;
        case '\\': *dst++ = '\\'; *dst++ = '\\'; break;
        default:
            if (c >= ' ' && c <= 126)
                *dst++ = c;
            else
            {
                *dst++ = '\\';
                sprintf(dst,"%04x",c);
                dst+=4;
            }
        }
    }
    *dst++ = '"';
    if (n > 0)
    {
        *dst++ = '.';
        *dst++ = '.';
        *dst++ = '.';
    }
    *dst++ = 0;
    release_temp_buffer( res, dst - res );
    return res;
}

const char *wine_dbgstr_guid( const GUID *guid )
{
    char *res;

    if (!guid) return "(null)";
    res = get_temp_buffer( 39 ); /* CHARS_IN_GUID */
    sprintf( res, "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
             guid->Data1, guid->Data2, guid->Data3, guid->Data4[0],
             guid->Data4[1], guid->Data4[2], guid->Data4[3], guid->Data4[4],
             guid->Data4[5], guid->Data4[6], guid->Data4[7] );
    return res;
}

const char *wine_dbgstr_rect( const RECT *rect )
{
    char *res;

    if (!rect) return "(null)";
    res = get_temp_buffer( 60 );
    sprintf( res, "(%d,%d)-(%d,%d)", rect->left, rect->top, rect->right, rect->bottom );
    release_temp_buffer( res, strlen(res) + 1 );
    return res;
}

const char *wine_dbgstr_longlong( ULONGLONG ll )
{
    char *res;

    res = get_temp_buffer( 17 );
    if (sizeof(ll) > sizeof(unsigned long) && ll >> 32)
        sprintf( res, "%lx%08lx", (unsigned long)(ll >> 32), (unsigned long)ll );
    else
        sprintf( res, "%lx", (unsigned long)ll );
    release_temp_buffer( res, strlen(res) + 1 );
    return res;
}

/* Find a test by name */
static const struct test *find_test( const char *name )
{
    const struct test *test;
    const char *p;
    size_t len;

    if ((p = strrchr( name, '/' ))) name = p + 1;
    if ((p = strrchr( name, '\\' ))) name = p + 1;
    len = strlen(name);
    if (len > 2 && !strcmp( name + len - 2, ".c" )) len -= 2;

    for (test = winetest_testlist; test->name; test++)
    {
        if (!strncmp( test->name, name, len ) && !test->name[len]) break;
    }
    return test->name ? test : NULL;
}


/* Display list of valid tests */
static void list_tests(void)
{
    const struct test *test;

    printf( "Valid test names:\n" );
    for (test = winetest_testlist; test->name; test++)
        printf( "    %s\n", test->name );
}


/* Run a named test, and return exit status */
static int run_test( const char *name )
{
    const struct test *test;
    int status;

    if (!(test = find_test( name )))
    {
        printf( "Fatal: test '%s' does not exist.\n", name );
        exit_process(1);
    }
    successes = failures = todo_successes = todo_failures = 0;
    tls_index=TlsAlloc();
    current_test = test;
    test->func();

    if (winetest_debug)
    {
        printf( "%04x:%s: %d tests executed (%d marked as todo, %d %s), %d skipped.\n",
                GetCurrentProcessId(), test->name,
                successes + failures + todo_successes + todo_failures,
                todo_successes, failures + todo_failures,
                (failures + todo_failures != 1) ? "failures" : "failure",
                skipped );
    }
    status = (failures + todo_failures < 255) ? failures + todo_failures : 255;
    return status;
}


/* Display usage and exit */
static void usage( const char *argv0 )
{
    printf( "Usage: %s test_name\n\n", argv0 );
    list_tests();
    exit_process(1);
}

/* trap unhandled exceptions */
static LONG CALLBACK exc_filter( EXCEPTION_POINTERS *ptrs )
{
    struct tls_data *data = get_tls_data();

    if (data->current_file)
        printf( "%s:%d: this is the last test seen before the exception\n",
                data->current_file, data->current_line );
    printf( "%04x:%s: unhandled exception %08x at %p\n",
            GetCurrentProcessId(), current_test->name,
            ptrs->ExceptionRecord->ExceptionCode, ptrs->ExceptionRecord->ExceptionAddress );
    fflush( stdout );
    return EXCEPTION_EXECUTE_HANDLER;
}

/* check if we're running under wine */
static BOOL running_under_wine(void)
{
    HMODULE module = GetModuleHandleA( "ntdll.dll" );
    if (!module) return FALSE;
    return (GetProcAddress( module, "wine_server_call" ) != NULL);
}

#ifdef __GNUC__
void _fpreset(void) {} /* override the mingw fpu init code */
#endif

/* main function */
int main( int argc, char **argv )
{
    char p[128];

    setvbuf (stdout, NULL, _IONBF, 0);

    winetest_argc = argc;
    winetest_argv = argv;

    if (GetEnvironmentVariableA( "WINETEST_PLATFORM", p, sizeof(p) ))
        winetest_platform = strdup(p);
    else if (running_under_wine())
        winetest_platform = "wine";

    if (GetEnvironmentVariableA( "WINETEST_DEBUG", p, sizeof(p) )) winetest_debug = atoi(p);
    if (GetEnvironmentVariableA( "WINETEST_INTERACTIVE", p, sizeof(p) )) winetest_interactive = atoi(p);
    if (GetEnvironmentVariableA( "WINETEST_REPORT_SUCCESS", p, sizeof(p) )) report_success = atoi(p);

    if (!strcmp( winetest_platform, "windows" )) SetUnhandledExceptionFilter( exc_filter );
    if (!winetest_interactive) SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX );

    if (!argv[1])
    {
        if (winetest_testlist[0].name && !winetest_testlist[1].name)  /* only one test */
            return run_test( winetest_testlist[0].name );
        usage( argv[0] );
    }
    if (!strcmp( argv[1], "--list" ))
    {
        list_tests();
        return 0;
    }
    return run_test(argv[1]);
}

#endif  /* STANDALONE */

#endif  /* __WINE_WINE_TEST_H */
