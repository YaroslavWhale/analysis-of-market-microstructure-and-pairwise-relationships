#pragma once

// -----------------------------------------------------------------------------
// Minimal fixed-size dense matrix / vector library.
//
// A column vector is just a matrix with one column:
//     Vector<T, N>  ==  Matrix<T, N, 1>
//
// The storage layout is a plain row-major std::array so that the code
//     m.data[i][j]
// works exactly as in the algorithmic reference implementation.
//
// Only the operations required by the filters are provided:
//   transpose(), identity(), operator+/-/* (matrix), operator* (scalar).
// -----------------------------------------------------------------------------

#include <array>
#include <cstddef>

template <typename T, std::size_t R, std::size_t C>
struct Matrix {
    using value_type = T;

    static constexpr std::size_t rows = R;
    static constexpr std::size_t cols = C;

    // Value-initialised -> all entries are zero by default.
    std::array<std::array<T, C>, R> data{};

    // Return a (C x R) transposed copy.
    Matrix<T, C, R> transpose() const {
        Matrix<T, C, R> out;
        for (std::size_t i = 0; i < R; ++i)
            for (std::size_t j = 0; j < C; ++j)
                out.data[j][i] = data[i][j];
        return out;
    }

    // Make this matrix the identity (only meaningful for square matrices).
    void identity() {
        for (std::size_t i = 0; i < R; ++i)
            for (std::size_t j = 0; j < C; ++j)
                data[i][j] = (i == j) ? static_cast<T>(1) : static_cast<T>(0);
    }

    static Matrix eye() {
        Matrix m;
        m.identity();
        return m;
    }

    static Matrix zeros() { return Matrix{}; }

    // Diagonal matrix helper (used to build KF process / covariance matrices).
    static Matrix diagonal(T value) {
        Matrix m;
        for (std::size_t i = 0; i < R && i < C; ++i) m.data[i][i] = value;
        return m;
    }
};

// A column vector.
template <typename T, std::size_t N>
using Vector = Matrix<T, N, 1>;

// ------------------------------ element-wise --------------------------------

template <typename T, std::size_t R, std::size_t C>
Matrix<T, R, C> operator+(const Matrix<T, R, C>& a, const Matrix<T, R, C>& b) {
    Matrix<T, R, C> out;
    for (std::size_t i = 0; i < R; ++i)
        for (std::size_t j = 0; j < C; ++j)
            out.data[i][j] = a.data[i][j] + b.data[i][j];
    return out;
}

template <typename T, std::size_t R, std::size_t C>
Matrix<T, R, C> operator-(const Matrix<T, R, C>& a, const Matrix<T, R, C>& b) {
    Matrix<T, R, C> out;
    for (std::size_t i = 0; i < R; ++i)
        for (std::size_t j = 0; j < C; ++j)
            out.data[i][j] = a.data[i][j] - b.data[i][j];
    return out;
}

// ----------------------------- matrix product -------------------------------

template <typename T, std::size_t R, std::size_t K, std::size_t C>
Matrix<T, R, C> operator*(const Matrix<T, R, K>& a, const Matrix<T, K, C>& b) {
    Matrix<T, R, C> out;
    for (std::size_t i = 0; i < R; ++i)
        for (std::size_t k = 0; k < K; ++k) {
            const T aik = a.data[i][k];
            if (aik == static_cast<T>(0)) continue;
            for (std::size_t j = 0; j < C; ++j)
                out.data[i][j] += aik * b.data[k][j];
        }
    return out;
}

// ------------------------------ scalar product ------------------------------

template <typename T, std::size_t R, std::size_t C, typename U>
Matrix<T, R, C> operator*(const Matrix<T, R, C>& a, U s) {
    Matrix<T, R, C> out;
    for (std::size_t i = 0; i < R; ++i)
        for (std::size_t j = 0; j < C; ++j)
            out.data[i][j] = a.data[i][j] * static_cast<T>(s);
    return out;
}

template <typename T, std::size_t R, std::size_t C, typename U>
Matrix<T, R, C> operator*(U s, const Matrix<T, R, C>& a) {
    return a * s;
}
