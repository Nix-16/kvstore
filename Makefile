CC      := gcc
AR      := ar

# ---------------- jemalloc (git submodule) ----------------
JEMALLOC_DIR := third_party/jemalloc
JEMALLOC_INC := -I$(JEMALLOC_DIR)/include
JEMALLOC_LIB := $(JEMALLOC_DIR)/lib/libjemalloc.a

# ---------------- flags ----------------
CFLAGS  := -Wall -Wextra -O2 -g -I. -Isrc $(JEMALLOC_INC)
# jemalloc 静态库通常还需要 -ldl；你原来有 -lpthread
LDFLAGS := -lpthread -ldl $(JEMALLOC_LIB)

BUILD_DIR := build
BIN_DIR   := bin

# ---- library sources (NO kvstore.c here; it has main now) ----
LIB_SRCS := \
  src/buffer.c \
  src/reactor.c \
  src/resp.c \
  src/resp_reply.c \
  src/kvs_alloc.c \
  src/kvs_array.c \
  src/kvs_hash.c \
  src/kvs_rbtree.c \
  src/kvs_config.c \
  src/kvs_aof.c	\
  src/kvs_snapshot.c

LIB_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(LIB_SRCS))
LIB_A    := $(BUILD_DIR)/libkvstore.a

# ---- server binary ----
KVS_BIN := $(BIN_DIR)/kvstore_server
KVS_OBJ := $(BUILD_DIR)/kvstore.o


# ---- unit tests ----
UNIT_SRCS := \
  test/unit/test_buffer.c \
  test/unit/test_array.c \
  test/unit/test_hash.c \
  test/unit/test_rbtree.c

UNIT_BINS := $(patsubst test/unit/%.c,$(BIN_DIR)/%,$(UNIT_SRCS))

.PHONY: all clean test run-echo run-kvs jemalloc

all: jemalloc $(LIB_A) $(KVS_BIN)

# ---------------- common dirs ----------------
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ---------------- jemalloc build ----------------
jemalloc: $(JEMALLOC_LIB)

$(JEMALLOC_LIB):
	cd $(JEMALLOC_DIR) && ./autogen.sh
	cd $(JEMALLOC_DIR) && ./configure --disable-cxx --enable-static --disable-shared
	$(MAKE) -C $(JEMALLOC_DIR) -j

# ---------------- lib ----------------
$(LIB_A): jemalloc $(BUILD_DIR) $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

# ---------------- kvstore server ----------------
$(KVS_BIN): $(BIN_DIR) $(LIB_A) $(KVS_OBJ)
	$(CC) $(CFLAGS) $(KVS_OBJ) $(LIB_A) -o $@ $(LDFLAGS)

run-kvs: $(KVS_BIN)
	./$(KVS_BIN)

# ---------------- unit tests ----------------
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
	rm -rf appendonly.aof
	rm -rf dump.kvs