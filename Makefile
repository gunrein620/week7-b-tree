CC = gcc
BASE_CFLAGS = -std=c99 -Wall -Wextra -I./src
RELEASE_FLAGS = -O2
DEBUG_FLAGS = -g -DDEBUG

BUILD_DIR = build
TEST_BUILD_DIR = $(BUILD_DIR)/tests

SRCS = $(wildcard src/*.c)
MAIN_SRC = src/main.c
OBJS = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS))
LIB_OBJS = $(filter-out $(BUILD_DIR)/main.o,$(OBJS))

TEST_SRCS = $(wildcard tests/test_*.c)
TEST_OBJS = $(patsubst tests/%.c,$(TEST_BUILD_DIR)/%.o,$(TEST_SRCS))
TEST_BINS = $(patsubst tests/%.c,$(TEST_BUILD_DIR)/%,$(TEST_SRCS))

CFLAGS = $(BASE_CFLAGS) $(RELEASE_FLAGS)

.PHONY: all debug test clean help run-f directories tools

all: CFLAGS = $(BASE_CFLAGS) $(RELEASE_FLAGS)
all: directories sqlengine

debug: CFLAGS = $(BASE_CFLAGS) $(DEBUG_FLAGS)
debug: directories sqlengine

test: CFLAGS = $(BASE_CFLAGS) $(DEBUG_FLAGS)
test: directories sqlengine $(TEST_BINS)
	@passed=0; failed=0; \
	for test_bin in $(TEST_BINS); do \
		$$test_bin; \
		if [ $$? -eq 0 ]; then \
			passed=$$((passed + 1)); \
		else \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo "Test executables: $$passed passed, $$failed failed"; \
	test $$failed -eq 0

help:
	@echo "Targets:"
	@echo "  all      Build sqlengine"
	@echo "  debug    Build sqlengine with debug flags"
	@echo "  test     Build and run unit tests"
	@echo "  clean    Remove build artifacts"
	@echo "  run-f    Run ./sqlengine -f \$$SQL"
	@echo "  help     Show this help message"

run-f: all
	@if [ -z "$(SQL)" ]; then \
		echo "Usage: make run-f SQL=sql/members_demo.sql"; \
		exit 1; \
	fi
	./sqlengine -f $(SQL)

sqlengine: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

tools: tools/gen_members

tools/gen_members: tools/gen_members.c | directories
	$(CC) $(BASE_CFLAGS) $(RELEASE_FLAGS) $< -o $@

$(BUILD_DIR)/%.o: src/%.c | directories
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(TEST_BUILD_DIR)/%.o: tests/%.c | directories
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(TEST_BUILD_DIR)/%: $(TEST_BUILD_DIR)/%.o $(LIB_OBJS) | directories sqlengine
	$(CC) $(CFLAGS) $^ -o $@

directories:
	@mkdir -p $(BUILD_DIR) $(TEST_BUILD_DIR) data schemas sql

clean:
	rm -rf build sqlengine

-include $(OBJS:.o=.d)
-include $(TEST_OBJS:.o=.d)
