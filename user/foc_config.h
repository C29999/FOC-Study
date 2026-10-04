#ifndef FOC_CONFIG_H
#define FOC_CONFIG_H

/* Electrical frequency only. No pole-pair count or mechanical RPM is assumed. */
#define FOC_PWM_FREQUENCY_HZ        (20000u)
#define FOC_CONTROL_PERIOD_US       (100u)
#define FOC_CONTROL_DT_S            ((float)FOC_CONTROL_PERIOD_US * 0.000001f)
#define FOC_DEFAULT_FREQ_HZ         (5.0f)
#define FOC_DEFAULT_AMP             (0.05f)
#define FOC_MIN_FREQ_HZ             (0.1f)
#define FOC_MAX_FREQ_HZ             (100.0f)
#define FOC_MAX_AMP                 (0.15f)
#define FOC_ALIGN_AMP               (0.03f)
#define FOC_ALIGN_MS                (200u)
#define FOC_FREQ_RAMP_HZ_S          (2.0f)
#define FOC_AMP_RAMP_PER_S          (0.02f)
#define FOC_RESET_MS                (2u)
#define FOC_WAKE_MS                 (5u)
#define FOC_PWM_SETTLE_MS           (1u)
#define FOC_DUTY_MIN                (0.05f)
#define FOC_DUTY_MAX                (0.95f)

/* TC264 pin tokens are expanded only by the target port, not by host tests.
 * CCU61 T12 is exclusively reserved for all three PWM inputs; CCU60_CH0
 * remains the 100 us control PIT. Do not call motor_init() in this program:
 * its old ATOM2 output P33.6 conflicts with the new nSLEEP connection.
 */
#define FOC_PWM_MODULE              (&MODULE_CCU61)
#define FOC_PWM_A_PIN               P00_1
#define FOC_PWM_B_PIN               P00_3
#define FOC_PWM_C_PIN               P00_5
#define FOC_PWM_A_OUTPUT            (&IfxCcu61_CC60_P00_1_OUT)
#define FOC_PWM_B_OUTPUT            (&IfxCcu61_CC61_P00_3_OUT)
#define FOC_PWM_C_OUTPUT            (&IfxCcu61_CC62_P00_5_OUT)
#define FOC_EN_PIN                  P33_5
#define FOC_NSLEEP_PIN              P33_6
#define FOC_NRESET_PIN              P33_7
#define FOC_NFAULT_PIN              P15_4
#define FOC_NFAULT_EXTI             ERU_CH0_REQ0_P15_4

#endif
