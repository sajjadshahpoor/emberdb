CC       := gcc
STD      := -std=gnu11
WARN     := -Wall -Wextra -Wpedantic
INCLUDES := -Iinclude
LDLIBS   := -lpthread

BUILD_DIR := build
BIN_DIR   := bin

CORE_SRCS := $(filter-out src/main.c src/ember_cli_main.c,$(wildcard src/*.c))
CORE_OBJS := $(patsubst src/%.c,$(BUILD_DIR)/obj/%.o,$(CORE_SRCS))

TEST_SRCS := $(wildcard tests/test_*.c)
TEST_BINS := $(patsubst tests/%.c,$(BUILD_DIR)/tests/%,$(TEST_SRCS))

ifeq ($(DEBUG),1)
CFLAGS  := $(STD) $(WARN) $(INCLUDES) -g -O0 -fsanitize=address,undefined
LDFLAGS := -fsanitize=address,undefined
else
CFLAGS  := $(STD) $(WARN) $(INCLUDES) -O2 -DNDEBUG
LDFLAGS :=
endif

.PHONY: all test debug release clean

all: $(BIN_DIR)/emberdb-server $(BIN_DIR)/emberdb-cli

debug:
	$(MAKE) DEBUG=1 all test

release: all

$(BUILD_DIR)/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/libember.a: $(CORE_OBJS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(BIN_DIR)/emberdb-server: src/main.c $(BUILD_DIR)/libember.a
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ -L$(BUILD_DIR) -lember $(LDLIBS) $(LDFLAGS)

$(BIN_DIR)/emberdb-cli: src/ember_cli_main.c $(BUILD_DIR)/libember.a
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ -L$(BUILD_DIR) -lember $(LDLIBS) $(LDFLAGS)

$(BUILD_DIR)/tests/%: tests/%.c $(BUILD_DIR)/libember.a
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@ -L$(BUILD_DIR) -lember $(LDLIBS) $(LDFLAGS)

test: $(TEST_BINS)
	@set -e; for t in $(TEST_BINS); do echo "== $$t =="; $$t; done

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
