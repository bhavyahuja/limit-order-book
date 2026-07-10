CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS ?=

BUILD_DIR := build
HEADERS := $(wildcard include/lob/*.hpp)
LIB_SRCS := src/order_book.cpp
ENGINE_SRCS := src/main.cpp $(LIB_SRCS)
TEST_SRCS := tests/test_order_book.cpp $(LIB_SRCS)

.PHONY: all clean test engine

all: $(BUILD_DIR)/lob_engine $(BUILD_DIR)/lob_tests

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/lob_engine: $(ENGINE_SRCS) $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(ENGINE_SRCS) $(LDFLAGS)

$(BUILD_DIR)/lob_tests: $(TEST_SRCS) $(HEADERS) third_party/catch.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -Ithird_party -o $@ $(TEST_SRCS) $(LDFLAGS)

engine: $(BUILD_DIR)/lob_engine

test: $(BUILD_DIR)/lob_tests
	$(BUILD_DIR)/lob_tests

clean:
	rm -rf $(BUILD_DIR)
