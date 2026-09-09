#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

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
        if ((a) != (b)) { \
            fprintf(stderr, "    ASSERTION FAILED: %s == %s (at %s:%d)\n", #a, #b, __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            fprintf(stderr, "    ASSERTION FAILED: %s != %s (at %s:%d)\n", #a, #b, __FILE__, __LINE__); \
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
