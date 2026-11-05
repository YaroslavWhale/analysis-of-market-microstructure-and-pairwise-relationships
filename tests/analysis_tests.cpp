// Lightweight, dependency-free self-tests for the Kalman filter and the pair
// analysis pipeline.  Run with `make check` (no network access required).

#include "analysis/pair_analyzer.hpp"
#include "analysis/rolling_stats.hpp"
#include "filters/kalman_filter.hpp"
#include "filters/kalman_filter_2d.hpp"
#include "math/matrix.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <string>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    std::cout << (cond ? "[ OK ] " : "[FAIL] ") << what << "\n";
    if (!cond) ++g_failures;
}

void check_close(double got, double want, double tol, const std::string& what) {
    check(std::fabs(got - want) <= tol,
          what + " (got " + std::to_string(got) +
              ", want " + std::to_string(want) + ")");
}

// ---------------------------------------------------------------- matrix ----
void test_matrix() {
    Matrix<double, 2, 2> a;
    a.data[0][0] = 1; a.data[0][1] = 2;
    a.data[1][0] = 3; a.data[1][1] = 4;

    auto at = a.transpose();
    check_close(at.data[0][1], 3.0, 1e-12, "transpose [0][1]");
    check_close(at.data[1][0], 2.0, 1e-12, "transpose [1][0]");

    Matrix<double, 2, 2> I;
    I.identity();
    auto prod = a * I;
    check_close(prod.data[0][0], 1.0, 1e-12, "A * I == A [0][0]");
    check_close(prod.data[1][1], 4.0, 1e-12, "A * I == A [1][1]");

    Vector<double, 2> v;
    v.data[0][0] = 1.0;
    v.data[1][0] = 1.0;
    auto av = a * v;  // [1+2, 3+4] = [3, 7]
    check_close(av.data[0][0], 3.0, 1e-12, "A * v [0]");
    check_close(av.data[1][0], 7.0, 1e-12, "A * v [1]");
}

// --------------------------------------------------- KalmanFilter<2> --------
void test_kalman_template() {
    // Recover a static line z = 2 + 3*x from noisy samples.
    Matrix<double, 2, 2> Q;
    Q.data[0][0] = 1e-6;
    Q.data[1][1] = 1e-6;
    Vector<double, 2> x0;
    x0.data[0][0] = 0.0;
    x0.data[1][0] = 0.0;
    Matrix<double, 2, 2> P0;
    P0.data[0][0] = 1.0;
    P0.data[1][1] = 1.0;

    KalmanFilter<2> kf(Q, 1e-3, x0, P0);
    std::mt19937 rng(7);
    std::normal_distribution<double> noise(0.0, 0.01);

    for (int i = 0; i < 4000; ++i) {
        const double x = -5.0 + 10.0 * (i % 100) / 99.0;
        const double z = 2.0 + 3.0 * x + noise(rng);
        Vector<double, 2> H;
        H.data[0][0] = 1.0;
        H.data[1][0] = x;
        kf.update(H, z);
    }
    check_close(kf.state().data[1][0], 3.0, 0.1, "Kalman<2> beta ~ 3");
    check_close(kf.state().data[0][0], 2.0, 0.1, "Kalman<2> alpha ~ 2");
}

// --------------------------------------------------- KalmanFilter2D ---------
void test_kalman_filter_2d() {
    KalmanFilter2D kf(1e-4, 1e-5, 1e-5, 0.0, 1.0, 1.0, 1.0);
    std::mt19937 rng(11);
    std::normal_distribution<double> eps(0.0, 1.0);
    double trend = std::log(1000.0);
    double spread = 0.0;
    for (int i = 0; i < 5000; ++i) {
        trend += eps(rng) * 0.001;
        spread = 0.9 * spread + eps(rng) * 0.002;
        const double l2 = trend;
        const double l1 = 0.1 + 0.8 * l2 + spread;
        kf.update(l1, l2);
    }
    check_close(kf.get_beta(), 0.8, 0.1, "KalmanFilter2D beta ~ 0.8");
    check_close(kf.get_alpha(), 0.1, 0.1, "KalmanFilter2D alpha ~ 0.1");
}

