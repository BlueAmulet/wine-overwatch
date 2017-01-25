/*
 * Typelib (SLTG) generation
 *
 * Copyright 2015 Dmitry Timoshkov
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

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

#define NONAMELESSUNION

#include "windef.h"
#include "winbase.h"

#include "widl.h"
#include "typelib.h"
#include "typelib_struct.h"
#include "utils.h"
#include "header.h"
#include "typetree.h"

static const GUID sltg_library_guid = { 0x204ff,0,0,{ 0xc0,0,0,0,0,0,0,0x46 } };

struct sltg_index
{
    int size, allocated;
    char *names;
};

struct sltg_name_table
{
    int size, allocated;
    char *names;
};

struct sltg_library
{
    short name;
    char *helpstring;
    char *helpfile;
    int helpcontext;
    int syskind;
    LCID lcid;
    int libflags;
    int version;
    GUID uuid;
};

struct sltg_block
{
    int length;
    int index_string;
    void *data;
    struct sltg_block *next;
};

struct sltg_typelib
{
    typelib_t *typelib;
    struct sltg_index index;
    struct sltg_name_table name_table;
    struct sltg_library library;
    struct sltg_block *blocks;
    int n_file_blocks;
    int first_block;
    int typeinfo_count;
};

static int add_index(struct sltg_index *index, const char *name)
{
    int name_offset = index->size;
    int new_size = index->size + strlen(name) + 1;

    if (new_size > index->allocated)
    {
        index->allocated = max(index->allocated * 2, new_size);
        index->names = xrealloc(index->names, index->allocated);
    }

    strcpy(index->names + index->size, name);
    index->size = new_size;

    return name_offset;
}

static void init_index(struct sltg_index *index)
{
    static const char compobj[] = { 1,'C','o','m','p','O','b','j',0 };

    index->size = 0;
    index->allocated = 0x10;
    index->names = xmalloc(0x10);

    add_index(index, compobj);
}

static int add_name(struct sltg_name_table *name_table, const char *name)
{
    int name_offset = name_table->size;
    int new_size = name_table->size + strlen(name) + 1 + 8;

    new_size = (new_size + 1) & ~1; /* align */

    if (new_size > name_table->allocated)
    {
        name_table->allocated = max(name_table->allocated * 2, new_size);
        name_table->names = xrealloc(name_table->names, name_table->allocated);
    }

    memset(name_table->names + name_table->size, 0xff, 8);
    strcpy(name_table->names + name_table->size + 8, name);
    name_table->size = new_size;
    name_table->names[name_table->size - 1] = 0; /* clear alignment */

    return name_offset;
}

static void init_name_table(struct sltg_name_table *name_table)
{
    name_table->size = 0;
    name_table->allocated = 0x10;
    name_table->names = xmalloc(0x10);
}

static void init_library(struct sltg_typelib *sltg)
{
    const attr_t *attr;

    sltg->library.name = add_name(&sltg->name_table, sltg->typelib->name);
    sltg->library.helpstring = NULL;
    sltg->library.helpcontext = 0;
    sltg->library.syskind = SYS_WIN32;
    sltg->library.lcid = 0x0409;
    sltg->library.libflags = 0;
    sltg->library.version = 0;
    sltg->library.helpfile = NULL;
    memset(&sltg->library.uuid, 0, sizeof(sltg->library.uuid));

    if (!sltg->typelib->attrs) return;

    LIST_FOR_EACH_ENTRY(attr, sltg->typelib->attrs, const attr_t, entry)
    {
        const expr_t *expr;

        switch (attr->type)
        {
        case ATTR_VERSION:
            sltg->library.version = attr->u.ival;
            break;
        case ATTR_HELPSTRING:
            sltg->library.helpstring = attr->u.pval;
            break;
        case ATTR_HELPFILE:
            sltg->library.helpfile = attr->u.pval;
            break;
        case ATTR_UUID:
            sltg->library.uuid = *(GUID *)attr->u.pval;
            break;
        case ATTR_HELPCONTEXT:
            expr = attr->u.pval;
            sltg->library.helpcontext = expr->cval;
            break;
        case ATTR_LIBLCID:
            expr = attr->u.pval;
            sltg->library.lcid = expr->cval;
            break;
        case ATTR_CONTROL:
            sltg->library.libflags |= 0x02; /* LIBFLAG_FCONTROL */
            break;
        case ATTR_HIDDEN:
            sltg->library.libflags |= 0x04; /* LIBFLAG_FHIDDEN */
            break;
        case ATTR_RESTRICTED:
            sltg->library.libflags |= 0x01; /* LIBFLAG_FRESTRICTED */
            break;
        default:
            break;
        }
    }
}

