/*
 * 32-bit spec files
 *
 * Copyright 1993 Robert J. Amstadt
 * Copyright 1995 Martin von Loewis
 * Copyright 1995, 1996, 1997 Alexandre Julliard
 * Copyright 1997 Eric Youngdale
 * Copyright 1999 Ulrich Weigand
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
#include <ctype.h>
#include <stdarg.h>
#include <string.h>

#include "build.h"

#define IMAGE_FILE_MACHINE_UNKNOWN 0
#define IMAGE_FILE_MACHINE_I386    0x014c
#define IMAGE_FILE_MACHINE_POWERPC 0x01f0
#define IMAGE_FILE_MACHINE_AMD64   0x8664
#define IMAGE_FILE_MACHINE_ARMNT   0x01C4
#define IMAGE_FILE_MACHINE_ARM64   0xaa64

#define IMAGE_SIZEOF_NT_OPTIONAL32_HEADER 224
#define IMAGE_SIZEOF_NT_OPTIONAL64_HEADER 240

#define IMAGE_NT_OPTIONAL_HDR32_MAGIC 0x10b
#define IMAGE_NT_OPTIONAL_HDR64_MAGIC 0x20b
#define IMAGE_ROM_OPTIONAL_HDR_MAGIC  0x107

int needs_get_pc_thunk = 0;

/* check if entry point needs a relay thunk */
static inline int needs_relay( const ORDDEF *odp )
{
    /* skip nonexistent entry points */
    if (!odp) return 0;
    /* skip non-functions */
    switch (odp->type)
    {
    case TYPE_STDCALL:
    case TYPE_CDECL:
    case TYPE_THISCALL:
        break;
    case TYPE_STUB:
        if (odp->u.func.nb_args != -1) break;
        /* fall through */
    default:
        return 0;
    }
    /* skip norelay and forward entry points */
    if (odp->flags & (FLAG_NORELAY|FLAG_FORWARD)) return 0;
    return 1;
}

static int is_float_arg( const ORDDEF *odp, int arg )
{
    if (arg >= odp->u.func.nb_args) return 0;
    return (odp->u.func.args[arg] == ARG_FLOAT || odp->u.func.args[arg] == ARG_DOUBLE);
}

/* check if dll will output relay thunks */
static int has_relays( DLLSPEC *spec )
{
    int i;

    if (target_cpu != CPU_x86 && target_cpu != CPU_x86_64 &&
        target_cpu != CPU_ARM && target_cpu != CPU_ARM64)
        return 0;

    for (i = spec->base; i <= spec->limit; i++)
    {
        ORDDEF *odp = spec->ordinals[i];
        if (needs_relay( odp )) return 1;
    }
    return 0;
}

/*******************************************************************
 *         output_relay_debug
 *
 * Output entry points for relay debugging
 */
static void output_relay_debug( DLLSPEC *spec )
{
    int i, j;
    unsigned int pos, args, flags;

    /* first the table of entry point offsets */

    output( "\t%s\n", get_asm_rodata_section() );
    output( "\t.align %d\n", get_alignment(4) );
    output( ".L__wine_spec_relay_entry_point_offsets:\n" );

    for (i = spec->base; i <= spec->limit; i++)
    {
        ORDDEF *odp = spec->ordinals[i];

        if (needs_relay( odp ))
            output( "\t.long .L__wine_spec_relay_entry_point_%d-__wine_spec_relay_entry_points\n", i );
        else
            output( "\t.long 0\n" );
    }

    /* then the table of argument types */

    output( "\t.align %d\n", get_alignment(4) );
    output( ".L__wine_spec_relay_arg_types:\n" );

    for (i = spec->base; i <= spec->limit; i++)
    {
        ORDDEF *odp = spec->ordinals[i];
        unsigned int mask = 0;

        if (needs_relay( odp ))
        {
            for (j = pos = 0; pos < 16 && j < odp->u.func.nb_args; j++)
            {
                switch (odp->u.func.args[j])
                {
                case ARG_STR:    mask |= 1 << (2 * pos++); break;
                case ARG_WSTR:   mask |= 2 << (2 * pos++); break;
                case ARG_INT64:
                case ARG_DOUBLE: pos += 8 / get_ptr_size(); break;
                case ARG_INT128: pos += (target_cpu == CPU_x86) ? 4 : 1; break;
                default:         pos++; break;
                }
            }
        }
        output( "\t.long 0x%08x\n", mask );
    }

    /* then the relay thunks */

    output( "\t.text\n" );
    output( "__wine_spec_relay_entry_points:\n" );
    output( "\tnop\n" );  /* to avoid 0 offset */

    for (i = spec->base; i <= spec->limit; i++)
    {
        ORDDEF *odp = spec->ordinals[i];

        if (!needs_relay( odp )) continue;

        output( "\t.align %d\n", get_alignment(4) );
        output( ".L__wine_spec_relay_entry_point_%d:\n", i );
        output_cfi( ".cfi_startproc" );

        args = get_args_size(odp) / get_ptr_size();
        flags = 0;

        switch (target_cpu)
        {
        case CPU_x86:
            if (odp->type == TYPE_THISCALL)  /* add the this pointer */
            {
                output( "\tpopl %%eax\n" );
                output( "\tpushl %%ecx\n" );
                output( "\tpushl %%eax\n" );
                flags |= 2;
            }
            output( "\tpushl %%esp\n" );
            output_cfi( ".cfi_adjust_cfa_offset 4" );

            if (odp->flags & FLAG_RET64) flags |= 1;
            output( "\tpushl $%u\n", (flags << 24) | (args << 16) | (i - spec->base) );
            output_cfi( ".cfi_adjust_cfa_offset 4" );

            if (UsePIC)
            {
                output( "\tcall %s\n", asm_name("__wine_spec_get_pc_thunk_eax") );
                output( "1:\tleal .L__wine_spec_relay_descr-1b(%%eax),%%eax\n" );
                needs_get_pc_thunk = 1;
            }
            else output( "\tmovl $.L__wine_spec_relay_descr,%%eax\n" );
            output( "\tpushl %%eax\n" );
            output_cfi( ".cfi_adjust_cfa_offset 4" );

            output( "\tcall *4(%%eax)\n" );
            output_cfi( ".cfi_adjust_cfa_offset -12" );
            if (odp->type == TYPE_STDCALL || odp->type == TYPE_THISCALL)
                output( "\tret $%u\n", args * get_ptr_size() );
            else
                output( "\tret\n" );
            break;

        case CPU_ARM:
        {
            unsigned int mask, val, count = 0;
            unsigned int stack_size = min( 16, (args * 4 + 7) & ~7 );

            if (odp->flags & FLAG_RET64) flags |= 1;
            val = (flags << 24) | (args << 16) | (i - spec->base);
            switch (stack_size)
            {
            case 16: output( "\tpush {r0-r3}\n" ); break;
            case 8:  output( "\tpush {r0-r1}\n" ); break;
            case 0:  break;
            }
            output( "\tpush {LR}\n" );
            output( "\tmov r2, SP\n");
            output( "\tsub SP, #4\n");
            for (mask = 0xff; mask; mask <<= 8)
                if (val & mask) output( "\t%s r1,#%u\n", count++ ? "add" : "mov", val & mask );
            if (!count) output( "\tmov r1,#0\n" );
            output( "\tldr r0, 2f\n");
            output( "\tadd r0, PC\n");
            output( "\tldr IP, [r0, #4]\n");
            output( "1:\tblx IP\n");
            output( "\tldr IP, [SP, #4]\n" );
            output( "\tadd SP, #%u\n", stack_size + 8 );
            output( "\tbx IP\n");
            output( "2:\t.long .L__wine_spec_relay_descr-1b\n" );
            break;
        }

        case CPU_ARM64:
            switch (args)
            {
            default:
            case 8:
            case 7:  output( "\tstp x6, x7, [SP,#-16]!\n" );
            /* fall through */
            case 6:
            case 5:  output( "\tstp x4, x5, [SP,#-16]!\n" );
            /* fall through */
            case 4:
            case 3:  output( "\tstp x2, x3, [SP,#-16]!\n" );
            /* fall through */
            case 2:
            case 1:  output( "\tstp x0, x1, [SP,#-16]!\n" );
            /* fall through */
            case 0:  break;
            }
            output( "\tstp x29, x30, [SP,#-16]!\n" );
            output( "\tstp x8, x9, [SP,#-16]!\n" );
            output( "\tmov x2, SP\n");
            if (odp->flags & FLAG_RET64) flags |= 1;
            output( "\tmov w1, #%u\n", (flags << 24) );
            if (args) output( "\tadd w1, w1, #%u\n", (args << 16) );
            if (i - spec->base) output( "\tadd w1, w1, #%u\n", i - spec->base );
            output( "\tadrp x0, .L__wine_spec_relay_descr\n");
            output( "\tadd x0, x0, #:lo12:.L__wine_spec_relay_descr\n");
            output( "\tldr x3, [x0, #8]\n");
            output( "\tblr x3\n");
            output( "\tadd SP, SP, #16\n" );
            output( "\tldp x29, x30, [SP], #16\n" );
            if (args) output( "\tadd SP, SP, #%u\n", 8 * ((min(args, 8) + 1) & 0xe) );
            output( "\tret\n");
            break;

        case CPU_x86_64:
            output( "\tsubq $40,%%rsp\n" );
            output_cfi( ".cfi_adjust_cfa_offset 40" );
            switch (args)
            {
            default: output( "\tmovq %%%s,72(%%rsp)\n", is_float_arg( odp, 3 ) ? "xmm3" : "r9" );
            /* fall through */
            case 3:  output( "\tmovq %%%s,64(%%rsp)\n", is_float_arg( odp, 2 ) ? "xmm2" : "r8" );
            /* fall through */
            case 2:  output( "\tmovq %%%s,56(%%rsp)\n", is_float_arg( odp, 1 ) ? "xmm1" : "rdx" );
            /* fall through */
            case 1:  output( "\tmovq %%%s,48(%%rsp)\n", is_float_arg( odp, 0 ) ? "xmm0" : "rcx" );
            /* fall through */
            case 0:  break;
            }
            output( "\tleaq 40(%%rsp),%%r8\n" );
            output( "\tmovq $%u,%%rdx\n", (flags << 24) | (args << 16) | (i - spec->base) );
            output( "\tleaq .L__wine_spec_relay_descr(%%rip),%%rcx\n" );
            output( "\tcallq *8(%%rcx)\n" );
            output( "\taddq $40,%%rsp\n" );
            output_cfi( ".cfi_adjust_cfa_offset -40" );
            output( "\tret\n" );
            break;

        default:
            assert(0);
        }
        output_cfi( ".cfi_endproc" );
    }
}

