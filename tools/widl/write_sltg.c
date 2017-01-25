/*
 * Typelib (SLTG) generation
 *
 * Copyright 2015,2016 Dmitry Timoshkov
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

struct sltg_data
{
    int size, allocated;
    char *data;
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
    struct sltg_data index;
    struct sltg_data name_table;
    struct sltg_library library;
    struct sltg_block *blocks;
    int n_file_blocks;
    int first_block;
    int typeinfo_count;
    int typeinfo_size;
    struct sltg_block *typeinfo;
};

struct sltg_hrefmap
{
    int href_count;
    int *href;
};

#include "pshpack1.h"
struct sltg_typeinfo_header
{
    short magic;
    int href_offset;
    int res06;
    int member_offset;
    int res0e;
    int version;
    int res16;
    struct
    {
        unsigned unknown1  : 3;
        unsigned flags     : 13;
        unsigned unknown2  : 8;
        unsigned typekind  : 8;
    } misc;
    int res1e;
};

struct sltg_member_header
{
    short res00;
    short res02;
    char res04;
    int extra;
};

struct sltg_variable
{
    char magic; /* 0x0a */
    char flags;
    short next;
    short name;
    short byte_offs; /* pos in struct, or offset to const type or const data (if flags & 0x08) */
    short type; /* if flags & 0x02 this is the type, else offset to type */
    int memid;
    short helpcontext;
    short helpstring;
    short varflags; /* only present if magic & 0x02 */
};

struct sltg_tail
{
    short cFuncs;
    short cVars;
    short cImplTypes;
    short res06; /* always 0000 */
    short funcs_off; /* offset to functions (starting from the member header) */
    short vars_off; /* offset to vars (starting from the member header) */
    short impls_off; /* offset to implemented types (starting from the member header) */
    short funcs_bytes; /* bytes used by function data */
    short vars_bytes; /* bytes used by var data */
    short impls_bytes; /* bytes used by implemented type data */
    short tdescalias_vt; /* for TKIND_ALIAS */
    short res16; /* always ffff */
    short res18; /* always 0000 */
    short res1a; /* always 0000 */
    short simple_alias; /* tdescalias_vt is a vt rather than an offset? */
    short res1e; /* always 0000 */
    short cbSizeInstance;
    short cbAlignment;
    short res24; /* always ffff */
    short res26; /* always ffff */
    short cbSizeVft;
    short res2a; /* always ffff */
    short res2c; /* always ffff */
    short res2e; /* always ffff */
    short res30; /* always ffff */
    short res32; /* unknown */
    short type_bytes; /* bytes used by type descriptions */
};

struct sltg_hrefinfo
{
    char magic; /* 0xdf */
    char res01; /* 0x00 */
    int res02;  /* 0xffffffff */
    int res06;  /* 0xffffffff */
    int res0a;  /* 0xffffffff */
    int res0e;  /* 0xffffffff */
    int res12;  /* 0xffffffff */
    int res16;  /* 0xffffffff */
    int res1a;  /* 0xffffffff */
    int res1e;  /* 0xffffffff */
    int res22;  /* 0xffffffff */
    int res26;  /* 0xffffffff */
    int res2a;  /* 0xffffffff */
    int res2e;  /* 0xffffffff */
    int res32;  /* 0xffffffff */
    int res36;  /* 0xffffffff */
    int res3a;  /* 0xffffffff */
    int res3e;  /* 0xffffffff */
    short res42;/* 0xffff */
    int number; /* this is 8 times the number of refs */
    /* Now we have number bytes (8 for each ref) of SLTG_UnknownRefInfo */

    short res50;/* 0xffff */
    char res52; /* 0x01 */
    int res53;  /* 0x00000000 */
    /*    Now we have number/8 SLTG_Names (first WORD is no of bytes in the ascii
     *    string).  Strings look like "*\Rxxxx*#n".  If xxxx == ffff then the
     *    ref refers to the nth type listed in this library (0 based).  Else
     *    the xxxx (which maybe fewer than 4 digits) is the offset into the name
     *    table to a string "*\G{<guid>}#1.0#0#C:\WINNT\System32\stdole32.tlb#"
     *    The guid is the typelib guid; the ref again refers to the nth type of
     *    the imported typelib.
     */

