#pragma once

// -----------------------------------------------------------------------------
// Common interface for state estimators used to model the relationship between
// two (or more) price series.
//
// update() consumes the two series in log-space and advances the estimator.
// get_spread() returns the current cointegrating residual (the part of series 1
// that is not explained by series 2).
// -----------------------------------------------------------------------------

class IStateEstimator {
public:
    virtual ~IStateEstimator() = default;

    virtual void update(double price1_log, double price2_log) = 0;
    virtual double get_spread() const = 0;
    virtual void reset() = 0;

    // Kalman-specific diagnostics (default 0 for non-Kalman estimators).
    virtual double innovation() const { return 0.0; }
    virtual double innovation_variance() const { return 0.0; }
};
