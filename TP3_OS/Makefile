CC = gcc
CFLAGS = -Wall -Wextra -D_REENTRANT
LIBS = -lreadline -lpthread

TARGET = biceps
SRCS = biceps.c gescom.c creme.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

memory-leak: clean
	$(CC) -g -O0 -Wall -DTRACE -o $(TARGET)-memleak $(SRCS) $(LIBS)
	$(CC) -g -O0 -Wall -DTRACE -o $(TARGET) $(SRCS) $(LIBS)

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET)-memleak

.PHONY: all clean memory-leak