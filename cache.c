/*
 * cache implementation
 * use a linked list to add cache blocks and two pointers (cache_start &
 * cache_end) to record the start and end of the linked list implement a FIFO
 * rule refcnt is used to guanrantee that cache blocks will not be evicted while
 * transmitting
 *
 * @author Yuqiao Hu
 * @andrew id: yuqiaohu
 */

#include "csapp.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>

/*
 * Max cache and object sizes
 */
#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)

int current_cache_size = 0;

typedef struct cache cache_block;
struct cache {
    char url[MAX_OBJECT_SIZE];
    char data[MAX_OBJECT_SIZE];
    int size;
    int count;
    int refcnt;
    long int time;
    cache_block *next;
};

// global variable cache
cache_block *cache_start;
cache_block *cache_end;

// get the current running time
long int get_time() {
    clock_t currtime = clock();
    return (long int)currtime;
}

void init_cache() {
    cache_block *cache = malloc(sizeof(cache_block));
    memcpy(cache->url, "", 1);
    memcpy(cache->data, "", 1);
    cache->size = 0;
    cache->count = 0;
    cache->refcnt = 0;
    cache->next = NULL;
    cache->time = 0;
    cache_start = cache;
    cache_end = cache;
}

// read into cache to see if there's any url key that matches the given url
// return 0 if not found
// return 1 if found and successfully extracted the web object
int read_cache(char *url, int fd) {
    cache_block *start = cache_start;
    printf("target url: %s\n", url);
    while (start) {
        printf("loop through url: %s\n", start->url);
        if (!strcmp(start->url, url)) {
            // current url has a previous storage
            start->count += 1;
            start->refcnt += 1;
            rio_writen(fd, start->data, start->size);
            start->refcnt -= 1;
            start->time = get_time();
            printf("read from memory\n");
            return 1;
        }
        start = start->next;
    }

    printf("no previous object\n");
    return 0;
}

// evict the least recently used block from the cache
void evict_one() {
    int least_count = cache_start->count;
    long int least_time = cache_start->time;
    cache_block *start = cache_start->next;
    cache_block *eviction_block = cache_start;
    while (start) {
        if (start->count <= least_count && start->time <= least_time) {
            least_count = start->count;
            least_time = start->time;
            eviction_block = start;
        }
        start = start->next;
    }
    cache_block *tmp = cache_start;
    // evict cache_start block
    if (tmp == eviction_block) {
        if (cache_end == cache_start) {
            cache_end = cache_start->next;
        }
        cache_start = cache_start->next;
        current_cache_size -= tmp->size;
        tmp->refcnt -= 1;
        while (tmp->refcnt) {
            continue;
        }
        free(tmp);
        return;
    }
    while (tmp && (tmp->next != eviction_block)) {
        tmp = tmp->next;
    }
    eviction_block->refcnt -= 1;
    while (eviction_block->refcnt) {
        continue;
    }
    tmp->next = eviction_block->next;
    if (eviction_block == cache_end) {
        cache_end = tmp;
    }
    current_cache_size -= eviction_block->size;
    free(eviction_block);
}

// write the web object into cache
void write_one(cache_block *cache, char *url, char *object, int len) {
    memcpy(cache->url, url, MAXLINE);
    memcpy(cache->data, object, len);
    cache->size = len;
    cache->count = 1;
    cache->time = get_time();
    cache->refcnt = 1;
    current_cache_size += len;
    cache_end->next = cache;
    cache->next = NULL;
    cache_end = cache;
}

bool in_cache(char *url, char *object) {
    cache_block *start = cache_start;
    while (start) {
        if (!strcmp(start->url, url)) {
            return true;
        }
        start = start->next;
    }
    return false;
}

// write the web object into cache with url as the key
void write_cache(char *url, char *object, int len) {
    cache_block *cache;
    if (in_cache(url, object))
        return;
    if (current_cache_size + len <= MAX_CACHE_SIZE) {
        if (cache_start->next == NULL && (!strcmp(cache_start->url, ""))) {
            cache = cache_start;
            printf("first try\n");
        } else {
            cache = malloc(sizeof(cache_block));
        }
        write_one(cache, url, object, len);
        printf("----write cache %s with size %d\n", url, len);
        return;
    }
    // exceeds max size, need eviction
    while (current_cache_size + len > MAX_CACHE_SIZE) {
        evict_one();
    }
    cache = malloc(sizeof(cache_block));
    write_one(cache, url, object, len);
    printf("evict and write cache\n");
}

// free all cache objects
void free_cache() {
    cache_block *start = cache_start;
    while (start) {
        start = start->next;
        free(start);
    }
}