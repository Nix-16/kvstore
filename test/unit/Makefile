CC=gcc
CFLAGS=-std=c11 -g -O0 -Wall -Wextra -I../../src

BUILD=build
TARGET=$(BUILD)/test_array

SRCS=../../src/kvs_array.c ../../src/kvs_alloc.c test_array.c

all: $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(TARGET): $(SRCS) | $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD)
