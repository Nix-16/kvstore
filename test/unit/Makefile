CC=gcc
CFLAGS=-std=c11 -g -O0 -Wall -Wextra -I../../src

BUILD=build

TARGET_ARRAY=$(BUILD)/test_array
TARGET_HASH=$(BUILD)/test_hash
TARGET_RBTREE=$(BUILD)/test_rbtree
TARGET_BUFFER=$(BUILD)/test_buffer
SRCS_COMMON=../../src/kvs_alloc.c
SRCS_ARRAY=../../src/kvs_array.c test_array.c
SRCS_HASH=../../src/kvs_hash.c test_hash.c
SRCS_RBTREE=../../src/kvs_rbtree.c test_rbtree.c
SRCS_BUFFER=../../src/buffer.c test_buffer.c

all: $(TARGET_ARRAY) $(TARGET_HASH) $(TARGET_RBTREE) $(TARGET_BUFFER)

$(BUILD):
	mkdir -p $(BUILD)

$(TARGET_ARRAY): $(SRCS_COMMON) $(SRCS_ARRAY) | $(BUILD)
	$(CC) $(CFLAGS) $(SRCS_COMMON) $(SRCS_ARRAY) -o $(TARGET_ARRAY)

$(TARGET_HASH): $(SRCS_COMMON) $(SRCS_HASH) | $(BUILD)
	$(CC) $(CFLAGS) $(SRCS_COMMON) $(SRCS_HASH) -o $(TARGET_HASH)

$(TARGET_RBTREE): $(SRCS_COMMON) $(SRCS_RBTREE) | $(BUILD)
	$(CC) $(CFLAGS) $(SRCS_COMMON) $(SRCS_RBTREE) -o $(TARGET_RBTREE)

$(TARGET_BUFFER): $(SRCS_COMMON) $(SRCS_BUFFER) | $(BUILD)
	$(CC) $(CFLAGS) $(SRCS_COMMON) $(SRCS_BUFFER) -o $(TARGET_BUFFER)

run_array: $(TARGET_ARRAY)
	./$(TARGET_ARRAY)

run_hash: $(TARGET_HASH)
	./$(TARGET_HASH)

run_rbtree: $(TARGET_RBTREE)
	./$(TARGET_RBTREE)

run_buffer: $(TARGET_BUFFER)
	./$(TARGET_BUFFER)

run: run_array run_hash run_rbtree run_buffer

clean:
	rm -rf $(BUILD)
