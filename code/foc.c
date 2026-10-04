/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "foc.h"
#include "foc_config.h"
#include "foc_port.h"
#include <math.h>
#define FOC_TWO_PI 6.283185307179586f
#define FOC_PHASE_SHIFT 2.094395102393195f
#define FOC_SQRT3_OVER4 0.433012701892219f
static volatile foc_status_t motor;
static bool initialized;
static volatile bool monitor_fault;
static uint32_t wait_ticks;

static uint32_t ticks_for_ms(uint32_t ms)
{
    return (ms * 1000u + FOC_CONTROL_PERIOD_US - 1u) / FOC_CONTROL_PERIOD_US;
}
static float approach(float value, float target, float step)
{
    if (value < target) return (target - value < step) ? target : value + step;
    return (value - target < step) ? target : value - step;
}
static float clamp_duty(float value)
{
    if (value < FOC_DUTY_MIN) return FOC_DUTY_MIN;
    if (value > FOC_DUTY_MAX) return FOC_DUTY_MAX;
    return value;
}
/* Caller holds CPU0 critical section. EN is always removed first. */
static void latch_fault(void)
{
    foc_port_disable_driver_immediate();
    if (!motor.fault_latched) ++motor.fault_count;
    motor.fault_latched = true;
    motor.enabled = false;
    motor.state = FOC_FAULT;
    motor.frequency_hz = motor.amplitude = 0.0f;
    foc_port_disable();
}
bool foc_init(void)
{
    unsigned int key;
    bool ok = foc_port_init(); /* EN=0 is the first hardware action. */
    key = foc_port_enter_critical();
    motor.state = ok ? FOC_STOPPED : FOC_FAULT;
    motor.fault_latched = !ok;
    motor.enabled = false;
    motor.target_frequency_hz = FOC_DEFAULT_FREQ_HZ;
    motor.target_amplitude = FOC_DEFAULT_AMP;
    motor.frequency_hz = motor.amplitude = motor.angle_rad = 0.0f;
    motor.duty_a = motor.duty_b = motor.duty_c = 0.5f;
    motor.pwm_frequency_hz = foc_port_pwm_frequency_hz();
    motor.fault_count = ok ? 0u : 1u;
    initialized = ok;
    monitor_fault = false;
    wait_ticks = 0u;
    foc_port_exit_critical(key);
    return ok;
}
bool foc_start(void)
{
    bool accepted = false;
    unsigned int key = foc_port_enter_critical();
    if (initialized && !motor.fault_latched && motor.state == FOC_STOPPED &&
        motor.target_amplitude > 0.0f && motor.target_frequency_hz > 0.0f) {
        monitor_fault = false;
        foc_port_disable();
        foc_port_set_awake(false);
        motor.enabled = false;
        motor.frequency_hz = motor.amplitude = motor.angle_rad = 0.0f;
        motor.state = FOC_RESET_WAIT;
        wait_ticks = ticks_for_ms(FOC_RESET_MS);
        accepted = true;
    }
    foc_port_exit_critical(key);
    return accepted;
}
void foc_stop(void)
{
    unsigned int key = foc_port_enter_critical();
    foc_port_disable_driver_immediate();
    motor.enabled = false;
    monitor_fault = false;
    foc_port_disable();
    foc_port_set_awake(false);
    motor.frequency_hz = motor.amplitude = 0.0f;
    motor.state = motor.fault_latched ? FOC_FAULT : FOC_STOPPED;
    wait_ticks = 0u;
    foc_port_exit_critical(key);
}
void foc_fault_isr(void)
{
    unsigned int key;
    foc_port_disable_driver_immediate(); /* First action, even during wake. */
    key = foc_port_enter_critical();
    if (monitor_fault) latch_fault();
    /* Expected edges during sleep/reset are ignored; level checked after wake. */
    foc_port_exit_critical(key);
}
bool foc_clear_fault(void)
{
    bool accepted = false;
    unsigned int key = foc_port_enter_critical();
    if (initialized && motor.fault_latched && motor.state == FOC_FAULT) {
        foc_port_disable_driver_immediate();
        foc_port_disable();
        motor.enabled = false;
        monitor_fault = false;
        foc_port_set_awake(false);
        motor.state = FOC_CLEAR_RESET;
        wait_ticks = ticks_for_ms(FOC_RESET_MS);
        accepted = true;
    }
    foc_port_exit_critical(key);
    return accepted;
}
bool foc_set_frequency(float hz)
{
    unsigned int key;
    /* Ordered comparisons reject NaN/Inf. No mechanical RPM conversion. */
    if (!(hz >= FOC_MIN_FREQ_HZ && hz <= FOC_MAX_FREQ_HZ)) return false;
    key = foc_port_enter_critical();
    motor.target_frequency_hz = hz;
    foc_port_exit_critical(key);
    return true;
}
bool foc_set_amplitude(float amp)
{
    unsigned int key;
    if (!(amp >= 0.0f && amp <= FOC_MAX_AMP)) return false;
    key = foc_port_enter_critical();
    motor.target_amplitude = amp;
    foc_port_exit_critical(key);
    return true;
}
void foc_tick_isr(void)
{
    float angle, amp, a, b, c;
    bool output;
    unsigned int key = foc_port_enter_critical();
    if (!initialized) { foc_port_exit_critical(key); return; }
    if (monitor_fault && foc_port_fault_active()) latch_fault();
    if (wait_ticks != 0u) --wait_ticks;
    switch (motor.state) {
    case FOC_RESET_WAIT:
    case FOC_CLEAR_RESET:
        if (wait_ticks == 0u) {
            foc_port_set_awake(true);
            motor.state = (motor.state == FOC_CLEAR_RESET) ? FOC_CLEAR_WAKE : FOC_WAKE_WAIT;
            wait_ticks = ticks_for_ms(FOC_WAKE_MS);
        }
        break;
    case FOC_WAKE_WAIT:
        if (wait_ticks == 0u) {
            monitor_fault = true;
            if (foc_port_fault_active()) { latch_fault(); break; }
            motor.amplitude = (motor.target_amplitude < FOC_ALIGN_AMP) ?
                              motor.target_amplitude : FOC_ALIGN_AMP;
            motor.duty_a = 0.5f;
            motor.duty_b = clamp_duty(0.5f - FOC_SQRT3_OVER4 * motor.amplitude);
            motor.duty_c = clamp_duty(0.5f + FOC_SQRT3_OVER4 * motor.amplitude);
            foc_port_start_pwm();
            foc_port_update_duties(motor.duty_a, motor.duty_b, motor.duty_c);
            motor.state = FOC_PWM_WAIT;
            wait_ticks = ticks_for_ms(FOC_PWM_SETTLE_MS);
        }
        break;
    case FOC_PWM_WAIT:
        if (wait_ticks == 0u) {
            if (motor.fault_latched || !foc_port_enable_driver()) { latch_fault(); break; }
            motor.enabled = true;
            motor.state = FOC_ALIGN;
            wait_ticks = ticks_for_ms(FOC_ALIGN_MS);
        }
        break;
    case FOC_ALIGN:
        if (motor.amplitude > motor.target_amplitude) motor.amplitude = motor.target_amplitude;
        if (wait_ticks == 0u) motor.state = FOC_RAMP;
        break;
    case FOC_RAMP:
    case FOC_RUNNING:
        motor.frequency_hz = approach(motor.frequency_hz, motor.target_frequency_hz,
                                      FOC_FREQ_RAMP_HZ_S * FOC_CONTROL_DT_S);
        motor.amplitude = approach(motor.amplitude, motor.target_amplitude,
                                   FOC_AMP_RAMP_PER_S * FOC_CONTROL_DT_S);
        motor.angle_rad += FOC_TWO_PI * motor.frequency_hz * FOC_CONTROL_DT_S;
        if (motor.angle_rad >= FOC_TWO_PI) motor.angle_rad -= FOC_TWO_PI;
        motor.state = (motor.frequency_hz == motor.target_frequency_hz &&
                       motor.amplitude == motor.target_amplitude) ? FOC_RUNNING : FOC_RAMP;
        break;
    case FOC_CLEAR_WAKE:
        if (wait_ticks == 0u) {
            monitor_fault = true;
            if (foc_port_fault_active()) motor.state = FOC_FAULT;
            else { motor.fault_latched = false; motor.state = FOC_STOPPED; }
        }
        break;
    default: break;
    }
    output = motor.enabled && !motor.fault_latched;
    angle = motor.angle_rad;
    amp = motor.amplitude;
    foc_port_exit_critical(key);
    if (!output) return;
    /* Math runs with fault interrupt enabled. Never print UART in this ISR. */
    a = clamp_duty(0.5f + 0.5f * amp * sinf(angle));
    b = clamp_duty(0.5f + 0.5f * amp * sinf(angle - FOC_PHASE_SHIFT));
    c = clamp_duty(0.5f + 0.5f * amp * sinf(angle + FOC_PHASE_SHIFT));
    key = foc_port_enter_critical();
    /* A fault may have interrupted sine calculations. Never restore EN/state. */
    if (motor.enabled && !motor.fault_latched) {
        if (foc_port_fault_active()) latch_fault();
        else {
            motor.duty_a = a; motor.duty_b = b; motor.duty_c = c;
            foc_port_update_duties(a, b, c);
        }
    }
    foc_port_exit_critical(key);
}
void foc_get_status(foc_status_t *s)
{
    unsigned int key;
    if (s == 0) return;
    key = foc_port_enter_critical();
    s->state = motor.state;
    s->fault_latched = motor.fault_latched; s->enabled = motor.enabled;
    s->target_frequency_hz = motor.target_frequency_hz;
    s->target_amplitude = motor.target_amplitude;
    s->frequency_hz = motor.frequency_hz; s->amplitude = motor.amplitude;
    s->angle_rad = motor.angle_rad;
    s->duty_a = motor.duty_a; s->duty_b = motor.duty_b; s->duty_c = motor.duty_c;
    s->pwm_frequency_hz = motor.pwm_frequency_hz; s->fault_count = motor.fault_count;
    foc_port_exit_critical(key);
}
const char *foc_state_name(foc_state_t state)
{
    switch (state) {
    case FOC_STOPPED: return "STOPPED";
    case FOC_RESET_WAIT: return "RESET_WAIT";
    case FOC_WAKE_WAIT: return "WAKE_WAIT";
    case FOC_PWM_WAIT: return "PWM_WAIT";
    case FOC_ALIGN: return "ALIGN";
    case FOC_RAMP: return "RAMP";
    case FOC_RUNNING: return "RUNNING";
    case FOC_CLEAR_RESET: return "CLEAR_RESET";
    case FOC_CLEAR_WAKE: return "CLEAR_WAKE";
    case FOC_FAULT: return "FAULT";
    default: return "UNKNOWN";
    }
}
