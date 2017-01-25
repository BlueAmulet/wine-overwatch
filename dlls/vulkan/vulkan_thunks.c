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
#include "wine/list.h"
#include "wine/debug.h"
#include "wine/library.h"

#include "vulkan_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(vulkan);
WINE_DECLARE_DEBUG_CHANNEL(winediag);

#define ARRAY_SIZE(array) (sizeof(array)/sizeof(*array))

static HANDLE function_heap;

static CRITICAL_SECTION function_section;
static CRITICAL_SECTION_DEBUG critsect_debug =
{
    0, 0, &function_section,
    { &critsect_debug.ProcessLocksList, &critsect_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": function_section") }
};
static CRITICAL_SECTION function_section = { &critsect_debug, -1, 0, 0, 0, 0 };

#if defined(__i386__)
#include <pshpack1.h>
struct PFN_vkAllocationFunction_opcodes
{
    BYTE pushl_ebp[1];      /* pushl %ebp */
    BYTE movl_esp_ebp[2];   /* movl %esp,%ebp */
    BYTE subl_0x08_esp[3];  /* subl $8,%esp */
    BYTE pushl_0x14_ebp[3]; /* pushl 20(%ebp) */
    BYTE pushl_0x10_ebp[3]; /* pushl 16(%ebp) */
    BYTE pushl_0x0c_ebp[3]; /* pushl 12(%ebp) */
    BYTE pushl_0x08_ebp[3]; /* pushl 8(%ebp) */
    BYTE movl_func_eax[1];  /* movl function,%eax */
    void *function;
    BYTE call_eax[2];       /* call *%eax */
    BYTE leave[1];          /* leave */
    BYTE ret[1];            /* ret */
};
#include <poppack.h>

static const struct PFN_vkAllocationFunction_opcodes PFN_vkAllocationFunction_code =
{
    { 0x55 },               /* pushl %ebp */
    { 0x89, 0xe5 },         /* movl %esp,%ebp */
    { 0x83, 0xec, 0x08 },   /* subl $8,%esp */
    { 0xff, 0x75, 0x14 },   /* pushl 20(%ebp) */
    { 0xff, 0x75, 0x10 },   /* pushl 16(%ebp) */
    { 0xff, 0x75, 0x0c },   /* pushl 12(%ebp) */
    { 0xff, 0x75, 0x08 },   /* pushl 8(%ebp) */
    { 0xb8 },               /* movl function,%eax */
    NULL,
    { 0xff, 0xd0 },         /* call *%eax */
    { 0xc9 },               /* leave */
    { 0xc3 },               /* ret */
};
#elif defined(__x86_64__)
#include <pshpack1.h>
struct PFN_vkAllocationFunction_opcodes
{
    BYTE pushq_rbp[1];          /* pushq %rbp */
    BYTE movq_rsp_rbp[3];       /* movq %rsp,%rbp */
    BYTE subq_0x20_rsp[4];      /* subq $0x20,%rsp */
    BYTE movq_rcx_r9[3];        /* movq %rcx,%r9 */
    BYTE movq_rdx_r8[3];        /* movq %rdx,%r8 */
    BYTE movq_rsi_rdx[3];       /* movq %rsi,%rdx */
    BYTE movq_rsi_rcx[3];       /* movq %rdi,%rcx */
    BYTE movq_func_rax[2];      /* movq function,%rax */
    void *function;
    BYTE call_rax[2];           /* call *%rax */
    BYTE movq_rbp_rsp[3];       /* movq %rbp,%rsp */
    BYTE popq_rbp[1];           /* popq %rbp */
    BYTE ret[1];                /* ret */
};
#include <poppack.h>

static const struct PFN_vkAllocationFunction_opcodes PFN_vkAllocationFunction_code =
{
    { 0x55 },                   /* pushq %rbp */
    { 0x48, 0x89, 0xe5 },       /* movq %rsp,%rbp */
    { 0x48, 0x83, 0xec, 0x20 }, /* subq $0x20,%rsp */
    { 0x49, 0x89, 0xc9 },       /* movq %rcx,%r9 */
    { 0x49, 0x89, 0xd0 },       /* movq %rdx,%r8 */
    { 0x48, 0x89, 0xf2 },       /* movq %rsi,%rdx */
    { 0x48, 0x89, 0xf9 },       /* movq %rdi,%rcx */
    { 0x48, 0xb8 },             /* movq function,%rax */
    NULL,
    { 0xff, 0xd0 },             /* call *%eax */
    { 0x48, 0x89, 0xec },       /* movq %rbp,%rsp */
    { 0x5d },                   /* popq %rbp */
    { 0xc3 },                   /* ret */
};
#endif

#if defined(__i386__) || defined(__x86_64__)
struct PFN_vkAllocationFunction_function
{
    struct list entry;
    struct PFN_vkAllocationFunction_opcodes code;
};
#endif

void convert_PFN_vkAllocationFunction( PFN_vkAllocationFunction_host *out,
        const PFN_vkAllocationFunction *in )
{
#if defined(__i386__) || defined(__x86_64__)
    static struct list function_list = LIST_INIT( function_list );
    struct PFN_vkAllocationFunction_function *function;

    TRACE( "(%p, %p)\n", out, in );

    if (!*in)
    {
        *out = NULL;
        return;
    }
    EnterCriticalSection( &function_section );

    LIST_FOR_EACH_ENTRY( function, &function_list, struct PFN_vkAllocationFunction_function, entry )
        if (function->code.function == *in) break;

    if (&function->entry == &function_list)
    {
        function = HeapAlloc( function_heap, 0, sizeof(*function) );
        list_add_tail( &function_list, &function->entry );
        function->code = PFN_vkAllocationFunction_code;
        function->code.function = *in;
    }
    *out = (void *)&function->code;

    LeaveCriticalSection( &function_section );
#else
    *out = *in;
#endif
}
void release_PFN_vkAllocationFunction( PFN_vkAllocationFunction *out,
        PFN_vkAllocationFunction_host *in )
{
    TRACE( "(%p, %p)\n", out, in );
}

#if defined(__i386__)
#include <pshpack1.h>
struct PFN_vkReallocationFunction_opcodes
{
    BYTE pushl_ebp[1];      /* pushl %ebp */
    BYTE movl_esp_ebp[2];   /* movl %esp,%ebp */
    BYTE subl_0x04_esp[3];  /* subl $4,%esp */
    BYTE pushl_0x18_ebp[3]; /* pushl 24(%ebp) */
    BYTE pushl_0x14_ebp[3]; /* pushl 20(%ebp) */
    BYTE pushl_0x10_ebp[3]; /* pushl 16(%ebp) */
    BYTE pushl_0x0c_ebp[3]; /* pushl 12(%ebp) */
    BYTE pushl_0x08_ebp[3]; /* pushl 8(%ebp) */
    BYTE movl_func_eax[1];  /* movl function,%eax */
    void *function;
    BYTE call_eax[2];       /* call *%eax */
    BYTE leave[1];          /* leave */
    BYTE ret[1];            /* ret */
};
#include <poppack.h>

static const struct PFN_vkReallocationFunction_opcodes PFN_vkReallocationFunction_code =
{
    { 0x55 },               /* pushl %ebp */
    { 0x89, 0xe5 },         /* movl %esp,%ebp */
    { 0x83, 0xec, 0x04 },   /* subl $4,%esp */
    { 0xff, 0x75, 0x18 },   /* pushl 24(%ebp) */
    { 0xff, 0x75, 0x14 },   /* pushl 20(%ebp) */
    { 0xff, 0x75, 0x10 },   /* pushl 16(%ebp) */
    { 0xff, 0x75, 0x0c },   /* pushl 12(%ebp) */
    { 0xff, 0x75, 0x08 },   /* pushl 8(%ebp) */
    { 0xb8 },               /* movl function,%eax */
    NULL,
    { 0xff, 0xd0 },         /* call *%eax */
    { 0xc9 },               /* leave */
    { 0xc3 },               /* ret */
};
#elif defined(__x86_64__)
#include <pshpack1.h>
struct PFN_vkReallocationFunction_opcodes
{
    BYTE pushq_rbp[1];          /* pushq %rbp */
    BYTE movq_rsp_rbp[3];       /* movq %rsp,%rbp */
    BYTE subq_0x08_rsp[4];      /* subq $8,%rsp */
    BYTE pushq_r8[2];           /* pushq %r8 */
    BYTE subq_0x20_rsp[4];      /* subq $0x20,%rsp */
    BYTE movq_rcx_r9[3];        /* movq %rcx,%r9 */
    BYTE movq_rdx_r8[3];        /* movq %rdx,%r8 */
    BYTE movq_rsi_rdx[3];       /* movq %rsi,%rdx */
    BYTE movq_rsi_rcx[3];       /* movq %rdi,%rcx */
    BYTE movq_func_rax[2];      /* movq function,%rax */
    void *function;
    BYTE call_rax[2];           /* call *%rax */
    BYTE movq_rbp_rsp[3];       /* movq %rbp,%rsp */
    BYTE popq_rbp[1];           /* popq %rbp */
    BYTE ret[1];                /* ret */
};
#include <poppack.h>

static const struct PFN_vkReallocationFunction_opcodes PFN_vkReallocationFunction_code =
{
    { 0x55 },                   /* pushq %rbp */
    { 0x48, 0x89, 0xe5 },       /* movq %rsp,%rbp */
    { 0x48, 0x83, 0xec, 0x08 }, /* subq $8,%rsp */
    { 0x41, 0x50 },             /* pushq %r8 */
    { 0x48, 0x83, 0xec, 0x20 }, /* subq $0x20,%rsp */
    { 0x49, 0x89, 0xc9 },       /* movq %rcx,%r9 */
    { 0x49, 0x89, 0xd0 },       /* movq %rdx,%r8 */
    { 0x48, 0x89, 0xf2 },       /* movq %rsi,%rdx */
    { 0x48, 0x89, 0xf9 },       /* movq %rdi,%rcx */
    { 0x48, 0xb8 },             /* movq function,%rax */
    NULL,
    { 0xff, 0xd0 },             /* call *%eax */
    { 0x48, 0x89, 0xec },       /* movq %rbp,%rsp */
    { 0x5d },                   /* popq %rbp */
    { 0xc3 },                   /* ret */
};
#endif

#if defined(__i386__) || defined(__x86_64__)
struct PFN_vkReallocationFunction_function
{
    struct list entry;
    struct PFN_vkReallocationFunction_opcodes code;
};
#endif

void convert_PFN_vkReallocationFunction( PFN_vkReallocationFunction_host *out,
        const PFN_vkReallocationFunction *in )
{
#if defined(__i386__) || defined(__x86_64__)
    static struct list function_list = LIST_INIT( function_list );
    struct PFN_vkReallocationFunction_function *function;

    TRACE( "(%p, %p)\n", out, in );

    if (!*in)
    {
        *out = NULL;
        return;
    }
    EnterCriticalSection( &function_section );

    LIST_FOR_EACH_ENTRY( function, &function_list, struct PFN_vkReallocationFunction_function,
            entry )
        if (function->code.function == *in) break;

    if (&function->entry == &function_list)
    {
        function = HeapAlloc( function_heap, 0, sizeof(*function) );
        list_add_tail( &function_list, &function->entry );
        function->code = PFN_vkReallocationFunction_code;
        function->code.function = *in;
    }
    *out = (void *)&function->code;

    LeaveCriticalSection( &function_section );
#else
    *out = *in;
#endif
}
void release_PFN_vkReallocationFunction( PFN_vkReallocationFunction *out,
        PFN_vkReallocationFunction_host *in )
{
    TRACE( "(%p, %p)\n", out, in );
}

#if defined(__i386__)
#include <pshpack1.h>
struct PFN_vkFreeFunction_opcodes
{
    BYTE pushl_ebp[1];      /* pushl %ebp */
    BYTE movl_esp_ebp[2];   /* movl %esp,%ebp */
    BYTE pushl_0x0c_ebp[3]; /* pushl 12(%ebp) */
    BYTE pushl_0x08_ebp[3]; /* pushl 8(%ebp) */
    BYTE movl_func_eax[1];  /* movl function,%eax */
    void *function;
    BYTE call_eax[2];       /* call *%eax */
    BYTE leave[1];          /* leave */
    BYTE ret[1];            /* ret */
};
#include <poppack.h>

static const struct PFN_vkFreeFunction_opcodes PFN_vkFreeFunction_code =
{
    { 0x55 },               /* pushl %ebp */
    { 0x89, 0xe5 },         /* movl %esp,%ebp */
    { 0xff, 0x75, 0x0c },   /* pushl 12(%ebp) */
    { 0xff, 0x75, 0x08 },   /* pushl 8(%ebp) */
    { 0xb8 },               /* movl function,%eax */
    NULL,
    { 0xff, 0xd0 },         /* call *%eax */
    { 0xc9 },               /* leave */
    { 0xc3 },               /* ret */
};
#elif defined(__x86_64__)
#include <pshpack1.h>
struct PFN_vkFreeFunction_opcodes
{
    BYTE pushq_rbp[1];          /* pushq %rbp */
    BYTE movq_rsp_rbp[3];       /* movq %rsp,%rbp */
    BYTE subq_0x20_rsp[4];      /* subq $0x20,%rsp */
    BYTE movq_rsi_rdx[3];       /* movq %rsi,%rdx */
    BYTE movq_rsi_rcx[3];       /* movq %rdi,%rcx */
    BYTE movq_func_rax[2];      /* movq function,%rax */
    void *function;
    BYTE call_rax[2];           /* call *%rax */
    BYTE movq_rbp_rsp[3];       /* movq %rbp,%rsp */
    BYTE popq_rbp[1];           /* popq %rbp */
    BYTE ret[1];                /* ret */
};
#include <poppack.h>

static const struct PFN_vkFreeFunction_opcodes PFN_vkFreeFunction_code =
{
    { 0x55 },                   /* pushq %rbp */
    { 0x48, 0x89, 0xe5 },       /* movq %rsp,%rbp */
    { 0x48, 0x83, 0xec, 0x20 }, /* subq $0x20,%rsp */
    { 0x48, 0x89, 0xf2 },       /* movq %rsi,%rdx */
    { 0x48, 0x89, 0xf9 },       /* movq %rdi,%rcx */
    { 0x48, 0xb8 },             /* movq function,%rax */
    NULL,
    { 0xff, 0xd0 },             /* call *%eax */
    { 0x48, 0x89, 0xec },       /* movq %rbp,%rsp */
    { 0x5d },                   /* popq %rbp */
    { 0xc3 },                   /* ret */
};
#endif

#if defined(__i386__) || defined(__x86_64__)
struct PFN_vkFreeFunction_function
{
    struct list entry;
    struct PFN_vkFreeFunction_opcodes code;
};
#endif

void convert_PFN_vkFreeFunction( PFN_vkFreeFunction_host *out, const PFN_vkFreeFunction *in )
{
#if defined(__i386__) || defined(__x86_64__)
    static struct list function_list = LIST_INIT( function_list );
    struct PFN_vkFreeFunction_function *function;

    TRACE( "(%p, %p)\n", out, in );

    if (!*in)
    {
        *out = NULL;
        return;
    }
    EnterCriticalSection( &function_section );

    LIST_FOR_EACH_ENTRY( function, &function_list, struct PFN_vkFreeFunction_function, entry )
        if (function->code.function == *in) break;

    if (&function->entry == &function_list)
    {
        function = HeapAlloc( function_heap, 0, sizeof(*function) );
        list_add_tail( &function_list, &function->entry );
        function->code = PFN_vkFreeFunction_code;
        function->code.function = *in;
    }
    *out = (void *)&function->code;

    LeaveCriticalSection( &function_section );
#else
    *out = *in;
#endif
}
void release_PFN_vkFreeFunction( PFN_vkFreeFunction *out, PFN_vkFreeFunction_host *in )
{
    TRACE( "(%p, %p)\n", out, in );
}

#if defined(__i386__)
#include <pshpack1.h>
struct PFN_vkInternalAllocationNotification_opcodes
{
    BYTE pushl_ebp[1];      /* pushl %ebp */
    BYTE movl_esp_ebp[2];   /* movl %esp,%ebp */
    BYTE subl_0x08_esp[3];  /* subl $8,%esp */
    BYTE pushl_0x14_ebp[3]; /* pushl 20(%ebp) */
    BYTE pushl_0x10_ebp[3]; /* pushl 16(%ebp) */
    BYTE pushl_0x0c_ebp[3]; /* pushl 12(%ebp) */
    BYTE pushl_0x08_ebp[3]; /* pushl 8(%ebp) */
    BYTE movl_func_eax[1];  /* movl function,%eax */
    void *function;
    BYTE call_eax[2];       /* call *%eax */
    BYTE leave[1];          /* leave */
    BYTE ret[1];            /* ret */
};
#include <poppack.h>

static const struct PFN_vkInternalAllocationNotification_opcodes PFN_vkInternalAllocationNotification_code =
{
    { 0x55 },               /* pushl %ebp */
    { 0x89, 0xe5 },         /* movl %esp,%ebp */
    { 0x83, 0xec, 0x08 },   /* subl $8,%esp */
    { 0xff, 0x75, 0x14 },   /* pushl 20(%ebp) */
    { 0xff, 0x75, 0x10 },   /* pushl 16(%ebp) */
    { 0xff, 0x75, 0x0c },   /* pushl 12(%ebp) */
    { 0xff, 0x75, 0x08 },   /* pushl 8(%ebp) */
    { 0xb8 },               /* movl function,%eax */
    NULL,
    { 0xff, 0xd0 },         /* call *%eax */
    { 0xc9 },               /* leave */
    { 0xc3 },               /* ret */
};
#elif defined(__x86_64__)
#include <pshpack1.h>
struct PFN_vkInternalAllocationNotification_opcodes
{
    BYTE pushq_rbp[1];          /* pushq %rbp */
    BYTE movq_rsp_rbp[3];       /* movq %rsp,%rbp */
    BYTE subq_0x20_rsp[4];      /* subq $0x20,%rsp */
    BYTE movq_rcx_r9[3];        /* movq %rcx,%r9 */
    BYTE movq_rdx_r8[3];        /* movq %rdx,%r8 */
    BYTE movq_rsi_rdx[3];       /* movq %rsi,%rdx */
    BYTE movq_rsi_rcx[3];       /* movq %rdi,%rcx */
    BYTE movq_func_rax[2];      /* movq function,%rax */
    void *function;
    BYTE call_rax[2];           /* call *%rax */
    BYTE movq_rbp_rsp[3];       /* movq %rbp,%rsp */
    BYTE popq_rbp[1];           /* popq %rbp */
    BYTE ret[1];                /* ret */
};
#include <poppack.h>

static const struct PFN_vkInternalAllocationNotification_opcodes PFN_vkInternalAllocationNotification_code =
{
    { 0x55 },                   /* pushq %rbp */
    { 0x48, 0x89, 0xe5 },       /* movq %rsp,%rbp */
    { 0x48, 0x83, 0xec, 0x20 }, /* subq $0x20,%rsp */
    { 0x49, 0x89, 0xc9 },       /* movq %rcx,%r9 */
    { 0x49, 0x89, 0xd0 },       /* movq %rdx,%r8 */
    { 0x48, 0x89, 0xf2 },       /* movq %rsi,%rdx */
    { 0x48, 0x89, 0xf9 },       /* movq %rdi,%rcx */
    { 0x48, 0xb8 },             /* movq function,%rax */
    NULL,
    { 0xff, 0xd0 },             /* call *%eax */
    { 0x48, 0x89, 0xec },       /* movq %rbp,%rsp */
    { 0x5d },                   /* popq %rbp */
    { 0xc3 },                   /* ret */
};
#endif

#if defined(__i386__) || defined(__x86_64__)
struct PFN_vkInternalAllocationNotification_function
{
    struct list entry;
    struct PFN_vkInternalAllocationNotification_opcodes code;
};
#endif

void convert_PFN_vkInternalAllocationNotification( PFN_vkInternalAllocationNotification_host *out,
        const PFN_vkInternalAllocationNotification *in )
{
#if defined(__i386__) || defined(__x86_64__)
    static struct list function_list = LIST_INIT( function_list );
    struct PFN_vkInternalAllocationNotification_function *function;

    TRACE( "(%p, %p)\n", out, in );

    if (!*in)
    {
        *out = NULL;
        return;
    }
    EnterCriticalSection( &function_section );

    LIST_FOR_EACH_ENTRY( function, &function_list,
            struct PFN_vkInternalAllocationNotification_function, entry )
        if (function->code.function == *in) break;

    if (&function->entry == &function_list)
    {
        function = HeapAlloc( function_heap, 0, sizeof(*function) );
        list_add_tail( &function_list, &function->entry );
        function->code = PFN_vkInternalAllocationNotification_code;
        function->code.function = *in;
    }
    *out = (void *)&function->code;

    LeaveCriticalSection( &function_section );
#else
    *out = *in;
#endif
}
void release_PFN_vkInternalAllocationNotification( PFN_vkInternalAllocationNotification *out,
        PFN_vkInternalAllocationNotification_host *in )
{
    TRACE( "(%p, %p)\n", out, in );
}

#if defined(__i386__)
#include <pshpack1.h>
struct PFN_vkInternalFreeNotification_opcodes
{
    BYTE pushl_ebp[1];      /* pushl %ebp */
    BYTE movl_esp_ebp[2];   /* movl %esp,%ebp */
    BYTE subl_0x08_esp[3];  /* subl $8,%esp */
    BYTE pushl_0x14_ebp[3]; /* pushl 20(%ebp) */
    BYTE pushl_0x10_ebp[3]; /* pushl 16(%ebp) */
    BYTE pushl_0x0c_ebp[3]; /* pushl 12(%ebp) */
    BYTE pushl_0x08_ebp[3]; /* pushl 8(%ebp) */
    BYTE movl_func_eax[1];  /* movl function,%eax */
    void *function;
    BYTE call_eax[2];       /* call *%eax */
    BYTE leave[1];          /* leave */
    BYTE ret[1];            /* ret */
};
#include <poppack.h>

static const struct PFN_vkInternalFreeNotification_opcodes PFN_vkInternalFreeNotification_code =
{
    { 0x55 },               /* pushl %ebp */
    { 0x89, 0xe5 },         /* movl %esp,%ebp */
    { 0x83, 0xec, 0x08 },   /* subl $8,%esp */
    { 0xff, 0x75, 0x14 },   /* pushl 20(%ebp) */
    { 0xff, 0x75, 0x10 },   /* pushl 16(%ebp) */
    { 0xff, 0x75, 0x0c },   /* pushl 12(%ebp) */
    { 0xff, 0x75, 0x08 },   /* pushl 8(%ebp) */
    { 0xb8 },               /* movl function,%eax */
    NULL,
    { 0xff, 0xd0 },         /* call *%eax */
    { 0xc9 },               /* leave */
    { 0xc3 },               /* ret */
};
#elif defined(__x86_64__)
#include <pshpack1.h>
struct PFN_vkInternalFreeNotification_opcodes
{
    BYTE pushq_rbp[1];          /* pushq %rbp */
    BYTE movq_rsp_rbp[3];       /* movq %rsp,%rbp */
    BYTE subq_0x20_rsp[4];      /* subq $0x20,%rsp */
    BYTE movq_rcx_r9[3];        /* movq %rcx,%r9 */
    BYTE movq_rdx_r8[3];        /* movq %rdx,%r8 */
    BYTE movq_rsi_rdx[3];       /* movq %rsi,%rdx */
    BYTE movq_rsi_rcx[3];       /* movq %rdi,%rcx */
    BYTE movq_func_rax[2];      /* movq function,%rax */
    void *function;
    BYTE call_rax[2];           /* call *%rax */
    BYTE movq_rbp_rsp[3];       /* movq %rbp,%rsp */
    BYTE popq_rbp[1];           /* popq %rbp */
    BYTE ret[1];                /* ret */
};
#include <poppack.h>

static const struct PFN_vkInternalFreeNotification_opcodes PFN_vkInternalFreeNotification_code =
{
    { 0x55 },                   /* pushq %rbp */
    { 0x48, 0x89, 0xe5 },       /* movq %rsp,%rbp */
    { 0x48, 0x83, 0xec, 0x20 }, /* subq $0x20,%rsp */
    { 0x49, 0x89, 0xc9 },       /* movq %rcx,%r9 */
    { 0x49, 0x89, 0xd0 },       /* movq %rdx,%r8 */
    { 0x48, 0x89, 0xf2 },       /* movq %rsi,%rdx */
    { 0x48, 0x89, 0xf9 },       /* movq %rdi,%rcx */
    { 0x48, 0xb8 },             /* movq function,%rax */
    NULL,
    { 0xff, 0xd0 },             /* call *%eax */
    { 0x48, 0x89, 0xec },       /* movq %rbp,%rsp */
    { 0x5d },                   /* popq %rbp */
    { 0xc3 },                   /* ret */
};
#endif

#if defined(__i386__) || defined(__x86_64__)
struct PFN_vkInternalFreeNotification_function
{
    struct list entry;
    struct PFN_vkInternalFreeNotification_opcodes code;
};
#endif

void convert_PFN_vkInternalFreeNotification( PFN_vkInternalFreeNotification_host *out,
        const PFN_vkInternalFreeNotification *in )
{
#if defined(__i386__) || defined(__x86_64__)
    static struct list function_list = LIST_INIT( function_list );
    struct PFN_vkInternalFreeNotification_function *function;

    TRACE( "(%p, %p)\n", out, in );

    if (!*in)
    {
        *out = NULL;
        return;
    }
    EnterCriticalSection( &function_section );

    LIST_FOR_EACH_ENTRY( function, &function_list, struct PFN_vkInternalFreeNotification_function,
            entry )
        if (function->code.function == *in) break;

    if (&function->entry == &function_list)
    {
        function = HeapAlloc( function_heap, 0, sizeof(*function) );
        list_add_tail( &function_list, &function->entry );
        function->code = PFN_vkInternalFreeNotification_code;
        function->code.function = *in;
    }
    *out = (void *)&function->code;

    LeaveCriticalSection( &function_section );
#else
    *out = *in;
#endif
}
void release_PFN_vkInternalFreeNotification( PFN_vkInternalFreeNotification *out,
        PFN_vkInternalFreeNotification_host *in )
{
    TRACE( "(%p, %p)\n", out, in );
}

VkAllocationCallbacks_host *convert_VkAllocationCallbacks( VkAllocationCallbacks_host *out,
        const VkAllocationCallbacks *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->pUserData  = in->pUserData;
    convert_PFN_vkAllocationFunction( &out->pfnAllocation, &in->pfnAllocation );
    convert_PFN_vkReallocationFunction( &out->pfnReallocation, &in->pfnReallocation );
    convert_PFN_vkFreeFunction( &out->pfnFree, &in->pfnFree );
    convert_PFN_vkInternalAllocationNotification( &out->pfnInternalAllocation, &in->pfnInternalAllocation );
    convert_PFN_vkInternalFreeNotification( &out->pfnInternalFree, &in->pfnInternalFree );

    return out;
}
void release_VkAllocationCallbacks( VkAllocationCallbacks *out, VkAllocationCallbacks_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return;

    release_PFN_vkAllocationFunction( out ? &out->pfnAllocation : NULL, &in->pfnAllocation );
    release_PFN_vkReallocationFunction( out ? &out->pfnReallocation : NULL, &in->pfnReallocation );
    release_PFN_vkFreeFunction( out ? &out->pfnFree : NULL, &in->pfnFree );
    release_PFN_vkInternalAllocationNotification( out ? &out->pfnInternalAllocation : NULL, &in->pfnInternalAllocation );
    release_PFN_vkInternalFreeNotification( out ? &out->pfnInternalFree : NULL, &in->pfnInternalFree );

    if (!out) return;

    out->pUserData  = in->pUserData;
}

VkAllocationCallbacks_host *convert_VkAllocationCallbacks_array( const VkAllocationCallbacks *in,
        int count )
{
    VkAllocationCallbacks_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkAllocationCallbacks( &out[i], &in[i] );

    return out;
}

