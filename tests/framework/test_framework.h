#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>

static int g_tests_run = 0;
static int g_tests_failed = 0;

#define TEST_SUITE_BEGIN(name) \
    int main(void) { \
        printf("\n========================================\n"); \
        printf(" RUNNING SUITE: %s\n", name); \
        printf("========================================\n\n");

#define TEST_SUITE_END() \
        printf("\n========================================\n"); \
        printf(" SUMMARY: %d tests run, %d failed\n", g_tests_run, g_tests_failed); \
        printf("========================================\n\n"); \
        return (g_tests_failed == 0) ? 0 : 1; \
    }

#define RUN_TEST(test_func) \
    do { \
        g_tests_run++; \
        printf("  [RUN]  %s\n", #test_func); \
        if (test_func()) { \
            printf("  [PASS] %s\n", #test_func); \
        } else { \
            g_tests_failed++; \
            printf("  [FAIL] %s\n", #test_func); \
        } \
    } while (0)

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "    ASSERTION FAILED: %s (at %s:%d)\n", #expr, __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(a, b) \
    do { \
        intptr_t _va = (intptr_t)(a); \
        intptr_t _vb = (intptr_t)(b); \
        if (_va != _vb) { \
            fprintf(stderr, "    ASSERTION FAILED: %s == %s (got %lld, expected %lld at %s:%d)\n", \
                    #a, #b, (long long)_va, (long long)_vb, __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        intptr_t _va = (intptr_t)(a); \
        intptr_t _vb = (intptr_t)(b); \
        if (_va == _vb) { \
            fprintf(stderr, "    ASSERTION FAILED: %s != %s (both are %lld at %s:%d)\n", \
                    #a, #b, (long long)_va, __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

#define ASSERT_GTE(a, b) \
    do { \
        intptr_t _va = (intptr_t)(a); \
        intptr_t _vb = (intptr_t)(b); \
        if (_va < _vb) { \
            fprintf(stderr, "    ASSERTION FAILED: %s >= %s (got %lld, expected >= %lld at %s:%d)\n", \
                    #a, #b, (long long)_va, (long long)_vb, __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

#define ASSERT_LTE(a, b) \
    do { \
        intptr_t _va = (intptr_t)(a); \
        intptr_t _vb = (intptr_t)(b); \
        if (_va > _vb) { \
            fprintf(stderr, "    ASSERTION FAILED: %s <= %s (got %lld, expected <= %lld at %s:%d)\n", \
                    #a, #b, (long long)_va, (long long)_vb, __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

#define ASSERT_STR_EQ(a, b) \
    do { \
        if (strcmp((a), (b)) != 0) { \
            fprintf(stderr, "    ASSERTION FAILED: '%s' == '%s' (at %s:%d)\n", (a), (b), __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

#endif /* TEST_FRAMEWORK_H */
