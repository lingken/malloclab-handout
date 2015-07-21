/*
 * mm.c
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
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
#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif


/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(p) (((size_t)(p) + (ALIGNMENT-1)) & ~0x7)

/* $begin mallocmacros */
/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */ //line:vm:mm:beginconst
#define DSIZE       8       /* Double word size (bytes) */
#define CHUNKSIZE  (1<<12)  /* Extend heap by this amount (bytes) */  //line:vm:mm:endconst 

#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc)) //line:vm:mm:pack

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))            //line:vm:mm:get
#define PUT(p, val)  (*(unsigned int *)(p) = (val))    //line:vm:mm:put

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)                   //line:vm:mm:getsize
#define GET_ALLOC(p) (GET(p) & 0x1)                    //line:vm:mm:getalloc

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)                      //line:vm:mm:hdrp
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) //line:vm:mm:ftrp

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) //line:vm:mm:nextblkp
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) //line:vm:mm:prevblkp
/* $end mallocmacros */

/* Global variables */
static char *heap_listp = 0;  /* Pointer to first block */  
#ifdef NEXT_FIT
static char *rover;           /* Next fit rover */
#endif

/* Given block ptr bp, compute address of its successor or predecessor
   field which stores the offset of adjacent block in the explicit free list*/
#define SUCCP(bp) ((char *)(bp) + WSIZE)
#define PREDP(bp) ((char *)(bp) + DSIZE)

/* Given block ptr bp, compute the block ptr of its successor or predecessor
   in the explicit free list */
#define SUCC_FREE_BLKP(bp) (heap_listp + GET(SUCCP(bp)))
#define PRED_FREE_BLKP(bp) (heap_listp + GET(PREDP(bp)))

/* compute the relative offset from a block pointer to first block of heap
   which saves space than storing a real pointer in the block */
HEAP_OFFSET(bp) ((char *)(bp) - heap_listp)

/* See if the previous block is allocated to eliminate unneccesary footer fields*/
#define GET_PREV_ALLOC(p) ((GET(p) & 0x2) >> 1)
/* pack a size, allocated bit of current block and allocated bit of previous block into a word */
#define NEW_PACK(size, alloc, prev_alloc) ((size) | (alloc) | (prev_alloc << 1))

/* Function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void printblock(void *bp); 
static void checkheap(int verbose);
static void checkblock(void *bp);

/*
 * Initialize: return -1 on error, 0 on success.
 */
int mm_init(void) {
    /* Create the initial empty heap */
    root = NULL;
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) //line:vm:mm:begininit
        return -1;
    PUT(heap_listp, 0);                          /* Alignment padding */
    PUT(heap_listp + (1*WSIZE), NEW_PACK(DSIZE, 1, 1)); /* Prologue header */ 
    PUT(heap_listp + (2*WSIZE), NEW_PACK(DSIZE, 1, 1)); /* Prologue footer */ 
    PUT(heap_listp + (3*WSIZE), NEW_PACK(0, 1, 1));     /* Epilogue header */
    heap_listp += (2*WSIZE);                     //line:vm:mm:endinit  


    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if ((root = extend_heap(CHUNKSIZE/WSIZE)) == NULL) 
        return -1;
    return 0;
}

/*
 * malloc
 */
void *malloc (size_t size) {
    size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    size_t tmp;
    char *bp;      

    if (heap_listp == 0){
        mm_init();
    }

    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE){                                          //line:vm:mm:sizeadjust1
        asize = 2*DSIZE;
    }                                        //line:vm:mm:sizeadjust2
    else{
        // asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE); //line:vm:mm:sizeadjust3
        tmp = (size + (WSIZE - 1)) / WSIZE; // real payload
        asize = (tmp & 0x1 ? (tmp + 1) : (tmp + 2)) * WSIZE;
    }

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {  //line:vm:mm:findfitcall
        place(bp, asize);                  //line:vm:mm:findfitplace
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);                 //line:vm:mm:growheap1
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)  
        return NULL;                                  //line:vm:mm:growheap2
    place(bp, asize);                                 //line:vm:mm:growheap3
    return bp;
}

/*
 * free
 */
