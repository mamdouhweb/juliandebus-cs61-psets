#include "io61.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>

#define PAGESIZE (4<<10)
#define NCACHES 10


// io61_file
//    Data structure for io61 file wrappers. Add your own stuff.

typedef enum cache_status{
    CACHE_EMPTY   = 0,
    CACHE_ACTIVE  = 1
}cache_state;

typedef struct io61_cache {
    char *buf;
    cache_state state;
    size_t pos;
    size_t offset;
    size_t bufsize;
    int lifetime;
}io61_cache;

struct io61_file {
    int fd;
    int mode;
    // size of the file represented by fd
    ssize_t filesize;
    // Caches associated with file represented by fd
    // Interaction should regularly happen through these functions: 
    //    * freeCache(io61_file *f)
    //    * getCurrentCache(io61_file *f)
    //    * buildCacheForPos(io61_file *f, size_t pos)
    //    * getCacheForPos(io61_file *f, size_t pos)
    io61_cache caches[NCACHES];
    // index into caches array, -1 if no current cache present
    int currentCache;
    // holds cache/mmaped file
    char *buf;
    // offset of passed out/read in bytes within *buf
    ssize_t offset;
    // size of the memory region buf points to
    ssize_t bufsize;
};

// freeCache(io61_file *f) -- returns a pointer to either a free cache element 
//      or the oldest used cache
io61_cache *freeCache(io61_file *f) {
    int oldestCache=0;
    int maxLifetime = -1;
    for (int i=0;i<NCACHES;++i) {
        io61_cache *currentCache=&f->caches[i];
        if (currentCache->state==CACHE_EMPTY)
            return currentCache;
        if (currentCache->lifetime>maxLifetime) {
            oldestCache=i;
            maxLifetime=currentCache->lifetime;
        }
    }
    return &f->caches[oldestCache];
}

// buildCacheForPos(io61_file *f, size_t pos) -- creates a io61_cache element
//      and inserts it into the array. Returns a pointer to the created io61_cache element or
//      NULL on error(EOF). The least recently used element may be evicted. 
io61_cache *buildCacheForPos(io61_file *f, size_t pos) {
    io61_cache *newCache=freeCache(f);
    if (newCache->state==CACHE_EMPTY)
        newCache->buf=malloc(PAGESIZE);
    ssize_t readchars;
    if (f->filesize==-1) {
        // The file is not seekable, just read sequentially
        readchars=read(f->fd, newCache->buf, PAGESIZE);
    }
    else {
        // Here we can mmap the file rather than reading it!
        // pread(int d, void *buf, size_t nbyte, off_t offset);
        readchars=pread(f->fd, newCache->buf, PAGESIZE, (off_t)pos);
    }
    if (readchars==0) {
        // if nothing can be read into the buffer, return -1
        newCache->state=CACHE_EMPTY;
        free(newCache->buf);
        return NULL;
    }
    newCache->bufsize=readchars;
    newCache->pos=pos;
    newCache->offset=0;
    newCache->lifetime=0;
    newCache->state=CACHE_ACTIVE;
    f->currentCache=newCache-f->caches;
    return newCache;
}

// getCacheForPos(io61_file *f, size_t pos) -- returns a pointer to io61_cache
//      element on success (a cache currently holds the memory at pos)
//      and sets the found cache to be the current cache, else NULL
io61_cache *getCacheForPos(io61_file *f, size_t pos) {
    io61_cache *foundCache=NULL;
    for (int i=0;i<NCACHES;++i) {
        io61_cache *currentCache=&f->caches[i];
        if ((!foundCache && currentCache->state == CACHE_ACTIVE)
                &&
           (currentCache->pos<=pos && currentCache->pos+currentCache->bufsize>pos)
           ){
            f->currentCache=i;
            foundCache=currentCache;
            --foundCache->lifetime;
        }
        ++currentCache->lifetime;
    }
    return foundCache;
}

io61_cache *getCurrentCache(io61_file *f) {
    if (f->currentCache==-1)
        return NULL;
    return &f->caches[f->currentCache]; 
}

