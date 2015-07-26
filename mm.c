/*
 mm.c

      Name: Ken Ling
 Andrew ID: kling1

 Description: This is a dynamite memory allocator based on segregated list with 
 immediate coalescing strategy. The footer of a normal block will be eliminated 
 if the block is allocated.

 A nomal free block is like: |HDR|SUCC|PRED|...(space for payload)...|FTR|
 A normal allocated block is:|HDR|..............PAYLOAD..............|FTR|
 A normal free block has 4 fields, so the minimum size of a normal block is 16 
 bytes.
 
 HDR -  the block header which consists of block size, allocated bit of this 
 block and allocated bit of previous block.
 SUCC - the offset of the block's successor in seglist. Offset is calculated 
 by HEAP_OFFSET(bp).
 PRED - the offset of the block's predecessor in seglist. Offset is calculated 
 by HEAP_OFFSET(bp).
 FTR - the block footer which consists of block size, allocated bit of this 
 block and allocated bit of previous block.
 
 The block pointer points to SUCC field. When a block is allocated, the SUCC, 
 PRED and FTR will be replaced by payload. If payload is larger than 12 bytes, 
 a block will have additional space between HDR and FTR.

 The structure of heap is as follows: (each field costs 4 bytes)
 |tail|tail|seg_header_i|HDR|FTR|HDR|SUCC|PRED|...space...|FTR|HDR|
                        (pro    (    many normal blocks       )(epi
                        -logue)                                -logue)

 Tail is the end sentinel of all level of seglist. The second byte of tail 
 immitates the PRED field which gurantees the uniform of coding.
 Seg_header_i (equals SUCC field) stores the offset of first element in i-level 
 seglist. It stores offset of tail (0) when i-levle seglist is empty.
 Prologue block and epilogue block have 8 bytes (HDR, FTR) and 4 bytes (HDR) 
 respectively as are shown above.

 Segregated list is organized as follows:
 seg_header_i --> block --> block --> tail
 The i-level seglist (i starts from 0) contains blocks whose size 
 is [ 16 << i, 16 << (i+1) ) except for the last level seglist which 
 contains block with size to infinity.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */

// #define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
# define dbg_checkheap(...) checkheap(__VA_ARGS__)
#else
# define dbg_printf(...)
# define dbg_checkheap(...)
#endif

/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* 8-byte alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(p) (((size_t)(p) + (ALIGNMENT-1)) & ~0x7)

/* Basic constants and macros */
#define FSIZE       4       /* Size of each field in a block (bytes) */
#define CHUNKSIZE  (1<<9)  /* Extend heap by this amount (bytes) */
#define N_SEGLIST   13      /* Number of different level of seglists. Should be 
an odd number to guarantee alignment of heap*/

#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* pack a size, allocated bit of current block and allocated bit of previous 
block into a word */
#define PACK(size, alloc, prev_alloc) ((size) | (alloc) | (prev_alloc << 1))

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))

#ifdef DEBUG
 /*
  Debug garbled bytes problem.
  Test whether p points to memory allocated to user.
  */
 #define PUT(p, val)  { \
  (*(unsigned int *)(p) = (val)); \
  check_access_user_memory(p, __LINE__);}
#else
 #define PUT(p, val)  (*(unsigned int *)(p) = (val))   
#endif

/* Read the size, allocated bit block and allocated bit of previous block from 
address p */
#define GET_SIZE(p)  (GET(p) & ~0x7) 
#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_PREV_ALLOC(p) ((GET(p) & 0x2) >> 1)

/* Given block ptr bp, compute address of its HDR, SUCC, PRED and FTR */
#define HDRP(bp)       ((char *)(bp) - FSIZE) 
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - 2*FSIZE) 
#define SUCCP(bp)      ((char *)(bp))
#define PREDP(bp)      ((char *)(bp) + FSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - FSIZE))) 
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - 2*FSIZE))) 

/* Given block ptr bp, compute the block ptr of its successor or predecessor in 
the segregated list */
#define SUCC_FREE_BLKP(bp)  (heap_startp + GET(SUCCP(bp)))
#define PRED_FREE_BLKP(bp)  (heap_startp + GET(PREDP(bp)))

