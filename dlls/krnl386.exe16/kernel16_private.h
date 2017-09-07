/*
 * Kernel 16-bit private definitions
 *
 * Copyright 1995 Alexandre Julliard
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

#ifndef __WINE_KERNEL16_PRIVATE_H
#define __WINE_KERNEL16_PRIVATE_H

#include "wine/winbase16.h"
#include "winreg.h"
#include "winternl.h"

#include "pshpack1.h"

/* In-memory module structure. See 'Windows Internals' p. 219 */
typedef struct _NE_MODULE
{
    WORD      ne_magic;         /* 00 'NE' signature */
    WORD      count;            /* 02 Usage count (ne_ver/ne_rev on disk) */
    WORD      ne_enttab;        /* 04 Near ptr to entry table */
    HMODULE16 next;             /* 06 Selector to next module (ne_cbenttab on disk) */
    WORD      dgroup_entry;     /* 08 Near ptr to segment entry for DGROUP (ne_crc on disk) */
    WORD      fileinfo;         /* 0a Near ptr to file info (OFSTRUCT) (ne_crc on disk) */
    WORD      ne_flags;         /* 0c Module flags */
    WORD      ne_autodata;      /* 0e Logical segment for DGROUP */
    WORD      ne_heap;          /* 10 Initial heap size */
    WORD      ne_stack;         /* 12 Initial stack size */
    DWORD     ne_csip;          /* 14 Initial cs:ip */
    DWORD     ne_sssp;          /* 18 Initial ss:sp */
    WORD      ne_cseg;          /* 1c Number of segments in segment table */
    WORD      ne_cmod;          /* 1e Number of module references */
    WORD      ne_cbnrestab;     /* 20 Size of non-resident names table */
    WORD      ne_segtab;        /* 22 Near ptr to segment table */
    WORD      ne_rsrctab;       /* 24 Near ptr to resource table */
    WORD      ne_restab;        /* 26 Near ptr to resident names table */
    WORD      ne_modtab;        /* 28 Near ptr to module reference table */
    WORD      ne_imptab;        /* 2a Near ptr to imported names table */
    DWORD     ne_nrestab;       /* 2c File offset of non-resident names table */
    WORD      ne_cmovent;       /* 30 Number of moveable entries in entry table*/
    WORD      ne_align;         /* 32 Alignment shift count */
    WORD      ne_cres;          /* 34 # of resource segments */
    BYTE      ne_exetyp;        /* 36 Operating system flags */
    BYTE      ne_flagsothers;   /* 37 Misc. flags */
    HANDLE16  dlls_to_init;     /* 38 List of DLLs to initialize (ne_pretthunks on disk) */
    HANDLE16  nrname_handle;    /* 3a Handle to non-resident name table (ne_psegrefbytes on disk) */
    WORD      ne_swaparea;      /* 3c Min. swap area size */
    WORD      ne_expver;        /* 3e Expected Windows version */
    /* From here, these are extra fields not present in normal Windows */
    HMODULE   module32;         /* PE module handle for Win32 modules */
    HMODULE   owner32;          /* PE module containing this one for 16-bit builtins */
    HMODULE16 self;             /* Handle for this module */
    WORD      self_loading_sel; /* Selector used for self-loading apps. */
    LPVOID    rsrc32_map;       /* HRSRC 16->32 map (for 32-bit modules) */
    LPCVOID   mapping;          /* mapping of the binary file */
    SIZE_T    mapping_size;     /* size of the file mapping */
} NE_MODULE;

typedef struct
{
    BYTE type;
    BYTE flags;
    BYTE segnum;
    WORD offs;
} ET_ENTRY;

typedef struct
{
    WORD first; /* ordinal */
    WORD last;  /* ordinal */
    WORD next;  /* bundle */
} ET_BUNDLE;


  /* In-memory segment table */
typedef struct
{
    WORD      filepos;   /* Position in file, in sectors */
    WORD      size;      /* Segment size on disk */
    WORD      flags;     /* Segment flags */
    WORD      minsize;   /* Min. size of segment in memory */
    HANDLE16  hSeg;      /* Selector or handle (selector - 1) of segment in memory */
} SEGTABLEENTRY;

/* this structure is always located at offset 0 of the DGROUP segment */
typedef struct
{
    WORD null;        /* Always 0 */
    DWORD old_ss_sp;  /* Stack pointer; used by SwitchTaskTo() */
    WORD heap;        /* Pointer to the local heap information (if any) */
    WORD atomtable;   /* Pointer to the local atom table (if any) */
    WORD stacktop;    /* Top of the stack */
    WORD stackmin;    /* Lowest stack address used so far */
    WORD stackbottom; /* Bottom of the stack */
} INSTANCEDATA;