// io61_fdopen(fd, mode)
//    Return a new io61_file that reads from and/or writes to the given
//    file descriptor `fd`. `mode` is either O_RDONLY for a read-only file
//    or O_WRONLY for a write-only file. You need not support read/write
//    files.

io61_file *io61_fdopen(int fd, int mode) {
    assert(fd >= 0);
    io61_file *f = (io61_file *) malloc(sizeof(io61_file));
    f->fd = fd;
    f->bufsize=0;
    f->mode=mode;
    f->currentCache=-1;
    f->filesize=io61_filesize(f);
    return f;
}


// io61_close(f)
//    Close the io61_file `f`.

int io61_close(io61_file *f) {
    io61_flush(f);
    int r = close(f->fd);
    f->bufsize=0;
    for (int i=0;i<NCACHES;++i) {
        io61_cache *currentCache=&f->caches[i];
        if (currentCache->state) {
            free(currentCache->buf);
            currentCache->state=CACHE_EMPTY;
        }
    }
    free(f);
    return r;
}


// io61_readc(f)
//    Read a single (unsigned) character from `f` and return it. Returns EOF
//    (which is -1) on error or end-of-file.

int io61_readc(io61_file *f) {
    // initialize the buffer
    if (getCurrentCache(f)==0) {
        // this sets the current cache
        if(buildCacheForPos(f,0)==NULL)
            return EOF;
    }
    io61_cache *currentCache=getCurrentCache(f);
    // The current buffer can still satisfy the request
    if (currentCache->offset<currentCache->bufsize) {
        char returnChar=currentCache->buf[currentCache->offset];
        ++currentCache->offset;
        return returnChar;
    }
    else {
        // Request a new cache that covers the next bytes
        // Here we could give the OS prefetching advice
        if(buildCacheForPos(f,currentCache->pos+currentCache->offset)==NULL)
            return EOF;
        return io61_readc(f);
    }
}


// io61_writec(f)
//    Write a single character `ch` to `f`. Returns 0 on success or
//    -1 on error.

int io61_writec(io61_file *f, int ch) {
    if (getCurrentCache(f)==0) {
        f->currentCache=0;
        f->caches[0].buf=malloc(PAGESIZE);
        f->caches[0].bufsize=PAGESIZE;
        f->caches[0].offset=0;
        f->caches[0].state=CACHE_ACTIVE;
    }
    io61_cache *currentCache=getCurrentCache(f);
    // fill the buffer
    if (currentCache->offset<currentCache->bufsize) {
        currentCache->buf[currentCache->offset] = ch; 
        ++currentCache->offset;
        return 0;
    }
    // flush the buffer
    else {
        io61_flush(f);
        return io61_writec(f, ch);
    }
}


// io61_flush(f)
//    Forces a write of any `f` buffers that contain data.

int io61_flush(io61_file *f) {
    if (f->mode!=O_WRONLY)
        return 0;
    for (int i=0;i<NCACHES;++i) {
        io61_cache *currentCache=&f->caches[i];
        if (currentCache->offset>0) {
            write(f->fd, currentCache->buf, currentCache->offset);
            currentCache->offset=0;
        }
    }
    return 0;
}


// io61_read(f, buf, sz)
//    Read up to `sz` characters from `f` into `buf`. Returns the number of
//    characters read on success; normally this is `sz`. Returns a short
//    count if the file ended before `sz` characters could be read. Returns
//    -1 an error occurred before any characters were read.

ssize_t io61_read(io61_file *f, char *buf, size_t sz) {
    // initialize the buffer
    if (getCurrentCache(f)==0) {
        // this sets the current cache
        if(buildCacheForPos(f,0)==NULL)
            return 0;
    }
    io61_cache *currentCache=getCurrentCache(f);
    // The current buffer can still satisfy the request
    if (currentCache->offset<currentCache->bufsize) {
        size_t nread = 0;
        size_t cycleRead = 0;
        size_t charsLeft = 0;
        while (nread != sz) {
            charsLeft = currentCache->bufsize-currentCache->offset;
            cycleRead = (sz-nread)>charsLeft?charsLeft:(sz-nread);
            memcpy(&buf[nread],&currentCache->buf[currentCache->offset],cycleRead); 
            nread+=cycleRead;
            currentCache->offset+=cycleRead;
            if (currentCache->offset==currentCache->bufsize){
                if(buildCacheForPos(f,currentCache->pos+currentCache->offset)==NULL)
                    return nread;
            }
            assert(nread<=sz);
        }
        return nread;
    }
    else {
        // Request a new cache that covers the next bytes
        // Here we could give the OS prefetching advice
        if(buildCacheForPos(f,currentCache->pos+currentCache->offset)==NULL)
            return 0;
        return io61_read(f, buf, sz);
    }
}