void release_VkAllocationCallbacks_array( VkAllocationCallbacks *out,
        VkAllocationCallbacks_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkAllocationCallbacks( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}

#if defined(USE_STRUCT_CONVERSION)
VkCommandBufferInheritanceInfo_host *convert_VkCommandBufferInheritanceInfo(
        VkCommandBufferInheritanceInfo_host *out, const VkCommandBufferInheritanceInfo *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->sType                  = in->sType;
    out->pNext                  = in->pNext;
    out->renderPass             = in->renderPass;
    out->subpass                = in->subpass;
    out->framebuffer            = in->framebuffer;
    out->occlusionQueryEnable   = in->occlusionQueryEnable;
    out->queryFlags             = in->queryFlags;
    out->pipelineStatistics     = in->pipelineStatistics;

    return out;
}
void release_VkCommandBufferInheritanceInfo( VkCommandBufferInheritanceInfo *out,
        VkCommandBufferInheritanceInfo_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->sType                  = in->sType;
    out->pNext                  = in->pNext;
    out->renderPass             = in->renderPass;
    out->subpass                = in->subpass;
    out->framebuffer            = in->framebuffer;
    out->occlusionQueryEnable   = in->occlusionQueryEnable;
    out->queryFlags             = in->queryFlags;
    out->pipelineStatistics     = in->pipelineStatistics;
}

VkCommandBufferInheritanceInfo_host *convert_VkCommandBufferInheritanceInfo_array(
        const VkCommandBufferInheritanceInfo *in, int count )
{
    VkCommandBufferInheritanceInfo_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkCommandBufferInheritanceInfo( &out[i], &in[i] );

    return out;
}

void release_VkCommandBufferInheritanceInfo_array( VkCommandBufferInheritanceInfo *out,
        VkCommandBufferInheritanceInfo_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkCommandBufferInheritanceInfo( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkCommandBufferBeginInfo_host *convert_VkCommandBufferBeginInfo(
        VkCommandBufferBeginInfo_host *out, const VkCommandBufferBeginInfo *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->sType              = in->sType;
    out->pNext              = in->pNext;
    out->flags              = in->flags;
    out->pInheritanceInfo   = convert_VkCommandBufferInheritanceInfo_array( in->pInheritanceInfo, 1 );

    return out;
}
void release_VkCommandBufferBeginInfo( VkCommandBufferBeginInfo *out,
        VkCommandBufferBeginInfo_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return;

    release_VkCommandBufferInheritanceInfo_array( out ? out->pInheritanceInfo : NULL, in->pInheritanceInfo, 1 );

    if (!out) return;

    out->sType              = in->sType;
    out->pNext              = in->pNext;
    out->flags              = in->flags;
}

VkCommandBufferBeginInfo_host *convert_VkCommandBufferBeginInfo_array(
        const VkCommandBufferBeginInfo *in, int count )
{
    VkCommandBufferBeginInfo_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkCommandBufferBeginInfo( &out[i], &in[i] );

    return out;
}

void release_VkCommandBufferBeginInfo_array( VkCommandBufferBeginInfo *out,
        VkCommandBufferBeginInfo_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkCommandBufferBeginInfo( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkImageMemoryBarrier_host *convert_VkImageMemoryBarrier( VkImageMemoryBarrier_host *out,
        const VkImageMemoryBarrier *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->sType                  = in->sType;
    out->pNext                  = in->pNext;
    out->srcAccessMask          = in->srcAccessMask;
    out->dstAccessMask          = in->dstAccessMask;
    out->oldLayout              = in->oldLayout;
    out->newLayout              = in->newLayout;
    out->srcQueueFamilyIndex    = in->srcQueueFamilyIndex;
    out->dstQueueFamilyIndex    = in->dstQueueFamilyIndex;
    out->image                  = in->image;
    out->subresourceRange       = in->subresourceRange;

    return out;
}
void release_VkImageMemoryBarrier( VkImageMemoryBarrier *out, VkImageMemoryBarrier_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->sType                  = in->sType;
    out->pNext                  = in->pNext;
    out->srcAccessMask          = in->srcAccessMask;
    out->dstAccessMask          = in->dstAccessMask;
    out->oldLayout              = in->oldLayout;
    out->newLayout              = in->newLayout;
    out->srcQueueFamilyIndex    = in->srcQueueFamilyIndex;
    out->dstQueueFamilyIndex    = in->dstQueueFamilyIndex;
    out->image                  = in->image;
    out->subresourceRange       = in->subresourceRange;
}

VkImageMemoryBarrier_host *convert_VkImageMemoryBarrier_array( const VkImageMemoryBarrier *in,
        int count )
{
    VkImageMemoryBarrier_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkImageMemoryBarrier( &out[i], &in[i] );

    return out;
}

void release_VkImageMemoryBarrier_array( VkImageMemoryBarrier *out, VkImageMemoryBarrier_host *in,
        int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkImageMemoryBarrier( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkBufferCreateInfo_host *convert_VkBufferCreateInfo( VkBufferCreateInfo_host *out,
        const VkBufferCreateInfo *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->sType                  = in->sType;
    out->pNext                  = in->pNext;
    out->flags                  = in->flags;
    out->size                   = in->size;
    out->usage                  = in->usage;
    out->sharingMode            = in->sharingMode;
    out->queueFamilyIndexCount  = in->queueFamilyIndexCount;
    out->pQueueFamilyIndices    = in->pQueueFamilyIndices; /* length is queueFamilyIndexCount */

    return out;
}
void release_VkBufferCreateInfo( VkBufferCreateInfo *out, VkBufferCreateInfo_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->sType                  = in->sType;
    out->pNext                  = in->pNext;
    out->flags                  = in->flags;
    out->size                   = in->size;
    out->usage                  = in->usage;
    out->sharingMode            = in->sharingMode;
    out->queueFamilyIndexCount  = in->queueFamilyIndexCount;
    out->pQueueFamilyIndices    = in->pQueueFamilyIndices; /* length is queueFamilyIndexCount */
}

VkBufferCreateInfo_host *convert_VkBufferCreateInfo_array( const VkBufferCreateInfo *in, int count )
{
    VkBufferCreateInfo_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkBufferCreateInfo( &out[i], &in[i] );

    return out;
}

void release_VkBufferCreateInfo_array( VkBufferCreateInfo *out, VkBufferCreateInfo_host *in,
        int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkBufferCreateInfo( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkBufferViewCreateInfo_host *convert_VkBufferViewCreateInfo( VkBufferViewCreateInfo_host *out,
        const VkBufferViewCreateInfo *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->sType      = in->sType;
    out->pNext      = in->pNext;
    out->flags      = in->flags;
    out->buffer     = in->buffer;
    out->format     = in->format;
    out->offset     = in->offset;
    out->range      = in->range;

    return out;
}
void release_VkBufferViewCreateInfo( VkBufferViewCreateInfo *out, VkBufferViewCreateInfo_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->sType      = in->sType;
    out->pNext      = in->pNext;
    out->flags      = in->flags;
    out->buffer     = in->buffer;
    out->format     = in->format;
    out->offset     = in->offset;
    out->range      = in->range;
}

VkBufferViewCreateInfo_host *convert_VkBufferViewCreateInfo_array(
        const VkBufferViewCreateInfo *in, int count )
{
    VkBufferViewCreateInfo_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkBufferViewCreateInfo( &out[i], &in[i] );

    return out;
}

void release_VkBufferViewCreateInfo_array( VkBufferViewCreateInfo *out,
        VkBufferViewCreateInfo_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkBufferViewCreateInfo( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkPipelineShaderStageCreateInfo_host *convert_VkPipelineShaderStageCreateInfo(
        VkPipelineShaderStageCreateInfo_host *out, const VkPipelineShaderStageCreateInfo *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->sType                  = in->sType;
    out->pNext                  = in->pNext;
    out->flags                  = in->flags;
    out->stage                  = in->stage;
    out->module                 = in->module;
    out->pName                  = in->pName;
    out->pSpecializationInfo    = in->pSpecializationInfo;

    return out;
}
void release_VkPipelineShaderStageCreateInfo( VkPipelineShaderStageCreateInfo *out,
        VkPipelineShaderStageCreateInfo_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->sType                  = in->sType;
    out->pNext                  = in->pNext;
    out->flags                  = in->flags;
    out->stage                  = in->stage;
    out->module                 = in->module;
    out->pName                  = in->pName;
    out->pSpecializationInfo    = in->pSpecializationInfo;
}

VkPipelineShaderStageCreateInfo_host *convert_VkPipelineShaderStageCreateInfo_array(
        const VkPipelineShaderStageCreateInfo *in, int count )
{
    VkPipelineShaderStageCreateInfo_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkPipelineShaderStageCreateInfo( &out[i], &in[i] );

    return out;
}

void release_VkPipelineShaderStageCreateInfo_array( VkPipelineShaderStageCreateInfo *out,
        VkPipelineShaderStageCreateInfo_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkPipelineShaderStageCreateInfo( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkComputePipelineCreateInfo_host *convert_VkComputePipelineCreateInfo(
        VkComputePipelineCreateInfo_host *out, const VkComputePipelineCreateInfo *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->sType                  = in->sType;
    out->pNext                  = in->pNext;
    out->flags                  = in->flags;
    convert_VkPipelineShaderStageCreateInfo( &out->stage, &in->stage );
    out->layout                 = in->layout;
    out->basePipelineHandle     = in->basePipelineHandle;
    out->basePipelineIndex      = in->basePipelineIndex;

    return out;
}
void release_VkComputePipelineCreateInfo( VkComputePipelineCreateInfo *out,
        VkComputePipelineCreateInfo_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return;

    release_VkPipelineShaderStageCreateInfo( out ? &out->stage : NULL, &in->stage );

    if (!out) return;

    out->sType                  = in->sType;
    out->pNext                  = in->pNext;
    out->flags                  = in->flags;
    out->layout                 = in->layout;
    out->basePipelineHandle     = in->basePipelineHandle;
    out->basePipelineIndex      = in->basePipelineIndex;
}

VkComputePipelineCreateInfo_host *convert_VkComputePipelineCreateInfo_array(
        const VkComputePipelineCreateInfo *in, int count )
{
    VkComputePipelineCreateInfo_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkComputePipelineCreateInfo( &out[i], &in[i] );

    return out;
}

void release_VkComputePipelineCreateInfo_array( VkComputePipelineCreateInfo *out,
        VkComputePipelineCreateInfo_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkComputePipelineCreateInfo( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(__i386__)
#include <pshpack1.h>
struct PFN_vkDebugReportCallbackEXT_opcodes
{
    BYTE pushl_ebp[1];      /* pushl %ebp */
    BYTE movl_esp_ebp[2];   /* movl %esp,%ebp */
    BYTE subl_0x04_esp[3];  /* subl $4,%esp */
    BYTE pushl_0x28_ebp[3]; /* pushl 40(%ebp) */
    BYTE pushl_0x24_ebp[3]; /* pushl 36(%ebp) */
    BYTE pushl_0x20_ebp[3]; /* pushl 32(%ebp) */
    BYTE pushl_0x1c_ebp[3]; /* pushl 28(%ebp) */
    BYTE pushl_0x18_ebp[3]; /* pushl 24(%ebp) */
    BYTE pushl_0x14_ebp[3]; /* pushl 20(%ebp) */
    BYTE pushl_0x10_ebp[3]; /* pushl 16(%ebp) */
    BYTE pushl_0x0c_ebp[3]; /* pushl 12(%ebp) */
    BYTE pushl_0x08_ebp[3]; /* pushl 8(%ebp) */
    BYTE movl_func_eax[1];  /* movl function,%eax */
    void *function;
    BYTE call_eax[2];       /* call *%eax */
    BYTE leave[1];          /* leave */
    BYTE ret[1];            /* ret */
};
#include <poppack.h>

static const struct PFN_vkDebugReportCallbackEXT_opcodes PFN_vkDebugReportCallbackEXT_code =
{
    { 0x55 },               /* pushl %ebp */
    { 0x89, 0xe5 },         /* movl %esp,%ebp */
    { 0x83, 0xec, 0x04 },   /* subl $4,%esp */
    { 0xff, 0x75, 0x28 },   /* pushl 40(%ebp) */
    { 0xff, 0x75, 0x24 },   /* pushl 36(%ebp) */
    { 0xff, 0x75, 0x20 },   /* pushl 32(%ebp) */
    { 0xff, 0x75, 0x1c },   /* pushl 28(%ebp) */
    { 0xff, 0x75, 0x18 },   /* pushl 24(%ebp) */
    { 0xff, 0x75, 0x14 },   /* pushl 20(%ebp) */
    { 0xff, 0x75, 0x10 },   /* pushl 16(%ebp) */
    { 0xff, 0x75, 0x0c },   /* pushl 12(%ebp) */
    { 0xff, 0x75, 0x08 },   /* pushl 8(%ebp) */
    { 0xb8 },               /* movl function,%eax */
    NULL,
    { 0xff, 0xd0 },         /* call *%eax */
    { 0xc9 },               /* leave */
    { 0xc3 },               /* ret */
};
#elif defined(__x86_64__)
#include <pshpack1.h>
struct PFN_vkDebugReportCallbackEXT_opcodes
{
    BYTE pushq_rbp[1];          /* pushq %rbp */
    BYTE movq_rsp_rbp[3];       /* movq %rsp,%rbp */
    BYTE pushq_0x18_rbp[3];     /* pushq 24(%rbp) */
    BYTE pushq_0x10_rbp[3];     /* pushq 16(%rbp) */
    BYTE pushq_r9[2];           /* pushq %r9 */
    BYTE pushq_r8[2];           /* pushq %r8 */
    BYTE subq_0x20_rsp[4];      /* subq $0x20,%rsp */
    BYTE movq_rcx_r9[3];        /* movq %rcx,%r9 */
    BYTE movq_rdx_r8[3];        /* movq %rdx,%r8 */
    BYTE movq_rsi_rdx[3];       /* movq %rsi,%rdx */
    BYTE movq_rsi_rcx[3];       /* movq %rdi,%rcx */
    BYTE movq_func_rax[2];      /* movq function,%rax */
    void *function;
    BYTE call_rax[2];           /* call *%rax */
    BYTE movq_rbp_rsp[3];       /* movq %rbp,%rsp */
    BYTE popq_rbp[1];           /* popq %rbp */
    BYTE ret[1];                /* ret */
};
#include <poppack.h>

static const struct PFN_vkDebugReportCallbackEXT_opcodes PFN_vkDebugReportCallbackEXT_code =
{
    { 0x55 },                   /* pushq %rbp */
    { 0x48, 0x89, 0xe5 },       /* movq %rsp,%rbp */
    { 0xff, 0x75, 0x18 },       /* pushq 24(%rbp) */
    { 0xff, 0x75, 0x10 },       /* pushq 16(%rbp) */
    { 0x41, 0x51 },             /* pushq %r9 */
    { 0x41, 0x50 },             /* pushq %r8 */
    { 0x48, 0x83, 0xec, 0x20 }, /* subq $0x20,%rsp */
    { 0x49, 0x89, 0xc9 },       /* movq %rcx,%r9 */
    { 0x49, 0x89, 0xd0 },       /* movq %rdx,%r8 */
    { 0x48, 0x89, 0xf2 },       /* movq %rsi,%rdx */
    { 0x48, 0x89, 0xf9 },       /* movq %rdi,%rcx */
    { 0x48, 0xb8 },             /* movq function,%rax */
    NULL,
    { 0xff, 0xd0 },             /* call *%eax */
    { 0x48, 0x89, 0xec },       /* movq %rbp,%rsp */
    { 0x5d },                   /* popq %rbp */
    { 0xc3 },                   /* ret */
};
#endif

#if defined(__i386__) || defined(__x86_64__)
struct PFN_vkDebugReportCallbackEXT_function
{
    struct list entry;
    struct PFN_vkDebugReportCallbackEXT_opcodes code;
};
#endif

void convert_PFN_vkDebugReportCallbackEXT( PFN_vkDebugReportCallbackEXT_host *out,
        const PFN_vkDebugReportCallbackEXT *in )
{
#if defined(__i386__) || defined(__x86_64__)
    static struct list function_list = LIST_INIT( function_list );
    struct PFN_vkDebugReportCallbackEXT_function *function;

    TRACE( "(%p, %p)\n", out, in );

    if (!*in)
    {
        *out = NULL;
        return;
    }
    EnterCriticalSection( &function_section );

    LIST_FOR_EACH_ENTRY( function, &function_list, struct PFN_vkDebugReportCallbackEXT_function,
            entry )
        if (function->code.function == *in) break;

    if (&function->entry == &function_list)
    {
        function = HeapAlloc( function_heap, 0, sizeof(*function) );
        list_add_tail( &function_list, &function->entry );
        function->code = PFN_vkDebugReportCallbackEXT_code;
        function->code.function = *in;
    }
    *out = (void *)&function->code;

    LeaveCriticalSection( &function_section );
#else
    *out = *in;
#endif
}
void release_PFN_vkDebugReportCallbackEXT( PFN_vkDebugReportCallbackEXT *out,
        PFN_vkDebugReportCallbackEXT_host *in )
{
    TRACE( "(%p, %p)\n", out, in );
}

VkDebugReportCallbackCreateInfoEXT_host *convert_VkDebugReportCallbackCreateInfoEXT(
        VkDebugReportCallbackCreateInfoEXT_host *out, const VkDebugReportCallbackCreateInfoEXT *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->sType      = in->sType;
    out->pNext      = in->pNext;
    out->flags      = in->flags;
    convert_PFN_vkDebugReportCallbackEXT( &out->pfnCallback, &in->pfnCallback );
    out->pUserData  = in->pUserData;

    return out;
}
void release_VkDebugReportCallbackCreateInfoEXT( VkDebugReportCallbackCreateInfoEXT *out,
        VkDebugReportCallbackCreateInfoEXT_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return;

    release_PFN_vkDebugReportCallbackEXT( out ? &out->pfnCallback : NULL, &in->pfnCallback );

    if (!out) return;

    out->sType      = in->sType;
    out->pNext      = in->pNext;
    out->flags      = in->flags;
    out->pUserData  = in->pUserData;
}

VkDebugReportCallbackCreateInfoEXT_host *convert_VkDebugReportCallbackCreateInfoEXT_array(
        const VkDebugReportCallbackCreateInfoEXT *in, int count )
{
    VkDebugReportCallbackCreateInfoEXT_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkDebugReportCallbackCreateInfoEXT( &out[i], &in[i] );

    return out;
}

void release_VkDebugReportCallbackCreateInfoEXT_array( VkDebugReportCallbackCreateInfoEXT *out,
        VkDebugReportCallbackCreateInfoEXT_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkDebugReportCallbackCreateInfoEXT( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}

#if defined(USE_STRUCT_CONVERSION)
VkDisplaySurfaceCreateInfoKHR_host *convert_VkDisplaySurfaceCreateInfoKHR(
        VkDisplaySurfaceCreateInfoKHR_host *out, const VkDisplaySurfaceCreateInfoKHR *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->sType              = in->sType;
    out->pNext              = in->pNext;
    out->flags              = in->flags;
    out->displayMode        = in->displayMode;
    out->planeIndex         = in->planeIndex;
    out->planeStackIndex    = in->planeStackIndex;
    out->transform          = in->transform;
    out->globalAlpha        = in->globalAlpha;
    out->alphaMode          = in->alphaMode;
    out->imageExtent        = in->imageExtent;

    return out;
}
void release_VkDisplaySurfaceCreateInfoKHR( VkDisplaySurfaceCreateInfoKHR *out,
        VkDisplaySurfaceCreateInfoKHR_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->sType              = in->sType;
    out->pNext              = in->pNext;
    out->flags              = in->flags;
    out->displayMode        = in->displayMode;
    out->planeIndex         = in->planeIndex;
    out->planeStackIndex    = in->planeStackIndex;
    out->transform          = in->transform;
    out->globalAlpha        = in->globalAlpha;
    out->alphaMode          = in->alphaMode;
    out->imageExtent        = in->imageExtent;
}

VkDisplaySurfaceCreateInfoKHR_host *convert_VkDisplaySurfaceCreateInfoKHR_array(
        const VkDisplaySurfaceCreateInfoKHR *in, int count )
{
    VkDisplaySurfaceCreateInfoKHR_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkDisplaySurfaceCreateInfoKHR( &out[i], &in[i] );

    return out;
}

void release_VkDisplaySurfaceCreateInfoKHR_array( VkDisplaySurfaceCreateInfoKHR *out,
        VkDisplaySurfaceCreateInfoKHR_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkDisplaySurfaceCreateInfoKHR( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkFramebufferCreateInfo_host *convert_VkFramebufferCreateInfo( VkFramebufferCreateInfo_host *out,
        const VkFramebufferCreateInfo *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->sType              = in->sType;
    out->pNext              = in->pNext;
    out->flags              = in->flags;
    out->renderPass         = in->renderPass;
    out->attachmentCount    = in->attachmentCount;
    out->pAttachments       = in->pAttachments; /* length is attachmentCount */
    out->width              = in->width;
    out->height             = in->height;
    out->layers             = in->layers;

    return out;
}
void release_VkFramebufferCreateInfo( VkFramebufferCreateInfo *out,
        VkFramebufferCreateInfo_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->sType              = in->sType;
    out->pNext              = in->pNext;
    out->flags              = in->flags;
    out->renderPass         = in->renderPass;
    out->attachmentCount    = in->attachmentCount;
    out->pAttachments       = in->pAttachments; /* length is attachmentCount */
    out->width              = in->width;
    out->height             = in->height;
    out->layers             = in->layers;
}

VkFramebufferCreateInfo_host *convert_VkFramebufferCreateInfo_array(
        const VkFramebufferCreateInfo *in, int count )
{
    VkFramebufferCreateInfo_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkFramebufferCreateInfo( &out[i], &in[i] );

    return out;
}

void release_VkFramebufferCreateInfo_array( VkFramebufferCreateInfo *out,
        VkFramebufferCreateInfo_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkFramebufferCreateInfo( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkGraphicsPipelineCreateInfo_host *convert_VkGraphicsPipelineCreateInfo(
        VkGraphicsPipelineCreateInfo_host *out, const VkGraphicsPipelineCreateInfo *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->sType                  = in->sType;
    out->pNext                  = in->pNext;
    out->flags                  = in->flags;
    out->stageCount             = in->stageCount;
    out->pStages                = convert_VkPipelineShaderStageCreateInfo_array( in->pStages, in->stageCount );
    out->pVertexInputState      = in->pVertexInputState;
    out->pInputAssemblyState    = in->pInputAssemblyState;
    out->pTessellationState     = in->pTessellationState;
    out->pViewportState         = in->pViewportState;
    out->pRasterizationState    = in->pRasterizationState;
    out->pMultisampleState      = in->pMultisampleState;
    out->pDepthStencilState     = in->pDepthStencilState;
    out->pColorBlendState       = in->pColorBlendState;
    out->pDynamicState          = in->pDynamicState;
    out->layout                 = in->layout;
    out->renderPass             = in->renderPass;
    out->subpass                = in->subpass;
    out->basePipelineHandle     = in->basePipelineHandle;
    out->basePipelineIndex      = in->basePipelineIndex;

    return out;
}
void release_VkGraphicsPipelineCreateInfo( VkGraphicsPipelineCreateInfo *out,
        VkGraphicsPipelineCreateInfo_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return;

    release_VkPipelineShaderStageCreateInfo_array( out ? out->pStages : NULL, in->pStages, in->stageCount );

    if (!out) return;

    out->sType                  = in->sType;
    out->pNext                  = in->pNext;
    out->flags                  = in->flags;
    out->stageCount             = in->stageCount;
    out->pVertexInputState      = in->pVertexInputState;
    out->pInputAssemblyState    = in->pInputAssemblyState;
    out->pTessellationState     = in->pTessellationState;
    out->pViewportState         = in->pViewportState;
    out->pRasterizationState    = in->pRasterizationState;
    out->pMultisampleState      = in->pMultisampleState;
    out->pDepthStencilState     = in->pDepthStencilState;
    out->pColorBlendState       = in->pColorBlendState;
    out->pDynamicState          = in->pDynamicState;
    out->layout                 = in->layout;
    out->renderPass             = in->renderPass;
    out->subpass                = in->subpass;
    out->basePipelineHandle     = in->basePipelineHandle;
    out->basePipelineIndex      = in->basePipelineIndex;
}

VkGraphicsPipelineCreateInfo_host *convert_VkGraphicsPipelineCreateInfo_array(
        const VkGraphicsPipelineCreateInfo *in, int count )
{
    VkGraphicsPipelineCreateInfo_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkGraphicsPipelineCreateInfo( &out[i], &in[i] );

    return out;
}

void release_VkGraphicsPipelineCreateInfo_array( VkGraphicsPipelineCreateInfo *out,
        VkGraphicsPipelineCreateInfo_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkGraphicsPipelineCreateInfo( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkImageViewCreateInfo_host *convert_VkImageViewCreateInfo( VkImageViewCreateInfo_host *out,
        const VkImageViewCreateInfo *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->sType              = in->sType;
    out->pNext              = in->pNext;
    out->flags              = in->flags;
    out->image              = in->image;
    out->viewType           = in->viewType;
    out->format             = in->format;
    out->components         = in->components;
    out->subresourceRange   = in->subresourceRange;

    return out;
}
void release_VkImageViewCreateInfo( VkImageViewCreateInfo *out, VkImageViewCreateInfo_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->sType              = in->sType;
    out->pNext              = in->pNext;
    out->flags              = in->flags;
    out->image              = in->image;
    out->viewType           = in->viewType;
    out->format             = in->format;
    out->components         = in->components;
    out->subresourceRange   = in->subresourceRange;
}

VkImageViewCreateInfo_host *convert_VkImageViewCreateInfo_array( const VkImageViewCreateInfo *in,
        int count )
{
    VkImageViewCreateInfo_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkImageViewCreateInfo( &out[i], &in[i] );

    return out;
}

void release_VkImageViewCreateInfo_array( VkImageViewCreateInfo *out,
        VkImageViewCreateInfo_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkImageViewCreateInfo( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkSwapchainCreateInfoKHR_host *convert_VkSwapchainCreateInfoKHR(
        VkSwapchainCreateInfoKHR_host *out, const VkSwapchainCreateInfoKHR *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->sType                  = in->sType;
    out->pNext                  = in->pNext;
    out->flags                  = in->flags;
    out->surface                = in->surface;
    out->minImageCount          = in->minImageCount;
    out->imageFormat            = in->imageFormat;
    out->imageColorSpace        = in->imageColorSpace;
    out->imageExtent            = in->imageExtent;
    out->imageArrayLayers       = in->imageArrayLayers;
    out->imageUsage             = in->imageUsage;
    out->imageSharingMode       = in->imageSharingMode;
    out->queueFamilyIndexCount  = in->queueFamilyIndexCount;
    out->pQueueFamilyIndices    = in->pQueueFamilyIndices; /* length is queueFamilyIndexCount */
    out->preTransform           = in->preTransform;
    out->compositeAlpha         = in->compositeAlpha;
    out->presentMode            = in->presentMode;
    out->clipped                = in->clipped;
    out->oldSwapchain           = in->oldSwapchain;

    return out;
}
void release_VkSwapchainCreateInfoKHR( VkSwapchainCreateInfoKHR *out,
        VkSwapchainCreateInfoKHR_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->sType                  = in->sType;
    out->pNext                  = in->pNext;
    out->flags                  = in->flags;
    out->surface                = in->surface;
    out->minImageCount          = in->minImageCount;
    out->imageFormat            = in->imageFormat;
    out->imageColorSpace        = in->imageColorSpace;
    out->imageExtent            = in->imageExtent;
    out->imageArrayLayers       = in->imageArrayLayers;
    out->imageUsage             = in->imageUsage;
    out->imageSharingMode       = in->imageSharingMode;
    out->queueFamilyIndexCount  = in->queueFamilyIndexCount;
    out->pQueueFamilyIndices    = in->pQueueFamilyIndices; /* length is queueFamilyIndexCount */
    out->preTransform           = in->preTransform;
    out->compositeAlpha         = in->compositeAlpha;
    out->presentMode            = in->presentMode;
    out->clipped                = in->clipped;
    out->oldSwapchain           = in->oldSwapchain;
}

VkSwapchainCreateInfoKHR_host *convert_VkSwapchainCreateInfoKHR_array(
        const VkSwapchainCreateInfoKHR *in, int count )
{
    VkSwapchainCreateInfoKHR_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkSwapchainCreateInfoKHR( &out[i], &in[i] );

    return out;
}

void release_VkSwapchainCreateInfoKHR_array( VkSwapchainCreateInfoKHR *out,
        VkSwapchainCreateInfoKHR_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkSwapchainCreateInfoKHR( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkDisplayModePropertiesKHR_host *convert_VkDisplayModePropertiesKHR(
        VkDisplayModePropertiesKHR_host *out, const VkDisplayModePropertiesKHR *in )
{
    TRACE( "(%p, %p)\n", out, in );

    /* return-only type, skipping conversion */
    return in ? out : NULL;
}
void release_VkDisplayModePropertiesKHR( VkDisplayModePropertiesKHR *out,
        VkDisplayModePropertiesKHR_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->displayMode    = in->displayMode;
    out->parameters     = in->parameters;
}

VkDisplayModePropertiesKHR_host *convert_VkDisplayModePropertiesKHR_array(
        const VkDisplayModePropertiesKHR *in, int count )
{
    VkDisplayModePropertiesKHR_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkDisplayModePropertiesKHR( &out[i], &in[i] );

    return out;
}

void release_VkDisplayModePropertiesKHR_array( VkDisplayModePropertiesKHR *out,
        VkDisplayModePropertiesKHR_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkDisplayModePropertiesKHR( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkDisplayPlanePropertiesKHR_host *convert_VkDisplayPlanePropertiesKHR(
        VkDisplayPlanePropertiesKHR_host *out, const VkDisplayPlanePropertiesKHR *in )
{
    TRACE( "(%p, %p)\n", out, in );

    /* return-only type, skipping conversion */
    return in ? out : NULL;
}
void release_VkDisplayPlanePropertiesKHR( VkDisplayPlanePropertiesKHR *out,
        VkDisplayPlanePropertiesKHR_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->currentDisplay     = in->currentDisplay;
    out->currentStackIndex  = in->currentStackIndex;
}

VkDisplayPlanePropertiesKHR_host *convert_VkDisplayPlanePropertiesKHR_array(
        const VkDisplayPlanePropertiesKHR *in, int count )
{
    VkDisplayPlanePropertiesKHR_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkDisplayPlanePropertiesKHR( &out[i], &in[i] );

    return out;
}

void release_VkDisplayPlanePropertiesKHR_array( VkDisplayPlanePropertiesKHR *out,
        VkDisplayPlanePropertiesKHR_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkDisplayPlanePropertiesKHR( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkImageFormatProperties_host *convert_VkImageFormatProperties( VkImageFormatProperties_host *out,
        const VkImageFormatProperties *in )
{
    TRACE( "(%p, %p)\n", out, in );

    /* return-only type, skipping conversion */
    return in ? out : NULL;
}
void release_VkImageFormatProperties( VkImageFormatProperties *out,
        VkImageFormatProperties_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->maxExtent          = in->maxExtent;
    out->maxMipLevels       = in->maxMipLevels;
    out->maxArrayLayers     = in->maxArrayLayers;
    out->sampleCounts       = in->sampleCounts;
    out->maxResourceSize    = in->maxResourceSize;
}

VkImageFormatProperties_host *convert_VkImageFormatProperties_array(
        const VkImageFormatProperties *in, int count )
{
    VkImageFormatProperties_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkImageFormatProperties( &out[i], &in[i] );

    return out;
}

void release_VkImageFormatProperties_array( VkImageFormatProperties *out,
        VkImageFormatProperties_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkImageFormatProperties( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkExternalImageFormatPropertiesNV_host *convert_VkExternalImageFormatPropertiesNV(
        VkExternalImageFormatPropertiesNV_host *out, const VkExternalImageFormatPropertiesNV *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    convert_VkImageFormatProperties( &out->imageFormatProperties, &in->imageFormatProperties );
    out->externalMemoryFeatures         = in->externalMemoryFeatures;
    out->exportFromImportedHandleTypes  = in->exportFromImportedHandleTypes;
    out->compatibleHandleTypes          = in->compatibleHandleTypes;

    return out;
}
void release_VkExternalImageFormatPropertiesNV( VkExternalImageFormatPropertiesNV *out,
        VkExternalImageFormatPropertiesNV_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return;

    release_VkImageFormatProperties( out ? &out->imageFormatProperties : NULL, &in->imageFormatProperties );

    if (!out) return;

    out->externalMemoryFeatures         = in->externalMemoryFeatures;
    out->exportFromImportedHandleTypes  = in->exportFromImportedHandleTypes;
    out->compatibleHandleTypes          = in->compatibleHandleTypes;
}

VkExternalImageFormatPropertiesNV_host *convert_VkExternalImageFormatPropertiesNV_array(
        const VkExternalImageFormatPropertiesNV *in, int count )
{
    VkExternalImageFormatPropertiesNV_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkExternalImageFormatPropertiesNV( &out[i], &in[i] );

    return out;
}

void release_VkExternalImageFormatPropertiesNV_array( VkExternalImageFormatPropertiesNV *out,
        VkExternalImageFormatPropertiesNV_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkExternalImageFormatPropertiesNV( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkMemoryHeap_host *convert_VkMemoryHeap( VkMemoryHeap_host *out, const VkMemoryHeap *in )
{
    TRACE( "(%p, %p)\n", out, in );

    /* return-only type, skipping conversion */
    return in ? out : NULL;
}
void release_VkMemoryHeap( VkMemoryHeap *out, VkMemoryHeap_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->size   = in->size;
    out->flags  = in->flags;
}

VkMemoryHeap_host *convert_VkMemoryHeap_array( const VkMemoryHeap *in, int count )
{
    VkMemoryHeap_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkMemoryHeap( &out[i], &in[i] );

    return out;
}

void release_VkMemoryHeap_array( VkMemoryHeap *out, VkMemoryHeap_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkMemoryHeap( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkPhysicalDeviceMemoryProperties_host *convert_VkPhysicalDeviceMemoryProperties(
        VkPhysicalDeviceMemoryProperties_host *out, const VkPhysicalDeviceMemoryProperties *in )
{
    TRACE( "(%p, %p)\n", out, in );

    /* return-only type, skipping conversion */
    return in ? out : NULL;
}
void release_VkPhysicalDeviceMemoryProperties( VkPhysicalDeviceMemoryProperties *out,
        VkPhysicalDeviceMemoryProperties_host *in )
{
    int i;

    TRACE( "(%p, %p)\n", out, in );

    if (!in) return;

    for (i = 0; i < ARRAY_SIZE(in->memoryHeaps); i++)
        release_VkMemoryHeap( out ? &out->memoryHeaps[i] : NULL, &in->memoryHeaps[i] );

    if (!out) return;

    out->memoryTypeCount    = in->memoryTypeCount;
    memcpy(out->memoryTypes, in->memoryTypes, sizeof(in->memoryTypes));
    out->memoryHeapCount    = in->memoryHeapCount;
}

VkPhysicalDeviceMemoryProperties_host *convert_VkPhysicalDeviceMemoryProperties_array(
        const VkPhysicalDeviceMemoryProperties *in, int count )
{
    VkPhysicalDeviceMemoryProperties_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkPhysicalDeviceMemoryProperties( &out[i], &in[i] );

    return out;
}

void release_VkPhysicalDeviceMemoryProperties_array( VkPhysicalDeviceMemoryProperties *out,
        VkPhysicalDeviceMemoryProperties_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkPhysicalDeviceMemoryProperties( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkPhysicalDeviceLimits_host *convert_VkPhysicalDeviceLimits( VkPhysicalDeviceLimits_host *out,
        const VkPhysicalDeviceLimits *in )
{
    TRACE( "(%p, %p)\n", out, in );

    /* return-only type, skipping conversion */
    return in ? out : NULL;
}
void release_VkPhysicalDeviceLimits( VkPhysicalDeviceLimits *out, VkPhysicalDeviceLimits_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->maxImageDimension1D                      = in->maxImageDimension1D;
    out->maxImageDimension2D                      = in->maxImageDimension2D;
    out->maxImageDimension3D                      = in->maxImageDimension3D;
    out->maxImageDimensionCube                    = in->maxImageDimensionCube;
    out->maxImageArrayLayers                      = in->maxImageArrayLayers;
    out->maxTexelBufferElements                   = in->maxTexelBufferElements;
    out->maxUniformBufferRange                    = in->maxUniformBufferRange;
    out->maxStorageBufferRange                    = in->maxStorageBufferRange;
    out->maxPushConstantsSize                     = in->maxPushConstantsSize;
    out->maxMemoryAllocationCount                 = in->maxMemoryAllocationCount;
    out->maxSamplerAllocationCount                = in->maxSamplerAllocationCount;
    out->bufferImageGranularity                   = in->bufferImageGranularity;
    out->sparseAddressSpaceSize                   = in->sparseAddressSpaceSize;
    out->maxBoundDescriptorSets                   = in->maxBoundDescriptorSets;
    out->maxPerStageDescriptorSamplers            = in->maxPerStageDescriptorSamplers;
    out->maxPerStageDescriptorUniformBuffers      = in->maxPerStageDescriptorUniformBuffers;
    out->maxPerStageDescriptorStorageBuffers      = in->maxPerStageDescriptorStorageBuffers;
    out->maxPerStageDescriptorSampledImages       = in->maxPerStageDescriptorSampledImages;
    out->maxPerStageDescriptorStorageImages       = in->maxPerStageDescriptorStorageImages;
    out->maxPerStageDescriptorInputAttachments    = in->maxPerStageDescriptorInputAttachments;
    out->maxPerStageResources                     = in->maxPerStageResources;
    out->maxDescriptorSetSamplers                 = in->maxDescriptorSetSamplers;
    out->maxDescriptorSetUniformBuffers           = in->maxDescriptorSetUniformBuffers;
    out->maxDescriptorSetUniformBuffersDynamic    = in->maxDescriptorSetUniformBuffersDynamic;
    out->maxDescriptorSetStorageBuffers           = in->maxDescriptorSetStorageBuffers;
    out->maxDescriptorSetStorageBuffersDynamic    = in->maxDescriptorSetStorageBuffersDynamic;
    out->maxDescriptorSetSampledImages            = in->maxDescriptorSetSampledImages;
    out->maxDescriptorSetStorageImages            = in->maxDescriptorSetStorageImages;
    out->maxDescriptorSetInputAttachments         = in->maxDescriptorSetInputAttachments;
    out->maxVertexInputAttributes                 = in->maxVertexInputAttributes;
    out->maxVertexInputBindings                   = in->maxVertexInputBindings;
    out->maxVertexInputAttributeOffset            = in->maxVertexInputAttributeOffset;
    out->maxVertexInputBindingStride              = in->maxVertexInputBindingStride;
    out->maxVertexOutputComponents                = in->maxVertexOutputComponents;
    out->maxTessellationGenerationLevel           = in->maxTessellationGenerationLevel;
    out->maxTessellationPatchSize                 = in->maxTessellationPatchSize;
    out->maxTessellationControlPerVertexInputComponents = in->maxTessellationControlPerVertexInputComponents;
    out->maxTessellationControlPerVertexOutputComponents = in->maxTessellationControlPerVertexOutputComponents;
    out->maxTessellationControlPerPatchOutputComponents = in->maxTessellationControlPerPatchOutputComponents;
    out->maxTessellationControlTotalOutputComponents = in->maxTessellationControlTotalOutputComponents;
    out->maxTessellationEvaluationInputComponents = in->maxTessellationEvaluationInputComponents;
    out->maxTessellationEvaluationOutputComponents = in->maxTessellationEvaluationOutputComponents;
    out->maxGeometryShaderInvocations             = in->maxGeometryShaderInvocations;
    out->maxGeometryInputComponents               = in->maxGeometryInputComponents;
    out->maxGeometryOutputComponents              = in->maxGeometryOutputComponents;
    out->maxGeometryOutputVertices                = in->maxGeometryOutputVertices;
    out->maxGeometryTotalOutputComponents         = in->maxGeometryTotalOutputComponents;
    out->maxFragmentInputComponents               = in->maxFragmentInputComponents;
    out->maxFragmentOutputAttachments             = in->maxFragmentOutputAttachments;
    out->maxFragmentDualSrcAttachments            = in->maxFragmentDualSrcAttachments;
    out->maxFragmentCombinedOutputResources       = in->maxFragmentCombinedOutputResources;
    out->maxComputeSharedMemorySize               = in->maxComputeSharedMemorySize;
    memcpy(out->maxComputeWorkGroupCount, in->maxComputeWorkGroupCount, sizeof(in->maxComputeWorkGroupCount));
    out->maxComputeWorkGroupInvocations           = in->maxComputeWorkGroupInvocations;
    memcpy(out->maxComputeWorkGroupSize, in->maxComputeWorkGroupSize, sizeof(in->maxComputeWorkGroupSize));
    out->subPixelPrecisionBits                    = in->subPixelPrecisionBits;
    out->subTexelPrecisionBits                    = in->subTexelPrecisionBits;
    out->mipmapPrecisionBits                      = in->mipmapPrecisionBits;
    out->maxDrawIndexedIndexValue                 = in->maxDrawIndexedIndexValue;
    out->maxDrawIndirectCount                     = in->maxDrawIndirectCount;
    out->maxSamplerLodBias                        = in->maxSamplerLodBias;
    out->maxSamplerAnisotropy                     = in->maxSamplerAnisotropy;
    out->maxViewports                             = in->maxViewports;
    memcpy(out->maxViewportDimensions, in->maxViewportDimensions, sizeof(in->maxViewportDimensions));
    memcpy(out->viewportBoundsRange, in->viewportBoundsRange, sizeof(in->viewportBoundsRange));
    out->viewportSubPixelBits                     = in->viewportSubPixelBits;
    out->minMemoryMapAlignment                    = in->minMemoryMapAlignment;
    out->minTexelBufferOffsetAlignment            = in->minTexelBufferOffsetAlignment;
    out->minUniformBufferOffsetAlignment          = in->minUniformBufferOffsetAlignment;
    out->minStorageBufferOffsetAlignment          = in->minStorageBufferOffsetAlignment;
    out->minTexelOffset                           = in->minTexelOffset;
    out->maxTexelOffset                           = in->maxTexelOffset;
    out->minTexelGatherOffset                     = in->minTexelGatherOffset;
    out->maxTexelGatherOffset                     = in->maxTexelGatherOffset;
    out->minInterpolationOffset                   = in->minInterpolationOffset;
    out->maxInterpolationOffset                   = in->maxInterpolationOffset;
    out->subPixelInterpolationOffsetBits          = in->subPixelInterpolationOffsetBits;
    out->maxFramebufferWidth                      = in->maxFramebufferWidth;
    out->maxFramebufferHeight                     = in->maxFramebufferHeight;
    out->maxFramebufferLayers                     = in->maxFramebufferLayers;
    out->framebufferColorSampleCounts             = in->framebufferColorSampleCounts;
    out->framebufferDepthSampleCounts             = in->framebufferDepthSampleCounts;
    out->framebufferStencilSampleCounts           = in->framebufferStencilSampleCounts;
    out->framebufferNoAttachmentsSampleCounts     = in->framebufferNoAttachmentsSampleCounts;
    out->maxColorAttachments                      = in->maxColorAttachments;
    out->sampledImageColorSampleCounts            = in->sampledImageColorSampleCounts;
    out->sampledImageIntegerSampleCounts          = in->sampledImageIntegerSampleCounts;
    out->sampledImageDepthSampleCounts            = in->sampledImageDepthSampleCounts;
    out->sampledImageStencilSampleCounts          = in->sampledImageStencilSampleCounts;
    out->storageImageSampleCounts                 = in->storageImageSampleCounts;
    out->maxSampleMaskWords                       = in->maxSampleMaskWords;
    out->timestampComputeAndGraphics              = in->timestampComputeAndGraphics;
    out->timestampPeriod                          = in->timestampPeriod;
    out->maxClipDistances                         = in->maxClipDistances;
    out->maxCullDistances                         = in->maxCullDistances;
    out->maxCombinedClipAndCullDistances          = in->maxCombinedClipAndCullDistances;
    out->discreteQueuePriorities                  = in->discreteQueuePriorities;
    memcpy(out->pointSizeRange, in->pointSizeRange, sizeof(in->pointSizeRange));
    memcpy(out->lineWidthRange, in->lineWidthRange, sizeof(in->lineWidthRange));
    out->pointSizeGranularity                     = in->pointSizeGranularity;
    out->lineWidthGranularity                     = in->lineWidthGranularity;
    out->strictLines                              = in->strictLines;
    out->standardSampleLocations                  = in->standardSampleLocations;
    out->optimalBufferCopyOffsetAlignment         = in->optimalBufferCopyOffsetAlignment;
    out->optimalBufferCopyRowPitchAlignment       = in->optimalBufferCopyRowPitchAlignment;
    out->nonCoherentAtomSize                      = in->nonCoherentAtomSize;
}

VkPhysicalDeviceLimits_host *convert_VkPhysicalDeviceLimits_array(
        const VkPhysicalDeviceLimits *in, int count )
{
    VkPhysicalDeviceLimits_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkPhysicalDeviceLimits( &out[i], &in[i] );

    return out;
}

void release_VkPhysicalDeviceLimits_array( VkPhysicalDeviceLimits *out,
        VkPhysicalDeviceLimits_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkPhysicalDeviceLimits( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkPhysicalDeviceProperties_host *convert_VkPhysicalDeviceProperties(
        VkPhysicalDeviceProperties_host *out, const VkPhysicalDeviceProperties *in )
{
    TRACE( "(%p, %p)\n", out, in );

    /* return-only type, skipping conversion */
    return in ? out : NULL;
}
void release_VkPhysicalDeviceProperties( VkPhysicalDeviceProperties *out,
        VkPhysicalDeviceProperties_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return;

    release_VkPhysicalDeviceLimits( out ? &out->limits : NULL, &in->limits );

    if (!out) return;

    out->apiVersion         = in->apiVersion;
    out->driverVersion      = in->driverVersion;
    out->vendorID           = in->vendorID;
    out->deviceID           = in->deviceID;
    out->deviceType         = in->deviceType;
    memcpy(out->deviceName, in->deviceName, sizeof(in->deviceName));
    memcpy(out->pipelineCacheUUID, in->pipelineCacheUUID, sizeof(in->pipelineCacheUUID));
    out->sparseProperties   = in->sparseProperties;
}

VkPhysicalDeviceProperties_host *convert_VkPhysicalDeviceProperties_array(
        const VkPhysicalDeviceProperties *in, int count )
{
    VkPhysicalDeviceProperties_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkPhysicalDeviceProperties( &out[i], &in[i] );

    return out;
}

void release_VkPhysicalDeviceProperties_array( VkPhysicalDeviceProperties *out,
        VkPhysicalDeviceProperties_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkPhysicalDeviceProperties( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkSparseMemoryBind_host *convert_VkSparseMemoryBind( VkSparseMemoryBind_host *out,
        const VkSparseMemoryBind *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->resourceOffset     = in->resourceOffset;
    out->size               = in->size;
    out->memory             = in->memory;
    out->memoryOffset       = in->memoryOffset;
    out->flags              = in->flags;

    return out;
}
void release_VkSparseMemoryBind( VkSparseMemoryBind *out, VkSparseMemoryBind_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->resourceOffset     = in->resourceOffset;
    out->size               = in->size;
    out->memory             = in->memory;
    out->memoryOffset       = in->memoryOffset;
    out->flags              = in->flags;
}

VkSparseMemoryBind_host *convert_VkSparseMemoryBind_array( const VkSparseMemoryBind *in, int count )
{
    VkSparseMemoryBind_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkSparseMemoryBind( &out[i], &in[i] );

    return out;
}

void release_VkSparseMemoryBind_array( VkSparseMemoryBind *out, VkSparseMemoryBind_host *in,
        int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkSparseMemoryBind( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkSparseBufferMemoryBindInfo_host *convert_VkSparseBufferMemoryBindInfo(
        VkSparseBufferMemoryBindInfo_host *out, const VkSparseBufferMemoryBindInfo *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->buffer     = in->buffer;
    out->bindCount  = in->bindCount;
    out->pBinds     = convert_VkSparseMemoryBind_array( in->pBinds, in->bindCount );

    return out;
}
void release_VkSparseBufferMemoryBindInfo( VkSparseBufferMemoryBindInfo *out,
        VkSparseBufferMemoryBindInfo_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return;

    release_VkSparseMemoryBind_array( out ? out->pBinds : NULL, in->pBinds, in->bindCount );

    if (!out) return;

    out->buffer     = in->buffer;
    out->bindCount  = in->bindCount;
}

VkSparseBufferMemoryBindInfo_host *convert_VkSparseBufferMemoryBindInfo_array(
        const VkSparseBufferMemoryBindInfo *in, int count )
{
    VkSparseBufferMemoryBindInfo_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkSparseBufferMemoryBindInfo( &out[i], &in[i] );

    return out;
}

void release_VkSparseBufferMemoryBindInfo_array( VkSparseBufferMemoryBindInfo *out,
        VkSparseBufferMemoryBindInfo_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkSparseBufferMemoryBindInfo( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkSparseImageOpaqueMemoryBindInfo_host *convert_VkSparseImageOpaqueMemoryBindInfo(
        VkSparseImageOpaqueMemoryBindInfo_host *out, const VkSparseImageOpaqueMemoryBindInfo *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->image      = in->image;
    out->bindCount  = in->bindCount;
    out->pBinds     = convert_VkSparseMemoryBind_array( in->pBinds, in->bindCount );

    return out;
}
void release_VkSparseImageOpaqueMemoryBindInfo( VkSparseImageOpaqueMemoryBindInfo *out,
        VkSparseImageOpaqueMemoryBindInfo_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return;

    release_VkSparseMemoryBind_array( out ? out->pBinds : NULL, in->pBinds, in->bindCount );

    if (!out) return;

    out->image      = in->image;
    out->bindCount  = in->bindCount;
}

VkSparseImageOpaqueMemoryBindInfo_host *convert_VkSparseImageOpaqueMemoryBindInfo_array(
        const VkSparseImageOpaqueMemoryBindInfo *in, int count )
{
    VkSparseImageOpaqueMemoryBindInfo_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkSparseImageOpaqueMemoryBindInfo( &out[i], &in[i] );

    return out;
}

void release_VkSparseImageOpaqueMemoryBindInfo_array( VkSparseImageOpaqueMemoryBindInfo *out,
        VkSparseImageOpaqueMemoryBindInfo_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkSparseImageOpaqueMemoryBindInfo( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkSparseImageMemoryBind_host *convert_VkSparseImageMemoryBind( VkSparseImageMemoryBind_host *out,
        const VkSparseImageMemoryBind *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->subresource    = in->subresource;
    out->offset         = in->offset;
    out->extent         = in->extent;
    out->memory         = in->memory;
    out->memoryOffset   = in->memoryOffset;
    out->flags          = in->flags;

    return out;
}
void release_VkSparseImageMemoryBind( VkSparseImageMemoryBind *out,
        VkSparseImageMemoryBind_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->subresource    = in->subresource;
    out->offset         = in->offset;
    out->extent         = in->extent;
    out->memory         = in->memory;
    out->memoryOffset   = in->memoryOffset;
    out->flags          = in->flags;
}

VkSparseImageMemoryBind_host *convert_VkSparseImageMemoryBind_array(
        const VkSparseImageMemoryBind *in, int count )
{
    VkSparseImageMemoryBind_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkSparseImageMemoryBind( &out[i], &in[i] );

    return out;
}

void release_VkSparseImageMemoryBind_array( VkSparseImageMemoryBind *out,
        VkSparseImageMemoryBind_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkSparseImageMemoryBind( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkSparseImageMemoryBindInfo_host *convert_VkSparseImageMemoryBindInfo(
        VkSparseImageMemoryBindInfo_host *out, const VkSparseImageMemoryBindInfo *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->image      = in->image;
    out->bindCount  = in->bindCount;
    out->pBinds     = convert_VkSparseImageMemoryBind_array( in->pBinds, in->bindCount );

    return out;
}
void release_VkSparseImageMemoryBindInfo( VkSparseImageMemoryBindInfo *out,
        VkSparseImageMemoryBindInfo_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return;

    release_VkSparseImageMemoryBind_array( out ? out->pBinds : NULL, in->pBinds, in->bindCount );

    if (!out) return;

    out->image      = in->image;
    out->bindCount  = in->bindCount;
}

VkSparseImageMemoryBindInfo_host *convert_VkSparseImageMemoryBindInfo_array(
        const VkSparseImageMemoryBindInfo *in, int count )
{
    VkSparseImageMemoryBindInfo_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkSparseImageMemoryBindInfo( &out[i], &in[i] );

    return out;
}

void release_VkSparseImageMemoryBindInfo_array( VkSparseImageMemoryBindInfo *out,
        VkSparseImageMemoryBindInfo_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkSparseImageMemoryBindInfo( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkBindSparseInfo_host *convert_VkBindSparseInfo( VkBindSparseInfo_host *out,
        const VkBindSparseInfo *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->sType                  = in->sType;
    out->pNext                  = in->pNext;
    out->waitSemaphoreCount     = in->waitSemaphoreCount;
    out->pWaitSemaphores        = in->pWaitSemaphores; /* length is waitSemaphoreCount */
    out->bufferBindCount        = in->bufferBindCount;
    out->pBufferBinds           = convert_VkSparseBufferMemoryBindInfo_array( in->pBufferBinds, in->bufferBindCount );
    out->imageOpaqueBindCount   = in->imageOpaqueBindCount;
    out->pImageOpaqueBinds      = convert_VkSparseImageOpaqueMemoryBindInfo_array( in->pImageOpaqueBinds, in->imageOpaqueBindCount );
    out->imageBindCount         = in->imageBindCount;
    out->pImageBinds            = convert_VkSparseImageMemoryBindInfo_array( in->pImageBinds, in->imageBindCount );
    out->signalSemaphoreCount   = in->signalSemaphoreCount;
    out->pSignalSemaphores      = in->pSignalSemaphores; /* length is signalSemaphoreCount */

    return out;
}
void release_VkBindSparseInfo( VkBindSparseInfo *out, VkBindSparseInfo_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return;

    release_VkSparseBufferMemoryBindInfo_array( out ? out->pBufferBinds : NULL, in->pBufferBinds, in->bufferBindCount );
    release_VkSparseImageOpaqueMemoryBindInfo_array( out ? out->pImageOpaqueBinds : NULL, in->pImageOpaqueBinds, in->imageOpaqueBindCount );
    release_VkSparseImageMemoryBindInfo_array( out ? out->pImageBinds : NULL, in->pImageBinds, in->imageBindCount );

    if (!out) return;

    out->sType                  = in->sType;
    out->pNext                  = in->pNext;
    out->waitSemaphoreCount     = in->waitSemaphoreCount;
    out->pWaitSemaphores        = in->pWaitSemaphores; /* length is waitSemaphoreCount */
    out->bufferBindCount        = in->bufferBindCount;
    out->imageOpaqueBindCount   = in->imageOpaqueBindCount;
    out->imageBindCount         = in->imageBindCount;
    out->signalSemaphoreCount   = in->signalSemaphoreCount;
    out->pSignalSemaphores      = in->pSignalSemaphores; /* length is signalSemaphoreCount */
}

VkBindSparseInfo_host *convert_VkBindSparseInfo_array( const VkBindSparseInfo *in, int count )
{
    VkBindSparseInfo_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkBindSparseInfo( &out[i], &in[i] );

    return out;
}

void release_VkBindSparseInfo_array( VkBindSparseInfo *out, VkBindSparseInfo_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkBindSparseInfo( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkDescriptorImageInfo_host *convert_VkDescriptorImageInfo( VkDescriptorImageInfo_host *out,
        const VkDescriptorImageInfo *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->sampler        = in->sampler;
    out->imageView      = in->imageView;
    out->imageLayout    = in->imageLayout;

    return out;
}
void release_VkDescriptorImageInfo( VkDescriptorImageInfo *out, VkDescriptorImageInfo_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->sampler        = in->sampler;
    out->imageView      = in->imageView;
    out->imageLayout    = in->imageLayout;
}

VkDescriptorImageInfo_host *convert_VkDescriptorImageInfo_array( const VkDescriptorImageInfo *in,
        int count )
{
    VkDescriptorImageInfo_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkDescriptorImageInfo( &out[i], &in[i] );

    return out;
}

void release_VkDescriptorImageInfo_array( VkDescriptorImageInfo *out,
        VkDescriptorImageInfo_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkDescriptorImageInfo( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkDescriptorBufferInfo_host *convert_VkDescriptorBufferInfo( VkDescriptorBufferInfo_host *out,
        const VkDescriptorBufferInfo *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->buffer     = in->buffer;
    out->offset     = in->offset;
    out->range      = in->range;

    return out;
}
void release_VkDescriptorBufferInfo( VkDescriptorBufferInfo *out, VkDescriptorBufferInfo_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->buffer     = in->buffer;
    out->offset     = in->offset;
    out->range      = in->range;
}

VkDescriptorBufferInfo_host *convert_VkDescriptorBufferInfo_array(
        const VkDescriptorBufferInfo *in, int count )
{
    VkDescriptorBufferInfo_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkDescriptorBufferInfo( &out[i], &in[i] );

    return out;
}

void release_VkDescriptorBufferInfo_array( VkDescriptorBufferInfo *out,
        VkDescriptorBufferInfo_host *in, int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkDescriptorBufferInfo( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkWriteDescriptorSet_host *convert_VkWriteDescriptorSet( VkWriteDescriptorSet_host *out,
        const VkWriteDescriptorSet *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->sType              = in->sType;
    out->pNext              = in->pNext;
    out->dstSet             = in->dstSet;
    out->dstBinding         = in->dstBinding;
    out->dstArrayElement    = in->dstArrayElement;
    out->descriptorCount    = in->descriptorCount;
    out->descriptorType     = in->descriptorType;
    out->pImageInfo         = valid_pImageInfo( in->descriptorType ) ?
                              convert_VkDescriptorImageInfo_array( in->pImageInfo, in->descriptorCount ) :
                              (void *)0xbadc0ded;  /* should be ignored */
    out->pBufferInfo        = valid_pBufferInfo( in->descriptorType ) ?
                              convert_VkDescriptorBufferInfo_array( in->pBufferInfo, in->descriptorCount ) :
                              (void *)0xbadc0ded;  /* should be ignored */
    out->pTexelBufferView   = in->pTexelBufferView; /* length is descriptorCount */

    return out;
}
void release_VkWriteDescriptorSet( VkWriteDescriptorSet *out, VkWriteDescriptorSet_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return;

    release_VkDescriptorImageInfo_array( out ? out->pImageInfo : NULL, in->pImageInfo, in->descriptorCount );
    release_VkDescriptorBufferInfo_array( out ? out->pBufferInfo : NULL, in->pBufferInfo, in->descriptorCount );

    if (!out) return;

    out->sType              = in->sType;
    out->pNext              = in->pNext;
    out->dstSet             = in->dstSet;
    out->dstBinding         = in->dstBinding;
    out->dstArrayElement    = in->dstArrayElement;
    out->descriptorCount    = in->descriptorCount;
    out->descriptorType     = in->descriptorType;
    out->pTexelBufferView   = in->pTexelBufferView; /* length is descriptorCount */
}

VkWriteDescriptorSet_host *convert_VkWriteDescriptorSet_array( const VkWriteDescriptorSet *in,
        int count )
{
    VkWriteDescriptorSet_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkWriteDescriptorSet( &out[i], &in[i] );

    return out;
}

void release_VkWriteDescriptorSet_array( VkWriteDescriptorSet *out, VkWriteDescriptorSet_host *in,
        int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkWriteDescriptorSet( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
VkCopyDescriptorSet_host *convert_VkCopyDescriptorSet( VkCopyDescriptorSet_host *out,
        const VkCopyDescriptorSet *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in) return NULL;

    out->sType              = in->sType;
    out->pNext              = in->pNext;
    out->srcSet             = in->srcSet;
    out->srcBinding         = in->srcBinding;
    out->srcArrayElement    = in->srcArrayElement;
    out->dstSet             = in->dstSet;
    out->dstBinding         = in->dstBinding;
    out->dstArrayElement    = in->dstArrayElement;
    out->descriptorCount    = in->descriptorCount;

    return out;
}
void release_VkCopyDescriptorSet( VkCopyDescriptorSet *out, VkCopyDescriptorSet_host *in )
{
    TRACE( "(%p, %p)\n", out, in );

    if (!in || !out) return;

    out->sType              = in->sType;
    out->pNext              = in->pNext;
    out->srcSet             = in->srcSet;
    out->srcBinding         = in->srcBinding;
    out->srcArrayElement    = in->srcArrayElement;
    out->dstSet             = in->dstSet;
    out->dstBinding         = in->dstBinding;
    out->dstArrayElement    = in->dstArrayElement;
    out->descriptorCount    = in->descriptorCount;
}

VkCopyDescriptorSet_host *convert_VkCopyDescriptorSet_array( const VkCopyDescriptorSet *in,
        int count )
{
    VkCopyDescriptorSet_host *out;
    int i;

    TRACE( "(%p, %d)\n", in, count );

    if (!in) return NULL;

    out = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*out) );
    for (i = 0; i < count; i++)
        convert_VkCopyDescriptorSet( &out[i], &in[i] );

    return out;
}

void release_VkCopyDescriptorSet_array( VkCopyDescriptorSet *out, VkCopyDescriptorSet_host *in,
        int count )
{
    int i;

    TRACE( "(%p, %p, %d)\n", out, in, count );

    if (!in) return;

    for (i = 0; i < count; i++)
        release_VkCopyDescriptorSet( out ? &out[i] : NULL, &in[i] );
    HeapFree( GetProcessHeap(), 0, in );
}
#endif /* defined(USE_STRUCT_CONVERSION) */

static VkResult null_vkAcquireNextImageKHR( VkDevice device, VkSwapchainKHR swapchain,
        uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex )
{
    FIXME( "(%p, %s, %s, %s, %s, %p) not supported\n", device, wine_dbgstr_longlong(swapchain),
            wine_dbgstr_longlong(timeout), wine_dbgstr_longlong(semaphore),
            wine_dbgstr_longlong(fence), pImageIndex );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkAllocateCommandBuffers( VkDevice device,
        const VkCommandBufferAllocateInfo *pAllocateInfo, VkCommandBuffer *pCommandBuffers )
{
    FIXME( "(%p, %p, %p) not supported\n", device, pAllocateInfo, pCommandBuffers );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkAllocateDescriptorSets( VkDevice device,
        const VkDescriptorSetAllocateInfo *pAllocateInfo, VkDescriptorSet *pDescriptorSets )
{
    FIXME( "(%p, %p, %p) not supported\n", device, pAllocateInfo, pDescriptorSets );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkAllocateMemory( VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkDeviceMemory *pMemory )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pAllocateInfo, pAllocator, pMemory );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkBeginCommandBuffer( VkCommandBuffer commandBuffer,
        const VkCommandBufferBeginInfo_host *pBeginInfo )
{
    FIXME( "(%p, %p) not supported\n", commandBuffer, pBeginInfo );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkBindBufferMemory( VkDevice device, VkBuffer buffer, VkDeviceMemory memory,
        VkDeviceSize memoryOffset )
{
    FIXME( "(%p, %s, %s, %s) not supported\n", device, wine_dbgstr_longlong(buffer),
            wine_dbgstr_longlong(memory), wine_dbgstr_longlong(memoryOffset) );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkBindImageMemory( VkDevice device, VkImage image, VkDeviceMemory memory,
        VkDeviceSize memoryOffset )
{
    FIXME( "(%p, %s, %s, %s) not supported\n", device, wine_dbgstr_longlong(image),
            wine_dbgstr_longlong(memory), wine_dbgstr_longlong(memoryOffset) );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static void null_vkCmdBeginQuery( VkCommandBuffer commandBuffer, VkQueryPool queryPool,
        uint32_t query, VkQueryControlFlags flags )
{
    FIXME( "(%p, %s, %u, %u) not supported\n", commandBuffer, wine_dbgstr_longlong(queryPool),
            query, flags );
}

static void null_vkCmdBeginRenderPass( VkCommandBuffer commandBuffer,
        const VkRenderPassBeginInfo *pRenderPassBegin, VkSubpassContents contents )
{
    FIXME( "(%p, %p, %d) not supported\n", commandBuffer, pRenderPassBegin, contents );
}

static void null_vkCmdBindDescriptorSets( VkCommandBuffer commandBuffer,
        VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet,
        uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets,
        uint32_t dynamicOffsetCount, const uint32_t *pDynamicOffsets )
{
    FIXME( "(%p, %d, %s, %u, %u, %p, %u, %p) not supported\n", commandBuffer, pipelineBindPoint,
            wine_dbgstr_longlong(layout), firstSet, descriptorSetCount, pDescriptorSets,
            dynamicOffsetCount, pDynamicOffsets );
}

static void null_vkCmdBindIndexBuffer( VkCommandBuffer commandBuffer, VkBuffer buffer,
        VkDeviceSize offset, VkIndexType indexType )
{
    FIXME( "(%p, %s, %s, %d) not supported\n", commandBuffer, wine_dbgstr_longlong(buffer),
            wine_dbgstr_longlong(offset), indexType );
}

static void null_vkCmdBindPipeline( VkCommandBuffer commandBuffer,
        VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline )
{
    FIXME( "(%p, %d, %s) not supported\n", commandBuffer, pipelineBindPoint,
            wine_dbgstr_longlong(pipeline) );
}

static void null_vkCmdBindVertexBuffers( VkCommandBuffer commandBuffer, uint32_t firstBinding,
        uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets )
{
    FIXME( "(%p, %u, %u, %p, %p) not supported\n", commandBuffer, firstBinding, bindingCount,
            pBuffers, pOffsets );
}

static void null_vkCmdBlitImage( VkCommandBuffer commandBuffer, VkImage srcImage,
        VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout,
        uint32_t regionCount, const VkImageBlit *pRegions, VkFilter filter )
{
    FIXME( "(%p, %s, %d, %s, %d, %u, %p, %d) not supported\n", commandBuffer,
            wine_dbgstr_longlong(srcImage), srcImageLayout, wine_dbgstr_longlong(dstImage),
            dstImageLayout, regionCount, pRegions, filter );
}

static void null_vkCmdClearAttachments( VkCommandBuffer commandBuffer, uint32_t attachmentCount,
        const VkClearAttachment *pAttachments, uint32_t rectCount, const VkClearRect *pRects )
{
    FIXME( "(%p, %u, %p, %u, %p) not supported\n", commandBuffer, attachmentCount, pAttachments,
            rectCount, pRects );
}

static void null_vkCmdClearColorImage( VkCommandBuffer commandBuffer, VkImage image,
        VkImageLayout imageLayout, const VkClearColorValue *pColor, uint32_t rangeCount,
        const VkImageSubresourceRange *pRanges )
{
    FIXME( "(%p, %s, %d, %p, %u, %p) not supported\n", commandBuffer, wine_dbgstr_longlong(image),
            imageLayout, pColor, rangeCount, pRanges );
}

static void null_vkCmdClearDepthStencilImage( VkCommandBuffer commandBuffer, VkImage image,
        VkImageLayout imageLayout, const VkClearDepthStencilValue *pDepthStencil,
        uint32_t rangeCount, const VkImageSubresourceRange *pRanges )
{
    FIXME( "(%p, %s, %d, %p, %u, %p) not supported\n", commandBuffer, wine_dbgstr_longlong(image),
            imageLayout, pDepthStencil, rangeCount, pRanges );
}

static void null_vkCmdCopyBuffer( VkCommandBuffer commandBuffer, VkBuffer srcBuffer,
        VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy *pRegions )
{
    FIXME( "(%p, %s, %s, %u, %p) not supported\n", commandBuffer, wine_dbgstr_longlong(srcBuffer),
            wine_dbgstr_longlong(dstBuffer), regionCount, pRegions );
}

static void null_vkCmdCopyBufferToImage( VkCommandBuffer commandBuffer, VkBuffer srcBuffer,
        VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
        const VkBufferImageCopy *pRegions )
{
    FIXME( "(%p, %s, %s, %d, %u, %p) not supported\n", commandBuffer,
            wine_dbgstr_longlong(srcBuffer), wine_dbgstr_longlong(dstImage), dstImageLayout,
            regionCount, pRegions );
}

static void null_vkCmdCopyImage( VkCommandBuffer commandBuffer, VkImage srcImage,
        VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout,
        uint32_t regionCount, const VkImageCopy *pRegions )
{
    FIXME( "(%p, %s, %d, %s, %d, %u, %p) not supported\n", commandBuffer,
            wine_dbgstr_longlong(srcImage), srcImageLayout, wine_dbgstr_longlong(dstImage),
            dstImageLayout, regionCount, pRegions );
}

static void null_vkCmdCopyImageToBuffer( VkCommandBuffer commandBuffer, VkImage srcImage,
        VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount,
        const VkBufferImageCopy *pRegions )
{
    FIXME( "(%p, %s, %d, %s, %u, %p) not supported\n", commandBuffer,
            wine_dbgstr_longlong(srcImage), srcImageLayout, wine_dbgstr_longlong(dstBuffer),
            regionCount, pRegions );
}

static void null_vkCmdCopyQueryPoolResults( VkCommandBuffer commandBuffer, VkQueryPool queryPool,
        uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset,
        VkDeviceSize stride, VkQueryResultFlags flags )
{
    FIXME( "(%p, %s, %u, %u, %s, %s, %s, %u) not supported\n", commandBuffer,
            wine_dbgstr_longlong(queryPool), firstQuery, queryCount,
            wine_dbgstr_longlong(dstBuffer), wine_dbgstr_longlong(dstOffset),
            wine_dbgstr_longlong(stride), flags );
}

static void null_vkCmdDispatch( VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z )
{
    FIXME( "(%p, %u, %u, %u) not supported\n", commandBuffer, x, y, z );
}

static void null_vkCmdDispatchIndirect( VkCommandBuffer commandBuffer, VkBuffer buffer,
        VkDeviceSize offset )
{
    FIXME( "(%p, %s, %s) not supported\n", commandBuffer, wine_dbgstr_longlong(buffer),
            wine_dbgstr_longlong(offset) );
}

static void null_vkCmdDraw( VkCommandBuffer commandBuffer, uint32_t vertexCount,
        uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance )
{
    FIXME( "(%p, %u, %u, %u, %u) not supported\n", commandBuffer, vertexCount, instanceCount,
            firstVertex, firstInstance );
}

static void null_vkCmdDrawIndexed( VkCommandBuffer commandBuffer, uint32_t indexCount,
        uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance )
{
    FIXME( "(%p, %u, %u, %u, %d, %u) not supported\n", commandBuffer, indexCount, instanceCount,
            firstIndex, vertexOffset, firstInstance );
}

static void null_vkCmdDrawIndexedIndirect( VkCommandBuffer commandBuffer, VkBuffer buffer,
        VkDeviceSize offset, uint32_t drawCount, uint32_t stride )
{
    FIXME( "(%p, %s, %s, %u, %u) not supported\n", commandBuffer, wine_dbgstr_longlong(buffer),
            wine_dbgstr_longlong(offset), drawCount, stride );
}

static void null_vkCmdDrawIndexedIndirectCountAMD( VkCommandBuffer commandBuffer, VkBuffer buffer,
        VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset,
        uint32_t maxDrawCount, uint32_t stride )
{
    FIXME( "(%p, %s, %s, %s, %s, %u, %u) not supported\n", commandBuffer,
            wine_dbgstr_longlong(buffer), wine_dbgstr_longlong(offset),
            wine_dbgstr_longlong(countBuffer), wine_dbgstr_longlong(countBufferOffset),
            maxDrawCount, stride );
}

static void null_vkCmdDrawIndirect( VkCommandBuffer commandBuffer, VkBuffer buffer,
        VkDeviceSize offset, uint32_t drawCount, uint32_t stride )
{
    FIXME( "(%p, %s, %s, %u, %u) not supported\n", commandBuffer, wine_dbgstr_longlong(buffer),
            wine_dbgstr_longlong(offset), drawCount, stride );
}

static void null_vkCmdDrawIndirectCountAMD( VkCommandBuffer commandBuffer, VkBuffer buffer,
        VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset,
        uint32_t maxDrawCount, uint32_t stride )
{
    FIXME( "(%p, %s, %s, %s, %s, %u, %u) not supported\n", commandBuffer,
            wine_dbgstr_longlong(buffer), wine_dbgstr_longlong(offset),
            wine_dbgstr_longlong(countBuffer), wine_dbgstr_longlong(countBufferOffset),
            maxDrawCount, stride );
}

static void null_vkCmdEndQuery( VkCommandBuffer commandBuffer, VkQueryPool queryPool,
        uint32_t query )
{
    FIXME( "(%p, %s, %u) not supported\n", commandBuffer, wine_dbgstr_longlong(queryPool), query );
}

static void null_vkCmdEndRenderPass( VkCommandBuffer commandBuffer )
{
    FIXME( "(%p) not supported\n", commandBuffer );
}

static void null_vkCmdExecuteCommands( VkCommandBuffer commandBuffer, uint32_t commandBufferCount,
        const VkCommandBuffer *pCommandBuffers )
{
    FIXME( "(%p, %u, %p) not supported\n", commandBuffer, commandBufferCount, pCommandBuffers );
}

static void null_vkCmdFillBuffer( VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
        VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data )
{
    FIXME( "(%p, %s, %s, %s, %u) not supported\n", commandBuffer, wine_dbgstr_longlong(dstBuffer),
            wine_dbgstr_longlong(dstOffset), wine_dbgstr_longlong(size), data );
}

static void null_vkCmdNextSubpass( VkCommandBuffer commandBuffer, VkSubpassContents contents )
{
    FIXME( "(%p, %d) not supported\n", commandBuffer, contents );
}

static void null_vkCmdPipelineBarrier( VkCommandBuffer commandBuffer,
        VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
        VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount,
        const VkMemoryBarrier *pMemoryBarriers, uint32_t bufferMemoryBarrierCount,
        const VkBufferMemoryBarrier *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount,
        const VkImageMemoryBarrier_host *pImageMemoryBarriers )
{
    FIXME( "(%p, %u, %u, %u, %u, %p, %u, %p, %u, %p) not supported\n", commandBuffer, srcStageMask,
            dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers,
            bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount,
            pImageMemoryBarriers );
}

static void null_vkCmdPushConstants( VkCommandBuffer commandBuffer, VkPipelineLayout layout,
        VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void *pValues )
{
    FIXME( "(%p, %s, %u, %u, %u, %p) not supported\n", commandBuffer, wine_dbgstr_longlong(layout),
            stageFlags, offset, size, pValues );
}

static void null_vkCmdResetEvent( VkCommandBuffer commandBuffer, VkEvent event,
        VkPipelineStageFlags stageMask )
{
    FIXME( "(%p, %s, %u) not supported\n", commandBuffer, wine_dbgstr_longlong(event), stageMask );
}

static void null_vkCmdResetQueryPool( VkCommandBuffer commandBuffer, VkQueryPool queryPool,
        uint32_t firstQuery, uint32_t queryCount )
{
    FIXME( "(%p, %s, %u, %u) not supported\n", commandBuffer, wine_dbgstr_longlong(queryPool),
            firstQuery, queryCount );
}

static void null_vkCmdResolveImage( VkCommandBuffer commandBuffer, VkImage srcImage,
        VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout,
        uint32_t regionCount, const VkImageResolve *pRegions )
{
    FIXME( "(%p, %s, %d, %s, %d, %u, %p) not supported\n", commandBuffer,
            wine_dbgstr_longlong(srcImage), srcImageLayout, wine_dbgstr_longlong(dstImage),
            dstImageLayout, regionCount, pRegions );
}

static void null_vkCmdSetBlendConstants( VkCommandBuffer commandBuffer,
        const float blendConstants[4] )
{
    FIXME( "(%p, %p) not supported\n", commandBuffer, blendConstants );
}

static void null_vkCmdSetDepthBias( VkCommandBuffer commandBuffer, float depthBiasConstantFactor,
        float depthBiasClamp, float depthBiasSlopeFactor )
{
    FIXME( "(%p, %f, %f, %f) not supported\n", commandBuffer, depthBiasConstantFactor,
            depthBiasClamp, depthBiasSlopeFactor );
}

static void null_vkCmdSetDepthBounds( VkCommandBuffer commandBuffer, float minDepthBounds,
        float maxDepthBounds )
{
    FIXME( "(%p, %f, %f) not supported\n", commandBuffer, minDepthBounds, maxDepthBounds );
}

static void null_vkCmdSetEvent( VkCommandBuffer commandBuffer, VkEvent event,
        VkPipelineStageFlags stageMask )
{
    FIXME( "(%p, %s, %u) not supported\n", commandBuffer, wine_dbgstr_longlong(event), stageMask );
}

static void null_vkCmdSetLineWidth( VkCommandBuffer commandBuffer, float lineWidth )
{
    FIXME( "(%p, %f) not supported\n", commandBuffer, lineWidth );
}

static void null_vkCmdSetScissor( VkCommandBuffer commandBuffer, uint32_t firstScissor,
        uint32_t scissorCount, const VkRect2D *pScissors )
{
    FIXME( "(%p, %u, %u, %p) not supported\n", commandBuffer, firstScissor, scissorCount,
            pScissors );
}

static void null_vkCmdSetStencilCompareMask( VkCommandBuffer commandBuffer,
        VkStencilFaceFlags faceMask, uint32_t compareMask )
{
    FIXME( "(%p, %u, %u) not supported\n", commandBuffer, faceMask, compareMask );
}

static void null_vkCmdSetStencilReference( VkCommandBuffer commandBuffer,
        VkStencilFaceFlags faceMask, uint32_t reference )
{
    FIXME( "(%p, %u, %u) not supported\n", commandBuffer, faceMask, reference );
}

static void null_vkCmdSetStencilWriteMask( VkCommandBuffer commandBuffer,
        VkStencilFaceFlags faceMask, uint32_t writeMask )
{
    FIXME( "(%p, %u, %u) not supported\n", commandBuffer, faceMask, writeMask );
}

static void null_vkCmdSetViewport( VkCommandBuffer commandBuffer, uint32_t firstViewport,
        uint32_t viewportCount, const VkViewport *pViewports )
{
    FIXME( "(%p, %u, %u, %p) not supported\n", commandBuffer, firstViewport, viewportCount,
            pViewports );
}

static void null_vkCmdUpdateBuffer( VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
        VkDeviceSize dstOffset, VkDeviceSize dataSize, const void *pData )
{
    FIXME( "(%p, %s, %s, %s, %p) not supported\n", commandBuffer, wine_dbgstr_longlong(dstBuffer),
            wine_dbgstr_longlong(dstOffset), wine_dbgstr_longlong(dataSize), pData );
}

static void null_vkCmdWaitEvents( VkCommandBuffer commandBuffer, uint32_t eventCount,
        const VkEvent *pEvents, VkPipelineStageFlags srcStageMask,
        VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount,
        const VkMemoryBarrier *pMemoryBarriers, uint32_t bufferMemoryBarrierCount,
        const VkBufferMemoryBarrier *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount,
        const VkImageMemoryBarrier_host *pImageMemoryBarriers )
{
    FIXME( "(%p, %u, %p, %u, %u, %u, %p, %u, %p, %u, %p) not supported\n", commandBuffer,
            eventCount, pEvents, srcStageMask, dstStageMask, memoryBarrierCount, pMemoryBarriers,
            bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount,
            pImageMemoryBarriers );
}

static void null_vkCmdWriteTimestamp( VkCommandBuffer commandBuffer,
        VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query )
{
    FIXME( "(%p, %d, %s, %u) not supported\n", commandBuffer, pipelineStage,
            wine_dbgstr_longlong(queryPool), query );
}

static VkResult null_vkCreateBuffer( VkDevice device, const VkBufferCreateInfo_host *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkBuffer *pBuffer )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pCreateInfo, pAllocator, pBuffer );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateBufferView( VkDevice device,
        const VkBufferViewCreateInfo_host *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkBufferView *pView )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pCreateInfo, pAllocator, pView );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateCommandPool( VkDevice device,
        const VkCommandPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks_host *pAllocator,
        VkCommandPool *pCommandPool )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pCreateInfo, pAllocator, pCommandPool );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateComputePipelines( VkDevice device, VkPipelineCache pipelineCache,
        uint32_t createInfoCount, const VkComputePipelineCreateInfo_host *pCreateInfos,
        const VkAllocationCallbacks_host *pAllocator, VkPipeline *pPipelines )
{
    FIXME( "(%p, %s, %u, %p, %p, %p) not supported\n", device, wine_dbgstr_longlong(pipelineCache),
            createInfoCount, pCreateInfos, pAllocator, pPipelines );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateDebugReportCallbackEXT( VkInstance instance,
        const VkDebugReportCallbackCreateInfoEXT_host *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkDebugReportCallbackEXT *pCallback )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", instance, pCreateInfo, pAllocator, pCallback );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateDescriptorPool( VkDevice device,
        const VkDescriptorPoolCreateInfo *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkDescriptorPool *pDescriptorPool )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pCreateInfo, pAllocator, pDescriptorPool );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateDescriptorSetLayout( VkDevice device,
        const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkDescriptorSetLayout *pSetLayout )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pCreateInfo, pAllocator, pSetLayout );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateDevice( VkPhysicalDevice physicalDevice,
        const VkDeviceCreateInfo *pCreateInfo, const VkAllocationCallbacks_host *pAllocator,
        VkDevice *pDevice )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", physicalDevice, pCreateInfo, pAllocator, pDevice );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateDisplayModeKHR( VkPhysicalDevice physicalDevice, VkDisplayKHR display,
        const VkDisplayModeCreateInfoKHR *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkDisplayModeKHR *pMode )
{
    FIXME( "(%p, %s, %p, %p, %p) not supported\n", physicalDevice, wine_dbgstr_longlong(display),
            pCreateInfo, pAllocator, pMode );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateDisplayPlaneSurfaceKHR( VkInstance instance,
        const VkDisplaySurfaceCreateInfoKHR_host *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkSurfaceKHR *pSurface )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", instance, pCreateInfo, pAllocator, pSurface );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateEvent( VkDevice device, const VkEventCreateInfo *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkEvent *pEvent )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pCreateInfo, pAllocator, pEvent );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateFence( VkDevice device, const VkFenceCreateInfo *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkFence *pFence )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pCreateInfo, pAllocator, pFence );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateFramebuffer( VkDevice device,
        const VkFramebufferCreateInfo_host *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkFramebuffer *pFramebuffer )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pCreateInfo, pAllocator, pFramebuffer );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateGraphicsPipelines( VkDevice device, VkPipelineCache pipelineCache,
        uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo_host *pCreateInfos,
        const VkAllocationCallbacks_host *pAllocator, VkPipeline *pPipelines )
{
    FIXME( "(%p, %s, %u, %p, %p, %p) not supported\n", device, wine_dbgstr_longlong(pipelineCache),
            createInfoCount, pCreateInfos, pAllocator, pPipelines );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateImage( VkDevice device, const VkImageCreateInfo *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkImage *pImage )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pCreateInfo, pAllocator, pImage );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateImageView( VkDevice device,
        const VkImageViewCreateInfo_host *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkImageView *pView )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pCreateInfo, pAllocator, pView );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateInstance( const VkInstanceCreateInfo *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkInstance *pInstance )
{
    FIXME( "(%p, %p, %p) not supported\n", pCreateInfo, pAllocator, pInstance );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreatePipelineCache( VkDevice device,
        const VkPipelineCacheCreateInfo *pCreateInfo, const VkAllocationCallbacks_host *pAllocator,
        VkPipelineCache *pPipelineCache )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pCreateInfo, pAllocator, pPipelineCache );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreatePipelineLayout( VkDevice device,
        const VkPipelineLayoutCreateInfo *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkPipelineLayout *pPipelineLayout )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pCreateInfo, pAllocator, pPipelineLayout );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateQueryPool( VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkQueryPool *pQueryPool )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pCreateInfo, pAllocator, pQueryPool );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateRenderPass( VkDevice device,
        const VkRenderPassCreateInfo *pCreateInfo, const VkAllocationCallbacks_host *pAllocator,
        VkRenderPass *pRenderPass )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pCreateInfo, pAllocator, pRenderPass );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateSampler( VkDevice device, const VkSamplerCreateInfo *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkSampler *pSampler )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pCreateInfo, pAllocator, pSampler );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateSemaphore( VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkSemaphore *pSemaphore )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pCreateInfo, pAllocator, pSemaphore );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateShaderModule( VkDevice device,
        const VkShaderModuleCreateInfo *pCreateInfo, const VkAllocationCallbacks_host *pAllocator,
        VkShaderModule *pShaderModule )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pCreateInfo, pAllocator, pShaderModule );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateSharedSwapchainsKHR( VkDevice device, uint32_t swapchainCount,
        const VkSwapchainCreateInfoKHR_host *pCreateInfos,
        const VkAllocationCallbacks_host *pAllocator, VkSwapchainKHR *pSwapchains )
{
    FIXME( "(%p, %u, %p, %p, %p) not supported\n", device, swapchainCount, pCreateInfos,
            pAllocator, pSwapchains );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateSwapchainKHR( VkDevice device,
        const VkSwapchainCreateInfoKHR_host *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkSwapchainKHR *pSwapchain )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", device, pCreateInfo, pAllocator, pSwapchain );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateXcbSurfaceKHR( VkInstance instance,
        const VkXcbSurfaceCreateInfoKHR_host *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkSurfaceKHR *pSurface )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", instance, pCreateInfo, pAllocator, pSurface );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkCreateXlibSurfaceKHR( VkInstance instance,
        const VkXlibSurfaceCreateInfoKHR_host *pCreateInfo,
        const VkAllocationCallbacks_host *pAllocator, VkSurfaceKHR *pSurface )
{
    FIXME( "(%p, %p, %p, %p) not supported\n", instance, pCreateInfo, pAllocator, pSurface );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static void null_vkDebugReportMessageEXT( VkInstance instance, VkDebugReportFlagsEXT flags,
        VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location,
        int32_t messageCode, const char *pLayerPrefix, const char *pMessage )
{
    FIXME( "(%p, %u, %d, %s, %lu, %d, %s, %s) not supported\n", instance, flags, objectType,
            wine_dbgstr_longlong(object), (SIZE_T)location, messageCode, debugstr_a(pLayerPrefix),
            debugstr_a(pMessage) );
}

static void null_vkDestroyBuffer( VkDevice device, VkBuffer buffer,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(buffer), pAllocator );
}

static void null_vkDestroyBufferView( VkDevice device, VkBufferView bufferView,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(bufferView), pAllocator );
}

static void null_vkDestroyCommandPool( VkDevice device, VkCommandPool commandPool,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(commandPool), pAllocator );
}

static void null_vkDestroyDebugReportCallbackEXT( VkInstance instance,
        VkDebugReportCallbackEXT callback, const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", instance, wine_dbgstr_longlong(callback), pAllocator );
}

static void null_vkDestroyDescriptorPool( VkDevice device, VkDescriptorPool descriptorPool,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(descriptorPool),
            pAllocator );
}

static void null_vkDestroyDescriptorSetLayout( VkDevice device,
        VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(descriptorSetLayout),
            pAllocator );
}

static void null_vkDestroyDevice( VkDevice device, const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %p) not supported\n", device, pAllocator );
}

static void null_vkDestroyEvent( VkDevice device, VkEvent event,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(event), pAllocator );
}

static void null_vkDestroyFence( VkDevice device, VkFence fence,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(fence), pAllocator );
}

static void null_vkDestroyFramebuffer( VkDevice device, VkFramebuffer framebuffer,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(framebuffer), pAllocator );
}

static void null_vkDestroyImage( VkDevice device, VkImage image,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(image), pAllocator );
}

static void null_vkDestroyImageView( VkDevice device, VkImageView imageView,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(imageView), pAllocator );
}

static void null_vkDestroyInstance( VkInstance instance,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %p) not supported\n", instance, pAllocator );
}

static void null_vkDestroyPipeline( VkDevice device, VkPipeline pipeline,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(pipeline), pAllocator );
}

static void null_vkDestroyPipelineCache( VkDevice device, VkPipelineCache pipelineCache,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(pipelineCache),
            pAllocator );
}

static void null_vkDestroyPipelineLayout( VkDevice device, VkPipelineLayout pipelineLayout,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(pipelineLayout),
            pAllocator );
}

static void null_vkDestroyQueryPool( VkDevice device, VkQueryPool queryPool,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(queryPool), pAllocator );
}

static void null_vkDestroyRenderPass( VkDevice device, VkRenderPass renderPass,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(renderPass), pAllocator );
}

static void null_vkDestroySampler( VkDevice device, VkSampler sampler,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(sampler), pAllocator );
}

static void null_vkDestroySemaphore( VkDevice device, VkSemaphore semaphore,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(semaphore), pAllocator );
}

static void null_vkDestroyShaderModule( VkDevice device, VkShaderModule shaderModule,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(shaderModule), pAllocator );
}

static void null_vkDestroySurfaceKHR( VkInstance instance, VkSurfaceKHR surface,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", instance, wine_dbgstr_longlong(surface), pAllocator );
}

static void null_vkDestroySwapchainKHR( VkDevice device, VkSwapchainKHR swapchain,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(swapchain), pAllocator );
}

static VkResult null_vkDeviceWaitIdle( VkDevice device )
{
    FIXME( "(%p) not supported\n", device );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkEndCommandBuffer( VkCommandBuffer commandBuffer )
{
    FIXME( "(%p) not supported\n", commandBuffer );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkEnumerateDeviceExtensionProperties( VkPhysicalDevice physicalDevice,
        const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties )
{
    FIXME( "(%p, %s, %p, %p) not supported\n", physicalDevice, debugstr_a(pLayerName),
            pPropertyCount, pProperties );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkEnumerateDeviceLayerProperties( VkPhysicalDevice physicalDevice,
        uint32_t *pPropertyCount, VkLayerProperties *pProperties )
{
    FIXME( "(%p, %p, %p) not supported\n", physicalDevice, pPropertyCount, pProperties );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkEnumerateInstanceExtensionProperties( const char *pLayerName,
        uint32_t *pPropertyCount, VkExtensionProperties *pProperties )
{
    FIXME( "(%s, %p, %p) not supported\n", debugstr_a(pLayerName), pPropertyCount, pProperties );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkEnumerateInstanceLayerProperties( uint32_t *pPropertyCount,
        VkLayerProperties *pProperties )
{
    FIXME( "(%p, %p) not supported\n", pPropertyCount, pProperties );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkEnumeratePhysicalDevices( VkInstance instance,
        uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices )
{
    FIXME( "(%p, %p, %p) not supported\n", instance, pPhysicalDeviceCount, pPhysicalDevices );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkFlushMappedMemoryRanges( VkDevice device, uint32_t memoryRangeCount,
        const VkMappedMemoryRange *pMemoryRanges )
{
    FIXME( "(%p, %u, %p) not supported\n", device, memoryRangeCount, pMemoryRanges );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static void null_vkFreeCommandBuffers( VkDevice device, VkCommandPool commandPool,
        uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers )
{
    FIXME( "(%p, %s, %u, %p) not supported\n", device, wine_dbgstr_longlong(commandPool),
            commandBufferCount, pCommandBuffers );
}

static VkResult null_vkFreeDescriptorSets( VkDevice device, VkDescriptorPool descriptorPool,
        uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets )
{
    FIXME( "(%p, %s, %u, %p) not supported\n", device, wine_dbgstr_longlong(descriptorPool),
            descriptorSetCount, pDescriptorSets );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static void null_vkFreeMemory( VkDevice device, VkDeviceMemory memory,
        const VkAllocationCallbacks_host *pAllocator )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(memory), pAllocator );
}

static void null_vkGetBufferMemoryRequirements( VkDevice device, VkBuffer buffer,
        VkMemoryRequirements *pMemoryRequirements )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(buffer),
            pMemoryRequirements );
}

static void null_vkGetDeviceMemoryCommitment( VkDevice device, VkDeviceMemory memory,
        VkDeviceSize *pCommittedMemoryInBytes )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(memory),
            pCommittedMemoryInBytes );
}

static PFN_vkVoidFunction_host null_vkGetDeviceProcAddr( VkDevice device, const char *pName )
{
    FIXME( "(%p, %s) not supported\n", device, debugstr_a(pName) );
    return NULL;
}

static void null_vkGetDeviceQueue( VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex,
        VkQueue *pQueue )
{
    FIXME( "(%p, %u, %u, %p) not supported\n", device, queueFamilyIndex, queueIndex, pQueue );
}

static VkResult null_vkGetDisplayModePropertiesKHR( VkPhysicalDevice physicalDevice,
        VkDisplayKHR display, uint32_t *pPropertyCount,
        VkDisplayModePropertiesKHR_host *pProperties )
{
    FIXME( "(%p, %s, %p, %p) not supported\n", physicalDevice, wine_dbgstr_longlong(display),
            pPropertyCount, pProperties );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkGetDisplayPlaneCapabilitiesKHR( VkPhysicalDevice physicalDevice,
        VkDisplayModeKHR mode, uint32_t planeIndex, VkDisplayPlaneCapabilitiesKHR *pCapabilities )
{
    FIXME( "(%p, %s, %u, %p) not supported\n", physicalDevice, wine_dbgstr_longlong(mode),
            planeIndex, pCapabilities );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkGetDisplayPlaneSupportedDisplaysKHR( VkPhysicalDevice physicalDevice,
        uint32_t planeIndex, uint32_t *pDisplayCount, VkDisplayKHR *pDisplays )
{
    FIXME( "(%p, %u, %p, %p) not supported\n", physicalDevice, planeIndex, pDisplayCount,
            pDisplays );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkGetEventStatus( VkDevice device, VkEvent event )
{
    FIXME( "(%p, %s) not supported\n", device, wine_dbgstr_longlong(event) );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkGetFenceStatus( VkDevice device, VkFence fence )
{
    FIXME( "(%p, %s) not supported\n", device, wine_dbgstr_longlong(fence) );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static void null_vkGetImageMemoryRequirements( VkDevice device, VkImage image,
        VkMemoryRequirements *pMemoryRequirements )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(image),
            pMemoryRequirements );
}

static void null_vkGetImageSparseMemoryRequirements( VkDevice device, VkImage image,
        uint32_t *pSparseMemoryRequirementCount,
        VkSparseImageMemoryRequirements *pSparseMemoryRequirements )
{
    FIXME( "(%p, %s, %p, %p) not supported\n", device, wine_dbgstr_longlong(image),
            pSparseMemoryRequirementCount, pSparseMemoryRequirements );
}

static void null_vkGetImageSubresourceLayout( VkDevice device, VkImage image,
        const VkImageSubresource *pSubresource, VkSubresourceLayout *pLayout )
{
    FIXME( "(%p, %s, %p, %p) not supported\n", device, wine_dbgstr_longlong(image), pSubresource,
            pLayout );
}

static PFN_vkVoidFunction_host null_vkGetInstanceProcAddr( VkInstance instance, const char *pName )
{
    FIXME( "(%p, %s) not supported\n", instance, debugstr_a(pName) );
    return NULL;
}

static VkResult null_vkGetPhysicalDeviceDisplayPlanePropertiesKHR( VkPhysicalDevice physicalDevice,
        uint32_t *pPropertyCount, VkDisplayPlanePropertiesKHR_host *pProperties )
{
    FIXME( "(%p, %p, %p) not supported\n", physicalDevice, pPropertyCount, pProperties );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkGetPhysicalDeviceDisplayPropertiesKHR( VkPhysicalDevice physicalDevice,
        uint32_t *pPropertyCount, VkDisplayPropertiesKHR *pProperties )
{
    FIXME( "(%p, %p, %p) not supported\n", physicalDevice, pPropertyCount, pProperties );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkGetPhysicalDeviceExternalImageFormatPropertiesNV(
        VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling,
        VkImageUsageFlags usage, VkImageCreateFlags flags,
        VkExternalMemoryHandleTypeFlagsNV externalHandleType,
        VkExternalImageFormatPropertiesNV_host *pExternalImageFormatProperties )
{
    FIXME( "(%p, %d, %d, %d, %u, %u, %u, %p) not supported\n", physicalDevice, format, type,
            tiling, usage, flags, externalHandleType, pExternalImageFormatProperties );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static void null_vkGetPhysicalDeviceFeatures( VkPhysicalDevice physicalDevice,
        VkPhysicalDeviceFeatures *pFeatures )
{
    FIXME( "(%p, %p) not supported\n", physicalDevice, pFeatures );
}

static void null_vkGetPhysicalDeviceFormatProperties( VkPhysicalDevice physicalDevice,
        VkFormat format, VkFormatProperties *pFormatProperties )
{
    FIXME( "(%p, %d, %p) not supported\n", physicalDevice, format, pFormatProperties );
}

static VkResult null_vkGetPhysicalDeviceImageFormatProperties( VkPhysicalDevice physicalDevice,
        VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage,
        VkImageCreateFlags flags, VkImageFormatProperties *pImageFormatProperties )
{
    FIXME( "(%p, %d, %d, %d, %u, %u, %p) not supported\n", physicalDevice, format, type, tiling,
            usage, flags, pImageFormatProperties );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static void null_vkGetPhysicalDeviceMemoryProperties( VkPhysicalDevice physicalDevice,
        VkPhysicalDeviceMemoryProperties_host *pMemoryProperties )
{
    FIXME( "(%p, %p) not supported\n", physicalDevice, pMemoryProperties );
}

static void null_vkGetPhysicalDeviceProperties( VkPhysicalDevice physicalDevice,
        VkPhysicalDeviceProperties_host *pProperties )
{
    FIXME( "(%p, %p) not supported\n", physicalDevice, pProperties );
}

static void null_vkGetPhysicalDeviceQueueFamilyProperties( VkPhysicalDevice physicalDevice,
        uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties *pQueueFamilyProperties )
{
    FIXME( "(%p, %p, %p) not supported\n", physicalDevice, pQueueFamilyPropertyCount,
            pQueueFamilyProperties );
}

static void null_vkGetPhysicalDeviceSparseImageFormatProperties( VkPhysicalDevice physicalDevice,
        VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage,
        VkImageTiling tiling, uint32_t *pPropertyCount, VkSparseImageFormatProperties *pProperties )
{
    FIXME( "(%p, %d, %d, %d, %u, %d, %p, %p) not supported\n", physicalDevice, format, type,
            samples, usage, tiling, pPropertyCount, pProperties );
}

static VkResult null_vkGetPhysicalDeviceSurfaceCapabilitiesKHR( VkPhysicalDevice physicalDevice,
        VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *pSurfaceCapabilities )
{
    FIXME( "(%p, %s, %p) not supported\n", physicalDevice, wine_dbgstr_longlong(surface),
            pSurfaceCapabilities );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkGetPhysicalDeviceSurfaceFormatsKHR( VkPhysicalDevice physicalDevice,
        VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount, VkSurfaceFormatKHR *pSurfaceFormats )
{
    FIXME( "(%p, %s, %p, %p) not supported\n", physicalDevice, wine_dbgstr_longlong(surface),
            pSurfaceFormatCount, pSurfaceFormats );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkGetPhysicalDeviceSurfacePresentModesKHR( VkPhysicalDevice physicalDevice,
        VkSurfaceKHR surface, uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes )
{
    FIXME( "(%p, %s, %p, %p) not supported\n", physicalDevice, wine_dbgstr_longlong(surface),
            pPresentModeCount, pPresentModes );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkGetPhysicalDeviceSurfaceSupportKHR( VkPhysicalDevice physicalDevice,
        uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32 *pSupported )
{
    FIXME( "(%p, %u, %s, %p) not supported\n", physicalDevice, queueFamilyIndex,
            wine_dbgstr_longlong(surface), pSupported );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkBool32 null_vkGetPhysicalDeviceXcbPresentationSupportKHR( VkPhysicalDevice physicalDevice,
        uint32_t queueFamilyIndex, xcb_connection_t *connection, xcb_visualid_t visual_id )
{
    FIXME( "(%p, %u, %p, %u) not supported\n", physicalDevice, queueFamilyIndex, connection,
            visual_id );
    return VK_FALSE;
}

static VkBool32 null_vkGetPhysicalDeviceXlibPresentationSupportKHR(
        VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, Display *dpy,
        VisualID visualID )
{
    FIXME( "(%p, %u, %p, %u) not supported\n", physicalDevice, queueFamilyIndex, dpy,
            (uint32_t)visualID );
    return VK_FALSE;
}

static VkResult null_vkGetPipelineCacheData( VkDevice device, VkPipelineCache pipelineCache,
        size_t *pDataSize, void *pData )
{
    FIXME( "(%p, %s, %p, %p) not supported\n", device, wine_dbgstr_longlong(pipelineCache),
            pDataSize, pData );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkGetQueryPoolResults( VkDevice device, VkQueryPool queryPool,
        uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void *pData,
        VkDeviceSize stride, VkQueryResultFlags flags )
{
    FIXME( "(%p, %s, %u, %u, %lu, %p, %s, %u) not supported\n", device,
            wine_dbgstr_longlong(queryPool), firstQuery, queryCount, (SIZE_T)dataSize, pData,
            wine_dbgstr_longlong(stride), flags );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static void null_vkGetRenderAreaGranularity( VkDevice device, VkRenderPass renderPass,
        VkExtent2D *pGranularity )
{
    FIXME( "(%p, %s, %p) not supported\n", device, wine_dbgstr_longlong(renderPass), pGranularity );
}

static VkResult null_vkGetSwapchainImagesKHR( VkDevice device, VkSwapchainKHR swapchain,
        uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages )
{
    FIXME( "(%p, %s, %p, %p) not supported\n", device, wine_dbgstr_longlong(swapchain),
            pSwapchainImageCount, pSwapchainImages );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkInvalidateMappedMemoryRanges( VkDevice device, uint32_t memoryRangeCount,
        const VkMappedMemoryRange *pMemoryRanges )
{
    FIXME( "(%p, %u, %p) not supported\n", device, memoryRangeCount, pMemoryRanges );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkMapMemory( VkDevice device, VkDeviceMemory memory, VkDeviceSize offset,
        VkDeviceSize size, VkMemoryMapFlags flags, void **ppData )
{
    FIXME( "(%p, %s, %s, %s, %u, %p) not supported\n", device, wine_dbgstr_longlong(memory),
            wine_dbgstr_longlong(offset), wine_dbgstr_longlong(size), flags, ppData );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkMergePipelineCaches( VkDevice device, VkPipelineCache dstCache,
        uint32_t srcCacheCount, const VkPipelineCache *pSrcCaches )
{
    FIXME( "(%p, %s, %u, %p) not supported\n", device, wine_dbgstr_longlong(dstCache),
            srcCacheCount, pSrcCaches );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkQueueBindSparse( VkQueue queue, uint32_t bindInfoCount,
        const VkBindSparseInfo_host *pBindInfo, VkFence fence )
{
    FIXME( "(%p, %u, %p, %s) not supported\n", queue, bindInfoCount, pBindInfo,
            wine_dbgstr_longlong(fence) );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkQueuePresentKHR( VkQueue queue, const VkPresentInfoKHR *pPresentInfo )
{
    FIXME( "(%p, %p) not supported\n", queue, pPresentInfo );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkQueueSubmit( VkQueue queue, uint32_t submitCount,
        const VkSubmitInfo *pSubmits, VkFence fence )
{
    FIXME( "(%p, %u, %p, %s) not supported\n", queue, submitCount, pSubmits,
            wine_dbgstr_longlong(fence) );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkQueueWaitIdle( VkQueue queue )
{
    FIXME( "(%p) not supported\n", queue );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkResetCommandBuffer( VkCommandBuffer commandBuffer,
        VkCommandBufferResetFlags flags )
{
    FIXME( "(%p, %u) not supported\n", commandBuffer, flags );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkResetCommandPool( VkDevice device, VkCommandPool commandPool,
        VkCommandPoolResetFlags flags )
{
    FIXME( "(%p, %s, %u) not supported\n", device, wine_dbgstr_longlong(commandPool), flags );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkResetDescriptorPool( VkDevice device, VkDescriptorPool descriptorPool,
        VkDescriptorPoolResetFlags flags )
{
    FIXME( "(%p, %s, %u) not supported\n", device, wine_dbgstr_longlong(descriptorPool), flags );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkResetEvent( VkDevice device, VkEvent event )
{
    FIXME( "(%p, %s) not supported\n", device, wine_dbgstr_longlong(event) );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkResetFences( VkDevice device, uint32_t fenceCount, const VkFence *pFences )
{
    FIXME( "(%p, %u, %p) not supported\n", device, fenceCount, pFences );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static VkResult null_vkSetEvent( VkDevice device, VkEvent event )
{
    FIXME( "(%p, %s) not supported\n", device, wine_dbgstr_longlong(event) );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static void null_vkUnmapMemory( VkDevice device, VkDeviceMemory memory )
{
    FIXME( "(%p, %s) not supported\n", device, wine_dbgstr_longlong(memory) );
}

static void null_vkUpdateDescriptorSets( VkDevice device, uint32_t descriptorWriteCount,
        const VkWriteDescriptorSet_host *pDescriptorWrites, uint32_t descriptorCopyCount,
        const VkCopyDescriptorSet_host *pDescriptorCopies )
{
    FIXME( "(%p, %u, %p, %u, %p) not supported\n", device, descriptorWriteCount, pDescriptorWrites,
            descriptorCopyCount, pDescriptorCopies );
}

static VkResult null_vkWaitForFences( VkDevice device, uint32_t fenceCount, const VkFence *pFences,
        VkBool32 waitAll, uint64_t timeout )
{
    FIXME( "(%p, %u, %p, %u, %s) not supported\n", device, fenceCount, pFences, waitAll,
            wine_dbgstr_longlong(timeout) );
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

VkResult (*p_vkAcquireNextImageKHR)( VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence,
        uint32_t * ) = null_vkAcquireNextImageKHR;
VkResult (*p_vkAllocateCommandBuffers)( VkDevice, const VkCommandBufferAllocateInfo *,
        VkCommandBuffer * ) = null_vkAllocateCommandBuffers;
VkResult (*p_vkAllocateDescriptorSets)( VkDevice, const VkDescriptorSetAllocateInfo *,
        VkDescriptorSet * ) = null_vkAllocateDescriptorSets;
VkResult (*p_vkAllocateMemory)( VkDevice, const VkMemoryAllocateInfo *,
        const VkAllocationCallbacks_host *, VkDeviceMemory * ) = null_vkAllocateMemory;
VkResult (*p_vkBeginCommandBuffer)( VkCommandBuffer, const VkCommandBufferBeginInfo_host * ) =
        null_vkBeginCommandBuffer;
VkResult (*p_vkBindBufferMemory)( VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize ) =
        null_vkBindBufferMemory;
VkResult (*p_vkBindImageMemory)( VkDevice, VkImage, VkDeviceMemory, VkDeviceSize ) =
        null_vkBindImageMemory;
void (*p_vkCmdBeginQuery)( VkCommandBuffer, VkQueryPool, uint32_t, VkQueryControlFlags ) =
        null_vkCmdBeginQuery;
void (*p_vkCmdBeginRenderPass)( VkCommandBuffer, const VkRenderPassBeginInfo *,
        VkSubpassContents ) = null_vkCmdBeginRenderPass;
void (*p_vkCmdBindDescriptorSets)( VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout,
        uint32_t, uint32_t, const VkDescriptorSet *, uint32_t, const uint32_t * ) =
        null_vkCmdBindDescriptorSets;
void (*p_vkCmdBindIndexBuffer)( VkCommandBuffer, VkBuffer, VkDeviceSize, VkIndexType ) =
        null_vkCmdBindIndexBuffer;
void (*p_vkCmdBindPipeline)( VkCommandBuffer, VkPipelineBindPoint, VkPipeline ) =
        null_vkCmdBindPipeline;
void (*p_vkCmdBindVertexBuffers)( VkCommandBuffer, uint32_t, uint32_t, const VkBuffer *,
        const VkDeviceSize * ) = null_vkCmdBindVertexBuffers;
void (*p_vkCmdBlitImage)( VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout,
        uint32_t, const VkImageBlit *, VkFilter ) = null_vkCmdBlitImage;
void (*p_vkCmdClearAttachments)( VkCommandBuffer, uint32_t, const VkClearAttachment *, uint32_t,
        const VkClearRect * ) = null_vkCmdClearAttachments;
void (*p_vkCmdClearColorImage)( VkCommandBuffer, VkImage, VkImageLayout, const VkClearColorValue *,
        uint32_t, const VkImageSubresourceRange * ) = null_vkCmdClearColorImage;
void (*p_vkCmdClearDepthStencilImage)( VkCommandBuffer, VkImage, VkImageLayout,
        const VkClearDepthStencilValue *, uint32_t, const VkImageSubresourceRange * ) =
        null_vkCmdClearDepthStencilImage;
void (*p_vkCmdCopyBuffer)( VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy * ) =
        null_vkCmdCopyBuffer;
void (*p_vkCmdCopyBufferToImage)( VkCommandBuffer, VkBuffer, VkImage, VkImageLayout, uint32_t,
        const VkBufferImageCopy * ) = null_vkCmdCopyBufferToImage;
void (*p_vkCmdCopyImage)( VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout,
        uint32_t, const VkImageCopy * ) = null_vkCmdCopyImage;
void (*p_vkCmdCopyImageToBuffer)( VkCommandBuffer, VkImage, VkImageLayout, VkBuffer, uint32_t,
        const VkBufferImageCopy * ) = null_vkCmdCopyImageToBuffer;
void (*p_vkCmdCopyQueryPoolResults)( VkCommandBuffer, VkQueryPool, uint32_t, uint32_t, VkBuffer,
        VkDeviceSize, VkDeviceSize, VkQueryResultFlags ) = null_vkCmdCopyQueryPoolResults;
void (*p_vkCmdDispatch)( VkCommandBuffer, uint32_t, uint32_t, uint32_t ) = null_vkCmdDispatch;
void (*p_vkCmdDispatchIndirect)( VkCommandBuffer, VkBuffer, VkDeviceSize ) =
        null_vkCmdDispatchIndirect;
void (*p_vkCmdDraw)( VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t ) = null_vkCmdDraw;
void (*p_vkCmdDrawIndexed)( VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t ) =
        null_vkCmdDrawIndexed;
void (*p_vkCmdDrawIndexedIndirect)( VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t ) =
        null_vkCmdDrawIndexedIndirect;
void (*p_vkCmdDrawIndexedIndirectCountAMD)( VkCommandBuffer, VkBuffer, VkDeviceSize, VkBuffer,
        VkDeviceSize, uint32_t, uint32_t ) = null_vkCmdDrawIndexedIndirectCountAMD;
void (*p_vkCmdDrawIndirect)( VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t ) =
        null_vkCmdDrawIndirect;
void (*p_vkCmdDrawIndirectCountAMD)( VkCommandBuffer, VkBuffer, VkDeviceSize, VkBuffer,
        VkDeviceSize, uint32_t, uint32_t ) = null_vkCmdDrawIndirectCountAMD;
void (*p_vkCmdEndQuery)( VkCommandBuffer, VkQueryPool, uint32_t ) = null_vkCmdEndQuery;
void (*p_vkCmdEndRenderPass)( VkCommandBuffer ) = null_vkCmdEndRenderPass;
void (*p_vkCmdExecuteCommands)( VkCommandBuffer, uint32_t, const VkCommandBuffer * ) =
        null_vkCmdExecuteCommands;
void (*p_vkCmdFillBuffer)( VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, uint32_t ) =
        null_vkCmdFillBuffer;
void (*p_vkCmdNextSubpass)( VkCommandBuffer, VkSubpassContents ) = null_vkCmdNextSubpass;
void (*p_vkCmdPipelineBarrier)( VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags,
        VkDependencyFlags, uint32_t, const VkMemoryBarrier *, uint32_t,
        const VkBufferMemoryBarrier *, uint32_t, const VkImageMemoryBarrier_host * ) =
        null_vkCmdPipelineBarrier;
void (*p_vkCmdPushConstants)( VkCommandBuffer, VkPipelineLayout, VkShaderStageFlags, uint32_t,
        uint32_t, const void * ) = null_vkCmdPushConstants;
void (*p_vkCmdResetEvent)( VkCommandBuffer, VkEvent, VkPipelineStageFlags ) = null_vkCmdResetEvent;
void (*p_vkCmdResetQueryPool)( VkCommandBuffer, VkQueryPool, uint32_t, uint32_t ) =
        null_vkCmdResetQueryPool;
void (*p_vkCmdResolveImage)( VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout,
        uint32_t, const VkImageResolve * ) = null_vkCmdResolveImage;
void (*p_vkCmdSetBlendConstants)( VkCommandBuffer, const float[4] ) = null_vkCmdSetBlendConstants;
void (*p_vkCmdSetDepthBias)( VkCommandBuffer, float, float, float ) = null_vkCmdSetDepthBias;
void (*p_vkCmdSetDepthBounds)( VkCommandBuffer, float, float ) = null_vkCmdSetDepthBounds;
void (*p_vkCmdSetEvent)( VkCommandBuffer, VkEvent, VkPipelineStageFlags ) = null_vkCmdSetEvent;
void (*p_vkCmdSetLineWidth)( VkCommandBuffer, float ) = null_vkCmdSetLineWidth;
void (*p_vkCmdSetScissor)( VkCommandBuffer, uint32_t, uint32_t, const VkRect2D * ) =
        null_vkCmdSetScissor;
void (*p_vkCmdSetStencilCompareMask)( VkCommandBuffer, VkStencilFaceFlags, uint32_t ) =
        null_vkCmdSetStencilCompareMask;
void (*p_vkCmdSetStencilReference)( VkCommandBuffer, VkStencilFaceFlags, uint32_t ) =
        null_vkCmdSetStencilReference;
void (*p_vkCmdSetStencilWriteMask)( VkCommandBuffer, VkStencilFaceFlags, uint32_t ) =
        null_vkCmdSetStencilWriteMask;
void (*p_vkCmdSetViewport)( VkCommandBuffer, uint32_t, uint32_t, const VkViewport * ) =
        null_vkCmdSetViewport;
void (*p_vkCmdUpdateBuffer)( VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize,
        const void * ) = null_vkCmdUpdateBuffer;
void (*p_vkCmdWaitEvents)( VkCommandBuffer, uint32_t, const VkEvent *, VkPipelineStageFlags,
        VkPipelineStageFlags, uint32_t, const VkMemoryBarrier *, uint32_t,
        const VkBufferMemoryBarrier *, uint32_t, const VkImageMemoryBarrier_host * ) =
        null_vkCmdWaitEvents;
void (*p_vkCmdWriteTimestamp)( VkCommandBuffer, VkPipelineStageFlagBits, VkQueryPool, uint32_t ) =
        null_vkCmdWriteTimestamp;
VkResult (*p_vkCreateBuffer)( VkDevice, const VkBufferCreateInfo_host *,
        const VkAllocationCallbacks_host *, VkBuffer * ) = null_vkCreateBuffer;
VkResult (*p_vkCreateBufferView)( VkDevice, const VkBufferViewCreateInfo_host *,
        const VkAllocationCallbacks_host *, VkBufferView * ) = null_vkCreateBufferView;
VkResult (*p_vkCreateCommandPool)( VkDevice, const VkCommandPoolCreateInfo *,
        const VkAllocationCallbacks_host *, VkCommandPool * ) = null_vkCreateCommandPool;
VkResult (*p_vkCreateComputePipelines)( VkDevice, VkPipelineCache, uint32_t,
        const VkComputePipelineCreateInfo_host *, const VkAllocationCallbacks_host *,
        VkPipeline * ) = null_vkCreateComputePipelines;
VkResult (*p_vkCreateDebugReportCallbackEXT)( VkInstance,
        const VkDebugReportCallbackCreateInfoEXT_host *, const VkAllocationCallbacks_host *,
        VkDebugReportCallbackEXT * ) = null_vkCreateDebugReportCallbackEXT;
VkResult (*p_vkCreateDescriptorPool)( VkDevice, const VkDescriptorPoolCreateInfo *,
        const VkAllocationCallbacks_host *, VkDescriptorPool * ) = null_vkCreateDescriptorPool;
VkResult (*p_vkCreateDescriptorSetLayout)( VkDevice, const VkDescriptorSetLayoutCreateInfo *,
        const VkAllocationCallbacks_host *, VkDescriptorSetLayout * ) =
        null_vkCreateDescriptorSetLayout;
VkResult (*p_vkCreateDevice)( VkPhysicalDevice, const VkDeviceCreateInfo *,
        const VkAllocationCallbacks_host *, VkDevice * ) = null_vkCreateDevice;
VkResult (*p_vkCreateDisplayModeKHR)( VkPhysicalDevice, VkDisplayKHR,
        const VkDisplayModeCreateInfoKHR *, const VkAllocationCallbacks_host *,
        VkDisplayModeKHR * ) = null_vkCreateDisplayModeKHR;
VkResult (*p_vkCreateDisplayPlaneSurfaceKHR)( VkInstance,
        const VkDisplaySurfaceCreateInfoKHR_host *, const VkAllocationCallbacks_host *,
        VkSurfaceKHR * ) = null_vkCreateDisplayPlaneSurfaceKHR;
VkResult (*p_vkCreateEvent)( VkDevice, const VkEventCreateInfo *,
        const VkAllocationCallbacks_host *, VkEvent * ) = null_vkCreateEvent;
VkResult (*p_vkCreateFence)( VkDevice, const VkFenceCreateInfo *,
        const VkAllocationCallbacks_host *, VkFence * ) = null_vkCreateFence;
VkResult (*p_vkCreateFramebuffer)( VkDevice, const VkFramebufferCreateInfo_host *,
        const VkAllocationCallbacks_host *, VkFramebuffer * ) = null_vkCreateFramebuffer;
VkResult (*p_vkCreateGraphicsPipelines)( VkDevice, VkPipelineCache, uint32_t,
        const VkGraphicsPipelineCreateInfo_host *, const VkAllocationCallbacks_host *,
        VkPipeline * ) = null_vkCreateGraphicsPipelines;
VkResult (*p_vkCreateImage)( VkDevice, const VkImageCreateInfo *,
        const VkAllocationCallbacks_host *, VkImage * ) = null_vkCreateImage;
VkResult (*p_vkCreateImageView)( VkDevice, const VkImageViewCreateInfo_host *,
        const VkAllocationCallbacks_host *, VkImageView * ) = null_vkCreateImageView;
VkResult (*p_vkCreateInstance)( const VkInstanceCreateInfo *, const VkAllocationCallbacks_host *,
        VkInstance * ) = null_vkCreateInstance;
VkResult (*p_vkCreatePipelineCache)( VkDevice, const VkPipelineCacheCreateInfo *,
        const VkAllocationCallbacks_host *, VkPipelineCache * ) = null_vkCreatePipelineCache;
VkResult (*p_vkCreatePipelineLayout)( VkDevice, const VkPipelineLayoutCreateInfo *,
        const VkAllocationCallbacks_host *, VkPipelineLayout * ) = null_vkCreatePipelineLayout;
VkResult (*p_vkCreateQueryPool)( VkDevice, const VkQueryPoolCreateInfo *,
        const VkAllocationCallbacks_host *, VkQueryPool * ) = null_vkCreateQueryPool;
VkResult (*p_vkCreateRenderPass)( VkDevice, const VkRenderPassCreateInfo *,
        const VkAllocationCallbacks_host *, VkRenderPass * ) = null_vkCreateRenderPass;
VkResult (*p_vkCreateSampler)( VkDevice, const VkSamplerCreateInfo *,
        const VkAllocationCallbacks_host *, VkSampler * ) = null_vkCreateSampler;
VkResult (*p_vkCreateSemaphore)( VkDevice, const VkSemaphoreCreateInfo *,
        const VkAllocationCallbacks_host *, VkSemaphore * ) = null_vkCreateSemaphore;
VkResult (*p_vkCreateShaderModule)( VkDevice, const VkShaderModuleCreateInfo *,
        const VkAllocationCallbacks_host *, VkShaderModule * ) = null_vkCreateShaderModule;
VkResult (*p_vkCreateSharedSwapchainsKHR)( VkDevice, uint32_t,
        const VkSwapchainCreateInfoKHR_host *, const VkAllocationCallbacks_host *,
        VkSwapchainKHR * ) = null_vkCreateSharedSwapchainsKHR;
VkResult (*p_vkCreateSwapchainKHR)( VkDevice, const VkSwapchainCreateInfoKHR_host *,
        const VkAllocationCallbacks_host *, VkSwapchainKHR * ) = null_vkCreateSwapchainKHR;
VkResult (*p_vkCreateXcbSurfaceKHR)( VkInstance, const VkXcbSurfaceCreateInfoKHR_host *,
        const VkAllocationCallbacks_host *, VkSurfaceKHR * ) = null_vkCreateXcbSurfaceKHR;
VkResult (*p_vkCreateXlibSurfaceKHR)( VkInstance, const VkXlibSurfaceCreateInfoKHR_host *,
        const VkAllocationCallbacks_host *, VkSurfaceKHR * ) = null_vkCreateXlibSurfaceKHR;
void (*p_vkDebugReportMessageEXT)( VkInstance, VkDebugReportFlagsEXT, VkDebugReportObjectTypeEXT,
        uint64_t, size_t, int32_t, const char *, const char * ) = null_vkDebugReportMessageEXT;
void (*p_vkDestroyBuffer)( VkDevice, VkBuffer, const VkAllocationCallbacks_host * ) =
        null_vkDestroyBuffer;
void (*p_vkDestroyBufferView)( VkDevice, VkBufferView, const VkAllocationCallbacks_host * ) =
        null_vkDestroyBufferView;
void (*p_vkDestroyCommandPool)( VkDevice, VkCommandPool, const VkAllocationCallbacks_host * ) =
        null_vkDestroyCommandPool;
void (*p_vkDestroyDebugReportCallbackEXT)( VkInstance, VkDebugReportCallbackEXT,
        const VkAllocationCallbacks_host * ) = null_vkDestroyDebugReportCallbackEXT;
void (*p_vkDestroyDescriptorPool)( VkDevice, VkDescriptorPool,
        const VkAllocationCallbacks_host * ) = null_vkDestroyDescriptorPool;
void (*p_vkDestroyDescriptorSetLayout)( VkDevice, VkDescriptorSetLayout,
        const VkAllocationCallbacks_host * ) = null_vkDestroyDescriptorSetLayout;
void (*p_vkDestroyDevice)( VkDevice, const VkAllocationCallbacks_host * ) = null_vkDestroyDevice;
void (*p_vkDestroyEvent)( VkDevice, VkEvent, const VkAllocationCallbacks_host * ) =
        null_vkDestroyEvent;
void (*p_vkDestroyFence)( VkDevice, VkFence, const VkAllocationCallbacks_host * ) =
        null_vkDestroyFence;
void (*p_vkDestroyFramebuffer)( VkDevice, VkFramebuffer, const VkAllocationCallbacks_host * ) =
        null_vkDestroyFramebuffer;
void (*p_vkDestroyImage)( VkDevice, VkImage, const VkAllocationCallbacks_host * ) =
        null_vkDestroyImage;
void (*p_vkDestroyImageView)( VkDevice, VkImageView, const VkAllocationCallbacks_host * ) =
        null_vkDestroyImageView;
void (*p_vkDestroyInstance)( VkInstance, const VkAllocationCallbacks_host * ) =
        null_vkDestroyInstance;
void (*p_vkDestroyPipeline)( VkDevice, VkPipeline, const VkAllocationCallbacks_host * ) =
        null_vkDestroyPipeline;
void (*p_vkDestroyPipelineCache)( VkDevice, VkPipelineCache, const VkAllocationCallbacks_host * ) =
        null_vkDestroyPipelineCache;
void (*p_vkDestroyPipelineLayout)( VkDevice, VkPipelineLayout,
        const VkAllocationCallbacks_host * ) = null_vkDestroyPipelineLayout;
void (*p_vkDestroyQueryPool)( VkDevice, VkQueryPool, const VkAllocationCallbacks_host * ) =
        null_vkDestroyQueryPool;
void (*p_vkDestroyRenderPass)( VkDevice, VkRenderPass, const VkAllocationCallbacks_host * ) =
        null_vkDestroyRenderPass;
void (*p_vkDestroySampler)( VkDevice, VkSampler, const VkAllocationCallbacks_host * ) =
        null_vkDestroySampler;
void (*p_vkDestroySemaphore)( VkDevice, VkSemaphore, const VkAllocationCallbacks_host * ) =
        null_vkDestroySemaphore;
void (*p_vkDestroyShaderModule)( VkDevice, VkShaderModule, const VkAllocationCallbacks_host * ) =
        null_vkDestroyShaderModule;
void (*p_vkDestroySurfaceKHR)( VkInstance, VkSurfaceKHR, const VkAllocationCallbacks_host * ) =
        null_vkDestroySurfaceKHR;
void (*p_vkDestroySwapchainKHR)( VkDevice, VkSwapchainKHR, const VkAllocationCallbacks_host * ) =
        null_vkDestroySwapchainKHR;
VkResult (*p_vkDeviceWaitIdle)( VkDevice ) = null_vkDeviceWaitIdle;
VkResult (*p_vkEndCommandBuffer)( VkCommandBuffer ) = null_vkEndCommandBuffer;
VkResult (*p_vkEnumerateDeviceExtensionProperties)( VkPhysicalDevice, const char *, uint32_t *,
        VkExtensionProperties * ) = null_vkEnumerateDeviceExtensionProperties;
VkResult (*p_vkEnumerateDeviceLayerProperties)( VkPhysicalDevice, uint32_t *,
        VkLayerProperties * ) = null_vkEnumerateDeviceLayerProperties;
VkResult (*p_vkEnumerateInstanceExtensionProperties)( const char *, uint32_t *,
        VkExtensionProperties * ) = null_vkEnumerateInstanceExtensionProperties;
VkResult (*p_vkEnumerateInstanceLayerProperties)( uint32_t *, VkLayerProperties * ) =
        null_vkEnumerateInstanceLayerProperties;
VkResult (*p_vkEnumeratePhysicalDevices)( VkInstance, uint32_t *, VkPhysicalDevice * ) =
        null_vkEnumeratePhysicalDevices;
VkResult (*p_vkFlushMappedMemoryRanges)( VkDevice, uint32_t, const VkMappedMemoryRange * ) =
        null_vkFlushMappedMemoryRanges;
void (*p_vkFreeCommandBuffers)( VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer * ) =
        null_vkFreeCommandBuffers;
VkResult (*p_vkFreeDescriptorSets)( VkDevice, VkDescriptorPool, uint32_t,
        const VkDescriptorSet * ) = null_vkFreeDescriptorSets;
void (*p_vkFreeMemory)( VkDevice, VkDeviceMemory, const VkAllocationCallbacks_host * ) =
        null_vkFreeMemory;
void (*p_vkGetBufferMemoryRequirements)( VkDevice, VkBuffer, VkMemoryRequirements * ) =
        null_vkGetBufferMemoryRequirements;
void (*p_vkGetDeviceMemoryCommitment)( VkDevice, VkDeviceMemory, VkDeviceSize * ) =
        null_vkGetDeviceMemoryCommitment;
PFN_vkVoidFunction_host (*p_vkGetDeviceProcAddr)( VkDevice, const char * ) =
        null_vkGetDeviceProcAddr;
void (*p_vkGetDeviceQueue)( VkDevice, uint32_t, uint32_t, VkQueue * ) = null_vkGetDeviceQueue;
VkResult (*p_vkGetDisplayModePropertiesKHR)( VkPhysicalDevice, VkDisplayKHR, uint32_t *,
        VkDisplayModePropertiesKHR_host * ) = null_vkGetDisplayModePropertiesKHR;
VkResult (*p_vkGetDisplayPlaneCapabilitiesKHR)( VkPhysicalDevice, VkDisplayModeKHR, uint32_t,
        VkDisplayPlaneCapabilitiesKHR * ) = null_vkGetDisplayPlaneCapabilitiesKHR;
VkResult (*p_vkGetDisplayPlaneSupportedDisplaysKHR)( VkPhysicalDevice, uint32_t, uint32_t *,
        VkDisplayKHR * ) = null_vkGetDisplayPlaneSupportedDisplaysKHR;
VkResult (*p_vkGetEventStatus)( VkDevice, VkEvent ) = null_vkGetEventStatus;
VkResult (*p_vkGetFenceStatus)( VkDevice, VkFence ) = null_vkGetFenceStatus;
void (*p_vkGetImageMemoryRequirements)( VkDevice, VkImage, VkMemoryRequirements * ) =
        null_vkGetImageMemoryRequirements;
void (*p_vkGetImageSparseMemoryRequirements)( VkDevice, VkImage, uint32_t *,
        VkSparseImageMemoryRequirements * ) = null_vkGetImageSparseMemoryRequirements;
void (*p_vkGetImageSubresourceLayout)( VkDevice, VkImage, const VkImageSubresource *,
        VkSubresourceLayout * ) = null_vkGetImageSubresourceLayout;
PFN_vkVoidFunction_host (*p_vkGetInstanceProcAddr)( VkInstance, const char * ) =
        null_vkGetInstanceProcAddr;
VkResult (*p_vkGetPhysicalDeviceDisplayPlanePropertiesKHR)( VkPhysicalDevice, uint32_t *,
        VkDisplayPlanePropertiesKHR_host * ) = null_vkGetPhysicalDeviceDisplayPlanePropertiesKHR;
VkResult (*p_vkGetPhysicalDeviceDisplayPropertiesKHR)( VkPhysicalDevice, uint32_t *,
        VkDisplayPropertiesKHR * ) = null_vkGetPhysicalDeviceDisplayPropertiesKHR;
VkResult (*p_vkGetPhysicalDeviceExternalImageFormatPropertiesNV)( VkPhysicalDevice, VkFormat,
        VkImageType, VkImageTiling, VkImageUsageFlags, VkImageCreateFlags,
        VkExternalMemoryHandleTypeFlagsNV, VkExternalImageFormatPropertiesNV_host * ) =
        null_vkGetPhysicalDeviceExternalImageFormatPropertiesNV;
void (*p_vkGetPhysicalDeviceFeatures)( VkPhysicalDevice, VkPhysicalDeviceFeatures * ) =
        null_vkGetPhysicalDeviceFeatures;
void (*p_vkGetPhysicalDeviceFormatProperties)( VkPhysicalDevice, VkFormat, VkFormatProperties * ) =
        null_vkGetPhysicalDeviceFormatProperties;
VkResult (*p_vkGetPhysicalDeviceImageFormatProperties)( VkPhysicalDevice, VkFormat, VkImageType,
        VkImageTiling, VkImageUsageFlags, VkImageCreateFlags, VkImageFormatProperties * ) =
        null_vkGetPhysicalDeviceImageFormatProperties;
void (*p_vkGetPhysicalDeviceMemoryProperties)( VkPhysicalDevice,
        VkPhysicalDeviceMemoryProperties_host * ) = null_vkGetPhysicalDeviceMemoryProperties;
void (*p_vkGetPhysicalDeviceProperties)( VkPhysicalDevice, VkPhysicalDeviceProperties_host * ) =
        null_vkGetPhysicalDeviceProperties;
void (*p_vkGetPhysicalDeviceQueueFamilyProperties)( VkPhysicalDevice, uint32_t *,
        VkQueueFamilyProperties * ) = null_vkGetPhysicalDeviceQueueFamilyProperties;
void (*p_vkGetPhysicalDeviceSparseImageFormatProperties)( VkPhysicalDevice, VkFormat, VkImageType,
        VkSampleCountFlagBits, VkImageUsageFlags, VkImageTiling, uint32_t *,
        VkSparseImageFormatProperties * ) = null_vkGetPhysicalDeviceSparseImageFormatProperties;
VkResult (*p_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)( VkPhysicalDevice, VkSurfaceKHR,
        VkSurfaceCapabilitiesKHR * ) = null_vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
VkResult (*p_vkGetPhysicalDeviceSurfaceFormatsKHR)( VkPhysicalDevice, VkSurfaceKHR, uint32_t *,
        VkSurfaceFormatKHR * ) = null_vkGetPhysicalDeviceSurfaceFormatsKHR;
VkResult (*p_vkGetPhysicalDeviceSurfacePresentModesKHR)( VkPhysicalDevice, VkSurfaceKHR,
        uint32_t *, VkPresentModeKHR * ) = null_vkGetPhysicalDeviceSurfacePresentModesKHR;
VkResult (*p_vkGetPhysicalDeviceSurfaceSupportKHR)( VkPhysicalDevice, uint32_t, VkSurfaceKHR,
        VkBool32 * ) = null_vkGetPhysicalDeviceSurfaceSupportKHR;
VkBool32 (*p_vkGetPhysicalDeviceXcbPresentationSupportKHR)( VkPhysicalDevice, uint32_t,
        xcb_connection_t *, xcb_visualid_t ) = null_vkGetPhysicalDeviceXcbPresentationSupportKHR;
VkBool32 (*p_vkGetPhysicalDeviceXlibPresentationSupportKHR)( VkPhysicalDevice, uint32_t, Display *,
        VisualID ) = null_vkGetPhysicalDeviceXlibPresentationSupportKHR;
VkResult (*p_vkGetPipelineCacheData)( VkDevice, VkPipelineCache, size_t *, void * ) =
        null_vkGetPipelineCacheData;
VkResult (*p_vkGetQueryPoolResults)( VkDevice, VkQueryPool, uint32_t, uint32_t, size_t, void *,
        VkDeviceSize, VkQueryResultFlags ) = null_vkGetQueryPoolResults;
void (*p_vkGetRenderAreaGranularity)( VkDevice, VkRenderPass, VkExtent2D * ) =
        null_vkGetRenderAreaGranularity;
VkResult (*p_vkGetSwapchainImagesKHR)( VkDevice, VkSwapchainKHR, uint32_t *, VkImage * ) =
        null_vkGetSwapchainImagesKHR;
VkResult (*p_vkInvalidateMappedMemoryRanges)( VkDevice, uint32_t, const VkMappedMemoryRange * ) =
        null_vkInvalidateMappedMemoryRanges;
VkResult (*p_vkMapMemory)( VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags,
        void ** ) = null_vkMapMemory;
VkResult (*p_vkMergePipelineCaches)( VkDevice, VkPipelineCache, uint32_t,
        const VkPipelineCache * ) = null_vkMergePipelineCaches;
VkResult (*p_vkQueueBindSparse)( VkQueue, uint32_t, const VkBindSparseInfo_host *, VkFence ) =
        null_vkQueueBindSparse;
VkResult (*p_vkQueuePresentKHR)( VkQueue, const VkPresentInfoKHR * ) = null_vkQueuePresentKHR;
VkResult (*p_vkQueueSubmit)( VkQueue, uint32_t, const VkSubmitInfo *, VkFence ) =
        null_vkQueueSubmit;
VkResult (*p_vkQueueWaitIdle)( VkQueue ) = null_vkQueueWaitIdle;
VkResult (*p_vkResetCommandBuffer)( VkCommandBuffer, VkCommandBufferResetFlags ) =
        null_vkResetCommandBuffer;
VkResult (*p_vkResetCommandPool)( VkDevice, VkCommandPool, VkCommandPoolResetFlags ) =
        null_vkResetCommandPool;
VkResult (*p_vkResetDescriptorPool)( VkDevice, VkDescriptorPool, VkDescriptorPoolResetFlags ) =
        null_vkResetDescriptorPool;
VkResult (*p_vkResetEvent)( VkDevice, VkEvent ) = null_vkResetEvent;
VkResult (*p_vkResetFences)( VkDevice, uint32_t, const VkFence * ) = null_vkResetFences;
VkResult (*p_vkSetEvent)( VkDevice, VkEvent ) = null_vkSetEvent;
void (*p_vkUnmapMemory)( VkDevice, VkDeviceMemory ) = null_vkUnmapMemory;
void (*p_vkUpdateDescriptorSets)( VkDevice, uint32_t, const VkWriteDescriptorSet_host *, uint32_t,
        const VkCopyDescriptorSet_host * ) = null_vkUpdateDescriptorSets;
VkResult (*p_vkWaitForFences)( VkDevice, uint32_t, const VkFence *, VkBool32, uint64_t ) =
        null_vkWaitForFences;

/***********************************************************************
 *              vkAcquireNextImageKHR (VULKAN.@)
 */
VkResult WINAPI vkAcquireNextImageKHR( VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
        VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex )
{
    TRACE( "(%p, %s, %s, %s, %s, %p)\n", device, wine_dbgstr_longlong(swapchain),
            wine_dbgstr_longlong(timeout), wine_dbgstr_longlong(semaphore),
            wine_dbgstr_longlong(fence), pImageIndex );
    return p_vkAcquireNextImageKHR( device, swapchain, timeout, semaphore, fence, pImageIndex );
}

/***********************************************************************
 *              vkAllocateCommandBuffers (VULKAN.@)
 */
VkResult WINAPI vkAllocateCommandBuffers( VkDevice device,
        const VkCommandBufferAllocateInfo *pAllocateInfo, VkCommandBuffer *pCommandBuffers )
{
    TRACE( "(%p, %p, %p)\n", device, pAllocateInfo, pCommandBuffers );
    return p_vkAllocateCommandBuffers( device, pAllocateInfo, pCommandBuffers );
}

/***********************************************************************
 *              vkAllocateDescriptorSets (VULKAN.@)
 */
VkResult WINAPI vkAllocateDescriptorSets( VkDevice device,
        const VkDescriptorSetAllocateInfo *pAllocateInfo, VkDescriptorSet *pDescriptorSets )
{
    TRACE( "(%p, %p, %p)\n", device, pAllocateInfo, pDescriptorSets );
    return p_vkAllocateDescriptorSets( device, pAllocateInfo, pDescriptorSets );
}

/***********************************************************************
 *              vkAllocateMemory (VULKAN.@)
 */
VkResult WINAPI vkAllocateMemory( VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo,
        const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pAllocateInfo, pAllocator, pMemory );

    /* conversion of pAllocateInfo not necessary, offsets match */
    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkAllocateMemory( device, pAllocateInfo, ptr_pAllocator, pMemory );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );

    return res;
}

/***********************************************************************
 *              vkBeginCommandBuffer (VULKAN.@)
 */
VkResult WINAPI vkBeginCommandBuffer( VkCommandBuffer commandBuffer,
        const VkCommandBufferBeginInfo *pBeginInfo )
{
    VkCommandBufferBeginInfo_host tmp_pBeginInfo, *ptr_pBeginInfo;
    VkResult res;

    TRACE( "(%p, %p)\n", commandBuffer, pBeginInfo );

    ptr_pBeginInfo = convert_VkCommandBufferBeginInfo( &tmp_pBeginInfo, pBeginInfo );
    res = p_vkBeginCommandBuffer( commandBuffer, ptr_pBeginInfo );
    release_VkCommandBufferBeginInfo( NULL, ptr_pBeginInfo );

    return res;
}

/***********************************************************************
 *              vkBindBufferMemory (VULKAN.@)
 */
VkResult WINAPI vkBindBufferMemory( VkDevice device, VkBuffer buffer, VkDeviceMemory memory,
        VkDeviceSize memoryOffset )
{
    TRACE( "(%p, %s, %s, %s)\n", device, wine_dbgstr_longlong(buffer),
            wine_dbgstr_longlong(memory), wine_dbgstr_longlong(memoryOffset) );
    return p_vkBindBufferMemory( device, buffer, memory, memoryOffset );
}

/***********************************************************************
 *              vkBindImageMemory (VULKAN.@)
 */
VkResult WINAPI vkBindImageMemory( VkDevice device, VkImage image, VkDeviceMemory memory,
        VkDeviceSize memoryOffset )
{
    TRACE( "(%p, %s, %s, %s)\n", device, wine_dbgstr_longlong(image), wine_dbgstr_longlong(memory),
            wine_dbgstr_longlong(memoryOffset) );
    return p_vkBindImageMemory( device, image, memory, memoryOffset );
}

/***********************************************************************
 *              vkCmdBeginQuery (VULKAN.@)
 */
void WINAPI vkCmdBeginQuery( VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query,
        VkQueryControlFlags flags )
{
    TRACE( "(%p, %s, %u, %u)\n", commandBuffer, wine_dbgstr_longlong(queryPool), query, flags );
    p_vkCmdBeginQuery( commandBuffer, queryPool, query, flags );
}

/***********************************************************************
 *              vkCmdBeginRenderPass (VULKAN.@)
 */
void WINAPI vkCmdBeginRenderPass( VkCommandBuffer commandBuffer,
        const VkRenderPassBeginInfo *pRenderPassBegin, VkSubpassContents contents )
{
    TRACE( "(%p, %p, %d)\n", commandBuffer, pRenderPassBegin, contents );
    p_vkCmdBeginRenderPass( commandBuffer, pRenderPassBegin, contents );
}

/***********************************************************************
 *              vkCmdBindDescriptorSets (VULKAN.@)
 */
void WINAPI vkCmdBindDescriptorSets( VkCommandBuffer commandBuffer,
        VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet,
        uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets,
        uint32_t dynamicOffsetCount, const uint32_t *pDynamicOffsets )
{
    TRACE( "(%p, %d, %s, %u, %u, %p, %u, %p)\n", commandBuffer, pipelineBindPoint,
            wine_dbgstr_longlong(layout), firstSet, descriptorSetCount, pDescriptorSets,
            dynamicOffsetCount, pDynamicOffsets );
    p_vkCmdBindDescriptorSets( commandBuffer, pipelineBindPoint, layout, firstSet,
            descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets );
}

/***********************************************************************
 *              vkCmdBindIndexBuffer (VULKAN.@)
 */
void WINAPI vkCmdBindIndexBuffer( VkCommandBuffer commandBuffer, VkBuffer buffer,
        VkDeviceSize offset, VkIndexType indexType )
{
    TRACE( "(%p, %s, %s, %d)\n", commandBuffer, wine_dbgstr_longlong(buffer),
            wine_dbgstr_longlong(offset), indexType );
    p_vkCmdBindIndexBuffer( commandBuffer, buffer, offset, indexType );
}

/***********************************************************************
 *              vkCmdBindPipeline (VULKAN.@)
 */
void WINAPI vkCmdBindPipeline( VkCommandBuffer commandBuffer,
        VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline )
{
    TRACE( "(%p, %d, %s)\n", commandBuffer, pipelineBindPoint, wine_dbgstr_longlong(pipeline) );
    p_vkCmdBindPipeline( commandBuffer, pipelineBindPoint, pipeline );
}

/***********************************************************************
 *              vkCmdBindVertexBuffers (VULKAN.@)
 */
void WINAPI vkCmdBindVertexBuffers( VkCommandBuffer commandBuffer, uint32_t firstBinding,
        uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets )
{
    TRACE( "(%p, %u, %u, %p, %p)\n", commandBuffer, firstBinding, bindingCount, pBuffers,
            pOffsets );
    p_vkCmdBindVertexBuffers( commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets );
}

/***********************************************************************
 *              vkCmdBlitImage (VULKAN.@)
 */
void WINAPI vkCmdBlitImage( VkCommandBuffer commandBuffer, VkImage srcImage,
        VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout,
        uint32_t regionCount, const VkImageBlit *pRegions, VkFilter filter )
{
    TRACE( "(%p, %s, %d, %s, %d, %u, %p, %d)\n", commandBuffer, wine_dbgstr_longlong(srcImage),
            srcImageLayout, wine_dbgstr_longlong(dstImage), dstImageLayout, regionCount, pRegions,
            filter );
    p_vkCmdBlitImage( commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout,
            regionCount, pRegions, filter );
}

/***********************************************************************
 *              vkCmdClearAttachments (VULKAN.@)
 */
void WINAPI vkCmdClearAttachments( VkCommandBuffer commandBuffer, uint32_t attachmentCount,
        const VkClearAttachment *pAttachments, uint32_t rectCount, const VkClearRect *pRects )
{
    TRACE( "(%p, %u, %p, %u, %p)\n", commandBuffer, attachmentCount, pAttachments, rectCount,
            pRects );
    p_vkCmdClearAttachments( commandBuffer, attachmentCount, pAttachments, rectCount, pRects );
}

/***********************************************************************
 *              vkCmdClearColorImage (VULKAN.@)
 */
void WINAPI vkCmdClearColorImage( VkCommandBuffer commandBuffer, VkImage image,
        VkImageLayout imageLayout, const VkClearColorValue *pColor, uint32_t rangeCount,
        const VkImageSubresourceRange *pRanges )
{
    TRACE( "(%p, %s, %d, %p, %u, %p)\n", commandBuffer, wine_dbgstr_longlong(image), imageLayout,
            pColor, rangeCount, pRanges );
    p_vkCmdClearColorImage( commandBuffer, image, imageLayout, pColor, rangeCount, pRanges );
}

/***********************************************************************
 *              vkCmdClearDepthStencilImage (VULKAN.@)
 */
void WINAPI vkCmdClearDepthStencilImage( VkCommandBuffer commandBuffer, VkImage image,
        VkImageLayout imageLayout, const VkClearDepthStencilValue *pDepthStencil,
        uint32_t rangeCount, const VkImageSubresourceRange *pRanges )
{
    TRACE( "(%p, %s, %d, %p, %u, %p)\n", commandBuffer, wine_dbgstr_longlong(image), imageLayout,
            pDepthStencil, rangeCount, pRanges );
    p_vkCmdClearDepthStencilImage( commandBuffer, image, imageLayout, pDepthStencil, rangeCount,
            pRanges );
}

/***********************************************************************
 *              vkCmdCopyBuffer (VULKAN.@)
 */
void WINAPI vkCmdCopyBuffer( VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer,
        uint32_t regionCount, const VkBufferCopy *pRegions )
{
    TRACE( "(%p, %s, %s, %u, %p)\n", commandBuffer, wine_dbgstr_longlong(srcBuffer),
            wine_dbgstr_longlong(dstBuffer), regionCount, pRegions );
    p_vkCmdCopyBuffer( commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions );
}

/***********************************************************************
 *              vkCmdCopyBufferToImage (VULKAN.@)
 */
void WINAPI vkCmdCopyBufferToImage( VkCommandBuffer commandBuffer, VkBuffer srcBuffer,
        VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
        const VkBufferImageCopy *pRegions )
{
    TRACE( "(%p, %s, %s, %d, %u, %p)\n", commandBuffer, wine_dbgstr_longlong(srcBuffer),
            wine_dbgstr_longlong(dstImage), dstImageLayout, regionCount, pRegions );
    p_vkCmdCopyBufferToImage( commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount,
            pRegions );
}

/***********************************************************************
 *              vkCmdCopyImage (VULKAN.@)
 */
void WINAPI vkCmdCopyImage( VkCommandBuffer commandBuffer, VkImage srcImage,
        VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout,
        uint32_t regionCount, const VkImageCopy *pRegions )
{
    TRACE( "(%p, %s, %d, %s, %d, %u, %p)\n", commandBuffer, wine_dbgstr_longlong(srcImage),
            srcImageLayout, wine_dbgstr_longlong(dstImage), dstImageLayout, regionCount, pRegions );
    p_vkCmdCopyImage( commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout,
            regionCount, pRegions );
}

/***********************************************************************
 *              vkCmdCopyImageToBuffer (VULKAN.@)
 */
void WINAPI vkCmdCopyImageToBuffer( VkCommandBuffer commandBuffer, VkImage srcImage,
        VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount,
        const VkBufferImageCopy *pRegions )
{
    TRACE( "(%p, %s, %d, %s, %u, %p)\n", commandBuffer, wine_dbgstr_longlong(srcImage),
            srcImageLayout, wine_dbgstr_longlong(dstBuffer), regionCount, pRegions );
    p_vkCmdCopyImageToBuffer( commandBuffer, srcImage, srcImageLayout, dstBuffer, regionCount,
            pRegions );
}

/***********************************************************************
 *              vkCmdCopyQueryPoolResults (VULKAN.@)
 */
void WINAPI vkCmdCopyQueryPoolResults( VkCommandBuffer commandBuffer, VkQueryPool queryPool,
        uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset,
        VkDeviceSize stride, VkQueryResultFlags flags )
{
    TRACE( "(%p, %s, %u, %u, %s, %s, %s, %u)\n", commandBuffer, wine_dbgstr_longlong(queryPool),
            firstQuery, queryCount, wine_dbgstr_longlong(dstBuffer),
            wine_dbgstr_longlong(dstOffset), wine_dbgstr_longlong(stride), flags );
    p_vkCmdCopyQueryPoolResults( commandBuffer, queryPool, firstQuery, queryCount, dstBuffer,
            dstOffset, stride, flags );
}

/***********************************************************************
 *              vkCmdDispatch (VULKAN.@)
 */
void WINAPI vkCmdDispatch( VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z )
{
    TRACE( "(%p, %u, %u, %u)\n", commandBuffer, x, y, z );
    p_vkCmdDispatch( commandBuffer, x, y, z );
}

/***********************************************************************
 *              vkCmdDispatchIndirect (VULKAN.@)
 */
void WINAPI vkCmdDispatchIndirect( VkCommandBuffer commandBuffer, VkBuffer buffer,
        VkDeviceSize offset )
{
    TRACE( "(%p, %s, %s)\n", commandBuffer, wine_dbgstr_longlong(buffer),
            wine_dbgstr_longlong(offset) );
    p_vkCmdDispatchIndirect( commandBuffer, buffer, offset );
}

/***********************************************************************
 *              vkCmdDraw (VULKAN.@)
 */
void WINAPI vkCmdDraw( VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount,
        uint32_t firstVertex, uint32_t firstInstance )
{
    TRACE( "(%p, %u, %u, %u, %u)\n", commandBuffer, vertexCount, instanceCount, firstVertex,
            firstInstance );
    p_vkCmdDraw( commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance );
}

/***********************************************************************
 *              vkCmdDrawIndexed (VULKAN.@)
 */
void WINAPI vkCmdDrawIndexed( VkCommandBuffer commandBuffer, uint32_t indexCount,
        uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance )
{
    TRACE( "(%p, %u, %u, %u, %d, %u)\n", commandBuffer, indexCount, instanceCount, firstIndex,
            vertexOffset, firstInstance );
    p_vkCmdDrawIndexed( commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset,
            firstInstance );
}

/***********************************************************************
 *              vkCmdDrawIndexedIndirect (VULKAN.@)
 */
void WINAPI vkCmdDrawIndexedIndirect( VkCommandBuffer commandBuffer, VkBuffer buffer,
        VkDeviceSize offset, uint32_t drawCount, uint32_t stride )
{
    TRACE( "(%p, %s, %s, %u, %u)\n", commandBuffer, wine_dbgstr_longlong(buffer),
            wine_dbgstr_longlong(offset), drawCount, stride );
    p_vkCmdDrawIndexedIndirect( commandBuffer, buffer, offset, drawCount, stride );
}

/***********************************************************************
 *              vkCmdDrawIndexedIndirectCountAMD (VULKAN.@)
 */
void WINAPI vkCmdDrawIndexedIndirectCountAMD( VkCommandBuffer commandBuffer, VkBuffer buffer,
        VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset,
        uint32_t maxDrawCount, uint32_t stride )
{
    TRACE( "(%p, %s, %s, %s, %s, %u, %u)\n", commandBuffer, wine_dbgstr_longlong(buffer),
            wine_dbgstr_longlong(offset), wine_dbgstr_longlong(countBuffer),
            wine_dbgstr_longlong(countBufferOffset), maxDrawCount, stride );
    p_vkCmdDrawIndexedIndirectCountAMD( commandBuffer, buffer, offset, countBuffer,
            countBufferOffset, maxDrawCount, stride );
}

/***********************************************************************
 *              vkCmdDrawIndirect (VULKAN.@)
 */
void WINAPI vkCmdDrawIndirect( VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
        uint32_t drawCount, uint32_t stride )
{
    TRACE( "(%p, %s, %s, %u, %u)\n", commandBuffer, wine_dbgstr_longlong(buffer),
            wine_dbgstr_longlong(offset), drawCount, stride );
    p_vkCmdDrawIndirect( commandBuffer, buffer, offset, drawCount, stride );
}

/***********************************************************************
 *              vkCmdDrawIndirectCountAMD (VULKAN.@)
 */
void WINAPI vkCmdDrawIndirectCountAMD( VkCommandBuffer commandBuffer, VkBuffer buffer,
        VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset,
        uint32_t maxDrawCount, uint32_t stride )
{
    TRACE( "(%p, %s, %s, %s, %s, %u, %u)\n", commandBuffer, wine_dbgstr_longlong(buffer),
            wine_dbgstr_longlong(offset), wine_dbgstr_longlong(countBuffer),
            wine_dbgstr_longlong(countBufferOffset), maxDrawCount, stride );
    p_vkCmdDrawIndirectCountAMD( commandBuffer, buffer, offset, countBuffer, countBufferOffset,
            maxDrawCount, stride );
}

/***********************************************************************
 *              vkCmdEndQuery (VULKAN.@)
 */
void WINAPI vkCmdEndQuery( VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query )
{
    TRACE( "(%p, %s, %u)\n", commandBuffer, wine_dbgstr_longlong(queryPool), query );
    p_vkCmdEndQuery( commandBuffer, queryPool, query );
}

/***********************************************************************
 *              vkCmdEndRenderPass (VULKAN.@)
 */
void WINAPI vkCmdEndRenderPass( VkCommandBuffer commandBuffer )
{
    TRACE( "(%p)\n", commandBuffer );
    p_vkCmdEndRenderPass( commandBuffer );
}

/***********************************************************************
 *              vkCmdExecuteCommands (VULKAN.@)
 */
void WINAPI vkCmdExecuteCommands( VkCommandBuffer commandBuffer, uint32_t commandBufferCount,
        const VkCommandBuffer *pCommandBuffers )
{
    TRACE( "(%p, %u, %p)\n", commandBuffer, commandBufferCount, pCommandBuffers );
    p_vkCmdExecuteCommands( commandBuffer, commandBufferCount, pCommandBuffers );
}

/***********************************************************************
 *              vkCmdFillBuffer (VULKAN.@)
 */
void WINAPI vkCmdFillBuffer( VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
        VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data )
{
    TRACE( "(%p, %s, %s, %s, %u)\n", commandBuffer, wine_dbgstr_longlong(dstBuffer),
            wine_dbgstr_longlong(dstOffset), wine_dbgstr_longlong(size), data );
    p_vkCmdFillBuffer( commandBuffer, dstBuffer, dstOffset, size, data );
}

/***********************************************************************
 *              vkCmdNextSubpass (VULKAN.@)
 */
void WINAPI vkCmdNextSubpass( VkCommandBuffer commandBuffer, VkSubpassContents contents )
{
    TRACE( "(%p, %d)\n", commandBuffer, contents );
    p_vkCmdNextSubpass( commandBuffer, contents );
}

/***********************************************************************
 *              vkCmdPipelineBarrier (VULKAN.@)
 */
void WINAPI vkCmdPipelineBarrier( VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
        VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
        uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
        uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers,
        uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers )
{
    VkImageMemoryBarrier_host *ptr_pImageMemoryBarriers;

    TRACE( "(%p, %u, %u, %u, %u, %p, %u, %p, %u, %p)\n", commandBuffer, srcStageMask, dstStageMask,
            dependencyFlags, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount,
            pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers );

    ptr_pImageMemoryBarriers = convert_VkImageMemoryBarrier_array( pImageMemoryBarriers,
            imageMemoryBarrierCount );
    p_vkCmdPipelineBarrier( commandBuffer, srcStageMask, dstStageMask, dependencyFlags,
            memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers,
            imageMemoryBarrierCount, ptr_pImageMemoryBarriers );
    release_VkImageMemoryBarrier_array( NULL, ptr_pImageMemoryBarriers, imageMemoryBarrierCount );
}

/***********************************************************************
 *              vkCmdPushConstants (VULKAN.@)
 */
void WINAPI vkCmdPushConstants( VkCommandBuffer commandBuffer, VkPipelineLayout layout,
        VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void *pValues )
{
    TRACE( "(%p, %s, %u, %u, %u, %p)\n", commandBuffer, wine_dbgstr_longlong(layout), stageFlags,
            offset, size, pValues );
    p_vkCmdPushConstants( commandBuffer, layout, stageFlags, offset, size, pValues );
}

/***********************************************************************
 *              vkCmdResetEvent (VULKAN.@)
 */
void WINAPI vkCmdResetEvent( VkCommandBuffer commandBuffer, VkEvent event,
        VkPipelineStageFlags stageMask )
{
    TRACE( "(%p, %s, %u)\n", commandBuffer, wine_dbgstr_longlong(event), stageMask );
    p_vkCmdResetEvent( commandBuffer, event, stageMask );
}

/***********************************************************************
 *              vkCmdResetQueryPool (VULKAN.@)
 */
void WINAPI vkCmdResetQueryPool( VkCommandBuffer commandBuffer, VkQueryPool queryPool,
        uint32_t firstQuery, uint32_t queryCount )
{
    TRACE( "(%p, %s, %u, %u)\n", commandBuffer, wine_dbgstr_longlong(queryPool), firstQuery,
            queryCount );
    p_vkCmdResetQueryPool( commandBuffer, queryPool, firstQuery, queryCount );
}

/***********************************************************************
 *              vkCmdResolveImage (VULKAN.@)
 */
void WINAPI vkCmdResolveImage( VkCommandBuffer commandBuffer, VkImage srcImage,
        VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout,
        uint32_t regionCount, const VkImageResolve *pRegions )
{
    TRACE( "(%p, %s, %d, %s, %d, %u, %p)\n", commandBuffer, wine_dbgstr_longlong(srcImage),
            srcImageLayout, wine_dbgstr_longlong(dstImage), dstImageLayout, regionCount, pRegions );
    p_vkCmdResolveImage( commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout,
            regionCount, pRegions );
}

/***********************************************************************
 *              vkCmdSetBlendConstants (VULKAN.@)
 */
void WINAPI vkCmdSetBlendConstants( VkCommandBuffer commandBuffer, const float blendConstants[4] )
{
    TRACE( "(%p, %p)\n", commandBuffer, blendConstants );
    p_vkCmdSetBlendConstants( commandBuffer, blendConstants );
}

/***********************************************************************
 *              vkCmdSetDepthBias (VULKAN.@)
 */
void WINAPI vkCmdSetDepthBias( VkCommandBuffer commandBuffer, float depthBiasConstantFactor,
        float depthBiasClamp, float depthBiasSlopeFactor )
{
    TRACE( "(%p, %f, %f, %f)\n", commandBuffer, depthBiasConstantFactor, depthBiasClamp,
            depthBiasSlopeFactor );
    p_vkCmdSetDepthBias( commandBuffer, depthBiasConstantFactor, depthBiasClamp,
            depthBiasSlopeFactor );
}

/***********************************************************************
 *              vkCmdSetDepthBounds (VULKAN.@)
 */
void WINAPI vkCmdSetDepthBounds( VkCommandBuffer commandBuffer, float minDepthBounds,
        float maxDepthBounds )
{
    TRACE( "(%p, %f, %f)\n", commandBuffer, minDepthBounds, maxDepthBounds );
    p_vkCmdSetDepthBounds( commandBuffer, minDepthBounds, maxDepthBounds );
}

/***********************************************************************
 *              vkCmdSetEvent (VULKAN.@)
 */
void WINAPI vkCmdSetEvent( VkCommandBuffer commandBuffer, VkEvent event,
        VkPipelineStageFlags stageMask )
{
    TRACE( "(%p, %s, %u)\n", commandBuffer, wine_dbgstr_longlong(event), stageMask );
    p_vkCmdSetEvent( commandBuffer, event, stageMask );
}

/***********************************************************************
 *              vkCmdSetLineWidth (VULKAN.@)
 */
void WINAPI vkCmdSetLineWidth( VkCommandBuffer commandBuffer, float lineWidth )
{
    TRACE( "(%p, %f)\n", commandBuffer, lineWidth );
    p_vkCmdSetLineWidth( commandBuffer, lineWidth );
}

/***********************************************************************
 *              vkCmdSetScissor (VULKAN.@)
 */
void WINAPI vkCmdSetScissor( VkCommandBuffer commandBuffer, uint32_t firstScissor,
        uint32_t scissorCount, const VkRect2D *pScissors )
{
    TRACE( "(%p, %u, %u, %p)\n", commandBuffer, firstScissor, scissorCount, pScissors );
    p_vkCmdSetScissor( commandBuffer, firstScissor, scissorCount, pScissors );
}

/***********************************************************************
 *              vkCmdSetStencilCompareMask (VULKAN.@)
 */
void WINAPI vkCmdSetStencilCompareMask( VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
        uint32_t compareMask )
{
    TRACE( "(%p, %u, %u)\n", commandBuffer, faceMask, compareMask );
    p_vkCmdSetStencilCompareMask( commandBuffer, faceMask, compareMask );
}

/***********************************************************************
 *              vkCmdSetStencilReference (VULKAN.@)
 */
void WINAPI vkCmdSetStencilReference( VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
        uint32_t reference )
{
    TRACE( "(%p, %u, %u)\n", commandBuffer, faceMask, reference );
    p_vkCmdSetStencilReference( commandBuffer, faceMask, reference );
}

/***********************************************************************
 *              vkCmdSetStencilWriteMask (VULKAN.@)
 */
void WINAPI vkCmdSetStencilWriteMask( VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
        uint32_t writeMask )
{
    TRACE( "(%p, %u, %u)\n", commandBuffer, faceMask, writeMask );
    p_vkCmdSetStencilWriteMask( commandBuffer, faceMask, writeMask );
}

/***********************************************************************
 *              vkCmdSetViewport (VULKAN.@)
 */
void WINAPI vkCmdSetViewport( VkCommandBuffer commandBuffer, uint32_t firstViewport,
        uint32_t viewportCount, const VkViewport *pViewports )
{
    TRACE( "(%p, %u, %u, %p)\n", commandBuffer, firstViewport, viewportCount, pViewports );
    p_vkCmdSetViewport( commandBuffer, firstViewport, viewportCount, pViewports );
}

/***********************************************************************
 *              vkCmdUpdateBuffer (VULKAN.@)
 */
void WINAPI vkCmdUpdateBuffer( VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
        VkDeviceSize dstOffset, VkDeviceSize dataSize, const void *pData )
{
    TRACE( "(%p, %s, %s, %s, %p)\n", commandBuffer, wine_dbgstr_longlong(dstBuffer),
            wine_dbgstr_longlong(dstOffset), wine_dbgstr_longlong(dataSize), pData );
    p_vkCmdUpdateBuffer( commandBuffer, dstBuffer, dstOffset, dataSize, pData );
}

/***********************************************************************
 *              vkCmdWaitEvents (VULKAN.@)
 */
void WINAPI vkCmdWaitEvents( VkCommandBuffer commandBuffer, uint32_t eventCount,
        const VkEvent *pEvents, VkPipelineStageFlags srcStageMask,
        VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount,
        const VkMemoryBarrier *pMemoryBarriers, uint32_t bufferMemoryBarrierCount,
        const VkBufferMemoryBarrier *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount,
        const VkImageMemoryBarrier *pImageMemoryBarriers )
{
    VkImageMemoryBarrier_host *ptr_pImageMemoryBarriers;

    TRACE( "(%p, %u, %p, %u, %u, %u, %p, %u, %p, %u, %p)\n", commandBuffer, eventCount, pEvents,
            srcStageMask, dstStageMask, memoryBarrierCount, pMemoryBarriers,
            bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount,
            pImageMemoryBarriers );

    ptr_pImageMemoryBarriers = convert_VkImageMemoryBarrier_array( pImageMemoryBarriers,
            imageMemoryBarrierCount );
    p_vkCmdWaitEvents( commandBuffer, eventCount, pEvents, srcStageMask, dstStageMask,
            memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers,
            imageMemoryBarrierCount, ptr_pImageMemoryBarriers );
    release_VkImageMemoryBarrier_array( NULL, ptr_pImageMemoryBarriers, imageMemoryBarrierCount );
}

/***********************************************************************
 *              vkCmdWriteTimestamp (VULKAN.@)
 */
void WINAPI vkCmdWriteTimestamp( VkCommandBuffer commandBuffer,
        VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query )
{
    TRACE( "(%p, %d, %s, %u)\n", commandBuffer, pipelineStage, wine_dbgstr_longlong(queryPool),
            query );
    p_vkCmdWriteTimestamp( commandBuffer, pipelineStage, queryPool, query );
}

/***********************************************************************
 *              vkCreateBuffer (VULKAN.@)
 */
VkResult WINAPI vkCreateBuffer( VkDevice device, const VkBufferCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer )
{
    VkBufferCreateInfo_host tmp_pCreateInfo, *ptr_pCreateInfo;
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pBuffer );

    ptr_pCreateInfo = convert_VkBufferCreateInfo( &tmp_pCreateInfo, pCreateInfo );
    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateBuffer( device, ptr_pCreateInfo, ptr_pAllocator, pBuffer );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
    release_VkBufferCreateInfo( NULL, ptr_pCreateInfo );

    return res;
}

/***********************************************************************
 *              vkCreateBufferView (VULKAN.@)
 */
VkResult WINAPI vkCreateBufferView( VkDevice device, const VkBufferViewCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator, VkBufferView *pView )
{
    VkBufferViewCreateInfo_host tmp_pCreateInfo, *ptr_pCreateInfo;
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pView );

    ptr_pCreateInfo = convert_VkBufferViewCreateInfo( &tmp_pCreateInfo, pCreateInfo );
    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateBufferView( device, ptr_pCreateInfo, ptr_pAllocator, pView );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
    release_VkBufferViewCreateInfo( NULL, ptr_pCreateInfo );

    return res;
}

/***********************************************************************
 *              vkCreateCommandPool (VULKAN.@)
 */
VkResult WINAPI vkCreateCommandPool( VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pCommandPool );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateCommandPool( device, pCreateInfo, ptr_pAllocator, pCommandPool );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );

    return res;
}

/***********************************************************************
 *              vkCreateComputePipelines (VULKAN.@)
 */
VkResult WINAPI vkCreateComputePipelines( VkDevice device, VkPipelineCache pipelineCache,
        uint32_t createInfoCount, const VkComputePipelineCreateInfo *pCreateInfos,
        const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines )
{
    VkComputePipelineCreateInfo_host *ptr_pCreateInfos;
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %s, %u, %p, %p, %p)\n", device, wine_dbgstr_longlong(pipelineCache),
            createInfoCount, pCreateInfos, pAllocator, pPipelines );

    ptr_pCreateInfos = convert_VkComputePipelineCreateInfo_array( pCreateInfos, createInfoCount );
    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateComputePipelines( device, pipelineCache, createInfoCount, ptr_pCreateInfos,
            ptr_pAllocator, pPipelines );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
    release_VkComputePipelineCreateInfo_array( NULL, ptr_pCreateInfos, createInfoCount );

    return res;
}

/***********************************************************************
 *              vkCreateDebugReportCallbackEXT (VULKAN.@)
 */
VkResult WINAPI vkCreateDebugReportCallbackEXT( VkInstance instance,
        const VkDebugReportCallbackCreateInfoEXT *pCreateInfo,
        const VkAllocationCallbacks *pAllocator, VkDebugReportCallbackEXT *pCallback )
{
    VkDebugReportCallbackCreateInfoEXT_host tmp_pCreateInfo, *ptr_pCreateInfo;
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", instance, pCreateInfo, pAllocator, pCallback );

    ptr_pCreateInfo = convert_VkDebugReportCallbackCreateInfoEXT( &tmp_pCreateInfo, pCreateInfo );
    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateDebugReportCallbackEXT( instance, ptr_pCreateInfo, ptr_pAllocator, pCallback );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
    release_VkDebugReportCallbackCreateInfoEXT( NULL, ptr_pCreateInfo );

    return res;
}

/***********************************************************************
 *              vkCreateDescriptorPool (VULKAN.@)
 */
VkResult WINAPI vkCreateDescriptorPool( VkDevice device,
        const VkDescriptorPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
        VkDescriptorPool *pDescriptorPool )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pDescriptorPool );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateDescriptorPool( device, pCreateInfo, ptr_pAllocator, pDescriptorPool );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );

    return res;
}

/***********************************************************************
 *              vkCreateDescriptorSetLayout (VULKAN.@)
 */
VkResult WINAPI vkCreateDescriptorSetLayout( VkDevice device,
        const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pSetLayout );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateDescriptorSetLayout( device, pCreateInfo, ptr_pAllocator, pSetLayout );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );

    return res;
}

/***********************************************************************
 *              vkCreateDevice (VULKAN.@)
 */
VkResult WINAPI vkCreateDevice( VkPhysicalDevice physicalDevice,
        const VkDeviceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
        VkDevice *pDevice )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", physicalDevice, pCreateInfo, pAllocator, pDevice );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateDevice( physicalDevice, pCreateInfo, ptr_pAllocator, pDevice );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );

    return res;
}

/***********************************************************************
 *              vkCreateDisplayModeKHR (VULKAN.@)
 */
VkResult WINAPI vkCreateDisplayModeKHR( VkPhysicalDevice physicalDevice, VkDisplayKHR display,
        const VkDisplayModeCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator,
        VkDisplayModeKHR *pMode )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %s, %p, %p, %p)\n", physicalDevice, wine_dbgstr_longlong(display), pCreateInfo,
            pAllocator, pMode );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateDisplayModeKHR( physicalDevice, display, pCreateInfo, ptr_pAllocator, pMode );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );

    return res;
}

/***********************************************************************
 *              vkCreateDisplayPlaneSurfaceKHR (VULKAN.@)
 */
VkResult WINAPI vkCreateDisplayPlaneSurfaceKHR( VkInstance instance,
        const VkDisplaySurfaceCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator,
        VkSurfaceKHR *pSurface )
{
    VkDisplaySurfaceCreateInfoKHR_host tmp_pCreateInfo, *ptr_pCreateInfo;
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", instance, pCreateInfo, pAllocator, pSurface );

    ptr_pCreateInfo = convert_VkDisplaySurfaceCreateInfoKHR( &tmp_pCreateInfo, pCreateInfo );
    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateDisplayPlaneSurfaceKHR( instance, ptr_pCreateInfo, ptr_pAllocator, pSurface );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
    release_VkDisplaySurfaceCreateInfoKHR( NULL, ptr_pCreateInfo );

    return res;
}

/***********************************************************************
 *              vkCreateEvent (VULKAN.@)
 */
VkResult WINAPI vkCreateEvent( VkDevice device, const VkEventCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator, VkEvent *pEvent )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pEvent );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateEvent( device, pCreateInfo, ptr_pAllocator, pEvent );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );

    return res;
}

/***********************************************************************
 *              vkCreateFence (VULKAN.@)
 */
VkResult WINAPI vkCreateFence( VkDevice device, const VkFenceCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator, VkFence *pFence )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pFence );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateFence( device, pCreateInfo, ptr_pAllocator, pFence );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );

    return res;
}

/***********************************************************************
 *              vkCreateFramebuffer (VULKAN.@)
 */
VkResult WINAPI vkCreateFramebuffer( VkDevice device, const VkFramebufferCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer )
{
    VkFramebufferCreateInfo_host tmp_pCreateInfo, *ptr_pCreateInfo;
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pFramebuffer );

    ptr_pCreateInfo = convert_VkFramebufferCreateInfo( &tmp_pCreateInfo, pCreateInfo );
    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateFramebuffer( device, ptr_pCreateInfo, ptr_pAllocator, pFramebuffer );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
    release_VkFramebufferCreateInfo( NULL, ptr_pCreateInfo );

    return res;
}

/***********************************************************************
 *              vkCreateGraphicsPipelines (VULKAN.@)
 */
VkResult WINAPI vkCreateGraphicsPipelines( VkDevice device, VkPipelineCache pipelineCache,
        uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo *pCreateInfos,
        const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines )
{
    VkGraphicsPipelineCreateInfo_host *ptr_pCreateInfos;
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %s, %u, %p, %p, %p)\n", device, wine_dbgstr_longlong(pipelineCache),
            createInfoCount, pCreateInfos, pAllocator, pPipelines );

    ptr_pCreateInfos = convert_VkGraphicsPipelineCreateInfo_array( pCreateInfos, createInfoCount );
    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateGraphicsPipelines( device, pipelineCache, createInfoCount, ptr_pCreateInfos,
            ptr_pAllocator, pPipelines );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
    release_VkGraphicsPipelineCreateInfo_array( NULL, ptr_pCreateInfos, createInfoCount );

    return res;
}

/***********************************************************************
 *              vkCreateImage (VULKAN.@)
 */
VkResult WINAPI vkCreateImage( VkDevice device, const VkImageCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator, VkImage *pImage )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pImage );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateImage( device, pCreateInfo, ptr_pAllocator, pImage );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );

    return res;
}

/***********************************************************************
 *              vkCreateImageView (VULKAN.@)
 */
VkResult WINAPI vkCreateImageView( VkDevice device, const VkImageViewCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator, VkImageView *pView )
{
    VkImageViewCreateInfo_host tmp_pCreateInfo, *ptr_pCreateInfo;
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pView );

    ptr_pCreateInfo = convert_VkImageViewCreateInfo( &tmp_pCreateInfo, pCreateInfo );
    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateImageView( device, ptr_pCreateInfo, ptr_pAllocator, pView );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
    release_VkImageViewCreateInfo( NULL, ptr_pCreateInfo );

    return res;
}

/***********************************************************************
 *              vkCreatePipelineCache (VULKAN.@)
 */
VkResult WINAPI vkCreatePipelineCache( VkDevice device,
        const VkPipelineCacheCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
        VkPipelineCache *pPipelineCache )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pPipelineCache );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreatePipelineCache( device, pCreateInfo, ptr_pAllocator, pPipelineCache );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );

    return res;
}

/***********************************************************************
 *              vkCreatePipelineLayout (VULKAN.@)
 */
VkResult WINAPI vkCreatePipelineLayout( VkDevice device,
        const VkPipelineLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
        VkPipelineLayout *pPipelineLayout )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pPipelineLayout );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreatePipelineLayout( device, pCreateInfo, ptr_pAllocator, pPipelineLayout );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );

    return res;
}

/***********************************************************************
 *              vkCreateQueryPool (VULKAN.@)
 */
VkResult WINAPI vkCreateQueryPool( VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator, VkQueryPool *pQueryPool )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pQueryPool );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateQueryPool( device, pCreateInfo, ptr_pAllocator, pQueryPool );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );

    return res;
}

/***********************************************************************
 *              vkCreateRenderPass (VULKAN.@)
 */
VkResult WINAPI vkCreateRenderPass( VkDevice device, const VkRenderPassCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pRenderPass );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateRenderPass( device, pCreateInfo, ptr_pAllocator, pRenderPass );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );

    return res;
}

/***********************************************************************
 *              vkCreateSampler (VULKAN.@)
 */
VkResult WINAPI vkCreateSampler( VkDevice device, const VkSamplerCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator, VkSampler *pSampler )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pSampler );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateSampler( device, pCreateInfo, ptr_pAllocator, pSampler );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );

    return res;
}

/***********************************************************************
 *              vkCreateSemaphore (VULKAN.@)
 */
VkResult WINAPI vkCreateSemaphore( VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pSemaphore );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateSemaphore( device, pCreateInfo, ptr_pAllocator, pSemaphore );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );

    return res;
}

/***********************************************************************
 *              vkCreateShaderModule (VULKAN.@)
 */
VkResult WINAPI vkCreateShaderModule( VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pShaderModule );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateShaderModule( device, pCreateInfo, ptr_pAllocator, pShaderModule );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );

    return res;
}

/***********************************************************************
 *              vkCreateSharedSwapchainsKHR (VULKAN.@)
 */
VkResult WINAPI vkCreateSharedSwapchainsKHR( VkDevice device, uint32_t swapchainCount,
        const VkSwapchainCreateInfoKHR *pCreateInfos, const VkAllocationCallbacks *pAllocator,
        VkSwapchainKHR *pSwapchains )
{
    VkSwapchainCreateInfoKHR_host *ptr_pCreateInfos;
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %u, %p, %p, %p)\n", device, swapchainCount, pCreateInfos, pAllocator,
            pSwapchains );

    ptr_pCreateInfos = convert_VkSwapchainCreateInfoKHR_array( pCreateInfos, swapchainCount );
    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateSharedSwapchainsKHR( device, swapchainCount, ptr_pCreateInfos, ptr_pAllocator,
            pSwapchains );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
    release_VkSwapchainCreateInfoKHR_array( NULL, ptr_pCreateInfos, swapchainCount );

    return res;
}

/***********************************************************************
 *              vkCreateSwapchainKHR (VULKAN.@)
 */
VkResult WINAPI vkCreateSwapchainKHR( VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo,
        const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain )
{
    VkSwapchainCreateInfoKHR_host tmp_pCreateInfo, *ptr_pCreateInfo;
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;
    VkResult res;

    TRACE( "(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pSwapchain );

    ptr_pCreateInfo = convert_VkSwapchainCreateInfoKHR( &tmp_pCreateInfo, pCreateInfo );
    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    res = p_vkCreateSwapchainKHR( device, ptr_pCreateInfo, ptr_pAllocator, pSwapchain );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
    release_VkSwapchainCreateInfoKHR( NULL, ptr_pCreateInfo );

    return res;
}

/***********************************************************************
 *              vkDebugReportMessageEXT (VULKAN.@)
 */
void WINAPI vkDebugReportMessageEXT( VkInstance instance, VkDebugReportFlagsEXT flags,
        VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location,
        int32_t messageCode, const char *pLayerPrefix, const char *pMessage )
{
    TRACE( "(%p, %u, %d, %s, %lu, %d, %s, %s)\n", instance, flags, objectType,
            wine_dbgstr_longlong(object), (SIZE_T)location, messageCode, debugstr_a(pLayerPrefix),
            debugstr_a(pMessage) );
    p_vkDebugReportMessageEXT( instance, flags, objectType, object, location, messageCode,
            pLayerPrefix, pMessage );
}

/***********************************************************************
 *              vkDestroyBuffer (VULKAN.@)
 */
void WINAPI vkDestroyBuffer( VkDevice device, VkBuffer buffer,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(buffer), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyBuffer( device, buffer, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroyBufferView (VULKAN.@)
 */
void WINAPI vkDestroyBufferView( VkDevice device, VkBufferView bufferView,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(bufferView), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyBufferView( device, bufferView, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroyCommandPool (VULKAN.@)
 */
void WINAPI vkDestroyCommandPool( VkDevice device, VkCommandPool commandPool,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(commandPool), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyCommandPool( device, commandPool, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroyDebugReportCallbackEXT (VULKAN.@)
 */
void WINAPI vkDestroyDebugReportCallbackEXT( VkInstance instance,
        VkDebugReportCallbackEXT callback, const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", instance, wine_dbgstr_longlong(callback), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyDebugReportCallbackEXT( instance, callback, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroyDescriptorPool (VULKAN.@)
 */
void WINAPI vkDestroyDescriptorPool( VkDevice device, VkDescriptorPool descriptorPool,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(descriptorPool), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyDescriptorPool( device, descriptorPool, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroyDescriptorSetLayout (VULKAN.@)
 */
void WINAPI vkDestroyDescriptorSetLayout( VkDevice device,
        VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(descriptorSetLayout), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyDescriptorSetLayout( device, descriptorSetLayout, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroyDevice (VULKAN.@)
 */
void WINAPI vkDestroyDevice( VkDevice device, const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %p)\n", device, pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyDevice( device, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroyEvent (VULKAN.@)
 */
void WINAPI vkDestroyEvent( VkDevice device, VkEvent event,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(event), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyEvent( device, event, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroyFence (VULKAN.@)
 */
void WINAPI vkDestroyFence( VkDevice device, VkFence fence,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(fence), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyFence( device, fence, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroyFramebuffer (VULKAN.@)
 */
void WINAPI vkDestroyFramebuffer( VkDevice device, VkFramebuffer framebuffer,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(framebuffer), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyFramebuffer( device, framebuffer, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroyImage (VULKAN.@)
 */
void WINAPI vkDestroyImage( VkDevice device, VkImage image,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(image), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyImage( device, image, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroyImageView (VULKAN.@)
 */
void WINAPI vkDestroyImageView( VkDevice device, VkImageView imageView,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(imageView), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyImageView( device, imageView, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroyInstance (VULKAN.@)
 */
void WINAPI vkDestroyInstance( VkInstance instance, const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %p)\n", instance, pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyInstance( instance, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroyPipeline (VULKAN.@)
 */
void WINAPI vkDestroyPipeline( VkDevice device, VkPipeline pipeline,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(pipeline), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyPipeline( device, pipeline, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroyPipelineCache (VULKAN.@)
 */
void WINAPI vkDestroyPipelineCache( VkDevice device, VkPipelineCache pipelineCache,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(pipelineCache), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyPipelineCache( device, pipelineCache, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroyPipelineLayout (VULKAN.@)
 */
void WINAPI vkDestroyPipelineLayout( VkDevice device, VkPipelineLayout pipelineLayout,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(pipelineLayout), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyPipelineLayout( device, pipelineLayout, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroyQueryPool (VULKAN.@)
 */
void WINAPI vkDestroyQueryPool( VkDevice device, VkQueryPool queryPool,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(queryPool), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyQueryPool( device, queryPool, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroyRenderPass (VULKAN.@)
 */
void WINAPI vkDestroyRenderPass( VkDevice device, VkRenderPass renderPass,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(renderPass), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyRenderPass( device, renderPass, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroySampler (VULKAN.@)
 */
void WINAPI vkDestroySampler( VkDevice device, VkSampler sampler,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(sampler), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroySampler( device, sampler, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroySemaphore (VULKAN.@)
 */
void WINAPI vkDestroySemaphore( VkDevice device, VkSemaphore semaphore,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(semaphore), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroySemaphore( device, semaphore, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroyShaderModule (VULKAN.@)
 */
void WINAPI vkDestroyShaderModule( VkDevice device, VkShaderModule shaderModule,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(shaderModule), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroyShaderModule( device, shaderModule, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroySurfaceKHR (VULKAN.@)
 */
void WINAPI vkDestroySurfaceKHR( VkInstance instance, VkSurfaceKHR surface,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", instance, wine_dbgstr_longlong(surface), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroySurfaceKHR( instance, surface, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDestroySwapchainKHR (VULKAN.@)
 */
void WINAPI vkDestroySwapchainKHR( VkDevice device, VkSwapchainKHR swapchain,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(swapchain), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkDestroySwapchainKHR( device, swapchain, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkDeviceWaitIdle (VULKAN.@)
 */
VkResult WINAPI vkDeviceWaitIdle( VkDevice device )
{
    TRACE( "(%p)\n", device );
    return p_vkDeviceWaitIdle( device );
}

/***********************************************************************
 *              vkEndCommandBuffer (VULKAN.@)
 */
VkResult WINAPI vkEndCommandBuffer( VkCommandBuffer commandBuffer )
{
    TRACE( "(%p)\n", commandBuffer );
    return p_vkEndCommandBuffer( commandBuffer );
}

/***********************************************************************
 *              vkEnumerateDeviceExtensionProperties (VULKAN.@)
 */
VkResult WINAPI vkEnumerateDeviceExtensionProperties( VkPhysicalDevice physicalDevice,
        const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties )
{
    TRACE( "(%p, %s, %p, %p)\n", physicalDevice, debugstr_a(pLayerName), pPropertyCount,
            pProperties );
    return p_vkEnumerateDeviceExtensionProperties( physicalDevice, pLayerName, pPropertyCount,
            pProperties );
}

/***********************************************************************
 *              vkEnumerateDeviceLayerProperties (VULKAN.@)
 */
VkResult WINAPI vkEnumerateDeviceLayerProperties( VkPhysicalDevice physicalDevice,
        uint32_t *pPropertyCount, VkLayerProperties *pProperties )
{
    TRACE( "(%p, %p, %p)\n", physicalDevice, pPropertyCount, pProperties );
    return p_vkEnumerateDeviceLayerProperties( physicalDevice, pPropertyCount, pProperties );
}

/***********************************************************************
 *              vkEnumerateInstanceLayerProperties (VULKAN.@)
 */
VkResult WINAPI vkEnumerateInstanceLayerProperties( uint32_t *pPropertyCount,
        VkLayerProperties *pProperties )
{
    TRACE( "(%p, %p)\n", pPropertyCount, pProperties );
    return p_vkEnumerateInstanceLayerProperties( pPropertyCount, pProperties );
}

/***********************************************************************
 *              vkEnumeratePhysicalDevices (VULKAN.@)
 */
VkResult WINAPI vkEnumeratePhysicalDevices( VkInstance instance, uint32_t *pPhysicalDeviceCount,
        VkPhysicalDevice *pPhysicalDevices )
{
    TRACE( "(%p, %p, %p)\n", instance, pPhysicalDeviceCount, pPhysicalDevices );
    return p_vkEnumeratePhysicalDevices( instance, pPhysicalDeviceCount, pPhysicalDevices );
}

/***********************************************************************
 *              vkFlushMappedMemoryRanges (VULKAN.@)
 */
VkResult WINAPI vkFlushMappedMemoryRanges( VkDevice device, uint32_t memoryRangeCount,
        const VkMappedMemoryRange *pMemoryRanges )
{
    TRACE( "(%p, %u, %p)\n", device, memoryRangeCount, pMemoryRanges );
    return p_vkFlushMappedMemoryRanges( device, memoryRangeCount, pMemoryRanges );
}

/***********************************************************************
 *              vkFreeCommandBuffers (VULKAN.@)
 */
void WINAPI vkFreeCommandBuffers( VkDevice device, VkCommandPool commandPool,
        uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers )
{
    TRACE( "(%p, %s, %u, %p)\n", device, wine_dbgstr_longlong(commandPool), commandBufferCount,
            pCommandBuffers );
    p_vkFreeCommandBuffers( device, commandPool, commandBufferCount, pCommandBuffers );
}

/***********************************************************************
 *              vkFreeDescriptorSets (VULKAN.@)
 */
VkResult WINAPI vkFreeDescriptorSets( VkDevice device, VkDescriptorPool descriptorPool,
        uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets )
{
    TRACE( "(%p, %s, %u, %p)\n", device, wine_dbgstr_longlong(descriptorPool), descriptorSetCount,
            pDescriptorSets );
    return p_vkFreeDescriptorSets( device, descriptorPool, descriptorSetCount, pDescriptorSets );
}

/***********************************************************************
 *              vkFreeMemory (VULKAN.@)
 */
void WINAPI vkFreeMemory( VkDevice device, VkDeviceMemory memory,
        const VkAllocationCallbacks *pAllocator )
{
    VkAllocationCallbacks_host tmp_pAllocator, *ptr_pAllocator;

    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(memory), pAllocator );

    ptr_pAllocator = convert_VkAllocationCallbacks( &tmp_pAllocator, pAllocator );
    p_vkFreeMemory( device, memory, ptr_pAllocator );
    release_VkAllocationCallbacks( NULL, ptr_pAllocator );
}

/***********************************************************************
 *              vkGetBufferMemoryRequirements (VULKAN.@)
 */
void WINAPI vkGetBufferMemoryRequirements( VkDevice device, VkBuffer buffer,
        VkMemoryRequirements *pMemoryRequirements )
{
    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(buffer), pMemoryRequirements );
    p_vkGetBufferMemoryRequirements( device, buffer, pMemoryRequirements );
}

/***********************************************************************
 *              vkGetDeviceMemoryCommitment (VULKAN.@)
 */
void WINAPI vkGetDeviceMemoryCommitment( VkDevice device, VkDeviceMemory memory,
        VkDeviceSize *pCommittedMemoryInBytes )
{
    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(memory), pCommittedMemoryInBytes );
    p_vkGetDeviceMemoryCommitment( device, memory, pCommittedMemoryInBytes );
}

/***********************************************************************
 *              vkGetDeviceQueue (VULKAN.@)
 */
void WINAPI vkGetDeviceQueue( VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex,
        VkQueue *pQueue )
{
    TRACE( "(%p, %u, %u, %p)\n", device, queueFamilyIndex, queueIndex, pQueue );
    p_vkGetDeviceQueue( device, queueFamilyIndex, queueIndex, pQueue );
}

/***********************************************************************
 *              vkGetDisplayModePropertiesKHR (VULKAN.@)
 */
VkResult WINAPI vkGetDisplayModePropertiesKHR( VkPhysicalDevice physicalDevice,
        VkDisplayKHR display, uint32_t *pPropertyCount, VkDisplayModePropertiesKHR *pProperties )
{
    VkDisplayModePropertiesKHR_host *ptr_pProperties;
    VkResult res;

    TRACE( "(%p, %s, %p, %p)\n", physicalDevice, wine_dbgstr_longlong(display), pPropertyCount,
            pProperties );

    ptr_pProperties = convert_VkDisplayModePropertiesKHR_array( pProperties, *pPropertyCount );
    res = p_vkGetDisplayModePropertiesKHR( physicalDevice, display, pPropertyCount,
            ptr_pProperties );
    release_VkDisplayModePropertiesKHR_array( pProperties, ptr_pProperties, *pPropertyCount );

    return res;
}

/***********************************************************************
 *              vkGetDisplayPlaneCapabilitiesKHR (VULKAN.@)
 */
VkResult WINAPI vkGetDisplayPlaneCapabilitiesKHR( VkPhysicalDevice physicalDevice,
        VkDisplayModeKHR mode, uint32_t planeIndex, VkDisplayPlaneCapabilitiesKHR *pCapabilities )
{
    TRACE( "(%p, %s, %u, %p)\n", physicalDevice, wine_dbgstr_longlong(mode), planeIndex,
            pCapabilities );
    return p_vkGetDisplayPlaneCapabilitiesKHR( physicalDevice, mode, planeIndex, pCapabilities );
}

/***********************************************************************
 *              vkGetDisplayPlaneSupportedDisplaysKHR (VULKAN.@)
 */
VkResult WINAPI vkGetDisplayPlaneSupportedDisplaysKHR( VkPhysicalDevice physicalDevice,
        uint32_t planeIndex, uint32_t *pDisplayCount, VkDisplayKHR *pDisplays )
{
    TRACE( "(%p, %u, %p, %p)\n", physicalDevice, planeIndex, pDisplayCount, pDisplays );
    return p_vkGetDisplayPlaneSupportedDisplaysKHR( physicalDevice, planeIndex, pDisplayCount,
            pDisplays );
}

/***********************************************************************
 *              vkGetEventStatus (VULKAN.@)
 */
VkResult WINAPI vkGetEventStatus( VkDevice device, VkEvent event )
{
    TRACE( "(%p, %s)\n", device, wine_dbgstr_longlong(event) );
    return p_vkGetEventStatus( device, event );
}

/***********************************************************************
 *              vkGetFenceStatus (VULKAN.@)
 */
VkResult WINAPI vkGetFenceStatus( VkDevice device, VkFence fence )
{
    TRACE( "(%p, %s)\n", device, wine_dbgstr_longlong(fence) );
    return p_vkGetFenceStatus( device, fence );
}

/***********************************************************************
 *              vkGetImageMemoryRequirements (VULKAN.@)
 */
void WINAPI vkGetImageMemoryRequirements( VkDevice device, VkImage image,
        VkMemoryRequirements *pMemoryRequirements )
{
    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(image), pMemoryRequirements );
    p_vkGetImageMemoryRequirements( device, image, pMemoryRequirements );
}

/***********************************************************************
 *              vkGetImageSparseMemoryRequirements (VULKAN.@)
 */
void WINAPI vkGetImageSparseMemoryRequirements( VkDevice device, VkImage image,
        uint32_t *pSparseMemoryRequirementCount,
        VkSparseImageMemoryRequirements *pSparseMemoryRequirements )
{
    TRACE( "(%p, %s, %p, %p)\n", device, wine_dbgstr_longlong(image),
            pSparseMemoryRequirementCount, pSparseMemoryRequirements );
    p_vkGetImageSparseMemoryRequirements( device, image, pSparseMemoryRequirementCount,
            pSparseMemoryRequirements );
}

/***********************************************************************
 *              vkGetImageSubresourceLayout (VULKAN.@)
 */
void WINAPI vkGetImageSubresourceLayout( VkDevice device, VkImage image,
        const VkImageSubresource *pSubresource, VkSubresourceLayout *pLayout )
{
    TRACE( "(%p, %s, %p, %p)\n", device, wine_dbgstr_longlong(image), pSubresource, pLayout );
    p_vkGetImageSubresourceLayout( device, image, pSubresource, pLayout );
}

/***********************************************************************
 *              vkGetPhysicalDeviceDisplayPlanePropertiesKHR (VULKAN.@)
 */
VkResult WINAPI vkGetPhysicalDeviceDisplayPlanePropertiesKHR( VkPhysicalDevice physicalDevice,
        uint32_t *pPropertyCount, VkDisplayPlanePropertiesKHR *pProperties )
{
    VkDisplayPlanePropertiesKHR_host *ptr_pProperties;
    VkResult res;

    TRACE( "(%p, %p, %p)\n", physicalDevice, pPropertyCount, pProperties );

    ptr_pProperties = convert_VkDisplayPlanePropertiesKHR_array( pProperties, *pPropertyCount );
    res = p_vkGetPhysicalDeviceDisplayPlanePropertiesKHR( physicalDevice, pPropertyCount,
            ptr_pProperties );
    release_VkDisplayPlanePropertiesKHR_array( pProperties, ptr_pProperties, *pPropertyCount );

    return res;
}

/***********************************************************************
 *              vkGetPhysicalDeviceDisplayPropertiesKHR (VULKAN.@)
 */
VkResult WINAPI vkGetPhysicalDeviceDisplayPropertiesKHR( VkPhysicalDevice physicalDevice,
        uint32_t *pPropertyCount, VkDisplayPropertiesKHR *pProperties )
{
    TRACE( "(%p, %p, %p)\n", physicalDevice, pPropertyCount, pProperties );
    return p_vkGetPhysicalDeviceDisplayPropertiesKHR( physicalDevice, pPropertyCount, pProperties );
}

/***********************************************************************
 *              vkGetPhysicalDeviceExternalImageFormatPropertiesNV (VULKAN.@)
 */
VkResult WINAPI vkGetPhysicalDeviceExternalImageFormatPropertiesNV(
        VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling,
        VkImageUsageFlags usage, VkImageCreateFlags flags,
        VkExternalMemoryHandleTypeFlagsNV externalHandleType,
        VkExternalImageFormatPropertiesNV *pExternalImageFormatProperties )
{
    VkExternalImageFormatPropertiesNV_host tmp_pExternalImageFormatProperties, *ptr_pExternalImageFormatProperties;
    VkResult res;

    TRACE( "(%p, %d, %d, %d, %u, %u, %u, %p)\n", physicalDevice, format, type, tiling, usage,
            flags, externalHandleType, pExternalImageFormatProperties );

    ptr_pExternalImageFormatProperties = convert_VkExternalImageFormatPropertiesNV(
            &tmp_pExternalImageFormatProperties, pExternalImageFormatProperties );
    res = p_vkGetPhysicalDeviceExternalImageFormatPropertiesNV( physicalDevice, format, type,
            tiling, usage, flags, externalHandleType, ptr_pExternalImageFormatProperties );
    release_VkExternalImageFormatPropertiesNV( pExternalImageFormatProperties,
            ptr_pExternalImageFormatProperties );

    return res;
}

/***********************************************************************
 *              vkGetPhysicalDeviceFeatures (VULKAN.@)
 */
void WINAPI vkGetPhysicalDeviceFeatures( VkPhysicalDevice physicalDevice,
        VkPhysicalDeviceFeatures *pFeatures )
{
    TRACE( "(%p, %p)\n", physicalDevice, pFeatures );
    p_vkGetPhysicalDeviceFeatures( physicalDevice, pFeatures );
}

/***********************************************************************
 *              vkGetPhysicalDeviceFormatProperties (VULKAN.@)
 */
void WINAPI vkGetPhysicalDeviceFormatProperties( VkPhysicalDevice physicalDevice, VkFormat format,
        VkFormatProperties *pFormatProperties )
{
    TRACE( "(%p, %d, %p)\n", physicalDevice, format, pFormatProperties );
    p_vkGetPhysicalDeviceFormatProperties( physicalDevice, format, pFormatProperties );
}

/***********************************************************************
 *              vkGetPhysicalDeviceImageFormatProperties (VULKAN.@)
 */
VkResult WINAPI vkGetPhysicalDeviceImageFormatProperties( VkPhysicalDevice physicalDevice,
        VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage,
        VkImageCreateFlags flags, VkImageFormatProperties *pImageFormatProperties )
{
    TRACE( "(%p, %d, %d, %d, %u, %u, %p)\n", physicalDevice, format, type, tiling, usage, flags,
            pImageFormatProperties );
    return p_vkGetPhysicalDeviceImageFormatProperties( physicalDevice, format, type, tiling, usage,
            flags, pImageFormatProperties );
}

/***********************************************************************
 *              vkGetPhysicalDeviceMemoryProperties (VULKAN.@)
 */
void WINAPI vkGetPhysicalDeviceMemoryProperties( VkPhysicalDevice physicalDevice,
        VkPhysicalDeviceMemoryProperties *pMemoryProperties )
{
    VkPhysicalDeviceMemoryProperties_host tmp_pMemoryProperties, *ptr_pMemoryProperties;

    TRACE( "(%p, %p)\n", physicalDevice, pMemoryProperties );

    ptr_pMemoryProperties = convert_VkPhysicalDeviceMemoryProperties( &tmp_pMemoryProperties,
            pMemoryProperties );
    p_vkGetPhysicalDeviceMemoryProperties( physicalDevice, ptr_pMemoryProperties );
    release_VkPhysicalDeviceMemoryProperties( pMemoryProperties, ptr_pMemoryProperties );
}

/***********************************************************************
 *              vkGetPhysicalDeviceProperties (VULKAN.@)
 */
void WINAPI vkGetPhysicalDeviceProperties( VkPhysicalDevice physicalDevice,
        VkPhysicalDeviceProperties *pProperties )
{
    VkPhysicalDeviceProperties_host tmp_pProperties, *ptr_pProperties;

    TRACE( "(%p, %p)\n", physicalDevice, pProperties );

    ptr_pProperties = convert_VkPhysicalDeviceProperties( &tmp_pProperties, pProperties );
    p_vkGetPhysicalDeviceProperties( physicalDevice, ptr_pProperties );
    release_VkPhysicalDeviceProperties( pProperties, ptr_pProperties );
}

/***********************************************************************
 *              vkGetPhysicalDeviceQueueFamilyProperties (VULKAN.@)
 */
void WINAPI vkGetPhysicalDeviceQueueFamilyProperties( VkPhysicalDevice physicalDevice,
        uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties *pQueueFamilyProperties )
{
    TRACE( "(%p, %p, %p)\n", physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties );
    p_vkGetPhysicalDeviceQueueFamilyProperties( physicalDevice, pQueueFamilyPropertyCount,
            pQueueFamilyProperties );
}

/***********************************************************************
 *              vkGetPhysicalDeviceSparseImageFormatProperties (VULKAN.@)
 */
void WINAPI vkGetPhysicalDeviceSparseImageFormatProperties( VkPhysicalDevice physicalDevice,
        VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage,
        VkImageTiling tiling, uint32_t *pPropertyCount, VkSparseImageFormatProperties *pProperties )
{
    TRACE( "(%p, %d, %d, %d, %u, %d, %p, %p)\n", physicalDevice, format, type, samples, usage,
            tiling, pPropertyCount, pProperties );
    p_vkGetPhysicalDeviceSparseImageFormatProperties( physicalDevice, format, type, samples, usage,
            tiling, pPropertyCount, pProperties );
}

/***********************************************************************
 *              vkGetPhysicalDeviceSurfaceCapabilitiesKHR (VULKAN.@)
 */
VkResult WINAPI vkGetPhysicalDeviceSurfaceCapabilitiesKHR( VkPhysicalDevice physicalDevice,
        VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *pSurfaceCapabilities )
{
    TRACE( "(%p, %s, %p)\n", physicalDevice, wine_dbgstr_longlong(surface), pSurfaceCapabilities );
    return p_vkGetPhysicalDeviceSurfaceCapabilitiesKHR( physicalDevice, surface,
            pSurfaceCapabilities );
}

/***********************************************************************
 *              vkGetPhysicalDeviceSurfaceFormatsKHR (VULKAN.@)
 */
VkResult WINAPI vkGetPhysicalDeviceSurfaceFormatsKHR( VkPhysicalDevice physicalDevice,
        VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount, VkSurfaceFormatKHR *pSurfaceFormats )
{
    TRACE( "(%p, %s, %p, %p)\n", physicalDevice, wine_dbgstr_longlong(surface),
            pSurfaceFormatCount, pSurfaceFormats );
    return p_vkGetPhysicalDeviceSurfaceFormatsKHR( physicalDevice, surface, pSurfaceFormatCount,
            pSurfaceFormats );
}

/***********************************************************************
 *              vkGetPhysicalDeviceSurfacePresentModesKHR (VULKAN.@)
 */
VkResult WINAPI vkGetPhysicalDeviceSurfacePresentModesKHR( VkPhysicalDevice physicalDevice,
        VkSurfaceKHR surface, uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes )
{
    TRACE( "(%p, %s, %p, %p)\n", physicalDevice, wine_dbgstr_longlong(surface), pPresentModeCount,
            pPresentModes );
    return p_vkGetPhysicalDeviceSurfacePresentModesKHR( physicalDevice, surface, pPresentModeCount,
            pPresentModes );
}

/***********************************************************************
 *              vkGetPhysicalDeviceSurfaceSupportKHR (VULKAN.@)
 */
VkResult WINAPI vkGetPhysicalDeviceSurfaceSupportKHR( VkPhysicalDevice physicalDevice,
        uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32 *pSupported )
{
    TRACE( "(%p, %u, %s, %p)\n", physicalDevice, queueFamilyIndex, wine_dbgstr_longlong(surface),
            pSupported );
    return p_vkGetPhysicalDeviceSurfaceSupportKHR( physicalDevice, queueFamilyIndex, surface,
            pSupported );
}

/***********************************************************************
 *              vkGetPipelineCacheData (VULKAN.@)
 */
VkResult WINAPI vkGetPipelineCacheData( VkDevice device, VkPipelineCache pipelineCache,
        size_t *pDataSize, void *pData )
{
    TRACE( "(%p, %s, %p, %p)\n", device, wine_dbgstr_longlong(pipelineCache), pDataSize, pData );
    return p_vkGetPipelineCacheData( device, pipelineCache, pDataSize, pData );
}

/***********************************************************************
 *              vkGetQueryPoolResults (VULKAN.@)
 */
VkResult WINAPI vkGetQueryPoolResults( VkDevice device, VkQueryPool queryPool, uint32_t firstQuery,
        uint32_t queryCount, size_t dataSize, void *pData, VkDeviceSize stride,
        VkQueryResultFlags flags )
{
    TRACE( "(%p, %s, %u, %u, %lu, %p, %s, %u)\n", device, wine_dbgstr_longlong(queryPool),
            firstQuery, queryCount, (SIZE_T)dataSize, pData, wine_dbgstr_longlong(stride), flags );
    return p_vkGetQueryPoolResults( device, queryPool, firstQuery, queryCount, dataSize, pData,
            stride, flags );
}

/***********************************************************************
 *              vkGetRenderAreaGranularity (VULKAN.@)
 */
void WINAPI vkGetRenderAreaGranularity( VkDevice device, VkRenderPass renderPass,
        VkExtent2D *pGranularity )
{
    TRACE( "(%p, %s, %p)\n", device, wine_dbgstr_longlong(renderPass), pGranularity );
    p_vkGetRenderAreaGranularity( device, renderPass, pGranularity );
}

/***********************************************************************
 *              vkGetSwapchainImagesKHR (VULKAN.@)
 */
VkResult WINAPI vkGetSwapchainImagesKHR( VkDevice device, VkSwapchainKHR swapchain,
        uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages )
{
    TRACE( "(%p, %s, %p, %p)\n", device, wine_dbgstr_longlong(swapchain), pSwapchainImageCount,
            pSwapchainImages );
    return p_vkGetSwapchainImagesKHR( device, swapchain, pSwapchainImageCount, pSwapchainImages );
}

/***********************************************************************
 *              vkInvalidateMappedMemoryRanges (VULKAN.@)
 */
VkResult WINAPI vkInvalidateMappedMemoryRanges( VkDevice device, uint32_t memoryRangeCount,
        const VkMappedMemoryRange *pMemoryRanges )
{
    TRACE( "(%p, %u, %p)\n", device, memoryRangeCount, pMemoryRanges );
    return p_vkInvalidateMappedMemoryRanges( device, memoryRangeCount, pMemoryRanges );
}

/***********************************************************************
 *              vkMapMemory (VULKAN.@)
 */
VkResult WINAPI vkMapMemory( VkDevice device, VkDeviceMemory memory, VkDeviceSize offset,
        VkDeviceSize size, VkMemoryMapFlags flags, void **ppData )
{
    TRACE( "(%p, %s, %s, %s, %u, %p)\n", device, wine_dbgstr_longlong(memory),
            wine_dbgstr_longlong(offset), wine_dbgstr_longlong(size), flags, ppData );
    return p_vkMapMemory( device, memory, offset, size, flags, ppData );
}

/***********************************************************************
 *              vkMergePipelineCaches (VULKAN.@)
 */
VkResult WINAPI vkMergePipelineCaches( VkDevice device, VkPipelineCache dstCache,
        uint32_t srcCacheCount, const VkPipelineCache *pSrcCaches )
{
    TRACE( "(%p, %s, %u, %p)\n", device, wine_dbgstr_longlong(dstCache), srcCacheCount,
            pSrcCaches );
    return p_vkMergePipelineCaches( device, dstCache, srcCacheCount, pSrcCaches );
}

/***********************************************************************
 *              vkQueueBindSparse (VULKAN.@)
 */
VkResult WINAPI vkQueueBindSparse( VkQueue queue, uint32_t bindInfoCount,
        const VkBindSparseInfo *pBindInfo, VkFence fence )
{
    VkBindSparseInfo_host *ptr_pBindInfo;
    VkResult res;

    TRACE( "(%p, %u, %p, %s)\n", queue, bindInfoCount, pBindInfo, wine_dbgstr_longlong(fence) );

    ptr_pBindInfo = convert_VkBindSparseInfo_array( pBindInfo, bindInfoCount );
    res = p_vkQueueBindSparse( queue, bindInfoCount, ptr_pBindInfo, fence );
    release_VkBindSparseInfo_array( NULL, ptr_pBindInfo, bindInfoCount );

    return res;
}

/***********************************************************************
 *              vkQueuePresentKHR (VULKAN.@)
 */
VkResult WINAPI vkQueuePresentKHR( VkQueue queue, const VkPresentInfoKHR *pPresentInfo )
{
    TRACE( "(%p, %p)\n", queue, pPresentInfo );
    return p_vkQueuePresentKHR( queue, pPresentInfo );
}

/***********************************************************************
 *              vkQueueSubmit (VULKAN.@)
 */
VkResult WINAPI vkQueueSubmit( VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits,
        VkFence fence )
{
    TRACE( "(%p, %u, %p, %s)\n", queue, submitCount, pSubmits, wine_dbgstr_longlong(fence) );
    return p_vkQueueSubmit( queue, submitCount, pSubmits, fence );
}

/***********************************************************************
 *              vkQueueWaitIdle (VULKAN.@)
 */
VkResult WINAPI vkQueueWaitIdle( VkQueue queue )
{
    TRACE( "(%p)\n", queue );
    return p_vkQueueWaitIdle( queue );
}

/***********************************************************************
 *              vkResetCommandBuffer (VULKAN.@)
 */
VkResult WINAPI vkResetCommandBuffer( VkCommandBuffer commandBuffer,
        VkCommandBufferResetFlags flags )
{
    TRACE( "(%p, %u)\n", commandBuffer, flags );
    return p_vkResetCommandBuffer( commandBuffer, flags );
}

/***********************************************************************
 *              vkResetCommandPool (VULKAN.@)
 */
VkResult WINAPI vkResetCommandPool( VkDevice device, VkCommandPool commandPool,
        VkCommandPoolResetFlags flags )
{
    TRACE( "(%p, %s, %u)\n", device, wine_dbgstr_longlong(commandPool), flags );
    return p_vkResetCommandPool( device, commandPool, flags );
}

/***********************************************************************
 *              vkResetDescriptorPool (VULKAN.@)
 */
VkResult WINAPI vkResetDescriptorPool( VkDevice device, VkDescriptorPool descriptorPool,
        VkDescriptorPoolResetFlags flags )
{
    TRACE( "(%p, %s, %u)\n", device, wine_dbgstr_longlong(descriptorPool), flags );
    return p_vkResetDescriptorPool( device, descriptorPool, flags );
}

/***********************************************************************
 *              vkResetEvent (VULKAN.@)
 */
VkResult WINAPI vkResetEvent( VkDevice device, VkEvent event )
{
    TRACE( "(%p, %s)\n", device, wine_dbgstr_longlong(event) );
    return p_vkResetEvent( device, event );
}

/***********************************************************************
 *              vkResetFences (VULKAN.@)
 */
VkResult WINAPI vkResetFences( VkDevice device, uint32_t fenceCount, const VkFence *pFences )
{
    TRACE( "(%p, %u, %p)\n", device, fenceCount, pFences );
    return p_vkResetFences( device, fenceCount, pFences );
}

/***********************************************************************
 *              vkSetEvent (VULKAN.@)
 */
VkResult WINAPI vkSetEvent( VkDevice device, VkEvent event )
{
    TRACE( "(%p, %s)\n", device, wine_dbgstr_longlong(event) );
    return p_vkSetEvent( device, event );
}

/***********************************************************************
 *              vkUnmapMemory (VULKAN.@)
 */
void WINAPI vkUnmapMemory( VkDevice device, VkDeviceMemory memory )
{
    TRACE( "(%p, %s)\n", device, wine_dbgstr_longlong(memory) );
    p_vkUnmapMemory( device, memory );
}

/***********************************************************************
 *              vkUpdateDescriptorSets (VULKAN.@)
 */
void WINAPI vkUpdateDescriptorSets( VkDevice device, uint32_t descriptorWriteCount,
        const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount,
        const VkCopyDescriptorSet *pDescriptorCopies )
{
    VkWriteDescriptorSet_host *ptr_pDescriptorWrites;
    VkCopyDescriptorSet_host *ptr_pDescriptorCopies;

    TRACE( "(%p, %u, %p, %u, %p)\n", device, descriptorWriteCount, pDescriptorWrites,
            descriptorCopyCount, pDescriptorCopies );

    ptr_pDescriptorWrites = convert_VkWriteDescriptorSet_array( pDescriptorWrites,
            descriptorWriteCount );
    ptr_pDescriptorCopies = convert_VkCopyDescriptorSet_array( pDescriptorCopies,
            descriptorCopyCount );
    p_vkUpdateDescriptorSets( device, descriptorWriteCount, ptr_pDescriptorWrites,
            descriptorCopyCount, ptr_pDescriptorCopies );
    release_VkCopyDescriptorSet_array( NULL, ptr_pDescriptorCopies, descriptorCopyCount );
    release_VkWriteDescriptorSet_array( NULL, ptr_pDescriptorWrites, descriptorWriteCount );
}

/***********************************************************************
 *              vkWaitForFences (VULKAN.@)
 */
VkResult WINAPI vkWaitForFences( VkDevice device, uint32_t fenceCount, const VkFence *pFences,
        VkBool32 waitAll, uint64_t timeout )
{
    TRACE( "(%p, %u, %p, %u, %s)\n", device, fenceCount, pFences, waitAll,
            wine_dbgstr_longlong(timeout) );
    return p_vkWaitForFences( device, fenceCount, pFences, waitAll, timeout );
}

struct function_entry
{
    const char *name;
    void **host_func;
    void *null_func;
};

static const struct function_entry function_table[] =
{
#define DEFINE_FUNCTION( f ) { #f, (void **)&p_##f, null_##f },
    /* functions must be sorted alphabetically */
    DEFINE_FUNCTION( vkAcquireNextImageKHR )
    DEFINE_FUNCTION( vkAllocateCommandBuffers )
    DEFINE_FUNCTION( vkAllocateDescriptorSets )
    DEFINE_FUNCTION( vkAllocateMemory )
    DEFINE_FUNCTION( vkBeginCommandBuffer )
    DEFINE_FUNCTION( vkBindBufferMemory )
    DEFINE_FUNCTION( vkBindImageMemory )
    DEFINE_FUNCTION( vkCmdBeginQuery )
    DEFINE_FUNCTION( vkCmdBeginRenderPass )
    DEFINE_FUNCTION( vkCmdBindDescriptorSets )
    DEFINE_FUNCTION( vkCmdBindIndexBuffer )
    DEFINE_FUNCTION( vkCmdBindPipeline )
    DEFINE_FUNCTION( vkCmdBindVertexBuffers )
    DEFINE_FUNCTION( vkCmdBlitImage )
    DEFINE_FUNCTION( vkCmdClearAttachments )
    DEFINE_FUNCTION( vkCmdClearColorImage )
    DEFINE_FUNCTION( vkCmdClearDepthStencilImage )
    DEFINE_FUNCTION( vkCmdCopyBuffer )
    DEFINE_FUNCTION( vkCmdCopyBufferToImage )
    DEFINE_FUNCTION( vkCmdCopyImage )
    DEFINE_FUNCTION( vkCmdCopyImageToBuffer )
    DEFINE_FUNCTION( vkCmdCopyQueryPoolResults )
    DEFINE_FUNCTION( vkCmdDispatch )
    DEFINE_FUNCTION( vkCmdDispatchIndirect )
    DEFINE_FUNCTION( vkCmdDraw )
    DEFINE_FUNCTION( vkCmdDrawIndexed )
    DEFINE_FUNCTION( vkCmdDrawIndexedIndirect )
    DEFINE_FUNCTION( vkCmdDrawIndexedIndirectCountAMD )
    DEFINE_FUNCTION( vkCmdDrawIndirect )
    DEFINE_FUNCTION( vkCmdDrawIndirectCountAMD )
    DEFINE_FUNCTION( vkCmdEndQuery )
    DEFINE_FUNCTION( vkCmdEndRenderPass )
    DEFINE_FUNCTION( vkCmdExecuteCommands )
    DEFINE_FUNCTION( vkCmdFillBuffer )
    DEFINE_FUNCTION( vkCmdNextSubpass )
    DEFINE_FUNCTION( vkCmdPipelineBarrier )
    DEFINE_FUNCTION( vkCmdPushConstants )
    DEFINE_FUNCTION( vkCmdResetEvent )
    DEFINE_FUNCTION( vkCmdResetQueryPool )
    DEFINE_FUNCTION( vkCmdResolveImage )
    DEFINE_FUNCTION( vkCmdSetBlendConstants )
    DEFINE_FUNCTION( vkCmdSetDepthBias )
    DEFINE_FUNCTION( vkCmdSetDepthBounds )
    DEFINE_FUNCTION( vkCmdSetEvent )
    DEFINE_FUNCTION( vkCmdSetLineWidth )
    DEFINE_FUNCTION( vkCmdSetScissor )
    DEFINE_FUNCTION( vkCmdSetStencilCompareMask )
    DEFINE_FUNCTION( vkCmdSetStencilReference )
    DEFINE_FUNCTION( vkCmdSetStencilWriteMask )
    DEFINE_FUNCTION( vkCmdSetViewport )
    DEFINE_FUNCTION( vkCmdUpdateBuffer )
    DEFINE_FUNCTION( vkCmdWaitEvents )
    DEFINE_FUNCTION( vkCmdWriteTimestamp )
    DEFINE_FUNCTION( vkCreateBuffer )
    DEFINE_FUNCTION( vkCreateBufferView )
    DEFINE_FUNCTION( vkCreateCommandPool )
    DEFINE_FUNCTION( vkCreateComputePipelines )
    DEFINE_FUNCTION( vkCreateDebugReportCallbackEXT )
    DEFINE_FUNCTION( vkCreateDescriptorPool )
    DEFINE_FUNCTION( vkCreateDescriptorSetLayout )
    DEFINE_FUNCTION( vkCreateDevice )
    DEFINE_FUNCTION( vkCreateDisplayModeKHR )
    DEFINE_FUNCTION( vkCreateDisplayPlaneSurfaceKHR )
    DEFINE_FUNCTION( vkCreateEvent )
    DEFINE_FUNCTION( vkCreateFence )
    DEFINE_FUNCTION( vkCreateFramebuffer )
    DEFINE_FUNCTION( vkCreateGraphicsPipelines )
    DEFINE_FUNCTION( vkCreateImage )
    DEFINE_FUNCTION( vkCreateImageView )
    DEFINE_FUNCTION( vkCreateInstance )
    DEFINE_FUNCTION( vkCreatePipelineCache )
    DEFINE_FUNCTION( vkCreatePipelineLayout )
    DEFINE_FUNCTION( vkCreateQueryPool )
    DEFINE_FUNCTION( vkCreateRenderPass )
    DEFINE_FUNCTION( vkCreateSampler )
    DEFINE_FUNCTION( vkCreateSemaphore )
    DEFINE_FUNCTION( vkCreateShaderModule )
    DEFINE_FUNCTION( vkCreateSharedSwapchainsKHR )
    DEFINE_FUNCTION( vkCreateSwapchainKHR )
    DEFINE_FUNCTION( vkCreateXcbSurfaceKHR )
    DEFINE_FUNCTION( vkCreateXlibSurfaceKHR )
    DEFINE_FUNCTION( vkDebugReportMessageEXT )
    DEFINE_FUNCTION( vkDestroyBuffer )
    DEFINE_FUNCTION( vkDestroyBufferView )
    DEFINE_FUNCTION( vkDestroyCommandPool )
    DEFINE_FUNCTION( vkDestroyDebugReportCallbackEXT )
    DEFINE_FUNCTION( vkDestroyDescriptorPool )
    DEFINE_FUNCTION( vkDestroyDescriptorSetLayout )
    DEFINE_FUNCTION( vkDestroyDevice )
    DEFINE_FUNCTION( vkDestroyEvent )
    DEFINE_FUNCTION( vkDestroyFence )
    DEFINE_FUNCTION( vkDestroyFramebuffer )
    DEFINE_FUNCTION( vkDestroyImage )
    DEFINE_FUNCTION( vkDestroyImageView )
    DEFINE_FUNCTION( vkDestroyInstance )
    DEFINE_FUNCTION( vkDestroyPipeline )
    DEFINE_FUNCTION( vkDestroyPipelineCache )
    DEFINE_FUNCTION( vkDestroyPipelineLayout )
    DEFINE_FUNCTION( vkDestroyQueryPool )
    DEFINE_FUNCTION( vkDestroyRenderPass )
    DEFINE_FUNCTION( vkDestroySampler )
    DEFINE_FUNCTION( vkDestroySemaphore )
    DEFINE_FUNCTION( vkDestroyShaderModule )
    DEFINE_FUNCTION( vkDestroySurfaceKHR )
    DEFINE_FUNCTION( vkDestroySwapchainKHR )
    DEFINE_FUNCTION( vkDeviceWaitIdle )
    DEFINE_FUNCTION( vkEndCommandBuffer )
    DEFINE_FUNCTION( vkEnumerateDeviceExtensionProperties )
    DEFINE_FUNCTION( vkEnumerateDeviceLayerProperties )
    DEFINE_FUNCTION( vkEnumerateInstanceExtensionProperties )
    DEFINE_FUNCTION( vkEnumerateInstanceLayerProperties )
    DEFINE_FUNCTION( vkEnumeratePhysicalDevices )
    DEFINE_FUNCTION( vkFlushMappedMemoryRanges )
    DEFINE_FUNCTION( vkFreeCommandBuffers )
    DEFINE_FUNCTION( vkFreeDescriptorSets )
    DEFINE_FUNCTION( vkFreeMemory )
    DEFINE_FUNCTION( vkGetBufferMemoryRequirements )
    DEFINE_FUNCTION( vkGetDeviceMemoryCommitment )
    DEFINE_FUNCTION( vkGetDeviceProcAddr )
    DEFINE_FUNCTION( vkGetDeviceQueue )
    DEFINE_FUNCTION( vkGetDisplayModePropertiesKHR )
    DEFINE_FUNCTION( vkGetDisplayPlaneCapabilitiesKHR )
    DEFINE_FUNCTION( vkGetDisplayPlaneSupportedDisplaysKHR )
    DEFINE_FUNCTION( vkGetEventStatus )
    DEFINE_FUNCTION( vkGetFenceStatus )
    DEFINE_FUNCTION( vkGetImageMemoryRequirements )
    DEFINE_FUNCTION( vkGetImageSparseMemoryRequirements )
    DEFINE_FUNCTION( vkGetImageSubresourceLayout )
    DEFINE_FUNCTION( vkGetInstanceProcAddr )
    DEFINE_FUNCTION( vkGetPhysicalDeviceDisplayPlanePropertiesKHR )
    DEFINE_FUNCTION( vkGetPhysicalDeviceDisplayPropertiesKHR )
    DEFINE_FUNCTION( vkGetPhysicalDeviceExternalImageFormatPropertiesNV )
    DEFINE_FUNCTION( vkGetPhysicalDeviceFeatures )
    DEFINE_FUNCTION( vkGetPhysicalDeviceFormatProperties )
    DEFINE_FUNCTION( vkGetPhysicalDeviceImageFormatProperties )
    DEFINE_FUNCTION( vkGetPhysicalDeviceMemoryProperties )
    DEFINE_FUNCTION( vkGetPhysicalDeviceProperties )
    DEFINE_FUNCTION( vkGetPhysicalDeviceQueueFamilyProperties )
    DEFINE_FUNCTION( vkGetPhysicalDeviceSparseImageFormatProperties )
    DEFINE_FUNCTION( vkGetPhysicalDeviceSurfaceCapabilitiesKHR )
    DEFINE_FUNCTION( vkGetPhysicalDeviceSurfaceFormatsKHR )
    DEFINE_FUNCTION( vkGetPhysicalDeviceSurfacePresentModesKHR )
    DEFINE_FUNCTION( vkGetPhysicalDeviceSurfaceSupportKHR )
    DEFINE_FUNCTION( vkGetPhysicalDeviceXcbPresentationSupportKHR )
    DEFINE_FUNCTION( vkGetPhysicalDeviceXlibPresentationSupportKHR )
    DEFINE_FUNCTION( vkGetPipelineCacheData )
    DEFINE_FUNCTION( vkGetQueryPoolResults )
    DEFINE_FUNCTION( vkGetRenderAreaGranularity )
    DEFINE_FUNCTION( vkGetSwapchainImagesKHR )
    DEFINE_FUNCTION( vkInvalidateMappedMemoryRanges )
    DEFINE_FUNCTION( vkMapMemory )
    DEFINE_FUNCTION( vkMergePipelineCaches )
    DEFINE_FUNCTION( vkQueueBindSparse )
    DEFINE_FUNCTION( vkQueuePresentKHR )
    DEFINE_FUNCTION( vkQueueSubmit )
    DEFINE_FUNCTION( vkQueueWaitIdle )
    DEFINE_FUNCTION( vkResetCommandBuffer )
    DEFINE_FUNCTION( vkResetCommandPool )
    DEFINE_FUNCTION( vkResetDescriptorPool )
    DEFINE_FUNCTION( vkResetEvent )
    DEFINE_FUNCTION( vkResetFences )
    DEFINE_FUNCTION( vkSetEvent )
    DEFINE_FUNCTION( vkUnmapMemory )
    DEFINE_FUNCTION( vkUpdateDescriptorSets )
    DEFINE_FUNCTION( vkWaitForFences )
#undef DEFINE_FUNCTION
};

static void *libvulkan_handle;

BOOL init_vulkan( void )
{
    static const char *libname[] =
    {
        "libvulkan.so.1",
        "libvulkan.so",
    };
    void *ptr;
    int i;

    if (!(function_heap = HeapCreate( HEAP_CREATE_ENABLE_EXECUTE, 0, 0 )))
        return FALSE;

    for (i = 0; i < ARRAY_SIZE(libname); i++)
    {
        libvulkan_handle = wine_dlopen( libname[i], RTLD_NOW, NULL, 0 );
        if (libvulkan_handle) break;
    }

    if (!libvulkan_handle)
    {
        ERR_(winediag)( "failed to load libvulkan.so, no support for vulkan\n" );
        HeapDestroy( function_heap );
        return FALSE;
    }

    for (i = 0; i < ARRAY_SIZE(function_table); i++)
    {
        if ((ptr = wine_dlsym( libvulkan_handle, function_table[i].name, NULL, 0 )))
            *function_table[i].host_func = ptr;
        else
            WARN( "failed to load %s\n", function_table[i].name );
    }

    return TRUE;
}

static int compare_function_entry( const void *a, const void *b )
{
    return strcmp( ((struct function_entry *)a)->name, ((struct function_entry *)b)->name );
}

BOOL is_null_func( const char *name )
{
    struct function_entry search = { name, NULL, NULL };
    struct function_entry *found;

    found = bsearch( &search, function_table, ARRAY_SIZE(function_table),
            sizeof(function_table[0]), compare_function_entry );

    return found ? (*found->host_func == found->null_func) : FALSE;
}

void free_vulkan( void )
{
    if (!libvulkan_handle) return;
    HeapDestroy( function_heap );
    wine_dlclose( libvulkan_handle, NULL, 0 );
    libvulkan_handle = NULL;
}
