#pragma once

#include "analysis/rolling_stats.hpp"
#include "filters/kalman_filter_2d.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

// -----------------------------------------------------------------------------
// PairAnalyzer
//
// Consumes two synchronised price series tick-by-tick, estimates the dynamic
// hedge ratio with a Kalman filter and reports everything required for a
// cointegration / statistical-arbitrage analysis:
//
//   * dynamic intercept (alpha) and hedge ratio (beta)
//   * current spread and its rolling mean / std / z-score
//   * mean-reversion half-life   (AR(1) on the spread)
//   * ADF t-statistic            (with the 5% critical value)
//   * correlation of log-returns
//   * Kalman innovation and its variance (model diagnostics)
//   * a trading signal derived from the z-score with hysteresis
// -----------------------------------------------------------------------------

enum class PairSignal {
    Flat,
    LongSpread,   // buy leg1, sell leg2  (spread too low)
    ShortSpread,  // sell leg1, buy leg2  (spread too high)
};

const char* pair_signal_name(PairSignal s);

struct PairAnalysis {
    std::string symbol1;
    std::string symbol2;

    std::uint64_t samples = 0;

    double price1 = 0.0;
    double price2 = 0.0;
    double log_price1 = 0.0;
    double log_price2 = 0.0;

    double alpha = 0.0;              // dynamic intercept (Kalman)
    double beta = 0.0;               // dynamic hedge ratio (Kalman)

    double alpha_static = 0.0;       // static intercept over the window (OLS)
    double beta_static = 0.0;        // static hedge ratio over the window (OLS)
    double eg_residual = 0.0;        // current Engle-Granger residual in the window

    double spread = 0.0;             // dynamic spread = log p1 - alpha - beta*log p2
    double spread_mean = 0.0;
    double spread_std = 0.0;
    double zscore = 0.0;

    // Mean-reversion diagnostics computed on the *static* (Engle-Granger)
    // residual of the window, so that the adaptive filter does not bias them.
    double half_life = 0.0;          // in samples; 0 => not mean-reverting
    double adf_stat = 0.0;
    double adf_crit_5pct = -2.86;

    double return_corr = 0.0;

    double innovation = 0.0;
    double innovation_variance = 0.0;

    double entry_threshold = 2.0;
    double exit_threshold = 0.5;

    PairSignal signal = PairSignal::Flat;
    bool has_stats = false;      // enough spread history to compute ADF / half-life

    std::string to_string(int decimals = 6) const;

    static std::string csv_header();
    std::string csv_row() const;
};

struct PairAnalyzerConfig {
    std::size_t window = 200;      // rolling window (in samples) for spread stats
    std::size_t min_samples = 30;  // minimum samples before stats are "ready"

    // Kalman filter tuning.
    double R = 1e-4;             // measurement noise variance
    double Q_alpha = 1e-5;       // process noise for alpha
    double Q_beta = 1e-5;        // process noise for beta
    double init_alpha = 0.0;
    double init_beta = 1.0;
    double init_P_alpha = 1.0;
    double init_P_beta = 1.0;

    // Signal thresholds.
    double entry_z = 2.0;
    double exit_z = 0.5;
};

class PairAnalyzer {
public:
    PairAnalyzer(std::string symbol1, std::string symbol2,
                 PairAnalyzerConfig config = PairAnalyzerConfig{});

    // Feed one synchronised observation of both raw prices.
    // Returns the freshly computed analysis snapshot.
    PairAnalysis update(double price1, double price2);

    void reset();

    const PairAnalysis& last() const { return last_; }
    const KalmanFilter2D& filter() const { return kf_; }
    const PairAnalyzerConfig& config() const { return cfg_; }

    std::uint64_t samples() const { return samples_; }
    bool ready() const;

private:
    std::string symbol1_;
    std::string symbol2_;
    PairAnalyzerConfig cfg_;
    KalmanFilter2D kf_;

    RollingWindow<double> spread_win_;
    RollingWindow<double> ret1_win_;
    RollingWindow<double> ret2_win_;
    RollingWindow<double> log1_win_;   // log(price1), for the static EG fit
    RollingWindow<double> log2_win_;   // log(price2), for the static EG fit

    double prev_log1_ = 0.0;
    double prev_log2_ = 0.0;
    bool has_prev_ = false;

    std::uint64_t samples_ = 0;
    PairSignal current_signal_ = PairSignal::Flat;

    PairAnalysis last_;
};
