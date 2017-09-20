/*
 * Win32 heap functions
 *
 * Copyright 1996 Alexandre Julliard
 * Copyright 1998 Ulrich Weigand
 * Copyright 2017 Sebastian Lackner
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
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#ifdef HAVE_VALGRIND_MEMCHECK_H
#include <valgrind/memcheck.h>
#else
#define RUNNING_ON_VALGRIND 0
#endif

#include "ntstatus.h"
#define WIN32_NO_STATUS
#define NONAMELESSUNION
#include "windef.h"
#include "winnt.h"
#include "winternl.h"
#include "ntdll_misc.h"
#include "wine/list.h"
#include "wine/rbtree.h"
#include "wine/debug.h"
#include "wine/server.h"

WINE_DEFAULT_DEBUG_CHANNEL(heap);

/* Note: the heap data structures are loosely based on what Pietrek describes in his
 * book 'Windows 95 System Programming Secrets', with some adaptations for
 * better compatibility with NT.
 */

typedef struct tagARENA_INUSE
{
    DWORD  size;                    /* Block size; must be the first field */
    DWORD  magic : 24;              /* Magic number */
    DWORD  unused_bytes : 8;        /* Number of bytes in the block not used by user data (max value is HEAP_MIN_DATA_SIZE+HEAP_MIN_SHRINK_SIZE) */
} ARENA_INUSE;

typedef struct tagARENA_FREE
{
    DWORD                 size;     /* Block size; must be the first field */
    DWORD                 magic;    /* Magic number */
    union
    {
        struct list list;           /* Entry in free list */
        struct wine_rb_entry tree;  /* Entry in free tree */
    } entry;
} ARENA_FREE;

typedef struct
{
    struct list           entry;      /* entry in heap large blocks list */
    SIZE_T                data_size;  /* size of user data */
    SIZE_T                block_size; /* total size of virtual memory block */
    DWORD                 pad[2];     /* padding to ensure 16-byte alignment of data */
    DWORD                 size;       /* fields for compatibility with normal arenas */
    DWORD                 magic;      /* these must remain at the end of the structure */
} ARENA_LARGE;

#define ARENA_FLAG_FREE_LIST   0x00000001  /* flags OR'ed with arena size */
#define ARENA_FLAG_FREE_TREE   0x00000002
#define ARENA_FLAG_FREE        (ARENA_FLAG_FREE_LIST | ARENA_FLAG_FREE_TREE)
#define ARENA_FLAG_PREV_FREE   0x00000004
#define ARENA_SIZE_MASK        (~7)
#define ARENA_LARGE_SIZE       0xfedcba90  /* magic value for 'size' field in large blocks */

/* Value for arena 'magic' field */
#define ARENA_INUSE_MAGIC      0x455355
#define ARENA_PENDING_MAGIC    0xbedead
#define ARENA_FREE_MAGIC       0x45455246
#define ARENA_LARGE_MAGIC      0x6752614c

#define ARENA_INUSE_FILLER     0x55
#define ARENA_TAIL_FILLER      0xab
#define ARENA_FREE_FILLER      0xfeeefeee

/* everything is aligned on 8 byte boundaries (16 for Win64) */
#define ALIGNMENT              (2*sizeof(void*))
#define LARGE_ALIGNMENT        16  /* large blocks have stricter alignment */
#define ARENA_OFFSET           (ALIGNMENT - sizeof(ARENA_INUSE))

C_ASSERT( (sizeof(ARENA_INUSE) & ~ARENA_SIZE_MASK) == 0 );
C_ASSERT( (sizeof(ARENA_FREE) & ~ARENA_SIZE_MASK) == 0 );
C_ASSERT( sizeof(ARENA_LARGE) % LARGE_ALIGNMENT == 0 );

#define ROUND_SIZE(size)       ((((size) + ALIGNMENT - 1) & ~(ALIGNMENT-1)) + ARENA_OFFSET)

#define QUIET                  1           /* Suppress messages  */
#define NOISY                  0           /* Report all errors  */

/* minimum data size (without arenas) of an allocated block */
#define HEAP_MIN_DATA_SIZE    ROUND_SIZE(sizeof(ARENA_FREE) + sizeof(ARENA_FREE*) - sizeof(ARENA_INUSE))
/* minimum size that must remain to shrink an allocated block */
#define HEAP_MIN_SHRINK_SIZE  (HEAP_MIN_DATA_SIZE+sizeof(ARENA_FREE))
/* minimum size to start allocating large blocks */
#define HEAP_MIN_LARGE_BLOCK_SIZE  0x7f000
/* extra size to add at the end of block for tail checking */
#define HEAP_TAIL_EXTRA_SIZE(flags) \
    ((flags & HEAP_TAIL_CHECKING_ENABLED) || RUNNING_ON_VALGRIND ? ALIGNMENT : 0)
/* size of the blocks on the free lists */
#define HEAP_FREELIST_SIZE(index) \
    ((DWORD)(((index) * ALIGNMENT) + HEAP_MIN_DATA_SIZE + sizeof(ARENA_INUSE)))
/* returns index for a given block size */
#define HEAP_SIZE_TO_FREELIST_INDEX(size) \
    (((size) - HEAP_MIN_DATA_SIZE - sizeof(ARENA_INUSE)) / ALIGNMENT)
/* number of free lists */
#define HEAP_NB_FREE_LISTS  128

struct tagHEAP;

typedef struct tagSUBHEAP
{
    void               *base;       /* Base address of the sub-heap memory block */
    SIZE_T              size;       /* Size of the whole sub-heap */
    SIZE_T              min_commit; /* Minimum committed size */
    SIZE_T              commitSize; /* Committed size of the sub-heap */
    struct list         entry;      /* Entry in sub-heap list */
    struct tagHEAP     *heap;       /* Main heap structure */
    DWORD               headerSize; /* Size of the heap header */
    DWORD               magic;      /* Magic number */
} SUBHEAP;

#define SUBHEAP_MAGIC    ((DWORD)('S' | ('U'<<8) | ('B'<<16) | ('H'<<24)))

typedef struct tagHEAP
{
    DWORD_PTR        unknown1[2];
    DWORD            unknown2;
    DWORD            flags;         /* Heap flags */
    DWORD            force_flags;   /* Forced heap flags for debugging */
    SUBHEAP          subheap;       /* First sub-heap */
    struct list      entry;         /* Entry in process heap list */
    struct list      subheap_list;  /* Sub-heap list */
    struct list      large_list;    /* Large blocks list */
    SIZE_T           grow_size;     /* Size of next subheap for growing heap */
    DWORD            magic;         /* Magic number */
    DWORD            pending_pos;   /* Position in pending free requests ring */
    ARENA_INUSE    **pending_free;  /* Ring buffer for pending free requests */
    RTL_CRITICAL_SECTION critSection; /* Critical section for serialization */
    struct list     *freeList;      /* Free lists */
    struct wine_rb_tree freeTree;   /* Free tree */
    unsigned long    freeMask[HEAP_NB_FREE_LISTS / (8 * sizeof(unsigned long))];
} HEAP;

#define HEAP_FREEMASK_BLOCK    (8 * sizeof(unsigned long))
#define HEAP_FREEMASK_INDEX(x) ((x) / HEAP_FREEMASK_BLOCK)
#define HEAP_FREEMASK_BIT(x)   (1UL << ((x) & (HEAP_FREEMASK_BLOCK - 1)))

C_ASSERT( HEAP_NB_FREE_LISTS % HEAP_FREEMASK_BLOCK == 0 );

#define HEAP_MAGIC       ((DWORD)('H' | ('E'<<8) | ('A'<<16) | ('P'<<24)))

#define HEAP_DEF_SIZE        0x110000   /* Default heap size = 1Mb + 64Kb */
#define COMMIT_MASK          0xffff  /* bitmask for commit/decommit granularity */
#define MAX_FREE_PENDING     1024    /* max number of free requests to delay */

/* some undocumented flags (names are made up) */
#define HEAP_PAGE_ALLOCS      0x01000000
#define HEAP_VALIDATE         0x10000000
#define HEAP_VALIDATE_ALL     0x20000000
#define HEAP_VALIDATE_PARAMS  0x40000000

static HEAP *processHeap;  /* main process heap */

static BOOL HEAP_IsRealArena( HEAP *heapPtr, DWORD flags, LPCVOID block, BOOL quiet );

/* get arena size for an rb tree entry */
static inline DWORD get_arena_size( const struct wine_rb_entry *entry )
{
    ARENA_FREE *arena = WINE_RB_ENTRY_VALUE( entry, ARENA_FREE, entry.tree );
    return (arena->size & ARENA_SIZE_MASK);
}

/* return number of trailing 0-bits in x */
static inline int ctzl(unsigned long x)
{
#ifdef HAVE___BUILTIN_CTZL
    return __builtin_ctzl(x);
#else
    int c = 1;
    if (!(x & 0xffffffff)) { x >>= 32; c += 32; }
    if (!(x & 0x0000ffff)) { x >>= 16; c += 16; }
    if (!(x & 0x000000ff)) { x >>=  8; c +=  8; }
    if (!(x & 0x0000000f)) { x >>=  4; c +=  4; }
    if (!(x & 0x00000003)) { x >>=  2; c +=  2; }
    c -= (x & 0x00000001);
    return c;
#endif
}

/* mark a block of memory as free for debugging purposes */
static inline void mark_block_free( void *ptr, SIZE_T size, DWORD flags )
{
    if (flags & HEAP_FREE_CHECKING_ENABLED)
    {
        SIZE_T i;
        for (i = 0; i < size / sizeof(DWORD); i++) ((DWORD *)ptr)[i] = ARENA_FREE_FILLER;
    }
#if defined(VALGRIND_MAKE_MEM_NOACCESS)
    VALGRIND_DISCARD( VALGRIND_MAKE_MEM_NOACCESS( ptr, size ));
#elif defined( VALGRIND_MAKE_NOACCESS)
    VALGRIND_DISCARD( VALGRIND_MAKE_NOACCESS( ptr, size ));
#endif
}

/* mark a block of memory as initialized for debugging purposes */
static inline void mark_block_initialized( void *ptr, SIZE_T size )
{
#if defined(VALGRIND_MAKE_MEM_DEFINED)
    VALGRIND_DISCARD( VALGRIND_MAKE_MEM_DEFINED( ptr, size ));
#elif defined(VALGRIND_MAKE_READABLE)
    VALGRIND_DISCARD( VALGRIND_MAKE_READABLE( ptr, size ));
#endif
}

/* mark a block of memory as uninitialized for debugging purposes */
static inline void mark_block_uninitialized( void *ptr, SIZE_T size )
{
#if defined(VALGRIND_MAKE_MEM_UNDEFINED)
    VALGRIND_DISCARD( VALGRIND_MAKE_MEM_UNDEFINED( ptr, size ));
#elif defined(VALGRIND_MAKE_WRITABLE)
    VALGRIND_DISCARD( VALGRIND_MAKE_WRITABLE( ptr, size ));
#endif
}

/* mark a block of memory as a tail block */
static inline void mark_block_tail( void *ptr, SIZE_T size, DWORD flags )
{
    if (flags & HEAP_TAIL_CHECKING_ENABLED)
    {
        mark_block_uninitialized( ptr, size );
        memset( ptr, ARENA_TAIL_FILLER, size );
    }
#if defined(VALGRIND_MAKE_MEM_NOACCESS)
    VALGRIND_DISCARD( VALGRIND_MAKE_MEM_NOACCESS( ptr, size ));
#elif defined( VALGRIND_MAKE_NOACCESS)
    VALGRIND_DISCARD( VALGRIND_MAKE_NOACCESS( ptr, size ));
#endif
}

/* initialize contents of a newly created block of memory */
static inline void initialize_block( void *ptr, SIZE_T size, SIZE_T unused, DWORD flags )
{
    if (flags & HEAP_ZERO_MEMORY)
    {
        mark_block_initialized( ptr, size );
        memset( ptr, 0, size );
    }
    else
    {
        mark_block_uninitialized( ptr, size );
        if (flags & HEAP_FREE_CHECKING_ENABLED)
        {
            memset( ptr, ARENA_INUSE_FILLER, size );
            mark_block_uninitialized( ptr, size );
        }
    }

    mark_block_tail( (char *)ptr + size, unused, flags );
}