/*******************************************************************
 *         output_syscall_thunks_x86
 *
 * Output entry points for system call functions
 */
static void output_syscall_thunks_x86( DLLSPEC *spec )
{
    const unsigned int page_size = get_page_size();
    int i;

    if (!spec->nb_syscalls)
        return;

    /* Reserve space for PE header directly before syscalls. */
    if (target_platform == PLATFORM_APPLE)
        output( "\t.text\n" );
    else
        output( "\n\t.section \".text.startup\"\n" );

    output( "\t.align %d\n", get_alignment(65536) );
    output( "__wine_spec_pe_header_syscalls:\n" );
    output( "\t.byte 0\n" );
    output( "\t.balign %d, 0\n", page_size );

    output( "\n/* syscall thunks */\n\n" );
    for (i = 0; i < spec->nb_syscalls; i++)
    {
        ORDDEF *odp = spec->syscalls[i];
        const char *name = odp->link_name;

        output( "\t.balign 16, 0\n" );
        output( "\t%s\n", func_declaration(name) );
        output( "%s\n", asm_globl(name) );
        output_cfi( ".cfi_startproc" );
        output( "\t.byte 0xb8\n" );                               /* mov eax, SYSCALL */
        output( "\t.long %d\n", i );
        output( "\t.byte 0x33,0xc9\n" );                          /* xor ecx, ecx */
        output( "\t.byte 0x8d,0x54,0x24,0x04\n" );                /* lea edx, [esp + 4] */
        output( "\t.byte 0x64,0xff,0x15,0xc0,0x00,0x00,0x00\n" ); /* call dword ptr fs:[0C0h] */
        output( "\t.byte 0x83,0xc4,0x04\n" );                     /* add esp, 4 */
        output( "\t.byte 0xc2\n" );                               /* ret X */
        output( "\t.short %d\n", get_args_size(odp) );
        output_cfi( ".cfi_endproc" );
        output_function_size( name );
    }

    for (i = 0; i < 0x20; i++)
        output( "\t.byte 0\n" );

    output( "\n/* syscall table */\n\n" );
    output( "\t.data\n" );
    output( "%s\n", asm_globl("__wine_syscall_table") );
    for (i = 0; i < spec->nb_syscalls; i++)
    {
        ORDDEF *odp = spec->syscalls[i];
        output ("\t%s %s\n", get_asm_ptr_keyword(), asm_name(odp->impl_name) );
    }

    output( "\n/* syscall dispatcher */\n\n" );
    output( "\t.text\n" );
    output( "\t.align %d\n", get_alignment(16) );
    output( "\t%s\n", func_declaration("__wine_syscall_dispatcher") );
    output( "%s\n", asm_globl("__wine_syscall_dispatcher") );
    output_cfi( ".cfi_startproc" );
    output( "\tadd $4, %%esp\n" );
    if (UsePIC)
    {
        output( "\tcall 1f\n" );
        output( "1:\tpopl %%ecx\n" );
        output( "\tjmpl *(%s-1b)(%%ecx,%%eax,%d)\n", asm_name("__wine_syscall_table"), get_ptr_size() );
    }
    else output( "\tjmpl *%s(,%%eax,%d)\n", asm_name("__wine_syscall_table"), get_ptr_size() );
    output( "\tret\n" );
    output_cfi( ".cfi_endproc" );
    output_function_size( "__wine_syscall_dispatcher" );
}

/*******************************************************************
 *         output_syscall_thunks_x64
 *
 * Output entry points for system call functions
 */
