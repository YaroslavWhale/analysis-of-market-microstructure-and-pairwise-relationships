#include "analysis/pair_analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <iomanip>
#include <sstream>

const char* pair_signal_name(PairSignal s) {
    switch (s) {
        case PairSignal::LongSpread:  return "LONG_SPREAD  (buy leg1 / sell leg2)";
        case PairSignal::ShortSpread: return "SHORT_SPREAD (sell leg1 / buy leg2)";
        case PairSignal::Flat:        return "FLAT";
    }
    return "FLAT";
}

namespace {

struct MeanReversion {
    bool ok = false;
    double slope = 0.0;
    double t_stat = 0.0;
    double half_life = 0.0;
};

// OLS of  dS_t = c + b * S_{t-1} + e_t  on the spread series.
// The slope b is the AR(1) coefficient minus one; the t-statistic of b is the
// (simplified) ADF statistic and the half-life is -ln(2)/ln(1 + b).
MeanReversion estimate_mean_reversion(const std::deque<double>& s) {
    MeanReversion result;
    const std::size_t n = s.size();
    if (n < 5) return result;

    const std::size_t m = n - 1;  // number of (x, y) pairs
    double sum_x = 0.0, sum_y = 0.0, sum_xx = 0.0, sum_xy = 0.0;
    for (std::size_t t = 1; t < n; ++t) {
        const double x = s[t - 1];
        const double y = s[t] - s[t - 1];
        sum_x += x;
        sum_y += y;
        sum_xx += x * x;
        sum_xy += x * y;
    }

    const double mean_x = sum_x / static_cast<double>(m);
    const double mean_y = sum_y / static_cast<double>(m);
    const double sxx = sum_xx - static_cast<double>(m) * mean_x * mean_x;
    if (sxx <= 1e-18) return result;

    const double sxy = sum_xy - static_cast<double>(m) * mean_x * mean_y;
    const double slope = sxy / sxx;
    const double intercept = mean_y - slope * mean_x;

    double sse = 0.0;
    for (std::size_t t = 1; t < n; ++t) {
        const double x = s[t - 1];
        const double y = s[t] - s[t - 1];
        const double resid = y - (intercept + slope * x);
        sse += resid * resid;
    }

    const double sigma2 = (m > 2) ? sse / static_cast<double>(m - 2) : 0.0;
    const double se = (sxx > 0.0) ? std::sqrt(sigma2 / sxx) : 0.0;

    result.slope = slope;
    result.t_stat = (se > 0.0) ? slope / se : 0.0;
    if (slope < 0.0 && (1.0 + slope) > 0.0) {
        result.half_life = -std::log(2.0) / std::log(1.0 + slope);
    }
    result.ok = true;
    return result;
}

// Pearson correlation between the aligned tails of two series.
double pearson_tail(const std::deque<double>& a, const std::deque<double>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    if (n < 3) return 0.0;

    const std::size_t a0 = a.size() - n;
    const std::size_t b0 = b.size() - n;

    double sx = 0.0, sy = 0.0, sxx = 0.0, syy = 0.0, sxy = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double x = a[a0 + i];
        const double y = b[b0 + i];
        sx += x;
        sy += y;
        sxx += x * x;
        syy += y * y;
        sxy += x * y;
    }
    const double dn = static_cast<double>(n);
    const double cov = sxy - dn * (sx / dn) * (sy / dn);
    const double vx = sxx - dn * (sx / dn) * (sx / dn);
    const double vy = syy - dn * (sy / dn) * (sy / dn);
    const double den = std::sqrt(vx * vy);
    return (den > 1e-18) ? cov / den : 0.0;
}

// Static OLS of  y = alpha + beta * x  plus the residual series (the
// Engle-Granger cointegrating residual for the window).
struct StaticFit {
    bool ok = false;
    double alpha = 0.0;
    double beta = 0.0;
    std::deque<double> residual;
};

