#include "filters/kalman_filter_2d.hpp"

namespace {

// Helpers that build the small matrices with a readable layout.
Matrix<double, 2, 2> make_symmetric_diag(double d0, double d1) {
    Matrix<double, 2, 2> m;
    m.data[0][0] = d0;
    m.data[1][1] = d1;
    return m;
}

Vector<double, 2> make_state(double alpha, double beta) {
    Vector<double, 2> v;
    v.data[0][0] = alpha;
    v.data[1][0] = beta;
    return v;
}

}  // namespace

KalmanFilter2D::KalmanFilter2D(double R, double Q_alpha, double Q_beta,
                               double init_alpha, double init_beta,
                               double init_P_alpha, double init_P_beta)
    : R_(R),
      Q_(make_symmetric_diag(Q_alpha, Q_beta)),
      kf_(Q_, R,
          make_state(init_alpha, init_beta),
          make_symmetric_diag(init_P_alpha, init_P_beta)),
      init_alpha_(init_alpha),
      init_beta_(init_beta),
      init_P_alpha_(init_P_alpha),
      init_P_beta_(init_P_beta) {}

void KalmanFilter2D::update(double price1_log, double price2_log) {
    last_price1_ = price1_log;
    last_price2_ = price2_log;

    Vector<double, 2> H;
    H.data[0][0] = 1.0;
    H.data[1][0] = price2_log;

    kf_.update(H, price1_log);
    ++samples_;
}

double KalmanFilter2D::get_spread() const {
    // Innovation of the observation model equals the spread when H uses the
    // freshly observed price2.  We recompute it explicitly for clarity.
    return last_price1_ - (get_alpha() + get_beta() * last_price2_);
}

void KalmanFilter2D::reset() {
    kf_.reset(make_state(init_alpha_, init_beta_),
              make_symmetric_diag(init_P_alpha_, init_P_beta_));
    last_price1_ = 0.0;
    last_price2_ = 0.0;
    samples_ = 0;
}
