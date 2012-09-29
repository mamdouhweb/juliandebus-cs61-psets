#define M61_DISABLE 1
#include "m61.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#define THETA 25 
#define NUMBERCOUNTERS (int)(100/THETA-1)

unsigned long long active_count; // # active allocations
unsigned long long active_size;	 // # bytes in active allocations
unsigned long long total_count;	 // # total allocations
unsigned long long total_size;	 // # bytes in total allocations
unsigned long long fail_count;	 // # failed allocation attempts
unsigned long long fail_size;	 // # bytes in failed alloc attempts

metadata *lastAlloc; //points to the object that was allocated last
void *firstHeap;     //points to the first Address that lives on the heap
//structs to track frequency and size of heavy hitters
hitTracker *szTracker;
hitTracker *freqTracker;

//functions that are not part of the public api
metadata *getMetadata(void *ptr);
void *getPayload(metadata *ptr);
size_t maximumSizeValid();
unsigned short int addressIsInHeap(void *ptr);
void allocationFailedWithSize(size_t sz);
metadata *scanMemoryForAllocation(void *ptr);
void trackAllocByHH(size_t sz, const char *file, int line);
void updateCounters(hitTracker *tracker, int elements, size_t occurrence, const char *file, int line);
void sortHitTracker(hitTracker *tracker, int elements);

//returns the address of the metadata when given a ptr to the ptr passed to the user
metadata *getMetadata(void *ptr){
    metadata *meta_ptr=(metadata *)ptr;
    --meta_ptr;
    return meta_ptr;
}

//returns a pointer to the payload (this pointer is handed out to the user)
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
    if(!firstHeap)
        firstHeap=(void *)&total_size;
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

//recursively scans the memory left of a given pointer and returns a pointer to the adress of the metadata of the found allocation
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
    (void) file, (void) line;   //avoid uninitialized variable warnings
    if(sz>maximumSizeValid()){
        allocationFailedWithSize(sz);
	    return NULL;
    }
	    
    metadata *meta_ptr=malloc(sizeof(metadata)+sz+sizeof(backpack));
	if(meta_ptr==NULL){
        allocationFailedWithSize(sz);
		return NULL;
	}
	
    trackAllocByHH(sz,file,line);   //update HeavyHitterStats
    memset(meta_ptr, 0, sz+sizeof(metadata)+sizeof(backpack));
    if((char *)firstHeap>(char *)meta_ptr||!firstHeap)
        firstHeap=(void *)meta_ptr;
   
    //update links in doubly linked list 
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
    backpack *backpack_ptr=(backpack *)((char *)meta_ptr+sz+sizeof(metadata));
    backpack_ptr->self=backpack_ptr;
	return getPayload(meta_ptr); 
}

