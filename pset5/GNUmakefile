PROGRAMS = pong61
all: $(PROGRAMS)

CC = $(shell if test -f /opt/local/bin/gcc-mp-4.7; then \
	    echo gcc-mp-4.7; else echo gcc; fi)
CFLAGS := -std=gnu99 -g -W -Wall -Werror -O2 -pthread
DEPCFLAGS = -MD -MF $(DEPSDIR)/$*.d -MP

-include build/rules.mk

%.o: %.c $(REBUILDSTAMP)
	$(CC) $(CFLAGS) $(DEPCFLAGS) -o $@ -c $<

all:
	@:

$(PROGRAMS): %: %.o
	$(CC) $(CFLAGS) -o $@ $^

clean: clean-main clean-hook
clean-main:
	rm -f $(PROGRAMS) *.o core *.core
	rm -rf out $(DEPSDIR)

distclean: clean
	rm -rf $(DEPSDIR)

.PRECIOUS: %.o
.PHONY: all clean clean-main clean-hook distclean check check-% prepare-check