static void add_block(struct sltg_typelib *sltg, void *data, int size, const char *name)
{
    struct sltg_block *block = xmalloc(sizeof(*block));

    block->length = size;
    block->data = data;
    block->index_string = add_index(&sltg->index, name);
    block->next = sltg->blocks;

    sltg->blocks = block;
    sltg->n_file_blocks++;
}

static void add_library_block(struct sltg_typelib *typelib)
{
    void *block;
    short *p;
    int size;

    size = sizeof(short) * 9 + sizeof(int) * 3 + sizeof(GUID);
    if (typelib->library.helpstring) size += strlen(typelib->library.helpstring);
    if (typelib->library.helpfile) size += strlen(typelib->library.helpfile);

    block = xmalloc(size);
    p = block;
    *p++ = 0x51cc; /* magic */
    *p++ = 3; /* res02 */
    *p++ = typelib->library.name;
    *p++ = 0xffff; /* res06 */
    if (typelib->library.helpstring)
    {
        *p++ = strlen(typelib->library.helpstring);
        strcpy((char *)p, typelib->library.helpstring);
        p = (short *)((char *)p + strlen(typelib->library.helpstring));
    }
    else
        *p++ = 0xffff;
    if (typelib->library.helpfile)
    {
        *p++ = strlen(typelib->library.helpfile);
        strcpy((char *)p, typelib->library.helpfile);
        p = (short *)((char *)p + strlen(typelib->library.helpfile));
    }
    else
        *p++ = 0xffff;
    *(int *)p = typelib->library.helpcontext;
    p += 2;
    *p++ = typelib->library.syskind;
    *p++ = typelib->library.lcid;
    *(int *)p = 0; /* res12 */
    p += 2;
    *p++ = typelib->library.libflags;
    *(int *)p = typelib->library.version;
    p += 2;
    *(GUID *)p = typelib->library.uuid;

    add_block(typelib, block, size, "dir");
}

static void add_typedef_typeinfo(struct sltg_typelib *typelib, type_t *type)
{
    error("add_typedef_typeinfo: %s not implemented\n", type->name);
}

static void add_module_typeinfo(struct sltg_typelib *typelib, type_t *type)
{
    error("add_module_typeinfo: %s not implemented\n", type->name);
}

static void add_interface_typeinfo(struct sltg_typelib *typelib, type_t *type)
{
    error("add_interface_typeinfo: %s not implemented\n", type->name);
}

static void add_structure_typeinfo(struct sltg_typelib *typelib, type_t *type)
{
    error("add_structure_typeinfo: %s not implemented\n", type->name);
}

static void add_enum_typeinfo(struct sltg_typelib *typelib, type_t *type)
{
    error("add_enum_typeinfo: %s not implemented\n", type->name);
}

static void add_union_typeinfo(struct sltg_typelib *typelib, type_t *type)
{
    error("add_union_typeinfo: %s not implemented\n", type->name);
}

static void add_coclass_typeinfo(struct sltg_typelib *typelib, type_t *type)
{
    error("add_coclass_typeinfo: %s not implemented\n", type->name);
}

