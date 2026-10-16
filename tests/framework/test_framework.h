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
#include <stdlib.h>

/* Run failure cleanups before returning, while test-local objects still exist.
 * Entries are removed on explicit destruction and unwind in reverse order. */
typedef void (*test_cleanup_fn)(void*);
typedef struct test_cleanup_entry {
    void* object;
    test_cleanup_fn destroy;
} test_cleanup_entry_t;

static test_cleanup_entry_t g_test_cleanups[64];
static size_t g_test_cleanup_count;

static inline void test_cleanup_register(void* object, test_cleanup_fn destroy) {
    if (!object) return;
    if (g_test_cleanup_count == 64) abort();
    g_test_cleanups[g_test_cleanup_count++] = (test_cleanup_entry_t){object, destroy};
}

static inline void test_cleanup_remove(void* object) {
    for (size_t i = g_test_cleanup_count; i > 0; i--) {
        if (g_test_cleanups[i - 1].object == object) {
            memmove(&g_test_cleanups[i - 1], &g_test_cleanups[i],
                    (g_test_cleanup_count - i) * sizeof(g_test_cleanups[0]));
            g_test_cleanup_count--;
            return;
        }
    }
}

static inline void test_cleanup_run(void) {
    while (g_test_cleanup_count > 0) {
        test_cleanup_entry_t entry = g_test_cleanups[--g_test_cleanup_count];
        entry.destroy(entry.object);
    }
}

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
            if (g_test_cleanup_count != 0) { \
                fprintf(stderr, "Unreleased test resources in %s\n", #test_func); \
                abort(); \
            } \
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
            test_cleanup_run(); \
            return false; \
        } \
    } while (0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

/* Callers include rel4u_time.h. Poll the observable condition with a deadline. */
#define ASSERT_EVENTUALLY(expr, timeout_ms) \
    do { \
        uint64_t _deadline = rel4u_time_now_ms() + (uint64_t)(timeout_ms); \
        bool _satisfied = false; \
        do { \
            if ((expr)) { _satisfied = true; break; } \
            rel4u_time_sleep_ms(1); \
        } while (rel4u_time_now_ms() < _deadline); \
        ASSERT_TRUE(_satisfied); \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        intptr_t _va = (intptr_t)(a); \
        intptr_t _vb = (intptr_t)(b); \
        if (_va != _vb) { \
            fprintf(stderr, "    ASSERTION FAILED: %s == %s (got %lld, expected %lld at %s:%d)\n", \
                    #a, #b, (long long)_va, (long long)_vb, __FILE__, __LINE__); \
            test_cleanup_run(); \
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
            test_cleanup_run(); \
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
            test_cleanup_run(); \
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
            test_cleanup_run(); \
            return false; \
        } \
    } while (0)

#define ASSERT_STR_EQ(a, b) \
    do { \
        if (strcmp((a), (b)) != 0) { \
            fprintf(stderr, "    ASSERTION FAILED: '%s' == '%s' (at %s:%d)\n", (a), (b), __FILE__, __LINE__); \
            test_cleanup_run(); \
            return false; \
        } \
    } while (0)

#endif /* TEST_FRAMEWORK_H */
