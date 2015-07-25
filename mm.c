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

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(p) (((size_t)(p) + (ALIGNMENT-1)) & ~0x7)

/* $begin mallocmacros */
/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */ //line:vm:mm:beginconst
#define DSIZE       8       /* Double word size (bytes) */
#define CHUNKSIZE  (1<<9)  /* Extend heap by this amount (bytes) */  //line:vm:mm:endconst 
#define N_SEGLIST   13      /* should be an odd number */

#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc)) //line:vm:mm:pack

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))            //line:vm:mm:get

#ifdef DEBUG
 #define PUT(p, val)  (*(unsigned int *)(p) = (val)); check_access_user_memory(p, __LINE__);    //line:vm:mm:put
#else
 #define PUT(p, val)  (*(unsigned int *)(p) = (val))    //line:vm:mm:put
#endif

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

/*
    My code to check user allocated memory
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
static void add_to_user_mm_array(char *bp, size_t size) {
    if (mm_array_tail == ARRAY_SIZE) {
        printf("too many blocks for array\n");
        exit(1);
    }
    user_mm_array[mm_array_tail].bp = bp;
    user_mm_array[mm_array_tail].size = size;
    mm_array_tail ++;
}
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
static void check_access_user_memory(char *ptr, int lineno) {
    int i = 0;
    for (i = 0; i < mm_array_tail; i ++) {
        if ((ptr >= user_mm_array[i].bp) && (ptr < user_mm_array[i].bp + user_mm_array[i].size)) {
            printf("(%d) Invalid access user allocated memory, ptr: %p\n", lineno, ptr);
        }
    }
}
#endif

/* Global variables */
static char *heap_listp = 0;  /* Pointer to first block */ 
static char *heap_startp = 0;
// static char *root = 0;
static char *tail = 0;

/* Given block ptr bp, compute address of its successor or predecessor
   field which stores the offset of adjacent block in the explicit free list*/
#define SUCCP(bp) ((char *)(bp))
#define PREDP(bp) ((char *)(bp) + WSIZE)

/* Given block ptr bp, compute the block ptr of its successor or predecessor
   in the explicit free list */
#define SUCC_FREE_BLKP(bp)  (heap_startp + GET(SUCCP(bp)))
#define PRED_FREE_BLKP(bp)  (heap_startp + GET(PREDP(bp)))

/* compute the relative offset from a block pointer to first block of heap
   which saves space than storing a real pointer in the block */
// #define HEAP_OFFSET(bp) ((bp) ? (((char *)(bp) - heap_listp)) : (0)) // heap_listp is 0x800000008 after first moving
#define HEAP_OFFSET(bp) ((char *)(bp) - heap_startp)

/* See if the previous block is allocated to eliminate unneccesary footer fields*/
#define GET_PREV_ALLOC(p) ((GET(p) & 0x2) >> 1)
/* pack a size, allocated bit of current block and allocated bit of previous block into a word */
#define NEW_PACK(size, alloc, prev_alloc) ((size) | (alloc) | (prev_alloc << 1))

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
 * Initialize: return -1 on error, 0 on success.
 */
