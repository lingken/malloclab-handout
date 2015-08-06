#include <stdio.h>

typedef struct Cache_Block {
	int size;
	int timestamp;
	char URN[MAXLINE];
	char Host[MAXLINE];
	char *response;
	Cache_Block *prev;
	Cache_Block *next;
} Cache_Block;

typedef struct Cache {
	int max_size;
	int available_size;
	Cache_Block *root;
} Cache;

void initialize_cache_block(Cache_Block *cache_block, int size, char *response, char *URN, char *Host);
Cache_Block *find_elem(Cache_Block *root, char *URN, char *Host);
void add_elem(Cache_Block *root, Cache_Block *elem);
Cache_Block *delete_elem(Cache_Block *elem);
void refresh(Cache_Block *cache_block);
void free_cache_block(Cache_Block *cache_block);

void initialize_cache(Cache *cache, int maxsize);
Cache_Block *find_least_recent_used(Cache *cache);
char *read_from_cache(Cache *cache, char *URN, char *Host);
void write_to_cache(Cache *cache, int size, char *response, char *URN, char *Host);
void free_cache(Cache *cache);