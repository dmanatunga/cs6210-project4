ifneq (DEBUG, "-g")
	DEBUG += "-O2"
endif

IDIR = ../
CC = gcc
CFLAGS = -ggdb -Wall $(DEBUG) -I$(IDIR) -std=gnu11
LDFLAGS = -lrvm -L../

EXEC = abort basic multi-abort multi truncate test01 test02 test03 test04 test05

all: $(EXEC)

.PHONY: all 

$(EXEC): %: %.o
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS)

%.o: %.c %.h
	$(CC) -o $@ -c $< $(CFLAGS)

.PHONY: debug
debug: clean
	$(MAKE) $(MAKEFILE) all DEBUG="-g"

.PHONY: clean
clean:
	rm -f *.o $(EXEC)
