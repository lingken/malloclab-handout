/*
       Name: Ken Ling
  Andrew ID: kling1

  A cache based on linked list for the web proxy.
*/
#include <string.h>
#include <time.h>
#include <limits.h>
#include "cache.h"
#include "csapp.h"
/* Operations for linked list */
/*
    Find the element in linked list according to URN and Host.
    Return NULL if the element is not found.
*/
Cache_Block *find_elem(Cache_Block *root, char *URN, char *Host) {
    Cache_Block *ptr = root->next;
    while (ptr) {
        if ((strcmp(ptr->Host, Host) == 0) && 
            (strcmp(ptr->URN, URN) == 0)) {
            break;
        }
        ptr = ptr->next;
    }
    return ptr;
}
/* Add a new element block in linked list */
void add_elem(Cache_Block *root, Cache_Block *elem) {
    elem->next = root->next;
    elem->prev = root;
    if (root->next) {
        root->next->prev = elem;
    }
    root->next = elem;
}
/* Delete an element in linked list and return the pointer of it */
Cache_Block *delete_elem(Cache_Block *elem) {
    elem->prev->next = elem->next;
    if (elem->next) {
        elem->next->prev = elem->prev;
    }
    return elem;
}
/* Initialize the parameters of a cache block */
void initialize_cache_block(Cache_Block *cache_block, int size, char *response, char *URN, char *Host) {
    sem_init(&cache_block->mutex, 0, 1);
    cache_block->size = size;
    strncpy(cache_block->URN, URN, MAXLINE);
    strncpy(cache_block->Host, Host, MAXLINE);
    refresh(cache_block);
    char *content = malloc(size);
    memset(content, 0, size);
    memcpy(content, response, size);
    cache_block->response = content;
}
/* Update the timestamp of a block when being read or written */
void refresh(Cache_Block *cache_block) {
    P(&cache_block->mutex);
    cache_block->timestamp = time(NULL);
    V(&cache_block->mutex);
}
/* Free the content and cache block itself */
void free_cache_block(Cache_Block *cache_block) {
    if (cache_block) {
        if (cache_block->response) {
            free(cache_block->response);
        }
        free(cache_block);
    }
}

/* Initialize the parameters of a cache */
void initialize_cache(Cache *cache, int maxsize) {
    sem_init(&cache->mutex, 0, 1);
    sem_init(&cache->w, 0, 1);
    cache->readers = 0;

    cache->max_size = maxsize;
    cache->available_size = maxsize;
    cache->root = malloc(sizeof(Cache_Block));
    memset(cache->root, 0, sizeof(Cache_Block));
}
/*
    Return the least recent used block.
    Return NULL if the cache is empty.
*/
Cache_Block *find_least_recent_used(Cache *cache) {
    int tmp = INT_MAX;
    Cache_Block *rt = NULL;
    Cache_Block *ptr = cache->root->next;
    while (ptr) {
        if (ptr->timestamp > tmp) {
            tmp = ptr->timestamp;
            rt = ptr;
        }
        ptr = ptr->next;
    }
    return rt;
}
/* 
    Find and return a copy of content in cache according to URN and Host.
    Return the copy will avoid race conditions. 
    Return NULL if the content is not cached
*/
char *read_from_cache(Cache *cache, char *URN, char *Host) {
    P(&cache->mutex);
    cache->readers ++;
    if (cache->readers == 1) {
        P(&cache->w);
    }
    V(&cache->mutex);

    char *response = NULL;
    Cache_Block *ptr = find_elem(cache->root, URN, Host);
    if (ptr) { // the content is cached
        response = malloc(ptr->size);
        memset(response, 0, ptr->size);
        // response = ptr->response;
        memcpy(response, ptr->response, ptr->size);
        refresh(ptr);
    }

    P(&cache->mutex);
    cache->readers --;
    if (cache->readers == 0) {
        V(&cache->w);
    }
    V(&cache->mutex);
    return response;
}
/*
    Add a new cache block into cache.
    Replace least recent used blocks with the new one
    if space is not enough.
*/
void write_to_cache(Cache *cache, int size, char *response, char *URN, char *Host) {
    P(&cache->w);

    if (size > cache->max_size) { // Content is too large to be cached
        return;
    }
    while (size < cache->available_size) {
        // Replace some old cache blocks
        Cache_Block *old_block = find_least_recent_used(cache);
        old_block = delete_elem(old_block);
        cache->available_size += old_block->size;
        free_cache_block(old_block);
    }
    Cache_Block *new_block = malloc(sizeof(Cache_Block));
    memset(new_block, 0, sizeof(Cache_Block));
    initialize_cache_block(new_block, size, response, URN, Host);
    add_elem(cache->root, new_block);
    cache->available_size -= size;
    
    V(&cache->w);
}

void free_cache(Cache *cache) {
    if (cache) {
        if (cache->root) {
            while (cache->root->next) {
                Cache_Block *ptr = delete_elem(cache->root->next);
                free_cache_block(ptr);
            }
            free(cache->root);
        }
        free(cache);
    }
}
