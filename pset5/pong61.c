#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>
#include "serverinfo.h"
#include <pthread.h>

#define MIN(a,b) ((a) < (b) ? a : b)
#define MINTIME 0.001
#define MAXTHREADS 30

pthread_mutex_t myLock;
void *startConnection(void *con_info);

typedef struct coord_info {
    int x;
    int y;
    int dx;
    int dy;
    int width;
    int height;
} coord_info;

typedef struct con_info {
    struct addrinfo *ai;
    struct coord_info *cd;
} con_info;

static const char *pong_host = PONG_HOST;
static const char *pong_port = PONG_PORT;
static const char *pong_user = PONG_USER;


// Timer and interrupt functions (defined and explained below)

double timestamp(void);
void sleep_for(double delay);
void interrupt_after(double delay);
void interrupt_cancel(void);


// HTTP connection management functions

// http_connection
//    This object represents an open HTTP connection to a server.
typedef struct http_connection {
    int fd;                 // Socket file descriptor

    int state;              // Response parsing status (see below)
    int status_code;        // Response status code (e.g., 200, 402)
    size_t content_length;  // Content-Length value
    int has_content_length; // 1 iff Content-Length was provided

    char buf[BUFSIZ];       // Response buffer
    size_t len;             // Length of response buffer
} http_connection;

// `http_connection::state` constants:
#define HTTP_REQUEST 0      // Request not sent yet
#define HTTP_INITIAL 1      // Before first line of response
#define HTTP_HEADERS 2      // After first line of response, in headers
#define HTTP_BODY    3      // In body
#define HTTP_DONE    (-1)   // Body complete, available for a new request
#define HTTP_CLOSED  (-2)   // Body complete, connection closed
#define HTTP_BROKEN  (-3)   // Parse error

// helper functions
char *http_truncate_response(http_connection *conn);
static int http_consume_headers(http_connection *conn, int eof);

static void usage(void);


// http_connect(ai)
//    Open a new connection to the server described by `ai`. Returns a new
//    `http_connection` object for that server connection. Exits with an
//    error message if the connection fails.
http_connection *http_connect(const struct addrinfo *ai) {
    // connect to the server
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(1);
    }

    int yes = 1;
    (void) setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

    int r = connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (r < 0) {
        perror("connect");
        exit(1);
    }

    // construct an http_connection object for this connection
    http_connection *conn = (http_connection *) malloc(sizeof(http_connection));
    conn->fd = fd;
    conn->state = HTTP_REQUEST;
    return conn;
}


// http_close(conn)
//    Close the HTTP connection `conn` and free its resources.
void http_close(http_connection *conn) {
    close(conn->fd);
    free(conn);
}


// http_send_request(conn, uri)
//    Send an HTTP POST request for `uri` to connection `conn`.
//    Exit on error.
void http_send_request(http_connection *conn, const char *uri) {
    assert(conn->state == HTTP_REQUEST || conn->state == HTTP_DONE);

    // prepare and write the request
    char reqbuf[BUFSIZ];
    size_t reqsz = sprintf(reqbuf,
                           "POST /%s/%s HTTP/1.0\r\n"
                           "Host: %s\r\n"
                           "Connection: keep-alive\r\n"
                           "\r\n",
                           pong_user, uri, pong_host);
    size_t pos = 0;
    while (pos < reqsz) {
        ssize_t nw = write(conn->fd, &reqbuf[pos], reqsz - pos);
        if (nw == 0)
            break;
        else if (nw == -1 && errno != EINTR && errno != EAGAIN) {
            perror("write");
            exit(1);
        } else if (nw != -1)
            pos += nw;
    }

    if (pos != reqsz) {
        fprintf(stderr, "connection closed prematurely\n");
        exit(1);
    }

    // clear response information
    conn->state = HTTP_INITIAL;
    conn->status_code = -1;
    conn->content_length = 0;
    conn->has_content_length = 0;
    conn->len = 0;
}


// http_receive_response(conn)
//    Receive a response from the server. On return, `conn->status_code`
//    holds the server's status code, and `conn->buf` holds the response
//    body, which is `conn->len` bytes long and has been null-terminated.
//    If the connection terminated prematurely, `conn->status_code`
//    is -1.
void http_receive_response(http_connection *conn) {
    assert(conn->state != HTTP_REQUEST);
    if (conn->state < 0)
        return;

    // parse connection (http_consume_headers tells us when to stop)
    size_t eof = 0;
    double start=timestamp();
    int reps = 0;
    while (http_consume_headers(conn, eof)) {
        ++reps;
        
        if(reps>1)
            // Here we should start a new thread.
            fprintf(stderr,"Delayed Body\n");

        ssize_t nr = read(conn->fd, &conn->buf[conn->len], BUFSIZ);
        if(timestamp()-start>0.05)
            fprintf(stderr,"Delay: %fs\n",(timestamp()-start));
        if (nr == 0)
            eof = 1;
        else if (nr == -1 && errno != EINTR && errno != EAGAIN) {
            perror("read");
            exit(1);
        } else if (nr != -1)
            conn->len += nr;
    }

    // null-terminate body
    conn->buf[conn->len] = 0;

    // Status codes >= 500 mean we are overloading the server and should exit.
    if (conn->status_code >= 500) {
        fprintf(stderr, "exiting because of server status %d (%s)\n",
                conn->status_code, http_truncate_response(conn));
        exit(1);
    }
}


