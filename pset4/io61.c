#include "io61.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>

#define PAGESIZE (4<<14)

// io61_file
//    Data structure for io61 file wrappers. Add your own stuff.

struct io61_file {
    int fd;
    char *buf;
    ssize_t offset;
    ssize_t bufsize;
    // Set to true if buf contains the whole file f or holds the last chunk
    // of a file. Then, bufsize represents the size of the chunk in buf.
    int bufDidSlurpFile;
    int shouldStopExpanding;
    ssize_t filesize;
};

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
    (void) mode;
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
    if (f->bufsize==0) {
        // start out with PAGESIZE
        f->buf=(char *)malloc(PAGESIZE);
        f->bufsize=PAGESIZE;
        int readchars=read(f->fd, f->buf, PAGESIZE);
        if (readchars<PAGESIZE)
            f->buf[readchars]=EOF;
        f->offset=-1;
    }
    ++f->offset;
    ssize_t loffset=f->offset;
    ssize_t lbufsize=f->bufsize;
    // the buffer can still satisfy the request
    if (loffset<lbufsize){
        if (f->buf[loffset] != EOF){
            return f->buf[loffset];
        }
        else{
            ssize_t readchars=read(f->fd, f->buf, lbufsize);
            //fprintf(stderr,"Read: %zu %zu\n",readchars,lbufsize);
            if (readchars==0)
                return EOF;
            else if (readchars<lbufsize) {
                f->bufsize=readchars;
                f->offset=-1;
                f->buf[readchars]=EOF;
                f->shouldStopExpanding=1;
                return io61_readc(f);
            }
            f->offset=-1;
            return io61_readc(f);
        }
    }
    // the buffer needs to be refilled
    else {
        // expand the buffer
        if(!f->shouldStopExpanding) {
            free(f->buf);
            f->buf=(char *)malloc(lbufsize*2);
            f->bufsize*=2;
            fprintf(stderr,"Expanding Buffer: %zu\n",f->bufsize);
        }
        ssize_t readchars=read(f->fd, f->buf, f->bufsize);
        assert(readchars!=-1);
        //fprintf(stderr,"Read: %zu\n",readchars);
//        if (readchars==0)
//            return EOF;
        if (readchars<f->bufsize)
            f->buf[readchars]=EOF;
        f->offset=-1;
        return io61_readc(f);
    }
}


// io61_writec(f)
//    Write a single character `ch` to `f`. Returns 0 on success or
//    -1 on error.

int io61_writec(io61_file *f, int ch) {
    if (f->bufsize==0) {
        f->buf=(char *)malloc(PAGESIZE);
        f->bufsize=PAGESIZE;
        f->offset=0;
    }
    ssize_t lbufsize=f->bufsize;
    ssize_t loffset=f->offset;
    // fill the buffer
    if (loffset<lbufsize){
        f->buf[loffset] = ch; 
        ++f->offset;
        return 0;
    }
    // flush the buffer
    else {
        io61_flush(f);
        free(f->buf);
        f->buf=(char *)malloc(lbufsize*2);
        f->bufsize*=2;
        return io61_writec(f, ch);
    }
}


// io61_flush(f)
//    Forces a write of any `f` buffers that contain data.

int io61_flush(io61_file *f) {
    if (f->offset>0) {
        write(f->fd, f->buf, f->offset);
        f->offset=0;
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
    if (f->bufsize==0) {
        f->buf=(char *)malloc(PAGESIZE);
        f->bufsize=PAGESIZE;
        ssize_t readchars=read(f->fd, f->buf, f->bufsize);
        f->offset=0;
        f->bufDidSlurpFile=0;
        f->filesize=io61_filesize(f);
        //fprintf(stderr,"Filesize: %zu\n",(size_t)f->filesize);
        // file has been completely read into buffer
        if (readchars<f->bufsize) {
            f->bufDidSlurpFile=1;
            f->bufsize=readchars;
        }
    }
    // The buffer can still satisfy the request
    // The whole request can be satisfied
    if (f->offset+(ssize_t)sz<=f->bufsize) {
        memcpy(buf,&f->buf[f->offset],sz);
        f->offset+=sz;
        return sz;
    }
    // The request can only be partially satisfied
    // (The remaining chars in buffer are < sz)
    else if (f->bufDidSlurpFile) {
        memcpy(buf, &f->buf[f->offset], f->bufsize-f->offset);
        f->offset+=f->bufsize-f->offset;
        return f->bufsize-f->offset;
    }
    // the buffer needs to be refilled
    else {
        if((int)f->filesize>0) {
            free(f->buf);
            f->buf=(char *)malloc(f->bufsize*2);
            f->bufsize*=2;
            fprintf(stderr,"Expanding Buffer: %zu\n",f->bufsize);
        }
        //copy bytes that have not been returned to beginning of buffer
        memcpy(f->buf,&f->buf[f->offset],f->bufsize-f->offset);
        f->offset=0;
        ssize_t readchars=read(f->fd, &f->buf[f->offset], f->bufsize-f->offset);
        // file has been completely read into buffer
        if (readchars<f->bufsize-f->offset) {
            f->bufDidSlurpFile=1;
            f->bufsize=readchars;
        }
        return io61_read(f,buf,sz);
    }
}


// io61_write(f, buf, sz)
//    Write `sz` characters from `buf` to `f`. Returns the number of
//    characters written on success; normally this is `sz`. Returns -1 if
//    an error occurred before any characters were written.

ssize_t io61_write(io61_file *f, const char *buf, size_t sz) {
    size_t bufsize=64*sz;
    // initialize the buffer
    if(f->bufsize==0) {
        f->buf=(char *)malloc(bufsize);
        f->bufsize=bufsize;
        f->offset=0;
    }
    // fill the buffer
    if (f->offset+(ssize_t)sz<=f->bufsize) {
        memcpy(&f->buf[f->offset],buf,sz);
        f->offset+=sz;
        return sz;
    } 
    // flush the buffer 
    else {
        io61_flush(f);
        // if the buffer can't fit the chunk -> expand buffer
        // beware of buffer getting too large!!
        if ((ssize_t)sz>f->bufsize) {
            free(f->buf);
            f->offset=0;
            f->bufsize=bufsize;
            f->buf=(char *)malloc(f->bufsize);
        }
        return io61_write(f, buf, sz);
    }
}


// io61_seek(f, pos)
//    Change the file pointer for file `f` to `pos` bytes into the file.
//    Returns 0 on success and -1 on failure.

int io61_seek(io61_file *f, size_t pos) {
    io61_flush(f);
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
        fprintf(stderr, "%s: %s\n", filename, strerror(errno));
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