// io61_write(f, buf, sz)
//    Write `sz` characters from `buf` to `f`. Returns the number of
//    characters written on success; normally this is `sz`. Returns -1 if
//    an error occurred before any characters were written.

ssize_t io61_write(io61_file *f, const char *buf, size_t sz) {
    if (getCurrentCache(f)==NULL) {
        f->currentCache=0;
        f->caches[0].buf=malloc(10*PAGESIZE);
        f->caches[0].bufsize=10*PAGESIZE;
        f->caches[0].offset=0;
        f->caches[0].state=CACHE_ACTIVE;
    }
    io61_cache *currentCache=getCurrentCache(f);
    // fill the buffer
    if (currentCache->offset<currentCache->bufsize) {
        size_t nwritten = 0;
        size_t cycleWrite = 0;
        size_t charsLeft = 0;
        while (nwritten!=sz) {
            charsLeft=currentCache->bufsize-currentCache->offset;
            cycleWrite = (sz-nwritten)>charsLeft?charsLeft:(sz-nwritten);
            memcpy(&currentCache->buf[currentCache->offset],&buf[nwritten],cycleWrite); 
            nwritten+=cycleWrite;
            currentCache->offset+=cycleWrite;
            if (currentCache->offset==currentCache->bufsize){
                io61_flush(f);
            }
            assert(nwritten<=sz);
        }
        return nwritten;
    }
    // flush the buffer
    else {
        io61_flush(f);
        return io61_write(f, buf, sz);
    }
}


// io61_seek(f, pos)
//    Change the file pointer for file `f` to `pos` bytes into the file.
//    Returns 0 on success and -1 on failure.

int io61_seek(io61_file *f, size_t pos) {
    if (f->mode==O_WRONLY){
        io61_flush(f);
    }
    else{
        io61_cache *cache=getCurrentCache(f);
        if (cache){
            ssize_t delta=pos-(cache->pos+cache->offset-1);
            if(delta==-1&&!getCacheForPos(f,pos+delta)){
                buildCacheForPos(f, pos<PAGESIZE?0:pos+1-PAGESIZE);
            }
        }
        // if no cache can be used, build a new one
        if(!getCacheForPos(f, pos)){
            buildCacheForPos(f, pos);
        }
        io61_cache *currentCache=getCurrentCache(f);
        //posix_fadvise(f->fd, pos+delta, PAGESIZE, POSIX_FADV_NORMAL);
        currentCache->offset=pos-currentCache->pos;
    }
    off_t r = lseek(f->fd, (off_t) pos, SEEK_SET);
    if (r == (off_t) pos)
        return 0;
    else
        return -1;
}


// You should not need to change either of these functions.

// io61_open_check(filename, mode)
//    Open the file corresponding to `filename` and return its io61_file.
//    If `filename == NULL`, returns either the standard input or the
//    standard output, depending on `mode`. Exits with an error message if
//    `filename != NULL` and the named file cannot be opened.

io61_file *io61_open_check(const char *filename, int mode) {
    int fd;
    if (filename)
        fd = open(filename, mode);
    else if (mode == O_RDONLY)
        fd = STDIN_FILENO;
    else
        fd = STDOUT_FILENO;
    if (fd < 0) {
        exit(1);
    }
    return io61_fdopen(fd, mode);
}


// io61_filesize(f)
//    Return the number of bytes in `f`. Returns -1 if `f` is not a seekable
//    file (for instance, if it is a pipe).

ssize_t io61_filesize(io61_file *f) {
    struct stat s;
    int r = fstat(f->fd, &s);
    if (r >= 0 && S_ISREG(s.st_mode) && s.st_size <= SSIZE_MAX)
        return s.st_size;
    else
        return -1;
}