/* notify that a new block of memory has been allocated for debugging purposes */
static inline void notify_alloc( void *ptr, SIZE_T size, BOOL init )
{
#ifdef VALGRIND_MALLOCLIKE_BLOCK
    VALGRIND_MALLOCLIKE_BLOCK( ptr, size, 0, init );
#endif
}

/* notify that a block of memory has been freed for debugging purposes */
static inline void notify_free( void const *ptr )
{
#ifdef VALGRIND_FREELIKE_BLOCK
    VALGRIND_FREELIKE_BLOCK( ptr, 0 );
#endif
}

static inline void notify_realloc( void const *ptr, SIZE_T size_old, SIZE_T size_new )
{
#ifdef VALGRIND_RESIZEINPLACE_BLOCK
    /* zero is not a valid size */
    VALGRIND_RESIZEINPLACE_BLOCK( ptr, size_old ? size_old : 1, size_new ? size_new : 1, 0 );
#endif
}

static void subheap_notify_free_all(SUBHEAP const *subheap)
{
#ifdef VALGRIND_FREELIKE_BLOCK
    char const *ptr = (char const *)subheap->base + subheap->headerSize;

    if (!RUNNING_ON_VALGRIND) return;

    while (ptr < (char const *)subheap->base + subheap->size)
    {
        if (*(const DWORD *)ptr & ARENA_FLAG_FREE)
        {
            ARENA_FREE const *pArena = (ARENA_FREE const *)ptr;
            if (pArena->magic!=ARENA_FREE_MAGIC) ERR("bad free_magic @%p\n", pArena);
            ptr += sizeof(*pArena) + (pArena->size & ARENA_SIZE_MASK);
        }
        else
        {
            ARENA_INUSE const *pArena = (ARENA_INUSE const *)ptr;
            if (pArena->magic == ARENA_INUSE_MAGIC) notify_free(pArena + 1);
            else if (pArena->magic != ARENA_PENDING_MAGIC) ERR("bad inuse_magic @%p\n", pArena);
            ptr += sizeof(*pArena) + (pArena->size & ARENA_SIZE_MASK);
        }
    }
#endif
}

/* get the memory protection type to use for a given heap */
static inline ULONG get_protection_type( DWORD flags )
{
    return (flags & HEAP_CREATE_ENABLE_EXECUTE) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
}

static RTL_CRITICAL_SECTION_DEBUG process_heap_critsect_debug =
{
    0, 0, NULL,  /* will be set later */
    { &process_heap_critsect_debug.ProcessLocksList, &process_heap_critsect_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": main process heap section") }
};


/***********************************************************************
 *           HEAP_Dump
 */
static void HEAP_Dump( HEAP *heap )
{
    unsigned int i;
    SUBHEAP *subheap;
    char *ptr;

    DPRINTF( "Heap: %p\n", heap );
    DPRINTF( "Next: %p  Sub-heaps:", LIST_ENTRY( heap->entry.next, HEAP, entry ) );
    LIST_FOR_EACH_ENTRY( subheap, &heap->subheap_list, SUBHEAP, entry ) DPRINTF( " %p", subheap );

    DPRINTF( "\nFree lists:\n" );
    for (i = 0; i < HEAP_NB_FREE_LISTS; i++)
    {
        BOOL empty = !(heap->freeMask[ HEAP_FREEMASK_INDEX( i ) ] & HEAP_FREEMASK_BIT( i ));

        DPRINTF( "free %08x: ", HEAP_FREELIST_SIZE( i ) );
        if (!empty && !list_empty( &heap->freeList[i] ))
        {
            DPRINTF( "head=%p tail=%p\n",
                     LIST_ENTRY( heap->freeList[i].next, ARENA_FREE, entry.list ),
                     LIST_ENTRY( heap->freeList[i].prev, ARENA_FREE, entry.list ));
        }
        else if (empty && list_empty( &heap->freeList[i] ))
        {
            DPRINTF( "(empty)\n" );
        }
        else
        {
            DPRINTF( "(corrupted)\n" );
        }
    }

    DPRINTF( "free %08x: root=%p\n",
             HEAP_FREELIST_SIZE( HEAP_NB_FREE_LISTS ),
             heap->freeTree.root ? LIST_ENTRY( heap->freeTree.root, ARENA_FREE, entry.tree ) : NULL);

    LIST_FOR_EACH_ENTRY( subheap, &heap->subheap_list, SUBHEAP, entry )
    {
        SIZE_T freeSize = 0, usedSize = 0, arenaSize = subheap->headerSize;
        DPRINTF( "\n\nSub-heap %p: base=%p size=%08lx committed=%08lx\n",
                 subheap, subheap->base, subheap->size, subheap->commitSize );

        DPRINTF( "\n Block    Arena   Stat   Size    Id\n" );
        ptr = (char *)subheap->base + subheap->headerSize;
        while (ptr < (char *)subheap->base + subheap->size)
        {
            if (*(DWORD *)ptr & ARENA_FLAG_FREE)
            {
                ARENA_FREE *pArena = (ARENA_FREE *)ptr;
                if ((pArena->size & ARENA_FLAG_FREE) == ARENA_FLAG_FREE_LIST)
                {
                    DPRINTF( "%p %08x free %08x prev=%p next=%p\n",
                              pArena, pArena->magic, pArena->size & ARENA_SIZE_MASK,
                              LIST_ENTRY( pArena->entry.list.prev, ARENA_FREE, entry.list ),
                              LIST_ENTRY( pArena->entry.list.next, ARENA_FREE, entry.list ) );
                }
                else if ((pArena->size & ARENA_FLAG_FREE) == ARENA_FLAG_FREE_TREE)
                {
                    ARENA_FREE *parent = NULL, *left = NULL, *right = NULL;

                    if (pArena->entry.tree.parent)
                        parent = WINE_RB_ENTRY_VALUE( pArena->entry.tree.parent, ARENA_FREE, entry.tree );
                    if (pArena->entry.tree.left)
                        left = WINE_RB_ENTRY_VALUE( pArena->entry.tree.left, ARENA_FREE, entry.tree );
                    if (pArena->entry.tree.right)
                        right = WINE_RB_ENTRY_VALUE( pArena->entry.tree.right, ARENA_FREE, entry.tree );

                    DPRINTF( "%p %08x free %08x parent=%p left=%p right=%p\n",
                             pArena, pArena->magic, pArena->size & ARENA_SIZE_MASK, parent, left, right );
                }
                else
                {
                    DPRINTF( "%p %08x free %08x corrupted\n",
                             pArena, pArena->magic, pArena->size & ARENA_SIZE_MASK );
                }
                ptr += sizeof(*pArena) + (pArena->size & ARENA_SIZE_MASK);
                arenaSize += sizeof(ARENA_FREE);
                freeSize += pArena->size & ARENA_SIZE_MASK;
            }
            else if (*(DWORD *)ptr & ARENA_FLAG_PREV_FREE)
            {
                ARENA_INUSE *pArena = (ARENA_INUSE *)ptr;
                DPRINTF( "%p %08x Used %08x back=%p\n",
                        pArena, pArena->magic, pArena->size & ARENA_SIZE_MASK, *((ARENA_FREE **)pArena - 1) );
                ptr += sizeof(*pArena) + (pArena->size & ARENA_SIZE_MASK);
                arenaSize += sizeof(ARENA_INUSE);
                usedSize += pArena->size & ARENA_SIZE_MASK;
            }
            else
            {
                ARENA_INUSE *pArena = (ARENA_INUSE *)ptr;
                DPRINTF( "%p %08x %s %08x\n",
                         pArena, pArena->magic, pArena->magic == ARENA_INUSE_MAGIC ? "used" : "pend",
                         pArena->size & ARENA_SIZE_MASK );
                ptr += sizeof(*pArena) + (pArena->size & ARENA_SIZE_MASK);
                arenaSize += sizeof(ARENA_INUSE);
                usedSize += pArena->size & ARENA_SIZE_MASK;
            }
        }
        DPRINTF( "\nTotal: Size=%08lx Committed=%08lx Free=%08lx Used=%08lx Arenas=%08lx (%ld%%)\n\n",
	      subheap->size, subheap->commitSize, freeSize, usedSize,
	      arenaSize, (arenaSize * 100) / subheap->size );
    }
}


static void HEAP_DumpEntry( LPPROCESS_HEAP_ENTRY entry )
{
    WORD rem_flags;
    TRACE( "Dumping entry %p\n", entry );
    TRACE( "lpData\t\t: %p\n", entry->lpData );
    TRACE( "cbData\t\t: %08x\n", entry->cbData);
    TRACE( "cbOverhead\t: %08x\n", entry->cbOverhead);
    TRACE( "iRegionIndex\t: %08x\n", entry->iRegionIndex);
    TRACE( "WFlags\t\t: ");
    if (entry->wFlags & PROCESS_HEAP_REGION)
        TRACE( "PROCESS_HEAP_REGION ");
    if (entry->wFlags & PROCESS_HEAP_UNCOMMITTED_RANGE)
        TRACE( "PROCESS_HEAP_UNCOMMITTED_RANGE ");
    if (entry->wFlags & PROCESS_HEAP_ENTRY_BUSY)
        TRACE( "PROCESS_HEAP_ENTRY_BUSY ");
    if (entry->wFlags & PROCESS_HEAP_ENTRY_MOVEABLE)
        TRACE( "PROCESS_HEAP_ENTRY_MOVEABLE ");
    if (entry->wFlags & PROCESS_HEAP_ENTRY_DDESHARE)
        TRACE( "PROCESS_HEAP_ENTRY_DDESHARE ");
    rem_flags = entry->wFlags &
        ~(PROCESS_HEAP_REGION | PROCESS_HEAP_UNCOMMITTED_RANGE |
          PROCESS_HEAP_ENTRY_BUSY | PROCESS_HEAP_ENTRY_MOVEABLE|
          PROCESS_HEAP_ENTRY_DDESHARE);
    if (rem_flags)
        TRACE( "Unknown %08x", rem_flags);
    TRACE( "\n");
    if ((entry->wFlags & PROCESS_HEAP_ENTRY_BUSY )
        && (entry->wFlags & PROCESS_HEAP_ENTRY_MOVEABLE))
    {
        /* Treat as block */
        TRACE( "BLOCK->hMem\t\t:%p\n", entry->u.Block.hMem);
    }
    if (entry->wFlags & PROCESS_HEAP_REGION)
    {
        TRACE( "Region.dwCommittedSize\t:%08x\n",entry->u.Region.dwCommittedSize);
        TRACE( "Region.dwUnCommittedSize\t:%08x\n",entry->u.Region.dwUnCommittedSize);
        TRACE( "Region.lpFirstBlock\t:%p\n",entry->u.Region.lpFirstBlock);
        TRACE( "Region.lpLastBlock\t:%p\n",entry->u.Region.lpLastBlock);
    }
}

/***********************************************************************
 *           HEAP_GetPtr
 * RETURNS
 *	Pointer to the heap
 *	NULL: Failure
 */
static HEAP *HEAP_GetPtr(
             HANDLE heap /* [in] Handle to the heap */
) {
    HEAP *heapPtr = heap;
    if (!heapPtr || (heapPtr->magic != HEAP_MAGIC))
    {
        ERR("Invalid heap %p!\n", heap );
        return NULL;
    }
    if ((heapPtr->flags & HEAP_VALIDATE_ALL) && !HEAP_IsRealArena( heapPtr, 0, NULL, NOISY ))
    {
        if (TRACE_ON(heap))
        {
            HEAP_Dump( heapPtr );
            assert( FALSE );
        }
        return NULL;
    }
    return heapPtr;
}


/***********************************************************************
 *           HEAP_InsertFreeBlock
 *
 * Insert a free block into the free list.
 */
static inline void HEAP_InsertFreeBlock( HEAP *heap, ARENA_FREE *pArena, BOOL last )
{
    SIZE_T index = HEAP_SIZE_TO_FREELIST_INDEX( pArena->size + sizeof(*pArena) );

    if (index < HEAP_NB_FREE_LISTS)
    {
        list_add_tail( &heap->freeList[index], &pArena->entry.list );
        heap->freeMask[ HEAP_FREEMASK_INDEX( index ) ] |= HEAP_FREEMASK_BIT( index );
        pArena->size |= ARENA_FLAG_FREE_LIST;
    }
    else
    {
        wine_rb_put( &heap->freeTree, &pArena->size, &pArena->entry.tree );
        pArena->size |= ARENA_FLAG_FREE_TREE;
    }
}


