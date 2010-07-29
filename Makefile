CROSS_COMPILE ?=

CC	:= $(CROSS_COMPILE)gcc
CFLAGS	?= -O2 -W -Wall
LDFLAGS	?=

%.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<

all: yavta

yavta: yavta.o
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	-rm -f *.o
	-rm -f yavta

