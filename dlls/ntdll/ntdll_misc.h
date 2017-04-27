/*
 * Copyright 2000 Juergen Schmied
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

#ifndef __WINE_NTDLL_MISC_H
#define __WINE_NTDLL_MISC_H

#include <stdarg.h>
#include <signal.h>
#include <sys/types.h>
#include <pthread.h>

#include "windef.h"
#include "winnt.h"
#include "winternl.h"
#include "wine/server.h"

#define MAX_NT_PATH_LENGTH 277

#define MAX_DOS_DRIVES 26

#if defined(__i386__) || defined(__x86_64__)
static const UINT_PTR page_size = 0x1000;
#else
extern UINT_PTR page_size DECLSPEC_HIDDEN;
#endif

struct drive_info
{
    dev_t dev;
    ino_t ino;
};

extern NTSTATUS close_handle( HANDLE ) DECLSPEC_HIDDEN;
extern ULONG_PTR get_system_affinity_mask(void) DECLSPEC_HIDDEN;

/* exceptions */
extern void wait_suspend( CONTEXT *context ) DECLSPEC_HIDDEN;
extern NTSTATUS send_debug_event( EXCEPTION_RECORD *rec, int first_chance, CONTEXT *context ) DECLSPEC_HIDDEN;
extern LONG call_vectored_handlers( EXCEPTION_RECORD *rec, CONTEXT *context ) DECLSPEC_HIDDEN;
extern void raise_status( NTSTATUS status, EXCEPTION_RECORD *rec ) DECLSPEC_NORETURN DECLSPEC_HIDDEN;
extern void set_cpu_context( const CONTEXT *context ) DECLSPEC_HIDDEN;
extern void copy_context( CONTEXT *to, const CONTEXT *from, DWORD flags ) DECLSPEC_HIDDEN;
extern NTSTATUS context_to_server( context_t *to, const CONTEXT *from ) DECLSPEC_HIDDEN;
extern NTSTATUS context_from_server( CONTEXT *to, const context_t *from ) DECLSPEC_HIDDEN;
extern void call_thread_entry_point( LPTHREAD_START_ROUTINE entry, void *arg ) DECLSPEC_NORETURN DECLSPEC_HIDDEN;

/* debug helpers */
extern LPCSTR debugstr_us( const UNICODE_STRING *str ) DECLSPEC_HIDDEN;
extern LPCSTR debugstr_ObjectAttributes(const OBJECT_ATTRIBUTES *oa) DECLSPEC_HIDDEN;

/* init routines */
extern NTSTATUS signal_alloc_thread( TEB **teb ) DECLSPEC_HIDDEN;
extern void signal_free_thread( TEB *teb ) DECLSPEC_HIDDEN;
extern void signal_init_thread( TEB *teb ) DECLSPEC_HIDDEN;
extern void signal_init_process(void) DECLSPEC_HIDDEN;
extern void signal_init_early(void) DECLSPEC_HIDDEN;
extern void version_init( const WCHAR *appname ) DECLSPEC_HIDDEN;
extern void debug_init(void) DECLSPEC_HIDDEN;
extern HANDLE thread_init(void) DECLSPEC_HIDDEN;
extern void actctx_init(void) DECLSPEC_HIDDEN;
extern void virtual_init(void) DECLSPEC_HIDDEN;
extern void virtual_init_threading(void) DECLSPEC_HIDDEN;
extern void fill_cpu_info(void) DECLSPEC_HIDDEN;

/* heap routines */
extern void *grow_virtual_heap( HANDLE handle, SIZE_T *size ) DECLSPEC_HIDDEN;
extern void heap_set_debug_flags( HANDLE handle ) DECLSPEC_HIDDEN;

/* server support */
extern timeout_t server_start_time DECLSPEC_HIDDEN;
extern unsigned int server_cpus DECLSPEC_HIDDEN;
extern BOOL is_wow64 DECLSPEC_HIDDEN;
extern void server_init_process(void) DECLSPEC_HIDDEN;
extern NTSTATUS server_init_process_done(void) DECLSPEC_HIDDEN;
extern size_t server_init_thread( void *entry_point ) DECLSPEC_HIDDEN;
extern void DECLSPEC_NORETURN abort_thread( int status ) DECLSPEC_HIDDEN;
extern void DECLSPEC_NORETURN terminate_thread( int status ) DECLSPEC_HIDDEN;
extern void DECLSPEC_NORETURN exit_thread( int status ) DECLSPEC_HIDDEN;
extern sigset_t server_block_set DECLSPEC_HIDDEN;
extern void server_enter_uninterrupted_section( RTL_CRITICAL_SECTION *cs, sigset_t *sigset ) DECLSPEC_HIDDEN;
extern void server_leave_uninterrupted_section( RTL_CRITICAL_SECTION *cs, sigset_t *sigset ) DECLSPEC_HIDDEN;
extern unsigned int server_select( const select_op_t *select_op, data_size_t size,
                                   UINT flags, const LARGE_INTEGER *timeout ) DECLSPEC_HIDDEN;
