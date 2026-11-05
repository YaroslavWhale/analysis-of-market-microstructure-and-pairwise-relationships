#include "book/OrderBook.hpp"
#include <algorithm>
#include <stdexcept>

OrderBook::OrderBook(std::string sym, uint64_t depth)
    : symbol(std::move(sym)), depth(depth) {}

void OrderBook::set_price_multiplier(uint64_t mult) { price_multiplier = mult; }
void OrderBook::set_volume_multiplier(uint64_t mult) { volume_multiplier = mult; }
uint64_t OrderBook::get_price_multiplier() const { return price_multiplier; }
uint64_t OrderBook::get_volume_multiplier() const { return volume_multiplier; }
bool OrderBook::is_initialized() const { return isInitialized; }

uint64_t OrderBook::get_last_update_id() const {
    std::lock_guard<std::mutex> lock(mtx);
    return localLastUpdateId;
}

int64_t OrderBook::get_best_bid_price() const { return best_bid_price; }
int64_t OrderBook::get_best_ask_price() const { return best_ask_price; }
int64_t OrderBook::get_best_bid_volume() const { return best_bid_volume; }
int64_t OrderBook::get_best_ask_volume() const { return best_ask_volume; }

void OrderBook::snapshot(const SnapshotData& snap) {
    std::lock_guard<std::mutex> lock(mtx);
    snapshotLastUpdateId = snap.lastUpdateId;
    localLastUpdateId = snap.lastUpdateId;

    bids = snap.bids;
    asks = snap.asks;

    if (bids.size() > depth) bids.resize(depth);
    if (asks.size() > depth) asks.resize(depth);

    updateBestPrices();
    isInitialized = true;
}

void OrderBook::applyUpdate(const DepthUpdate& update) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!isInitialized) return;

    if (update.finalUpdateId <= localLastUpdateId) return;

    if (update.firstUpdateId > localLastUpdateId + 1) {
        throw std::runtime_error("Out of sync: need to re-snapshot");
    }

    for (const auto& level : update.bids) {
        if (level.volume == 0)
            removeLevel(bids, level.price);
        else
            addOrUpdateLevel(bids, level.price, level.volume, true);
    }

    for (const auto& level : update.asks) {
        if (level.volume == 0)
            removeLevel(asks, level.price);
        else
            addOrUpdateLevel(asks, level.price, level.volume, false);
    }

    localLastUpdateId = update.finalUpdateId;
    updateBestPrices();
}

void OrderBook::updateBestPrices() {
    best_bid_price = bids.empty() ? 0 : bids.front().price;
    best_bid_volume = bids.empty() ? 0 : bids.front().volume;
    best_ask_price = asks.empty() ? 0 : asks.front().price;
    best_ask_volume = asks.empty() ? 0 : asks.front().volume;
}

void OrderBook::addOrUpdateLevel(std::vector<Level>& levels, uint64_t price, uint64_t volume, bool isBid) {
    auto comp = [isBid](const Level& a, uint64_t p) {
        return isBid ? (a.price > p) : (a.price < p);
    };

    auto it = std::lower_bound(levels.begin(), levels.end(), price, comp);
    if (it != levels.end() && it->price == price) {
        it->volume = volume;
        return;
    }

    levels.insert(it, {price, volume});
    if (levels.size() > depth) levels.resize(depth);
}

void OrderBook::removeLevel(std::vector<Level>& levels, uint64_t price) {
    auto it = std::find_if(levels.begin(), levels.end(),
                           [price](const Level& l) { return l.price == price; });
    if (it != levels.end()) levels.erase(it);
}

std::vector<Level> OrderBook::get_bids() const {
    std::lock_guard<std::mutex> lock(mtx);
    return bids;
}

std::vector<Level> OrderBook::get_asks() const {
    std::lock_guard<std::mutex> lock(mtx);
    return asks;
}
