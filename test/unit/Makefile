CC=gcc
CFLAGS=-std=c11 -g -O0 -Wall -Wextra -I../../src

BUILD=build

TARGET_ARRAY=$(BUILD)/test_array
TARGET_HASH=$(BUILD)/test_hash

SRCS_COMMON=../../src/kvs_alloc.c
SRCS_ARRAY=../../src/kvs_array.c test_array.c
SRCS_HASH=../../src/kvs_hash.c test_hash.c

all: $(TARGET_ARRAY) $(TARGET_HASH)

$(BUILD):
	mkdir -p $(BUILD)

$(TARGET_ARRAY): $(SRCS_COMMON) $(SRCS_ARRAY) | $(BUILD)
	$(CC) $(CFLAGS) $(SRCS_COMMON) $(SRCS_ARRAY) -o $(TARGET_ARRAY)

$(TARGET_HASH): $(SRCS_COMMON) $(SRCS_HASH) | $(BUILD)
	$(CC) $(CFLAGS) $(SRCS_COMMON) $(SRCS_HASH) -o $(TARGET_HASH)

run_array: $(TARGET_ARRAY)
	./$(TARGET_ARRAY)

run_hash: $(TARGET_HASH)
	./$(TARGET_HASH)

run: run_array run_hash

clean:
	rm -rf $(BUILD)
