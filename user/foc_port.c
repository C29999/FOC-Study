/* DRV8313 3PWM port for the existing Seekfree TC264 / Infineon iLLD project.
 * CCU61 T12 has ONE shared up/down counter for CC60, CC61 and CC62.
 * Only CC outputs are routed. COUT complementary outputs and dead time are
 * disabled: the DRV8313 performs the half-bridge gate control itself.
 */
#include "foc_port.h"
#include "foc_config.h"
#include "IfxCcu6.h"
#include "IfxCpu.h"
#include "zf_driver_gpio.h"
#include "zf_driver_exti.h"

static volatile bool port_ready;
static volatile bool pwm_running;
static volatile bool driver_awake;
static uint32 half_period_ticks;
static float pwm_frequency_hz;

/* Preload the low output latch before switching an input to output mode.
 * This controls the pin only after firmware runs; it does not establish an
 * EN default during MCU reset. The original Mini has a SERIES 10 k resistor
 * on EN, not an external pull-down. DRV ENx inputs have internal pull-downs;
 * verify the complete reset-state circuit before adding external bias.
 */
static void output_init_low(gpio_pin_enum pin)
{
    Ifx_P *port = get_port(pin);
    uint8 index = (uint8)((unsigned int)pin & 31u);
    IfxPort_setPinLow(port, index);
    IfxPort_setPinMode(port, index, IfxPort_Mode_outputPushPullGeneral);
    IfxPort_setPinPadDriver(port, index, IfxPort_PadDriver_cmosAutomotiveSpeed1);
}

void foc_port_disable_driver_immediate(void)
{
    /* One atomic port OMR clear, before any timer/status manipulation. */
    IfxPort_setPinLow(get_port(FOC_EN_PIN), (uint8)((unsigned int)FOC_EN_PIN & 31u));
}

bool foc_port_fault_active(void)
{
    return IfxPort_getPinState(get_port(FOC_NFAULT_PIN),
        (uint8)((unsigned int)FOC_NFAULT_PIN & 31u)) == FALSE;
}

unsigned int foc_port_enter_critical(void)
{
    return (unsigned int)IfxCpu_disableInterrupts();
}

void foc_port_exit_critical(unsigned int state)
{
    IfxCpu_restoreInterrupts((boolean)(state != 0u));
}

