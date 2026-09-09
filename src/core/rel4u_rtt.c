#include "rel4u_rtt.h"
#include <stdlib.h>

void rel4u_rtt_init(rel4u_rtt_t* rtt, uint32_t initial_rto_ms, uint32_t min_rto_ms, uint32_t max_rto_ms) {
    if (!rtt) return;
    rtt->min_rto_ms = (min_rto_ms > 0) ? min_rto_ms : REL4U_RTT_MIN_RTO_MS;
    rtt->max_rto_ms = (max_rto_ms > 0) ? max_rto_ms : REL4U_RTT_MAX_RTO_MS;
    rtt->rto_ms = (initial_rto_ms > 0) ? initial_rto_ms : REL4U_RTT_DEFAULT_RTO_MS;
    if (rtt->rto_ms < rtt->min_rto_ms) rtt->rto_ms = rtt->min_rto_ms;
    if (rtt->rto_ms > rtt->max_rto_ms) rtt->rto_ms = rtt->max_rto_ms;
    rtt->srtt_ms = 0;
    rtt->rttvar_ms = 0;
    rtt->has_sample = false;
}

void rel4u_rtt_update_sample(rel4u_rtt_t* rtt, uint32_t sample_rtt_ms) {
    if (!rtt) return;

    if (!rtt->has_sample) {
        rtt->srtt_ms = sample_rtt_ms;
        rtt->rttvar_ms = sample_rtt_ms / 2;
        if (rtt->rttvar_ms < 1) rtt->rttvar_ms = 1;
        rtt->has_sample = true;
    } else {
        int32_t diff = (int32_t)rtt->srtt_ms - (int32_t)sample_rtt_ms;
        if (diff < 0) diff = -diff;

        /* RTTVAR = (1 - 1/4) * RTTVAR + 1/4 * |SRTT - R| */
        rtt->rttvar_ms = (3 * rtt->rttvar_ms + (uint32_t)diff) / 4;
        if (rtt->rttvar_ms < 1) rtt->rttvar_ms = 1;

        /* SRTT = (1 - 1/8) * SRTT + 1/8 * R */
        rtt->srtt_ms = (7 * rtt->srtt_ms + sample_rtt_ms) / 8;
    }

    /* RTO = SRTT + max(G, 4 * RTTVAR) where G = 1ms */
    uint32_t k_var = 4 * rtt->rttvar_ms;
    if (k_var < 1) k_var = 1;
    rtt->rto_ms = rtt->srtt_ms + k_var;

    if (rtt->rto_ms < rtt->min_rto_ms) rtt->rto_ms = rtt->min_rto_ms;
    if (rtt->rto_ms > rtt->max_rto_ms) rtt->rto_ms = rtt->max_rto_ms;
}

void rel4u_rtt_on_timeout(rel4u_rtt_t* rtt) {
    if (!rtt) return;
    /* Exponential backoff: RTO = min(2 * RTO, max_rto) */
    rtt->rto_ms *= 2;
    if (rtt->rto_ms > rtt->max_rto_ms) {
        rtt->rto_ms = rtt->max_rto_ms;
    }
}

uint32_t rel4u_rtt_get_rto(const rel4u_rtt_t* rtt) {
    return rtt ? rtt->rto_ms : REL4U_RTT_DEFAULT_RTO_MS;
}
