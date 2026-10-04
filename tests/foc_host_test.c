/* Execute the production C, mocking only board I/O. No independent PWM model. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "foc.h"
#include "foc_port.h"
#include "foc_config.h"
#include "foc_console.h"

enum event { EV_INIT, EV_EN_LOW, EV_PWM_OFF, EV_SLEEP, EV_AWAKE, EV_PWM_ON, EV_EN_HIGH };
static enum event events[256];
static unsigned event_count, checks, tick_count, pwm_on_tick, en_on_tick;
static bool driver_en, pwm_on, fault_input, port_init_ok, pwm_start_ok;
static bool refuse_enable;
static bool inject_fault_after_unlock;
static unsigned duty_updates;
static float last_a, last_b, last_c;

#define CHECK(expression) do { ++checks; if (!(expression)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); exit(1); } } while (0)

static void event(enum event e)
{
    if (event_count < sizeof(events) / sizeof(events[0])) events[event_count++] = e;
}

/* These mocks intentionally keep driver EN and PWM output independent. */
bool foc_port_init(void) { event(EV_INIT); driver_en = false; pwm_on = false; return port_init_ok; }
void foc_port_disable_driver_immediate(void) { driver_en = false; event(EV_EN_LOW); }
void foc_port_disable(void) { foc_port_disable_driver_immediate(); pwm_on = false; event(EV_PWM_OFF); }
void foc_port_set_awake(bool awake) { event(awake ? EV_AWAKE : EV_SLEEP); }
bool foc_port_fault_active(void) { return fault_input; }
void foc_port_start_pwm(void)
{
    event(EV_PWM_ON); pwm_on_tick = tick_count;
    if (pwm_start_ok) pwm_on = true;
}
bool foc_port_enable_driver(void)
{
    if (refuse_enable || fault_input || !pwm_on) return false;
    driver_en = true; en_on_tick = tick_count; event(EV_EN_HIGH); return true;
}
void foc_port_update_duties(float a, float b, float c)
{
    CHECK(a >= 0.0f && a <= 1.0f);
    CHECK(b >= 0.0f && b <= 1.0f);
    CHECK(c >= 0.0f && c <= 1.0f);
    CHECK(fabsf(a + b + c - 1.5f) < 0.00002f);
    last_a = a; last_b = b; last_c = c; ++duty_updates;
}
float foc_port_pwm_frequency_hz(void) { return 20000.0f; }
unsigned foc_port_enter_critical(void) { return 1u; }
void foc_port_exit_critical(unsigned previous)
{
    CHECK(previous == 1u);
    if (inject_fault_after_unlock) {
        inject_fault_after_unlock = false;
        fault_input = true;
        foc_fault_isr();
    }
}

static foc_status_t status(void)
{
    foc_status_t s;
    foc_get_status(&s);
    CHECK(s.angle_rad >= 0.0f && s.angle_rad < 6.283186f);
    CHECK(s.enabled == driver_en);
    return s;
}

static void tick(void) { ++tick_count; foc_tick_isr(); }
static void ticks(unsigned n) { while (n--) tick(); }
static unsigned duration_ticks(unsigned ms)
{
    return (ms * 1000u + FOC_CONTROL_PERIOD_US - 1u) / FOC_CONTROL_PERIOD_US;
}
static void until_state(foc_state_t desired, unsigned limit)
{
    while (status().state != desired && limit--) tick();
    CHECK(status().state == desired);
}

static void reset_fixture(void)
{
    event_count = tick_count = pwm_on_tick = en_on_tick = duty_updates = 0;
    driver_en = true; pwm_on = false; fault_input = false;
    port_init_ok = pwm_start_ok = true; refuse_enable = inject_fault_after_unlock = false;
    CHECK(foc_init());
    CHECK(!driver_en && !pwm_on);
    CHECK(status().state == FOC_STOPPED);
    event_count = 0;
}

static void stop_order(void)
{
    unsigned i;
    event_count = 0;
    foc_stop();
    CHECK(event_count >= 2);
    CHECK(events[0] == EV_EN_LOW);
    for (i = 1; i < event_count && events[i] != EV_PWM_OFF; ++i) CHECK(events[i] != EV_EN_HIGH);
    CHECK(i < event_count);
    CHECK(!driver_en && !pwm_on);
}