int mm_init(void) {
    dbg_printf("INIT\n");

    /*
        my code to check acess to user allocated memory
    */
    #ifdef DEBUG
    memset(user_mm_array, 0, ARRAY_SIZE * sizeof(user_mm));
    mm_array_tail = 0;
    #endif

    /* Create the initial empty heap */
    if ((heap_startp = mem_sbrk((N_SEGLIST + 5)*WSIZE)) == (void *)-1) //line:vm:mm:begininit
        return -1;

    PUT(heap_startp, 0); /* Address of tail and the SUCC field of tail */
    PUT(heap_startp + (1*WSIZE), 0); /*PRED field of tail*/
    tail = heap_startp;

    int i = 2;
    for (i = 2; i < N_SEGLIST + 2; i ++) { /* each header of seglist, also the SUCC field*/
        PUT(heap_startp + (i*WSIZE), HEAP_OFFSET(tail));
    }
    PUT(heap_startp + ((N_SEGLIST + 2)*WSIZE), NEW_PACK(DSIZE, 1, 1)); /* Prologue header */
    PUT(heap_startp + ((N_SEGLIST + 3)*WSIZE), NEW_PACK(DSIZE, 1, 1)); /* Prologue footer */
    PUT(heap_startp + ((N_SEGLIST + 4)*WSIZE), NEW_PACK(0, 1, 1)); /* Epilogue header */
    // tail = heap_listp + (6*WSIZE);
    heap_listp = heap_startp + (N_SEGLIST + 3)*WSIZE;                     //line:vm:mm:endinit
    // check_list(__LINE__, 1);
    // dbg_printf("root: %p, pred: %x, succ: %p\n", root, GET(PREDP(root)), SUCC_FREE_BLKP(root));
    // dbg_printf("tail: %p, pred: %p, succ: %x\n", tail, PRED_FREE_BLKP(tail), GET(SUCCP(tail)));
    // for (i = 0; i < N_SEGLIST + 5; i ++) {
    //     printf("%p: %x\n", heap_startp + i*WSIZE, *(heap_startp + i*WSIZE));
    // }
    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL){ 
        return -1;
    }
    dbg_checkheap(__LINE__, 0);
    dbg_printf("END INIT\n");
    return 0;
}

/*
 * malloc
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
        dbg_checkheap(__LINE__, 0);
        dbg_printf("END MALLOC (find_fit succeed)\n");
        #ifdef DEBUG
        add_to_user_mm_array(bp, size);
        #endif
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);                 //line:vm:mm:growheap1
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL) { 
        dbg_checkheap(__LINE__, 0);
        dbg_printf("END MALLOC (extend_heap Fails)\n");
        return NULL;                                  //line:vm:mm:growheap2
    }
    place(bp, asize);                                 //line:vm:mm:growheap3
    dbg_checkheap(__LINE__, 0);
    dbg_printf("END MALLOC (extend_heap)\n");
    #ifdef DEBUG
    add_to_user_mm_array(bp, size);
    #endif
    return bp;
}

/*
 * free
 */
void free(void *bp) {
    dbg_printf("FREE\n");
    if (bp == 0) 
        return;

    size_t size = GET_SIZE(HDRP(bp));

    if (heap_listp == 0){
        mm_init();
    }

    #ifdef DEBUG
    remove_from_user_mm_array(bp);
    #endif

    unsigned int prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    PUT(HDRP(bp), NEW_PACK(size, 0, prev_alloc));
    PUT(FTRP(bp), NEW_PACK(size, 0, prev_alloc));

    void *next_bp = NEXT_BLKP(bp);

    // change the prev_alloc bit of next block
    unsigned int block_size = GET_SIZE(HDRP(next_bp));
    unsigned int block_alloced = GET_ALLOC(HDRP(next_bp));
    PUT(HDRP(next_bp), NEW_PACK(block_size, block_alloced, 0));
    if (!block_alloced) {
        // next_block is free, the block can't be epilogue either
        PUT(FTRP(next_bp), NEW_PACK(block_size, block_alloced, 0));
    }
    coalesce(bp);
    dbg_printf("END FREE\n");
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
        return heap_startp + ((N_SEGLIST + 1)*WSIZE);
    } else if (k < 5) {
        return heap_startp + (2*WSIZE);
    }
    return heap_startp + ((k-3)*WSIZE);

}

