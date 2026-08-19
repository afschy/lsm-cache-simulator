# lsm-cache-simulator

TARGET   := lsm-sim
SRC_DIR  := src
INC_DIR  := include
BUILD_DIR := build
BIN_DIR  := bin

CXX      := g++
CXXSTD   := -std=c++20
WARN     := -Wall -Wextra -Wpedantic
OPT      := -O2
CXXFLAGS := $(CXXSTD) $(WARN) $(OPT) -I$(INC_DIR) -MMD -MP
LDFLAGS  :=
LDLIBS   := -lzstd

SRCS := $(wildcard $(SRC_DIR)/*.cc)
OBJS := $(patsubst $(SRC_DIR)/%.cc,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all debug clean run

all: $(BIN_DIR)/$(TARGET)

debug: OPT := -O0 -g -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: clean all

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
