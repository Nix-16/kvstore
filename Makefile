CC      := gcc
AR      := ar
CFLAGS  := -Wall -Wextra -O2 -g -Isrc
LDFLAGS := -lpthread

BUILD_DIR := build
BIN_DIR   := bin

# ---- library sources ----
LIB_SRCS := \
  src/buffer.c \
  src/reactor.c \
  src/kvs_alloc.c \
  src/kvs_array.c \
  src/kvs_hash.c \
  src/kvs_rbtree.c \
  src/kvs_config.c \
  kvstore.c

LIB_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(LIB_SRCS))
LIB_A    := $(BUILD_DIR)/libkvstore.a

# ---- examples ----
ECHO_BIN  := $(BIN_DIR)/echo_server
ECHO_SRC  := example/echo_server.c
ECHO_OBJ  := $(BUILD_DIR)/example/echo_server.o

# ---- unit tests ----
UNIT_SRCS := \
  test/unit/test_buffer.c \
  test/unit/test_array.c \
  test/unit/test_hash.c \
  test/unit/test_rbtree.c

UNIT_BINS := $(patsubst test/unit/%.c,$(BIN_DIR)/%,$(UNIT_SRCS))
UNIT_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(UNIT_SRCS))

.PHONY: all clean test run-echo

all: $(LIB_A) $(ECHO_BIN)

# ---- build dirs ----
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# ---- compile rule (keep directory structure) ----
$(BUILD_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ---- static library ----
$(LIB_A): $(BUILD_DIR) $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

# ---- echo server ----
$(ECHO_BIN): $(BIN_DIR) $(LIB_A) $(ECHO_OBJ)
	$(CC) $(CFLAGS) $(ECHO_OBJ) $(LIB_A) -o $@ $(LDFLAGS)

run-echo: $(ECHO_BIN)
	./$(ECHO_BIN) 6379

# ---- unit tests (each test is a standalone binary) ----
$(BIN_DIR)/test_%: $(LIB_A) $(BUILD_DIR)/test/unit/test_%.o | $(BIN_DIR)
	$(CC) $(CFLAGS) $< $(BUILD_DIR)/test/unit/test_$*.o -o $@ $(LDFLAGS)

test: $(UNIT_BINS)
	@set -e; \
	for t in $(UNIT_BINS); do \
		echo "==> $$t"; \
		./$$t; \
	done

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)