// vim: set tabstop=8: -*- tab-width: 8 -*-
#include "kernel.h"
#include "lib.h"

// kernel.c
//
//    This is the kernel.


// INITIAL PHYSICAL MEMORY LAYOUT
//
//  +-------------- Base Memory --------------+
//  v                                         v
// +-----+--------------------+----------------+--------------------+---------/
// |     | Kernel      Kernel |       :    I/O | App 1        App 1 | App 2
// |     | Code + Data  Stack |  ...  : Memory | Code + Data  Stack | Code ...
// +-----+--------------------+----------------+--------------------+---------/
// 0  0x40000              0x80000 0xA0000 0x100000             0x140000
//                                             ^
//                                             | \___ PROC_SIZE ___/
//                                      PROC_START_ADDR

#define PROC_SIZE 0x040000	// initial state only

static proc processes[NPROC];	// array of process descriptors
				// Note that `processes[0]` is never used.
proc *current;			// pointer to currently executing proc

#define HZ 100			// timer interrupt frequency (interrupts/sec)
static unsigned ticks;		// # timer interrupts so far

void schedule(void);
void run(proc *p) __attribute__((noreturn));

uintptr_t freeAddress(void);

// PAGEINFO
//
//    The pageinfo[] array keeps track of information about each physical page.
//    There is one entry per physical page, `NPAGES` total.
//    `pageinfo[pn]` holds the information for physical page number `pn`,
//    which is at address `(pn << PAGESHIFT)`.
//    You can get a physical page number from a physical address `pa` using
//    `PAGENUMBER(pa)`. This also works for page table entries.
//
//    pageinfo[pn].refcount is the number of times physical page `pn` is
//      currently referenced. 0 means it's free.
//    pageinfo[pn].owner is a constant indicating who owns the page.
//      PO_KERNEL means the kernel, PO_RESERVED means reserved memory (such
//      as the console), and a number >=0 means that process ID.
//
//    pageinfo_init() sets up the initial pageinfo[] state.

typedef struct pageinfo {
    int8_t owner;
    int8_t refcount;
} pageinfo_t;

static pageinfo_t pageinfo[NPAGES];

typedef enum pageowner {
    PO_FREE = 0,		// this page is free
    PO_RESERVED = -1,		// this page is reserved memory
    PO_KERNEL = -2		// this page is used by the kernel
} pageowner_t;

static void pageinfo_init(void);


// Memory functions

void virtual_memory_check(void);
void memshow_physical(void);
void memshow_virtual(pageentry_t *pagedir, const char *name);
void memshow_virtual_animate(void);


int page_alloc(pageentry_t *pagedir, uintptr_t addr, int8_t owner);
int my_page_alloc(uintptr_t addr, int8_t owner);

// start(command)
//    Initialize the hardware and processes and start running. The `command`
//    string is an optional string passed from the boot loader.

static void process_setup(pid_t pid, int program_number);

void start(const char *command) {
    hardware_init();
    pageinfo_init();
    console_clear();
    timer_init(HZ);

    // Set up process descriptors
    memset(processes, 0, sizeof(processes));
    for (pid_t i = 0; i < NPROC; i++) {
	processes[i].p_pid = i;
	processes[i].p_state = P_FREE;
    }
    
    // map kernel pages below console as read-only
    virtual_memory_map(kernel_pagedir, 0, 0, (size_t)console, PTE_P|PTE_W);
    // map kernel pages above console as read-only
    virtual_memory_map(kernel_pagedir,(uintptr_t) console+PAGESIZE,
                       (uintptr_t) console+PAGESIZE, 
                        (size_t)(PROC_START_ADDR-((uintptr_t)console+PAGESIZE)),
                        PTE_P|PTE_W
                        );

    if (command && strcmp(command, "fork") == 0)
	process_setup(1, 4);
    else if (command && strcmp(command, "forkexit") == 0)
	process_setup(1, 5);
    else
    for (pid_t i = 1; i <= 4; ++i){
        process_setup(i, i - 1);
    }

    // Switch to the first process using run()
    run(&processes[1]);
}

