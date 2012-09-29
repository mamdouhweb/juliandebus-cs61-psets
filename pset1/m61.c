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

metadata rootMetadata;
metadata *firstAlloc;
metadata *lastAlloc;

void *firstHeap;

metadata *getMetadata(void *ptr){
    metadata *meta_ptr=(metadata *)ptr;
    --meta_ptr;
    return meta_ptr;
}

void *getPayload(metadata *ptr){
    metadata *meta_ptr=ptr;
    ++meta_ptr;
    return meta_ptr;
}

//Returns the maximum size that a variable with type size_t can have in order to be malloced with the structs 'metadata' and 'backpack'
size_t maximumSizeValid(){
    return (size_t)-1-sizeof(metadata)-sizeof(backpack);
}

//Checks whether ptr points to an address that is in the heap
unsigned short int addressIsInHeap(void *ptr){
    if(!firstHeap)firstHeap=(void *)&total_size;
    char a;
    if((void *)&a<ptr)
        return 0;
    if((void *)firstHeap>ptr) 
        return 0;
    return 1;
}

void allocationFailedWithSize(size_t sz){
    ++fail_count;
    fail_size+=(unsigned long long)sz;
}

metadata *scanMemoryForAllocation(void *ptr){
   if(!addressIsInHeap(ptr))
       return NULL;
   metadata *meta_ptr=(metadata *)ptr;
   if(meta_ptr->self==meta_ptr)
      return meta_ptr; 
   else 
      return scanMemoryForAllocation(--ptr);
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
	memset(meta_ptr, 0, sz+sizeof(metadata)+sizeof(backpack));
    if(!firstHeap)
        firstHeap=(void *)meta_ptr;
    
    if(lastAlloc){
        lastAlloc->next=meta_ptr;
        meta_ptr->prv=lastAlloc;
    }
    lastAlloc=meta_ptr;
      
    ++total_count;
	++active_count;
	total_size+=sz;
	active_size+=(unsigned long long)sz;
	//save size and address of metadata struct to metadata
    meta_ptr->sz=sz;
    meta_ptr->self=meta_ptr;
    meta_ptr->previously_freed=0;
    meta_ptr->file=file;
    meta_ptr->line=line;
    //save address of metadata struct to backpack
    //char *start_ptr=(char *)meta_ptr;
    backpack *backpack_ptr=(backpack *)((char *)meta_ptr+sz+sizeof(metadata));
    backpack_ptr->self=backpack_ptr;
    //printf("%p %p\n",meta_ptr+1,backpack_ptr);
	return getPayload(meta_ptr); 
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
    metadata *meta_ptr=getMetadata(ptr);
    if(meta_ptr->previously_freed){
        printf("MEMORY BUG: %s:%i: double free of pointer %p\n",file,line,ptr);
        printf("  %s:%i: pointer %p previously freed here\n",meta_ptr->file,meta_ptr->line,ptr);
        return;
    }
    unsigned short int metadataIsValid=(meta_ptr==meta_ptr->self);
    //construct backpack pointer
    backpack *backpack_ptr=(backpack *)((char *)ptr+meta_ptr->sz);
    unsigned short int backpackIsValid=(backpack_ptr==backpack_ptr->self);
    
    if(!metadataIsValid&&!backpackIsValid){
        printf("MEMORY BUG: %s:%i: invalid free of pointer %p, not allocated\n",file,line,ptr);
        metadata *frontAlloc=scanMemoryForAllocation(meta_ptr);
        uintptr_t offset=(char *)meta_ptr-(char *)frontAlloc;
        if(frontAlloc!=NULL&&offset<sizeof(metadata)+frontAlloc->sz+sizeof(backpack)){
            printf("  %s:%i: %p is %zu bytes inside a %zu byte region allocated here\n",frontAlloc->file,frontAlloc->line,ptr,(char *)meta_ptr-(char *)frontAlloc,frontAlloc->sz);
            return;
        }
        return;
    }
    //this is basically an XOR since 1||1 will be caught by the AND  above
    if(!metadataIsValid||!backpackIsValid){
        printf("MEMORY BUG: %s:%i: detected wild write during free of pointer %p\n",file,line,ptr);
        printf("MEMORY BUG: %s:%i: boundary write error!\n",file,line);
    }
    
   	--active_count;
    size_t sz=meta_ptr->sz;
   	active_size-=(unsigned long long)sz;
    
    meta_ptr->self=NULL;
    backpack_ptr->self=NULL;
    
    meta_ptr->file=file;
    meta_ptr->line=line;
    meta_ptr->previously_freed=1;
    
    metadata *prv=meta_ptr->prv;
    metadata *next=meta_ptr->next;
    if(next==NULL){
        lastAlloc=prv;    
    }
    if(prv!=NULL){
        if(prv->next!=meta_ptr){
            printf("MEMORY BUG%s:%i: invalid free of pointer %p",file,line,ptr);
            return;
        }
        prv->next=next;
        if(next!=NULL){
            next->prv=prv;
        }
        else{
            lastAlloc=prv;
        } 
    }
    else{
        if(next!=NULL){
            next->prv=prv;
        }
        else{
            lastAlloc=NULL;
        }
    }
   	free(meta_ptr);
}

void *m61_realloc(void *ptr, size_t sz, const char *file, int line) {
    (void) file, (void) line;	// avoid uninitialized variable warnings
    void *new_ptr = NULL;
    if (sz != 0)
        new_ptr = m61_malloc(sz,file,line);
    if (ptr != NULL && new_ptr != NULL) {
            metadata *meta_ptr=getMetadata(ptr); 
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

void leakTraverse(metadata *ptr){
    if(ptr)
        printf("LEAK CHECK: %s:%d: allocated object %p with size %zu\n",ptr->file,ptr->line,getPayload(ptr),ptr->sz);
    if(ptr->prv)
        leakTraverse(ptr->prv);
   return; 
}

void m61_printleakreport(void) {
   if(lastAlloc)
      leakTraverse(lastAlloc); 
}