/***********************************************************************
 *           HEAP_DeleteFreeBlock
 *
 * Delete a free block from the free list.
 */
static inline void HEAP_DeleteFreeBlock( HEAP *heap, ARENA_FREE *pArena )
{
    if ((pArena->size & ARENA_FLAG_FREE) == ARENA_FLAG_FREE_LIST)
    {
        if (pArena->entry.list.prev == pArena->entry.list.next)
        {
            SIZE_T index = HEAP_SIZE_TO_FREELIST_INDEX( (pArena->size & ARENA_SIZE_MASK) + sizeof(*pArena) );
            heap->freeMask[ HEAP_FREEMASK_INDEX( index ) ] &= ~HEAP_FREEMASK_BIT( index );
        }
        list_remove( &pArena->entry.list );
    }
    else
    {
        wine_rb_remove( &heap->freeTree, &pArena->entry.tree );
    }
}


/***********************************************************************
 *           HEAP_FindSubHeap
 * Find the sub-heap containing a given address.
 *
 * RETURNS
 *	Pointer: Success
 *	NULL: Failure
 */
static SUBHEAP *HEAP_FindSubHeap(
                const HEAP *heap, /* [in] Heap pointer */
                LPCVOID ptr ) /* [in] Address */
{
    SUBHEAP *sub;
    LIST_FOR_EACH_ENTRY( sub, &heap->subheap_list, SUBHEAP, entry )
        if ((ptr >= sub->base) &&
            ((const char *)ptr < (const char *)sub->base + sub->size - sizeof(ARENA_INUSE)))
            return sub;
    return NULL;
}


/***********************************************************************
 *           HEAP_Commit
 *
 * Make sure the heap storage is committed for a given size in the specified arena.
 */
static inline BOOL HEAP_Commit( SUBHEAP *subheap, ARENA_INUSE *pArena, SIZE_T data_size )
{
    void *ptr = (char *)(pArena + 1) + data_size + sizeof(ARENA_FREE);
    SIZE_T size = (char *)ptr - (char *)subheap->base;
    size = (size + COMMIT_MASK) & ~COMMIT_MASK;
    if (size > subheap->size) size = subheap->size;
    if (size <= subheap->commitSize) return TRUE;
    size -= subheap->commitSize;
    ptr = (char *)subheap->base + subheap->commitSize;
    if (NtAllocateVirtualMemory( NtCurrentProcess(), &ptr, 0,
                                 &size, MEM_COMMIT, get_protection_type( subheap->heap->flags ) ))
    {
        WARN("Could not commit %08lx bytes at %p for heap %p\n",
                 size, ptr, subheap->heap );
        return FALSE;
    }
    subheap->commitSize += size;
    return TRUE;
}


/***********************************************************************
 *           HEAP_Decommit
 *
 * If possible, decommit the heap storage from (including) 'ptr'.
 */
static inline BOOL HEAP_Decommit( SUBHEAP *subheap, void *ptr )
{
    void *addr;
    SIZE_T decommit_size;
    SIZE_T size = (char *)ptr - (char *)subheap->base;

    /* round to next block and add one full block */
    size = ((size + COMMIT_MASK) & ~COMMIT_MASK) + COMMIT_MASK + 1;
    size = max( size, subheap->min_commit );
    if (size >= subheap->commitSize) return TRUE;
    decommit_size = subheap->commitSize - size;
    addr = (char *)subheap->base + size;

    if (NtFreeVirtualMemory( NtCurrentProcess(), &addr, &decommit_size, MEM_DECOMMIT ))
    {
        WARN("Could not decommit %08lx bytes at %p for heap %p\n",
             decommit_size, (char *)subheap->base + size, subheap->heap );
        return FALSE;
    }
    subheap->commitSize -= decommit_size;
    return TRUE;
}


/***********************************************************************
 *           HEAP_CreateFreeBlock
 *
 * Create a free block at a specified address. 'size' is the size of the
 * whole block, including the new arena.
 */
static void HEAP_CreateFreeBlock( SUBHEAP *subheap, void *ptr, SIZE_T size )
{
    ARENA_FREE *pFree;
    char *pEnd;
    BOOL last;
    DWORD flags = subheap->heap->flags;

    /* Create a free arena */
    mark_block_uninitialized( ptr, sizeof(ARENA_FREE) );
    pFree = ptr;
    pFree->magic = ARENA_FREE_MAGIC;

    /* If debugging, erase the freed block content */

    pEnd = (char *)ptr + size;
    if (pEnd > (char *)subheap->base + subheap->commitSize)
        pEnd = (char *)subheap->base + subheap->commitSize;
    if (pEnd > (char *)(pFree + 1)) mark_block_free( pFree + 1, pEnd - (char *)(pFree + 1), flags );

    /* Check if next block is free also */

    if (((char *)ptr + size < (char *)subheap->base + subheap->size) &&
        (*(DWORD *)((char *)ptr + size) & ARENA_FLAG_FREE))
    {
        /* Remove the next arena from the free list */
        ARENA_FREE *pNext = (ARENA_FREE *)((char *)ptr + size);
        HEAP_DeleteFreeBlock( subheap->heap, pNext );
        size += (pNext->size & ARENA_SIZE_MASK) + sizeof(*pNext);
        mark_block_free( pNext, sizeof(ARENA_FREE), flags );
    }

    /* Set the next block PREV_FREE flag and pointer */

    last = ((char *)ptr + size >= (char *)subheap->base + subheap->size);
    if (!last)
    {
        DWORD *pNext = (DWORD *)((char *)ptr + size);
        *pNext |= ARENA_FLAG_PREV_FREE;
        mark_block_initialized( (ARENA_FREE **)pNext - 1, sizeof( ARENA_FREE * ) );
        *((ARENA_FREE **)pNext - 1) = pFree;
    }

    /* Last, insert the new block into the free list */

    pFree->size = size - sizeof(*pFree);
    HEAP_InsertFreeBlock( subheap->heap, pFree, last );
}


/***********************************************************************
 *           HEAP_MakeInUseBlockFree
 *
 * Turn an in-use block into a free block. Can also decommit the end of
 * the heap, and possibly even free the sub-heap altogether.
 */
static void HEAP_MakeInUseBlockFree( SUBHEAP *subheap, ARENA_INUSE *pArena )
{
    HEAP *heap = subheap->heap;
    ARENA_FREE *pFree;
    SIZE_T size;

    if (heap->pending_free)
    {
        ARENA_INUSE *prev = heap->pending_free[heap->pending_pos];
        heap->pending_free[heap->pending_pos] = pArena;
        heap->pending_pos = (heap->pending_pos + 1) % MAX_FREE_PENDING;
        pArena->magic = ARENA_PENDING_MAGIC;
        mark_block_free( pArena + 1, pArena->size & ARENA_SIZE_MASK, heap->flags );
        if (!prev) return;
        pArena = prev;
        subheap = HEAP_FindSubHeap( heap, pArena );
    }

    /* Check if we can merge with previous block */

    size = (pArena->size & ARENA_SIZE_MASK) + sizeof(*pArena);
    if (pArena->size & ARENA_FLAG_PREV_FREE)
    {
        pFree = *((ARENA_FREE **)pArena - 1);
        size += (pFree->size & ARENA_SIZE_MASK) + sizeof(ARENA_FREE);
        /* Remove it from the free list */
        HEAP_DeleteFreeBlock( heap, pFree );
    }
    else pFree = (ARENA_FREE *)pArena;

    /* Create a free block */

    HEAP_CreateFreeBlock( subheap, pFree, size );
    size = (pFree->size & ARENA_SIZE_MASK) + sizeof(ARENA_FREE);
    if ((char *)pFree + size < (char *)subheap->base + subheap->size)
        return;  /* Not the last block, so nothing more to do */

    /* Free the whole sub-heap if it's empty and not the original one */

    if (((char *)pFree == (char *)subheap->base + subheap->headerSize) &&
        (subheap != &subheap->heap->subheap))
    {
        void *addr = subheap->base;

        size = 0;
        /* Remove the free block from the list */
        HEAP_DeleteFreeBlock( heap, pFree );
        /* Remove the subheap from the list */
        list_remove( &subheap->entry );
        /* Free the memory */
        subheap->magic = 0;
        NtFreeVirtualMemory( NtCurrentProcess(), &addr, &size, MEM_RELEASE );
        return;
    }

    /* Decommit the end of the heap */

    if (!(subheap->heap->flags & HEAP_SHARED)) HEAP_Decommit( subheap, pFree + 1 );
}


/***********************************************************************
 *           HEAP_ShrinkBlock
 *
 * Shrink an in-use block.
 */
static void HEAP_ShrinkBlock(SUBHEAP *subheap, ARENA_INUSE *pArena, SIZE_T size)
{
    if ((pArena->size & ARENA_SIZE_MASK) >= size + HEAP_MIN_SHRINK_SIZE)
    {
        HEAP_CreateFreeBlock( subheap, (char *)(pArena + 1) + size,
                              (pArena->size & ARENA_SIZE_MASK) - size );
	/* assign size plus previous arena flags */
        pArena->size = size | (pArena->size & ~ARENA_SIZE_MASK);
    }
    else
    {
        /* Turn off PREV_FREE flag in next block */
        char *pNext = (char *)(pArena + 1) + (pArena->size & ARENA_SIZE_MASK);
        if (pNext < (char *)subheap->base + subheap->size)
            *(DWORD *)pNext &= ~ARENA_FLAG_PREV_FREE;
    }
}


/***********************************************************************
 *           allocate_large_block
 */
static void *allocate_large_block( HEAP *heap, DWORD flags, SIZE_T size )
{
    ARENA_LARGE *arena;
    SIZE_T block_size = sizeof(*arena) + ROUND_SIZE(size) + HEAP_TAIL_EXTRA_SIZE(flags);
    LPVOID address = NULL;

    if (block_size < size) return NULL;  /* overflow */
    if (NtAllocateVirtualMemory( NtCurrentProcess(), &address, 5,
                                 &block_size, MEM_COMMIT, get_protection_type( flags ) ))
    {
        WARN("Could not allocate block for %08lx bytes\n", size );
        return NULL;
    }
    arena = address;
    arena->data_size = size;
    arena->block_size = block_size;
    arena->size = ARENA_LARGE_SIZE;
    arena->magic = ARENA_LARGE_MAGIC;
    mark_block_tail( (char *)(arena + 1) + size, block_size - sizeof(*arena) - size, flags );
    list_add_tail( &heap->large_list, &arena->entry );
    notify_alloc( arena + 1, size, flags & HEAP_ZERO_MEMORY );
    return arena + 1;
}


/***********************************************************************
 *           free_large_block
 */
static void free_large_block( HEAP *heap, DWORD flags, void *ptr )
{
    ARENA_LARGE *arena = (ARENA_LARGE *)ptr - 1;
    LPVOID address = arena;
    SIZE_T size = 0;

    list_remove( &arena->entry );
    NtFreeVirtualMemory( NtCurrentProcess(), &address, &size, MEM_RELEASE );
}


/***********************************************************************
 *           realloc_large_block
 */
static void *realloc_large_block( HEAP *heap, DWORD flags, void *ptr, SIZE_T size )
{
    ARENA_LARGE *arena = (ARENA_LARGE *)ptr - 1;
    void *new_ptr;

    if (arena->block_size - sizeof(*arena) >= size)
    {
        SIZE_T unused = arena->block_size - sizeof(*arena) - size;

        /* FIXME: we could remap zero-pages instead */
#ifdef VALGRIND_RESIZEINPLACE_BLOCK
        if (RUNNING_ON_VALGRIND)
            notify_realloc( arena + 1, arena->data_size, size );
        else
#endif
        if (size > arena->data_size)
            initialize_block( (char *)ptr + arena->data_size, size - arena->data_size, unused, flags );
        else
            mark_block_tail( (char *)ptr + size, unused, flags );
        arena->data_size = size;
        return ptr;
    }
    if (flags & HEAP_REALLOC_IN_PLACE_ONLY) return NULL;
    if (!(new_ptr = allocate_large_block( heap, flags, size )))
    {
        WARN("Could not allocate block for %08lx bytes\n", size );
        return NULL;
    }
    memcpy( new_ptr, ptr, arena->data_size );
    free_large_block( heap, flags, ptr );
    notify_free( ptr );
    return new_ptr;
}