/* compute the relative offset from a block pointer to start address of heap 
which saves space than storing a real pointer in the block */
#define HEAP_OFFSET(bp) ((char *)(bp) - heap_startp)

/*
    The following block of code is used to debug "garbled bytes". It checks if 
    a pointer points to memory allocated to users, which is used to check 
    access to user memory.
    The variables and functions are only defined in DEBUG mode.
    Although it contains global array, it does not count in the real memory 
    allocator.
*/
#ifdef DEBUG
#define ARRAY_SIZE 2000
typedef struct user_mm {
    char *bp;
    size_t size;
} user_mm;
// the global array is onle used for debugging, which will not be
// defined when actually using this memory allocator
static user_mm user_mm_array[ARRAY_SIZE];
static int mm_array_tail = 0;

/* Add the address and size of allocated memory in record array. Called when 
malloc() is called. */
static void add_to_user_mm_array(char *bp, size_t size) {
    if (mm_array_tail == ARRAY_SIZE) {
        printf("too many blocks for array\n");
        exit(1);
    }
    user_mm_array[mm_array_tail].bp = bp;
    user_mm_array[mm_array_tail].size = size;
    mm_array_tail ++;
}

/* Remove a memory block in record array. Called when free() is called. */
static void remove_from_user_mm_array(char *bp) {
    int i = 0;
    for (i = 0; i < mm_array_tail; i ++) {
        if (user_mm_array[i].bp == bp) {
            user_mm_array[i].bp = 0;
            user_mm_array[i].size = 0;
            return;
        }
    }
    printf("error free: no corresponding block in user mm array: %p\n", bp);
}

/* Check if a pointer points to user memory */
static void check_access_user_memory(char *ptr, int lineno) {
    int i = 0;
    for (i = 0; i < mm_array_tail; i ++) {
        if ((ptr >= user_mm_array[i].bp) && 
            (ptr < user_mm_array[i].bp + user_mm_array[i].size)) {
            printf("(%d) Invalid access user allocated memory, ptr: %p\n",
             lineno, ptr);
        }
    }
}
#endif

/* Global variables */
static char *heap_startp = 0; /* Pointer to start of heap */
static char *heap_listp = 0;  /* Pointer to first block */ 
static char *tail = 0;        /* End sentinel of all level of seglists */

/* Function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void checkheap(int lineno, int verbose);
static void printblock(void *bp); 
static void checkblock(void *bp, int lineno);
static size_t check_list(int lineno, int verbose);
static inline void *get_root(unsigned int asize);

/*
 Initialize global variables and the heap including prologue block, 
 epilogue block, header of each level of seglist and tail of all levels 
 of seglist.
 Request the first free block.
 */
int mm_init(void) {
    dbg_printf("INIT\n");

    /* debug garbled bytes */
    #ifdef DEBUG
    memset(user_mm_array, 0, ARRAY_SIZE * sizeof(user_mm));
    mm_array_tail = 0;
    #endif

    /* Create the initial empty heap */
    if ((heap_startp = mem_sbrk((N_SEGLIST + 5)*FSIZE)) == (void *)-1) 
        return -1;

    PUT(heap_startp, 0); /* Address of tail and the SUCC field of tail */
    PUT(heap_startp + (1*FSIZE), 0); /*PRED field of tail*/
    tail = heap_startp;

    int i = 2;
    for (i = 2; i < N_SEGLIST + 2; i ++) { /* header of each level of seglist */
        // Initialize each seg_header and let them point to tail
        PUT(heap_startp + (i*FSIZE), HEAP_OFFSET(tail)); 
    }

    PUT(heap_startp + ((N_SEGLIST + 2)*FSIZE),
     PACK(2*FSIZE, 1, 1)); /* Prologue block header */
    PUT(heap_startp + ((N_SEGLIST + 3)*FSIZE),
     PACK(2*FSIZE, 1, 1)); /* Prologue block footer */
    PUT(heap_startp + ((N_SEGLIST + 4)*FSIZE),
     PACK(0, 1, 1)); /* Epilogue block header */
    heap_listp = heap_startp + (N_SEGLIST + 3)*FSIZE;
    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/FSIZE) == NULL){ 
        return -1;
    }

    dbg_checkheap(__LINE__, 0);
    dbg_printf("END INIT\n");
    
    return 0;
}