/* relay entry points */

typedef struct
{
    WORD   pushw_bp;               /* pushw %bp */
    BYTE   pushl;                  /* pushl $target */
    void  *target;
    WORD   call;                   /* call CALLFROM16 */
    short  callfrom16;
} ENTRYPOINT16;

typedef struct
{
    BYTE   pushl;                  /* pushl $relay */
    void  *relay;
    BYTE   lcall;                  /* lcall __FLATCS__:glue */
    void  *glue;
    WORD   flatcs;
    WORD   ret[5];                 /* return sequence */
    WORD   movl;                   /* movl arg_types[1],arg_types[0](%esi) */
    DWORD  arg_types[2];           /* type of each argument */
} CALLFROM16;

/* THHOOK Kernel Data Structure */
typedef struct _THHOOK
{
    HANDLE16   hGlobalHeap;         /* 00 (handle BURGERMASTER) */
    WORD       pGlobalHeap;         /* 02 (selector BURGERMASTER) */
    HMODULE16  hExeHead;            /* 04 hFirstModule */
    HMODULE16  hExeSweep;           /* 06 (unused) */
    HANDLE16   TopPDB;              /* 08 (handle of KERNEL PDB) */
    HANDLE16   HeadPDB;             /* 0A (first PDB in list) */
    HANDLE16   TopSizePDB;          /* 0C (unused) */
    HTASK16    HeadTDB;             /* 0E hFirstTask */
    HTASK16    CurTDB;              /* 10 hCurrentTask */
    HTASK16    LoadTDB;             /* 12 (unused) */
    HTASK16    LockTDB;             /* 14 hLockedTask */
} THHOOK;

extern LONG __wine_call_from_16(void);
extern void __wine_call_from_16_regs(void);

extern THHOOK *pThhook DECLSPEC_HIDDEN;

#include "poppack.h"

#define NE_SEG_TABLE(pModule) \
    ((SEGTABLEENTRY *)((char *)(pModule) + (pModule)->ne_segtab))

#define NE_MODULE_NAME(pModule) \
    (((OFSTRUCT *)((char*)(pModule) + (pModule)->fileinfo))->szPathName)

#define NE_GET_DATA(pModule,offset,size) \
    ((const void *)(((offset)+(size) <= pModule->mapping_size) ? \
                    (const char *)pModule->mapping + (offset) : NULL))

#define NE_READ_DATA(pModule,buffer,offset,size) \
    (((offset)+(size) <= pModule->mapping_size) ? \
     (memcpy( buffer, (const char *)pModule->mapping + (offset), (size) ), TRUE) : FALSE)

#define CURRENT_STACK16 ((STACK16FRAME*)MapSL(PtrToUlong(NtCurrentTeb()->SystemReserved1[0])))
#define CURRENT_DS      (CURRENT_STACK16->ds)

/* push bytes on the 16-bit stack of a thread; return a segptr to the first pushed byte */
static inline SEGPTR stack16_push( int size )
{
    STACK16FRAME *frame = CURRENT_STACK16;
    memmove( (char*)frame - size, frame, sizeof(*frame) );
    NtCurrentTeb()->SystemReserved1[0] = (char *)NtCurrentTeb()->SystemReserved1[0] - size;
    return (SEGPTR)((char *)NtCurrentTeb()->SystemReserved1[0] + sizeof(*frame));
}

/* pop bytes from the 16-bit stack of a thread */
static inline void stack16_pop( int size )
{
    STACK16FRAME *frame = CURRENT_STACK16;
    memmove( (char*)frame + size, frame, sizeof(*frame) );
    NtCurrentTeb()->SystemReserved1[0] = (char *)NtCurrentTeb()->SystemReserved1[0] + size;
}