static void output_syscall_thunks_x64( DLLSPEC *spec )
{
    const unsigned int page_size = get_page_size();
    int i;

    if (!spec->nb_syscalls)
        return;

    /* Reserve space for PE header directly before syscalls. */
    if (target_platform == PLATFORM_APPLE)
        output( "\t.text\n" );
    else
        output( "\n\t.section \".text.startup\"\n" );

    output( "\t.align %d\n", get_alignment(65536) );
    output( "__wine_spec_pe_header_syscalls:\n" );
    output( "\t.byte 0\n" );
    output( "\t.balign %d, 0\n", page_size );

    output( "\n/* syscall thunks */\n\n" );
    for (i = 0; i < spec->nb_syscalls; i++)
    {
        ORDDEF *odp = spec->syscalls[i];
        const char *name = odp->link_name;

        output( "\t.balign 16, 0\n" );
        output( "\t%s\n", func_declaration(name) );
        output( "%s\n", asm_globl(name) );
        output_cfi( ".cfi_startproc" );
        output( "\t.byte 0xb8\n" );                                         /* mov eax, SYSCALL */
        output( "\t.long %d\n", i );
        if (target_platform == PLATFORM_APPLE)
        {
            output( "\t.byte 0xff,0x14,0x25\n" );                           /* call [0x7ffe1000] */
            output( "\t.long 0x7ffe1000\n" );
        }
        else
        {
            output( "\t.byte 0x65,0xff,0x14,0x25\n" );                      /* call qword ptr gs:[0x100] */
            output( "\t.long 0x100\n");
        }
        output( "\t.byte 0xc3\n" );                                        /* ret */
        output_cfi( ".cfi_endproc" );
        output_function_size( name );
    }

    for (i = 0; i < 0x20; i++)
        output( "\t.byte 0\n" );

    output( "\n/* syscall table */\n\n" );
    output( "\t.data\n" );
    output( "%s\n", asm_globl("__wine_syscall_table") );
    for (i = 0; i < spec->nb_syscalls; i++)
    {
        ORDDEF *odp = spec->syscalls[i];
        output ("\t%s %s\n", get_asm_ptr_keyword(), asm_name(odp->impl_name) );
    }

    output( "\n/* syscall dispatcher */\n\n" );
    output( "\t.text\n" );
    output( "\t.align %d\n", get_alignment(16) );
    output( "\t%s\n", func_declaration("__wine_syscall_dispatcher") );
    output( "%s\n", asm_globl("__wine_syscall_dispatcher") );
    output_cfi( ".cfi_startproc" );
    output( "\tadd $8, %%rsp\n" );
    output_cfi( ".cfi_adjust_cfa_offset -8" );
    output( "\tmovq $0xffffffff, %%r10\n" );
    output( "\tandq %%r10, %%rax\n" );
    if (UsePIC)
    {
        output( "\tleaq (%%rip), %%r10\n" );
        output( "1:\tjmpq *(%s-1b)(%%r10,%%rax,%d)\n", asm_name("__wine_syscall_table"), get_ptr_size() );
    }
    else output( "\tjmpq *%s(,%%rax,%d)\n", asm_name("__wine_syscall_table"), get_ptr_size() );
    output( "\tret\n" );
    output_cfi( ".cfi_endproc" );
    output_function_size( "__wine_syscall_dispatcher" );
}

/*******************************************************************
 *         output_exports
 *
 * Output the export table for a Win32 module.
 */
void output_exports( DLLSPEC *spec )
{
    int i, fwd_size = 0;
    int nr_exports = spec->base <= spec->limit ? spec->limit - spec->base + 1 : 0;

    if (!nr_exports) return;

    output( "\n/* export table */\n\n" );
    output( "\t.data\n" );
    output( "\t.align %d\n", get_alignment(4) );
    output( ".L__wine_spec_exports:\n" );

    /* export directory header */

    output( "\t.long 0\n" );                       /* Characteristics */
    output( "\t.long 0\n" );                       /* TimeDateStamp */
    output( "\t.long 0\n" );                       /* MajorVersion/MinorVersion */
    output( "\t.long .L__wine_spec_exp_names-.L__wine_spec_rva_base\n" ); /* Name */
    output( "\t.long %u\n", spec->base );          /* Base */
    output( "\t.long %u\n", nr_exports );          /* NumberOfFunctions */
    output( "\t.long %u\n", spec->nb_names );      /* NumberOfNames */
    output( "\t.long .L__wine_spec_exports_funcs-.L__wine_spec_rva_base\n" ); /* AddressOfFunctions */
    if (spec->nb_names)
    {
        output( "\t.long .L__wine_spec_exp_name_ptrs-.L__wine_spec_rva_base\n" ); /* AddressOfNames */
        output( "\t.long .L__wine_spec_exp_ordinals-.L__wine_spec_rva_base\n" );  /* AddressOfNameOrdinals */
    }
    else
    {
        output( "\t.long 0\n" );  /* AddressOfNames */
        output( "\t.long 0\n" );  /* AddressOfNameOrdinals */
    }

    /* output the function pointers */

    output( "\n.L__wine_spec_exports_funcs:\n" );
    for (i = spec->base; i <= spec->limit; i++)
    {
        ORDDEF *odp = spec->ordinals[i];
        if (!odp) output( "\t%s 0\n", get_asm_ptr_keyword() );
        else switch(odp->type)
        {
        case TYPE_EXTERN:
        case TYPE_STDCALL:
        case TYPE_VARARGS:
        case TYPE_CDECL:
        case TYPE_THISCALL:
            if (odp->flags & FLAG_FORWARD)
            {
                output( "\t%s .L__wine_spec_forwards+%u\n", get_asm_ptr_keyword(), fwd_size );
                fwd_size += strlen(odp->link_name) + 1;
            }
            else if (odp->flags & FLAG_EXT_LINK)
            {
                output( "\t%s %s_%s\n",
                         get_asm_ptr_keyword(), asm_name("__wine_spec_ext_link"), odp->link_name );
            }
            else
            {
                output( "\t%s %s\n", get_asm_ptr_keyword(), asm_name(odp->link_name) );
            }
            break;
        case TYPE_STUB:
            output( "\t%s %s\n", get_asm_ptr_keyword(),
                     asm_name( get_stub_name( odp, spec )) );
            break;
        default:
            assert(0);
        }
    }

    if (spec->nb_names)
    {
        /* output the function name pointers */

        int namepos = strlen(spec->file_name) + 1;

        output( "\n.L__wine_spec_exp_name_ptrs:\n" );
        for (i = 0; i < spec->nb_names; i++)
        {
            output( "\t.long .L__wine_spec_exp_names+%u-.L__wine_spec_rva_base\n", namepos );
            namepos += strlen(spec->names[i]->name) + 1;
        }

        /* output the function ordinals */

        output( "\n.L__wine_spec_exp_ordinals:\n" );
        for (i = 0; i < spec->nb_names; i++)
        {
            output( "\t.short %d\n", spec->names[i]->ordinal - spec->base );
        }
        if (spec->nb_names % 2)
        {
            output( "\t.short 0\n" );
        }
    }

    /* output the export name strings */

    output( "\n.L__wine_spec_exp_names:\n" );
    output( "\t%s \"%s\"\n", get_asm_string_keyword(), spec->file_name );
    for (i = 0; i < spec->nb_names; i++)
        output( "\t%s \"%s\"\n",
                 get_asm_string_keyword(), spec->names[i]->name );

    /* output forward strings */

    if (fwd_size)
    {
        output( "\n.L__wine_spec_forwards:\n" );
        for (i = spec->base; i <= spec->limit; i++)
        {
            ORDDEF *odp = spec->ordinals[i];
            if (odp && (odp->flags & FLAG_FORWARD))
                output( "\t%s \"%s\"\n", get_asm_string_keyword(), odp->link_name );
        }
    }
    output( "\t.align %d\n", get_alignment(get_ptr_size()) );
    output( ".L__wine_spec_exports_end:\n" );

    /* output relays */

    if (!has_relays( spec ))
    {
        output( "\t%s 0\n", get_asm_ptr_keyword() );
        return;
    }

    output( ".L__wine_spec_relay_descr:\n" );
    output( "\t%s 0xdeb90001\n", get_asm_ptr_keyword() );  /* magic */
    output( "\t%s 0,0\n", get_asm_ptr_keyword() );         /* relay funcs */
    output( "\t%s 0\n", get_asm_ptr_keyword() );           /* private data */
    output( "\t%s __wine_spec_relay_entry_points\n", get_asm_ptr_keyword() );
    output( "\t%s .L__wine_spec_relay_entry_point_offsets\n", get_asm_ptr_keyword() );
    output( "\t%s .L__wine_spec_relay_arg_types\n", get_asm_ptr_keyword() );

    output_relay_debug( spec );
}


