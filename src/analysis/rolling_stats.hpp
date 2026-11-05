#pragma once

#include <cmath>
#include <cstddef>
#include <deque>

// -----------------------------------------------------------------------------
// Streaming mean / variance (Welford's online algorithm) and a fixed-capacity
// ring buffer with basic statistics.  Both are header-only and dependency-free
// so they can be reused by the live pipeline and by the unit tests.
// -----------------------------------------------------------------------------

class RunningStats {
public:
    void push(double x) {
        ++n_;
        const double delta = x - mean_;
        mean_ += delta / static_cast<double>(n_);
        m2_ += delta * (x - mean_);
    }

    void clear() { n_ = 0; mean_ = 0.0; m2_ = 0.0; }

    std::size_t count() const { return n_; }
    double mean() const { return mean_; }

    // Sample variance (n - 1 denominator); 0 when fewer than 2 samples.
    double variance() const {
        return n_ > 1 ? m2_ / static_cast<double>(n_ - 1) : 0.0;
    }

    double stddev() const { return std::sqrt(variance()); }

private:
    std::size_t n_ = 0;
    double mean_ = 0.0;
    double m2_ = 0.0;
};

template <typename T>
class RollingWindow {
public:
    explicit RollingWindow(std::size_t capacity = 0) : capacity_(capacity) {}

    void set_capacity(std::size_t capacity) {
        capacity_ = capacity;
        while (capacity_ != 0 && buf_.size() > capacity_) buf_.pop_front();
    }

    void push(const T& value) {
        buf_.push_back(value);
        if (capacity_ != 0 && buf_.size() > capacity_) buf_.pop_front();
    }

    void clear() { buf_.clear(); }

    std::size_t size() const { return buf_.size(); }
    std::size_t capacity() const { return capacity_; }
    bool empty() const { return buf_.empty(); }

    const T& operator[](std::size_t i) const { return buf_[i]; }
    const std::deque<T>& data() const { return buf_; }

    double mean() const {
        if (buf_.empty()) return 0.0;
        double sum = 0.0;
        for (const T& v : buf_) sum += static_cast<double>(v);
        return sum / static_cast<double>(buf_.size());
    }

    double stddev() const {
        const std::size_t n = buf_.size();
        if (n < 2) return 0.0;
        const double mu = mean();
        double acc = 0.0;
        for (const T& v : buf_) {
            const double d = static_cast<double>(v) - mu;
            acc += d * d;
        }
        return std::sqrt(acc / static_cast<double>(n - 1));
    }

private:
    std::size_t capacity_;
    std::deque<T> buf_;
};
