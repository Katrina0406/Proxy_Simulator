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

#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)

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

void init_cache();
int read_cache(char *url, int fd);
void write_cache(char *url, char *object, int len);
void free_cache();
