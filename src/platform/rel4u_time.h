#ifndef REL4U_TIME_H
#define REL4U_TIME_H

#include <stdint.h>

/**
 * @brief Returns current monotonic time in nanoseconds.
 */
uint64_t rel4u_time_now_ns(void);

/**
 * @brief Returns current monotonic time in milliseconds.
 */
uint64_t rel4u_time_now_ms(void);

/**
 * @brief Sleep for specified milliseconds.
 */
void rel4u_time_sleep_ms(uint32_t ms);

#endif /* REL4U_TIME_H */