static void test_start_and_ramp(void)
{
    foc_status_t s, before;
    unsigned enabled_tick;
    reset_fixture();
    CHECK(foc_start());
    CHECK(!driver_en && !pwm_on);
    CHECK(status().state == FOC_RESET_WAIT);
    CHECK(!foc_start());
    ticks(duration_ticks(FOC_RESET_MS) - 1u);
    CHECK(status().state == FOC_RESET_WAIT && !driver_en && !pwm_on);
    tick(); CHECK(status().state == FOC_WAKE_WAIT && !driver_en && !pwm_on);
    ticks(duration_ticks(FOC_WAKE_MS) - 1u);
    CHECK(status().state == FOC_WAKE_WAIT && !driver_en && !pwm_on);
    tick(); CHECK(status().state == FOC_PWM_WAIT && !driver_en && pwm_on);
    ticks(duration_ticks(FOC_PWM_SETTLE_MS) - 1u);
    CHECK(status().state == FOC_PWM_WAIT && !driver_en);
    tick(); CHECK(status().state == FOC_ALIGN);
    CHECK(driver_en && pwm_on);
    CHECK(en_on_tick > pwm_on_tick);
    enabled_tick = tick_count;
    s = status();
    CHECK(s.frequency_hz == 0.0f && s.amplitude > 0.0f && s.amplitude < 0.1f);
    CHECK(fabsf(last_a - 0.5f) < 0.00001f);
    CHECK(last_b < last_a && last_c > last_a);
    ticks(duration_ticks(FOC_ALIGN_MS) - 1u);
    CHECK(status().state == FOC_ALIGN && status().frequency_hz == 0.0f);
    tick(); CHECK(status().state == FOC_RAMP);
    CHECK(tick_count == enabled_tick + duration_ticks(FOC_ALIGN_MS));
    before = status();
    tick(); s = status();
    CHECK(s.frequency_hz >= before.frequency_hz);
    CHECK(s.frequency_hz - before.frequency_hz <= FOC_FREQ_RAMP_HZ_S * FOC_CONTROL_DT_S + 0.000001f);
    CHECK(fabsf(s.amplitude - before.amplitude) <= FOC_AMP_RAMP_PER_S * FOC_CONTROL_DT_S + 0.0000001f);
    until_state(FOC_RUNNING, 100000);
    s = status();
    CHECK(fabsf(s.frequency_hz - s.target_frequency_hz) < 0.00001f);
    CHECK(fabsf(s.amplitude - s.target_amplitude) < 0.00001f);
    CHECK(duty_updates > 10000);
    ticks(20000);
    CHECK(status().state == FOC_RUNNING);
    stop_order();
    CHECK(status().state == FOC_STOPPED);
    ticks(20000); CHECK(!driver_en && !pwm_on);
    puts("PASS startup delays, low alignment, bounded slew, sine duties and angle wrap");
}

static void test_stop_during_start(void)
{
    reset_fixture(); CHECK(foc_start());
    stop_order(); ticks(10000);
    CHECK(status().state == FOC_STOPPED && !driver_en && !pwm_on);
    reset_fixture(); CHECK(foc_start()); until_state(FOC_WAKE_WAIT, 10000);
    stop_order(); ticks(10000);
    CHECK(status().state == FOC_STOPPED && !driver_en && !pwm_on);
    reset_fixture(); CHECK(foc_start()); until_state(FOC_PWM_WAIT, 10000);
    stop_order(); ticks(10000);
    CHECK(status().state == FOC_STOPPED && !driver_en && !pwm_on);
    puts("PASS STOP lowers EN before stopping PWM, including all startup wait states");
}

static void test_fault_latch_and_clear(void)
{
    unsigned count;
    reset_fixture(); CHECK(foc_start()); until_state(FOC_ALIGN, 10000);
    fault_input = true; event_count = 0;
    foc_fault_isr();
    CHECK(events[0] == EV_EN_LOW);
    CHECK(!driver_en && !pwm_on);
    CHECK(status().state == FOC_FAULT && status().fault_latched);
    count = status().fault_count; CHECK(count > 0);
    fault_input = false; ticks(50000);
    CHECK(status().state == FOC_FAULT && !driver_en);
    CHECK(!foc_start());
    stop_order(); CHECK(status().state == FOC_FAULT);
    CHECK(foc_clear_fault());
    CHECK(status().state == FOC_CLEAR_RESET);
    CHECK(!driver_en && !pwm_on);
    tick(); CHECK(status().fault_latched);
    until_state(FOC_CLEAR_WAKE, 10000);
    CHECK(status().fault_latched);
    until_state(FOC_STOPPED, 10000);
    CHECK(!status().fault_latched && !driver_en && !pwm_on);
    CHECK(status().fault_count == count);
    ticks(10000); CHECK(!driver_en);
    CHECK(foc_start()); until_state(FOC_ALIGN, 10000); CHECK(driver_en);
    fault_input = true; tick();
    CHECK(status().state == FOC_FAULT && !driver_en);
    CHECK(foc_clear_fault());
    until_state(FOC_FAULT, 10000);
    CHECK(status().fault_latched && !driver_en);
    puts("PASS ISR and sampled fault shutdown, latch, explicit reset/wake clear and no restart");
}

