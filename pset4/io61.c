#include "io61.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>

#define PAGESIZE (4<<10)
#define NCACHES 5


// io61_file
//    Data structure for io61 file wrappers. Add your own stuff.

typedef enum cache_status{
    CACHE_EMPTY   = 0,
    CACHE_ACTIVE  = 1,
    CACHE_DRAINED = 2
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
    // Interaction shall only occur through 3 functions:
    // getCurrentCache(io61_file *f) -- returns most recently used cache
    // setCurrentCache(io61_file *f, io61_cache *cache) -- set
    // buildCacheForPos(io61_file *f, size_t pos) -- creates a io61_cache element
    //      and inserts it into the array. Returns a pointer to the created io61_cache element.
    //      The least recently used element may be evicted. 
    // getCacheForPos(io61_file *f, size_t pos) -- returns pointer to io61_cache
    //      element on success and sets the current cache element, else 0
    io61_cache caches[NCACHES];
    // index into caches array, -1 if no current cache present
    int currentCache;
    // holds cache/mmaped file
    char *buf;
    // offset within fd
    ssize_t fdoffset;
    // offset of passed out/read in bytes within *buf
    ssize_t offset;
    // size of *buf
    ssize_t bufsize;
//  // Set to true if buf contains the whole file f or holds the last chunk
//  // of a file. Then, bufsize represents the size of the chunk in buf.
//  int bufDidSlurpFile;
//  // Bool indicating whether we're likely dealing with a pipe
//  // and thus shouldn't expand the buffer futher
//  //int shouldStopExpanding;
};

io61_cache *freeCache(io61_file *f) {
    int oldestCache=0;
    int maxLifetime = -1;
    for (int i=0;i<NCACHES;++i) {
        io61_cache *currentCache=&f->caches[i];
        if (currentCache->state==CACHE_EMPTY||currentCache->state==CACHE_DRAINED)
            return currentCache;
        if (currentCache->lifetime>maxLifetime) {
            oldestCache=i;
            maxLifetime=currentCache->lifetime;
        }
    }
    //fprintf(stderr,"I have found a Cache! #%d LT%d\n",oldestCache, maxLifetime);
    // This cache has been in use and thus has a malloced buf
    if (f->caches[oldestCache].state==CACHE_ACTIVE)
        free(f->caches[oldestCache].buf);
    return &f->caches[oldestCache];
}

// buildCacheForPos(io61_file *f, size_t pos) -- creates a io61_cache element
//      and inserts it into the array. Returns a pointer to the created io61_cache element or
//      NULL on error(EOF). The least recently used element may be evicted. 
io61_cache *buildCacheForPos(io61_file *f, size_t pos) {
    io61_cache *newCache=freeCache(f);
    newCache->buf=malloc(PAGESIZE);
    //pread(int d, void *buf, size_t nbyte, off_t offset);
    ssize_t readchars=pread(f->fd, newCache->buf, PAGESIZE, (off_t)pos);
    // The file is not seekable, just read sequentially and hope we don't get caught
    if (readchars==-1) {
        readchars=read(f->fd, newCache->buf, PAGESIZE);
    }
    //fprintf(stderr,"Size: %zd %s\n",readchars,strerror(errno));
    // if nothing can be read into the buffer, return -1
    if (readchars==0) {
        newCache->state=CACHE_EMPTY;
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

io61_cache *getCacheForPos(io61_file *f, size_t pos) {
    io61_cache *foundCache=0;
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
    return f;
}


// io61_close(f)
//    Close the io61_file `f`.

int io61_close(io61_file *f) {
    io61_flush(f);
    int r = close(f->fd);
    f->bufsize=0;
    free(f->buf);
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
    size_t nread = 0;
    while (nread != sz) {
        int ch = io61_readc(f);
        if (ch == EOF)
            break;
        buf[nread] = ch;
        ++nread;
    }
    f->fdoffset+=nread;
    if (nread == 0 && sz != 0)
        return -1;
    else
        return nread;
}


// io61_write(f, buf, sz)
//    Write `sz` characters from `buf` to `f`. Returns the number of
//    characters written on success; normally this is `sz`. Returns -1 if
//    an error occurred before any characters were written.

ssize_t io61_write(io61_file *f, const char *buf, size_t sz) {
    if (getCurrentCache(f)==NULL) {
        f->currentCache=0;
        f->caches[0].buf=malloc(PAGESIZE);
        f->caches[0].bufsize=PAGESIZE;
        f->caches[0].offset=0;
    }
    io61_cache *currentCache=getCurrentCache(f);
    // fill the buffer
    if (currentCache->offset<currentCache->bufsize) {
        size_t nwritten = 0;
        size_t cycleWrite = 0;
        size_t charsLeft = 0;
        while (nwritten!=sz) {
            charsLeft=currentCache->bufsize-currentCache->offset;
            //fprintf(stderr,"Chars left %zd \t SZ %zd\n",charsLeft,sz);
            cycleWrite = (sz-nwritten)>charsLeft?charsLeft:(sz-nwritten);
            memcpy(&currentCache->buf[currentCache->offset],&buf[nwritten],cycleWrite); 
            nwritten+=cycleWrite;
            //fprintf(stderr,"Nwritten %zd \t SZ %zd\n",nwritten,sz);
            currentCache->offset+=cycleWrite;
            if (currentCache->offset==currentCache->bufsize){
                //fprintf(stderr,"Flush\n");
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
//    size_t nwritten = 0;
//    while (nwritten != sz) {
//        if (io61_writec(f, buf[nwritten]) == -1)
//        break;
//        ++nwritten;
//    }
//    if (nwritten == 0 && sz != 0)
//        return -1;
//    else
//        return nwritten;
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
            //fprintf(stderr,"Building Cache @%zu\n",pos);
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