    char resxx; /* 0xdf */
};

#include "poppack.h"

static void init_sltg_data(struct sltg_data *data)
{
    data->size = 0;
    data->allocated = 0x10;
    data->data = xmalloc(0x10);
}

static int add_index(struct sltg_data *index, const char *name)
{
    int name_offset = index->size;
    int new_size = index->size + strlen(name) + 1;

    chat("add_index: name_offset %d, \"%s\"\n", name_offset, name);

    if (new_size > index->allocated)
    {
        index->allocated = max(index->allocated * 2, new_size);
        index->data = xrealloc(index->data, index->allocated);
    }

    strcpy(index->data + index->size, name);
    index->size = new_size;

    return name_offset;
}

static void init_index(struct sltg_data *index)
{
    static const char compobj[] = { 1,'C','o','m','p','O','b','j',0 };

    init_sltg_data(index);

    add_index(index, compobj);
}

static int add_name(struct sltg_typelib *sltg, const char *name)
{
    int name_offset = sltg->name_table.size;
    int new_size = sltg->name_table.size + strlen(name) + 1 + 8;
    int aligned_size;

    chat("add_name: %s\n", name);

    aligned_size = (new_size + 0x1f) & ~0x1f;
    if (aligned_size - new_size < 4)
        new_size = aligned_size;
    else
        new_size = (new_size + 1) & ~1;

    if (aligned_size > sltg->name_table.allocated)
    {
        sltg->name_table.allocated = max(sltg->name_table.allocated * 2, aligned_size);
        sltg->name_table.data = xrealloc(sltg->name_table.data, sltg->name_table.allocated);
    }

    memset(sltg->name_table.data + sltg->name_table.size, 0xff, 8);
    strcpy(sltg->name_table.data + sltg->name_table.size + 8, name);
    sltg->name_table.size = new_size;
    sltg->name_table.data[sltg->name_table.size - 1] = 0; /* clear alignment */

    return name_offset;
}

static void init_name_table(struct sltg_typelib *sltg)
{
    init_sltg_data(&sltg->name_table);
}