pageentry_t *copy_pagedir(pageentry_t *pagedir, pid_t owner){
	//init a pointer to the new (to be created) page directory
	pageentry_t *processdir=(pageentry_t *)freeAddress();
	my_page_alloc((uintptr_t)processdir,owner);
    
    //init a pointer to the new (to be created) page
	pageentry_t *processpage=(pageentry_t *)freeAddress();
	my_page_alloc((uintptr_t)processpage,owner);
	
	//set the first page to 0
	memset(processdir,0,PAGESIZE);

    //set the first entry of the directory to be the address of the new page
	processdir[0]=(pageentry_t) processpage | PTE_P | PTE_W | PTE_U;

	//calculate the offset (in bytes) inside pagetable
    int offset=PAGENUMBER(PROC_START_ADDR)*sizeof(pageentry_t); //1024
	
    //construct a pointer from the passed in page directory to its pagetable page
	//(strip away the permission bits)
    pageentry_t *pdpagetable=(pageentry_t *)PTE_ADDR(pagedir[0]);

	//copy the contents of the kernel directory page to the newly created one
	memcpy(processpage, pdpagetable,offset);

	//0out everything above the offset
	memset((char *)processpage+offset,0,PAGESIZE-offset);

    return processdir;
}

// process_setup(pid, program_number)
//    Load application program `program_number` as process number `pid`.
//    This loads the application's code and data into memory, sets its
//    %eip and %esp, gives it a stack page, and marks it as runnable.

void process_setup(pid_t pid, int program_number) {
    process_init(&processes[pid], 0);
    current->p_pid=pid;
    processes[pid].p_pagedir = copy_pagedir(kernel_pagedir, current->p_pid);
    
    int r = program_load(&processes[pid], program_number);
    assert(r >= 0);
    // set the stack to the top of the virtaul memory
    processes[pid].p_registers.reg_esp = MEMSIZE_VIRTUAL;
    // allocate a free physical page and map it in the process page directory
    uintptr_t procStack=freeAddress();
    my_page_alloc(procStack, pid);
    virtual_memory_map(processes[pid].p_pagedir, 
                        processes[pid].p_registers.reg_esp - PAGESIZE,
                        procStack, PAGESIZE, PTE_P|PTE_W|PTE_U
                        );
    processes[pid].p_state = P_RUNNABLE;
}

// page_alloc(pagedir, addr, owner)
//    Allocates the page with physical address `addr` to the given owner,
//    and maps it at the same address in the page directory `pagedir`.
//    The mapping uses permissions `PTE_P | PTE_W | PTE_U` (user-writable).
//    Fails if physical page `addr` was already allocated. Used by the
//    program loader.

int page_alloc(pageentry_t *pagedir, uintptr_t addr, int8_t owner) {
    if ((addr & 0xFFF) != 0 || addr >= MEMSIZE_PHYSICAL
	|| pageinfo[PAGENUMBER(addr)].refcount != 0)
	return -1;
    else {
	pageinfo[PAGENUMBER(addr)].refcount = 1;
	pageinfo[PAGENUMBER(addr)].owner = owner;
	virtual_memory_map(pagedir, addr, addr, PAGESIZE,
			   PTE_P | PTE_W | PTE_U);
	return 0;
    }
}

uintptr_t m_alloc(int8_t owner){
    uintptr_t freePhysicalAddress=freeAddress();
    if(freePhysicalAddress==-1)
        return -1;
    my_page_alloc(freePhysicalAddress,owner);
    return freePhysicalAddress;
}

int my_page_alloc(uintptr_t addr, int8_t owner) {
    if ((addr & 0xFFF) != 0 || addr >= MEMSIZE_PHYSICAL
	|| pageinfo[PAGENUMBER(addr)].refcount != 0)
	return -1;
    else {
	pageinfo[PAGENUMBER(addr)].refcount = 1;
	pageinfo[PAGENUMBER(addr)].owner = owner;
	return 0;
    }
}

uintptr_t freeAddress(){
	for (int i = 0; i < NPAGES; ++i) {
    	if(pageinfo[i].owner==0)
    		return i*PAGESIZE;
    }
    return -1;
}

// interrupt(reg)
//    Interrupt handler.
//
//    The register values from interrupt time are stored in `reg`.
//    The processor responds to an interrupt by saving application state on
//    the kernel's stack, then jumping to kernel assembly code (in
//    k-interrupt.S). That code saves more registers on the kernel's stack,
//    then calls interrupt().
//
//    Note that hardware interrupts are disabled for as long as the OS01
//    kernel is running.