/*
 Allocate memory to user according to size. First get the real size of 
 allocation by calculating aszie (adjusted block size).
 */
void *malloc (size_t size) {
    dbg_printf("MALLOC (size: %ld)\n", size);

    size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    size_t tmp;
    char *bp;      

    if (heap_listp == 0){
        mm_init();
    }
    /* Ignore spurious requests */
    if (size == 0){
        dbg_checkheap(__LINE__, 0);
        dbg_printf("END MALLOC (size == 0)\n");
        return NULL;
    }

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= 2*FSIZE){
        asize = 4*FSIZE;
    }
    else{
        /* tmp is the number of fields needed for payload */
        tmp = (size + (FSIZE - 1)) / FSIZE;
        asize = (tmp & 0x1 ? (tmp + 1) : (tmp + 2)) * FSIZE;
    }

    /* Search the seglist list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        dbg_checkheap(__LINE__, 0);
        dbg_printf("END MALLOC (find_fit succeed)\n");
        /* debug garbled bytes */
        #ifdef DEBUG
        add_to_user_mm_array(bp, size);
        #endif
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);
    if ((bp = extend_heap(extendsize/FSIZE)) == NULL) { 
        dbg_checkheap(__LINE__, 0);
        dbg_printf("END MALLOC (extend_heap Fails)\n");
        return NULL;
    }
    place(bp, asize);
    dbg_checkheap(__LINE__, 0);
    dbg_printf("END MALLOC (extend_heap)\n");
    /* debug garbled bytes */
    #ifdef DEBUG
    add_to_user_mm_array(bp, size);
    #endif
    return bp;
}

/*
 Free the memory block pointed by bp.
 */
void free(void *bp) {
    dbg_printf("FREE\n");

    if (bp == 0) {
        return;
    }

    size_t size = GET_SIZE(HDRP(bp));

    if (heap_listp == 0){
        mm_init();
    }

    /* debug garbled bytes */
    #ifdef DEBUG
    remove_from_user_mm_array(bp);
    #endif
    /* Change the state of this block to free */
    unsigned int prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0, prev_alloc));
    PUT(FTRP(bp), PACK(size, 0, prev_alloc));

    void *next_bp = NEXT_BLKP(bp);

    /* change the prev_allocated bit of next block */
    unsigned int block_size = GET_SIZE(HDRP(next_bp));
    unsigned int block_alloced = GET_ALLOC(HDRP(next_bp));
    PUT(HDRP(next_bp), PACK(block_size, block_alloced, 0));
    if (!block_alloced) {
        /*the next block is free, so it has a footer */
        PUT(FTRP(next_bp), PACK(block_size, block_alloced, 0));
    }
    coalesce(bp);

    dbg_printf("END FREE\n");
}

/*
 Reallocated the memory block pointed by ptr to a block of size bytes
 */
