ifneq (DEBUG, "-g")
	DEBUG += "-O2"
endif

IDIR = ../
CC = gcc
CFLAGS = -ggdb -Wall $(DEBUG) -I$(IDIR) -std=gnu11
CXXFLAGS = -ggdb -Wall $(DEBUG) -I$(IDIR) -std=c++11
LDFLAGS = -lrvm -L../

EXEC = abort \
       basic \
       multi-abort \
       multi truncate \
       test01 \
       test02 \
       test03 \
       test04 \
       test05 \
       test06 \
       test07 \
       test08 \
       test09 \
       test10 \
       test11 \
       test12 \
       test13 \
       test14 \
       test16 \
       test17 \
       test18 \
       test19

CXX_EXEC = test15 test20

all: $(EXEC) $(CXX_EXEC)

.PHONY: all 

$(EXEC): %: %.o
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS)

$(CXX_EXEC): %: %.o
	$(CXX) $< -o $@ $(CXXFLAGS) $(LDFLAGS)

%.o: %.c %.h
	$(CC) -o $@ -c $< $(CFLAGS)

%.o: %.cc %.h
	$(CXX) -o $@ -c $< $(CXXFLAGS)

.PHONY: debug
debug: clean
	$(MAKE) $(MAKEFILE) all DEBUG="-g"

.PHONY: clean
clean:
	rm -f *.o $(EXEC) $(CXX_EXEC)