static void init_library(struct sltg_typelib *sltg)
{
    const attr_t *attr;

    sltg->library.name = add_name(sltg, sltg->typelib->name);
    sltg->library.helpstring = NULL;
    sltg->library.helpcontext = 0;
    sltg->library.syskind = typelib_kind;
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

    chat("add_block: %p,%d,\"%s\"\n", data, size, name);

    block->length = size;
    block->data = data;
    block->index_string = add_index(&sltg->index, name);
    block->next = NULL;

    if (sltg->blocks)
    {
        struct sltg_block *blocks = sltg->blocks;

        while (blocks->next)
            blocks = blocks->next;

        blocks->next = block;
    }
    else
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

static const char *new_index_name(void)
{
    static char name[11] = "0000000000";
    static int pos = 0;
    char *new_name;

    if (name[pos] == 'Z')
    {
        pos++;
        if (pos > 9)
            error("too many index names\n");
    }

    name[pos]++;

    new_name = xmalloc(sizeof(name));
    strcpy(new_name, name);
    return new_name;
}

static void sltg_add_typeinfo(struct sltg_typelib *sltg, void *data, int size, const char *name)
{
    struct sltg_block *block = xmalloc(sizeof(*block));

    chat("sltg_add_typeinfo: %p,%d,%s\n", data, size, name);

    block->length = size;
    block->data = data;
    block->index_string = 0;
    block->next = NULL;

    if (sltg->typeinfo)
    {
        struct sltg_block *typeinfo = sltg->typeinfo;

        while (typeinfo->next)
            typeinfo = typeinfo->next;

        typeinfo->next = block;
    }
    else
        sltg->typeinfo = block;

    sltg->typeinfo_count++;
    sltg->typeinfo_size += size;
}

static void append_data(struct sltg_data *block, const void *data, int size)
{
    int new_size = block->size + size;

    if (new_size > block->allocated)
    {
        block->allocated = max(block->allocated * 2, new_size);
        block->data = xrealloc(block->data, block->allocated);
    }

    memcpy(block->data + block->size, data, size);
    block->size = new_size;
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

static const char *add_typeinfo_block(struct sltg_typelib *typelib, const type_t *type, int kind)
{
    const char *index_name, *other_name;
    void *block;
    short *p;
    int size, helpcontext = 0;
    GUID guid = { 0 };
    const expr_t *expr;

    index_name = new_index_name();
    other_name = new_index_name();

    expr = get_attrp(type->attrs, ATTR_HELPCONTEXT);
    if (expr) helpcontext = expr->cval;

    p = get_attrp(type->attrs, ATTR_UUID);
    if (p) guid = *(GUID *)p;

    size = sizeof(short) * 8 + 10 /* index_name */ * 2 + sizeof(int) + sizeof(GUID);

    block = xmalloc(size);
    p = block;
    *p++ = strlen(index_name);
    strcpy((char *)p, index_name);
    p = (short *)((char *)p + strlen(index_name));
    *p++ = strlen(other_name);
    strcpy((char *)p, other_name);
    p = (short *)((char *)p + strlen(other_name));
    *p++ = -1; /* res1a */
    *p++ = add_name(typelib, type->name); /* name offset */
    *p++ = 0; /* FIXME: helpstring */
    *p++ = -1; /* res20 */
    *(int *)p = helpcontext;
    p += 2;
    *p++ = -1; /* res26 */
    *(GUID *)p = guid;
    p += sizeof(GUID)/2;
    *p = kind;

    sltg_add_typeinfo(typelib, block, size, index_name);

    return index_name;
}

static void init_typeinfo(struct sltg_typeinfo_header *ti, const type_t *type, int kind,
                          const struct sltg_hrefmap *hrefmap)
{
    ti->magic = 0x0501;
    ti->href_offset = -1;
    ti->res06 = -1;
    ti->res0e = -1;
    ti->version = get_attrv(type->attrs, ATTR_VERSION);
    ti->res16 = 0xfffe0000;
    ti->misc.unknown1 = 0x02;
    ti->misc.flags = 0; /* FIXME */
    ti->misc.unknown2 = 0x02;
    ti->misc.typekind = kind;
    ti->res1e = -1;

    ti->member_offset = sizeof(*ti);

    if (hrefmap->href_count)
    {
        char name[64];
        int i, hrefinfo_size;

        hrefinfo_size = sizeof(struct sltg_hrefinfo);

        for (i = 0; i < hrefmap->href_count; i++)
        {
            sprintf(name, "*\\Rffff*#%x", hrefmap->href[i]);
            hrefinfo_size += 8 + 2 + strlen(name);
        }

        ti->href_offset = ti->member_offset;
        ti->member_offset += hrefinfo_size;
    }
}

static void init_sltg_tail(struct sltg_tail *tail)
{
    tail->cFuncs = 0;
    tail->cVars = 0;
    tail->cImplTypes = 0;
    tail->res06 = 0;
    tail->funcs_off = -1;
    tail->vars_off = 0;
    tail->impls_off = -1;
    tail->funcs_bytes = -1;
    tail->vars_bytes = 0;
    tail->impls_bytes = -1;
    tail->tdescalias_vt = -1;
    tail->res16 = -1;
    tail->res18 = 0;
    tail->res1a = 0;
    tail->simple_alias = 0;
    tail->res1e = 0;
    tail->cbSizeInstance = 0;
    tail->cbAlignment = 4;
    tail->res24 = -1;
    tail->res26 = -1;
    tail->cbSizeVft = 0;
    tail->res2a = -1;
    tail->res2c = -1;
    tail->res2e = -1;
    tail->res30 = -1;
    tail->res32 = 0;
    tail->type_bytes = 0;
}

static void write_hrefmap(struct sltg_data *data, const struct sltg_hrefmap *hrefmap)
{
    struct sltg_hrefinfo hrefinfo;
    char name[64];
    int i;

    if (!hrefmap->href_count) return;

    hrefinfo.magic = 0xdf;
    hrefinfo.res01 = 0;
    hrefinfo.res02 = -1;
    hrefinfo.res06 = -1;
    hrefinfo.res0a = -1;
    hrefinfo.res0e = -1;
    hrefinfo.res12 = -1;
    hrefinfo.res16 = -1;
    hrefinfo.res1a = -1;
    hrefinfo.res1e = -1;
    hrefinfo.res22 = -1;
    hrefinfo.res26 = -1;
    hrefinfo.res2a = -1;
    hrefinfo.res2e = -1;
    hrefinfo.res32 = -1;
    hrefinfo.res36 = -1;
    hrefinfo.res3a = -1;
    hrefinfo.res3e = -1;
    hrefinfo.res42 = -1;
    hrefinfo.number = hrefmap->href_count * 8;
    hrefinfo.res50 = -1;
    hrefinfo.res52 = 1;
    hrefinfo.res53 = 0;
    hrefinfo.resxx = 0xdf;

    append_data(data, &hrefinfo, offsetof(struct sltg_hrefinfo, res50));

    for (i = 0; i < hrefmap->href_count; i++)
        append_data(data, "\xff\xff\xff\xff\xff\xff\xff\xff", 8);

    append_data(data, &hrefinfo.res50, 7);

    for (i = 0; i < hrefmap->href_count; i++)
    {
        short len;

        sprintf(name, "*\\Rffff*#%x", hrefmap->href[i]);
        len = strlen(name);

        append_data(data, &len, sizeof(len));
        append_data(data, name, len);
    }

    append_data(data, &hrefinfo.resxx, sizeof(hrefinfo.resxx));
}

static void dump_var_desc(const char *data, int size)
{
    const unsigned char *p = (const unsigned char *)data;
    int i;

    if (!(debuglevel & (DEBUGLEVEL_TRACE | DEBUGLEVEL_CHAT))) return;

    chat("dump_var_desc: size %d bytes\n", size);

    for (i = 0; i < size; i++)
        fprintf(stderr, " %02x", *p++);

    fprintf(stderr, "\n");
}

static int get_element_size(type_t *type)
{
    int vt = get_type_vt(type);

    switch (vt)
    {
    case VT_I1:
    case VT_UI1:
        return 1;

    case VT_INT:
    case VT_UINT:
        return typelib_kind == SYS_WIN16 ? 2 : 4;

    case VT_UI2:
    case VT_I2:
    case VT_BOOL:
        return 2;

    case VT_I4:
    case VT_UI4:
    case VT_R4:
    case VT_ERROR:
    case VT_HRESULT:
        return 4;

    case VT_R8:
    case VT_I8:
    case VT_UI8:
    case VT_CY:
    case VT_DATE:
        return 8;

    case VT_DECIMAL:
        return 16;

    case VT_PTR:
    case VT_UNKNOWN:
    case VT_DISPATCH:
    case VT_BSTR:
    case VT_LPSTR:
    case VT_LPWSTR:
        return pointer_size;

    case VT_VOID:
        return 0;

    case VT_VARIANT:
        return pointer_size == 8 ? 24 : 16;

    case VT_USERDEFINED:
        return 0;

    default:
        error("get_element_size: unrecognized vt %d\n", vt);
        break;
    }

    return 0;
}

static int local_href(struct sltg_hrefmap *hrefmap, int typelib_href)
{
    int i, href = -1;

    for (i = 0; i < hrefmap->href_count; i++)
    {
        if (hrefmap->href[i] == typelib_href)
        {
            href = i;
            break;
        }
    }

    if (href == -1)
    {
        href = hrefmap->href_count;

        if (hrefmap->href)
            hrefmap->href = xrealloc(hrefmap->href, sizeof(*hrefmap->href) * (hrefmap->href_count + 1));
        else
            hrefmap->href = xmalloc(sizeof(*hrefmap->href));

        hrefmap->href[hrefmap->href_count] = typelib_href;
        hrefmap->href_count++;
    }

    chat("typelib href %d mapped to local href %d\n", typelib_href, href);

    return href << 2;
}

static short write_var_desc(struct sltg_data *data, type_t *type, short flags, short base_offset,
                            int *size_instance, struct sltg_hrefmap *hrefmap)
{
    short vt, vt_flags, desc_offset;

    chat("write_var_desc: type %p, type->name %s\n",
         type, type->name ? type->name : "NULL");

    if (is_array(type) && !type_array_is_decl_as_ptr(type))
    {
        int num_dims, elements, array_start, size, array_size;
        type_t *atype;
        struct
        {
            short cDims;
            short fFeatures;
            int cbElements;
            int cLocks;
            void *pvData;
            int bound[2];
        } *array;
        int *bound;
        short vt_off[2];

        elements = 1;
        num_dims = 0;

        atype = type;

        while (is_array(atype) && !type_array_is_decl_as_ptr(atype))
        {
            num_dims++;
            elements *= type_array_get_dim(atype);

            atype = type_array_get_element(atype);
        }

        chat("write_var_desc: VT_CARRAY: %d dimensions, %d elements\n", num_dims, elements);

        array_start = data->size;

        size = sizeof(*array) + (num_dims - 1) * 8 /* sizeof(SAFEARRAYBOUND) */;
        array = xmalloc(size);

        array->cDims = num_dims;
        array->fFeatures = 0x0004; /* FADF_EMBEDDED */
        array->cbElements = get_element_size(atype);
        array->cLocks = 0;
        array->pvData = NULL;

        bound = array->bound;

        array_size = array->cbElements;
        atype = type;

        while (is_array(atype) && !type_array_is_decl_as_ptr(atype))
        {
            bound[0] = type_array_get_dim(atype);
            array_size *= bound[0];
            bound[1] = 0;
            bound += 2;

            atype = type_array_get_element(atype);
        }

        *size_instance += array_size;
        size_instance = NULL; /* don't account for element size */

        append_data(data, array, size);

        desc_offset = data->size;

        vt_off[0] = VT_CARRAY;
        vt_off[1] = array_start + base_offset;
        append_data(data, vt_off, sizeof(vt_off));

        /* fall through to write array element description */
        type = atype;
    }
    else
        desc_offset = data->size;

    vt = get_type_vt(type);

    if (vt == VT_PTR)
    {
        type_t *ref = is_ptr(type) ? type_pointer_get_ref(type) : type_array_get_element(type);

        if (is_ptr(ref))
        {
            chat("write_var_desc: vt VT_PTR | 0x0400\n");
            vt = VT_PTR | 0x0400;
            append_data(data, &vt, sizeof(vt));
            write_var_desc(data, ref, 0, base_offset, size_instance, hrefmap);
        }
        else
            write_var_desc(data, ref, 0x0e00, base_offset, size_instance, hrefmap);
        return desc_offset;
    }

    chat("write_var_desc: vt %d, flags %04x\n", vt, flags);

    vt_flags = vt | flags;
    append_data(data, &vt_flags, sizeof(vt_flags));

    if (vt == VT_USERDEFINED)
    {
        short href;

        while (type->typelib_idx < 0 && type_is_alias(type))
            type = type_alias_get_aliasee(type);

        chat("write_var_desc: VT_USERDEFINED, type %p, name %s, real type %d, href %d\n",
             type, type->name, type_get_type(type), type->typelib_idx);

        if (type->typelib_idx == -1)
            error("write_var_desc: trying to ref not added type\n");

        href = local_href(hrefmap, type->typelib_idx);
        chat("write_var_desc: VT_USERDEFINED, local href %d\n", href);

        append_data(data, &href, sizeof(href));
    }

    if (size_instance)
        *size_instance += get_element_size(type);

    return desc_offset;
}

static void add_structure_typeinfo(struct sltg_typelib *typelib, type_t *type)
{
    struct sltg_data data, *var_data = NULL;
    struct sltg_hrefmap hrefmap;
    const char *index_name;
    struct sltg_typeinfo_header ti;
    struct sltg_member_header member;
    struct sltg_tail tail;
    int member_offset, var_count = 0, var_data_size = 0, size_instance = 0;
    short *type_desc_offset = NULL;

    chat("add_structure_typeinfo: type %p, type->name %s\n", type, type->name);

    type->typelib_idx = typelib->n_file_blocks;

    hrefmap.href_count = 0;
    hrefmap.href = NULL;

    init_sltg_data(&data);

    index_name = add_typeinfo_block(typelib, type, TKIND_RECORD);

    if (type_struct_get_fields(type))
    {
        int i = 0;
        var_t *var;

        var_count = list_count(type_struct_get_fields(type));

        var_data = xmalloc(var_count * sizeof(*var_data));
        type_desc_offset = xmalloc(var_count * sizeof(*type_desc_offset));

        LIST_FOR_EACH_ENTRY(var, type_struct_get_fields(type), var_t, entry)
        {
            short base_offset;

            chat("add_structure_typeinfo: var %p, name %s, type %p\n",
                 var, var->name, var->type);

            init_sltg_data(&var_data[i]);

            base_offset = var_data_size + (i + 1) * sizeof(struct sltg_variable);
            type_desc_offset[i] = write_var_desc(&var_data[i], var->type, 0, base_offset, &size_instance, &hrefmap);
            dump_var_desc(var_data[i].data, var_data[i].size);

            if (var_data[i].size > sizeof(short))
                var_data_size += var_data[i].size;
            i++;
        }
    }

    init_typeinfo(&ti, type, TKIND_RECORD, &hrefmap);
    append_data(&data, &ti, sizeof(ti));

    write_hrefmap(&data, &hrefmap);

    member_offset = data.size;

    member.res00 = 0x0001;
    member.res02 = 0xffff;
    member.res04 = 0x01;
    member.extra = var_data_size + var_count * sizeof(struct sltg_variable);
    append_data(&data, &member, sizeof(member));

    var_data_size = 0;

    if (type_struct_get_fields(type))
    {
        int i = 0;
        short next = member_offset;
        var_t *var;

        LIST_FOR_EACH_ENTRY(var, type_struct_get_fields(type), var_t, entry)
        {
            struct sltg_variable variable;

            next += sizeof(variable);

            variable.magic = 0x2a; /* always write flags to simplify calculations */
            variable.name = add_name(typelib, var->name);
            variable.byte_offs = 0;
            if (var_data[i].size > sizeof(short))
            {
                variable.flags = 0;
                var_data_size = next - member_offset + type_desc_offset[i];
                variable.type = var_data_size;
                next += var_data[i].size;
            }
            else
            {
                variable.flags = 0x02;
                variable.type = *(short *)var_data[i].data;
            }
            variable.next = i < var_count - 1 ? next - member_offset : -1;
            variable.memid = 0x40000000 + i;
            variable.helpcontext = -2; /* 0xfffe */
            variable.helpstring = -1;
            variable.varflags = 0;

            append_data(&data, &variable, sizeof(variable));
            if (var_data[i].size > sizeof(short))
                append_data(&data, var_data[i].data, var_data[i].size);

            i++;
        }
    }

    init_sltg_tail(&tail);

    tail.cVars = var_count;
    tail.vars_bytes = var_data_size;
    tail.cbSizeInstance = size_instance;
    tail.type_bytes = data.size - member_offset - sizeof(member);
    append_data(&data, &tail, sizeof(tail));

    add_block(typelib, data.data, data.size, index_name);
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
        entry.next = header.first_blk + i + 1;
        chat("sltg_write_header: writing block entry %d: length %#x, index_string %#x, next %#x\n",
             i, entry.length, entry.index_string, entry.next);
        put_data(&entry, sizeof(entry));

        block = block->next;
    }

    assert(block->next == NULL);

    /* library block length includes helpstrings and name table */
    entry.length = block->length + 0x40 + 2 + sltg->typeinfo_size + 4 + 6 + 12 + 0x200 + sltg->name_table.size + 12;
    entry.index_string = block->index_string;
    entry.next = 0;
    chat("sltg_write_header: writing library block entry %d: length %#x, index_string %#x, next %#x\n",
         i, entry.length, entry.index_string, entry.next);
    put_data(&entry, sizeof(entry));

    chat("sltg_write_header: writing index: %d bytes\n", sltg->index.size);
    put_data(sltg->index.data, sltg->index.size);
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
    chat("library_block_start = %#lx\n", (SIZE_T)output_buffer_pos);
    *library_block_start = output_buffer_pos;
    chat("sltg_write_header: writing library block %d: %d bytes\n", i, block->length);
    put_data(block->data, block->length);

    chat("sltg_write_header: writing pad 0x40 bytes\n");
    memset(pad, 0xff, 0x40);
    put_data(pad, 0x40);
}

static void sltg_write_typeinfo(struct sltg_typelib *typelib)
{
    int i;
    struct sltg_block *block;
    short count = typelib->typeinfo_count;

    put_data(&count, sizeof(count));

    block = typelib->typeinfo;
    for (i = 0; i < typelib->typeinfo_count; i++)
    {
        chat("sltg_write_typeinfo: writing block %d: %d bytes\n", i, block->length);

        put_data(block->data, block->length);
        block = block->next;
    }
    assert(block == NULL);
}

static void sltg_write_helpstrings(struct sltg_typelib *typelib)
{
    static const char dummy[6];

    chat("sltg_write_helpstrings: writing dummy 6 bytes\n");

    put_data(dummy, sizeof(dummy));
}

static void sltg_write_nametable(struct sltg_typelib *typelib)
{
    static const short dummy[6] = { 0xffff,1,2,0xff00,0xffff,0xffff };
    char pad[0x200];

    chat("sltg_write_nametable: writing 12+0x200+%d bytes\n", typelib->name_table.size);

    put_data(dummy, sizeof(dummy));
    memset(pad, 0xff, 0x200);
    put_data(pad, 0x200);
    put_data(&typelib->name_table.size, sizeof(typelib->name_table.size));
    put_data(typelib->name_table.data, typelib->name_table.size);
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
    chat("name_table_offset = %#lx\n", (SIZE_T)output_buffer_pos);
    put_data(&library_block_start, sizeof(library_block_start));

    sltg_write_helpstrings(typelib);

    *name_table_offset = output_buffer_pos - library_block_start;
    chat("*name_table_offset = %#x\n", *name_table_offset);

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

    pointer_size = (typelib_kind == SYS_WIN64) ? 8 : 4;

    sltg.typelib = typelib;
    sltg.typeinfo_count = 0;
    sltg.typeinfo_size = 0;
    sltg.typeinfo = NULL;
    sltg.blocks = NULL;
    sltg.n_file_blocks = 0;
    sltg.first_block = 1;

    init_index(&sltg.index);
    init_name_table(&sltg);
    init_library(&sltg);

    if (typelib->stmts)
        LIST_FOR_EACH_ENTRY(stmt, typelib->stmts, const statement_t, entry)
            add_statement(&sltg, stmt);

    add_library_block(&sltg);

    save_all_changes(&sltg);

    return 1;
}
