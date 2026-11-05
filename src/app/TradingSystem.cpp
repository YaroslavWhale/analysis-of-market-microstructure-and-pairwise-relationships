#include "app/TradingSystem.hpp"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <thread>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <mutex>
#include <atomic>
#include <random>
#include <vector>
#include <memory>
#include "book/OrderBook.hpp"
#include "book/OrderBookCore.hpp"
#include "net/OrderBookClient.hpp"
#include "analysis/pair_analyzer.hpp"

using json = nlohmann::json;

static volatile std::sig_atomic_t running = 1;

void signal_handler(int sig) {
    if (sig == SIGINT) running = 0;
}

Multipliers load_multiplier(const std::string& symbol) {
    std::string url = "https://api.binance.com/api/v3/exchangeInfo?symbol=" + symbol;
    cpr::Response r = cpr::Get(cpr::Url{url});
    if (r.status_code != 200) {
        throw std::runtime_error("Failed to fetch info for " + symbol +
                                 ", status: " + std::to_string(r.status_code));
    }
    auto json_data = json::parse(r.text);
    if (json_data["symbols"].empty()) {
        throw std::runtime_error("Symbol not found: " + symbol);
    }
    const auto& sym = json_data["symbols"][0];
    uint64_t price_mult = 1, volume_mult = 1;
    for (const auto& filter : sym["filters"]) {
        if (filter["filterType"] == "PRICE_FILTER") {
            double tick_size = std::stod(filter["tickSize"].get<std::string>());
            price_mult = static_cast<uint64_t>(1.0 / tick_size);
        } else if (filter["filterType"] == "LOT_SIZE") {
            double step_size = std::stod(filter["stepSize"].get<std::string>());
            volume_mult = static_cast<uint64_t>(1.0 / step_size);
        }
    }
    if (price_mult == 1 || volume_mult == 1) {
        throw std::runtime_error("Required filters not found for symbol: " + symbol);
    }
    return {price_mult, volume_mult};
}

SnapshotData get_snapshot(const std::string& symbol,
                          uint64_t price_mult,
                          uint64_t volume_mult,
                          int limit) {
    std::string url = "https://api.binance.com/api/v3/depth?symbol=" + symbol +
                      "&limit=" + std::to_string(limit);
    cpr::Response r = cpr::Get(cpr::Url{url});
    if (r.status_code != 200) {
        throw std::runtime_error("Failed to fetch depth: " + std::to_string(r.status_code));
    }
    auto json_data = json::parse(r.text);
    SnapshotData data;
    data.lastUpdateId = json_data["lastUpdateId"].get<uint64_t>();
    for (const auto& item : json_data["bids"]) {
        double price = std::stod(item[0].get<std::string>());
        double volume = std::stod(item[1].get<std::string>());
        data.bids.push_back({static_cast<uint64_t>(price * price_mult),
                             static_cast<uint64_t>(volume * volume_mult)});
    }
    for (const auto& item : json_data["asks"]) {
        double price = std::stod(item[0].get<std::string>());
        double volume = std::stod(item[1].get<std::string>());
        data.asks.push_back({static_cast<uint64_t>(price * price_mult),
                             static_cast<uint64_t>(volume * volume_mult)});
    }
    return data;
}

namespace {

// -----------------------------------------------------------------------------
// A single symbol feed: owns the order book, the depth-update buffer and the
// WebSocket connection lifecycle.  Extracted so the same logic drives both the
// single-symbol mode and the two legs of a pair.
// -----------------------------------------------------------------------------
class SymbolFeed {
public:
    SymbolFeed(std::string symbol, uint64_t depth, websocket_endpoint& endpoint)
        : symbol_(std::move(symbol)),
          depth_(depth),
          endpoint_(endpoint),
          book_(std::make_shared<OrderBook>(symbol_, depth_)) {}

    const std::string& symbol() const { return symbol_; }
    std::shared_ptr<OrderBook> book() const { return book_; }

    // Load tick/step multipliers and configure the book.
    void prepare() {
        Multipliers mult = load_multiplier(symbol_);
        book_->set_price_multiplier(mult.price);
        book_->set_volume_multiplier(mult.volume);
        mult_ = mult;
    }

    // Open the depth websocket stream.  Returns false on failure.
    bool start() {
        std::string lower = symbol_;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        const std::string uri =
            "wss://stream.binance.com:9443/ws/" + lower + "@depth@100ms";
        conn_id_ = endpoint_.connect(
            uri,
            [this](websocketpp::connection_hdl h, client::message_ptr m) {
                on_message(h, m);
            });
        return conn_id_ != -1;
    }