/*******************************************************************
 *         output_asm_constructor
 *
 * Output code for calling a dll constructor.
 */
static void output_asm_constructor( const char *constructor )
{
    if (target_platform == PLATFORM_APPLE)
    {
        /* Mach-O doesn't have an init section */
        output( "\n\t.mod_init_func\n" );
        output( "\t.align %d\n", get_alignment(get_ptr_size()) );
        output( "\t%s %s\n", get_asm_ptr_keyword(), asm_name(constructor) );
    }
    else
    {
        switch(target_cpu)
        {
        case CPU_x86:
        case CPU_x86_64:
            output( "\n\t.section \".init\",\"ax\"\n" );
            output( "\tcall %s\n", asm_name(constructor) );
            break;
        case CPU_ARM:
            output( "\n\t.section \".text\",\"ax\"\n" );
            output( "\tblx %s\n", asm_name(constructor) );
            break;
        case CPU_ARM64:
        case CPU_POWERPC:
            output( "\n\t.section \".init\",\"ax\"\n" );
            output( "\tbl %s\n", asm_name(constructor) );
            break;
        }
    }
}


/*******************************************************************
 *         output_module
 *
 * Output the module data.
 */
void output_module( DLLSPEC *spec )
{
    int machine = 0;
    unsigned int page_size = get_page_size();

    /* Reserve some space for the PE header */

    switch (target_platform)
    {
    case PLATFORM_APPLE:
        output( "\t.text\n" );
        output( "\t.align %d\n", get_alignment(page_size) );
        output( "__wine_spec_pe_header:\n" );
        output( "\t.space 65536\n" );
        break;
    case PLATFORM_SOLARIS:
        output( "\n\t.section \".text\",\"ax\"\n" );
        output( "__wine_spec_pe_header:\n" );
        output( "\t.skip %u\n", 65536 + page_size );
        break;
    default:
        switch(target_cpu)
        {
        case CPU_x86:
        case CPU_x86_64:
            output( "\n\t.section \".init\",\"ax\"\n" );
            output( "\tjmp 1f\n" );
            break;
        case CPU_ARM:
            output( "\n\t.section \".text\",\"ax\"\n" );
            output( "\tb 1f\n" );
            break;
        case CPU_ARM64:
        case CPU_POWERPC:
            output( "\n\t.section \".init\",\"ax\"\n" );
            output( "\tb 1f\n" );
            break;
        }
        output( "__wine_spec_pe_header:\n" );
        output( "\t.skip %u\n", 65536 + page_size );
        output( "1:\n" );
        break;
    }

    /* Output the NT header */

    output( "\n\t.data\n" );
    output( "\t.align %d\n", get_alignment(get_ptr_size()) );
    output( "%s\n", asm_globl("__wine_spec_nt_header") );
    output( ".L__wine_spec_rva_base:\n" );

    output( "\t.long 0x4550\n" );         /* Signature */
    switch(target_cpu)
    {
    case CPU_x86:     machine = IMAGE_FILE_MACHINE_I386; break;
    case CPU_x86_64:  machine = IMAGE_FILE_MACHINE_AMD64; break;
    case CPU_POWERPC: machine = IMAGE_FILE_MACHINE_POWERPC; break;
    case CPU_ARM:     machine = IMAGE_FILE_MACHINE_ARMNT; break;
    case CPU_ARM64:   machine = IMAGE_FILE_MACHINE_ARM64; break;
    }
    output( "\t.short 0x%04x\n",          /* Machine */
             machine );
    output( "\t.short 0\n" );             /* NumberOfSections */
    output( "\t.long 0\n" );              /* TimeDateStamp */
    output( "\t.long 0\n" );              /* PointerToSymbolTable */
    output( "\t.long 0\n" );              /* NumberOfSymbols */
    output( "\t.short %d\n",              /* SizeOfOptionalHeader */
             get_ptr_size() == 8 ? IMAGE_SIZEOF_NT_OPTIONAL64_HEADER : IMAGE_SIZEOF_NT_OPTIONAL32_HEADER );
    output( "\t.short 0x%04x\n",          /* Characteristics */
             spec->characteristics );
    output( "\t.short 0x%04x\n",          /* Magic */
             get_ptr_size() == 8 ? IMAGE_NT_OPTIONAL_HDR64_MAGIC : IMAGE_NT_OPTIONAL_HDR32_MAGIC );
    output( "\t.byte 7\n" );              /* MajorLinkerVersion */
    output( "\t.byte 10\n" );             /* MinorLinkerVersion */
    output( "\t.long 0\n" );              /* SizeOfCode */
    output( "\t.long 0\n" );              /* SizeOfInitializedData */
    output( "\t.long 0\n" );              /* SizeOfUninitializedData */
    /* note: we expand the AddressOfEntryPoint field on 64-bit by overwriting the BaseOfCode field */
    output( "\t%s %s\n",                  /* AddressOfEntryPoint */
            get_asm_ptr_keyword(), spec->init_func ? asm_name(spec->init_func) : "0" );
    if (get_ptr_size() == 4)
    {
        output( "\t.long 0\n" );          /* BaseOfCode */
        output( "\t.long 0\n" );          /* BaseOfData */
    }
    output( "\t%s __wine_spec_pe_header\n",         /* ImageBase */
             get_asm_ptr_keyword() );
    output( "\t.long %u\n", page_size );  /* SectionAlignment */
    output( "\t.long %u\n", page_size );  /* FileAlignment */
    output( "\t.short 1,0\n" );           /* Major/MinorOperatingSystemVersion */
    output( "\t.short 0,0\n" );           /* Major/MinorImageVersion */
    output( "\t.short %u,%u\n",           /* Major/MinorSubsystemVersion */
             spec->subsystem_major, spec->subsystem_minor );
    output( "\t.long 0\n" );                          /* Win32VersionValue */
    output( "\t.long %s-.L__wine_spec_rva_base\n",    /* SizeOfImage */
             asm_name("_end") );
    output( "\t.long %u\n", page_size );  /* SizeOfHeaders */
    output( "\t.long 0\n" );              /* CheckSum */
    output( "\t.short 0x%04x\n",          /* Subsystem */
             spec->subsystem );
    output( "\t.short 0x%04x\n",          /* DllCharacteristics */
            spec->dll_characteristics );
    output( "\t%s %u,%u\n",               /* SizeOfStackReserve/Commit */
             get_asm_ptr_keyword(), (spec->stack_size ? spec->stack_size : 1024) * 1024, page_size );
    output( "\t%s %u,%u\n",               /* SizeOfHeapReserve/Commit */
             get_asm_ptr_keyword(), (spec->heap_size ? spec->heap_size : 1024) * 1024, page_size );
    output( "\t.long 0\n" );              /* LoaderFlags */
    output( "\t.long 16\n" );             /* NumberOfRvaAndSizes */

    if (spec->base <= spec->limit)   /* DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT] */
        output( "\t.long .L__wine_spec_exports-.L__wine_spec_rva_base,"
                 ".L__wine_spec_exports_end-.L__wine_spec_exports\n" );
    else
        output( "\t.long 0,0\n" );

    if (has_imports())   /* DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] */
        output( "\t.long .L__wine_spec_imports-.L__wine_spec_rva_base,"
                 ".L__wine_spec_imports_end-.L__wine_spec_imports\n" );
    else
        output( "\t.long 0,0\n" );

    if (spec->nb_resources)   /* DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE] */
        output( "\t.long .L__wine_spec_resources-.L__wine_spec_rva_base,"
                 ".L__wine_spec_resources_end-.L__wine_spec_resources\n" );
    else
        output( "\t.long 0,0\n" );

    output( "\t.long 0,0\n" );  /* DataDirectory[3] */
    output( "\t.long 0,0\n" );  /* DataDirectory[4] */
    output( "\t.long 0,0\n" );  /* DataDirectory[5] */
    output( "\t.long 0,0\n" );  /* DataDirectory[6] */
    output( "\t.long 0,0\n" );  /* DataDirectory[7] */
    output( "\t.long 0,0\n" );  /* DataDirectory[8] */
    output( "\t.long 0,0\n" );  /* DataDirectory[9] */
    output( "\t.long 0,0\n" );  /* DataDirectory[10] */
    output( "\t.long 0,0\n" );  /* DataDirectory[11] */
    output( "\t.long 0,0\n" );  /* DataDirectory[12] */
    output( "\t.long 0,0\n" );  /* DataDirectory[13] */
    output( "\t.long 0,0\n" );  /* DataDirectory[14] */

    if (spec->nb_syscalls)   /* DataDirectory[15] */
    {
        output( "\t%s __wine_spec_pe_header_syscalls\n", get_asm_ptr_keyword() );
        if (get_ptr_size() == 4) output( "\t.long 0\n" );
    }
    else
        output( "\t.long 0,0\n" );

    output( "\n\t%s\n", get_asm_string_section() );
    output( "%s\n", asm_globl("__wine_spec_file_name") );
    output( ".L__wine_spec_file_name:\n" );
    output( "\t%s \"%s\"\n", get_asm_string_keyword(), spec->file_name );
    if (target_platform == PLATFORM_APPLE)
        output( "\t.lcomm %s,4\n", asm_name("_end") );

    output_asm_constructor( "__wine_spec_init_ctor" );
}


