# Makefile for Multicore Load Balancing Scheduler

CC = gcc
CFLAGS = -O2 -Wall -D_CRT_SECURE_NO_WARNINGS -Iinclude
LDFLAGS = -lkernel32 -ladvapi32 -lws2_32

TARGET = scheduler.exe
SOURCES = main.c src/kernel_detect.c src/scheduler.c src/ui.c src/gui.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	del /Q *.o src\*.o 2>nul
	del /Q $(TARGET) 2>nul

run: $(TARGET)
	$(TARGET)

.PHONY: all clean run