void free (void *bp) {
    /* $end mmfree */
    if (bp == 0) 
        return;

    /* $begin mmfree */
    size_t size = GET_SIZE(HDRP(bp));
    /* $end mmfree */

    if (heap_listp == 0){
        mm_init();
    }
    /* $begin mmfree */

    unsigned int prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    PUT(HDRP(bp), NEW_PACK(size, 0, prev_alloc));
    PUT(FTRP(bp), NEW_PACK(size, 0, prev_alloc));
    coalesce(bp);
}

/*
 * realloc - you may want to look at mm-naive.c
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
 * calloc - you may want to look at mm-naive.c
 * This function is not tested by mdriver, but it is
 * needed to run the traces.
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
 * Return whether the pointer is in the heap.
 * May be useful for debugging.
 */
static int in_heap(const void *p) {
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}

/*
 * Return whether the pointer is aligned.
 * May be useful for debugging.
 */
static int aligned(const void *p) {
    return (size_t)ALIGN(p) == (size_t)p;
}

/*
 * mm_checkheap
 */
void mm_checkheap(int lineno) {
}

static void *coalesce(void *bp) 
{
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    unsigned int size = GET_SIZE(HDRP(bp));
    if (root == NULL) {
        root = bp;
        PUT(PREDP(bp), 0);
        PUT(SUCCP(bp), 0);
    } else if (prev_alloc && next_alloc) { // Case 1
        PUT(PREDP(root), HEAP_OFFSET(bp));
        PUT(SUCCP(bp), HEAP_OFFSET(root));
        PUT(PREDP(bp), 0);
        root = bp;

    } else if (!prev_alloc && next_alloc) { // Case 2 before free, after alloced
        void *prev_bp = PREV_BLKP(bp);
        PUT(SUCCP(PRED_FREE_BLKP(prev_bp)), HEAP_OFFSET(SUCC_FREE_BLKP(prev_bp)));
        PUT(PREDP(SUCC_FREE_BLKP(prev_bp)), HEAP_OFFSET(PRED_FREE_BLKP(prev_bp)));

        size += GET_SIZE(HDRP(prev_bp));
        PUT(HDRP(prev_bp), NEW_PACK(size, 0, 1));
        PUT(FTRP(prev_bp), NEW_PACK(size, 0, 1)); // in fact, no need to store prev_alloc_bit in FTR
        bp = prev_bp;

        // the following part is the same as Case 1
        // should reuse the code
        PUT(PREDP(root), HEAP_OFFSET(bp));
        PUT(SUCCP(bp), HEAP_OFFSET(root));
        PUT(PREDP(bp), 0);
        root = bp;
    } else if (prev_alloc && !next_alloc) { // Case 3 before alloc, after free
        void *next_bp = NEXT_BLKP(bp);
        PUT(SUCCP(PRED_FREE_BLKP(next_bp)), HEAP_OFFSET(SUCC_FREE_BLKP(next_bp)));
        PUT(PREDP(SUCC_FREE_BLKP(next_bp)), HEAP_OFFSET(PRED_FREE_BLKP(next_bp)));

        size += GET_SIZE(HDRP(next_bp));
        PUT(HDRP(bp), NEW_PACK(size, 0, 1));
        PUT(FTRP(bp), NEW_PACK(size, 0, 1));

        PUT(PREDP(root), HEAP_OFFSET(bp));
        PUT(SUCCP(bp), HEAP_OFFSET(root));
        PUT(PREDP(bp), 0);
        root = bp;
    } else { // Case 4 before free, after free
        void *prev_bp = PREV_BLKP(bp);
        void *next_bp = NEXT_BLKP(bp);
        PUT(SUCCP(PRED_FREE_BLKP(prev_bp)), HEAP_OFFSET(SUCC_FREE_BLKP(prev_bp)));
        PUT(PREDP(SUCC_FREE_BLKP(prev_bp)), HEAP_OFFSET(PRED_FREE_BLKP(prev_bp)));
        PUT(SUCCP(PRED_FREE_BLKP(next_bp)), HEAP_OFFSET(SUCC_FREE_BLKP(next_bp)));
        PUT(PREDP(SUCC_FREE_BLKP(next_bp)), HEAP_OFFSET(PRED_FREE_BLKP(next_bp)));

        size += GET_SIZE(HDRP(prev_bp)) + GET_SIZE(HDRP(next_bp));
        bp = prev_bp;
        PUT(HDRP(bp), NEW_PACK(size, 0, 1));
        PUT(FTRP(bp), NEW_PACK(size, 0, 1));

        PUT(PREDP(root), HEAP_OFFSET(bp));
        PUT(SUCCP(bp), HEAP_OFFSET(root));
        PUT(PREDP(bp), 0);
    }
    return bp;
}