/*******************************************************************
 *         BuildSpec32File
 *
 * Build a Win32 C file from a spec file.
 */
void BuildSpec32File( DLLSPEC *spec )
{
    needs_get_pc_thunk = 0;
    resolve_imports( spec );
    output_standard_file_header();
    output_module( spec );
    if (target_cpu == CPU_x86)
        output_syscall_thunks_x86( spec );
    else if (target_cpu == CPU_x86_64)
        output_syscall_thunks_x64( spec );
    output_stubs( spec );
    output_exports( spec );
    output_imports( spec );
    if (needs_get_pc_thunk) output_get_pc_thunk();
    output_resources( spec );
    output_gnu_stack_note();
}


static int needs_stub_exports( DLLSPEC *spec )
{
    if (target_cpu != CPU_x86 && target_cpu != CPU_x86_64)
        return 0;
    if (!(spec->characteristics & IMAGE_FILE_DLL))
        return 0;
    if (!spec->nb_entry_points)
        return 0;
    return 1;
}


static void create_stub_exports_text_x86( DLLSPEC *spec )
{
    int i, nr_exports = spec->base <= spec->limit ? spec->limit - spec->base + 1 : 0;
    size_t rva, thunk;

    /* output syscalls */
    for (i = 0; i < spec->nb_syscalls; i++)
    {
        ORDDEF *odp = spec->syscalls[i];

        align_output_rva( 16, 16 );
        put_label( odp->link_name );
        put_byte( 0xb8 ); put_dword( i );                     /* mov eax, SYSCALL */
        put_byte( 0x33 ); put_byte( 0xc9 );                   /* xor ecx, ecx */
        put_byte( 0x8d ); put_byte( 0x54 );                   /* lea edx, [esp + 4] */
        put_byte( 0x24 ); put_byte( 0x04 );
        put_byte( 0x64 ); put_byte( 0xff );                   /* call dword ptr fs:[0C0h] */
        put_byte( 0x15 ); put_dword( 0xc0 );
        put_byte( 0x83 ); put_byte( 0xc4 ); put_byte( 0x04 ); /* add esp, 4 */
        put_byte( 0xc2 ); put_word( get_args_size(odp) );     /* ret X */
    }

    if (spec->nb_syscalls)
    {
        for (i = 0; i < 0x20; i++)
            put_byte( 0 );
    }

    /* output stub code for exports */
    for (i = 0; i < spec->nb_entry_points; i++)
    {
        ORDDEF *odp = &spec->entry_points[i];
        const char *name;

        if (odp->flags & FLAG_SYSCALL)
            continue;

        align_output_rva( 16, 16 );
        name = get_stub_name( odp, spec );
        put_label( name );
        put_byte( 0x8b ); put_byte( 0xff );                           /* mov edi, edi */
        put_byte( 0x55 );                                             /* push ebp */
        put_byte( 0x8b ); put_byte( 0xec );                           /* mov ebp, esp */
        put_byte( 0x68 ); put_dword( 0 );                             /* push dword 0 */
        put_byte( 0x68 ); put_dword( odp->ordinal );                  /* push ORDINAL */
        rva = output_buffer_rva + 5;
        put_byte( 0xe8 ); put_dword( label_rva("_forward") - rva );   /* call _forward */
        put_byte( 0x89 ); put_byte( 0xec );                           /* mov esp, ebp */
        put_byte( 0x5d );                                             /* pop ebp */
        if (odp->type == TYPE_STDCALL || odp->type == TYPE_THISCALL)
        {
            put_byte( 0xc2 ); put_word( get_args_size(odp) );         /* ret X */
        }
        else
        {
            put_byte( 0xc3 );                                         /* ret */
        }
    }

    /* output entry point */
    align_output_rva( 16, 16 );
    put_label( "entrypoint" );
    put_byte( 0xb8 ); put_dword( 1 );                                 /* mov eax, 1 */
    put_byte( 0xc2 ); put_word( 12 );                                 /* ret 12 */

    /* output forward function */
    align_output_rva( 16, 16 );
    put_label( "_forward" );
    put_byte( 0x8b ); put_byte( 0x6d ); put_byte( 0x00 );             /* mov ebp, dword[ebp] */
    put_byte( 0x89 ); put_byte( 0x44 );                               /* mov dword[esp+8], eax */
    put_byte( 0x24 ); put_byte( 0x08 );
    put_byte( 0x89 ); put_byte( 0x14 ); put_byte( 0x24 );             /* mov dword[esp], edx */
    put_byte( 0x8b ); put_byte( 0x54 );                               /* mov edx, dword[esp+4] */
    put_byte( 0x24 ); put_byte( 0x04 );
    put_byte( 0x89 ); put_byte( 0x4c );                               /* mov dword[esp+4], ecx */
    put_byte( 0x24 ); put_byte( 0x04 );
    put_byte( 0xe8 ); put_dword( 0 );                                 /* call 1f */
    thunk = output_buffer_rva;
    put_byte( 0x59 );                                                 /* pop ecx */
    put_byte( 0x8b ); put_byte( 0x84 ); put_byte( 0x91 );             /* mov eax, dword[_functions + 4 * (edx - BASE)] */
    put_dword( label_rva("_functions") - thunk - 4 * spec->base );
    put_byte( 0x09 ); put_byte( 0xc0 );                               /* or eax, eax */
    rva = output_buffer_rva + 2;
    put_byte( 0x74 ); put_byte( label_rva("_forward_load") - rva );   /* je _forward_load */

    put_label( "_forward_done" );
    put_byte( 0x89 ); put_byte( 0x44 );                               /* mov dword[esp+12], eax */
    put_byte( 0x24 ); put_byte( 0x0c );
    put_byte( 0x5a );                                                 /* pop edx */
    put_byte( 0x59 );                                                 /* pop ecx */
    put_byte( 0x58 );                                                 /* pop eax */
    put_byte( 0xc3 );                                                 /* ret */

    align_output_rva( 16, 16 );
    put_label( "_forward_load" );
    put_byte( 0x8d ); put_byte( 0x84 ); put_byte( 0x91 );             /* lea eax, [_functions + 4 * (edx - BASE)] */
    put_dword( label_rva("_functions") - thunk - 4 * spec->base );
    put_byte( 0x50 );                                                 /* push eax */
    put_byte( 0x52 );                                                 /* push edx */
    put_byte( 0x8d ); put_byte( 0x81 );                               /* lea eax, [dll_name] */
    put_dword( label_rva("dll_name") - thunk );
    put_byte( 0x50 );                                                 /* push eax */
    put_byte( 0x64 ); put_byte( 0xff );                               /* call dword ptr fs:[0F78h] */
    put_byte( 0x15 ); put_dword( 0xf78 );
    put_byte( 0x5a );                                                 /* pop edx */
    put_byte( 0x89 ); put_byte( 0x02 );                               /* mov dword[edx], eax */
    rva = output_buffer_rva + 2;
    put_byte( 0xeb ); put_byte( label_rva("_forward_done") - rva );   /* jmp _forward_done */

    /* export directory */
    align_output_rva( 16, 16 );
    put_label( "export_start" );
    put_dword( 0 );                             /* Characteristics */
    put_dword( 0 );                             /* TimeDateStamp */
    put_dword( 0 );                             /* MajorVersion/MinorVersion */
    put_dword( label_rva("dll_name") );         /* Name */
    put_dword( spec->base );                    /* Base */
    put_dword( nr_exports );                    /* NumberOfFunctions */
    put_dword( spec->nb_names );                /* NumberOfNames */
    put_dword( label_rva("export_funcs") );     /* AddressOfFunctions */
    put_dword( label_rva("export_names") );     /* AddressOfNames */
    put_dword( label_rva("export_ordinals") );  /* AddressOfNameOrdinals */

    put_label( "export_funcs" );
    for (i = spec->base; i <= spec->limit; i++)
    {
        ORDDEF *odp = spec->ordinals[i];
        if (odp)
        {
            const char *name = (odp->flags & FLAG_SYSCALL) ? odp->link_name : get_stub_name( odp, spec );
            put_dword( label_rva( name ) );
        }
        else
            put_dword( 0 );
    }

    if (spec->nb_names)
    {
        put_label( "export_names" );
        for (i = 0; i < spec->nb_names; i++)
            put_dword( label_rva(strmake("str_%s", get_stub_name(spec->names[i], spec))) );

        put_label( "export_ordinals" );
        for (i = 0; i < spec->nb_names; i++)
            put_word( spec->names[i]->ordinal - spec->base );
        if (spec->nb_names % 2)
            put_word( 0 );
    }

    put_label( "dll_name" );
    put_str( spec->file_name );

    for (i = 0; i < spec->nb_names; i++)
    {
        put_label( strmake("str_%s", get_stub_name(spec->names[i], spec)) );
        put_str( spec->names[i]->name );
    }

    put_label( "export_end" );
}


