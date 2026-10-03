#include "vector_math.hpp"
#include <gtest/gtest.h>
#include <vector>
#include <cmath>

using namespace vector_math;

TEST(VectorMathTest, DotProduct) {
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    std::vector<float> b = {4.0f, 5.0f, 6.0f};
    EXPECT_FLOAT_EQ(dot_product(a, b), 32.0f);
}

TEST(VectorMathTest, DotProductEmpty) {
    std::vector<float> a, b;
    EXPECT_FLOAT_EQ(dot_product(a, b), 0.0f);
}

TEST(VectorMathTest, DotProductMismatchedSize) {
    std::vector<float> a = {1.0f, 2.0f};
    std::vector<float> b = {1.0f, 2.0f, 3.0f};
    EXPECT_FLOAT_EQ(dot_product(a, b), 0.0f);
}

TEST(VectorMathTest, L2Norm) {
    std::vector<float> a = {3.0f, 4.0f};
    EXPECT_FLOAT_EQ(l2_norm(a), 5.0f);
}

TEST(VectorMathTest, Normalize) {
    std::vector<float> a = {3.0f, 4.0f};
    normalize(a);
    EXPECT_FLOAT_EQ(l2_norm(a), 1.0f);
    EXPECT_FLOAT_EQ(a[0], 0.6f);
    EXPECT_FLOAT_EQ(a[1], 0.8f);
}

TEST(VectorMathTest, CosineSimilarityIdentical) {
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    std::vector<float> b = {1.0f, 2.0f, 3.0f};
    EXPECT_FLOAT_EQ(cosine_similarity(a, b), 1.0f);
}

TEST(VectorMathTest, CosineSimilarityOpposite) {
    std::vector<float> a = {1.0f, 0.0f};
    std::vector<float> b = {-1.0f, 0.0f};
    EXPECT_FLOAT_EQ(cosine_similarity(a, b), -1.0f);
}

TEST(VectorMathTest, CosineSimilarityOrthogonal) {
    std::vector<float> a = {1.0f, 0.0f};
    std::vector<float> b = {0.0f, 1.0f};
    EXPECT_FLOAT_EQ(cosine_similarity(a, b), 0.0f);
}

TEST(VectorMathTest, CosineSimilarityZeroVector) {
    std::vector<float> a = {0.0f, 0.0f};
    std::vector<float> b = {1.0f, 2.0f};
    EXPECT_FLOAT_EQ(cosine_similarity(a, b), 0.0f);
}

TEST(VectorMathTest, CosineSimilarity768Dim) {
    std::vector<float> a(768, 0.0f);
    std::vector<float> b(768, 0.0f);
    a[0] = 1.0f;
    b[0] = 1.0f;
    EXPECT_FLOAT_EQ(cosine_similarity(a, b), 1.0f);
}

TEST(VectorMathTest, AVX2PathsCompile) {
#if defined(__AVX2__) || defined(__AVX__)
    std::vector<float> a(768, 0.1f);
    std::vector<float> b(768, 0.2f);
    float result = 0.0f;
    dot_product_avx2(a.data(), b.data(), a.size(), result);
    EXPECT_NEAR(result, dot_product(a, b), 1e-5);

    cosine_similarity_avx2(a.data(), b.data(), a.size(), result);
    EXPECT_NEAR(result, cosine_similarity(a, b), 1e-5);
#endif
}