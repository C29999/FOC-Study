/* DRV8313 3PWM port for the existing Seekfree TC264 / Infineon iLLD project.
 * ATOM0 CH3 is the common period/trigger channel. CH4, CH5 and CH6 generate
 * centered pulses on V3.1 mainboard brushed-motor header 2 (P7). All three
 * shadow pairs are enabled together at the common period boundary.
 */
#include "foc_port.h"
#include "foc_config.h"
#include "Gtm/Atom/Timer/IfxGtm_Atom_Timer.h"
#include "Gtm/Std/IfxGtm_Cmu.h"
#include "Gtm/Std/IfxGtm.h"
#include "IfxCpu.h"
#include "zf_driver_gpio.h"
#include "zf_driver_exti.h"

static volatile bool port_ready;
static volatile bool pwm_running;
static volatile bool driver_awake;
static IfxGtm_Atom_Timer pwm_timer;
static uint32 period_ticks;
static float pwm_frequency_hz;

#define FOC_ATOM_CLOCK_HZ          (20000000.0f)
#define FOC_ATOM_TIMER_CHANNEL     IfxGtm_Atom_Ch_3
#define FOC_ATOM_PWM_A_CHANNEL     IfxGtm_Atom_Ch_4
#define FOC_ATOM_PWM_B_CHANNEL     IfxGtm_Atom_Ch_5
#define FOC_ATOM_PWM_C_CHANNEL     IfxGtm_Atom_Ch_6
#define FOC_ATOM_PWM_MASK          ((uint16)((1u << 4) | (1u << 5) | (1u << 6)))
#define FOC_ATOM_ALL_MASK          ((uint16)((1u << 3) | FOC_ATOM_PWM_MASK))

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
    IfxGtm_Atom_Timer_Config timer_config;
    Ifx_GTM_ATOM *atom;
    Ifx_GTM_ATOM_AGC *agc;

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

    IfxGtm_enable(&MODULE_GTM);
    if ((MODULE_GTM.CMU.CLK_EN.U & 0x2u) == 0u)
    {
        IfxGtm_Cmu_setClkFrequency(&MODULE_GTM, IfxGtm_Cmu_Clk_0,
            FOC_ATOM_CLOCK_HZ);
        IfxGtm_Cmu_enableClocks(&MODULE_GTM, IFXGTM_CMU_CLKEN_CLK0);
    }

    IfxGtm_Atom_Timer_initConfig(&timer_config, &MODULE_GTM);
    timer_config.atom = IfxGtm_Atom_0;
    timer_config.timerChannel = FOC_ATOM_TIMER_CHANNEL;
    timer_config.clock = IfxGtm_Cmu_Clk_0;
    timer_config.base.frequency = (float32)FOC_PWM_FREQUENCY_HZ;
    timer_config.base.isrPriority = 0u;
    timer_config.base.trigger.enabled = FALSE;
    timer_config.initPins = FALSE;
    if (!IfxGtm_Atom_Timer_init(&pwm_timer, &timer_config))
    {
        return false;
    }

    atom = pwm_timer.atom;
    agc = pwm_timer.agc;
    period_ticks = (uint32)IfxGtm_Atom_Timer_getPeriod(&pwm_timer);
    pwm_frequency_hz = IfxGtm_Atom_Timer_getFrequency(&pwm_timer);
    if (period_ticks < 10u || !(pwm_frequency_hz > 0.0f))
    {
        return false;
    }

    /* SL=low makes CM1 the rising edge and CM0 the falling edge. Writing
     * CM1=(period-on)/2 and CM0=(period+on)/2 centers every high pulse.
     * All phase counters reset from CH3's common output trigger.
     */
    IfxGtm_Atom_Ch_configurePwmMode(atom, FOC_ATOM_PWM_A_CHANNEL,
        IfxGtm_Cmu_Clk_0, Ifx_ActiveState_low,
        IfxGtm_Atom_Ch_ResetEvent_onTrigger, IfxGtm_Atom_Ch_OutputTrigger_forward);
    IfxGtm_Atom_Ch_configurePwmMode(atom, FOC_ATOM_PWM_B_CHANNEL,
        IfxGtm_Cmu_Clk_0, Ifx_ActiveState_low,
        IfxGtm_Atom_Ch_ResetEvent_onTrigger, IfxGtm_Atom_Ch_OutputTrigger_forward);
    IfxGtm_Atom_Ch_configurePwmMode(atom, FOC_ATOM_PWM_C_CHANNEL,
        IfxGtm_Cmu_Clk_0, Ifx_ActiveState_low,
        IfxGtm_Atom_Ch_ResetEvent_onTrigger, IfxGtm_Atom_Ch_OutputTrigger_forward);
    IfxGtm_PinMap_setAtomTout(FOC_PWM_A_OUTPUT, IfxPort_OutputMode_pushPull,
        IfxPort_PadDriver_cmosAutomotiveSpeed1);
    IfxGtm_PinMap_setAtomTout(FOC_PWM_B_OUTPUT, IfxPort_OutputMode_pushPull,
        IfxPort_PadDriver_cmosAutomotiveSpeed1);
    IfxGtm_PinMap_setAtomTout(FOC_PWM_C_OUTPUT, IfxPort_OutputMode_pushPull,
        IfxPort_PadDriver_cmosAutomotiveSpeed1);

    IfxGtm_Atom_Timer_addToChannelMask(&pwm_timer, FOC_ATOM_PWM_A_CHANNEL);
    IfxGtm_Atom_Timer_addToChannelMask(&pwm_timer, FOC_ATOM_PWM_B_CHANNEL);
    IfxGtm_Atom_Timer_addToChannelMask(&pwm_timer, FOC_ATOM_PWM_C_CHANNEL);
    IfxGtm_Atom_Agc_enableChannelsOutput(agc, 0u, FOC_ATOM_PWM_MASK, TRUE);
    IfxGtm_Atom_Agc_enableChannels(agc, 0u, FOC_ATOM_ALL_MASK, TRUE);

    port_ready = true;
    foc_port_update_duties(0.5f, 0.5f, 0.5f);
    return true;
}

