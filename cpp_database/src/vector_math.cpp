#include "vector_math.hpp"

#if defined(__AVX2__)
#include <immintrin.h>

namespace vector_math {

void dot_product_avx2(const float* a, const float* b, size_t n, float& result) {
    __m256 sum = _mm256_setzero_ps();
    size_t i = 0;
    for (; i + 7 < n; i += 8) {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_loadu_ps(b + i);
        sum = _mm256_fmadd_ps(va, vb, sum);
    }
    float tmp[8];
    _mm256_storeu_ps(tmp, sum);
    result = tmp[0] + tmp[1] + tmp[2] + tmp[3] + tmp[4] + tmp[5] + tmp[6] + tmp[7];
    for (; i < n; ++i) {
        result += a[i] * b[i];
    }
}

void cosine_similarity_avx2(const float* a, const float* b, size_t n, float& result) {
    __m256 sum_ab = _mm256_setzero_ps();
    __m256 sum_a2 = _mm256_setzero_ps();
    __m256 sum_b2 = _mm256_setzero_ps();
    size_t i = 0;
    for (; i + 7 < n; i += 8) {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_loadu_ps(b + i);
        sum_ab = _mm256_fmadd_ps(va, vb, sum_ab);
        sum_a2 = _mm256_fmadd_ps(va, va, sum_a2);
        sum_b2 = _mm256_fmadd_ps(vb, vb, sum_b2);
    }
    float tab[8], ta2[8], tb2[8];
    _mm256_storeu_ps(tab, sum_ab);
    _mm256_storeu_ps(ta2, sum_a2);
    _mm256_storeu_ps(tb2, sum_b2);
    float dot = tab[0] + tab[1] + tab[2] + tab[3] + tab[4] + tab[5] + tab[6] + tab[7];
    float n_a = ta2[0] + ta2[1] + ta2[2] + ta2[3] + ta2[4] + ta2[5] + ta2[6] + ta2[7];
    float n_b = tb2[0] + tb2[1] + tb2[2] + tb2[3] + tb2[4] + tb2[5] + tb2[6] + tb2[7];
    for (; i < n; ++i) {
        dot += a[i] * b[i];
        n_a += a[i] * a[i];
        n_b += b[i] * b[i];
    }
    if (n_a == 0.0f || n_b == 0.0f) {
        result = 0.0f;
    } else {
        result = dot / (std::sqrt(n_a) * std::sqrt(n_b));
    }
}

} // namespace vector_math

#elif defined(__AVX__)
#include <immintrin.h>

namespace vector_math {

void dot_product_avx2(const float* a, const float* b, size_t n, float& result) {
    __m256 sum = _mm256_setzero_ps();
    size_t i = 0;
    for (; i + 7 < n; i += 8) {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_loadu_ps(b + i);
        sum = _mm256_add_ps(sum, _mm256_mul_ps(va, vb));
    }
    float tmp[8];
    _mm256_storeu_ps(tmp, sum);
    result = tmp[0] + tmp[1] + tmp[2] + tmp[3] + tmp[4] + tmp[5] + tmp[6] + tmp[7];
    for (; i < n; ++i) {
        result += a[i] * b[i];
    }
}

void cosine_similarity_avx2(const float* a, const float* b, size_t n, float& result) {
    __m256 sum_ab = _mm256_setzero_ps();
    __m256 sum_a2 = _mm256_setzero_ps();
    __m256 sum_b2 = _mm256_setzero_ps();
    size_t i = 0;
    for (; i + 7 < n; i += 8) {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_loadu_ps(b + i);
        sum_ab = _mm256_add_ps(sum_ab, _mm256_mul_ps(va, vb));
        sum_a2 = _mm256_add_ps(sum_a2, _mm256_mul_ps(va, va));
        sum_b2 = _mm256_add_ps(sum_b2, _mm256_mul_ps(vb, vb));
    }
    float tab[8], ta2[8], tb2[8];
    _mm256_storeu_ps(tab, sum_ab);
    _mm256_storeu_ps(ta2, sum_a2);
    _mm256_storeu_ps(tb2, sum_b2);
    float dot = tab[0] + tab[1] + tab[2] + tab[3] + tab[4] + tab[5] + tab[6] + tab[7];
    float n_a = ta2[0] + ta2[1] + ta2[2] + ta2[3] + ta2[4] + ta2[5] + ta2[6] + ta2[7];
    float n_b = tb2[0] + tb2[1] + tb2[2] + tb2[3] + tb2[4] + tb2[5] + tb2[6] + tb2[7];
    for (; i < n; ++i) {
        dot += a[i] * b[i];
        n_a += a[i] * a[i];
        n_b += b[i] * b[i];
    }
    if (n_a == 0.0f || n_b == 0.0f) {
        result = 0.0f;
    } else {
        result = dot / (std::sqrt(n_a) * std::sqrt(n_b));
    }
}

} // namespace vector_math

#endif