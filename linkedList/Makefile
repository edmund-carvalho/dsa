CC = gcc
CFLAGS = -Wall -Wextra -O2 -DDS_USE_THREAD_SAFETY_POSIX
LDFLAGS = -pthread

TARGET = testapp.exe
SRCS = main.c linkedList.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c linkedList.h
	$(CC) $(CFLAGS) -c $< -o $@

# This will now safely work because PowerShell understands 'rm' natively!
clean:
	rm -f *.o $(TARGET)

.PHONY: all clean