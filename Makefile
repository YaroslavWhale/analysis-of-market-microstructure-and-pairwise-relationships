# ---------------------------------------------------------------------------
# OrderBook / pairwise-cointegration analysis tool
# ---------------------------------------------------------------------------
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Isrc
LDFLAGS  ?= -pthread
LDLIBS   ?= -lcpr -lboost_system -lssl -lcrypto

TARGET   := main

SRCS := src/main.cpp \
        src/book/OrderBook.cpp \
        src/book/OrderBookCore.cpp \
        src/net/OrderBookClient.cpp \
        src/app/TradingSystem.cpp \
        src/filters/kalman_filter_2d.cpp \
        src/analysis/pair_analyzer.cpp
OBJS := $(SRCS:.cpp=.o)

# Tests only need the filter + analysis code (no networking, no cpr).
TEST_SRCS  := tests/analysis_tests.cpp \
              src/filters/kalman_filter_2d.cpp \
              src/analysis/pair_analyzer.cpp
TEST_TARGET := analysis_tests

.PHONY: all clean check demo run pair

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_TARGET): $(TEST_SRCS)
	$(CXX) $(CXXFLAGS) $(TEST_SRCS) -o $@ $(LDFLAGS)

# Offline validation of the Kalman filter + analysis pipeline.
check: $(TEST_TARGET)
	./$(TEST_TARGET)

# Offline demonstration on synthetic cointegrated data.
demo: $(TARGET)
	./$(TARGET) -demo

# Live single-symbol order book.
run: $(TARGET)
	./$(TARGET) -sym BTCUSDT -depth 10

# Live pairwise cointegration analysis.
pair: $(TARGET)
	./$(TARGET) -sym BTCUSDT -sym2 ETHUSDT -depth 10 -window 200 -csv pair.csv

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_TARGET)
