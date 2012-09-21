#define M61_DISABLE 1
#include "m61.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

unsigned long long active_count;	// # active allocations
unsigned long long active_size;	// # bytes in active allocations
unsigned long long total_count;	// # total allocations
unsigned long long total_size;	// # bytes in total allocations
unsigned long long fail_count;	// # failed allocation attempts
unsigned long long fail_size;	// # bytes in failed alloc attempts

void *m61_malloc(size_t sz, const char *file, int line) {
    (void) file, (void) line;	// avoid uninitialized variable warnings
	metadata *meta_ptr=malloc(sizeof(metadata)+sz);
	if(NULL==meta_ptr){
		++fail_count;
		fail_size+=(unsigned long long)sz;
		return NULL;
	}
	++total_count;
	++active_count;
	total_size+=sz;
	active_size+=(unsigned long long)sz;
	meta_ptr->sz=sz;
	return meta_ptr + 1; 
}

void m61_free(void *ptr, const char *file, int line) {
    (void) file, (void) line;	// avoid uninitialized variable warnings
    if(NULL!=ptr){
	    	--active_count;
		metadata *meta_ptr=ptr;
		meta_ptr-=1;
		active_size-=(unsigned long long)(meta_ptr->sz);
		free(meta_ptr);
	}
}

void *m61_realloc(void *ptr, size_t sz, const char *file, int line) {
    (void) file, (void) line;	// avoid uninitialized variable warnings
    // Your code here.
    return realloc(ptr, sz);
}

void *m61_calloc(size_t nmemb, size_t sz, const char *file, int line) {
    (void) file, (void) line;	// avoid uninitialized variable warnings
    // Your code here.
    return calloc(nmemb, sz);
}

void m61_getstatistics(struct m61_statistics *stats) {
    // Stub: set all statistics to 0
    memset(stats, 0, sizeof(struct m61_statistics));
	stats->total_count=total_count;
	stats->total_size=total_size;
	stats->active_size=active_size;
	stats->active_count=active_count;
	stats->fail_count=fail_count;
	stats->fail_size=fail_size;
}

void m61_printstatistics(void) {
    struct m61_statistics stats;
    m61_getstatistics(&stats);

    printf("malloc count: active %10llu   total %10llu   fail %10llu\n\
malloc size:  active %10llu   total %10llu   fail %10llu\n",
	   stats.active_count, stats.total_count, stats.fail_count,
	   stats.active_size, stats.total_size, stats.fail_size);
}

void m61_printleakreport(void) {
    // Your code here.
}
