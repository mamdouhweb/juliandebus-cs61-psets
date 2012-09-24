#define M61_DISABLE 1
#include "m61.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

//REMOVE LATER!!
#include <assert.h>

unsigned long long active_count;	// # active allocations
unsigned long long active_size;	// # bytes in active allocations
unsigned long long total_count;	// # total allocations
unsigned long long total_size;	// # bytes in total allocations
unsigned long long fail_count;	// # failed allocation attempts
unsigned long long fail_size;	// # bytes in failed alloc attempts

metadata *getMetadata(void *ptr){
    metadata *meta_ptr=ptr;
    meta_ptr-=1;
    return meta_ptr;    
}

//Returns the maximum size that a variable with type size_t can have in order to be malloced with the struct 'metadata'
size_t maximumSizeValid(){
    return (size_t)-1-sizeof(metadata);
}

//Checks whether ptr points to an address that is in the heap
unsigned short int addressIsInHeap(void *ptr){
    char a;
    if((void *)&a<ptr)
        return 0;
    if((void *)&active_size>ptr)
        return 0;
    return 1;
}

metadata *addressOfMetadata(void *ptr){
    metadata *meta_ptr=ptr;
    return meta_ptr-=1;
}

void allocationFailedWithSize(size_t sz){
    ++fail_count;
    fail_size+=(unsigned long long)sz;
}

void *m61_malloc(size_t sz, const char *file, int line) {
    (void) file, (void) line;	// avoid uninitialized variable warnings
	//Is this acceptable? Alternative? 
    if(sz>maximumSizeValid()){
        allocationFailedWithSize(sz);
	    return NULL;
    }
	    
    metadata *meta_ptr=malloc(sizeof(metadata)+sz+sizeof(backpack));
	if(meta_ptr==NULL){
        allocationFailedWithSize(sz);
		return NULL;
	}
	++total_count;
	++active_count;
	total_size+=sz;
	active_size+=(unsigned long long)sz;
	//save size and address of metadata struct to metadata
    meta_ptr->sz=sz;
    meta_ptr->sz_ptr=(uintptr_t)meta_ptr;
    meta_ptr->previously_freed=0;
    //save address of metadata struct to backpack
    char *start_ptr=(char *)meta_ptr;
    backpack *backpack_ptr=(backpack *)(start_ptr+sz+sizeof(metadata));
    backpack_ptr->sz_ptr=(uintptr_t)meta_ptr;
    //printf("%p %p\n",meta_ptr+1,backpack_ptr);
	return meta_ptr + 1; 
}

void m61_free(void *ptr, const char *file, int line) {
    (void) file, (void) line;	// avoid uninitialized variable warnings
    if(NULL==ptr){
        return;   
    }
    if(!addressIsInHeap(ptr)){
        printf("MEMORY BUG: %s:%i: invalid free of pointer %p, not in heap\n",file,line,ptr);
        return;
    }
    metadata *meta_ptr=addressOfMetadata(ptr);
    if(meta_ptr->previously_freed){
        printf("MEMORY BUG: %s:%i: double free of pointer %p\n",file,line,ptr);
        return;
    }
    unsigned short int metadataIsValid=(meta_ptr==(metadata *)meta_ptr->sz_ptr);
    //construct backpack pointer
    char *start_ptr=(char *)ptr;
    backpack *backpack_ptr=(backpack *)(start_ptr+meta_ptr->sz);
    unsigned short int backpackIsValid=(meta_ptr==(metadata *)backpack_ptr->sz_ptr);
    
    if(!metadataIsValid&&!backpackIsValid){
        printf("MEMORY BUG: %s:%i: invalid free of pointer %p, not allocated\n",file,line,ptr);
        return;
    }
    //if((!backpackIsValid&&metadataIsValid)||(backpackIsValid&&!metadataIsValid)){
    //    printf("MEMORY BUG: %s:%i: boundary write error!\n",file,line);
    //}
    
   	--active_count;
    size_t sz=meta_ptr->sz;
   	active_size-=(unsigned long long)sz;
    
    //double free: neither metadata nor backpack point to metadata (everything's been freed) 
    //invalid free: to be calculated: max=max(stack) min=global?
    //assert(metadataValid);
    //boundary write either metadata or backpack do not point to metadata
    meta_ptr->sz_ptr=0;
    backpack_ptr->sz_ptr=0;
   	free(meta_ptr);
    meta_ptr->previously_freed=1;
}

void *m61_realloc(void *ptr, size_t sz, const char *file, int line) {
    (void) file, (void) line;	// avoid uninitialized variable warnings
    void *new_ptr = NULL;
    if (sz != 0)
        new_ptr = m61_malloc(sz,file,line);
    if (ptr != NULL && new_ptr != NULL) {
            metadata *meta_ptr=addressOfMetadata(ptr); 
            size_t old_sz = meta_ptr->sz;
            if (old_sz < sz)
             memcpy(new_ptr, ptr, old_sz);
        else
             memcpy(new_ptr, ptr, sz);
    }
    m61_free(ptr,file, line);
    return new_ptr;
}

void *m61_calloc(size_t nmemb, size_t sz, const char *file, int line) {
    (void) file, (void) line;	// avoid uninitialized variable warnings
    if (sz>maximumSizeValid()/nmemb){
        ++fail_count;
	fail_size+=(unsigned long long)sz;
        return NULL;
    }
    void *ptr = m61_malloc(sz * nmemb, file, line);
    if (ptr != NULL)
	memset(ptr, 0, sz * nmemb);     // clear memory to 0
    return ptr;
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