/***********************************************************************
 *           find_large_block
 */
static ARENA_LARGE *find_large_block( HEAP *heap, const void *ptr )
{
    ARENA_LARGE *arena;

    LIST_FOR_EACH_ENTRY( arena, &heap->large_list, ARENA_LARGE, entry )
        if (ptr == arena + 1) return arena;

    return NULL;
}


/***********************************************************************
 *           validate_large_arena
 */
static BOOL validate_large_arena( HEAP *heap, const ARENA_LARGE *arena, BOOL quiet )
{
    DWORD flags = heap->flags;

    if ((ULONG_PTR)arena % page_size)
    {
        if (quiet == NOISY)
        {
            ERR( "Heap %p: invalid large arena pointer %p\n", heap, arena );
            if (TRACE_ON(heap)) HEAP_Dump( heap );
        }
        else if (WARN_ON(heap))
        {
            WARN( "Heap %p: unaligned arena pointer %p\n", heap, arena );
            if (TRACE_ON(heap)) HEAP_Dump( heap );
        }
        return FALSE;
    }
    if (arena->size != ARENA_LARGE_SIZE || arena->magic != ARENA_LARGE_MAGIC)
    {
        if (quiet == NOISY)
        {
            ERR( "Heap %p: invalid large arena %p values %x/%x\n",
                 heap, arena, arena->size, arena->magic );
            if (TRACE_ON(heap)) HEAP_Dump( heap );
        }
        else if (WARN_ON(heap))
        {
            WARN( "Heap %p: invalid large arena %p values %x/%x\n",
                  heap, arena, arena->size, arena->magic );
            if (TRACE_ON(heap)) HEAP_Dump( heap );
        }
        return FALSE;
    }
    if (arena->data_size > arena->block_size - sizeof(*arena))
    {
        ERR( "Heap %p: invalid large arena %p size %lx/%lx\n",
             heap, arena, arena->data_size, arena->block_size );
        return FALSE;
    }
    if (flags & HEAP_TAIL_CHECKING_ENABLED)
    {
        SIZE_T i, unused = arena->block_size - sizeof(*arena) - arena->data_size;
        const unsigned char *data = (const unsigned char *)(arena + 1) + arena->data_size;

        for (i = 0; i < unused; i++)
        {
            if (data[i] == ARENA_TAIL_FILLER) continue;
            ERR("Heap %p: block %p tail overwritten at %p (byte %lu/%lu == 0x%02x)\n",
                heap, arena + 1, data + i, i, unused, data[i] );
            return FALSE;
        }
    }
    return TRUE;
}


static inline int arena_free_compare( const void *key, const struct wine_rb_entry *entry )
{
    DWORD arena_size = get_arena_size( entry );
    if (*(DWORD *)key > arena_size) return 1;
    else if (*(DWORD *)key < arena_size) return -1;
    else return entry->left ? 1 : -1;
}


/***********************************************************************
 *           HEAP_CreateSubHeap
 */
static SUBHEAP *HEAP_CreateSubHeap( HEAP *heap, LPVOID address, DWORD flags,
                                    SIZE_T commitSize, SIZE_T totalSize )
{
    SUBHEAP *subheap;
    unsigned int i;

    if (!address)
    {
        if (!commitSize) commitSize = COMMIT_MASK + 1;
        totalSize = min( totalSize, 0xffff0000 );  /* don't allow a heap larger than 4Gb */
        if (totalSize < commitSize) totalSize = commitSize;
        if (flags & HEAP_SHARED) commitSize = totalSize;  /* always commit everything in a shared heap */
        commitSize = min( totalSize, (commitSize + COMMIT_MASK) & ~COMMIT_MASK );

        /* allocate the memory block */
        if (NtAllocateVirtualMemory( NtCurrentProcess(), &address, 0, &totalSize,
                                     MEM_RESERVE, get_protection_type( flags ) ))
        {
            WARN("Could not allocate %08lx bytes\n", totalSize );
            return NULL;
        }
        if (NtAllocateVirtualMemory( NtCurrentProcess(), &address, 0,
                                     &commitSize, MEM_COMMIT, get_protection_type( flags ) ))
        {
            WARN("Could not commit %08lx bytes for sub-heap %p\n", commitSize, address );
            return NULL;
        }
    }

    if (heap)
    {
        /* If this is a secondary subheap, insert it into list */

        subheap = address;
        subheap->base       = address;
        subheap->heap       = heap;
        subheap->size       = totalSize;
        subheap->min_commit = 0x10000;
        subheap->commitSize = commitSize;
        subheap->magic      = SUBHEAP_MAGIC;
        subheap->headerSize = ROUND_SIZE( sizeof(SUBHEAP) );
        list_add_head( &heap->subheap_list, &subheap->entry );
    }
    else
    {
        /* If this is a primary subheap, initialize main heap */

        heap = address;
        heap->flags         = flags;
        heap->magic         = HEAP_MAGIC;
        heap->grow_size     = max( HEAP_DEF_SIZE, totalSize );
        list_init( &heap->subheap_list );
        list_init( &heap->large_list );

        subheap = &heap->subheap;
        subheap->base       = address;
        subheap->heap       = heap;
        subheap->size       = totalSize;
        subheap->min_commit = commitSize;
        subheap->commitSize = commitSize;
        subheap->magic      = SUBHEAP_MAGIC;
        subheap->headerSize = ROUND_SIZE( sizeof(HEAP) );
        list_add_head( &heap->subheap_list, &subheap->entry );

        /* Initialize the free tree */

        heap->freeList = (struct list *)((char *)heap + subheap->headerSize);
        subheap->headerSize += HEAP_NB_FREE_LISTS * sizeof(struct list);
        for (i = 0; i < HEAP_NB_FREE_LISTS; i++)
            list_init( &heap->freeList[i] );

        /* Initialize the free tree */

        wine_rb_init( &heap->freeTree, arena_free_compare );

        /* Initialize the free mask */

        for (i = 0; i < sizeof(heap->freeMask) / sizeof(heap->freeMask[0]); i++)
            heap->freeMask[i] = 0;

        /* Initialize critical section */

        if (!processHeap)  /* do it by hand to avoid memory allocations */
        {
            heap->critSection.DebugInfo      = &process_heap_critsect_debug;
            heap->critSection.LockCount      = -1;
            heap->critSection.RecursionCount = 0;
            heap->critSection.OwningThread   = 0;
            heap->critSection.LockSemaphore  = 0;
            heap->critSection.SpinCount      = 0;
            process_heap_critsect_debug.CriticalSection = &heap->critSection;
        }
        else
        {
            RtlInitializeCriticalSection( &heap->critSection );
            heap->critSection.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": HEAP.critSection");
        }

        if (flags & HEAP_SHARED)
        {
            /* let's assume that only one thread at a time will try to do this */
            HANDLE sem = heap->critSection.LockSemaphore;
            if (!sem) NtCreateSemaphore( &sem, SEMAPHORE_ALL_ACCESS, NULL, 0, 1 );

            NtDuplicateObject( NtCurrentProcess(), sem, NtCurrentProcess(), &sem, 0, 0,
                               DUP_HANDLE_MAKE_GLOBAL | DUP_HANDLE_SAME_ACCESS | DUP_HANDLE_CLOSE_SOURCE );
            heap->critSection.LockSemaphore = sem;
            RtlFreeHeap( processHeap, 0, heap->critSection.DebugInfo );
            heap->critSection.DebugInfo = NULL;
        }
    }

    /* Create the first free block */

    HEAP_CreateFreeBlock( subheap, (LPBYTE)subheap->base + subheap->headerSize,
                          subheap->size - subheap->headerSize );

    return subheap;
}


/* helper function for HEAP_FindFreeBlock */
static struct wine_rb_entry *find_free_block( struct wine_rb_entry *entry, DWORD arena_size )
{
    for (;;)
    {
        if (!entry) return NULL;
        if (get_arena_size( entry ) >= arena_size) break;
        entry = entry->right;
    }

    for (;;)
    {
        if (!entry->left) return entry;
        if (get_arena_size( entry->left ) < arena_size) break;
        entry = entry->left;
    }

    if (entry->left->right)
    {
        struct wine_rb_entry *ret;
        if ((ret = find_free_block( entry->left->right, arena_size )))
            return ret;
    }

    return entry;
}


/***********************************************************************
 *           HEAP_FindFreeBlock
 *
 * Find a free block at least as large as the requested size, and make sure
 * the requested size is committed.
 */
static ARENA_FREE *HEAP_FindFreeBlock( HEAP *heap, SIZE_T size,
                                       SUBHEAP **ppSubHeap )
{
    struct wine_rb_entry *ptr;
    unsigned long mask;
    ARENA_FREE *arena;
    SUBHEAP *subheap;
    SIZE_T total_size;
    SIZE_T index = HEAP_SIZE_TO_FREELIST_INDEX( size + sizeof(ARENA_INUSE) );

    /* Find a suitable block from the free list */

    while (index < HEAP_NB_FREE_LISTS)
    {
        mask = heap->freeMask[ HEAP_FREEMASK_INDEX( index ) ] & ~(HEAP_FREEMASK_BIT( index ) - 1);
        if (mask)
        {
            index = (index & ~(HEAP_FREEMASK_BLOCK - 1)) | ctzl( mask );
            arena = LIST_ENTRY( heap->freeList[index].next, ARENA_FREE, entry.list );
            subheap = HEAP_FindSubHeap( heap, arena );
            if (!HEAP_Commit( subheap, (ARENA_INUSE *)arena, size )) return NULL;
            *ppSubHeap = subheap;
            return arena;
        }
        index = (index + HEAP_FREEMASK_BLOCK) & ~(HEAP_FREEMASK_BLOCK - 1);
    }

    /* Find a suitable block from the free tree */

    if ((ptr = find_free_block( heap->freeTree.root, size + sizeof(ARENA_INUSE) - sizeof(ARENA_FREE) )))
    {
        arena = WINE_RB_ENTRY_VALUE( ptr, ARENA_FREE, entry.tree );
        subheap = HEAP_FindSubHeap( heap, arena );
        if (!HEAP_Commit( subheap, (ARENA_INUSE *)arena, size )) return NULL;
        *ppSubHeap = subheap;
        return arena;
    }

    /* If no block was found, attempt to grow the heap */

    if (!(heap->flags & HEAP_GROWABLE))
    {
        WARN("Not enough space in heap %p for %08lx bytes\n", heap, size );
        return NULL;
    }
    /* make sure that we have a big enough size *committed* to fit another
     * last free arena in !
     * So just one heap struct, one first free arena which will eventually
     * get used, and a second free arena that might get assigned all remaining
     * free space in HEAP_ShrinkBlock() */
    total_size = size + ROUND_SIZE(sizeof(SUBHEAP)) + sizeof(ARENA_INUSE) + sizeof(ARENA_FREE);
    if (total_size < size) return NULL;  /* overflow */

    if ((subheap = HEAP_CreateSubHeap( heap, NULL, heap->flags, total_size,
                                       max( heap->grow_size, total_size ) )))
    {
        if (heap->grow_size < 128 * 1024 * 1024) heap->grow_size *= 2;
    }
    else while (!subheap)  /* shrink the grow size again if we are running out of space */
    {
        if (heap->grow_size <= total_size || heap->grow_size <= 4 * 1024 * 1024) return NULL;
        heap->grow_size /= 2;
        subheap = HEAP_CreateSubHeap( heap, NULL, heap->flags, total_size,
                                      max( heap->grow_size, total_size ) );
    }

    TRACE("created new sub-heap %p of %08lx bytes for heap %p\n",
          subheap, subheap->size, heap );

    *ppSubHeap = subheap;
    return (ARENA_FREE *)((char *)subheap->base + subheap->headerSize);
}


/***********************************************************************
 *           HEAP_IsValidArenaPtr
 *
 * Check that the pointer is inside the range possible for arenas.
 */