void *realloc(void *ptr, size_t size) {
    size_t oldsize;
    void *newptr;

    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0) {
        mm_free(ptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if(ptr == NULL) {
        return mm_malloc(size);
    }

    newptr = mm_malloc(size);

    /* If realloc() fails the original block is left untouched  */
    if(!newptr) {
        return 0;
    }

    /* Copy the old data. */
    oldsize = GET_SIZE(HDRP(ptr));
    if(size < oldsize) oldsize = size;
    memcpy(newptr, ptr, oldsize);

    /* Free the old block. */
    mm_free(ptr);

    return newptr;
}

/*
 malloc with content of memory initialized to zero
 */
void *calloc (size_t nmemb, size_t size) {
    size_t bytes = nmemb * size;
    void *newptr;

    newptr = malloc(bytes);
    if (newptr == NULL) {
        return NULL;
    }
    memset(newptr, 0, bytes);

    return newptr;
}

/*
 Return whether the pointer is in the heap.
 */
static int in_heap(const void *p) {
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}

/*
 Return whether the pointer is aligned.
 */
static int aligned(const void *p) {
    return (size_t)ALIGN(p) == (size_t)p;
}

/*
 Get the entrance to a level of seg_list according to asize
*/
static inline void *get_root(unsigned int asize) {
    size_t size = (size_t)asize;
    size_t mask = 0x80000000;
    int k = 32;
    for (k = 32; k > 0; k --) {
        if (size & mask) {
            break;
        }
        mask = mask >> 1;
    }
    if (k > N_SEGLIST + 4) {
        // the lowest level of seglist with block size [16, 32)
        return heap_startp + ((N_SEGLIST + 1)*FSIZE);
    } else if (k < 5) {
        // the highest level of seglist with block size to infinity
        return heap_startp + (2*FSIZE);
    }
    return heap_startp + ((k-3)*FSIZE);

}

/*
 If a free block has adjacent free blocks, then coalesce them together.
*/
static void *coalesce(void *bp) 
{
    dbg_printf("COALESCE\n");

    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    unsigned int size = GET_SIZE(HDRP(bp));
    
    if (prev_alloc && next_alloc) {
        /* Case 1 - no adjacent free blocks */
        dbg_printf("case 1\n");
    } else if (!prev_alloc && next_alloc) {
        /* Case 2 - previous block is free, next block is allocated */
        dbg_printf("case 2\n");

        void *prev_bp = PREV_BLKP(bp);
        PUT(SUCCP(PRED_FREE_BLKP(prev_bp)), 
            HEAP_OFFSET(SUCC_FREE_BLKP(prev_bp)));
        PUT(PREDP(SUCC_FREE_BLKP(prev_bp)), 
            HEAP_OFFSET(PRED_FREE_BLKP(prev_bp)));
        
        size += GET_SIZE(HDRP(prev_bp));
        bp = prev_bp;    
        
    } else if (prev_alloc && !next_alloc) {
        /* Case 3 - previous block is allocated, next block is free */
        dbg_printf("case 3\n");

        void *next_bp = NEXT_BLKP(bp);
        PUT(SUCCP(PRED_FREE_BLKP(next_bp)), 
            HEAP_OFFSET(SUCC_FREE_BLKP(next_bp)));
        PUT(PREDP(SUCC_FREE_BLKP(next_bp)), 
            HEAP_OFFSET(PRED_FREE_BLKP(next_bp)));

        size += GET_SIZE(HDRP(next_bp));
    } else {
        /* Case 4 - previous block and next block are both free */
        dbg_printf("case 4\n");

        void *prev_bp = PREV_BLKP(bp);
        void *next_bp = NEXT_BLKP(bp);
        PUT(SUCCP(PRED_FREE_BLKP(prev_bp)), 
            HEAP_OFFSET(SUCC_FREE_BLKP(prev_bp)));
        PUT(PREDP(SUCC_FREE_BLKP(prev_bp)), 
            HEAP_OFFSET(PRED_FREE_BLKP(prev_bp)));
        PUT(SUCCP(PRED_FREE_BLKP(next_bp)), 
            HEAP_OFFSET(SUCC_FREE_BLKP(next_bp)));
        PUT(PREDP(SUCC_FREE_BLKP(next_bp)), 
            HEAP_OFFSET(PRED_FREE_BLKP(next_bp)));

        size += GET_SIZE(HDRP(prev_bp)) + GET_SIZE(HDRP(next_bp));
        bp = prev_bp;
    }
    PUT(HDRP(bp), PACK(size, 0, 1));
    PUT(FTRP(bp), PACK(size, 0, 1));
    /* Link the coalesced block back into seglist */
    void *root = get_root(size);
    PUT(SUCCP(bp), HEAP_OFFSET(SUCC_FREE_BLKP(root)));
    PUT(PREDP(bp), HEAP_OFFSET(root));
    PUT(PREDP(SUCC_FREE_BLKP(bp)), HEAP_OFFSET(bp));
    PUT(SUCCP(root), HEAP_OFFSET(bp));

    dbg_checkheap(__LINE__, 0);
    dbg_printf("END COALESCE\n");
    return bp;
}

/* 
 Extend heap with free block and return its block pointer
 */
static void *extend_heap(size_t words) 
{
    dbg_printf("EXTEND_HEAP\n");
    /* the minimum size of a block is 16 bytes (HDR, SUCC, PRED, FTR) */
    if (words < 4) {
        words = 4;
    }
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * FSIZE : words * FSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)  
        return NULL;

    /* Initialize free block header/footer and the epilogue header */
    unsigned int prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0, prev_alloc)); /* Free block header */    
    PUT(FTRP(bp), PACK(size, 0, prev_alloc)); /* Free block footer */
    
    char *epi = NEXT_BLKP(bp);
    PUT(HDRP(epi), PACK(0, 1, 0)); /* New epilogue header */
    /* Coalesce if the previous block was free */
    void *rt = coalesce(bp);

    dbg_printf("END EXTEND_HEAP\n");
    return rt;
}