StaticFit static_ols(const std::deque<double>& y, const std::deque<double>& x) {
    StaticFit fit;
    const std::size_t n = std::min(x.size(), y.size());
    if (n < 5) return fit;

    const std::size_t y0 = y.size() - n;
    const std::size_t x0 = x.size() - n;

    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double xi = x[x0 + i];
        const double yi = y[y0 + i];
        sx += xi;
        sy += yi;
        sxx += xi * xi;
        sxy += xi * yi;
    }
    const double dn = static_cast<double>(n);
    const double mx = sx / dn;
    const double my = sy / dn;
    const double sxxc = sxx - dn * mx * mx;
    if (sxxc <= 1e-18) return fit;

    const double sxyc = sxy - dn * mx * my;
    fit.beta = sxyc / sxxc;
    fit.alpha = my - fit.beta * mx;

    fit.residual.clear();
    for (std::size_t i = 0; i < n; ++i) {
        fit.residual.push_back(y[y0 + i] - fit.alpha - fit.beta * x[x0 + i]);
    }
    fit.ok = true;
    return fit;
}

}  // namespace

PairAnalyzer::PairAnalyzer(std::string symbol1, std::string symbol2,
                           PairAnalyzerConfig config)
    : symbol1_(std::move(symbol1)),
      symbol2_(std::move(symbol2)),
      cfg_(config),
      kf_(cfg_.R, cfg_.Q_alpha, cfg_.Q_beta,
          cfg_.init_alpha, cfg_.init_beta,
          cfg_.init_P_alpha, cfg_.init_P_beta),
      spread_win_(cfg_.window),
      ret1_win_(cfg_.window),
      ret2_win_(cfg_.window),
      log1_win_(cfg_.window),
      log2_win_(cfg_.window) {}

void PairAnalyzer::reset() {
    kf_.reset();
    spread_win_.clear();
    ret1_win_.clear();
    ret2_win_.clear();
    log1_win_.clear();
    log2_win_.clear();
    prev_log1_ = prev_log2_ = 0.0;
    has_prev_ = false;
    samples_ = 0;
    current_signal_ = PairSignal::Flat;
    last_ = PairAnalysis{};
}

bool PairAnalyzer::ready() const {
    return samples_ >= cfg_.min_samples && spread_win_.size() >= cfg_.min_samples;
}

PairAnalysis PairAnalyzer::update(double price1, double price2) {
    PairAnalysis a;
    a.symbol1 = symbol1_;
    a.symbol2 = symbol2_;
    a.entry_threshold = cfg_.entry_z;
    a.exit_threshold = cfg_.exit_z;
    a.price1 = price1;
    a.price2 = price2;

    if (!(price1 > 0.0) || !(price2 > 0.0)) {
        last_ = a;
        return last_;
    }

    const double l1 = std::log(price1);
    const double l2 = std::log(price2);
    a.log_price1 = l1;
    a.log_price2 = l2;

    // --- Kalman filter: dynamic hedge ratio ---------------------------------
    kf_.update(l1, l2);
    ++samples_;

    a.samples = samples_;
    a.alpha = kf_.get_alpha();
    a.beta = kf_.get_beta();
    a.spread = kf_.get_spread();
    a.innovation = kf_.innovation();
    a.innovation_variance = kf_.innovation_variance();

    // --- rolling statistics --------------------------------------------------
    spread_win_.push(a.spread);
    log1_win_.push(l1);
    log2_win_.push(l2);

    if (has_prev_) {
        ret1_win_.push(l1 - prev_log1_);
        ret2_win_.push(l2 - prev_log2_);
    }
    prev_log1_ = l1;
    prev_log2_ = l2;
    has_prev_ = true;

    if (spread_win_.size() >= 2) {
        a.spread_mean = spread_win_.mean();
        a.spread_std = spread_win_.stddev();
        if (a.spread_std > 1e-12) {
            a.zscore = (a.spread - a.spread_mean) / a.spread_std;
        }
    }

    // --- cointegration diagnostics on the static (Engle-Granger) residual ----
    const StaticFit fit = static_ols(log1_win_.data(), log2_win_.data());
    if (fit.ok) {
        a.alpha_static = fit.alpha;
        a.beta_static = fit.beta;
        a.eg_residual = fit.residual.empty() ? 0.0 : fit.residual.back();

        const MeanReversion mr = estimate_mean_reversion(fit.residual);
        if (mr.ok) {
            a.half_life = mr.half_life;
            a.adf_stat = mr.t_stat;
            a.has_stats = true;
        }
    }

    a.return_corr = pearson_tail(ret1_win_.data(), ret2_win_.data());

    // --- signal with hysteresis ---------------------------------------------
    if (current_signal_ == PairSignal::Flat) {
        if (a.zscore <= -cfg_.entry_z) current_signal_ = PairSignal::LongSpread;
        else if (a.zscore >= cfg_.entry_z) current_signal_ = PairSignal::ShortSpread;
    } else if (current_signal_ == PairSignal::LongSpread) {
        if (a.zscore >= -cfg_.exit_z) current_signal_ = PairSignal::Flat;
        if (a.zscore >= cfg_.entry_z) current_signal_ = PairSignal::ShortSpread;
    } else {  // ShortSpread
        if (a.zscore <= cfg_.exit_z) current_signal_ = PairSignal::Flat;
        if (a.zscore <= -cfg_.entry_z) current_signal_ = PairSignal::LongSpread;
    }
    a.signal = current_signal_;

    last_ = a;
    return last_;
}