// /* 
//  * mm_checkheap - Check the heap for correctness
//  */
// void mm_checkheap(int verbose)  
// { 
//     checkheap(verbose);
// }

/* 
 * The remaining routines are internal helper routines 
 */

/* 
 * extend_heap - Extend heap with free block and return its block pointer
 */
static void *extend_heap(size_t words) 
{
    // the minimum size of a block is 16 bytes (HDR, SUCC, PRED, FTR)
    if (words < 4) {
        words = 4;
    }
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE; //line:vm:mm:beginextend
    if ((long)(bp = mem_sbrk(size)) == -1)  
        return NULL;                                        //line:vm:mm:endextend

    /* Initialize free block header/footer and the epilogue header */
    unsigned int prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    PUT(HDRP(bp), NEW_PACK(size, 0, prev_alloc)); /* Free block header */
    PUT(FTRP(bp), NEW_PACK(size, 0, prev_alloc)); /* Free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), NEW_PACK(0, 1, 0)); /* New epilogue header */
    /* Coalesce if the previous block was free */
    return coalesce(bp);                                          //line:vm:mm:returnblock
}

/* 
 * place - Place block of asize bytes at start of free block bp 
 *         and split if remainder would be at least minimum block size
 */
/* $begin mmplace */
/* $begin mmplace-proto */
static void place(void *bp, size_t asize)
/* $end mmplace-proto */
{
    size_t csize = GET_SIZE(HDRP(bp));   
    unsigned int prev_alloc = GET_PREV_ALLOC(HDRP(bp));

    if ((csize - asize) >= (2*DSIZE)) { 
        PUT(HDRP(bp), NEW_PACK(asize, 1, prev_alloc));
        // The block is used, no need to save a footer
        // PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), NEW_PACK(csize-asize, 0, 1));
        PUT(FTRP(bp), NEW_PACK(csize-asize, 0, 1));
        coalesce(bp);
    }
    else {
        PUT(HDRP(bp), NEW_PACK(csize, 1, prev_alloc));
        // The block is used, no need to save a footer
        // PUT(FTRP(bp), PACK(csize, 1));
    }
}
/* $end mmplace */

/* 
 * find_fit - Find a fit for a block with asize bytes 
 */

static void *find_fit(size_t asize)
{
    if (root == NULL) {
        return NULL;
    }
    /* First-fit search */
    void *bp = root;

    while (true) {
        if (GET_SIZE(HDRP(bp)) >= asize) {
            return bp;
        }
        if (GET(SUCCP(bp))) {
            bp = SUCC_FREE_BLKP(bp);
        } else {
            break;
        }
    }
    return NULL; /* No fit */

}

static void printblock(void *bp) 
{
    size_t hsize, halloc, fsize, falloc;

    checkheap(0);
    hsize = GET_SIZE(HDRP(bp));
    halloc = GET_ALLOC(HDRP(bp));  
    fsize = GET_SIZE(FTRP(bp));
    falloc = GET_ALLOC(FTRP(bp));  

    if (hsize == 0) {
        printf("%p: EOL\n", bp);
        return;
    }

    printf("%p: header: [%ld:%c] footer: [%ld:%c]\n", bp, 
           hsize, (halloc ? 'a' : 'f'), 
           fsize, (falloc ? 'a' : 'f')); 
}

static void checkblock(void *bp) 
{
    if ((size_t)bp % 8)
        printf("Error: %p is not doubleword aligned\n", bp);
    if (GET(HDRP(bp)) != GET(FTRP(bp)))
        printf("Error: header does not match footer\n");
}

/* 
 * checkheap - Minimal check of the heap for consistency 
 */
void checkheap(int verbose) 
{
    char *bp = heap_listp;

    if (verbose)
        printf("Heap (%p):\n", heap_listp);

    if ((GET_SIZE(HDRP(heap_listp)) != DSIZE) || !GET_ALLOC(HDRP(heap_listp)))
        printf("Bad prologue header\n");
    checkblock(heap_listp);

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (verbose) 
            printblock(bp);
        checkblock(bp);
    }

    if (verbose)
        printblock(bp);
    if ((GET_SIZE(HDRP(bp)) != 0) || !(GET_ALLOC(HDRP(bp))))
        printf("Bad epilogue header\n");
}

