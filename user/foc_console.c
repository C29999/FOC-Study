/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "foc_console.h"
#include "foc.h"
#include "foc_config.h"
#include "foc_port.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef FOC_HOST_TEST
#include "zf_common_headfile.h"
#endif
void foc_console_process_line(const char *line, char *reply, size_t capacity)
{
    char command[FOC_CONSOLE_LINE_MAX];
    const char *p;
    char *end;
    size_t n = 0u;
    float value;
    bool ok;
    foc_status_t status;
    if (reply == 0 || capacity == 0u) return;
    reply[0] = '\0';
    if (line == 0 || strlen(line) >= FOC_CONSOLE_LINE_MAX) {
        snprintf(reply, capacity, "ERR line too long\r\n"); return;
    }
    p = line;
    while (isspace((unsigned char)*p)) ++p;
    while (*p && !isspace((unsigned char)*p)) command[n++] = (char)toupper((unsigned char)*p++);
    command[n] = '\0';
    while (isspace((unsigned char)*p)) ++p;
    if (n == 0u) return;
    if (strcmp(command, "FREQ") == 0 || strcmp(command, "AMP") == 0) {
        value = strtof(p, &end);
        if (end == p) { snprintf(reply, capacity, "ERR numeric argument required\r\n"); return; }
        while (isspace((unsigned char)*end)) ++end;
        if (*end) { snprintf(reply, capacity, "ERR trailing argument\r\n"); return; }
        ok = (strcmp(command, "FREQ") == 0) ? foc_set_frequency(value) : foc_set_amplitude(value);
        if (ok) snprintf(reply, capacity, "OK target updated; ramp applied\r\n");
        else snprintf(reply, capacity, "ERR range: FREQ %.3g..%.3g electrical Hz, AMP 0..%.3g\r\n",
                      (double)FOC_MIN_FREQ_HZ, (double)FOC_MAX_FREQ_HZ, (double)FOC_MAX_AMP);
        return;
    }
    if (*p) { snprintf(reply, capacity, "ERR unexpected argument\r\n"); return; }
    if (strcmp(command, "START") == 0) {
        snprintf(reply, capacity, foc_start() ? "OK START accepted; STATUS reports progress\r\n" :
                 "ERR START needs STOPPED, AMP>0, no latched fault\r\n");
    } else if (strcmp(command, "STOP") == 0) {
        foc_stop(); snprintf(reply, capacity, "OK EN=0; PWM stopped\r\n");
    } else if (strcmp(command, "CLEAR_FAULT") == 0) {
        snprintf(reply, capacity, foc_clear_fault() ?
                 "OK clearing; wait STATUS=STOPPED then explicit START\r\n" :
                 "ERR CLEAR_FAULT needs FAULT state\r\n");
    } else if (strcmp(command, "STATUS") == 0) {
        foc_get_status(&status);
        snprintf(reply, capacity,
                 "STATE=%s EN=%u LATCH=%u NFAULT=%u FAULTS=%lu PWM_HZ=%.1f "
                 "FREQ_E_HZ=%.3f TARGET_E_HZ=%.3f AMP=%.4f TARGET_AMP=%.4f "
                 "DUTY=%.4f,%.4f,%.4f\r\n",
                 foc_state_name(status.state), (unsigned)status.enabled,
                 (unsigned)status.fault_latched, (unsigned)!foc_port_fault_active(),
                 (unsigned long)status.fault_count,
                 (double)status.pwm_frequency_hz, (double)status.frequency_hz,
                 (double)status.target_frequency_hz, (double)status.amplitude,
                 (double)status.target_amplitude, (double)status.duty_a,
                 (double)status.duty_b, (double)status.duty_c);
    } else if (strcmp(command, "HELP") == 0) {
        snprintf(reply, capacity, "START STOP FREQ <electrical Hz> AMP <modulation> STATUS CLEAR_FAULT HELP\r\n");
    } else snprintf(reply, capacity, "ERR unknown command; HELP\r\n");
}
static char line_buffer[FOC_CONSOLE_LINE_MAX];
static size_t line_length;
static bool dropping;
static unsigned char rx_ring[256];
static volatile unsigned int rx_head, rx_tail;
static volatile bool rx_overflow;

void foc_console_rx_byte_isr(unsigned char byte)
{
    unsigned int next = (rx_head + 1u) & 255u;
    if (next == rx_tail) rx_overflow = true;
    else { rx_ring[rx_head] = byte; rx_head = next; }
}
void foc_console_rx_error_isr(void) { rx_overflow = true; }
void foc_console_reset_input(void)
{
    unsigned int key = foc_port_enter_critical();
    rx_head = rx_tail = 0u;
    rx_overflow = false;
    line_length = 0u;
    dropping = false;
    foc_port_exit_critical(key);
}
bool foc_console_read_response(char *reply, size_t capacity)
{
    unsigned int key;
    char ch;
    if (reply == 0 || capacity == 0u) return false;
    reply[0] = '\0';
    for (;;) {
        key = foc_port_enter_critical();
        if (rx_overflow) {
            rx_tail = rx_head; /* No truncated/corrupt input may execute START. */
            rx_overflow = false;
            line_length = 0u;
            dropping = true;
        }
        if (rx_tail == rx_head) { foc_port_exit_critical(key); return false; }
        ch = (char)rx_ring[rx_tail];
        rx_tail = (rx_tail + 1u) & 255u;
        foc_port_exit_critical(key);
        if (ch == '\r' || ch == '\n') {
            if (dropping) snprintf(reply, capacity, "ERR input overflow/error; line discarded\r\n");
            else if (line_length != 0u) {
                line_buffer[line_length] = '\0';
                foc_console_process_line(line_buffer, reply, capacity);
            }
            line_length = 0u;
            dropping = false;
            if (reply[0] != '\0') return true;
        } else if (ch == '\b' || ch == 127) {
            if (!dropping && line_length != 0u) --line_length;
        } else if ((ch >= 32 && ch <= 126) || ch == '\t') {
            if (!dropping) {
                if (line_length + 1u < sizeof(line_buffer)) line_buffer[line_length++] = ch;
                else dropping = true;
            }
        } else dropping = true;
    }
}
#ifndef FOC_HOST_TEST
static char response[320];
void foc_console_init(void)
{
    foc_console_reset_input();
    /* debug_init configures UART3 P15.6/P15.7 on mainboard P9, 115200 8N1.
     * isr.c uses our RX ring so lost bytes cause whole-line rejection.
     */
    uart_write_string(DEBUG_UART_INDEX, "TC264 DRV8313 OPEN LOOP; EN=0; no auto-start. HELP\r\n");
}
void foc_console_poll(void)
{
    if (foc_console_read_response(response, sizeof(response)))
        uart_write_string(DEBUG_UART_INDEX, response);
}
#endif