std::string PairAnalysis::to_string(int decimals) const {
    std::ostringstream os;
    os << std::fixed << std::setprecision(decimals);
    os << "==================== PAIR ANALYSIS ====================\n";
    os << "Pair              : " << symbol1 << " / " << symbol2 << "\n";
    os << "Samples           : " << samples
       << "   window-ready: " << (has_stats ? "yes" : "no") << "\n";
    os << "price1 / price2   : " << price1 << " / " << price2 << "\n";
    os << "log(price1/2)     : " << log_price1 << " / " << log_price2 << "\n";
    os << "alpha (intercept) : " << alpha << "   (Kalman)\n";
    os << "beta  (hedge)     : " << beta << "   (Kalman, dynamic)\n";
    os << "beta_static (OLS) : " << beta_static
       << "   (alpha_static=" << alpha_static << ")\n";
    os << "spread (dynamic)  : " << spread << "\n";
    os << "EG residual       : " << eg_residual << "\n";
    os << "spread mean/std   : " << spread_mean << " / " << spread_std << "\n";
    os << "z-score           : " << zscore
       << "   (entry=" << entry_threshold << ", exit=" << exit_threshold << ")\n";
    os << "half-life (EG)    : " << half_life << " samples\n";
    os << "ADF t-stat (EG)   : " << adf_stat
       << "   (5% crit ~ " << adf_crit_5pct
       << (adf_stat <= adf_crit_5pct ? "  -> stationary)" : "  -> not significant)") << "\n";
    os << "corr(log return)  : " << return_corr << "\n";
    os << "innovation        : " << innovation << "\n";
    os << "innovation var    : " << innovation_variance << "\n";
    os << "signal            : " << pair_signal_name(signal) << "\n";
    os << "=======================================================";
    return os.str();
}

std::string PairAnalysis::csv_header() {
    return "samples,price1,price2,alpha,beta,beta_static,alpha_static,eg_residual,"
           "spread,spread_mean,spread_std,zscore,half_life,adf_stat,return_corr,"
           "innovation,innovation_var,signal";
}

std::string PairAnalysis::csv_row() const {
    std::ostringstream os;
    os << std::setprecision(10);
    os << samples << ','
       << price1 << ',' << price2 << ','
       << alpha << ',' << beta << ','
       << beta_static << ',' << alpha_static << ',' << eg_residual << ','
       << spread << ',' << spread_mean << ',' << spread_std << ','
       << zscore << ',' << half_life << ',' << adf_stat << ','
       << return_corr << ',' << innovation << ',' << innovation_variance << ','
       << static_cast<int>(signal);
    return os.str();
}