bool foc_port_init(void)
{
    Ifx_CCU6 *ccu6 = FOC_PWM_MODULE;
    float timer_clock_hz;
    uint32 prescaler;
    uint32 half_ticks;
    IfxCcu6_T12Channel channel;

    /* EN is the first hardware pin configured by this port. */
    output_init_low(FOC_EN_PIN);
    output_init_low(FOC_NSLEEP_PIN);
    output_init_low(FOC_NRESET_PIN);
    output_init_low(FOC_PWM_A_PIN);
    output_init_low(FOC_PWM_B_PIN);
    output_init_low(FOC_PWM_C_PIN);
    port_ready = false;
    pwm_running = false;
    driver_awake = false;
    pwm_frequency_hz = 0.0f;

    /* nFAULT is open drain. The Mini already has its 3.3 V pull-up.
     * P15.4 is an MP/VEXT input. Select TTL with hysteresis: in the TC26x
     * data sheet this guarantees VIH <= 2.03 V for VEXT <= 5.5 V, so the
     * Mini's 3.3 V high is valid even when the TC264 I/O domain is 5 V.
     * Disable the MCU pull device and use only the Mini's external pull-up.
     * TC26x UM 14.3.2/Table 14-6: PLx=1 selects TTL, PDx=1 hysteresis.
     * Seekfree ERU0 falling-edge interrupt uses EXTI_CH0_CH4_INT_PRIO = 255
     * in isr_config.h. Its ISR must pull EN low before clearing the flag.
     */
    exti_flag_clear(FOC_NFAULT_EXTI);
    exti_init(FOC_NFAULT_EXTI, EXTI_TRIGGER_FALLING);
    IfxPort_setPinMode(get_port(FOC_NFAULT_PIN),
        (uint8)((unsigned int)FOC_NFAULT_PIN & 31u), IfxPort_Mode_inputNoPullDevice);
    IfxPort_setPinPadDriver(get_port(FOC_NFAULT_PIN),
        (uint8)((unsigned int)FOC_NFAULT_PIN & 31u), IfxPort_PadDriver_ttlSpeed2);

    IfxCcu6_enableModule(ccu6);
    IfxCcu6_stopTimer(ccu6, TRUE, TRUE);
    ccu6->MODCTR.U = 0u;
    ccu6->IEN.U = 0u;              /* PWM timer itself has no CPU interrupts. */
    ccu6->ISR.U = 0xFFFFu;
    ccu6->TCTR2.U = 0u;            /* No external/automatic timer restart. */
    ccu6->T12MSEL.U = 0u;
    ccu6->T12DTC.U = 0u;
    ccu6->TRPCTR.U = 0u;           /* nFAULT is wired to ERU, not CTRAP. */
    IfxCcu6_enableTimer(ccu6, IfxCcu6_TimerId_t12);
    IfxCcu6_disableTimer(ccu6, IfxCcu6_TimerId_t13);
    IfxCcu6_setCountingInputMode(ccu6, IfxCcu6_TimerId_t12,
        IfxCcu6_CountingInputMode_internal);
    IfxCcu6_disableSingleShotMode(ccu6, IfxCcu6_TimerId_t12);

    /* iLLD IfxCcu6_setT12Frequency() uses the SPB peripheral clock as fCC6.
     * Calculate from that actual clock, not from the 200 MHz CPU clock.
     * Full center-aligned period = 2 * (T12PR + 1) timer ticks.
     */
    timer_clock_hz = IfxScuCcu_getSpbFrequency();
    if (!(timer_clock_hz > 0.0f && timer_clock_hz <= 300000000.0f))
    {
        return false;
    }

    half_ticks = 0u;
    for (prescaler = 0u; prescaler < 16u; ++prescaler)
    {
        half_ticks = (uint32)(timer_clock_hz /
            (2.0f * (float)FOC_PWM_FREQUENCY_HZ) + 0.5f);
        if (half_ticks >= 2u && half_ticks <= 65536u)
        {
            break;
        }
        timer_clock_hz *= 0.5f;
    }
    if (prescaler >= 16u)
    {
        return false;
    }
    IfxCcu6_setInputClockFrequency(ccu6, IfxCcu6_TimerId_t12,
        (IfxCcu6_TimerInputClock)(prescaler & 7u));
    if (prescaler >= 8u)
    {
        IfxCcu6_enableAdditionalPrescaler(ccu6, IfxCcu6_TimerId_t12);
    }
    else
    {
        IfxCcu6_disableAdditionalPrescaler(ccu6, IfxCcu6_TimerId_t12);
    }
    half_period_ticks = half_ticks;
    pwm_frequency_hz = timer_clock_hz / (2.0f * (float)half_ticks);
    IfxCcu6_setT12CountMode(ccu6, IfxCcu6_T12CountMode_centerAligned);
    IfxCcu6_setT12PeriodValue(ccu6, (uint16)(half_ticks - 1u));
    IfxCcu6_clearCounter(ccu6, TRUE, FALSE);

    for (channel = IfxCcu6_T12Channel_0; channel <= IfxCcu6_T12Channel_2;
        channel = (IfxCcu6_T12Channel)((unsigned int)channel + 1u))
    {
        IfxCcu6_setT12ChannelMode(ccu6, channel, IfxCcu6_T12ChannelMode_compareMode);
        IfxCcu6_disableDeadTime(ccu6, channel);
        IfxCcu6_setT12CaptureCompareState(ccu6, channel, IfxCcu6_CaptureCompareState_clear);
        IfxCcu6_setOutputPassiveState(ccu6, (IfxCcu6_ChannelOut)(2u * channel), FALSE);
        IfxCcu6_setOutputPassiveLevel(ccu6, (IfxCcu6_ChannelOut)(2u * channel), FALSE);
    }

    IfxCcu6_initCc60OutPin(FOC_PWM_A_OUTPUT, IfxPort_OutputMode_pushPull,
        IfxPort_PadDriver_cmosAutomotiveSpeed1);
    IfxCcu6_initCc61OutPin(FOC_PWM_B_OUTPUT, IfxPort_OutputMode_pushPull,
        IfxPort_PadDriver_cmosAutomotiveSpeed1);
    IfxCcu6_initCc62OutPin(FOC_PWM_C_OUTPUT, IfxPort_OutputMode_pushPull,
        IfxPort_PadDriver_cmosAutomotiveSpeed1);

    port_ready = true;
    foc_port_update_duties(0.5f, 0.5f, 0.5f);
    return true;
}

static uint16 duty_to_compare(float duty)
{
    /* Includes NaN rejection before the float-to-integer conversion. */
    if (!(duty >= FOC_DUTY_MIN))
    {
        duty = FOC_DUTY_MIN;
    }
    else if (duty > FOC_DUTY_MAX)
    {
        duty = FOC_DUTY_MAX;
    }

    /* For active-high CC6xST center PWM: compare = (period - onTime) / 2.
     * See the timing example in iLLD IfxCcu6_PwmHl_setOnTime().
     */
    return (uint16)((1.0f - duty) * (float)half_period_ticks + 0.5f);
}