static BOOL HEAP_IsValidArenaPtr( const HEAP *heap, const ARENA_FREE *ptr )
{
    const SUBHEAP *subheap = HEAP_FindSubHeap( heap, ptr );
    if (!subheap) return FALSE;
    if ((const char *)ptr >= (const char *)subheap->base + subheap->headerSize) return TRUE;
    if (subheap != &heap->subheap) return FALSE;
    return FALSE;
}


/***********************************************************************
 *           HEAP_ValidateFreeArena
 */
static BOOL HEAP_ValidateFreeArena( SUBHEAP *subheap, ARENA_FREE *pArena )
{
    DWORD flags = subheap->heap->flags;
    SIZE_T size;
    ARENA_FREE *prev = NULL, *next = NULL;
    char *heapEnd = (char *)subheap->base + subheap->size;

    /* Check for unaligned pointers */
    if ((ULONG_PTR)pArena % ALIGNMENT != ARENA_OFFSET)
    {
        ERR("Heap %p: unaligned arena pointer %p\n", subheap->heap, pArena );
        return FALSE;
    }

    /* Check magic number */
    if (pArena->magic != ARENA_FREE_MAGIC)
    {
        ERR("Heap %p: invalid free arena magic %08x for %p\n", subheap->heap, pArena->magic, pArena );
        return FALSE;
    }
    /* Check size flags */
    if (((pArena->size & ARENA_FLAG_FREE) != ARENA_FLAG_FREE_LIST &&
         (pArena->size & ARENA_FLAG_FREE) != ARENA_FLAG_FREE_TREE) ||
        (pArena->size & ARENA_FLAG_PREV_FREE))
    {
        ERR("Heap %p: bad flags %08x for free arena %p\n",
            subheap->heap, pArena->size & ~ARENA_SIZE_MASK, pArena );
        return FALSE;
    }
    /* Check arena size */
    size = pArena->size & ARENA_SIZE_MASK;
    if ((char *)(pArena + 1) + size > heapEnd)
    {
        ERR("Heap %p: bad size %08lx for free arena %p\n", subheap->heap, size, pArena );
        return FALSE;
    }
    if ((pArena->size & ARENA_FLAG_FREE) == ARENA_FLAG_FREE_LIST)
    {
        struct list *list = &subheap->heap->freeList[ HEAP_SIZE_TO_FREELIST_INDEX( size + sizeof(*pArena) ) ];
        if (pArena->entry.list.prev != list)
            prev = LIST_ENTRY( pArena->entry.list.prev, ARENA_FREE, entry.list );
        if (pArena->entry.list.next != list)
            next = LIST_ENTRY( pArena->entry.list.next, ARENA_FREE, entry.list );
    }
    else
    {
        if (pArena->entry.tree.left)
            prev = WINE_RB_ENTRY_VALUE( pArena->entry.tree.left, ARENA_FREE, entry.tree );
        if (pArena->entry.tree.right)
            next = WINE_RB_ENTRY_VALUE( pArena->entry.tree.right, ARENA_FREE, entry.tree );
    }
    /* Check that next pointer is valid */
    if (next && !HEAP_IsValidArenaPtr( subheap->heap, next ))
    {
        ERR("Heap %p: bad next ptr %p for arena %p\n",
            subheap->heap, next, pArena );
        return FALSE;
    }
    /* Check that next arena is free */
    if (next && (!(next->size & ARENA_FLAG_FREE) || (next->magic != ARENA_FREE_MAGIC)))
    {
        ERR("Heap %p: next arena %p invalid for %p\n",
            subheap->heap, next, pArena );
        return FALSE;
    }
    /* Check that prev pointer is valid */
    if (prev && !HEAP_IsValidArenaPtr( subheap->heap, prev ))
    {
        ERR("Heap %p: bad prev ptr %p for arena %p\n",
            subheap->heap, prev, pArena );
        return FALSE;
    }
    /* Check that prev arena is free */
    if (prev && (!(prev->size & ARENA_FLAG_FREE) || (prev->magic != ARENA_FREE_MAGIC)))
    {
        ERR("Heap %p: prev arena %p invalid for %p\n",
            subheap->heap, prev, pArena );
        return FALSE;
    }
    /* Check that next block has PREV_FREE flag */
    if ((char *)(pArena + 1) + size < heapEnd)
    {
        if (!(*(DWORD *)((char *)(pArena + 1) + size) & ARENA_FLAG_PREV_FREE))
        {
            ERR("Heap %p: free arena %p next block has no PREV_FREE flag\n",
                subheap->heap, pArena );
            return FALSE;
        }
        /* Check next block back pointer */
        if (*((ARENA_FREE **)((char *)(pArena + 1) + size) - 1) != pArena)
        {
            ERR("Heap %p: arena %p has wrong back ptr %p\n",
                subheap->heap, pArena,
                *((ARENA_FREE **)((char *)(pArena+1) + size) - 1));
            return FALSE;
        }
    }
    if (flags & HEAP_FREE_CHECKING_ENABLED)
    {
        DWORD *ptr = (DWORD *)(pArena + 1);
        char *end = (char *)(pArena + 1) + size;

        if (end >= heapEnd) end = (char *)subheap->base + subheap->commitSize;
        else end -= sizeof(ARENA_FREE *);
        while (ptr < (DWORD *)end)
        {
            if (*ptr != ARENA_FREE_FILLER)
            {
                ERR("Heap %p: free block %p overwritten at %p by %08x\n",
                    subheap->heap, (ARENA_INUSE *)pArena + 1, ptr, *ptr );
                return FALSE;
            }
            ptr++;
        }
    }
    return TRUE;
}


/***********************************************************************
 *           HEAP_ValidateInUseArena
 */
static BOOL HEAP_ValidateInUseArena( const SUBHEAP *subheap, const ARENA_INUSE *pArena, BOOL quiet )
{
    SIZE_T size;
    DWORD i, flags = subheap->heap->flags;
    const char *heapEnd = (const char *)subheap->base + subheap->size;

    /* Check for unaligned pointers */
    if ((ULONG_PTR)pArena % ALIGNMENT != ARENA_OFFSET)
    {
        if ( quiet == NOISY )
        {
            ERR( "Heap %p: unaligned arena pointer %p\n", subheap->heap, pArena );
            if ( TRACE_ON(heap) )
                HEAP_Dump( subheap->heap );
        }
        else if ( WARN_ON(heap) )
        {
            WARN( "Heap %p: unaligned arena pointer %p\n", subheap->heap, pArena );
            if ( TRACE_ON(heap) )
                HEAP_Dump( subheap->heap );
        }
        return FALSE;
    }

    /* Check magic number */
    if (pArena->magic != ARENA_INUSE_MAGIC && pArena->magic != ARENA_PENDING_MAGIC)
    {
        if (quiet == NOISY) {
            ERR("Heap %p: invalid in-use arena magic %08x for %p\n", subheap->heap, pArena->magic, pArena );
            if (TRACE_ON(heap))
               HEAP_Dump( subheap->heap );
        }  else if (WARN_ON(heap)) {
            WARN("Heap %p: invalid in-use arena magic %08x for %p\n", subheap->heap, pArena->magic, pArena );
            if (TRACE_ON(heap))
               HEAP_Dump( subheap->heap );
        }
        return FALSE;
    }
    /* Check size flags */
    if (pArena->size & ARENA_FLAG_FREE)
    {
        ERR("Heap %p: bad flags %08x for in-use arena %p\n",
            subheap->heap, pArena->size & ~ARENA_SIZE_MASK, pArena );
        return FALSE;
    }
    /* Check arena size */
    size = pArena->size & ARENA_SIZE_MASK;
    if ((const char *)(pArena + 1) + size > heapEnd ||
        (const char *)(pArena + 1) + size < (const char *)(pArena + 1))
    {
        ERR("Heap %p: bad size %08lx for in-use arena %p\n", subheap->heap, size, pArena );
        return FALSE;
    }
    /* Check next arena PREV_FREE flag */
    if (((const char *)(pArena + 1) + size < heapEnd) &&
        (*(const DWORD *)((const char *)(pArena + 1) + size) & ARENA_FLAG_PREV_FREE))
    {
        ERR("Heap %p: in-use arena %p next block %p has PREV_FREE flag %x\n",
            subheap->heap, pArena, (const char *)(pArena + 1) + size,*(const DWORD *)((const char *)(pArena + 1) + size) );
        return FALSE;
    }
    /* Check prev free arena */
    if (pArena->size & ARENA_FLAG_PREV_FREE)
    {
        const ARENA_FREE *pPrev = *((const ARENA_FREE * const*)pArena - 1);
        /* Check prev pointer */
        if (!HEAP_IsValidArenaPtr( subheap->heap, pPrev ))
        {
            ERR("Heap %p: bad back ptr %p for arena %p\n",
                subheap->heap, pPrev, pArena );
            return FALSE;
        }
        /* Check that prev arena is free */
        if (!(pPrev->size & ARENA_FLAG_FREE) ||
            (pPrev->magic != ARENA_FREE_MAGIC))
        {
            ERR("Heap %p: prev arena %p invalid for in-use %p\n",
                subheap->heap, pPrev, pArena );
            return FALSE;
        }
        /* Check that prev arena is really the previous block */
        if ((const char *)(pPrev + 1) + (pPrev->size & ARENA_SIZE_MASK) != (const char *)pArena)
        {
            ERR("Heap %p: prev arena %p is not prev for in-use %p\n",
                subheap->heap, pPrev, pArena );
            return FALSE;
        }
    }
    /* Check unused size */
    if (pArena->unused_bytes > size)
    {
        ERR("Heap %p: invalid unused size %08x/%08lx\n", subheap->heap, pArena->unused_bytes, size );
        return FALSE;
    }
    /* Check unused bytes */
    if (pArena->magic == ARENA_PENDING_MAGIC)
    {
        const DWORD *ptr = (const DWORD *)(pArena + 1);
        const DWORD *end = (const DWORD *)((const char *)ptr + size);

        while (ptr < end)
        {
            if (*ptr != ARENA_FREE_FILLER)
            {
                ERR("Heap %p: free block %p overwritten at %p by %08x\n",
                    subheap->heap, (const ARENA_INUSE *)pArena + 1, ptr, *ptr );
                if (!*ptr) { HEAP_Dump( subheap->heap ); DbgBreakPoint(); }
                return FALSE;
            }
            ptr++;
        }
    }
    else if (flags & HEAP_TAIL_CHECKING_ENABLED)
    {
        const unsigned char *data = (const unsigned char *)(pArena + 1) + size - pArena->unused_bytes;

        for (i = 0; i < pArena->unused_bytes; i++)
        {
            if (data[i] == ARENA_TAIL_FILLER) continue;
            ERR("Heap %p: block %p tail overwritten at %p (byte %u/%u == 0x%02x)\n",
                subheap->heap, pArena + 1, data + i, i, pArena->unused_bytes, data[i] );
            return FALSE;
        }
    }
    return TRUE;
}


/***********************************************************************
 *           HEAP_IsRealArena  [Internal]
 * Validates a block is a valid arena.
 *
 * RETURNS
 *	TRUE: Success
 *	FALSE: Failure
 */