void interrupt(struct registers *reg) {
    // Copy the saved registers into the `current` process descriptor
    // and always use the kernel's page directory.
    current->p_registers = *reg;
    lcr3(kernel_pagedir);

    // It can be useful to log events using `log_printf` (see host's `log.txt`).
    //log_printf("proc %d: interrupt %d\n", current->p_pid, reg->reg_intno);

    // Show the current cursor location and memory state.
    console_show_cursor(cursorpos);
    virtual_memory_check();
    memshow_physical();
    memshow_virtual_animate();

    // If Control-C was typed, exit the virtual machine.
    check_keyboard();

    // Actually handle the interrupt.
    switch (reg->reg_intno) {

    case INT_SYS_PANIC:
	panic("%s", (char *) current->p_registers.reg_eax);

    case INT_SYS_GETPID:
	current->p_registers.reg_eax = current->p_pid;
	run(current);

    case INT_SYS_YIELD:
	schedule();

    case INT_SYS_PAGE_ALLOC: {
    //find free physical Address
    uintptr_t freePhysicalAddress = freeAddress();
	//update pageinfo[]
	if(freePhysicalAddress==-1){
        current->p_registers.reg_eax=-1;
        run(current);
	}
	//map requested virtual memory address to found physical memory address
	virtual_memory_map(current->p_pagedir, current->p_registers.reg_eax,
                        freePhysicalAddress, PAGESIZE, PTE_P|PTE_W|PTE_U);
	current->p_registers.reg_eax = my_page_alloc(freePhysicalAddress,
                                                    current->p_pid);
	run(current);
    }
    
    case INT_TIMER:
	++ticks;
	schedule();

    case INT_PAGEFAULT: {
	// Analyze faulting address and access type.
	uint32_t addr = rcr2();
	const char *operation = (reg->reg_err & PFERR_WRITE ? "write" : "read");
	const char *problem = (reg->reg_err & PFERR_PRESENT ? "protection problem" : "missing page");

	if (!(reg->reg_err & PFERR_USER))
	    panic("Kernel page fault for %08X (%s %s, eip=%p)!\n",
		  addr, operation, problem, reg->reg_eip);
	console_printf(CPOS(24, 0), 0x0C00,
		       "Process %d page fault for %08X (%s %s, eip=%p)!\n",
		       current->p_pid, addr, operation, problem, reg->reg_eip);
	current->p_state = P_BROKEN;
	schedule();
    }
    
    case INT_SYS_FORK:{
        int slot=-1;
        // find a free process slot
        for (int i=1;i<NPROC;++i){
            if(processes[i].p_state==P_FREE){
                slot=i;
                break;
            }
        }
        // if no slot can be found -> return -1
        if(slot==-1){
            current->p_registers.reg_eax=-1;
            run(current);
            return;
        }
        else {
            proc *father=current;
            proc *child=&processes[slot];
            // initialize child process
            child->p_pid=slot;
            child->p_registers=father->p_registers;
            child->p_registers.reg_eax=0;
            child->p_state=P_RUNNABLE;
            // copy the father's pagedirectory
            pageentry_t *forkdir=copy_pagedir(father->p_pagedir, child->p_pid);
            // make a physical copy every user writable page and map the copy in the child's pagedirectory
            for (int i=PAGENUMBER(PROC_START_ADDR);i<PAGENUMBER(MEMSIZE_VIRTUAL);++i){
                uintptr_t va=i<<PAGESHIFT;
                uintptr_t pa=virtual_memory_lookup(father->p_pagedir,va);
                int pageIsUserWritable=(pa&(PTE_P|PTE_U|PTE_W))==(PTE_P|PTE_U|PTE_W);
                int pageIsUserReadable=(pa&(PTE_P|PTE_U))==(PTE_P|PTE_U);
                if(pageIsUserWritable){
                    uintptr_t freePhysicalAddress=m_alloc(child->p_pid); 
                    memcpy((char *)freePhysicalAddress, (char *)PTE_ADDR(pa), PAGESIZE);
                    virtual_memory_map(forkdir, va, freePhysicalAddress, PAGESIZE, PTE_P|PTE_W|PTE_U);
                }
                else if(pageIsUserReadable) {
                    virtual_memory_map(forkdir, va, PTE_ADDR(pa), PAGESIZE, PTE_P|PTE_U);
                    ++pageinfo[PAGENUMBER(PTE_ADDR(pa))].refcount;
                }
            }
            child->p_pagedir=forkdir;
            father->p_registers.reg_eax=child->p_pid;
            run(father);
        }
    }

    default:
	panic("Unexpected interrupt %d!\n", reg->reg_intno);

    }
}