static void create_stub_exports_text_x64( DLLSPEC *spec )
{
    int i, nr_exports = spec->base <= spec->limit ? spec->limit - spec->base + 1 : 0;

    /* output syscalls */
    for (i = 0; i < spec->nb_syscalls; i++)
    {
        ORDDEF *odp = spec->syscalls[i];

        align_output_rva( 16, 16 );
        put_label( odp->link_name );
        put_byte( 0xb8 ); put_dword( i );                      /* mov eax, SYSCALL */
        if (target_platform == PLATFORM_APPLE)
        {
            put_byte( 0xff ); put_byte( 0x14 );                /* call [0x7ffe1000] */
            put_byte( 0x25 ); put_dword( 0x7ffe1000 );
        }
        else
        {
            put_byte( 0x65 ); put_byte( 0xff );                /* call ptr gs:[0x100] */
            put_byte( 0x14 ); put_byte( 0x25 ); put_dword( 0x100 );

        }
        put_byte( 0xc3 );                                      /* ret */
    }

    if (spec->nb_syscalls)
    {
        for (i = 0; i < 0x20; i++)
            put_byte( 0 );
    }

    /* output stub code for exports */
    for (i = 0; i < spec->nb_entry_points; i++)
    {
        ORDDEF *odp = &spec->entry_points[i];
        const char *name;

        if (odp->flags & FLAG_SYSCALL)
            continue;

        align_output_rva( 16, 16 );
        name = get_stub_name( odp, spec );
        put_label( name );
        put_byte( 0xcc );                                             /* int $0x3 */
        put_byte( 0xc3 );                                             /* ret */
    }

    /* output entry point */
    align_output_rva( 16, 16 );
    put_label( "entrypoint" );
    put_byte( 0xb8 ); put_dword( 1 );                                 /* mov rax, 1 */
    put_byte( 0xc3 );                                                 /* ret */

    /* export directory */
    align_output_rva( 16, 16 );
    put_label( "export_start" );
    put_dword( 0 );                             /* Characteristics */
    put_dword( 0 );                             /* TimeDateStamp */
    put_dword( 0 );                             /* MajorVersion/MinorVersion */
    put_dword( label_rva("dll_name") );         /* Name */
    put_dword( spec->base );                    /* Base */
    put_dword( nr_exports );                    /* NumberOfFunctions */
    put_dword( spec->nb_names );                /* NumberOfNames */
    put_dword( label_rva("export_funcs") );     /* AddressOfFunctions */
    put_dword( label_rva("export_names") );     /* AddressOfNames */
    put_dword( label_rva("export_ordinals") );  /* AddressOfNameOrdinals */

    put_label( "export_funcs" );
    for (i = spec->base; i <= spec->limit; i++)
    {
        ORDDEF *odp = spec->ordinals[i];
        if (odp)
        {
            const char *name = (odp->flags & FLAG_SYSCALL) ? odp->link_name : get_stub_name( odp, spec );
            put_dword( label_rva( name ) );
        }
        else
            put_dword( 0 );
    }

    if (spec->nb_names)
    {
        put_label( "export_names" );
        for (i = 0; i < spec->nb_names; i++)
            put_dword( label_rva(strmake("str_%s", get_stub_name(spec->names[i], spec))) );

        put_label( "export_ordinals" );
        for (i = 0; i < spec->nb_names; i++)
            put_word( spec->names[i]->ordinal - spec->base );
        if (spec->nb_names % 2)
            put_word( 0 );
    }

    put_label( "dll_name" );
    put_str( spec->file_name );

    for (i = 0; i < spec->nb_names; i++)
    {
        put_label( strmake("str_%s", get_stub_name(spec->names[i], spec)) );
        put_str( spec->names[i]->name );
    }

    put_label( "export_end" );
}


static void create_stub_exports_data( DLLSPEC *spec )
{
    int i;

    put_label( "_functions" );
    for (i = spec->base; i <= spec->limit; i++)
        put_dword( 0 );
}


/*******************************************************************
 *         output_fake_module_pass
 *
 * Helper to create a fake binary module from a spec file.
 */