static BOOL HEAP_IsRealArena( HEAP *heapPtr,   /* [in] ptr to the heap */
              DWORD flags,   /* [in] Bit flags that control access during operation */
              LPCVOID block, /* [in] Optional pointer to memory block to validate */
              BOOL quiet )   /* [in] Flag - if true, HEAP_ValidateInUseArena
                              *             does not complain    */
{
    SUBHEAP *subheap;
    BOOL ret = TRUE;
    const ARENA_LARGE *large_arena;

    flags &= HEAP_NO_SERIALIZE;
    flags |= heapPtr->flags;
    /* calling HeapLock may result in infinite recursion, so do the critsect directly */
    if (!(flags & HEAP_NO_SERIALIZE))
        enter_critical_section( &heapPtr->critSection );

    if (block)  /* only check this single memory block */
    {
        const ARENA_INUSE *arena = (const ARENA_INUSE *)block - 1;

        if (!(subheap = HEAP_FindSubHeap( heapPtr, arena )) ||
            ((const char *)arena < (char *)subheap->base + subheap->headerSize))
        {
            if (!(large_arena = find_large_block( heapPtr, block )))
            {
                if (quiet == NOISY)
                    ERR("Heap %p: block %p is not inside heap\n", heapPtr, block );
                else if (WARN_ON(heap))
                    WARN("Heap %p: block %p is not inside heap\n", heapPtr, block );
                ret = FALSE;
            }
            else
                ret = validate_large_arena( heapPtr, large_arena, quiet );
        } else
            ret = HEAP_ValidateInUseArena( subheap, arena, quiet );

        if (!(flags & HEAP_NO_SERIALIZE))
            leave_critical_section( &heapPtr->critSection );
        return ret;
    }

    LIST_FOR_EACH_ENTRY( subheap, &heapPtr->subheap_list, SUBHEAP, entry )
    {
        char *ptr = (char *)subheap->base + subheap->headerSize;
        while (ptr < (char *)subheap->base + subheap->size)
        {
            if (*(DWORD *)ptr & ARENA_FLAG_FREE)
            {
                if (!HEAP_ValidateFreeArena( subheap, (ARENA_FREE *)ptr )) {
                    ret = FALSE;
                    break;
                }
                ptr += sizeof(ARENA_FREE) + (*(DWORD *)ptr & ARENA_SIZE_MASK);
            }
            else
            {
                if (!HEAP_ValidateInUseArena( subheap, (ARENA_INUSE *)ptr, NOISY )) {
                    ret = FALSE;
                    break;
                }
                ptr += sizeof(ARENA_INUSE) + (*(DWORD *)ptr & ARENA_SIZE_MASK);
            }
        }
        if (!ret) break;
    }

    LIST_FOR_EACH_ENTRY( large_arena, &heapPtr->large_list, ARENA_LARGE, entry )
        if (!(ret = validate_large_arena( heapPtr, large_arena, quiet ))) break;

    if (!(flags & HEAP_NO_SERIALIZE)) leave_critical_section( &heapPtr->critSection );
    return ret;
}


/***********************************************************************
 *           validate_block_pointer
 *
 * Minimum validation needed to catch bad parameters in heap functions.
 */
static BOOL validate_block_pointer( HEAP *heap, SUBHEAP **ret_subheap, const ARENA_INUSE *arena )
{
    SUBHEAP *subheap;
    BOOL ret = FALSE;

    if (!(*ret_subheap = subheap = HEAP_FindSubHeap( heap, arena )))
    {
        ARENA_LARGE *large_arena = find_large_block( heap, arena + 1 );

        if (!large_arena)
        {
            WARN( "Heap %p: pointer %p is not inside heap\n", heap, arena + 1 );
            return FALSE;
        }
        if ((heap->flags & HEAP_VALIDATE) && !validate_large_arena( heap, large_arena, QUIET ))
            return FALSE;
        return TRUE;
    }

    if ((const char *)arena < (char *)subheap->base + subheap->headerSize)
        WARN( "Heap %p: pointer %p is inside subheap %p header\n", subheap->heap, arena + 1, subheap );
    else if (subheap->heap->flags & HEAP_VALIDATE)  /* do the full validation */
        ret = HEAP_ValidateInUseArena( subheap, arena, QUIET );
    else if ((ULONG_PTR)arena % ALIGNMENT != ARENA_OFFSET)
        WARN( "Heap %p: unaligned arena pointer %p\n", subheap->heap, arena );
    else if (arena->magic == ARENA_PENDING_MAGIC)
        WARN( "Heap %p: block %p used after free\n", subheap->heap, arena + 1 );
    else if (arena->magic != ARENA_INUSE_MAGIC)
        WARN( "Heap %p: invalid in-use arena magic %08x for %p\n", subheap->heap, arena->magic, arena );
    else if (arena->size & ARENA_FLAG_FREE)
        ERR( "Heap %p: bad flags %08x for in-use arena %p\n",
             subheap->heap, arena->size & ~ARENA_SIZE_MASK, arena );
    else if ((const char *)(arena + 1) + (arena->size & ARENA_SIZE_MASK) > (const char *)subheap->base + subheap->size ||
             (const char *)(arena + 1) + (arena->size & ARENA_SIZE_MASK) < (const char *)(arena + 1))
        ERR( "Heap %p: bad size %08x for in-use arena %p\n",
             subheap->heap, arena->size & ARENA_SIZE_MASK, arena );
    else
        ret = TRUE;

    return ret;
}


/***********************************************************************
 *           heap_set_debug_flags
 */
void heap_set_debug_flags( HANDLE handle )
{
    HEAP *heap = HEAP_GetPtr( handle );
    ULONG global_flags = RtlGetNtGlobalFlags();
    ULONG flags = 0;

    if (TRACE_ON(heap)) global_flags |= FLG_HEAP_VALIDATE_ALL;
    if (WARN_ON(heap)) global_flags |= FLG_HEAP_VALIDATE_PARAMETERS;

    if (global_flags & FLG_HEAP_ENABLE_TAIL_CHECK) flags |= HEAP_TAIL_CHECKING_ENABLED;
    if (global_flags & FLG_HEAP_ENABLE_FREE_CHECK) flags |= HEAP_FREE_CHECKING_ENABLED;
    if (global_flags & FLG_HEAP_DISABLE_COALESCING) flags |= HEAP_DISABLE_COALESCE_ON_FREE;
    if (global_flags & FLG_HEAP_PAGE_ALLOCS) flags |= HEAP_PAGE_ALLOCS | HEAP_GROWABLE;

    if (global_flags & FLG_HEAP_VALIDATE_PARAMETERS)
        flags |= HEAP_VALIDATE | HEAP_VALIDATE_PARAMS |
                 HEAP_TAIL_CHECKING_ENABLED | HEAP_FREE_CHECKING_ENABLED;
    if (global_flags & FLG_HEAP_VALIDATE_ALL)
        flags |= HEAP_VALIDATE | HEAP_VALIDATE_ALL |
                 HEAP_TAIL_CHECKING_ENABLED | HEAP_FREE_CHECKING_ENABLED;

    if (RUNNING_ON_VALGRIND) flags = 0; /* no sense in validating since Valgrind catches accesses */

    heap->flags |= flags;
    heap->force_flags |= flags & ~(HEAP_VALIDATE | HEAP_DISABLE_COALESCE_ON_FREE);

    if (flags & (HEAP_FREE_CHECKING_ENABLED | HEAP_TAIL_CHECKING_ENABLED))  /* fix existing blocks */
    {
        SUBHEAP *subheap;
        ARENA_LARGE *large;

        LIST_FOR_EACH_ENTRY( subheap, &heap->subheap_list, SUBHEAP, entry )
        {
            char *ptr = (char *)subheap->base + subheap->headerSize;
            char *end = (char *)subheap->base + subheap->commitSize;
            while (ptr < end)
            {
                ARENA_INUSE *arena = (ARENA_INUSE *)ptr;
                SIZE_T size = arena->size & ARENA_SIZE_MASK;
                if (arena->size & ARENA_FLAG_FREE)
                {
                    SIZE_T count = size;

                    ptr += sizeof(ARENA_FREE) + size;
                    if (ptr >= end) count = end - (char *)((ARENA_FREE *)arena + 1);
                    else count -= sizeof(ARENA_FREE *);
                    mark_block_free( (ARENA_FREE *)arena + 1, count, flags );
                }
                else
                {
                    if (arena->magic == ARENA_PENDING_MAGIC)
                        mark_block_free( arena + 1, size, flags );
                    else
                        mark_block_tail( (char *)(arena + 1) + size - arena->unused_bytes,
                                         arena->unused_bytes, flags );
                    ptr += sizeof(ARENA_INUSE) + size;
                }
            }
        }

        LIST_FOR_EACH_ENTRY( large, &heap->large_list, ARENA_LARGE, entry )
            mark_block_tail( (char *)(large + 1) + large->data_size,
                             large->block_size - sizeof(*large) - large->data_size, flags );
    }

    if ((heap->flags & HEAP_GROWABLE) && !heap->pending_free &&
        ((flags & HEAP_FREE_CHECKING_ENABLED) || RUNNING_ON_VALGRIND))
    {
        void *ptr = NULL;
        SIZE_T size = MAX_FREE_PENDING * sizeof(*heap->pending_free);

        if (!NtAllocateVirtualMemory( NtCurrentProcess(), &ptr, 4, &size, MEM_COMMIT, PAGE_READWRITE ))
        {
            heap->pending_free = ptr;
            heap->pending_pos = 0;
        }
    }
}


/***********************************************************************
 *           RtlCreateHeap   (NTDLL.@)
 *
 * Create a new Heap.
 *
 * PARAMS
 *  flags      [I] HEAP_ flags from "winnt.h"
 *  addr       [I] Desired base address
 *  totalSize  [I] Total size of the heap, or 0 for a growable heap
 *  commitSize [I] Amount of heap space to commit
 *  unknown    [I] Not yet understood
 *  definition [I] Heap definition
 *
 * RETURNS
 *  Success: A HANDLE to the newly created heap.
 *  Failure: a NULL HANDLE.
 */
HANDLE WINAPI RtlCreateHeap( ULONG flags, PVOID addr, SIZE_T totalSize, SIZE_T commitSize,
                             PVOID unknown, PRTL_HEAP_DEFINITION definition )
{
    SUBHEAP *subheap;

    /* Allocate the heap block */

    if (!totalSize)
    {
        totalSize = HEAP_DEF_SIZE;
        flags |= HEAP_GROWABLE;
    }

    if (!(subheap = HEAP_CreateSubHeap( NULL, addr, flags, commitSize, totalSize ))) return 0;

    heap_set_debug_flags( subheap->heap );

    /* link it into the per-process heap list */
    if (processHeap)
    {
        HEAP *heapPtr = subheap->heap;
        enter_critical_section( &processHeap->critSection );
        list_add_head( &processHeap->entry, &heapPtr->entry );
        leave_critical_section( &processHeap->critSection );
    }
    else if (!addr)
    {
        processHeap = subheap->heap;  /* assume the first heap we create is the process main heap */
        list_init( &processHeap->entry );
    }

    return subheap->heap;
}


/***********************************************************************
 *           RtlDestroyHeap   (NTDLL.@)
 *
 * Destroy a Heap created with RtlCreateHeap().
 *
 * PARAMS
 *  heap [I] Heap to destroy.
 *
 * RETURNS
 *  Success: A NULL HANDLE, if heap is NULL or it was destroyed
 *  Failure: The Heap handle, if heap is the process heap.
 */
HANDLE WINAPI RtlDestroyHeap( HANDLE heap )
{
    HEAP *heapPtr = HEAP_GetPtr( heap );
    SUBHEAP *subheap, *next;
    ARENA_LARGE *arena, *arena_next;
    SIZE_T size;
    void *addr;

    TRACE("%p\n", heap );
    if (!heapPtr) return heap;

    if (heap == processHeap) return heap; /* cannot delete the main process heap */

    /* remove it from the per-process list */
    enter_critical_section( &processHeap->critSection );
    list_remove( &heapPtr->entry );
    leave_critical_section( &processHeap->critSection );

    heapPtr->critSection.DebugInfo->Spare[0] = 0;
    RtlDeleteCriticalSection( &heapPtr->critSection );

    LIST_FOR_EACH_ENTRY_SAFE( arena, arena_next, &heapPtr->large_list, ARENA_LARGE, entry )
    {
        list_remove( &arena->entry );
        size = 0;
        addr = arena;
        NtFreeVirtualMemory( NtCurrentProcess(), &addr, &size, MEM_RELEASE );
    }
    LIST_FOR_EACH_ENTRY_SAFE( subheap, next, &heapPtr->subheap_list, SUBHEAP, entry )
    {
        if (subheap == &heapPtr->subheap) continue;  /* do this one last */
        subheap_notify_free_all(subheap);
        list_remove( &subheap->entry );
        size = 0;
        addr = subheap->base;
        NtFreeVirtualMemory( NtCurrentProcess(), &addr, &size, MEM_RELEASE );
    }
    subheap_notify_free_all(&heapPtr->subheap);
    if (heapPtr->pending_free)
    {
        size = 0;
        addr = heapPtr->pending_free;
        NtFreeVirtualMemory( NtCurrentProcess(), &addr, &size, MEM_RELEASE );
    }
    size = 0;
    addr = heapPtr->subheap.base;
    NtFreeVirtualMemory( NtCurrentProcess(), &addr, &size, MEM_RELEASE );
    return 0;
}