// schedule
//    Pick the next process to run and then run it.
//    If there are no runnable processes, spins forever.

void schedule(void) {
    pid_t pid = current->p_pid;
    while (1) {
	pid = (pid + 1) % NPROC;
	if (processes[pid].p_state == P_RUNNABLE)
	    run(&processes[pid]);
	// If Control-C was typed, exit the virtual machine.
	check_keyboard();
    }
}


// run(p)
//    Run process `p`. This means reloading all the registers from
//    `p->p_registers` using the `popal`, `popl`, and `iret` instructions.
//
//    As a side effect, sets `current = p`.

void run(proc *p) {
    assert(p->p_state == P_RUNNABLE);
    current = p;

    lcr3(p->p_pagedir);
    asm volatile("movl %0,%%esp\n\t"
		 "popal\n\t"
		 "popl %%es\n\t"
		 "popl %%ds\n\t"
		 "addl $8, %%esp\n\t"
		 "iret"
		 :
		 : "g" (&p->p_registers)
		 : "memory");

 loop: goto loop;		/* should never get here */
}


// pageinfo_init
//    Initialize the `pageinfo[]` array.

void pageinfo_init(void) {
    extern char end[];

    for (uintptr_t addr = 0; addr < MEMSIZE_PHYSICAL; addr += PAGESIZE) {
	int owner;
	if (physical_memory_isreserved(addr))
	    owner = PO_RESERVED;
	else if ((addr >= KERNEL_START_ADDR && addr < (uintptr_t) end)
		 || addr == KERNEL_STACK_TOP - PAGESIZE)
	    owner = PO_KERNEL;
	else
	    owner = PO_FREE;
	pageinfo[PAGENUMBER(addr)].owner = owner;
	pageinfo[PAGENUMBER(addr)].refcount = (owner != PO_FREE);
    }
}


// virtual_memory_check
//    Check operating system invariants about virtual memory. Panic if any
//    of the invariants are false.

void virtual_memory_check(void) {
    // Process 0 must never be used.
    assert(processes[0].p_state == P_FREE);

    // The kernel page directory should be owned by the kernel.
    // Its reference count should equal 1 plus the number of processes that
    // share the directory.
    // Active processes have their own page directories. A process page
    // directory should be owned by that process and have reference count 1.
    // All page table pages must have reference count 1.

    // Calculate expected kernel refcount
    int expected_kernel_refcount = 1;
    for (int pid = 0; pid < NPROC; ++pid)
	if (processes[pid].p_state != P_FREE
	    && processes[pid].p_pagedir == kernel_pagedir)
	    ++expected_kernel_refcount;

    for (int pid = -1; pid < NPROC; ++pid) {
	if (pid >= 0 && processes[pid].p_state == P_FREE)
	    continue;

	pageentry_t *pagedir;
	int expected_owner, expected_refcount;
	if (pid < 0 || processes[pid].p_pagedir == kernel_pagedir) {
	    pagedir = kernel_pagedir;
	    expected_owner = PO_KERNEL;
	    expected_refcount = expected_kernel_refcount;
	} else {
	    pagedir = processes[pid].p_pagedir;
	    expected_owner = pid;
	    expected_refcount = 1;
	}
   
    // Check page directory itself
	assert(pageinfo[PAGENUMBER(pagedir)].owner == expected_owner);
	assert(pageinfo[PAGENUMBER(pagedir)].refcount == expected_refcount);

	// Check linked page table pages
	for (int pn = 0; pn < PAGETABLE_NENTRIES; ++pn)
	    if (pagedir[pn] & PTE_P) {
		pageentry_t pte = pagedir[pn];
        assert(pageinfo[PAGENUMBER(pte)].owner == expected_owner);
		assert(pageinfo[PAGENUMBER(pte)].refcount == 1);
	    }
    }

    // Check that all referenced pages refer to active processes
    for (int pn = 0; pn < NPAGES; ++pn)
	if (pageinfo[pn].refcount > 0 && pageinfo[pn].owner >= 0)
	    assert(processes[pageinfo[pn].owner].p_state != P_FREE);
}


