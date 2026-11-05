#pragma once
#include "types.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

Multipliers load_multiplier(const std::string& symbol);
SnapshotData get_snapshot(const std::string& symbol,
                          uint64_t price_mult,
                          uint64_t volume_mult,
                          int limit = 1000);

// Single-symbol mode (behaviour identical to the original program).
void run(std::string symbol, uint64_t depth);

// Pair mode: stream two order books, estimate the dynamic hedge ratio with a
// Kalman filter and print the cointegration analysis every sampling period.
// If csv_path is non-empty the per-sample rows are also appended to that file.
void run_pair(std::string symbol1,
              std::string symbol2,
              uint64_t depth,
              std::size_t window,
              const std::string& csv_path,
              double entry_z,
              double exit_z);

// Offline self-test / demonstration on a synthetic cointegrated series pair.
// Requires no network access and is used to validate the analysis pipeline.
void run_demo(std::size_t window, double entry_z, double exit_z);