void foc_port_update_duties(float duty_a, float duty_b, float duty_c)
{
    Ifx_CCU6 *ccu6 = FOC_PWM_MODULE;
    uint16 compare_a;
    uint16 compare_b;
    uint16 compare_c;
    if (!port_ready)
    {
        return;
    }
    compare_a = duty_to_compare(duty_a);
    compare_b = duty_to_compare(duty_b);
    compare_c = duty_to_compare(duty_c);

    /* Cancel a previous transfer request BEFORE writing any new shadow.
     * This prevents a PWM boundary during these writes transferring a mix
     * of old/new phases. One request transfers all CC6xSR synchronously at
     * the next T12 shadow-transfer boundary. No live compare writes occur.
     */
    IfxCcu6_disableShadowTransfer(ccu6, TRUE, FALSE);
    ccu6->CC60SR.U = compare_a;
    ccu6->CC61SR.U = compare_b;
    ccu6->CC62SR.U = compare_c;
    IfxCcu6_enableShadowTransfer(ccu6, TRUE, FALSE);
}

void foc_port_start_pwm(void)
{
    Ifx_CCU6 *ccu6 = FOC_PWM_MODULE;
    foc_port_disable_driver_immediate();
    if (!port_ready || !driver_awake || foc_port_fault_active())
    {
        return;
    }
    IfxCcu6_stopTimer(ccu6, TRUE, FALSE);
    IfxCcu6_clearCounter(ccu6, TRUE, FALSE);
    IfxCcu6_setT12CaptureCompareState(ccu6, IfxCcu6_T12Channel_0,
        IfxCcu6_CaptureCompareState_clear);
    IfxCcu6_setT12CaptureCompareState(ccu6, IfxCcu6_T12Channel_1,
        IfxCcu6_CaptureCompareState_clear);
    IfxCcu6_setT12CaptureCompareState(ccu6, IfxCcu6_T12Channel_2,
        IfxCcu6_CaptureCompareState_clear);
    foc_port_update_duties(0.5f, 0.5f, 0.5f);
    /* Enable CC60/CC61/CC62 only. Bits 1/3/5 (COUT) remain disabled. */
    ccu6->MODCTR.U = 0x15u;
    IfxCcu6_startTimer(ccu6, TRUE, FALSE);
    pwm_running = true;
    /* Caller waits FOC_PWM_SETTLE_MS before enabling the driver, allowing
     * period and all initial shadows to transfer while EN remains low.
     */
}

bool foc_port_enable_driver(void)
{
    bool success = false;
    unsigned int state = foc_port_enter_critical();
    /* An ERU flag also catches a brief low pulse that has already recovered.
     * Do not clear that event here; the fault ISR/control latch owns it.
     * The short critical section prevents returning from a preempting fault
     * ISR and then inadvertently executing an old EN-high instruction.
     */
    if (port_ready && driver_awake && pwm_running &&
        !foc_port_fault_active() && !exti_flag_get(FOC_NFAULT_EXTI))
    {
        IfxPort_setPinHigh(get_port(FOC_EN_PIN), (uint8)((unsigned int)FOC_EN_PIN & 31u));
        success = !foc_port_fault_active();
        if (!success)
        {
            foc_port_disable_driver_immediate();
        }
    }
    foc_port_exit_critical(state);
    return success;
}

void foc_port_disable(void)
{
    Ifx_CCU6 *ccu6 = FOC_PWM_MODULE;
    foc_port_disable_driver_immediate();
    if (port_ready)
    {
        ccu6->MODCTR.U = 0u;
        IfxCcu6_stopTimer(ccu6, TRUE, FALSE);
        IfxCcu6_disableShadowTransfer(ccu6, TRUE, FALSE);
    }
    pwm_running = false;
    /* Disabling PWM alone is not high impedance: EN=1 with IN=0 turns on
     * DRV8313 low-side switches. EN-low above is the high-Z action.
     */
}

void foc_port_set_awake(bool awake)
{
    foc_port_disable_driver_immediate();
    if (awake)
    {
        gpio_high(FOC_NRESET_PIN);
        gpio_high(FOC_NSLEEP_PIN);
    }
    else
    {
        gpio_low(FOC_NSLEEP_PIN);
        gpio_low(FOC_NRESET_PIN);
    }
    driver_awake = awake;
}

float foc_port_pwm_frequency_hz(void)
{
    return pwm_frequency_hz;
}
