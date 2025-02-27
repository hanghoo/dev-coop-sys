#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

/// @file kde.h
/// @brief Kernel Density Estimation for C++
///
/// @author Duncan McNae

/// @brief kdepp is the namespace for the entire kdepp project.
namespace kdepp {

/// @brief Helper math functions used by kdepp
namespace kdemath {

/// @brief Calculates variance of a std::vector<T> of data. 
template <typename T>
T variance(std::vector<T> const & data)
{
    const T init = 0.0;
    T sum = std::accumulate(data.begin(), data.end(), init);
    T mean = sum / data.size();

    T res = 0.0;
    std::for_each(data.begin(), data.end(), [&](T x) {
        res += std::pow(x - mean, 2.0);
    });

    return res / (data.size() - 1); 
}

/// @brief Calculates standard deviation of a std::vector<T> of data. 
template <typename T>
T std_dev(std::vector<T> const & data)
{
    T var = variance<T>(data);
    return std::sqrt(var);
}

/// @brief 2D Covariance matrix:
template <typename T>
std::array<typename T::value_type, 4> covariance2d(std::vector<T> const & data)
{
    using Data_type = typename T::value_type;
    std::array<Data_type, 2> mean = {0, 0};
    for (auto i : data) {
        mean[0] += i[0];
        mean[1] += i[1];
    }

    mean[0] = mean[0] / (1.0 * data.size());
    mean[1] = mean[1] / (1.0 * data.size());

    std::array<Data_type, 4> cov = {0, 0, 0, 0};
    for (auto x_j : data) {
        std::array<Data_type, 2> diff = { (x_j[0] - mean[0]), (x_j[1] - mean[1]) };
        cov[0] += diff[0] * diff[0];
        cov[1] += diff[0] * diff[1];
        cov[3] += diff[1] * diff[1];
    }

    cov[2] = cov[1];
    double n = static_cast<Data_type>(data.size());

    cov[0] = (1.0 / (n-1.0)) * cov[0];
    cov[1] = (1.0 / (n-1.0)) * cov[1];
    cov[2] = (1.0 / (n-1.0)) * cov[2];
    cov[3] = (1.0 / (n-1.0)) * cov[3];
    return cov;
}


/// @brief pi as constexpr
template <typename T>
constexpr T pi()
{
    return std::atan(1.0) * 4.0;
}

}  // namespace kdemath

/// @brief Kernel density estimation for one dimensional data.
template <typename T>
class Kde1d {
 public:
    Kde1d(std::vector<T> data,
          std::string bandwidth_method = "scott")
        : data_(data)
    {
        if (data_.size() < 2) {
            throw std::invalid_argument("Only one data point");
        }
        init_bandwidth(bandwidth_method);
        pre_calculate_terms();
    };

    /// @brief Evaluate the initialized kernel estimator at given point.
    T eval(T point) const
    {
        T sum = 0;
        for (auto const & i : data_) {
            sum += kernel(point - i);
        }
        T n = data_.size();
        return sum / n;
    };

    /// @brief Manually set bandwidth.
    void set_bandwidth(T h)
    {
        h_ = h;
        pre_calculate_terms();
    };


 private:
    T kernel(T diff) const
    {
        return pow_pi_term_ * h_pow_term_ * std::exp((-1.0/2.0) * diff * h_pow_exp_term_ * diff);
    }

    void init_bandwidth(std::string bandwidth_method)
    {
        if (bandwidth_method == "silverman") {
            T root_h = std::pow(4.0 / 3.0, 1.0/5.0) * std::pow(data_.size(), (-1.0 / 5.0)) * kdemath::std_dev(data_);
            h_ = root_h * root_h;
        } else {
            // scott:
            // TODO error check the input string of bandwidth_method
            // TODO cite scott, silverman
            T root_h = std::pow(data_.size(), (-1.0 / 5.0)) * kdemath::std_dev(data_);
            h_ = root_h * root_h;
        }
    }

    void pre_calculate_terms()
    {
        // for optimization do some pre calcs of power and exponential constants:
        pow_pi_term_ = std::pow(2 * kdemath::pi<T>(), -1.0/2.0);
        h_pow_term_ = std::pow(h_, -1.0/2.0);
        h_pow_exp_term_ = std::pow(h_, -1.0);
    }

    std::vector<T> const data_;
    T h_;

    // Pre calculated terms for efficiency only:
    T pow_pi_term_;
    T h_pow_term_;
    T h_pow_exp_term_;
};

/// @brief Kernel density estimation for two dimensional data.
template <typename T>
class Kde2d {
 public:
    // Note, using a seperate class for 2D because my multivariate
    // is too slow.
    // 2D is probably quite common as oppose to higher dims.

    // convenience type alias:
    using Data_type = typename T::value_type;