extern unsigned int server_queue_process_apc( HANDLE process, const apc_call_t *call, apc_result_t *result ) DECLSPEC_HIDDEN;
extern int server_remove_fd_from_cache( HANDLE handle ) DECLSPEC_HIDDEN;
extern int server_get_unix_fd( HANDLE handle, unsigned int access, int *unix_fd,
                               int *needs_close, enum server_fd_type *type, unsigned int *options ) DECLSPEC_HIDDEN;
extern int server_pipe( int fd[2] ) DECLSPEC_HIDDEN;
extern NTSTATUS alloc_object_attributes( const OBJECT_ATTRIBUTES *attr, struct object_attributes **ret,
                                         data_size_t *ret_len ) DECLSPEC_HIDDEN;
extern NTSTATUS validate_open_object_attributes( const OBJECT_ATTRIBUTES *attr ) DECLSPEC_HIDDEN;
extern void *server_get_shared_memory( HANDLE thread ) DECLSPEC_HIDDEN;

/* module handling */
extern LIST_ENTRY tls_links DECLSPEC_HIDDEN;
extern NTSTATUS MODULE_DllThreadAttach( LPVOID lpReserved ) DECLSPEC_HIDDEN;
extern FARPROC RELAY_GetProcAddress( HMODULE module, const IMAGE_EXPORT_DIRECTORY *exports,
                                     DWORD exp_size, FARPROC proc, DWORD ordinal, const WCHAR *user ) DECLSPEC_HIDDEN;
extern FARPROC SNOOP_GetProcAddress( HMODULE hmod, const IMAGE_EXPORT_DIRECTORY *exports, DWORD exp_size,
                                     FARPROC origfun, DWORD ordinal, const WCHAR *user ) DECLSPEC_HIDDEN;
extern void RELAY_SetupDLL( HMODULE hmod ) DECLSPEC_HIDDEN;
extern void SNOOP_SetupDLL( HMODULE hmod ) DECLSPEC_HIDDEN;
extern UNICODE_STRING system_dir DECLSPEC_HIDDEN;

typedef LONG (WINAPI *PUNHANDLED_EXCEPTION_FILTER)(PEXCEPTION_POINTERS);
extern PUNHANDLED_EXCEPTION_FILTER unhandled_exception_filter DECLSPEC_HIDDEN;

/* redefine these to make sure we don't reference kernel symbols */
#define GetProcessHeap()       (NtCurrentTeb()->Peb->ProcessHeap)
#define GetCurrentProcessId()  (HandleToULong(NtCurrentTeb()->ClientId.UniqueProcess))
#define GetCurrentThreadId()   (HandleToULong(NtCurrentTeb()->ClientId.UniqueThread))

/* Device IO */
extern NTSTATUS CDROM_DeviceIoControl(HANDLE hDevice, 
                                      HANDLE hEvent, PIO_APC_ROUTINE UserApcRoutine,
                                      PVOID UserApcContext, 
                                      PIO_STATUS_BLOCK piosb, 
                                      ULONG IoControlCode,
                                      LPVOID lpInBuffer, DWORD nInBufferSize,
                                      LPVOID lpOutBuffer, DWORD nOutBufferSize) DECLSPEC_HIDDEN;
extern NTSTATUS COMM_DeviceIoControl(HANDLE hDevice, 
                                     HANDLE hEvent, PIO_APC_ROUTINE UserApcRoutine,
                                     PVOID UserApcContext, 
                                     PIO_STATUS_BLOCK piosb, 
                                     ULONG IoControlCode,
                                     LPVOID lpInBuffer, DWORD nInBufferSize,
                                     LPVOID lpOutBuffer, DWORD nOutBufferSize) DECLSPEC_HIDDEN;
extern NTSTATUS TAPE_DeviceIoControl(HANDLE hDevice, 
                                     HANDLE hEvent, PIO_APC_ROUTINE UserApcRoutine,
                                     PVOID UserApcContext, 
                                     PIO_STATUS_BLOCK piosb, 
                                     ULONG IoControlCode,
                                     LPVOID lpInBuffer, DWORD nInBufferSize,
                                     LPVOID lpOutBuffer, DWORD nOutBufferSize) DECLSPEC_HIDDEN;