static void add_type_typeinfo(struct sltg_typelib *typelib, type_t *type)
{
    chat("add_type_typeinfo: adding %s, type %d\n", type->name, type_get_type(type));

    switch (type_get_type(type))
    {
    case TYPE_INTERFACE:
        add_interface_typeinfo(typelib, type);
        break;
    case TYPE_STRUCT:
        add_structure_typeinfo(typelib, type);
        break;
    case TYPE_ENUM:
        add_enum_typeinfo(typelib, type);
        break;
    case TYPE_UNION:
        add_union_typeinfo(typelib, type);
        break;
    case TYPE_COCLASS:
        add_coclass_typeinfo(typelib, type);
        break;
    case TYPE_BASIC:
    case TYPE_POINTER:
        break;
    default:
        error("add_type_typeinfo: unhandled type %d for %s\n", type_get_type(type), type->name);
        break;
    }
}

static void add_statement(struct sltg_typelib *typelib, const statement_t *stmt)
{
    switch(stmt->type)
    {
    case STMT_LIBRARY:
    case STMT_IMPORT:
    case STMT_PRAGMA:
    case STMT_CPPQUOTE:
    case STMT_DECLARATION:
        /* not included in typelib */
        break;
    case STMT_IMPORTLIB:
        /* not processed here */
        break;

    case STMT_TYPEDEF:
    {
        const type_list_t *type_entry = stmt->u.type_list;
        for (; type_entry; type_entry = type_entry->next)
        {
            /* if the type is public then add the typedef, otherwise attempt
             * to add the aliased type */
            if (is_attr(type_entry->type->attrs, ATTR_PUBLIC))
                add_typedef_typeinfo(typelib, type_entry->type);
            else
                add_type_typeinfo(typelib, type_alias_get_aliasee(type_entry->type));
        }
        break;
    }

    case STMT_MODULE:
        add_module_typeinfo(typelib, stmt->u.type);
        break;

    case STMT_TYPE:
    case STMT_TYPEREF:
    {
        type_t *type = stmt->u.type;
        add_type_typeinfo(typelib, type);
        break;
    }

    default:
        error("add_statement: unhandled statement type %d\n", stmt->type);
        break;
    }
}

static void sltg_write_header(struct sltg_typelib *sltg, int *library_block_start)
{
    char pad[0x40];
    struct sltg_header
    {
        int magic;
        short n_file_blocks;
        short res06;
        short size_of_index;
        short first_blk;
        GUID uuid;
        int res1c;
        int res20;
    } header;
    struct sltg_block_entry
    {
        int length;
        short index_string;
        short next;
    } entry;
    struct sltg_block *block;
    int i;

    header.magic = 0x47544c53;
    header.n_file_blocks = sltg->n_file_blocks + 1;
    header.res06 = 9;
    header.size_of_index = sltg->index.size;
    header.first_blk = 1;
    header.uuid = sltg_library_guid;
    header.res1c = 0x00000044;
    header.res20 = 0xffff0000;

    put_data(&header, sizeof(header));

    block = sltg->blocks;
    for (i = 0; i < sltg->n_file_blocks - 1; i++)
    {
        assert(block->next != NULL);

        entry.length = block->length;
        entry.index_string = block->index_string;
        entry.next = header.first_blk + i;
        put_data(&entry, sizeof(entry));

        block = block->next;
    }

    assert(block->next == NULL);

    /* library block length includes helpstrings and name table */
    entry.length = block->length + 0x40 + 2 + 4 + 6 + 12 + 0x200 + sltg->name_table.size + 12;
    entry.index_string = block->index_string;
    entry.next = 0;
    put_data(&entry, sizeof(entry));

    put_data(sltg->index.names, sltg->index.size);
    memset(pad, 0, 9);
    put_data(pad, 9);

    block = sltg->blocks;
    for (i = 0; i < sltg->n_file_blocks - 1; i++)
    {
        chat("sltg_write_header: writing block %d: %d bytes\n", i, block->length);

        put_data(block->data, block->length);
        block = block->next;
    }

    assert(block->next == NULL);

    /* library block */
    *library_block_start = output_buffer_pos;
    put_data(block->data, block->length);

    memset(pad, 0xff, 0x40);
    put_data(pad, 0x40);
}

