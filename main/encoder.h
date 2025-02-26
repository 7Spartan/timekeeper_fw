#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

/**
 * @brief Initializes the rotary encoder.
 */
void encoder_init(void);

/**
 * @brief Gets the current count of the rotary encoder.
 *
 * @return int32_t The current count value.
 */
int32_t encoder_get_count(void);

/**
 * @brief Task to periodically print the encoder value.
 *
 * @param pvParameters Task parameters (unused).
 */
void encoder_print_task(void *pvParameters);

#endif // ENCODER_H