void m61_free(void *ptr, const char *file, int line) {
    (void) file, (void) line;    //avoid uninitialized variable warnings
    if(ptr==NULL){
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
    
    backpack *backpack_ptr=(backpack *)((char *)ptr+meta_ptr->sz);    //construct backpack pointer
    unsigned short int backpackIsValid=(backpack_ptr==backpack_ptr->self);
   

    //If neither the metadata nor the backpack is intact, we assume that the pointer was not handed out by m61_malloc() 
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
    //this is basically an XOR since 1||1 will be caught by the AND above
    //If one of metadata or backpack is not intact, we assume that a boundary write occured
    if(!metadataIsValid||!backpackIsValid){
        printf("MEMORY BUG: %s:%i: detected wild write during free of pointer %p\n",file,line,ptr);
        printf("MEMORY BUG: %s:%i: boundary write error!\n",file,line);
    }
    
   	--active_count;
    size_t sz=meta_ptr->sz;
   	active_size-=(unsigned long long)sz;
   
    //make metadata and backpack invalid 
    meta_ptr->self=NULL;
    backpack_ptr->self=NULL;
    
    meta_ptr->file=file;
    meta_ptr->line=line;
    meta_ptr->previously_freed=1;       //this will be checked on each call to m61_free() to catch double frees 

    //Update of the doubly linked list 
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
  //if allocated objects exist, start recursive traversal of the list of Allocations 
  if(lastAlloc)
      leakTraverse(lastAlloc); 
}

void printHeavyHitterReport(void){
    sortHitTracker(szTracker,NUMBERCOUNTERS);
    sortHitTracker(freqTracker,NUMBERCOUNTERS);
    
    unsigned long long freqCounterSum=0;
    unsigned long long szCounterSum=0;
    for(int i=0;i<NUMBERCOUNTERS;++i){
        freqCounterSum+=freqTracker[i].counter;
        szCounterSum+=szTracker[i].counter;   
    }

    printf("---------------Heavy Hitter Report-----------------\n");
    for(int i=0;i<NUMBERCOUNTERS;++i){
        if(freqTracker[i].counter<0.05*total_count) continue;
        unsigned long long count=freqTracker[i].counter+(total_count-freqCounterSum)/(NUMBERCOUNTERS+1);
        printf("HEAVY HITTER: %s:%d: %llu allocations (~%d%%)\n",freqTracker[i].file,freqTracker[i].line,count,(int)(count*100/total_count));
    }
    for(int i=0;i<NUMBERCOUNTERS;++i){
        if(szTracker[i].counter<0.05*total_size) continue;
        unsigned long long count=szTracker[i].counter+(total_size-szCounterSum)/(NUMBERCOUNTERS+1);
        printf("HEAVY HITTER: %s:%d: %llu bytes (~%d%%)\n",szTracker[i].file,szTracker[i].line,count,(int)(count*100/total_size));
    }
    printf("---------------------------------------------------\n");
}

//wrapper function which initializes the memory for the hitTracker structs and passes them to updateCounters
void trackAllocByHH(size_t sz, const char *file, int line){
    if(!szTracker){
        szTracker=malloc(NUMBERCOUNTERS*sizeof(hitTracker));
        freqTracker=malloc(NUMBERCOUNTERS*sizeof(hitTracker));
        memset(szTracker,0,NUMBERCOUNTERS*sizeof(hitTracker));
        memset(szTracker,0,NUMBERCOUNTERS*sizeof(hitTracker));
    }
    updateCounters(szTracker,NUMBERCOUNTERS,sz,file,line); 
    updateCounters(freqTracker,NUMBERCOUNTERS,1,file,line); 
}

//modified implementation of the algorithm "FREQUENT" which doesn't rely on differential encoding
//a pointer to an array of hitTracker structs is passed in along with the occurence which is either 1 (for count) or the size
void updateCounters(hitTracker *tracker, int elements, size_t occurrence, const char *file, int line){
    unsigned long long minimumValue;
    minimumValue=(unsigned long long)-1;    //We keep track of the minimum count in the array in order to prevent an overflow from happening
    unsigned long long subtractValue;
    for(int i=0;i<elements;++i){
        if(tracker[i].counter<minimumValue)
            minimumValue=tracker[i].counter;
        if(tracker[i].file==file&&tracker[i].line==line){
            tracker[i].counter+=occurrence;
            return;
        }
        else if(tracker[i].counter==0){
            tracker[i].file=file;
            tracker[i].line=line;
            tracker[i].counter+=occurrence;
            return;
        }
    }
    if(occurrence>minimumValue)         //occurence>minimumValue would lead to an underflow of (at least) the smallest item in the array
        subtractValue=minimumValue;
    else
        subtractValue=occurrence;
    for(int i=0;i<elements;++i){
        tracker[i].counter-=subtractValue;
    }
    if(occurrence>minimumValue)
       updateCounters(tracker,elements,occurrence-minimumValue,file,line);  //recursive call to make up for the difference of occurence and subtractValue
}


//sort the Array of Counters by count (bubblesort, but the cost should be negligible due to small n(1/theta) and the fact that it's only called twice
void sortHitTracker(hitTracker *tracker, int elements){
    int sorted;
    sorted=0;
    while(!sorted){
        sorted=1;
        for(int i=0;i<elements-1;++i){
            if(tracker[i].counter<tracker[i+1].counter){ 
                hitTracker tempTracker;
                tempTracker=tracker[i];
                tracker[i]=tracker[i+1];
                tracker[i+1]=tempTracker;
                sorted=0;
            }
        }
    }
}