/***********************************************************************
 *           RtlAllocateHeap   (NTDLL.@)
 *
 * Allocate a memory block from a Heap.
 *
 * PARAMS
 *  heap  [I] Heap to allocate block from
 *  flags [I] HEAP_ flags from "winnt.h"
 *  size  [I] Size of the memory block to allocate
 *
 * RETURNS
 *  Success: A pointer to the newly allocated block
 *  Failure: NULL.
 *
 * NOTES
 *  This call does not SetLastError().
 */
PVOID WINAPI RtlAllocateHeap( HANDLE heap, ULONG flags, SIZE_T size )
{
    ARENA_FREE *pArena;
    ARENA_INUSE *pInUse;
    SUBHEAP *subheap;
    HEAP *heapPtr = HEAP_GetPtr( heap );
    SIZE_T rounded_size;

    /* Validate the parameters */

    if (!heapPtr) return NULL;
    flags &= HEAP_GENERATE_EXCEPTIONS | HEAP_NO_SERIALIZE | HEAP_ZERO_MEMORY;
    flags |= heapPtr->flags;
    rounded_size = ROUND_SIZE(size) + HEAP_TAIL_EXTRA_SIZE( flags );
    if (rounded_size < size)  /* overflow */
    {
        if (flags & HEAP_GENERATE_EXCEPTIONS) RtlRaiseStatus( STATUS_NO_MEMORY );
        return NULL;
    }
    if (rounded_size < HEAP_MIN_DATA_SIZE) rounded_size = HEAP_MIN_DATA_SIZE;

    if (!(flags & HEAP_NO_SERIALIZE)) enter_critical_section( &heapPtr->critSection );

    if (rounded_size >= HEAP_MIN_LARGE_BLOCK_SIZE && (flags & HEAP_GROWABLE))
    {
        void *ret = allocate_large_block( heap, flags, size );
        if (!(flags & HEAP_NO_SERIALIZE)) leave_critical_section( &heapPtr->critSection );
        if (!ret && (flags & HEAP_GENERATE_EXCEPTIONS)) RtlRaiseStatus( STATUS_NO_MEMORY );
        TRACE("(%p,%08x,%08lx): returning %p\n", heap, flags, size, ret );
        return ret;
    }

    /* Locate a suitable free block */

    if (!(pArena = HEAP_FindFreeBlock( heapPtr, rounded_size, &subheap )))
    {
        TRACE("(%p,%08x,%08lx): returning NULL\n",
                  heap, flags, size  );
        if (!(flags & HEAP_NO_SERIALIZE)) leave_critical_section( &heapPtr->critSection );
        if (flags & HEAP_GENERATE_EXCEPTIONS) RtlRaiseStatus( STATUS_NO_MEMORY );
        return NULL;
    }

    /* Remove the arena from the free list */

    HEAP_DeleteFreeBlock( heapPtr, pArena );

    /* Build the in-use arena */

    pInUse = (ARENA_INUSE *)pArena;

    /* in-use arena is smaller than free arena,
     * so we have to add the difference to the size */
    pInUse->size  = (pInUse->size & ~ARENA_FLAG_FREE) + sizeof(ARENA_FREE) - sizeof(ARENA_INUSE);
    pInUse->magic = ARENA_INUSE_MAGIC;

    /* Shrink the block */

    HEAP_ShrinkBlock( subheap, pInUse, rounded_size );
    pInUse->unused_bytes = (pInUse->size & ARENA_SIZE_MASK) - size;

    notify_alloc( pInUse + 1, size, flags & HEAP_ZERO_MEMORY );
    initialize_block( pInUse + 1, size, pInUse->unused_bytes, flags );

    if (!(flags & HEAP_NO_SERIALIZE)) leave_critical_section( &heapPtr->critSection );

    TRACE("(%p,%08x,%08lx): returning %p\n", heap, flags, size, pInUse + 1 );
    return pInUse + 1;
}


/***********************************************************************
 *           RtlFreeHeap   (NTDLL.@)
 *
 * Free a memory block allocated with RtlAllocateHeap().
 *
 * PARAMS
 *  heap  [I] Heap that block was allocated from
 *  flags [I] HEAP_ flags from "winnt.h"
 *  ptr   [I] Block to free
 *
 * RETURNS
 *  Success: TRUE, if ptr is NULL or was freed successfully.
 *  Failure: FALSE.
 */
BOOLEAN WINAPI RtlFreeHeap( HANDLE heap, ULONG flags, PVOID ptr )
{
    ARENA_INUSE *pInUse;
    SUBHEAP *subheap;
    HEAP *heapPtr;

    /* Validate the parameters */

    if (!ptr) return TRUE;  /* freeing a NULL ptr isn't an error in Win2k */

    heapPtr = HEAP_GetPtr( heap );
    if (!heapPtr)
    {
        RtlSetLastWin32ErrorAndNtStatusFromNtStatus( STATUS_INVALID_HANDLE );
        return FALSE;
    }

    flags &= HEAP_NO_SERIALIZE;
    flags |= heapPtr->flags;
    if (!(flags & HEAP_NO_SERIALIZE)) enter_critical_section( &heapPtr->critSection );

    /* Inform valgrind we are trying to free memory, so it can throw up an error message */
    notify_free( ptr );

    /* Some sanity checks */
    pInUse  = (ARENA_INUSE *)ptr - 1;
    if (!validate_block_pointer( heapPtr, &subheap, pInUse )) goto error;

    if (!subheap)
        free_large_block( heapPtr, flags, ptr );
    else
        HEAP_MakeInUseBlockFree( subheap, pInUse );

    if (!(flags & HEAP_NO_SERIALIZE)) leave_critical_section( &heapPtr->critSection );
    TRACE("(%p,%08x,%p): returning TRUE\n", heap, flags, ptr );
    return TRUE;

error:
    if (!(flags & HEAP_NO_SERIALIZE)) leave_critical_section( &heapPtr->critSection );
    RtlSetLastWin32ErrorAndNtStatusFromNtStatus( STATUS_INVALID_PARAMETER );
    TRACE("(%p,%08x,%p): returning FALSE\n", heap, flags, ptr );
    return FALSE;
}


/***********************************************************************
 *           RtlReAllocateHeap   (NTDLL.@)
 *
 * Change the size of a memory block allocated with RtlAllocateHeap().
 *
 * PARAMS
 *  heap  [I] Heap that block was allocated from
 *  flags [I] HEAP_ flags from "winnt.h"
 *  ptr   [I] Block to resize
 *  size  [I] Size of the memory block to allocate
 *
 * RETURNS
 *  Success: A pointer to the resized block (which may be different).
 *  Failure: NULL.
 */
PVOID WINAPI RtlReAllocateHeap( HANDLE heap, ULONG flags, PVOID ptr, SIZE_T size )
{
    ARENA_INUSE *pArena;
    HEAP *heapPtr;
    SUBHEAP *subheap;
    SIZE_T oldBlockSize, oldActualSize, rounded_size;
    void *ret;

    if (!ptr) return NULL;
    if (!(heapPtr = HEAP_GetPtr( heap )))
    {
        RtlSetLastWin32ErrorAndNtStatusFromNtStatus( STATUS_INVALID_HANDLE );
        return NULL;
    }

    /* Validate the parameters */

    flags &= HEAP_GENERATE_EXCEPTIONS | HEAP_NO_SERIALIZE | HEAP_ZERO_MEMORY |
             HEAP_REALLOC_IN_PLACE_ONLY;
    flags |= heapPtr->flags;
    if (!(flags & HEAP_NO_SERIALIZE)) enter_critical_section( &heapPtr->critSection );

    rounded_size = ROUND_SIZE(size) + HEAP_TAIL_EXTRA_SIZE(flags);
    if (rounded_size < size) goto oom;  /* overflow */
    if (rounded_size < HEAP_MIN_DATA_SIZE) rounded_size = HEAP_MIN_DATA_SIZE;

    pArena = (ARENA_INUSE *)ptr - 1;
    if (!validate_block_pointer( heapPtr, &subheap, pArena )) goto error;
    if (!subheap)
    {
        if (!(ret = realloc_large_block( heapPtr, flags, ptr, size ))) goto oom;
        goto done;
    }

    /* Check if we need to grow the block */

    oldBlockSize = (pArena->size & ARENA_SIZE_MASK);
    oldActualSize = (pArena->size & ARENA_SIZE_MASK) - pArena->unused_bytes;
    if (rounded_size > oldBlockSize)
    {
        char *pNext = (char *)(pArena + 1) + oldBlockSize;

        if (rounded_size >= HEAP_MIN_LARGE_BLOCK_SIZE && (flags & HEAP_GROWABLE))
        {
            if (flags & HEAP_REALLOC_IN_PLACE_ONLY) goto oom;
            if (!(ret = allocate_large_block( heapPtr, flags, size ))) goto oom;
            memcpy( ret, pArena + 1, oldActualSize );
            notify_free( pArena + 1 );
            HEAP_MakeInUseBlockFree( subheap, pArena );
            goto done;
        }
        if ((pNext < (char *)subheap->base + subheap->size) &&
            (*(DWORD *)pNext & ARENA_FLAG_FREE) &&
            (oldBlockSize + (*(DWORD *)pNext & ARENA_SIZE_MASK) + sizeof(ARENA_FREE) >= rounded_size))
        {
            /* The next block is free and large enough */
            ARENA_FREE *pFree = (ARENA_FREE *)pNext;
            HEAP_DeleteFreeBlock( heapPtr, pFree );
            pArena->size += (pFree->size & ARENA_SIZE_MASK) + sizeof(*pFree);
            if (!HEAP_Commit( subheap, pArena, rounded_size )) goto oom;
            notify_realloc( pArena + 1, oldActualSize, size );
            HEAP_ShrinkBlock( subheap, pArena, rounded_size );
        }
        else  /* Do it the hard way */
        {
            ARENA_FREE *pNew;
            ARENA_INUSE *pInUse;
            SUBHEAP *newsubheap;

            if ((flags & HEAP_REALLOC_IN_PLACE_ONLY) ||
                !(pNew = HEAP_FindFreeBlock( heapPtr, rounded_size, &newsubheap )))
                goto oom;

            /* Build the in-use arena */

            HEAP_DeleteFreeBlock( heapPtr, pNew );
            pInUse = (ARENA_INUSE *)pNew;
            pInUse->size = (pInUse->size & ~ARENA_FLAG_FREE)
                           + sizeof(ARENA_FREE) - sizeof(ARENA_INUSE);
            pInUse->magic = ARENA_INUSE_MAGIC;
            HEAP_ShrinkBlock( newsubheap, pInUse, rounded_size );

            mark_block_initialized( pInUse + 1, oldActualSize );
            notify_alloc( pInUse + 1, size, FALSE );
            memcpy( pInUse + 1, pArena + 1, oldActualSize );

            /* Free the previous block */

            notify_free( pArena + 1 );
            HEAP_MakeInUseBlockFree( subheap, pArena );
            subheap = newsubheap;
            pArena  = pInUse;
        }
    }
    else
    {
        notify_realloc( pArena + 1, oldActualSize, size );
        HEAP_ShrinkBlock( subheap, pArena, rounded_size );
    }

    pArena->unused_bytes = (pArena->size & ARENA_SIZE_MASK) - size;

    /* Clear the extra bytes if needed */

    if (size > oldActualSize)
        initialize_block( (char *)(pArena + 1) + oldActualSize, size - oldActualSize,
                          pArena->unused_bytes, flags );
    else
        mark_block_tail( (char *)(pArena + 1) + size, pArena->unused_bytes, flags );

    /* Return the new arena */

    ret = pArena + 1;
done:
    if (!(flags & HEAP_NO_SERIALIZE)) leave_critical_section( &heapPtr->critSection );
    TRACE("(%p,%08x,%p,%08lx): returning %p\n", heap, flags, ptr, size, ret );
    return ret;

oom:
    if (!(flags & HEAP_NO_SERIALIZE)) leave_critical_section( &heapPtr->critSection );
    if (flags & HEAP_GENERATE_EXCEPTIONS) RtlRaiseStatus( STATUS_NO_MEMORY );
    RtlSetLastWin32ErrorAndNtStatusFromNtStatus( STATUS_NO_MEMORY );
    TRACE("(%p,%08x,%p,%08lx): returning NULL\n", heap, flags, ptr, size );
    return NULL;

error:
    if (!(flags & HEAP_NO_SERIALIZE)) leave_critical_section( &heapPtr->critSection );
    RtlSetLastWin32ErrorAndNtStatusFromNtStatus( STATUS_INVALID_PARAMETER );
    TRACE("(%p,%08x,%p,%08lx): returning NULL\n", heap, flags, ptr, size );
    return NULL;
}


