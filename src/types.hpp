#pragma once

#include <vector>
#include <cstdint>

struct Level{
	uint64_t price;
	uint64_t volume;
};

// struct ClintConfig{};

struct SnapshotData {
    uint64_t lastUpdateId;
    std::vector<Level> bids;
    std::vector<Level> asks;
};

struct Multipliers {
    uint64_t price;
    uint64_t volume;
};

struct DepthUpdate {
    uint64_t firstUpdateId;
    uint64_t finalUpdateId;
    std::vector<Level> bids;
    std::vector<Level> asks;
};