static void *coalesce(void *bp) 
{
    dbg_printf("COALESCE\n");
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    unsigned int size = GET_SIZE(HDRP(bp));
    
    if (prev_alloc && next_alloc) { // Case 1
        dbg_printf("case 1\n");
    } else if (!prev_alloc && next_alloc) { // Case 2 before free, after alloced
        dbg_printf("case 2\n");
        void *prev_bp = PREV_BLKP(bp);
        PUT(SUCCP(PRED_FREE_BLKP(prev_bp)), HEAP_OFFSET(SUCC_FREE_BLKP(prev_bp)));
        PUT(PREDP(SUCC_FREE_BLKP(prev_bp)), HEAP_OFFSET(PRED_FREE_BLKP(prev_bp)));
        
        size += GET_SIZE(HDRP(prev_bp));
        bp = prev_bp;    
        
    } else if (prev_alloc && !next_alloc) { // Case 3 before alloc, after free
        dbg_printf("case 3\n");
        void *next_bp = NEXT_BLKP(bp);
        PUT(SUCCP(PRED_FREE_BLKP(next_bp)), HEAP_OFFSET(SUCC_FREE_BLKP(next_bp)));
        PUT(PREDP(SUCC_FREE_BLKP(next_bp)), HEAP_OFFSET(PRED_FREE_BLKP(next_bp)));

        size += GET_SIZE(HDRP(next_bp));
    } else { // Case 4 before free, after free
        dbg_printf("case 4\n");
        void *prev_bp = PREV_BLKP(bp);
        void *next_bp = NEXT_BLKP(bp);
        PUT(SUCCP(PRED_FREE_BLKP(prev_bp)), HEAP_OFFSET(SUCC_FREE_BLKP(prev_bp)));
        PUT(PREDP(SUCC_FREE_BLKP(prev_bp)), HEAP_OFFSET(PRED_FREE_BLKP(prev_bp)));
        PUT(SUCCP(PRED_FREE_BLKP(next_bp)), HEAP_OFFSET(SUCC_FREE_BLKP(next_bp)));
        PUT(PREDP(SUCC_FREE_BLKP(next_bp)), HEAP_OFFSET(PRED_FREE_BLKP(next_bp)));

        size += GET_SIZE(HDRP(prev_bp)) + GET_SIZE(HDRP(next_bp));
        bp = prev_bp;
    }
    PUT(HDRP(bp), NEW_PACK(size, 0, 1));
    PUT(FTRP(bp), NEW_PACK(size, 0, 1));

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
 * The remaining routines are internal helper routines 
 */

/* 
 * extend_heap - Extend heap with free block and return its block pointer
 */
static void *extend_heap(size_t words) 
{
    dbg_printf("EXTEND_HEAP\n");
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
    // printf("bp: %p, HDRP: %p, header: %x, prev_alloc: %d\n", bp, HDRP(bp), GET(HDRP(bp)), prev_alloc);
    // printf("HDRP(%p): %p\n", bp, HDRP(bp));
    PUT(HDRP(bp), NEW_PACK(size, 0, prev_alloc)); /* Free block header and overwrite the original tail*/    
    PUT(FTRP(bp), NEW_PACK(size, 0, prev_alloc)); /* Free block footer */
    
    char *epi = NEXT_BLKP(bp);
    PUT(HDRP(epi), NEW_PACK(0, 1, 0)); /* New epilogue header */
    /* Coalesce if the previous block was free */
    void *rt = coalesce(bp);
    dbg_printf("END EXTEND_HEAP\n");
    return rt;                                          //line:vm:mm:returnblock
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
        dbg_printf("Case: (csize - asize) >= (2*DSIZE)\n");
        PUT(HDRP(bp), NEW_PACK(asize, 1, prev_alloc));
        // The block is used, no need to save a footer
        PUT(SUCCP(PRED_FREE_BLKP(bp)), HEAP_OFFSET(SUCC_FREE_BLKP(bp)));
        PUT(PREDP(SUCC_FREE_BLKP(bp)), HEAP_OFFSET(PRED_FREE_BLKP(bp)));

        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), NEW_PACK(csize-asize, 0, 1));
        PUT(FTRP(bp), NEW_PACK(csize-asize, 0, 1));
        coalesce(bp);
    }
    else {
        dbg_printf("Case: (csize - asize) < (2*DSIZE)\n");
        PUT(HDRP(bp), NEW_PACK(csize, 1, prev_alloc));
        // The block is used, no footer
        PUT(SUCCP(PRED_FREE_BLKP(bp)), HEAP_OFFSET(SUCC_FREE_BLKP(bp)));
        PUT(PREDP(SUCC_FREE_BLKP(bp)), HEAP_OFFSET(PRED_FREE_BLKP(bp)));

        bp = NEXT_BLKP(bp);
        unsigned int block_size = GET_SIZE(HDRP(bp));
        unsigned int block_alloced = GET_ALLOC(HDRP(bp));
        PUT(HDRP(bp), NEW_PACK(block_size, block_alloced, 1));
        if (!block_alloced) {
            // block is free, the block can't be epilogue either
            PUT(FTRP(bp), NEW_PACK(block_size, block_alloced, 1));
        }
    }
}

