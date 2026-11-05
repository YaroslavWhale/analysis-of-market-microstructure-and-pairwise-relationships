#pragma once

#include "filters/i_state_estimator.hpp"
#include "filters/kalman_filter.hpp"

#include <cstddef>

// -----------------------------------------------------------------------------
// Kalman filter specialised to a pair of price series.
//
// State:        x = [alpha, beta]
// Observation:  z = log(price1)
// Design:       H = [1, log(price2)]
//
// => the filter tracks the dynamic linear relationship
//        log(price1) = alpha + beta * log(price2) + noise
// and the cointegrating spread is
//        spread = log(price1) - (alpha + beta * log(price2)).
// -----------------------------------------------------------------------------

class KalmanFilter2D : public IStateEstimator {
public:
    KalmanFilter2D(double R, double Q_alpha, double Q_beta,
                   double init_alpha = 0.0, double init_beta = 1.0,
                   double init_P_alpha = 1e-4, double init_P_beta = 1e-6);

    void update(double price1_log, double price2_log) override;
    double get_spread() const override;
    void reset() override;

    double get_alpha() const { return kf_.state().data[0][0]; }
    double get_beta()  const { return kf_.state().data[1][0]; }

    double innovation() const override { return kf_.innovation(); }
    double innovation_variance() const override { return kf_.innovationVariance(); }

    double get_R() const { return R_; }
    double get_Q_alpha() const { return Q_.data[0][0]; }
    double get_Q_beta() const { return Q_.data[1][1]; }

    std::size_t samples() const { return samples_; }
    double last_price1_log() const { return last_price1_; }
    double last_price2_log() const { return last_price2_; }

private:
    double R_;
    Matrix<double, 2, 2> Q_;
    KalmanFilter<2> kf_;

    double init_alpha_;
    double init_beta_;
    double init_P_alpha_;
    double init_P_beta_;

    double last_price1_ = 0.0;
    double last_price2_ = 0.0;
    std::size_t samples_ = 0;
};