// http_truncate_response(conn)
//    Truncate the `conn` response text to a manageable length and return
//    that truncated text. Useful for error messages.
char *http_truncate_response(http_connection *conn) {
    char *eol = strchr(conn->buf, '\n');
    if (eol)
        *eol = 0;
    if (strnlen(conn->buf, 100) >= 100)
        conn->buf[100] = 0;
    return conn->buf;
}


// main(argc, argv)
//    The main loop.
int main(int argc, char **argv) {
    // parse arguments
    int ch;
    while ((ch = getopt(argc, argv, "h:p:u:")) != -1) {
        if (ch == 'h')
            pong_host = optarg;
        else if (ch == 'p')
            pong_port = optarg;
        else if (ch == 'u')
            pong_user = optarg;
        else
            usage();
    }
    if (optind == argc - 1)
        pong_user = argv[optind];
    else if (optind != argc)
        usage();

    // look up network address of pong server
    struct addrinfo ai_hints, *ai;
    memset(&ai_hints, 0, sizeof(ai_hints));
    ai_hints.ai_family = AF_INET;
    ai_hints.ai_socktype = SOCK_STREAM;
    ai_hints.ai_flags = AI_NUMERICSERV;
    int r = getaddrinfo(pong_host, pong_port, &ai_hints, &ai);
    if (r != 0) {
        fprintf(stderr, "problem contacting %s: %s\n",
                pong_host, gai_strerror(r));
        exit(1);
    }

    // reset pong board and get its dimensions
    int width, height;
    {
        http_connection *conn = http_connect(ai);
        http_send_request(conn, "reset");
        http_receive_response(conn);
        if (conn->status_code != 200
            || sscanf(conn->buf, "%d %d\n", &width, &height) != 2
            || width <= 0 || height <= 0) {
            fprintf(stderr, "bad response to \"reset\" RPC: %d %s\n",
                    conn->status_code, http_truncate_response(conn));
            exit(1);
        }
        http_close(conn);
    }

    // print display URL
    printf("Display: http://%s:%s/%s/\n", pong_host, pong_port, pong_user);

    // play game
    // initialize con_info struct
    coord_info cdi;
    cdi.x=0;
    cdi.y=0;
    cdi.dx=1;
    cdi.dy=1;
    cdi.width=width;
    cdi.height=height;
    con_info ci;
    ci.cd=&cdi;
    ci.ai=ai;
    
    //pthread_t threads[30];
    pthread_mutex_init(&myLock, NULL);
    // for(int i=0;i<30;++i){
    //     pthread_create(threads+i, NULL, startConnection, &ci);
    //     sleep_for(0.1);  
    // }

    pthread_t thread1;
    // pthread_t thread2;
    pthread_create(&thread1, NULL, startConnection, &ci);
    // sleep_for(0.1);
    // pthread_create(&thread2, NULL, startConnection, &ci);
    
    pthread_join(thread1, NULL); 
    // for(int i=0;i<30;++i){
    //     pthread_join(threads[i], NULL); 
    // }
}

// ai, x,y,dx,dy

