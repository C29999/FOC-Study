#ifndef FOC_PORT_H
#define FOC_PORT_H

#include <stdbool.h>

/* CPU0 owns this port. No motor operation is issued by CPU1. */
bool foc_port_init(void);
void foc_port_start_pwm(void);      /* EN remains low. Wait PWM_SETTLE_MS. */
bool foc_port_enable_driver(void);  /* Checks live nFAULT and pending ERU. */
void foc_port_disable_driver_immediate(void); /* First action of fault ISR. */
void foc_port_disable(void);        /* EN low, then PWM modulation/timer off. */
bool foc_port_fault_active(void);
void foc_port_set_awake(bool awake); /* Both nRESET and nSLEEP; EN held low. */
void foc_port_update_duties(float duty_a, float duty_b, float duty_c);
float foc_port_pwm_frequency_hz(void);
unsigned int foc_port_enter_critical(void);
void foc_port_exit_critical(unsigned int state);

#endif