/* $end mmplace */

/* 
 * find_fit - Find a fit for a block with asize bytes 
 */

static void *find_fit(size_t asize)
{
    /* First-fit search */
    char *root = get_root(asize);
    while (root != (heap_startp + ((N_SEGLIST + 2)*WSIZE))){
        void *bp = SUCC_FREE_BLKP(root);
        while (bp != tail) {
            if (GET_SIZE(HDRP(bp)) >= asize) {
                return bp;
            }
            bp = SUCC_FREE_BLKP(bp);
        }
        root = root + WSIZE;
    }
    return NULL; /* No fit */

    // // Best-fit in a level
    // void *rt = NULL;
    // unsigned int min_size = -1;
    // unsigned int block_size = 0;
    // char *root = get_root(asize);
    // while (root != (heap_startp + ((N_SEGLIST + 2)*WSIZE))){
    //     void *bp = SUCC_FREE_BLKP(root);
    //     while (bp != tail) {
    //         block_size = GET_SIZE(HDRP(bp));
    //         if (block_size >= asize && block_size < min_size) {
    //             rt = bp;
    //             min_size = block_size;
    //         }
    //         bp = SUCC_FREE_BLKP(bp);
    //     }
    //     if (rt) {
    //         break;
    //     }
    //     root = root + WSIZE;
    // }
    // return rt;
}

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
        printf("%p: EOL   :[%ld:%ld:%ld]\n", bp, hsize, halloc, hprevalloc);
        return;
    }
 
    if (halloc) {
        printf("%p: header: [%ld:%ld:%ld]\n", bp, hsize, halloc, hprevalloc);
    } else {
        printf("%p: header: [%ld:%ld:%ld] footer: [%ld:%ld:%ld] PRED: %x, SUCC: %x\n",
            bp,
            hsize, halloc, hprevalloc, fsize, falloc, fprevalloc,
            GET(PREDP(bp)), GET(SUCCP(bp)));
    }
}

/*
 * mm_checkheap
 */
void mm_checkheap(int lineno) {
    checkheap(lineno, 1);
}

