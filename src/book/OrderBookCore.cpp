#include "book/OrderBookCore.hpp"

#include <algorithm>

std::shared_ptr<OrderBook> OrderBookCore::get_or_create(const std::string& symbol,
                                                       uint64_t depth) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = books_.find(symbol);
    if (it != books_.end()) return it->second;

    auto book = std::make_shared<OrderBook>(symbol, depth);
    books_[symbol] = book;
    return book;
}

std::shared_ptr<OrderBook> OrderBookCore::get(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = books_.find(symbol);
    return it == books_.end() ? nullptr : it->second;
}

bool OrderBookCore::contains(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mtx_);
    return books_.find(symbol) != books_.end();
}

std::vector<std::string> OrderBookCore::symbols() const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::string> out;
    out.reserve(books_.size());
    for (const auto& kv : books_) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

void OrderBookCore::clear() {
    std::lock_guard<std::mutex> lock(mtx_);
    books_.clear();
}