/* dosmem.c */
extern BOOL   DOSMEM_Init(void) DECLSPEC_HIDDEN;
extern BOOL   DOSMEM_InitDosMemory(void) DECLSPEC_HIDDEN;
extern LPVOID DOSMEM_MapRealToLinear(DWORD) DECLSPEC_HIDDEN; /* real-mode to linear */
extern LPVOID DOSMEM_MapDosToLinear(UINT) DECLSPEC_HIDDEN;   /* linear DOS to Wine */
extern UINT   DOSMEM_MapLinearToDos(LPVOID) DECLSPEC_HIDDEN; /* linear Wine to DOS */
extern BOOL   DOSMEM_MapDosLayout(void) DECLSPEC_HIDDEN;
extern LPVOID DOSMEM_AllocBlock(UINT size, WORD* p) DECLSPEC_HIDDEN;
extern BOOL   DOSMEM_FreeBlock(void* ptr) DECLSPEC_HIDDEN;
extern UINT   DOSMEM_ResizeBlock(void* ptr, UINT size, BOOL exact) DECLSPEC_HIDDEN;
extern UINT   DOSMEM_Available(void) DECLSPEC_HIDDEN;

/* global16.c */
extern HGLOBAL16 GLOBAL_CreateBlock( UINT16 flags, void *ptr, DWORD size,
                                     HGLOBAL16 hOwner, unsigned char selflags ) DECLSPEC_HIDDEN;
extern BOOL16 GLOBAL_FreeBlock( HGLOBAL16 handle ) DECLSPEC_HIDDEN;
extern BOOL16 GLOBAL_MoveBlock( HGLOBAL16 handle, void *ptr, DWORD size ) DECLSPEC_HIDDEN;
extern HGLOBAL16 GLOBAL_Alloc( WORD flags, DWORD size, HGLOBAL16 hOwner, unsigned char selflags ) DECLSPEC_HIDDEN;

/* instr.c */
extern DWORD __wine_emulate_instruction( EXCEPTION_RECORD *rec, CONTEXT *context ) DECLSPEC_HIDDEN;
extern LONG CALLBACK INSTR_vectored_handler( EXCEPTION_POINTERS *ptrs ) DECLSPEC_HIDDEN;

/* ne_module.c */
extern NE_MODULE *NE_GetPtr( HMODULE16 hModule ) DECLSPEC_HIDDEN;
extern WORD NE_GetOrdinal( HMODULE16 hModule, const char *name ) DECLSPEC_HIDDEN;
extern FARPROC16 WINAPI NE_GetEntryPoint( HMODULE16 hModule, WORD ordinal ) DECLSPEC_HIDDEN;
extern FARPROC16 NE_GetEntryPointEx( HMODULE16 hModule, WORD ordinal, BOOL16 snoop ) DECLSPEC_HIDDEN;
extern BOOL16 NE_SetEntryPoint( HMODULE16 hModule, WORD ordinal, WORD offset ) DECLSPEC_HIDDEN;
extern DWORD NE_StartTask(void) DECLSPEC_HIDDEN;

/* ne_segment.c */
extern BOOL NE_LoadSegment( NE_MODULE *pModule, WORD segnum ) DECLSPEC_HIDDEN;
extern BOOL NE_LoadAllSegments( NE_MODULE *pModule ) DECLSPEC_HIDDEN;
extern BOOL NE_CreateSegment( NE_MODULE *pModule, int segnum ) DECLSPEC_HIDDEN;
extern BOOL NE_CreateAllSegments( NE_MODULE *pModule ) DECLSPEC_HIDDEN;
extern HINSTANCE16 NE_GetInstance( NE_MODULE *pModule ) DECLSPEC_HIDDEN;
extern void NE_InitializeDLLs( HMODULE16 hModule ) DECLSPEC_HIDDEN;
extern void NE_DllProcessAttach( HMODULE16 hModule ) DECLSPEC_HIDDEN;
extern void NE_CallUserSignalProc( HMODULE16 hModule, UINT16 code ) DECLSPEC_HIDDEN;

/* selector.c */
extern WORD SELECTOR_AllocBlock( const void *base, DWORD size, unsigned char flags ) DECLSPEC_HIDDEN;
extern WORD SELECTOR_ReallocBlock( WORD sel, const void *base, DWORD size ) DECLSPEC_HIDDEN;
extern void SELECTOR_FreeBlock( WORD sel ) DECLSPEC_HIDDEN;
#define IS_SELECTOR_32BIT(sel) \
   (wine_ldt_is_system(sel) || (wine_ldt_copy.flags[LOWORD(sel) >> 3] & WINE_LDT_FLAGS_32BIT))

/* relay16.c */
extern int relay_call_from_16( void *entry_point, unsigned char *args16, CONTEXT *context ) DECLSPEC_HIDDEN;
extern void RELAY16_InitDebugLists(void) DECLSPEC_HIDDEN;

