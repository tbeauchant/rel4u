#include "../framework/test_framework.h"

static int cleanup_order[2];
static size_t cleanup_calls;

static void record_cleanup(void* object) {
    cleanup_order[cleanup_calls++] = *(int*)object;
}

static bool intentionally_failing_test(void) {
    int first = 1, second = 2;
    test_cleanup_register(&first, record_cleanup);
    test_cleanup_register(&second, record_cleanup);
    fprintf(stderr, "  [EXPECTED] exercising assertion failure cleanup\n");
    ASSERT_TRUE(false);
    return true;
}

static bool test_assertion_unwinds_resources(void) {
    ASSERT_FALSE(intentionally_failing_test());
    ASSERT_EQ(cleanup_calls, 2);
    ASSERT_EQ(cleanup_order[0], 2);
    ASSERT_EQ(cleanup_order[1], 1);
    ASSERT_EQ(g_test_cleanup_count, 0);
    return true;
}

static bool test_explicit_cleanup_removes_registration(void) {
    int object = 3;
    test_cleanup_register(&object, record_cleanup);
    test_cleanup_remove(&object);
    test_cleanup_run();
    ASSERT_EQ(cleanup_calls, 2);
    return true;
}

TEST_SUITE_BEGIN("Test framework cleanup")
    RUN_TEST(test_assertion_unwinds_resources);
    RUN_TEST(test_explicit_cleanup_removes_registration);
TEST_SUITE_END()
