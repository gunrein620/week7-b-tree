CC = gcc
BASE_CFLAGS = -std=c99 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -I./src
RELEASE_FLAGS = -O2
DEBUG_FLAGS = -g -DDEBUG

BUILD_DIR = build
TEST_BUILD_DIR = $(BUILD_DIR)/tests
EDGE_TEST_BUILD_DIR = $(BUILD_DIR)/edgecase_test

SRCS = $(wildcard src/*.c)
MAIN_SRC = src/main.c
OBJS = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS))
LIB_OBJS = $(filter-out $(BUILD_DIR)/main.o,$(OBJS))

TEST_SRCS = $(wildcard tests/test_*.c)
TEST_OBJS = $(patsubst tests/%.c,$(TEST_BUILD_DIR)/%.o,$(TEST_SRCS))
EDGE_TEST_SRCS = $(wildcard edgecase_test/test_*.c)
EDGE_TEST_OBJS = $(patsubst edgecase_test/%.c,$(EDGE_TEST_BUILD_DIR)/%.o,$(EDGE_TEST_SRCS))

CFLAGS = $(BASE_CFLAGS) $(RELEASE_FLAGS)

ifeq ($(OS),Windows_NT)
EXE_EXT = .exe
SQLENGINE_BIN = sqlengine.exe
GEN_MEMBERS_BIN = tools/gen_members.exe
else
EXE_EXT =
SQLENGINE_BIN = sqlengine.bin
GEN_MEMBERS_BIN = tools/gen_members.bin
endif

TEST_BINS = $(patsubst tests/%.c,$(TEST_BUILD_DIR)/%$(EXE_EXT),$(TEST_SRCS))
EDGE_TEST_BINS = $(patsubst edgecase_test/%.c,$(EDGE_TEST_BUILD_DIR)/%$(EXE_EXT),$(EDGE_TEST_SRCS))

.PHONY: all debug test edge-test clean help run-f directories tools

all: CFLAGS = $(BASE_CFLAGS) $(RELEASE_FLAGS)
all: directories $(SQLENGINE_BIN)

debug: CFLAGS = $(BASE_CFLAGS) $(DEBUG_FLAGS)
debug: directories $(SQLENGINE_BIN)

test: CFLAGS = $(BASE_CFLAGS) $(DEBUG_FLAGS)
test: directories $(SQLENGINE_BIN) $(TEST_BINS)
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

edge-test: CFLAGS = $(BASE_CFLAGS) $(DEBUG_FLAGS)
edge-test: directories $(SQLENGINE_BIN) $(EDGE_TEST_BINS)
ifeq ($(OS),Windows_NT)
	@powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_edge_tests.ps1 $(foreach test,$(EDGE_TEST_BINS),"$(test)")
else
	@passed=0; failed=0; \
	for test_bin in $(EDGE_TEST_BINS); do \
		$$test_bin; \
		status=$$?; \
		if [ $$status -eq 0 ]; then \
			passed=$$((passed + 1)); \
		else \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo "Edge test executables: $$passed passed, $$failed failed"; \
	test $$failed -eq 0
endif

help:
	@echo "Targets:"
	@echo "  all      Build sqlengine"
	@echo "  debug    Build sqlengine with debug flags"
	@echo "  test     Build and run unit tests"
	@echo "  edge-test Build and run edge-case tests"
	@echo "  clean    Remove build artifacts"
	@echo "  run-f    Run ./sqlengine -f \$$SQL"
	@echo "  help     Show this help message"

run-f: all
	@if [ -z "$(SQL)" ]; then \
		echo "Usage: make run-f SQL=sql/members_demo.sql"; \
		exit 1; \
	fi
	./sqlengine -f $(SQL)

sqlengine: $(SQLENGINE_BIN)

$(SQLENGINE_BIN): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

tools: $(GEN_MEMBERS_BIN)

$(GEN_MEMBERS_BIN): tools/gen_members.c | directories
	$(CC) $(BASE_CFLAGS) $(RELEASE_FLAGS) $< -o $@

$(BUILD_DIR)/%.o: src/%.c | directories
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(TEST_BUILD_DIR)/%.o: tests/%.c | directories
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(EDGE_TEST_BUILD_DIR)/%.o: edgecase_test/%.c | directories
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I./tests -MMD -MP -c $< -o $@

$(TEST_BUILD_DIR)/%$(EXE_EXT): $(TEST_BUILD_DIR)/%.o $(LIB_OBJS) | directories $(SQLENGINE_BIN)
	$(CC) $(CFLAGS) $^ -o $@

$(EDGE_TEST_BUILD_DIR)/%$(EXE_EXT): $(EDGE_TEST_BUILD_DIR)/%.o $(LIB_OBJS) | directories $(SQLENGINE_BIN)
	$(CC) $(CFLAGS) $^ -o $@

directories:
	@mkdir -p $(BUILD_DIR) $(TEST_BUILD_DIR) $(EDGE_TEST_BUILD_DIR) data schemas sql

clean:
	rm -rf build sqlengine.bin sqlengine.exe tools/gen_members.exe tools/gen_members.bin
	-rm -f sqlengine.exe.tmp*

-include $(OBJS:.o=.d)
-include $(TEST_OBJS:.o=.d)
-include $(EDGE_TEST_OBJS:.o=.d)