static void output_fake_module_pass( DLLSPEC *spec )
{
    static const unsigned char dll_code_section[] = { 0x31, 0xc0,          /* xor %eax,%eax */
                                                      0xc2, 0x0c, 0x00 };  /* ret $12 */

    static const unsigned char exe_code_section[] = { 0xb8, 0x01, 0x00, 0x00, 0x00,  /* movl $1,%eax */
                                                      0xc2, 0x04, 0x00 };            /* ret $4 */

    static const char fakedll_signature[] = "Wine placeholder DLL";
    const unsigned int page_size = get_page_size();
    const unsigned int section_align = page_size;
    const unsigned int file_align = 0x200;
    const unsigned int lfanew = (0x40 + sizeof(fakedll_signature) + 15) & ~15;
    const unsigned int nb_sections = 2 + (needs_stub_exports( spec ) != 0) + (spec->nb_resources != 0);

    put_word( 0x5a4d );       /* e_magic */
    put_word( 0x40 );         /* e_cblp */
    put_word( 0x01 );         /* e_cp */
    put_word( 0 );            /* e_crlc */
    put_word( lfanew / 16 );  /* e_cparhdr */
    put_word( 0x0000 );       /* e_minalloc */
    put_word( 0xffff );       /* e_maxalloc */
    put_word( 0x0000 );       /* e_ss */
    put_word( 0x00b8 );       /* e_sp */
    put_word( 0 );            /* e_csum */
    put_word( 0 );            /* e_ip */
    put_word( 0 );            /* e_cs */
    put_word( lfanew );       /* e_lfarlc */
    put_word( 0 );            /* e_ovno */
    put_dword( 0 );           /* e_res */
    put_dword( 0 );
    put_word( 0 );            /* e_oemid */
    put_word( 0 );            /* e_oeminfo */
    put_dword( 0 );           /* e_res2 */
    put_dword( 0 );
    put_dword( 0 );
    put_dword( 0 );
    put_dword( 0 );
    put_dword( lfanew );

    put_data( fakedll_signature, sizeof(fakedll_signature) );
    align_output_rva( 16, 16 );

    put_dword( 0x4550 );                             /* Signature */
    switch(target_cpu)
    {
    case CPU_x86:     put_word( IMAGE_FILE_MACHINE_I386 ); break;
    case CPU_x86_64:  put_word( IMAGE_FILE_MACHINE_AMD64 ); break;
    case CPU_POWERPC: put_word( IMAGE_FILE_MACHINE_POWERPC ); break;
    case CPU_ARM:     put_word( IMAGE_FILE_MACHINE_ARMNT ); break;
    case CPU_ARM64:   put_word( IMAGE_FILE_MACHINE_ARM64 ); break;
    }
    put_word( nb_sections );                         /* NumberOfSections */
    put_dword( 0 );                                  /* TimeDateStamp */
    put_dword( 0 );                                  /* PointerToSymbolTable */
    put_dword( 0 );                                  /* NumberOfSymbols */
    put_word( get_ptr_size() == 8 ?
              IMAGE_SIZEOF_NT_OPTIONAL64_HEADER :
              IMAGE_SIZEOF_NT_OPTIONAL32_HEADER );   /* SizeOfOptionalHeader */
    put_word( spec->characteristics );               /* Characteristics */
    put_word( get_ptr_size() == 8 ?
              IMAGE_NT_OPTIONAL_HDR64_MAGIC :
              IMAGE_NT_OPTIONAL_HDR32_MAGIC );       /* Magic */
    put_byte(  7 );                                  /* MajorLinkerVersion */
    put_byte(  10 );                                 /* MinorLinkerVersion */
    put_dword( label_pos("text_end") - label_pos("text_start") ); /* SizeOfCode */
    put_dword( 0 );                                  /* SizeOfInitializedData */
    put_dword( 0 );                                  /* SizeOfUninitializedData */
    put_dword( label_rva("entrypoint") );            /* AddressOfEntryPoint */
    put_dword( label_rva("text_start") );            /* BaseOfCode */
    if (get_ptr_size() == 4) put_dword( label_rva("data_start") ); /* BaseOfData */
    put_pword( 0x10000000 );                         /* ImageBase */
    put_dword( section_align );                      /* SectionAlignment */
    put_dword( file_align );                         /* FileAlignment */
    put_word( 1 );                                   /* MajorOperatingSystemVersion */
    put_word( 0 );                                   /* MinorOperatingSystemVersion */
    put_word( 0 );                                   /* MajorImageVersion */
    put_word( 0 );                                   /* MinorImageVersion */
    put_word( spec->subsystem_major );               /* MajorSubsystemVersion */
    put_word( spec->subsystem_minor );               /* MinorSubsystemVersion */
    put_dword( 0 );                                  /* Win32VersionValue */
    put_dword( label_rva_align("file_end") );        /* SizeOfImage */
    put_dword( label_pos("header_end") );            /* SizeOfHeaders */
    put_dword( 0 );                                  /* CheckSum */
    put_word( spec->subsystem );                     /* Subsystem */
    put_word( spec->dll_characteristics );           /* DllCharacteristics */
    put_pword( (spec->stack_size ? spec->stack_size : 1024) * 1024 ); /* SizeOfStackReserve */
    put_pword( page_size );                          /* SizeOfStackCommit */
    put_pword( (spec->heap_size ? spec->heap_size : 1024) * 1024 );   /* SizeOfHeapReserve */
    put_pword( page_size );                          /* SizeOfHeapCommit */
    put_dword( 0 );                                  /* LoaderFlags */
    put_dword( 16 );                                 /* NumberOfRvaAndSizes */

    put_dword( label_rva("export_start") ); /* DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT] */
    put_dword( label_pos("export_end") - label_pos("export_start") );
    put_dword( 0 ); put_dword( 0 );   /* DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] */
    if (spec->nb_resources)           /* DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE] */
    {
        put_dword( label_rva("res_start") );
        put_dword( label_pos("res_end") - label_pos("res_start") );
    }
    else
    {
        put_dword( 0 );
        put_dword( 0 );
    }

    put_dword( 0 ); put_dword( 0 );   /* DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION] */
    put_dword( 0 ); put_dword( 0 );   /* DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY] */
    put_dword( label_rva("reloc_start") ); /* DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC] */
    put_dword( label_pos("reloc_end") - label_pos("reloc_start") );
    put_dword( 0 ); put_dword( 0 );   /* DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG] */
    put_dword( 0 ); put_dword( 0 );   /* DataDirectory[IMAGE_DIRECTORY_ENTRY_COPYRIGHT] */
    put_dword( 0 ); put_dword( 0 );   /* DataDirectory[IMAGE_DIRECTORY_ENTRY_GLOBALPTR] */
    put_dword( 0 ); put_dword( 0 );   /* DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS] */
    put_dword( 0 ); put_dword( 0 );   /* DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG] */
    put_dword( 0 ); put_dword( 0 );   /* DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT] */
    put_dword( 0 ); put_dword( 0 );   /* DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT] */
    put_dword( 0 ); put_dword( 0 );   /* DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT] */
    put_dword( 0 ); put_dword( 0 );   /* DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR] */
    put_dword( 0 ); put_dword( 0 );   /* DataDirectory[15] */

    /* .text section */
    put_data( ".text\0\0", 8 );                                           /* Name */
    put_dword( label_rva_align("text_end") - label_rva("text_start") );   /* VirtualSize */
    put_dword( label_rva("text_start") );                                 /* VirtualAddress */
    put_dword( label_pos("text_end") - label_pos("text_start") );         /* SizeOfRawData */
    put_dword( label_pos("text_start") );                                 /* PointerToRawData */
    put_dword( 0 );                                                       /* PointerToRelocations */
    put_dword( 0 );                                                       /* PointerToLinenumbers */
    put_word( 0 );                                                        /* NumberOfRelocations */
    put_word( 0 );                                                        /* NumberOfLinenumbers */
    put_dword( 0x60000020 /* CNT_CODE|MEM_EXECUTE|MEM_READ */ );          /* Characteristics  */

    /* .data section */
    if (needs_stub_exports( spec ))
    {
        put_data( ".data\0\0", 8 );                                       /* Name */
        put_dword( label_rva_align("data_end") - label_rva("data_start") ); /* VirtualSize */
        put_dword( label_rva("data_start") );                             /* VirtualAddress */
        put_dword( label_pos("data_end") - label_pos("data_start") );     /* SizeOfRawData */
        put_dword( label_pos("data_start") );                             /* PointerToRawData */
        put_dword( 0 );                                                   /* PointerToRelocations */
        put_dword( 0 );                                                   /* PointerToLinenumbers */
        put_word( 0 );                                                    /* NumberOfRelocations */
        put_word( 0 );                                                    /* NumberOfLinenumbers */
        put_dword( 0xc0000040 /* CNT_INITIALIZED_DATA|MEM_READ|MEM_WRITE */ ); /* Characteristics  */
    }

    /* .reloc section */
    put_data( ".reloc\0", 8 );                                            /* Name */
    put_dword( label_rva_align("reloc_end") - label_rva("reloc_start") ); /* VirtualSize */
    put_dword( label_rva("reloc_start") );                                /* VirtualAddress */
    put_dword( label_pos("reloc_end") - label_pos("reloc_start") );       /* SizeOfRawData */
    put_dword( label_pos("reloc_start") );                                /* PointerToRawData */
    put_dword( 0 );                                                       /* PointerToRelocations */
    put_dword( 0 );                                                       /* PointerToLinenumbers */
    put_word( 0 );                                                        /* NumberOfRelocations */
    put_word( 0 );                                                        /* NumberOfLinenumbers */
    put_dword( 0x42000040 /* CNT_INITIALIZED_DATA|MEM_DISCARDABLE|MEM_READ */ ); /* Characteristics */

    /* .rsrc section */
    if (spec->nb_resources)
    {
        put_data( ".rsrc\0\0", 8 );                                       /* Name */
        put_dword( label_rva_align("res_end") - label_rva("res_start") ); /* VirtualSize */
        put_dword( label_rva("res_start") );                              /* VirtualAddress */
        put_dword( label_pos("res_end") - label_pos("res_start") );       /* SizeOfRawData */
        put_dword( label_pos("res_start") );                              /* PointerToRawData */
        put_dword( 0 );                                                   /* PointerToRelocations */
        put_dword( 0 );                                                   /* PointerToLinenumbers */
        put_word( 0 );                                                    /* NumberOfRelocations */
        put_word( 0 );                                                    /* NumberOfLinenumbers */
        put_dword( 0x40000040 /* CNT_INITIALIZED_DATA|MEM_READ */ );      /* Characteristics */
    }

    align_output_rva( file_align, file_align );
    put_label( "header_end" );

    /* .text contents */
    align_output_rva( file_align, section_align );
    if (needs_stub_exports( spec ))
    {
        put_label( "text_start" );
        if (target_cpu == CPU_x86)
            create_stub_exports_text_x86( spec );
        else if (target_cpu == CPU_x86_64)
            create_stub_exports_text_x64( spec );
        put_label( "text_end" );
    }
    else
    {
        put_label( "text_start" );
        put_label( "entrypoint" );
        if (spec->characteristics & IMAGE_FILE_DLL)
            put_data( dll_code_section, sizeof(dll_code_section) );
        else
            put_data( exe_code_section, sizeof(exe_code_section) );
        put_label( "text_end" );
    }

    /* .data contents */
    align_output_rva( file_align, section_align );
    if (needs_stub_exports( spec ))
    {
        put_label( "data_start" );
        create_stub_exports_data( spec );
        put_label( "data_end" );
    }

    /* .reloc contents */
    align_output_rva( file_align, section_align );
    put_label( "reloc_start" );
    put_dword( label_rva("text_start") );   /* VirtualAddress */
    put_dword( 8 );                         /* SizeOfBlock */
    put_label( "reloc_end" );

    /* .rsrc contents */
    if (spec->nb_resources)
    {
        align_output_rva( file_align, section_align );
        put_label( "res_start" );
        output_bin_resources( spec, label_rva("res_start") );
        put_label( "res_end" );
    }

    put_label( "file_end" );
}


