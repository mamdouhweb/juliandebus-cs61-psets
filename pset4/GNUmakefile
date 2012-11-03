TESTS = cat61 blockcat61 randomcat61 reordercat61 \
	stridecat61 ostridecat61 reverse61 pipeexchange61
SLOWTESTS = $(patsubst %,slow-%,$(TESTS))
STDIOTESTS = $(patsubst %,stdio-%,$(TESTS))
all: $(TESTS) $(SLOWTESTS) $(STDIOTESTS)

CC = $(shell if test -f /opt/local/bin/gcc-mp-4.7; then \
	    echo gcc-mp-4.7; else echo gcc; fi)
CFLAGS := -std=gnu99 -g -W -Wall -Werror -O2
DEPCFLAGS = -MD -MF $(DEPSDIR)/$*.d -MP

-include build/rules.mk

%.o: %.c io61.h $(REBUILDSTAMP)
	$(CC) $(CFLAGS) $(DEPCFLAGS) -o $@ -c $<

all:
	@echo "*** Run 'make check' to check your work."

$(TESTS): %: io61.o %.o
	$(CC) $(CFLAGS) -o $@ $^

$(SLOWTESTS): slow-%: slow-io61.o %.o
	$(CC) $(CFLAGS) -o $@ $^

$(STDIOTESTS): stdio-%: stdio-io61.o %.o
	$(CC) $(CFLAGS) -o $@ $^

text20meg.txt:
	echo > text20meg.txt
	while perl -e "exit((-s 'text20meg.txt') > 20000000)"; do cat /usr/share/dict/words >> text20meg.txt; done

clean:
	rm -f $(TESTS) $(SLOWTESTS) hhtest *.o
	rm -rf out $(DEPSDIR)

check: $(TESTS) $(STDIOTESTS)
	perl check.pl

check-%: $(TESTS) $(STDIOTESTS)
	perl check.pl $(subst check-,,$@)

.PRECIOUS: %.o
.PHONY: all clean check check-% prepare-check
