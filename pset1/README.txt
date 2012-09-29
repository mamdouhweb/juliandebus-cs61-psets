README for CS 61 Problem Set 1
------------------------------
YOU MUST FILL OUT THIS FILE BEFORE SUBMITTING!

YOUR NAME: Julian Debus
YOUR HUID: 40879447

(Optional, for partner)
YOUR NAME:
YOUR HUID:

OTHER COLLABORATORS AND CITATIONS (if any):
I relied on the Algorithm "FREQUENT" which was pointed to in the assignment.
I implemented a modified version of this algorithm - without differential encoding.
I hadn't originally planned to use a doubly linked list -- this was brought up during lecture and 'inspired' me to pursue this approach.

NOTES FOR THE GRADER (if any):
I'm confused: free() seems to be nulling memory and thus wiping some of my metadata. I had to change the order of the members of my metadata struct to pass the 'make check' tests on the appliance. They had been working on my Mac previously. I ran gdb on the appliance and got the following output (meta_ptr is a pointer to my metadata struct):

1: meta_ptr -> file = 0x80496dc "test016.c"
(gdb) n
201     free(meta_ptr);
1: meta_ptr -> file = 0x80496dc "test016.c"
(gdb) n
202 }
1: meta_ptr -> file = 0x0

Anyways, after reordering the members the tests are passing in the appliance :)

EXTRA CREDIT ATTEMPTED (if any):


THOUGHTS ON METADATA
My metadata consists of two structs:
typedef struct metadata {
    struct metadata *prv;
    struct metadata *next;
    size_t sz;
    const char *file;
    int line;
    double previously_freed; //This ensures a correct alignment
    struct metadata *self;
}metadata;

typedef struct backpack {
    struct backpack *self; 
}backpack;

Each allocated object is part of a doubly linked list, so I added two pointers for the previous and next element
In order to keep track of the size of memory requested by the user, I added the variable sz
The file and line variables hold the file and line where a block of memory was allocated or freed
The double previously_freed acts as a boolean which is false unless the memory was freed and is no longer owned by the user.
    I chose a double to ensure proper alignment of the memory (as it would be on a call to 'standard' malloc())
The pointer self is used to check the validity of allocated memory (both in the metadata and the backpack).
    It points to the start of the respective struct if the memory is still owned by the user and no boundary write errors or something else caused inconsitencies.
    If the backpack or the metadata are not consistent it is assumed that a boundary write error happened.
    If both the backpack and metadata are not consistent it is assumed that a respective pointer (from which the memory addresses of the two structs were calculated) was not handed out by m61_malloc()