/*******************************************************************
 *         output_fake_module
 *
 * Build a fake binary module from a spec file.
 */
void output_fake_module( DLLSPEC *spec )
{
    resolve_imports( spec );

    /* First pass */
    init_output_buffer();
    output_fake_module_pass( spec );

    /* Second pass */
    output_buffer_pos = 0;
    output_buffer_rva = 0;
    output_fake_module_pass( spec );

    flush_output_buffer();
}


/*******************************************************************
 *         output_def_file
 *
 * Build a Win32 def file from a spec file.
 */
void output_def_file( DLLSPEC *spec, int include_private )
{
    DLLSPEC *spec32 = NULL;
    const char *name;
    int i, total;

    if (spec->type == SPEC_WIN16)
    {
        spec32 = alloc_dll_spec();
        add_16bit_exports( spec32, spec );
        spec = spec32;
    }

    if (spec_file_name)
        output( "; File generated automatically from %s; do not edit!\n\n",
                 spec_file_name );
    else
        output( "; File generated automatically; do not edit!\n\n" );

    output( "LIBRARY %s\n\n", spec->file_name);
    output( "EXPORTS\n");

    /* Output the exports and relay entry points */

    for (i = total = 0; i < spec->nb_entry_points; i++)
    {
        const ORDDEF *odp = &spec->entry_points[i];
        int is_data = 0;

        if (odp->name) name = odp->name;
        else if (odp->export_name) name = odp->export_name;
        else continue;

        if (!(odp->flags & FLAG_PRIVATE)) total++;
        else if (!include_private) continue;

        if (odp->type == TYPE_STUB) continue;

        output( "  %s", name );

        switch(odp->type)
        {
        case TYPE_EXTERN:
            is_data = 1;
            /* fall through */
        case TYPE_VARARGS:
        case TYPE_CDECL:
        case TYPE_THISCALL:
            /* try to reduce output */
            if(strcmp(name, odp->link_name) || (odp->flags & FLAG_FORWARD))
                output( "=%s", odp->link_name );
            break;
        case TYPE_STDCALL:
        {
            int at_param = get_args_size( odp );
            if (!kill_at && target_cpu == CPU_x86) output( "@%d", at_param );
            if  (odp->flags & FLAG_FORWARD)
            {
                output( "=%s", odp->link_name );
            }
            else if (strcmp(name, odp->link_name)) /* try to reduce output */
            {
                output( "=%s", odp->link_name );
                if (!kill_at && target_cpu == CPU_x86) output( "@%d", at_param );
            }
            break;
        }
        default:
            assert(0);
        }
        output( " @%d", odp->ordinal );
        if (!odp->name || (odp->flags & FLAG_ORDINAL)) output( " NONAME" );
        if (is_data) output( " DATA" );
        if (odp->flags & FLAG_PRIVATE) output( " PRIVATE" );
        output( "\n" );
    }
    if (!total) warning( "%s: Import library doesn't export anything\n", spec->file_name );
    if (spec32) free_dll_spec( spec32 );
}
