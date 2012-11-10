#include "io61.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>

#define CHUNKS (4<<10)

// io61_file
//    Data structure for io61 file wrappers. Add your own stuff.

struct io61_file {
    int fd;
};

struct seq_buf {
    struct io61_file *f;
    char *buf;
    ssize_t offset;
    ssize_t bufsize;
    // Set to true if buf contains the whole file f or holds the last chunk
    // of a file. Then, bufsize represents the size of the chunk in buf.
    int bufDidSlurpFile;
};

struct seq_buf rbuf;
struct seq_buf wbuf;

// io61_fdopen(fd, mode)
//    Return a new io61_file that reads from and/or writes to the given
//    file descriptor `fd`. `mode` is either O_RDONLY for a read-only file
//    or O_WRONLY for a write-only file. You need not support read/write
//    files.

io61_file *io61_fdopen(int fd, int mode) {
    assert(fd >= 0);
    io61_file *f = (io61_file *) malloc(sizeof(io61_file));
    f->fd = fd;
    (void) mode;
    return f;
}


// io61_close(f)
//    Close the io61_file `f`.

int io61_close(io61_file *f) {
    io61_flush(f);
    int r = close(f->fd);
    free(f);
    return r;
}


// io61_readc(f)
//    Read a single (unsigned) character from `f` and return it. Returns EOF
//    (which is -1) on error or end-of-file.

int io61_readc(io61_file *f) {
    // initialize the buffer
    if(rbuf.f!=f){
        rbuf.f=f;
        free(rbuf.buf);
        rbuf.buf=(char *)malloc(CHUNKS);
        rbuf.bufsize=CHUNKS;
        int readchars=read(f->fd, rbuf.buf, CHUNKS);
        if (readchars<CHUNKS)
            rbuf.buf[readchars]=EOF;
        rbuf.offset=-1;
    }
    ++rbuf.offset;
    // the buffer can still satisfy the request
    if (rbuf.offset<rbuf.bufsize){
        if (rbuf.buf[rbuf.offset] != EOF){
            return rbuf.buf[rbuf.offset];
        }
        else{
            return EOF;
        }
    }
    // the buffer needs to be refilled
    else {
        int readchars=read(f->fd, rbuf.buf, CHUNKS);
        if (readchars==0)
            return EOF;
        if (readchars<rbuf.bufsize)
            rbuf.buf[readchars]=EOF;
        rbuf.offset=-1;
        return io61_readc(f);
    }
}


// io61_writec(f)
//    Write a single character `ch` to `f`. Returns 0 on success or
//    -1 on error.

int io61_writec(io61_file *f, int ch) {
    if(wbuf.f!=f) {
        io61_flush(wbuf.f);
        free(wbuf.buf);
        wbuf.buf=(char *)malloc(CHUNKS);
        wbuf.f=f;
        wbuf.offset=0;
    }
    // fill the buffer
    if (wbuf.offset<CHUNKS){
        wbuf.buf[wbuf.offset] = ch; 
        ++wbuf.offset;
        return 0;
    }
    // flush the buffer
    else {
        io61_flush(wbuf.f);
        return io61_writec(f, ch);
    }
}


// io61_flush(f)
//    Forces a write of any `f` buffers that contain data.

int io61_flush(io61_file *f) {
    if (wbuf.f==f&&wbuf.offset>0) {
        write(f->fd, wbuf.buf, wbuf.offset);
        wbuf.offset=0;
    }
    return 0;
}


// io61_read(f, buf, sz)
//    Read up to `sz` characters from `f` into `buf`. Returns the number of
//    characters read on success; normally this is `sz`. Returns a short
//    count if the file ended before `sz` characters could be read. Returns
//    -1 an error occurred before any characters were read.

ssize_t io61_read(io61_file *f, char *buf, size_t sz) {
    int bufsize=8*sz;
    // initialize the buffer
    if(rbuf.f!=f) {
        rbuf.f=f;
        free(rbuf.buf);
        rbuf.offset=0;
        rbuf.bufDidSlurpFile=0;
        rbuf.bufsize=bufsize;
        rbuf.buf=(char *)malloc(rbuf.bufsize);
        ssize_t readchars=read(f->fd, rbuf.buf, rbuf.bufsize);
        // file has been completely read into buffer
        if (readchars<rbuf.bufsize) {
            rbuf.bufDidSlurpFile=1;
            rbuf.bufsize=readchars;
        }
    }
    // The buffer can still satisfy the request
    if (rbuf.offset+(ssize_t)sz<=rbuf.bufsize||rbuf.bufDidSlurpFile) {
        // The whole request can be satisfied
        if((ssize_t)sz<=rbuf.bufsize-rbuf.offset) {
            memcpy(buf,&rbuf.buf[rbuf.offset],sz);
            rbuf.offset+=sz;
            return sz;
        }
        // The request can only be partially satisfied
        // (The remaining chars in buffer are < sz)
        else {
            memcpy(buf,&rbuf.buf[rbuf.offset],rbuf.bufsize-rbuf.offset);
            rbuf.offset+=rbuf.bufsize-rbuf.offset;
            return rbuf.bufsize-rbuf.offset;
        }
    } 
    // the buffer needs to be refilled
    else {
        char *temp=malloc(wbuf.bufsize);
        //copy bytes that have not been returned to beginning of buffer
        memcpy(rbuf.buf,&rbuf.buf[rbuf.offset],rbuf.bufsize-rbuf.offset);
        rbuf.offset=0;
        ssize_t readchars=read(f->fd, &rbuf.buf[rbuf.offset], rbuf.bufsize-rbuf.offset);
        // file has been completely read into buffer
        if (readchars<rbuf.bufsize-rbuf.offset) {
            rbuf.bufDidSlurpFile=1;
            rbuf.bufsize=readchars;
        }
        return io61_read(f,buf,sz);
    }
}


// io61_write(f, buf, sz)
//    Write `sz` characters from `buf` to `f`. Returns the number of
//    characters written on success; normally this is `sz`. Returns -1 if
//    an error occurred before any characters were written.

ssize_t io61_write(io61_file *f, const char *buf, size_t sz) {
    ssize_t bufsize=16*sz;
    // initialize the buffer
    if(rbuf.f!=f) {
        wbuf.f=f;
        io61_flush(wbuf.f);
        free(wbuf.buf);
        wbuf.offset=0;
        wbuf.bufsize=bufsize;
        wbuf.buf=(char *)malloc(wbuf.bufsize);
    }
    // fill the buffer
    if (wbuf.offset+(ssize_t)sz<=rbuf.bufsize) {
        memcpy(&wbuf.buf[wbuf.offset],buf,sz);
        wbuf.offset+=sz;
        return sz;
    } 
    // flush the buffer 
    else {
        io61_flush(wbuf.f);
        // if the buffer can't fit the chunk -> expand buffer
        // beware of buffer getting too large!!
        if ((ssize_t)sz>wbuf.bufsize) {
            free(wbuf.buf);
            wbuf.offset=0;
            wbuf.bufsize=bufsize;
            wbuf.buf=(char *)malloc(wbuf.bufsize);
        }
        return io61_write(f, buf, sz);
    }
}


// io61_seek(f, pos)
//    Change the file pointer for file `f` to `pos` bytes into the file.
//    Returns 0 on success and -1 on failure.

int io61_seek(io61_file *f, size_t pos) {
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