extern NTSTATUS COMM_FlushBuffersFile( int fd ) DECLSPEC_HIDDEN;

/* file I/O */
struct stat;
extern NTSTATUS FILE_GetNtStatus(void) DECLSPEC_HIDDEN;
extern int get_file_info( const char *path, struct stat *st, ULONG *attr ) DECLSPEC_HIDDEN;
extern NTSTATUS fill_file_info( const struct stat *st, ULONG attr, void *ptr,
                                FILE_INFORMATION_CLASS class ) DECLSPEC_HIDDEN;
extern NTSTATUS server_get_unix_name( HANDLE handle, ANSI_STRING *unix_name ) DECLSPEC_HIDDEN;
extern void DIR_init_windows_dir( const WCHAR *windir, const WCHAR *sysdir ) DECLSPEC_HIDDEN;
extern BOOL DIR_is_hidden_file( const char *name ) DECLSPEC_HIDDEN;
extern NTSTATUS DIR_unmount_device( HANDLE handle ) DECLSPEC_HIDDEN;
extern NTSTATUS DIR_get_unix_cwd( char **cwd ) DECLSPEC_HIDDEN;
extern unsigned int DIR_get_drives_info( struct drive_info info[MAX_DOS_DRIVES] ) DECLSPEC_HIDDEN;
extern NTSTATUS file_id_to_unix_file_name( const OBJECT_ATTRIBUTES *attr, ANSI_STRING *unix_name_ret ) DECLSPEC_HIDDEN;
extern NTSTATUS nt_to_unix_file_name_attr( const OBJECT_ATTRIBUTES *attr, ANSI_STRING *unix_name_ret,
                                           UINT disposition ) DECLSPEC_HIDDEN;

/* virtual memory */
extern void virtual_get_system_info( SYSTEM_BASIC_INFORMATION *info ) DECLSPEC_HIDDEN;
extern NTSTATUS virtual_create_builtin_view( void *base ) DECLSPEC_HIDDEN;
extern NTSTATUS virtual_alloc_thread_stack( TEB *teb, SIZE_T reserve_size, SIZE_T commit_size ) DECLSPEC_HIDDEN;
extern NTSTATUS virtual_map_shared_memory( int fd, PVOID *addr_ptr, ULONG zero_bits, SIZE_T *size_ptr, ULONG protect ) DECLSPEC_HIDDEN;
extern void virtual_clear_thread_stack(void) DECLSPEC_HIDDEN;
extern BOOL virtual_handle_stack_fault( void *addr ) DECLSPEC_HIDDEN;
extern BOOL virtual_is_valid_code_address( const void *addr, SIZE_T size ) DECLSPEC_HIDDEN;
extern NTSTATUS virtual_handle_fault( LPCVOID addr, DWORD err, BOOL on_signal_stack ) DECLSPEC_HIDDEN;
extern BOOL virtual_check_buffer_for_read( const void *ptr, SIZE_T size ) DECLSPEC_HIDDEN;
extern BOOL virtual_check_buffer_for_write( void *ptr, SIZE_T size ) DECLSPEC_HIDDEN;
extern void VIRTUAL_SetForceExec( BOOL enable ) DECLSPEC_HIDDEN;
extern void virtual_release_address_space(void) DECLSPEC_HIDDEN;
extern void virtual_set_large_address_space(void) DECLSPEC_HIDDEN;
extern struct _KUSER_SHARED_DATA *user_shared_data DECLSPEC_HIDDEN;

/* completion */
extern NTSTATUS NTDLL_AddCompletion( HANDLE hFile, ULONG_PTR CompletionValue,
                                     NTSTATUS CompletionStatus, ULONG Information ) DECLSPEC_HIDDEN;

/* code pages */
extern int ntdll_umbstowcs(DWORD flags, const char* src, int srclen, WCHAR* dst, int dstlen) DECLSPEC_HIDDEN;
extern int ntdll_wcstoumbs(DWORD flags, const WCHAR* src, int srclen, char* dst, int dstlen,
                           const char* defchar, int *used ) DECLSPEC_HIDDEN;

extern int CDECL NTDLL__vsnprintf( char *str, SIZE_T len, const char *format, __ms_va_list args ) DECLSPEC_HIDDEN;
extern int CDECL NTDLL__vsnwprintf( WCHAR *str, SIZE_T len, const WCHAR *format, __ms_va_list args ) DECLSPEC_HIDDEN;

/* load order */

enum loadorder
{
    LO_INVALID,
    LO_DISABLED,
    LO_NATIVE,
    LO_BUILTIN,
    LO_NATIVE_BUILTIN,  /* native then builtin */
    LO_BUILTIN_NATIVE,  /* builtin then native */
    LO_DEFAULT          /* nothing specified, use default strategy */
};

