# lsm-cache-simulator

TARGET   := lsm-sim
SRC_DIR  := src
INC_DIR  := include
BUILD_DIR := build
BIN_DIR  := bin

# Default to half the machine's cores (rounded up, min 1).
# An explicit -jN on the command line still overrides this.
NPROC := $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
JOBS  := $(shell echo $$(( ($(NPROC) + 1) / 2 )))
MAKEFLAGS += -j$(JOBS)

CXX      := g++
CXXSTD   := -std=c++20
WARN     := -Wall -Wextra -Wpedantic
OPT      := -O2
SAN      := -fsanitize=address,undefined
# Deferred (=) not immediate (:=) so `debug:`'s OPT override reaches the compile lines.
CXXFLAGS = $(CXXSTD) $(WARN) $(OPT) -I$(INC_DIR) -MMD -MP
LDFLAGS  :=
LDLIBS   := -lzstd

SRCS := $(wildcard $(SRC_DIR)/*.cc)
OBJS := $(patsubst $(SRC_DIR)/%.cc,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all debug clean run

all: $(BIN_DIR)/$(TARGET)

# Recursive rather than `debug: clean all`, which races under -j: clean can delete
# objects while all is compiling them.
debug:
	$(MAKE) clean
	$(MAKE) OPT="-O0 -g $(SAN)" LDFLAGS="$(LDFLAGS) $(SAN)"

$(BIN_DIR)/$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cc | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR) $(BIN_DIR):
	mkdir -p $@

run: all
	./$(BIN_DIR)/$(TARGET)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(BIN_DIR)/$(TARGET)

-include $(DEPS)
