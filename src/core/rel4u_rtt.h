#ifndef REL4U_RTT_H
#define REL4U_RTT_H

#include <stdint.h>
#include <stdbool.h>

#define REL4U_RTT_DEFAULT_RTO_MS 200
#define REL4U_RTT_MIN_RTO_MS     20
#define REL4U_RTT_MAX_RTO_MS     2000

typedef struct rel4u_rtt {
    uint32_t srtt_ms;      /* Smoothed Round Trip Time */
    uint32_t rttvar_ms;    /* RTT Variance */
    uint32_t rto_ms;       /* Current Retransmission Timeout */
    uint32_t min_rto_ms;
    uint32_t max_rto_ms;
    bool     has_sample;   /* True once first sample has been processed */
} rel4u_rtt_t;

void rel4u_rtt_init(rel4u_rtt_t* rtt, uint32_t initial_rto_ms, uint32_t min_rto_ms, uint32_t max_rto_ms);
void rel4u_rtt_update_sample(rel4u_rtt_t* rtt, uint32_t sample_rtt_ms);
void rel4u_rtt_on_timeout(rel4u_rtt_t* rtt);
uint32_t rel4u_rtt_get_rto(const rel4u_rtt_t* rtt);

#endif /* REL4U_RTT_H */