/* 
 Place block of asize bytes at start of free block bp 
 and split if remainder would be at least minimum block size
 */
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));   
    unsigned int prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    if ((csize - asize) >= (4*FSIZE)) {
        dbg_printf("Case: (csize - asize) >= (4*FSIZE)\n");

        PUT(HDRP(bp), PACK(asize, 1, prev_alloc));
        /* The block is allocated. No footer */
        PUT(SUCCP(PRED_FREE_BLKP(bp)), HEAP_OFFSET(SUCC_FREE_BLKP(bp)));
        PUT(PREDP(SUCC_FREE_BLKP(bp)), HEAP_OFFSET(PRED_FREE_BLKP(bp)));
        /* The splitted free block */
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-asize, 0, 1));
        PUT(FTRP(bp), PACK(csize-asize, 0, 1));
        coalesce(bp);
    }
    else {
        dbg_printf("Case: (csize - asize) < (4*FSIZE)\n");

        PUT(HDRP(bp), PACK(csize, 1, prev_alloc));
        /* The block is allocated. No footer */
        PUT(SUCCP(PRED_FREE_BLKP(bp)), HEAP_OFFSET(SUCC_FREE_BLKP(bp)));
        PUT(PREDP(SUCC_FREE_BLKP(bp)), HEAP_OFFSET(PRED_FREE_BLKP(bp)));
        /* Change the prev_allocated bit of next block */
        bp = NEXT_BLKP(bp);
        unsigned int block_size = GET_SIZE(HDRP(bp));
        unsigned int block_alloced = GET_ALLOC(HDRP(bp));
        PUT(HDRP(bp), PACK(block_size, block_alloced, 1));
        if (!block_alloced) {
            /* This block is free, so it has a footer */
            PUT(FTRP(bp), PACK(block_size, block_alloced, 1));
        }
    }
}

/* 
 Find a fit for a block with asize bytes in the seglist.
 It starts searching from the lowest possible level of seglish which contains
 block of asize. If no free block, then move to the higher level.
 */

static void *find_fit(size_t asize)
{
    /* First-fit search */
    char *root = get_root(asize);
    while (root != (heap_startp + ((N_SEGLIST + 2)*FSIZE))) {
        /* Search in a level of seglist */
        void *bp = SUCC_FREE_BLKP(root);
        while (bp != tail) {
            if (GET_SIZE(HDRP(bp)) >= asize) {
                return bp;
            }
            bp = SUCC_FREE_BLKP(bp);
        }
        /* Move to the higher level */
        root = root + FSIZE;
    }
    return NULL; /* No fit */
}

/*
 Print out the information of a block for debugging
 */