    // Fetch a REST snapshot and drain the buffered updates into it.
    bool synchronize() {
        try {
            SnapshotData snap = get_snapshot(symbol_, mult_.price, mult_.volume,
                                             static_cast<int>(depth_));
            book_->snapshot(snap);

            std::lock_guard<std::mutex> lock(buffer_mtx_);
            uint64_t lastId = book_->get_last_update_id();

            for (auto& upd : buffer_) {
                if (upd.finalUpdateId <= lastId) continue;
                if (upd.firstUpdateId > lastId + 1) {
                    std::cerr << "[" << symbol_
                              << "] Gap during buffer drain, re-snapshot needed\n";
                    buffer_.clear();
                    snapshot_ready_.store(false);
                    return false;
                }
                try {
                    book_->applyUpdate(upd);
                } catch (const std::exception& e) {
                    std::cerr << "[" << symbol_ << "] Buffer apply error: "
                              << e.what() << std::endl;
                    buffer_.clear();
                    snapshot_ready_.store(false);
                    return false;
                }
                lastId = upd.finalUpdateId;
            }
            buffer_.clear();

            snapshot_ready_.store(true);
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[" << symbol_ << "] Snapshot error: " << e.what()
                      << std::endl;
            return false;
        }
    }

    bool ready() const { return snapshot_ready_.load(); }
    bool need_resnapshot() const { return need_resnapshot_.load(); }
    void clear_resnapshot() { need_resnapshot_.store(false); }

    // Mid price in raw price units (0.0 if the book is not ready yet).
    double mid_price() const {
        if (!book_->is_initialized()) return 0.0;
        const double bid = static_cast<double>(book_->get_best_bid_price());
        const double ask = static_cast<double>(book_->get_best_ask_price());
        const double pm = static_cast<double>(book_->get_price_multiplier());
        if (bid <= 0.0 || ask <= 0.0 || pm <= 0.0) return 0.0;
        return 0.5 * (bid + ask) / pm;
    }

private:
    DepthUpdate parse_update(const json& jd) {
        DepthUpdate update;
        update.firstUpdateId = jd["U"].get<uint64_t>();
        update.finalUpdateId = jd["u"].get<uint64_t>();

        const double pm = static_cast<double>(book_->get_price_multiplier());
        const double vm = static_cast<double>(book_->get_volume_multiplier());

        for (const auto& item : jd["b"]) {
            const double price = std::stod(item[0].get<std::string>());
            const double volume = std::stod(item[1].get<std::string>());
            update.bids.push_back({static_cast<uint64_t>(price * pm),
                                   static_cast<uint64_t>(volume * vm)});
        }
        for (const auto& item : jd["a"]) {
            const double price = std::stod(item[0].get<std::string>());
            const double volume = std::stod(item[1].get<std::string>());
            update.asks.push_back({static_cast<uint64_t>(price * pm),
                                   static_cast<uint64_t>(volume * vm)});
        }
        return update;
    }

    void on_message(websocketpp::connection_hdl, client::message_ptr msg) {
        try {
            auto jd = json::parse(msg->get_payload());
            if (!jd.contains("e") || jd["e"] != "depthUpdate") return;

            DepthUpdate update = parse_update(jd);

            if (!snapshot_ready_.load()) {
                std::lock_guard<std::mutex> lock(buffer_mtx_);
                if (!snapshot_ready_.load()) {
                    buffer_.push_back(std::move(update));
                    return;
                }
            }

            try {
                book_->applyUpdate(update);
            } catch (const std::exception& e) {
                std::cerr << "[" << symbol_ << "] Runtime sync error: "
                          << e.what() << std::endl;
                need_resnapshot_.store(true);
                snapshot_ready_.store(false);
                std::lock_guard<std::mutex> lock(buffer_mtx_);
                buffer_.clear();
            }
        } catch (const std::exception& e) {
            std::cerr << "[" << symbol_ << "] Error processing message: "
                      << e.what() << std::endl;
        }
    }

    std::string symbol_;
    uint64_t depth_;
    websocket_endpoint& endpoint_;
    std::shared_ptr<OrderBook> book_;
    Multipliers mult_{1, 1};

    std::mutex buffer_mtx_;
    std::vector<DepthUpdate> buffer_;
    std::atomic<bool> snapshot_ready_{false};
    std::atomic<bool> need_resnapshot_{false};

    int conn_id_ = -1;
};

}  // namespace