extern enum loadorder get_load_order( const WCHAR *app_name, const WCHAR *path ) DECLSPEC_HIDDEN;
extern WCHAR* get_redirect( const WCHAR *app_name, const WCHAR *path, BYTE *buffer, ULONG size ) DECLSPEC_HIDDEN;

struct debug_info
{
    char *str_pos;       /* current position in strings buffer */
    char *out_pos;       /* current position in output buffer */
    char  strings[1024]; /* buffer for temporary strings */
    char  output[1024];  /* current output line */
};

/* thread private data, stored in NtCurrentTeb()->SpareBytes1 */
struct ntdll_thread_data
{
#if defined(__i386__) || defined(__x86_64__)
    DWORD_PTR          dr0;           /* 1bc/2e8 Debug registers */
    DWORD_PTR          dr1;           /* 1c0/2f0 */
    DWORD_PTR          dr2;           /* 1c4/2f8 */
    DWORD_PTR          dr3;           /* 1c8/300 */
    DWORD_PTR          dr6;           /* 1cc/308 */
    DWORD_PTR          dr7;           /* 1d0/310 */
#endif
#ifdef __i386__
    DWORD              fs;            /* 1d4 TEB selector */
    DWORD              gs;            /* 1d8 libc selector; update winebuild if you move this! */
    void              *vm86_ptr;      /* 1dc data for vm86 mode */
#else
    void              *exit_frame;    /*    /318 exit frame pointer */
#endif
    struct debug_info *debug_info;    /* 1e0/320 info for debugstr functions */
    int                request_fd;    /* 1e4/328 fd for sending server requests */
    int                reply_fd;      /* 1e8/32c fd for receiving server replies */
    int                wait_fd[2];    /* 1ec/330 fd for sleeping server requests */
    BOOL               wow64_redir;   /* 1f4/338 Wow64 filesystem redirection flag */
    pthread_t          pthread_id;    /* 1f8/340 pthread thread id */
#ifdef __i386__
    WINE_VM86_TEB_INFO vm86;          /* 1fc vm86 private data */
    void              *exit_frame;    /* 204 exit frame pointer */
#endif
    void              *pthread_stack; /* 208/318 pthread stack */
};

C_ASSERT( FIELD_OFFSET(TEB, SpareBytes1) + sizeof(struct ntdll_thread_data) <=
          FIELD_OFFSET(TEB, GdiTebBatch) + sizeof(((TEB *)0)->GdiTebBatch) );

#ifdef __i386__
C_ASSERT( FIELD_OFFSET(TEB, SpareBytes1) + FIELD_OFFSET(struct ntdll_thread_data, vm86) == FIELD_OFFSET(TEB, GdiTebBatch) );
C_ASSERT( FIELD_OFFSET(TEB, SpareBytes1) + FIELD_OFFSET(struct ntdll_thread_data, vm86) == 0x1fc );
C_ASSERT( FIELD_OFFSET(TEB, SpareBytes1) + FIELD_OFFSET(struct ntdll_thread_data, gs)   == 0x1d8 );
#endif

static inline struct ntdll_thread_data *ntdll_get_thread_data(void)
{
    return (struct ntdll_thread_data *)NtCurrentTeb()->SpareBytes1;
}

extern mode_t FILE_umask DECLSPEC_HIDDEN;
extern HANDLE keyed_event DECLSPEC_HIDDEN;

/* Register functions */

#ifdef __i386__
#define DEFINE_REGS_ENTRYPOINT( name, args ) \
    __ASM_GLOBAL_FUNC( name, \
                       ".byte 0x68\n\t"  /* pushl $__regs_func */       \
                       ".long " __ASM_NAME("__regs_") #name "-.-11\n\t" \
                       ".byte 0x6a," #args "\n\t" /* pushl $args */     \
                       "call " __ASM_NAME("__wine_call_from_regs") "\n\t" \
                       "ret $(4*" #args ")" ) /* fake ret to make copy protections happy */
#endif

#define HASH_STRING_ALGORITHM_DEFAULT  0
#define HASH_STRING_ALGORITHM_X65599   1
#define HASH_STRING_ALGORITHM_INVALID  0xffffffff

NTSTATUS WINAPI RtlHashUnicodeString(PCUNICODE_STRING,BOOLEAN,ULONG,ULONG*);

/* version */
extern const char * CDECL NTDLL_wine_get_version(void);
extern const char * CDECL NTDLL_wine_get_build_id(void);
extern void CDECL NTDLL_wine_get_host_version( const char **sysname, const char **release );

#endif