// memshow_physical
//    Draw a picture of physical memory on the CGA console.

static const uint16_t memstate_colors[] = {
    'K' | 0x0D00, 'R' | 0x0700, '.' | 0x0700,
    '1' | 0x0C00, '2' | 0x0A00, '3' | 0x0900, '4' | 0x0E00,
    '5' | 0x0F00, '6' | 0x0C00, '7' | 0x0A00, '8' | 0x0900, '9' | 0x0E00,
    'A' | 0x0F00, 'B' | 0x0C00, 'C' | 0x0A00, 'D' | 0x0900, 'E' | 0x0E00,
    'F' | 0x0F00
};

void memshow_physical(void) {
    console_printf(CPOS(0, 32), 0x0F00, "PHYSICAL MEMORY");
    for (int pn = 0; pn < NPAGES; ++pn) {
	if (pn % 64 == 0)
	    console_printf(CPOS(1 + pn / 64, 3), 0x0F00, "%08X ", pn << 12);

	int owner = pageinfo[pn].owner;
	if (pageinfo[pn].refcount == 0)
	    owner = PO_FREE;
	uint16_t color = memstate_colors[owner - PO_KERNEL];
	// darker color for shared pages
	if (pageinfo[pn].refcount > 1)
	    color &= 0x77FF;

	console[CPOS(1 + pn / 64, 12 + pn % 64)] = color;
    }
}


// memshow_virtual(pagedir, name)
//    Draw a picture of the virtual memory map `pagedir` (named `name`) on
//    the CGA console.

void memshow_virtual(pageentry_t *pagedir, const char *name) {
    assert((uintptr_t) pagedir == PTE_ADDR(pagedir));

    console_printf(CPOS(10, 26), 0x0F00, "VIRTUAL ADDRESS SPACE FOR %s", name);
    for (uintptr_t va = 0; va < MEMSIZE_VIRTUAL; va += PAGESIZE) {
	pageentry_t pte = virtual_memory_lookup(pagedir, va);
	uint16_t color;
	if (!pte)
	    color = ' ';
	else {
	    int owner = pageinfo[PAGENUMBER(pte)].owner;
	    if (pageinfo[PAGENUMBER(pte)].refcount == 0)
		owner = PO_FREE;
	    color = memstate_colors[owner - PO_KERNEL];
	    // reverse video for user-accessible pages
	    if (pte & PTE_U)
		color = ((color & 0x0F00) << 4) | ((color & 0xF000) >> 4)
		    | (color & 0x00FF);
	    // darker color for shared pages
	    if (pageinfo[PAGENUMBER(pte)].refcount > 1)
		color &= 0x77FF;
	}
	uint32_t pn = PAGENUMBER(va);
	if (pn % 64 == 0)
	    console_printf(CPOS(11 + pn / 64, 3), 0x0F00, "%08X ", va);
	console[CPOS(11 + pn / 64, 12 + pn % 64)] = color;
    }
}


// memshow_virtual_animate
//    Draw a picture of process virtual memory maps on the CGA console.
//    Starts with process 1, then switches to a new process every 0.25 sec.

void memshow_virtual_animate(void) {
    static unsigned last_ticks = 0;
    static int showing = 1;

    // switch to a new process every 0.25 sec
    if (last_ticks == 0 || ticks - last_ticks >= HZ / 4) {
	last_ticks = ticks;
	++showing;
    }

    // the current process may have died -- don't display it if so
    while (showing <= 2*NPROC && processes[showing % NPROC].p_state == P_FREE)
	++showing;
    showing = showing % NPROC;

    if (processes[showing].p_state != P_FREE) {
	char s[4];
	snprintf(s, 4, "%d ", showing);
	memshow_virtual(processes[showing].p_pagedir, s);
    }
}