void *startConnection(void *con_info){
    struct con_info *ci;
    ci = (struct con_info *)con_info;
    char url[BUFSIZ];
    double waittime;
    int x,y;
    while (1) {
        waittime=0;
        http_connection *conn;

        // lock here
        pthread_mutex_lock(&myLock);
        ci->cd->x += ci->cd->dx;
        ci->cd->y += ci->cd->dy;
        if (ci->cd->x < 0 || ci->cd->x >= ci->cd->width) {
            ci->cd->dx = -ci->cd->dx;
            ci->cd->x += 2 * ci->cd->dx;
        }
        if (ci->cd->y < 0 || ci->cd->y >= ci->cd->height) {
            ci->cd->dy = -ci->cd->dy;
            ci->cd->y += 2 * ci->cd->dy;
        }
        x=ci->cd->x;
        y=ci->cd->y;
        pthread_mutex_unlock(&myLock);

        sprintf(url, "move?x=%d&y=%d&style=on", x, y);
        // unlock here
        do {
            conn = http_connect(ci->ai);
            // Should I lock here?
            http_send_request(conn, url);

            http_receive_response(conn);
            if(conn->status_code==-1){
                http_close(conn);
                // Exponential backoff
                waittime=waittime==0?MINTIME:2*waittime;
                waittime=MIN(waittime,256*MINTIME);
                fprintf(stderr,"Sleeping for %fs\n",waittime);
                sleep_for(waittime);
            }
        } while(conn->status_code==-1);

        if (conn->status_code != 200)
            fprintf(stderr, "warning: %d,%d: server returned status %d "
                    "(expected 200)\n", ci->cd->x, ci->cd->y, conn->status_code);

        double result = strtod(conn->buf, NULL);
        if (result < 0) {
            fprintf(stderr, "server returned error: %s\n",
                    http_truncate_response(conn));
            exit(1);
        }

        fprintf(stderr,"Weee x: %d, y: %d\n", x, y);
        // wait 0.1sec before moving to next frame
        sleep_for(0.1);
        http_close(conn);
    }
}

// TIMING AND INTERRUPT FUNCTIONS
static void handle_sigalrm(int signo);

// timestamp()
//    Return the current time as a real number of seconds.
double timestamp(void) {
    struct timeval now;
    gettimeofday(&now, NULL);
    return now.tv_sec + (double) now.tv_usec / 1000000;
}


// sleep_for(delay)
//    Sleep for `delay` seconds, or until an interrupt, whichever comes
//    first.
void sleep_for(double delay) {
    usleep((long) (delay * 1000000));
}


// interrupt_after(delay)
//    Cause an interrupt to occur after `delay` seconds. This interrupt will
//    make any blocked `read` system call terminate early, without returning
//    any data.
void interrupt_after(double delay) {
    static int signal_set = 0;
    if (!signal_set) {
        struct sigaction sa;
        sa.sa_handler = handle_sigalrm;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        int r = sigaction(SIGALRM, &sa, NULL);
        if (r < 0) {
            perror("sigaction");
            exit(1);
        }
        signal_set = 1;
    }

    struct itimerval timer;
    timerclear(&timer.it_interval);
    timer.it_value.tv_sec = (long) delay;
    timer.it_value.tv_usec = (long) ((delay - timer.it_value.tv_sec) * 1000000);
    int r = setitimer(ITIMER_REAL, &timer, NULL);
    if (r < 0) {
        perror("setitimer");
        exit(1);
    }
}


// interrupt_cancel()
//    Cancel any outstanding interrupt.
void interrupt_cancel(void) {
    struct itimerval timer;
    timerclear(&timer.it_interval);
    timerclear(&timer.it_value);
    int r = setitimer(ITIMER_REAL, &timer, NULL);
    if (r < 0) {
        perror("setitimer");
        exit(1);
    }
}


// This is a helper function for `interrupt_after`.
static void handle_sigalrm(int signo) {
    (void) signo;
}


// HELPER FUNCTIONS FOR CODE ABOVE

// http_consume_headers(conn, eof)
//    Parse the response represented by `conn->buf`. Returns 1
//    if more data should be read into `conn->buf`, 0 if the response is
//    complete.
static int http_consume_headers(http_connection *conn, int eof) {
    size_t i = 0;
    while ((conn->state == HTTP_INITIAL || conn->state == HTTP_HEADERS)
           && i + 2 <= conn->len) {
        if (conn->buf[i] == '\r' && conn->buf[i+1] == '\n') {
            conn->buf[i] = 0;
            if (conn->state == HTTP_INITIAL) {
                int minor;
                if (sscanf(conn->buf, "HTTP/1.%d %d",
                           &minor, &conn->status_code) == 2)
                    conn->state = HTTP_HEADERS;
                else
                    conn->state = HTTP_BROKEN;
            } else if (i == 0)
                conn->state = HTTP_BODY;
            else if (strncmp(conn->buf, "Content-Length: ", 16) == 0) {
                conn->content_length = strtoul(conn->buf + 16, NULL, 0);
                conn->has_content_length = 1;
            }
            memmove(conn->buf, conn->buf + i + 2, conn->len - (i + 2));
            conn->len -= i + 2;
            i = 0;
        } else
            ++i;
    }

    if (conn->state == HTTP_BODY
        && (conn->has_content_length || eof)
        && conn->len >= conn->content_length)
        conn->state = HTTP_DONE;
    if (eof)
        conn->state = (conn->state == HTTP_DONE ? HTTP_CLOSED : HTTP_BROKEN);
    return conn->state >= 0;
}


// usage()
//    Explain how pong61 should be run.
static void usage(void) {
    fprintf(stderr, "Usage: ./pong61 [-h HOST] [-p PORT] [USER]\n");
    exit(1);
}
