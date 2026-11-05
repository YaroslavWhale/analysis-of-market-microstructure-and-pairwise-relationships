#pragma once

#include "math/matrix.hpp"
#include <algorithm>
#include <cstddef>

// -----------------------------------------------------------------------------
// Linear Kalman filter for a Dim-dimensional state and a scalar observation.
//
// The transition matrix is the identity (random-walk state), i.e.
//     x_k = x_{k-1} + w,   w ~ N(0, Q)
//     z_k = H_k * x_k + v, v ~ N(0, R)
//
// This is exactly the formulation used for dynamic hedge-ratio estimation:
// state = [alpha, beta], observation = log(price1), H = [1, log(price2)].
// -----------------------------------------------------------------------------

template <std::size_t Dim>
class KalmanFilter {
public:
    using StateVec = Vector<double, Dim>;
    using CovMat   = Matrix<double, Dim, Dim>;

    KalmanFilter(const CovMat& Q, double R,
                 const StateVec& x0, const CovMat& P0)
        : Q_(Q), R_(R), x_(x0), P_(P0) {}

    // Predict then correct with a new scalar observation z and row H.
    void update(const Vector<double, Dim>& H, double z) {
        predict();
        apply_measurement(H, z, R_);
    }

    void apply_measurement(const Vector<double, Dim>& H, double z, double R_meas) {
        double y = z - (H.transpose() * x_).data[0][0];

        auto HP = H.transpose() * P_;
        double S = (HP * H).data[0][0] + R_meas;
        if (S < 1e-12) S = 1e-12;
        double invS = 1.0 / S;

        auto K = P_ * H * invS;

        auto KH = K * H.transpose();
        CovMat I;
        I.identity();
        P_ = (I - KH) * P_;

        // Keep the covariance symmetric and positive-definite.
        for (std::size_t i = 0; i < Dim; ++i)
            for (std::size_t j = i + 1; j < Dim; ++j) {
                double avg = 0.5 * (P_.data[i][j] + P_.data[j][i]);
                P_.data[i][j] = P_.data[j][i] = avg;
            }
        for (std::size_t i = 0; i < Dim; ++i)
            P_.data[i][i] = std::max(P_.data[i][i], 1e-10);

        x_ = x_ + K * y;

        innovation_ = y;
        S_ = S;
    }

    // Random-walk prediction: inflate the state covariance by Q.
    void predict() {
        P_ = P_ + Q_;
    }

    const StateVec& state() const { return x_; }
    const CovMat& covariance() const { return P_; }
    double innovation() const { return innovation_; }
    double innovationVariance() const { return S_; }

    void reset(const StateVec& x0, const CovMat& P0) {
        x_ = x0;
        P_ = P0;
        innovation_ = 0.0;
        S_ = 0.0;
    }

private:
    CovMat Q_;
    double R_;
    StateVec x_;
    CovMat P_;
    double innovation_ = 0.0;
    double S_ = 0.0;
};