static void test_rejected_hardware_start(void)
{
    reset_fixture(); fault_input = true;
    CHECK(foc_start()); until_state(FOC_FAULT, 10000); CHECK(status().fault_latched && !driver_en);
    reset_fixture(); pwm_start_ok = false; CHECK(foc_start());
    until_state(FOC_FAULT, 10000); CHECK(!driver_en && !pwm_on);
    reset_fixture(); refuse_enable = true; CHECK(foc_start());
    until_state(FOC_FAULT, 10000); CHECK(!driver_en && !pwm_on);
    puts("PASS active nFAULT and failed PWM/EN startup stay disabled");
}

static void test_fault_preempts_math(void)
{
    unsigned before;
    reset_fixture(); CHECK(foc_start()); until_state(FOC_ALIGN, 10000);
    before = duty_updates;
    inject_fault_after_unlock = true;
    tick();
    CHECK(!driver_en && !pwm_on);
    CHECK(status().state == FOC_FAULT && status().fault_latched);
    CHECK(duty_updates == before);
    fault_input = false; ticks(10000);
    CHECK(!driver_en && status().state == FOC_FAULT);
    puts("PASS fault preempting unlocked PWM math cannot restore output or write duties");
}

static void test_input_validation(void)
{
    foc_status_t original;
    const float invalid[] = { NAN, INFINITY, -INFINITY, 1e20f, -1e20f };
    unsigned i;
    reset_fixture(); original = status();
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        CHECK(!foc_set_frequency(invalid[i])); CHECK(!foc_set_amplitude(invalid[i]));
        CHECK(status().target_frequency_hz == original.target_frequency_hz);
        CHECK(status().target_amplitude == original.target_amplitude);
    }
    CHECK(!foc_set_amplitude(-0.001f));
    CHECK(!foc_set_frequency(0.0f)); CHECK(!foc_set_frequency(-5.0f));
    CHECK(!foc_set_frequency(FOC_MIN_FREQ_HZ * 0.5f));
    CHECK(foc_set_frequency(FOC_MIN_FREQ_HZ));
    CHECK(foc_set_amplitude(0.0f));
    CHECK(!foc_start());
    CHECK(foc_set_frequency(5.0f)); CHECK(foc_set_amplitude(0.05f));
    CHECK(foc_start()); until_state(FOC_RUNNING, 100000);
    CHECK(status().frequency_hz > 0.0f);
    ticks(30000); CHECK(status().angle_rad >= 0.0f);
    CHECK(foc_set_frequency(FOC_MAX_FREQ_HZ));
    CHECK(foc_set_amplitude(FOC_MAX_AMP));
    ticks(100000); CHECK(status().frequency_hz > 5.0f && status().frequency_hz < FOC_MAX_FREQ_HZ);
    CHECK(status().amplitude <= FOC_MAX_AMP);
    CHECK(!foc_set_frequency(FOC_MAX_FREQ_HZ + 1.0f));
    CHECK(!foc_set_amplitude(FOC_MAX_AMP + 0.01f));
    puts("PASS NaN/infinity/range validation, zero amplitude startup rejection and hard limits");
}

static void command(const char *text, bool ok)
{
    char reply[512];
    foc_console_process_line(text, reply, sizeof(reply));
    CHECK(strncmp(reply, ok ? "OK" : "ERR", ok ? 2 : 3) == 0);
}

