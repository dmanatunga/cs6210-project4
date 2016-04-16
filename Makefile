#### RVM Library Makefile

CFLAGS  = -Wall -g -I. -std=c++11 -fPIC
LFLAGS  =
CC      = g++
RM      = /bin/rm -rf
AR      = ar rc
RANLIB  = ranlib

STATIC_LIBRARY = librvm.a
SHARED_LIBRARY = librvm.so

LIB_SRC = rvm.cpp

LIB_OBJ = $(patsubst %.cpp,%.o,$(LIB_SRC))

all: $(STATIC_LIBRARY) $(SHARED_LIBRARY)

.PHONY: all

%.o: %.cpp rvm.h
	$(CC) -c $(CFLAGS) $< -o $@

$(STATIC_LIBRARY): $(LIB_OBJ)
	$(AR) $(STATIC_LIBRARY) $(LIB_OBJ)
	$(RANLIB) $(STATIC_LIBRARY)

$(SHARED_LIBRARY): $(LIB_OBJ)
	$(CC) -shared -o $(SHARED_LIBRARY) $(LIB_OBJ)

clean:
	$(RM) $(STATIC_LIBRARY) $(SHARED_LIBRARY) $(LIB_OBJ)
