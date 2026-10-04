/* SPDX-License-Identifier: GPL-3.0-or-later
 * TC264 + DRV8313 voltage open loop, frequency in ELECTRICAL Hz.
 * No current feedback, rotor observer, or assumed pole-pair count.
 */
#ifndef FOC_H
#define FOC_H
#include <stdbool.h>
#include <stdint.h>
typedef enum {
    FOC_STOPPED, FOC_RESET_WAIT, FOC_WAKE_WAIT, FOC_PWM_WAIT,
    FOC_ALIGN, FOC_RAMP, FOC_RUNNING, FOC_CLEAR_RESET,
    FOC_CLEAR_WAKE, FOC_FAULT
} foc_state_t;
typedef struct {
    foc_state_t state;
    bool fault_latched, enabled;
    float target_frequency_hz, target_amplitude;
    float frequency_hz, amplitude, angle_rad;
    float duty_a, duty_b, duty_c, pwm_frequency_hz;
    uint32_t fault_count;
} foc_status_t;
bool foc_init(void);
bool foc_start(void);
void foc_stop(void);
void foc_tick_isr(void);
/* ERU: call before clearing event or enabling interrupt nesting. */
void foc_fault_isr(void);
/* Asynchronous reset/wake, keeps EN low; requires subsequent START. */
bool foc_clear_fault(void);
bool foc_set_frequency(float electrical_hz);
bool foc_set_amplitude(float modulation);
void foc_get_status(foc_status_t *status);
const char *foc_state_name(foc_state_t state);
#endif
