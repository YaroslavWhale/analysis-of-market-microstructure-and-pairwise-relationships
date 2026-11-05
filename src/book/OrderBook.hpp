#pragma once
#include "types.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>

class OrderBook {
public:
    OrderBook(std::string sym, uint64_t depth);

    void set_price_multiplier(uint64_t mult);
    void set_volume_multiplier(uint64_t mult);
    void snapshot(const SnapshotData& snap);
    void applyUpdate(const DepthUpdate& update);

    int64_t get_best_bid_price() const;
    int64_t get_best_ask_price() const;
    int64_t get_best_bid_volume() const;
    int64_t get_best_ask_volume() const;
    uint64_t get_price_multiplier() const;
    uint64_t get_volume_multiplier() const;
    uint64_t get_last_update_id() const;
    bool is_initialized() const;

    std::vector<Level> get_bids() const;
    std::vector<Level> get_asks() const;

private:
    std::string symbol;
    uint64_t depth;

    std::vector<Level> bids;
    std::vector<Level> asks;

    uint64_t price_multiplier = 1;
    uint64_t volume_multiplier = 1;

    int64_t best_bid_price = 0;
    int64_t best_ask_price = 0;
    int64_t best_bid_volume = 0;
    int64_t best_ask_volume = 0;

    uint64_t snapshotLastUpdateId = 0;
    uint64_t localLastUpdateId = 0;

    bool isInitialized = false;
    mutable std::mutex mtx;

    void updateBestPrices();
    void addOrUpdateLevel(std::vector<Level>& levels, uint64_t price, uint64_t volume, bool isBid);
    void removeLevel(std::vector<Level>& levels, uint64_t price);
};