static void test_commands(void)
{
    const char *bad[] = { "FREQ", "AMP", "FREQ nan", "FREQ inf", "AMP -inf", "AMP -1",
        "AMP 1", "FREQ 1e99", "FREQ 5 extra", "START extra", "CLEAR_FAULT extra", "UNKNOWN" };
    unsigned i;
    char oversized[512], tiny[5], reply[512];
    reset_fixture();
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) command(bad[i], false);
    command("FREQ 5", true); command("AMP 0.05", true);
    foc_console_process_line("STATUS", reply, sizeof(reply));
    CHECK(strstr(reply, "FREQ_E_HZ") != NULL);
    CHECK(strstr(reply, "NFAULT=1") != NULL);
    fault_input = true;
    foc_console_process_line("STATUS", reply, sizeof(reply));
    CHECK(strstr(reply, "NFAULT=0") != NULL);
    CHECK(strstr(reply, "LATCH=0") != NULL); /* Raw pin is distinct from the latch. */
    fault_input = false;
    command("START", true); command("STOP", true);
    CHECK(!driver_en && !pwm_on);
    memset(oversized, 'x', sizeof(oversized)); oversized[sizeof(oversized) - 1] = '\0';
    command(oversized, false);
    memset(tiny, '#', sizeof(tiny));
    foc_console_process_line("STATUS", tiny, 4);
    CHECK(tiny[3] == '\0' && tiny[4] == '#');
    command("START", true); until_state(FOC_ALIGN, 10000);
    fault_input = true; foc_fault_isr(); fault_input = false;
    command("START", false); command("CLEAR_FAULT", true);
    until_state(FOC_STOPPED, 10000); CHECK(!driver_en);
    puts("PASS serial command parsing, malformed numeric/trailing input and reply bounds");
}

static void receive(const char *text)
{
    while (*text) foc_console_rx_byte_isr((unsigned char)*text++);
}
static void response(bool ok)
{
    char reply[512];
    CHECK(foc_console_read_response(reply, sizeof(reply)));
    CHECK(strncmp(reply, ok ? "OK" : "ERR", ok ? 2 : 3) == 0);
}
static void test_rx_stream(void)
{
    char reply[512];
    unsigned i;
    reset_fixture(); foc_console_reset_input();
    receive("STA");
    CHECK(!foc_console_read_response(reply, sizeof(reply)));
    CHECK(status().state == FOC_STOPPED);
    receive("RT\r\n"); response(true);
    CHECK(status().state == FOC_RESET_WAIT && !driver_en);
    CHECK(!foc_console_read_response(reply, sizeof(reply)));
    foc_stop();
    receive("STARX\bT\n"); response(true);
    CHECK(status().state == FOC_RESET_WAIT); foc_stop();
    receive("FREQ 8\r\nAMP 0.04\r\n"); response(true); response(true);
    CHECK(status().target_frequency_hz == 8.0f && status().target_amplitude == 0.04f);
    CHECK(!foc_console_read_response(reply, sizeof(reply)));
    receive("STA"); foc_console_rx_byte_isr(0); receive("RT\n"); response(false);
    CHECK(status().state == FOC_STOPPED);
    for (i = 0; i < FOC_CONSOLE_LINE_MAX + 5u; ++i) foc_console_rx_byte_isr('x');
    receive("\nSTART\n"); response(false);
    CHECK(status().state == FOC_STOPPED); response(true);
    CHECK(status().state == FOC_RESET_WAIT); foc_stop();

    /* Even a complete START at the front of a corrupt ring must be discarded. */
    receive("START\n");
    for (i = 0; i < 300u; ++i) foc_console_rx_byte_isr('x');
    CHECK(!foc_console_read_response(reply, sizeof(reply)));
    CHECK(status().state == FOC_STOPPED);
    receive("START\n"); response(false);
    CHECK(status().state == FOC_STOPPED);
    receive("START\n"); response(true);
    CHECK(status().state == FOC_RESET_WAIT); foc_stop();

    receive("STA"); CHECK(!foc_console_read_response(reply, sizeof(reply)));
    foc_console_rx_error_isr(); receive("RT\n");
    CHECK(!foc_console_read_response(reply, sizeof(reply)));
    CHECK(status().state == FOC_STOPPED);
    receive("\n"); response(false);
    receive("START\n"); response(true);
    CHECK(status().state == FOC_RESET_WAIT);
    puts("PASS fragmented RX, CRLF/backspace, NUL, line/ring overflow and UART error recovery");
}

int main(void)
{
    test_start_and_ramp();
    test_stop_during_start();
    test_fault_latch_and_clear();
    test_rejected_hardware_start();
    test_fault_preempts_math();
    test_input_validation();
    test_commands();
    test_rx_stream();
    printf("ALL HOST C TESTS PASSED (%u checks). Hardware timing/motor motion unverified.\n", checks);
    return 0;
}