static void sltg_write_typeinfo(struct sltg_typelib *typelib)
{
    short count = typelib->typeinfo_count;

    put_data(&count, sizeof(count));
}

static void sltg_write_helpstrings(struct sltg_typelib *typelib)
{
    static const char dummy[6];

    put_data(dummy, sizeof(dummy));
}

static void sltg_write_nametable(struct sltg_typelib *typelib)
{
    static const short dummy[6] = { 0xffff,1,2,0xff00,0xffff,0xffff };
    char pad[0x200];

    put_data(dummy, sizeof(dummy));
    memset(pad, 0xff, 0x200);
    put_data(pad, 0x200);
    put_data(&typelib->name_table.size, sizeof(typelib->name_table.size));
    put_data(typelib->name_table.names, typelib->name_table.size);
}

static void sltg_write_remainder(void)
{
    static const short dummy1[] = { 1,0xfffe,0x0a03,0,0xffff,0xffff };
    static const short dummy2[] = { 0xffff,0xffff,0x0200,0,0,0 };
    static const char dummy3[] = { 0xf4,0x39,0xb2,0x71,0,0,0,0,0,0,0,0,0,0,0,0 };
    static const char TYPELIB[] = { 8,0,0,0,'T','Y','P','E','L','I','B',0 };
    int pad;

    pad = 0x01ffff01;
    put_data(&pad, sizeof(pad));
    pad = 0;
    put_data(&pad, sizeof(pad));

    put_data(dummy1, sizeof(dummy1));

    put_data(&sltg_library_guid, sizeof(sltg_library_guid));

    put_data(TYPELIB, sizeof(TYPELIB));

    put_data(dummy2, sizeof(dummy2));
    put_data(dummy3, sizeof(dummy3));
}

static void save_all_changes(struct sltg_typelib *typelib)
{
    int library_block_start;
    int *name_table_offset;

    sltg_write_header(typelib, &library_block_start);
    sltg_write_typeinfo(typelib);

    name_table_offset = (int *)(output_buffer + output_buffer_pos);
    put_data(&library_block_start, sizeof(library_block_start));

    sltg_write_helpstrings(typelib);

    *name_table_offset = output_buffer_pos - library_block_start;

    sltg_write_nametable(typelib);
    sltg_write_remainder();

    if (strendswith(typelib_name, ".res")) /* create a binary resource file */
    {
        char typelib_id[13] = "#1";

        expr_t *expr = get_attrp(typelib->typelib->attrs, ATTR_ID);
        if (expr)
            sprintf(typelib_id, "#%d", expr->cval);
        add_output_to_resources("TYPELIB", typelib_id);
        output_typelib_regscript(typelib->typelib);
        flush_output_resources(typelib_name);
    }
    else flush_output_buffer(typelib_name);
}

int create_sltg_typelib(typelib_t *typelib)
{
    struct sltg_typelib sltg;
    const statement_t *stmt;

    sltg.typelib = typelib;
    sltg.typeinfo_count = 0;
    sltg.blocks = NULL;
    sltg.n_file_blocks = 0;
    sltg.first_block = 1;

    init_index(&sltg.index);
    init_name_table(&sltg.name_table);
    init_library(&sltg);

    add_library_block(&sltg);

    if (typelib->stmts)
        LIST_FOR_EACH_ENTRY(stmt, typelib->stmts, const statement_t, entry)
            add_statement(&sltg, stmt);

    save_all_changes(&sltg);

    return 1;
}
