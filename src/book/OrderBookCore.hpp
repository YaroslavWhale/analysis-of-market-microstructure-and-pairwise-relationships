#pragma once

#include "book/OrderBook.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// -----------------------------------------------------------------------------
// Registry of order books, one per symbol.
//
// The original single-book program only ever needed one OrderBook.  Pair
// analysis needs at least two symbols kept alive simultaneously, so they are
// stored here under a mutex and handed out as shared_ptr (the websocket
// callbacks keep their own reference for the lifetime of the feed).
// -----------------------------------------------------------------------------

class OrderBookCore {
public:
    // Return the existing book for `symbol` or create a new one with the given
    // depth.  The depth argument is only used when the book is created.
    std::shared_ptr<OrderBook> get_or_create(const std::string& symbol, uint64_t depth);

    // Return the book for `symbol`, or nullptr if it does not exist.
    std::shared_ptr<OrderBook> get(const std::string& symbol) const;

    bool contains(const std::string& symbol) const;

    std::vector<std::string> symbols() const;

    void clear();

private:
    mutable std::mutex mtx_;
    std::unordered_map<std::string, std::shared_ptr<OrderBook>> books_;
};
