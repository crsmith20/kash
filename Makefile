bin=kash

CFLAGS += -Wall -g
LDFLAGS +=

src=shell.c history.c timer.c
obj=$(src:.c=.o)

all: $(bin)

$(bin): $(obj)
	$(CC) $(CFLAGS) $(LDFLAGS) $(obj) -o $@

history.o: history.c history.h
shell.o: shell.c
timer.o: timer.c timer.h

clean:
	rm -f $(bin) $(obj)

