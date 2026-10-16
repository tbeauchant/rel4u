#include "../framework/test_framework.h"
#include "../../src/core/rel4u_rtt.h"

#include "../framework/test_resources.h"

static bool test_rtt_basic_and_backoff(void) {
    rel4u_rtt_t rtt;
    rel4u_rtt_init(&rtt, 200, 20, 2000);

    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), 200);

    // Feed first sample of 50ms
    rel4u_rtt_update_sample(&rtt, 50);
    ASSERT_EQ(rtt.srtt_ms, 50);
    ASSERT_EQ(rtt.rttvar_ms, 25);
    // RTO = 50 + 4 * 25 = 150ms
    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), 150);

    // Feed second sample of 50ms
    rel4u_rtt_update_sample(&rtt, 50);
    // SRTT = (7 * 50 + 50) / 8 = 50
    // Diff = 0, RTTVAR = (3 * 25 + 0) / 4 = 18
    // RTO = 50 + 4 * 18 = 122ms
    ASSERT_EQ(rtt.srtt_ms, 50);
    ASSERT_EQ(rtt.rttvar_ms, 18);
    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), 122);

    // Feed third sample of 90ms (sample > srtt -> negative diff branch)
    rel4u_rtt_update_sample(&rtt, 90);
    // diff = |50 - 90| = 40
    // RTTVAR = (3 * 18 + 40) / 4 = 23
    // SRTT = (7 * 50 + 90) / 8 = 55
    // RTO = 55 + 4 * 23 = 147ms
    ASSERT_EQ(rtt.srtt_ms, 55);
    ASSERT_EQ(rtt.rttvar_ms, 23);
    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), 147);

    // Feed fourth sample of 15ms (sample < srtt -> positive diff branch)
    rel4u_rtt_update_sample(&rtt, 15);
    // diff = |55 - 15| = 40
    // RTTVAR = (3 * 23 + 40) / 4 = 27
    // SRTT = (7 * 55 + 15) / 8 = 50
    // RTO = 50 + 4 * 27 = 158ms
    ASSERT_EQ(rtt.srtt_ms, 50);
    ASSERT_EQ(rtt.rttvar_ms, 27);
    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), 158);

    // Test timeout exponential backoff
    uint32_t current_rto = rel4u_rtt_get_rto(&rtt);
    rel4u_rtt_on_timeout(&rtt);
    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), current_rto * 2);

    rel4u_rtt_on_timeout(&rtt);
    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), current_rto * 4);

    return true;
}

static bool test_rtt_clamp_and_defaults(void) {
    rel4u_rtt_t rtt;

    // Test default fallback when 0 is passed
    rel4u_rtt_init(&rtt, 0, 0, 0);
    ASSERT_EQ(rtt.min_rto_ms, REL4U_RTT_MIN_RTO_MS);
    ASSERT_EQ(rtt.max_rto_ms, REL4U_RTT_MAX_RTO_MS);
    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), REL4U_RTT_DEFAULT_RTO_MS);

    // Test initial_rto_ms clamping below min_rto_ms and above max_rto_ms
    rel4u_rtt_init(&rtt, 5, 50, 500);
    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), 50);
    rel4u_rtt_init(&rtt, 900, 50, 500);
    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), 500);

    // Test zero-ms sample floor (rttvar_ms >= 1 on both initial and subsequent samples)
    rel4u_rtt_init(&rtt, 100, 1, 1000);
    rel4u_rtt_update_sample(&rtt, 0);
    ASSERT_EQ(rtt.srtt_ms, 0);
    ASSERT_EQ(rtt.rttvar_ms, 1);
    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), 4);
    rel4u_rtt_update_sample(&rtt, 0);
    ASSERT_EQ(rtt.srtt_ms, 0);
    ASSERT_EQ(rtt.rttvar_ms, 1);
    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), 4);

    // Test min_rto clamp and max_rto clamp on update_sample
    rel4u_rtt_init(&rtt, 100, 50, 500);
    // Sample of 2ms produces raw RTO = 2 + 4 * 1 = 6ms, which must clamp to min 50ms
    rel4u_rtt_update_sample(&rtt, 2);
    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), 50);
    // Large sample produces raw RTO > 500ms, which must clamp to max 500ms
    rel4u_rtt_update_sample(&rtt, 1000);
    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), 500);

    // Test max_rto clamp on exponential backoff
    rel4u_rtt_init(&rtt, 200, 20, 500);
    rel4u_rtt_on_timeout(&rtt); // 200 * 2 = 400ms
    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), 400);
    rel4u_rtt_on_timeout(&rtt); // 400 * 2 = 800ms -> clamped to 500ms
    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), 500);
    rel4u_rtt_on_timeout(&rtt); // still clamped to 500ms
    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), 500);

    // Test NULL pointer safety
    rel4u_rtt_init(NULL, 0, 0, 0);
    rel4u_rtt_update_sample(NULL, 100);
    rel4u_rtt_on_timeout(NULL);
    ASSERT_EQ(rel4u_rtt_get_rto(NULL), REL4U_RTT_DEFAULT_RTO_MS);

    return true;
}

TEST_SUITE_BEGIN("RTT / RTO Estimation Unit Tests")
    RUN_TEST(test_rtt_basic_and_backoff);
    RUN_TEST(test_rtt_clamp_and_defaults);
TEST_SUITE_END()