/***********************************************************************
 *           RtlCompactHeap   (NTDLL.@)
 *
 * Compact the free space in a Heap.
 *
 * PARAMS
 *  heap  [I] Heap that block was allocated from
 *  flags [I] HEAP_ flags from "winnt.h"
 *
 * RETURNS
 *  The number of bytes compacted.
 *
 * NOTES
 *  This function is a harmless stub.
 */
ULONG WINAPI RtlCompactHeap( HANDLE heap, ULONG flags )
{
    static BOOL reported;
    if (!reported++) FIXME( "(%p, 0x%x) stub\n", heap, flags );
    return 0;
}


/***********************************************************************
 *           RtlLockHeap   (NTDLL.@)
 *
 * Lock a Heap.
 *
 * PARAMS
 *  heap  [I] Heap to lock
 *
 * RETURNS
 *  Success: TRUE. The Heap is locked.
 *  Failure: FALSE, if heap is invalid.
 */
BOOLEAN WINAPI RtlLockHeap( HANDLE heap )
{
    HEAP *heapPtr = HEAP_GetPtr( heap );
    if (!heapPtr) return FALSE;
    enter_critical_section( &heapPtr->critSection );
    return TRUE;
}


/***********************************************************************
 *           RtlUnlockHeap   (NTDLL.@)
 *
 * Unlock a Heap.
 *
 * PARAMS
 *  heap  [I] Heap to unlock
 *
 * RETURNS
 *  Success: TRUE. The Heap is unlocked.
 *  Failure: FALSE, if heap is invalid.
 */
BOOLEAN WINAPI RtlUnlockHeap( HANDLE heap )
{
    HEAP *heapPtr = HEAP_GetPtr( heap );
    if (!heapPtr) return FALSE;
    leave_critical_section( &heapPtr->critSection );
    return TRUE;
}


/***********************************************************************
 *           RtlSizeHeap   (NTDLL.@)
 *
 * Get the actual size of a memory block allocated from a Heap.
 *
 * PARAMS
 *  heap  [I] Heap that block was allocated from
 *  flags [I] HEAP_ flags from "winnt.h"
 *  ptr   [I] Block to get the size of
 *
 * RETURNS
 *  Success: The size of the block.
 *  Failure: -1, heap or ptr are invalid.
 *
 * NOTES
 *  The size may be bigger than what was passed to RtlAllocateHeap().
 */
SIZE_T WINAPI RtlSizeHeap( HANDLE heap, ULONG flags, const void *ptr )
{
    SIZE_T ret;
    const ARENA_INUSE *pArena;
    SUBHEAP *subheap;
    HEAP *heapPtr = HEAP_GetPtr( heap );

    if (!heapPtr)
    {
        RtlSetLastWin32ErrorAndNtStatusFromNtStatus( STATUS_INVALID_HANDLE );
        return ~0UL;
    }
    flags &= HEAP_NO_SERIALIZE;
    flags |= heapPtr->flags;
    if (!(flags & HEAP_NO_SERIALIZE)) enter_critical_section( &heapPtr->critSection );

    pArena = (const ARENA_INUSE *)ptr - 1;
    if (!validate_block_pointer( heapPtr, &subheap, pArena ))
    {
        RtlSetLastWin32ErrorAndNtStatusFromNtStatus( STATUS_INVALID_PARAMETER );
        ret = ~0UL;
    }
    else if (!subheap)
    {
        const ARENA_LARGE *large_arena = (const ARENA_LARGE *)ptr - 1;
        ret = large_arena->data_size;
    }
    else
    {
        ret = (pArena->size & ARENA_SIZE_MASK) - pArena->unused_bytes;
    }
    if (!(flags & HEAP_NO_SERIALIZE)) leave_critical_section( &heapPtr->critSection );

    TRACE("(%p,%08x,%p): returning %08lx\n", heap, flags, ptr, ret );
    return ret;
}


/***********************************************************************
 *           RtlValidateHeap   (NTDLL.@)
 *
 * Determine if a block is a valid allocation from a heap.
 *
 * PARAMS
 *  heap  [I] Heap that block was allocated from
 *  flags [I] HEAP_ flags from "winnt.h"
 *  ptr   [I] Block to check
 *
 * RETURNS
 *  Success: TRUE. The block was allocated from heap.
 *  Failure: FALSE, if heap is invalid or ptr was not allocated from it.
 */
BOOLEAN WINAPI RtlValidateHeap( HANDLE heap, ULONG flags, LPCVOID ptr )
{
    HEAP *heapPtr = HEAP_GetPtr( heap );
    if (!heapPtr) return FALSE;
    return HEAP_IsRealArena( heapPtr, flags, ptr, QUIET );
}


/***********************************************************************
 *           RtlWalkHeap    (NTDLL.@)
 *
 * FIXME
 *  The PROCESS_HEAP_ENTRY flag values seem different between this
 *  function and HeapWalk(). To be checked.
 */
NTSTATUS WINAPI RtlWalkHeap( HANDLE heap, PVOID entry_ptr )
{
    LPPROCESS_HEAP_ENTRY entry = entry_ptr; /* FIXME */
    HEAP *heapPtr = HEAP_GetPtr(heap);
    SUBHEAP *sub, *currentheap = NULL;
    NTSTATUS ret;
    char *ptr;
    int region_index = 0;

    if (!heapPtr || !entry) return STATUS_INVALID_PARAMETER;

    if (!(heapPtr->flags & HEAP_NO_SERIALIZE)) enter_critical_section( &heapPtr->critSection );

    /* FIXME: enumerate large blocks too */

    /* set ptr to the next arena to be examined */

    if (!entry->lpData) /* first call (init) ? */
    {
        TRACE("begin walking of heap %p.\n", heap);
        currentheap = &heapPtr->subheap;
        ptr = (char*)currentheap->base + currentheap->headerSize;
    }
    else
    {
        ptr = entry->lpData;
        LIST_FOR_EACH_ENTRY( sub, &heapPtr->subheap_list, SUBHEAP, entry )
        {
            if ((ptr >= (char *)sub->base) &&
                (ptr < (char *)sub->base + sub->size))
            {
                currentheap = sub;
                break;
            }
            region_index++;
        }
        if (currentheap == NULL)
        {
            ERR("no matching subheap found, shouldn't happen !\n");
            ret = STATUS_NO_MORE_ENTRIES;
            goto HW_end;
        }

        if (((ARENA_INUSE *)ptr - 1)->magic == ARENA_INUSE_MAGIC ||
            ((ARENA_INUSE *)ptr - 1)->magic == ARENA_PENDING_MAGIC)
        {
            ARENA_INUSE *pArena = (ARENA_INUSE *)ptr - 1;
            ptr += pArena->size & ARENA_SIZE_MASK;
        }
        else if (((ARENA_FREE *)ptr - 1)->magic == ARENA_FREE_MAGIC)
        {
            ARENA_FREE *pArena = (ARENA_FREE *)ptr - 1;
            ptr += pArena->size & ARENA_SIZE_MASK;
        }
        else
            ptr += entry->cbData; /* point to next arena */

        if (ptr > (char *)currentheap->base + currentheap->size - 1)
        {   /* proceed with next subheap */
            struct list *next = list_next( &heapPtr->subheap_list, &currentheap->entry );
            if (!next)
            {  /* successfully finished */
                TRACE("end reached.\n");
                ret = STATUS_NO_MORE_ENTRIES;
                goto HW_end;
            }
            currentheap = LIST_ENTRY( next, SUBHEAP, entry );
            ptr = (char *)currentheap->base + currentheap->headerSize;
        }
    }

    entry->wFlags = 0;
    if (*(DWORD *)ptr & ARENA_FLAG_FREE)
    {
        ARENA_FREE *pArena = (ARENA_FREE *)ptr;

        /*TRACE("free, magic: %04x\n", pArena->magic);*/

        entry->lpData = pArena + 1;
        entry->cbData = pArena->size & ARENA_SIZE_MASK;
        entry->cbOverhead = sizeof(ARENA_FREE);
        entry->wFlags = PROCESS_HEAP_UNCOMMITTED_RANGE;
    }
    else
    {
        ARENA_INUSE *pArena = (ARENA_INUSE *)ptr;

        /*TRACE("busy, magic: %04x\n", pArena->magic);*/

        entry->lpData = pArena + 1;
        entry->cbData = pArena->size & ARENA_SIZE_MASK;
        entry->cbOverhead = sizeof(ARENA_INUSE);
        entry->wFlags = (pArena->magic == ARENA_PENDING_MAGIC) ?
                        PROCESS_HEAP_UNCOMMITTED_RANGE : PROCESS_HEAP_ENTRY_BUSY;
        /* FIXME: can't handle PROCESS_HEAP_ENTRY_MOVEABLE
        and PROCESS_HEAP_ENTRY_DDESHARE yet */
    }

    entry->iRegionIndex = region_index;

    /* first element of heap ? */
    if (ptr == (char *)currentheap->base + currentheap->headerSize)
    {
        entry->wFlags |= PROCESS_HEAP_REGION;
        entry->u.Region.dwCommittedSize = currentheap->commitSize;
        entry->u.Region.dwUnCommittedSize =
                currentheap->size - currentheap->commitSize;
        entry->u.Region.lpFirstBlock = /* first valid block */
                (char *)currentheap->base + currentheap->headerSize;
        entry->u.Region.lpLastBlock  = /* first invalid block */
                (char *)currentheap->base + currentheap->size;
    }
    ret = STATUS_SUCCESS;
    if (TRACE_ON(heap)) HEAP_DumpEntry(entry);

HW_end:
    if (!(heapPtr->flags & HEAP_NO_SERIALIZE)) leave_critical_section( &heapPtr->critSection );
    return ret;
}


/***********************************************************************
 *           RtlGetProcessHeaps    (NTDLL.@)
 *
 * Get the Heaps belonging to the current process.
 *
 * PARAMS
 *  count [I] size of heaps
 *  heaps [O] Destination array for heap HANDLE's
 *
 * RETURNS
 *  Success: The number of Heaps allocated by the process.
 *  Failure: 0.
 */
ULONG WINAPI RtlGetProcessHeaps( ULONG count, HANDLE *heaps )
{
    ULONG total = 1;  /* main heap */
    struct list *ptr;

    enter_critical_section( &processHeap->critSection );
    LIST_FOR_EACH( ptr, &processHeap->entry ) total++;
    if (total <= count)
    {
        *heaps++ = processHeap;
        LIST_FOR_EACH( ptr, &processHeap->entry )
            *heaps++ = LIST_ENTRY( ptr, HEAP, entry );
    }
    leave_critical_section( &processHeap->critSection );
    return total;
}

/***********************************************************************
 *           RtlQueryHeapInformation    (NTDLL.@)
 */
NTSTATUS WINAPI RtlQueryHeapInformation( HANDLE heap, HEAP_INFORMATION_CLASS info_class,
                                         PVOID info, SIZE_T size_in, PSIZE_T size_out)
{
    switch (info_class)
    {
    case HeapCompatibilityInformation:
        if (size_out) *size_out = sizeof(ULONG);

        if (size_in < sizeof(ULONG))
            return STATUS_BUFFER_TOO_SMALL;

        *(ULONG *)info = 0; /* standard heap */
        return STATUS_SUCCESS;

    default:
        FIXME("Unknown heap information class %u\n", info_class);
        return STATUS_INVALID_INFO_CLASS;
    }
}

/***********************************************************************
 *           RtlSetHeapInformation    (NTDLL.@)
 */
NTSTATUS WINAPI RtlSetHeapInformation( HANDLE heap, HEAP_INFORMATION_CLASS info_class, PVOID info, SIZE_T size)
{
    FIXME("%p %d %p %ld stub\n", heap, info_class, info, size);
    return STATUS_SUCCESS;
}