/* snoop16.c */
extern void SNOOP16_RegisterDLL(HMODULE16,LPCSTR) DECLSPEC_HIDDEN;
extern FARPROC16 SNOOP16_GetProcAddress16(HMODULE16,DWORD,FARPROC16) DECLSPEC_HIDDEN;
extern BOOL SNOOP16_ShowDebugmsgSnoop(const char *dll,int ord,const char *fname) DECLSPEC_HIDDEN;

/* syslevel.c */
extern VOID SYSLEVEL_CheckNotLevel( INT level ) DECLSPEC_HIDDEN;

/* task.c */
extern void TASK_CreateMainTask(void) DECLSPEC_HIDDEN;
extern HTASK16 TASK_SpawnTask( NE_MODULE *pModule, WORD cmdShow,
                               LPCSTR cmdline, BYTE len, HANDLE *hThread ) DECLSPEC_HIDDEN;
extern void TASK_ExitTask(void) DECLSPEC_HIDDEN;
extern HTASK16 TASK_GetTaskFromThread( DWORD thread ) DECLSPEC_HIDDEN;
extern TDB *TASK_GetCurrent(void) DECLSPEC_HIDDEN;
extern void TASK_InstallTHHook( THHOOK *pNewThook ) DECLSPEC_HIDDEN;

extern BOOL WOWTHUNK_Init(void) DECLSPEC_HIDDEN;

extern WORD DOSMEM_0000H DECLSPEC_HIDDEN;
extern WORD DOSMEM_BiosDataSeg DECLSPEC_HIDDEN;
extern WORD DOSMEM_BiosSysSeg DECLSPEC_HIDDEN;
extern DWORD CallTo16_DataSelector DECLSPEC_HIDDEN;
extern DWORD CallTo16_TebSelector DECLSPEC_HIDDEN;
extern SEGPTR CALL32_CBClient_RetAddr DECLSPEC_HIDDEN;
extern SEGPTR CALL32_CBClientEx_RetAddr DECLSPEC_HIDDEN;

struct tagSYSLEVEL;

struct kernel_thread_data
{
    void               *reserved;       /* stack segment pointer */
    WORD                stack_sel;      /* 16-bit stack selector */
    WORD                htask16;        /* Win16 task handle */
    DWORD               sys_count[4];   /* syslevel mutex entry counters */
    struct tagSYSLEVEL *sys_mutex[4];   /* syslevel mutex pointers */
    void               *pad[44];        /* change this if you add fields! */
};

static inline struct kernel_thread_data *kernel_get_thread_data(void)
{
    return (struct kernel_thread_data *)NtCurrentTeb()->SystemReserved1;
}

/* Push a DWORD on the 32-bit stack */
static inline void stack32_push( CONTEXT *context, DWORD val )
{
    context->Esp -= sizeof(DWORD);
    *(DWORD *)context->Esp = val;
}

/* Pop a DWORD from the 32-bit stack */
static inline DWORD stack32_pop( CONTEXT *context )
{
    DWORD ret = *(DWORD *)context->Esp;
    context->Esp += sizeof(DWORD);
    return ret;
}

#define DEFINE_REGS_ENTRYPOINT(name) \
    __ASM_STDCALL_FUNC( name, 0,                                        \
                        "pushl %ebp\n\t"                                \
                        __ASM_CFI(".cfi_adjust_cfa_offset 4\n\t")       \
                        __ASM_CFI(".cfi_rel_offset %ebp,0\n\t")         \
                        "movl %esp,%ebp\n\t"                            \
                        __ASM_CFI(".cfi_def_cfa_register %ebp\n\t")     \
                        "leal -(0x2cc+4)(%esp),%esp\n\t"  /* sizeof(CONTEXT) + space for %eax */ \
                        "movl %eax,-4(%ebp)\n\t"                        \
                        "pushl %esp\n\t"             /* context */      \
                        "call " __ASM_NAME("RtlCaptureContext") __ASM_STDCALL(4) "\n\t" \
                        "movl -4(%ebp),%eax\n\t"                        \
                        "movl %eax,0xb0(%esp)\n\t"   /* context->Eax */ \
                        "pushl %esp\n\t"             /* context */      \
                        "call " __ASM_NAME("__regs_") #name __ASM_STDCALL(4) "\n\t" \
                        "pushl %esp\n\t"             /* context */      \
                        "pushl $-2\n\t"   /* GetCurrentThread() */      \
                        "call " __ASM_NAME("NtSetContextThread") __ASM_STDCALL(8) "\n\t" \
                        "ret" ) /* fake ret to make copy protections happy */

#endif  /* __WINE_KERNEL16_PRIVATE_H */
