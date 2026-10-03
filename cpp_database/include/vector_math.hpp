#ifndef VECTOR_MATH_HPP
#define VECTOR_MATH_HPP

#include <vector>
#include <cmath>
#include <cstddef>

namespace vector_math {

inline float dot_product(const float* a, const float* b, size_t n) {
    float sum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

inline float dot_product(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return 0.0f;
    return dot_product(a.data(), b.data(), a.size());
}

inline float l2_norm(const float* a, size_t n) {
    float sum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        sum += a[i] * a[i];
    }
    return std::sqrt(sum);
}

inline float l2_norm(const std::vector<float>& a) {
    return l2_norm(a.data(), a.size());
}

inline void normalize(float* a, size_t n) {
    float norm = l2_norm(a, n);
    if (norm > 0.0f) {
        for (size_t i = 0; i < n; ++i) {
            a[i] /= norm;
        }
    }
}

inline void normalize(std::vector<float>& a) {
    normalize(a.data(), a.size());
}

inline float cosine_similarity(const float* a, const float* b, size_t n) {
    float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }
    if (norm_a == 0.0f || norm_b == 0.0f) return 0.0f;
    return dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
}

inline float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return 0.0f;
    return cosine_similarity(a.data(), b.data(), a.size());
}

#if defined(__AVX2__) || defined(__AVX__)
void dot_product_avx2(const float* a, const float* b, size_t n, float& result);
void cosine_similarity_avx2(const float* a, const float* b, size_t n, float& result);
#endif

} // namespace vector_math

#endif