static void printblock(void *bp) 
{
    size_t hsize, halloc, hprevalloc, fsize, falloc, fprevalloc;

    hsize = GET_SIZE(HDRP(bp));
    halloc = GET_ALLOC(HDRP(bp));
    hprevalloc = GET_PREV_ALLOC(HDRP(bp));  
    fsize = GET_SIZE(FTRP(bp));
    falloc = GET_ALLOC(FTRP(bp));  
    fprevalloc = GET_PREV_ALLOC(FTRP(bp));

    if (hsize == 0) {
        /* Epilogue */
        printf("%p: EOL   :[%ld:%ld:%ld]\n", bp, hsize, halloc, hprevalloc);
        return;
    }
 
    if (halloc) {
        /* Allocated block with no footer */
        printf("%p: header: [%ld:%ld:%ld]\n", bp, hsize, halloc, hprevalloc);
    } else {
        /* Free block with footer */
        printf("%p: header: [%ld:%ld:%ld] footer: [%ld:%ld:%ld] \
         PRED: %x, SUCC: %x\n",
            bp,
            hsize, halloc, hprevalloc, fsize, falloc, fprevalloc,
            GET(PREDP(bp)), GET(SUCCP(bp)));
    }
}

/*
 Heapcheck function that will be automatically called in driver with -d.
 */
void mm_checkheap(int lineno) {
    checkheap(lineno, 1);
}

/*
 Real implementation of heapcheck function
 */
void checkheap(int lineno, int verbose) {
    if (verbose){
        if (lineno == 1) {
            /* Called by driver */
            printf("============================================\n");
        } else {
            /* Called manually */
            printf("********My Call*********\n");
        }
    }

    /* Check prologue block */
    if (verbose) {
        printf("Heap (%p):\n", heap_listp);
    }
    /* Check alignment and allocation bit */
    if ((GET_SIZE(HDRP(heap_listp))!=2*FSIZE)||!GET_ALLOC(HDRP(heap_listp))){   
        printf("(%d) Bad prologue header\n", lineno);
    }
    /* Check matching of header and footer */
    if (GET(HDRP(heap_listp)) != GET(FTRP(heap_listp))) { 
        printf("(%d) Bad prologue header does not match footer\n\n", lineno);
    }
    /* Check heap boundary */
    if (!in_heap(heap_listp)) {
        printf("(%d) Outside heap boundary\n", lineno);
    }
    size_t hsize, halloc, hprevalloc, fsize, falloc, fprevalloc;
    hsize = GET_SIZE(HDRP(heap_listp));
    halloc = GET_ALLOC(HDRP(heap_listp));
    hprevalloc = GET_PREV_ALLOC(HDRP(heap_listp));
    fsize = GET_SIZE(FTRP(heap_listp));
    falloc = GET_ALLOC(FTRP(heap_listp));
    fprevalloc = GET_PREV_ALLOC(FTRP(heap_listp));
    printf("%p: header: [%ld:%ld:%ld] footer: [%ld:%ld:%ld]\n",
            heap_listp,
            hsize, halloc, hprevalloc,
            fsize, falloc, fprevalloc);

    size_t free_blocks = 0; // number of free blocks in heap
    size_t prev_alloc = 1;
    char *bp;
    for (bp=NEXT_BLKP(heap_listp); GET_SIZE(HDRP(bp)) > 0; bp=NEXT_BLKP(bp)) {
        if (verbose) {
            printblock(bp);
        }
        checkblock(bp, lineno);
        /* Check previous allocate bit consistency */
        if (GET_PREV_ALLOC(HDRP(bp)) != prev_alloc) {
            printf("(%d) Error: %p prev_alloc bit: %d, \
                alloc_bit of prev blk: %zu\n", 
                lineno, bp, GET_PREV_ALLOC(HDRP(bp)), prev_alloc);
        }
        /* Check coalescing: whether consecutive free blocks exist */
        if (prev_alloc == 0 && GET_ALLOC(HDRP(bp)) == 0) {
            printf("(%d) Error: %p two consecutive free blocks in heap\n", 
                lineno, bp);
        }
        if (!GET_ALLOC(HDRP(bp))) {
            free_blocks ++;
        }
        prev_alloc = GET_ALLOC(HDRP(bp));
    }

    /* Check epilogue block */
    if (verbose) {
        printblock(bp);
    }
    /* Check alignment and allocation bit */
    if ((GET_SIZE(HDRP(bp)) != 0) || !(GET_ALLOC(HDRP(bp))))
        printf("(%d) Bad epilogue header\n", lineno);
    /* Check the segregated list */
    size_t free_blocks_in_list = check_list(lineno, verbose);
    /* The number of free blocks in heap and in seglist do not match */
    if (free_blocks != free_blocks_in_list) {
        printf(
            "(%d) free blocks: %ld, free blocks in list: %ld\n", 
            lineno, free_blocks, free_blocks_in_list);
    }

    /* Check heap boundary */
    if (!in_heap(bp)) {
        printf("(%d) Outside heap boundary\n", lineno);
    }
}