    // Constructor:
    Kde2d(std::vector<T> data,
        std::string bandwidth_method = "scott")
        : data_(data)
        , h_()
    {
        // TODO assert dimension is 2?
        if (data_.size() < 2) {
            throw std::invalid_argument("Only one data point");
        }
        init_bandwidth(bandwidth_method);
        pre_calculate_terms();
    };

     Kde2d(std::vector<T> data,
        std::array<float, 4> bandwidth)
        : data_(data)
        , h_()
    {
        // TODO assert dimension is 2?
        if (data_.size() < 2) {
            throw std::invalid_argument("Only one data point");
        }
        set_bandwidth(bandwidth);
        pre_calculate_terms();
    };

    Data_type eval(T point) const
    {
        Data_type sum = 0;
        std::array<Data_type, 2> diff;

        for (auto const & i : data_) {
            diff[0] = point[0] - i[0];
            diff[1] = point[1] - i[1];
            sum += kernel(diff);

        }
        //double n = 1.0 * data_.size();
        //return sum / n;
        return sum;
    };

    /// @brief Manually set bandwidth.
    void set_bandwidth(std::array<Data_type, 4> h)
    {
        h_ = h;
        h_inv_ = invert(h_);
        pre_calculate_terms();
    };

 private:
    using index_t = std::ptrdiff_t;  // core guidelines

    Data_type kernel(std::array<Data_type, 2> diff) const
    {
        // manually do vector * matrix * vector for this two dim case:
        std::array<Data_type, 2> vec_math1;
        vec_math1[0] = diff[0] * h_inv_[0] + diff[1] * h_inv_[2];
        vec_math1[1] = diff[0] * h_inv_[1] + diff[1] * h_inv_[3];

        Data_type vec_math2;
        vec_math2 = vec_math1[0] * diff[0] + vec_math1[1] * diff[1];

        return pow_pi_term_ * h_pow_term_ * std::exp((-1.0/2.0) * vec_math2);
    }

    void init_bandwidth(std::string bandwidth_method)
    {
        std::array<Data_type, 4> cov = kdemath::covariance2d(data_);

        Data_type n_term = std::pow(1.0*data_.size(), -1.0 / (2.0 + 4.0));
        if (bandwidth_method == "silverman") {
            double silverman_term = std::pow(4 / (2.0 + 2), -1.0 / (2.0 + 4.0));
            for (std::size_t i = 0; i < cov.size(); ++i) {
                h_[i] = cov[i] * (silverman_term * n_term * silverman_term * n_term);
            }
        } else {
            // scott:
            for (std::size_t i = 0; i < cov.size(); ++i) {
                h_[i] = cov[i] * (n_term * n_term);
            }
        }
        h_inv_ = invert(h_);
    }

    std::array<Data_type ,4> invert(std::array<Data_type, 4> const & mat)
    {
        std::array<Data_type ,4> inv = {0, 0, 0, 0};
        Data_type determinant = h_[0] * h_[3] - h_[1] * h_[2];
        if (determinant == 0) {
            throw std::runtime_error("Singular data matrix");
        }
        inv[0] = mat[3];
        inv[1] = -1 * mat[1];
        inv[2] = -1 * mat[2];
        inv[3] = mat[0];

        for (std::size_t i = 0; i < 4; ++i) {
            inv[i] = inv[i] / determinant;
        } 
        return inv;
    }

    void pre_calculate_terms()
    {
        // for optimization do some pre calcs of power constants:
        Data_type determinant = h_[0] * h_[3] - h_[1] * h_[2];
        pow_pi_term_ = std::pow(2 * kdemath::pi<Data_type>(), -2.0 / 2.0);
        h_pow_term_ = std::pow(determinant, -1.0/2.0);
        if (std::isnan(h_pow_term_)) {
            throw std::runtime_error("Math domain error");
        }
    }

    std::vector<T> const data_;

    // for the H matrix, just using an array of size 4:
    std::array<Data_type, 4> h_;
    std::array<Data_type, 4> h_inv_;

    // Pre calculated terms for efficiency only:
    Data_type pow_pi_term_;
    Data_type h_pow_term_;
};


// Allow user defined auto type deduction for c++17:
#if __cplusplus >= 201703L
template <typename T> Kde1d(std::vector<T>) -> Kde1d<T>;
template <typename T> Kde2d(std::vector<T>) -> Kde2d<T>;
#endif

// Convenience typedefs:
using Kde1d_d = Kde1d<double>;
using Kde1d_f = Kde1d<float>;
using Kde2d_vecd = Kde2d<std::vector<double>>;
using Kde2d_vecf = Kde2d<std::vector<float>>;
using Kde2d_arrd = Kde2d<std::array<double,2>>;
using Kde2d_arrf = Kde2d<std::array<float,2>>;

}  // namespace kdepp

// TODO Provde Kde1d and Kde2d,
// Other file provide KdeXd
// do: template <typename T> Kde2d(std::vector<T>) -> Kde2d<T>;
//
// In readme state that declaration goes from this to this for c++17;
// Kde2d<std::array<double, 2>> kde(data);
// Kde2d kde(data)