static uint32 duty_to_on_ticks(float duty)
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

    return (uint32)(duty * (float)period_ticks + 0.5f);
}

void foc_port_update_duties(float duty_a, float duty_b, float duty_c)
{
    Ifx_GTM_ATOM *atom = pwm_timer.atom;
    uint32 on_a;
    uint32 on_b;
    uint32 on_c;
    if (!port_ready)
    {
        return;
    }
    on_a = duty_to_on_ticks(duty_a);
    on_b = duty_to_on_ticks(duty_b);
    on_c = duty_to_on_ticks(duty_c);

    /* Disable ATOM shadow updates before writing any phase. This prevents a
     * period boundary during these writes from transferring a mixed set.
     * Re-enabling the common mask transfers all three pairs together at the
     * next CH3 period boundary. No live compare registers are written.
     */
    IfxGtm_Atom_Timer_disableUpdate(&pwm_timer);
    IfxGtm_Atom_Ch_setCompareShadow(atom, FOC_ATOM_PWM_A_CHANNEL,
        (period_ticks + on_a) / 2u, (period_ticks - on_a) / 2u);
    IfxGtm_Atom_Ch_setCompareShadow(atom, FOC_ATOM_PWM_B_CHANNEL,
        (period_ticks + on_b) / 2u, (period_ticks - on_b) / 2u);
    IfxGtm_Atom_Ch_setCompareShadow(atom, FOC_ATOM_PWM_C_CHANNEL,
        (period_ticks + on_c) / 2u, (period_ticks - on_c) / 2u);
    IfxGtm_Atom_Timer_applyUpdate(&pwm_timer);
}

void foc_port_start_pwm(void)
{
    Ifx_GTM_ATOM_AGC *agc = pwm_timer.agc;
    foc_port_disable_driver_immediate();
    if (!port_ready || !driver_awake || foc_port_fault_active())
    {
        return;
    }
    foc_port_update_duties(0.5f, 0.5f, 0.5f);
    /* Load every shadow and reset every counter before enabling outputs. */
    IfxGtm_Atom_Agc_setChannelsForceUpdate(agc, FOC_ATOM_ALL_MASK, 0u,
        FOC_ATOM_ALL_MASK, 0u);
    IfxGtm_Atom_Agc_enableChannels(agc, FOC_ATOM_ALL_MASK, 0u, FALSE);
    IfxGtm_Atom_Agc_enableChannelsOutput(agc, FOC_ATOM_PWM_MASK, 0u, FALSE);
    IfxGtm_Atom_Agc_trigger(agc);
    IfxGtm_Atom_Agc_setChannelsForceUpdate(agc, 0u, FOC_ATOM_ALL_MASK, 0u, 0u);
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
    Ifx_GTM_ATOM_AGC *agc = pwm_timer.agc;
    foc_port_disable_driver_immediate();
    if (port_ready)
    {
        IfxGtm_Atom_Agc_enableChannelsOutput(agc, 0u, FOC_ATOM_PWM_MASK, TRUE);
        IfxGtm_Atom_Agc_enableChannels(agc, 0u, FOC_ATOM_ALL_MASK, TRUE);
        IfxGtm_Atom_Timer_disableUpdate(&pwm_timer);
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
