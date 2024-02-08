# CS3102 Coursework P2 : Simple, Reliable Transport Protocol (SRTP)
#
# saleem (edited Jan 2024, Feb 2023, Jan 2022, Jan 2021)
# sjm55 (checked February 2024)
# 210016688 (edited March 2024)

CC	=clang
#CC	=gcc
CC-flags	=-Wall -g -Isrc #-DOBFUSCATE #-DDEBUG

PROGRAMS	= test-client-0 test-server-0 \
						test-client-1 test-server-1 \
						test-client-2 test-server-2 \
						test-client-3 test-server-3
H-files	= srtp.h srtp-common.h srtp-pcb.h byteorder64.h d_print.h
LIB-files	=srtp.o srtp-common.o srtp-pcb.o srtp-fsm.o byteorder64.o d_print.o

.SUFFIXES:	.h .c .o
.c.o:;	$(CC) $(CC-flags) -c $<

vpath test-%.h test
vpath test-%.c test
vpath %.h	src
vpath %.c	src

all: $(PROGRAMS) libsrtp.a

# Create the SRTP library
libsrtp.a: $(LIB-files)
	ar ruv $@ $+
srtp.c:	$(H-files) srtp-packet.h srtp-fsm.h
srtp-common.c: srtp-common.h
srtp-fsm.c: srtp-fsm.h srtp-fsm.h
srtp-pcb.c: srtp-pcb.h srtp-fsm.h
byteorder64.c:	byteorder64.h
d_print.c:	d_print.h

# Test 0 - Send and receive a single packet
test-client-0: test-client-0.o libsrtp.a
test-client-0.c: $(H-files)
test-server-0: test-server-0.o libsrtp.a
test-server-0.c: $(H-files)

# Test 1 - Client download
test-client-1: test-client-1.o libsrtp.a
test-client-1.c: $(H-files)
test-server-1: test-server-1.o libsrtp.a
test-server-1.c: $(H-files)

# Test 2 - Client upload
test-client-2: test-client-2.o libsrtp.a
test-client-2.c: $(H-files)
test-server-2: test-server-2.o libsrtp.a
test-server-2.c: $(H-files)

# Test 3 - Client-server exchange
test-client-3: test-client-3.o libsrtp.a
test-client-3.c: $(H-files)
test-server-3: test-server-3.o libsrtp.a
test-server-3.c: $(H-files)

.PHONY:	clean
clean:; rm -rf *.o *.a $(PROGRAMS) *~