void checkheap(int lineno, int verbose) {
    if (verbose){
        if (lineno == 1) {
            printf("============================================\n");
        } else {
            printf("********My Call*********\n");
        }
    }

    /* check prologue block */
    if (verbose)
        printf("Heap (%p):\n", heap_listp);
    if ((GET_SIZE(HDRP(heap_listp)) != 2*WSIZE) || !GET_ALLOC(HDRP(heap_listp)))
        printf("(%d) Bad prologue header\n", lineno);
    if (GET(HDRP(heap_listp)) != GET(FTRP(heap_listp))) {
        printf("(%d) Bad prologue header does not match footer\n\n", lineno);
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

    // checkblock(heap_listp, lineno);
    size_t free_blocks = 0;
    size_t prev_alloc = 1;
    char *bp;
    for (bp = NEXT_BLKP(heap_listp); GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (verbose) 
            printblock(bp);
        checkblock(bp, lineno);
        if (GET_PREV_ALLOC(HDRP(bp)) != prev_alloc) {
            printf("(%d) Error: %p prev_alloc bit: %d, alloc_bit of prev blk: %zu\n", lineno, bp, GET_PREV_ALLOC(HDRP(bp)), prev_alloc);
        }
        if (prev_alloc == 0 && GET_ALLOC(HDRP(bp)) == 0) {
            printf("(%d) Error: %p two consecutive free blocks in heap\n", lineno, bp);
        }
        if (!GET_ALLOC(HDRP(bp))) {
            free_blocks ++;
        }
        prev_alloc = GET_ALLOC(HDRP(bp));
    }

    /* check epilogue block */
    if (verbose)
        printblock(bp);
    if ((GET_SIZE(HDRP(bp)) != 0) || !(GET_ALLOC(HDRP(bp))))
        printf("(%d) Bad epilogue header\n", lineno);

    size_t free_blocks_in_list = check_list(lineno, verbose);
    if (free_blocks != free_blocks_in_list) {
        printf("(%d) free blocks: %ld, free blocks in list: %ld\n", lineno, free_blocks, free_blocks_in_list);
    }
}

/* for all blocks except for epilogue block */
static void checkblock(void *bp, int lineno) 
{
    /* check block's alignment */
    if (!aligned(bp))
        printf("(%d) Error: %p is not doubleword aligned\n", lineno, bp);
    // if (GET(HDRP(bp)) != GET(FTRP(bp)))
        // printf("Error: header does not match footer\n");
    if ((bp != heap_listp) && GET_SIZE(HDRP(bp)) < 4 * WSIZE) {
        printf("(%d) Error: %p block size is smaller than minimum size\n", lineno, bp);
    }
    if (!GET_ALLOC(HDRP(bp))) {
        // this is a free block, has its footer
        if (GET(HDRP(bp)) != GET(FTRP(bp))){
            printf("(%d) Error: header does not match footer\n", lineno);
        }
    }
}

static size_t check_list(int lineno, int verbose) {
    if (verbose) {
        printf("(%d) Segregated list:\n", lineno);
    }
    size_t free_blocks_in_list = 0;

    int i = 0;
    for (i = 0; i < N_SEGLIST; i ++) {
        void *root = heap_startp + ((i + 2)*WSIZE);
        unsigned int level_size = (1 << (4+i));
        printf("root (size %u): %p\n", level_size, root);

        void *ptr = SUCC_FREE_BLKP(root);
        while (ptr != tail) {
            free_blocks_in_list ++;
            if (verbose) {
                printblock(ptr);
            }
            if (!in_heap(ptr)) {
                printf("(%d) %p out of heap\n", lineno, ptr);
            }

            if (SUCC_FREE_BLKP(PRED_FREE_BLKP(ptr)) != ptr) {
                printf("(%d) %p inconsistent ptr->pred->succ\n", lineno, ptr);
            }
            if ((SUCC_FREE_BLKP(ptr) != tail) && PRED_FREE_BLKP(SUCC_FREE_BLKP(ptr)) != ptr) {
                printf("(%d) %p inconsistent ptr->succ->pred\n", lineno, ptr);
            }
            unsigned int block_size = GET_SIZE(HDRP(ptr));
            if (i != N_SEGLIST - 1) {
                if (block_size < level_size || block_size >= 2 * level_size) {
                    printf("(%d) %p with size of %u in the wrong list %u\n", lineno, ptr, block_size, level_size);
                }
            } else {
                if (block_size < level_size) {
                    printf("(%d) %p with size of %u in the wrong list %u\n", lineno, ptr, block_size, level_size);
                }
            }
            ptr = SUCC_FREE_BLKP(ptr);
        }
    }
    

    if (verbose) {
        printf("all tail: %p, pred: %p\n", tail, PRED_FREE_BLKP(tail));
        printf("(%d) END check list\n", lineno);
    }

    return free_blocks_in_list;
}