/* Check a specific block */
static void checkblock(void *bp, int lineno) 
{
    /* Check block's alignment */
    if (!aligned(bp)) {
        printf("(%d) Error: %p is not doubleword aligned\n", lineno, bp);
    }
    /* Check minimum size of a block */
    if ((bp != heap_listp) && GET_SIZE(HDRP(bp)) < 4 * FSIZE) {
        printf("(%d) Error: %p block size is smaller than minimum size\n", 
            lineno, bp);
    }
    /* Check matching of header and footer */
    if (!GET_ALLOC(HDRP(bp))) {
        // This is a free block and has its footer
        if (GET(HDRP(bp)) != GET(FTRP(bp))){
            printf("(%d) Error: header does not match footer\n", lineno);
        }
    }
}

/*
 Check the free list (segregated list)
 Return the number of free blocks in seglist.
 */
static size_t check_list(int lineno, int verbose) {
    if (verbose) {
        printf("(%d) Segregated list:\n", lineno);
    }

    size_t free_blocks_in_list = 0;
    
    int i = 0;
    for (i = 0; i < N_SEGLIST; i ++) {
        /* Check a specific level of seglist */
        void *root = heap_startp + ((i + 2)*FSIZE);
        unsigned int level_size = (1 << (4+i));
        printf("root (size %u): %p\n", level_size, root);

        void *ptr = SUCC_FREE_BLKP(root);
        while (ptr != tail) {
            /* Cound free blocks in seglist */
            free_blocks_in_list ++;
            if (verbose) {
                printblock(ptr);
            }
            /* Check whether the block is outside the heap boundary,
            whether list pointers points between mem_heap_lo and_mem heap_high
            */
            if (!in_heap(ptr)) {
                printf("(%d) %p out of heap\n", lineno, ptr);
            }
            /* Check the consistency of pointers */
            if (SUCC_FREE_BLKP(PRED_FREE_BLKP(ptr)) != ptr) {
                printf("(%d) %p inconsistent ptr->pred->succ\n", lineno, ptr);
            }
            /* Check the consistency of pointers */
            if ((SUCC_FREE_BLKP(ptr) != tail) && 
                PRED_FREE_BLKP(SUCC_FREE_BLKP(ptr)) != ptr) {
                printf("(%d) %p inconsistent ptr->succ->pred\n", lineno, ptr);
            }
            /* Check whether a blocks falls into the right level of seglist */
            unsigned int block_size = GET_SIZE(HDRP(ptr));
            if (i != N_SEGLIST - 1) { // not the higest level, has upper bound
                if (block_size < level_size || block_size >= 2 * level_size) {
                    printf("(%d) %p with size of %u in the wrong list %u\n", 
                        lineno, ptr, block_size, level_size);
                }
            } else { // highest level, no upper bound for block size
                if (block_size < level_size) {
                    printf("(%d) %p with size of %u in the wrong list %u\n", 
                        lineno, ptr, block_size, level_size);
                }
            }
            ptr = SUCC_FREE_BLKP(ptr);
        }
    }

    if (verbose) { // Tail sentinel
        printf("all tail: %p, pred: %p\n", tail, PRED_FREE_BLKP(tail));
        printf("(%d) END check list\n", lineno);
    }

    return free_blocks_in_list;
}