// --------------------------------------------------- RollingStats -----------
void test_rolling_stats() {
    RunningStats rs;
    for (double v : {1.0, 2.0, 3.0, 4.0, 5.0}) rs.push(v);
    check_close(rs.mean(), 3.0, 1e-12, "RunningStats mean");
    check_close(rs.variance(), 2.5, 1e-12, "RunningStats variance");

    RollingWindow<double> w(3);
    for (double v : {1.0, 2.0, 3.0, 4.0}) w.push(v);
    check(w.size() == 3 && w[0] == 2.0, "RollingWindow keeps last N");
}

// --------------------------------------------------- PairAnalyzer -----------
void test_pair_analyzer() {
    PairAnalyzerConfig cfg;
    cfg.window = 250;
    cfg.entry_z = 2.0;
    cfg.exit_z = 0.5;
    PairAnalyzer analyzer("A", "B", cfg);

    std::mt19937 rng(123);
    std::normal_distribution<double> eps(0.0, 1.0);
    double trend = std::log(2500.0);
    double spread = 0.0;
    PairAnalysis last;
    for (int i = 0; i < 3000; ++i) {
        trend += eps(rng) * 0.0015;
        spread = 0.95 * spread + eps(rng) * 0.002;
        const double l2 = trend;
        const double l1 = 0.1 + 0.8 * l2 + spread;
        last = analyzer.update(std::exp(l1), std::exp(l2));
    }
    check(analyzer.ready(), "PairAnalyzer is ready after enough samples");
    check_close(last.beta, 0.8, 0.1, "PairAnalyzer dynamic beta ~ 0.8");
    check_close(last.beta_static, 0.8, 0.1, "PairAnalyzer static beta ~ 0.8");
    check(last.adf_stat <= -2.86, "PairAnalyzer ADF significant (stationary spread)");
    check(last.half_life > 0.0, "PairAnalyzer half-life > 0");
    check(last.return_corr > 0.0, "PairAnalyzer return correlation > 0");
}

// The Engle-Granger ADF statistic must separate a genuine cointegrated pair
// from two independent random walks (which it must not flag as stationary).
void test_cointegration_separation() {
    auto run_series = [](bool cointegrated, unsigned seed) {
        PairAnalyzerConfig cfg;
        cfg.window = 250;
        PairAnalyzer analyzer("A", "B", cfg);

        std::mt19937 rng(seed);
        std::normal_distribution<double> eps(0.0, 1.0);
        double l1 = std::log(100.0), l2 = std::log(200.0);
        double trend = std::log(2500.0), spread = 0.0;
        PairAnalysis last;
        for (int i = 0; i < 3000; ++i) {
            if (cointegrated) {
                trend += eps(rng) * 0.0015;
                spread = 0.95 * spread + eps(rng) * 0.002;
                l2 = trend;
                l1 = 0.1 + 0.8 * l2 + spread;
            } else {
                l1 += eps(rng) * 0.002;
                l2 += eps(rng) * 0.002;
            }
            last = analyzer.update(std::exp(l1), std::exp(l2));
        }
        return last;
    };

    const PairAnalysis coint = run_series(true, 123);
    const PairAnalysis indep = run_series(false, 321);

    check(coint.adf_stat <= -2.86, "cointegrated pair: ADF significant");
    check(coint.adf_stat < indep.adf_stat,
          "cointegrated ADF more negative than independent walks");
}

// --------------------------------------------------- CSV format -------------
void test_csv_format() {
    PairAnalyzer analyzer("A", "B", PairAnalyzerConfig{});
    analyzer.update(100.0, 200.0);
    const PairAnalysis last = analyzer.update(101.0, 201.0);

    const std::string header = PairAnalysis::csv_header();
    const std::string row = last.csv_row();
    auto commas = [](const std::string& s) {
        return std::count(s.begin(), s.end(), ',');
    };
    check(commas(header) == commas(row),
          "CSV header and row have the same number of columns");
}

}  // namespace

int main() {
    test_matrix();
    test_kalman_template();
    test_kalman_filter_2d();
    test_rolling_stats();
    test_pair_analyzer();
    test_cointegration_separation();
    test_csv_format();

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks
              << " checks passed.\n";
    return g_failures == 0 ? 0 : 1;
}
