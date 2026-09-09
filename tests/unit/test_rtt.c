#include "../framework/test_framework.h"
#include "../../src/core/rel4u_rtt.h"

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

    // Test timeout exponential backoff
    uint32_t current_rto = rel4u_rtt_get_rto(&rtt);
    rel4u_rtt_on_timeout(&rtt);
    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), current_rto * 2);

    rel4u_rtt_on_timeout(&rtt);
    ASSERT_EQ(rel4u_rtt_get_rto(&rtt), current_rto * 4);

    return true;
}

TEST_SUITE_BEGIN("RTT / RTO Estimation Unit Tests")
    RUN_TEST(test_rtt_basic_and_backoff);
TEST_SUITE_END()