void run(std::string symbol, uint64_t depth) {
    signal(SIGINT, signal_handler);

    websocket_endpoint endpoint;
    SymbolFeed feed(symbol, depth, endpoint);
    feed.prepare();

    if (!feed.start()) {
        std::cerr << "Failed to connect to WebSocket" << std::endl;
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    while (running) {
        if (feed.synchronize()) {
            std::cout << "Synchronized!" << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "OrderBook is running. Press Ctrl+C to stop.\n";

    while (running) {
        if (feed.need_resnapshot()) {
            feed.clear_resnapshot();
            std::cout << "Re-synchronizing..." << std::endl;
            while (running) {
                if (feed.synchronize()) {
                    std::cout << "Synchronized!" << std::endl;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        if (feed.book()->is_initialized()) {
            std::cout << "\n=== " << symbol << " Order Book ===\n";
            std::cout << "Best Bid: " << feed.book()->get_best_bid_price()
                      << " Volume: " << feed.book()->get_best_bid_volume() << "\n";
            std::cout << "Best Ask: " << feed.book()->get_best_ask_price()
                      << " Volume: " << feed.book()->get_best_ask_volume() << "\n";
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "Shutting down...\n";
}

void run_pair(std::string symbol1, std::string symbol2, uint64_t depth,
              std::size_t window, const std::string& csv_path,
              double entry_z, double exit_z) {
    signal(SIGINT, signal_handler);

    websocket_endpoint endpoint;
    SymbolFeed feed1(symbol1, depth, endpoint);
    SymbolFeed feed2(symbol2, depth, endpoint);

    feed1.prepare();
    feed2.prepare();

    if (!feed1.start() || !feed2.start()) {
        std::cerr << "Failed to connect to WebSocket" << std::endl;
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "Synchronizing " << symbol1 << " and " << symbol2 << "...\n";
    bool ok1 = false, ok2 = false;
    while (running && !(ok1 && ok2)) {
        if (!ok1 && feed1.synchronize()) ok1 = true;
        if (!ok2 && feed2.synchronize()) ok2 = true;
        if (!ok1 || !ok2)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (!running) return;
    std::cout << "Synchronized!\n";

    PairAnalyzerConfig cfg;
    cfg.window = window;
    cfg.entry_z = entry_z;
    cfg.exit_z = exit_z;
    PairAnalyzer analyzer(symbol1, symbol2, cfg);

    std::ofstream csv;
    if (!csv_path.empty()) {
        csv.open(csv_path, std::ios::out | std::ios::trunc);
        if (csv) csv << PairAnalysis::csv_header() << "\n";
    }

    std::cout << "Pair analysis running. Press Ctrl+C to stop.\n";

    while (running) {
        // Recover individual feeds that lost sync.
        if (feed1.need_resnapshot()) {
            feed1.clear_resnapshot();
            std::cout << "Re-synchronizing " << symbol1 << "...\n";
            while (running && !feed1.synchronize())
                std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (feed2.need_resnapshot()) {
            feed2.clear_resnapshot();
            std::cout << "Re-synchronizing " << symbol2 << "...\n";
            while (running && !feed2.synchronize())
                std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        const double p1 = feed1.mid_price();
        const double p2 = feed2.mid_price();
        if (p1 > 0.0 && p2 > 0.0) {
            PairAnalysis a = analyzer.update(p1, p2);
            std::cout << "\n" << a.to_string() << std::endl;
            if (csv) {
                csv << a.csv_row() << "\n";
                csv.flush();
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "Shutting down...\n";
}


void run_demo(std::size_t window, double entry_z, double exit_z) {
    signal(SIGINT, signal_handler);

    // Synthetic cointegrated pair:
    //   two log-prices sharing one common stochastic trend plus a stationary
    //   (AR(1)) spread, i.e.  log(p1) = alpha + beta*log(p2) + spread.
    constexpr double true_alpha = 0.10;
    constexpr double true_beta = 0.80;
    constexpr double phi = 0.95;        // spread mean-reversion coefficient
    constexpr double sigma_trend = 0.0015;
    constexpr double sigma_spread = 0.002;

    std::mt19937 rng(20240517u);
    std::normal_distribution<double> eps(0.0, 1.0);

    PairAnalyzerConfig cfg;
    cfg.window = window;
    cfg.entry_z = entry_z;
    cfg.exit_z = exit_z;
    PairAnalyzer analyzer("DEMO_A", "DEMO_B", cfg);

    double trend = std::log(2000.0);
    double spread = 0.0;

    const int total = 2000;
    PairAnalysis last;
    for (int i = 0; i < total && running; ++i) {
        trend += eps(rng) * sigma_trend;
        spread = phi * spread + eps(rng) * sigma_spread;

        const double log_p2 = trend;
        const double log_p1 = true_alpha + true_beta * log_p2 + spread;

        last = analyzer.update(std::exp(log_p1), std::exp(log_p2));

        if (i % 200 == 0 || i == total - 1)
            std::cout << "\n" << last.to_string() << std::endl;
    }

    std::cout << "\nSynthetic ground truth: alpha=" << true_alpha
              << "  beta=" << true_beta
              << "  spread phi=" << phi
              << "  (half-life = " << (-std::log(2.0) / std::log(phi)) << " samples)\n";
}



