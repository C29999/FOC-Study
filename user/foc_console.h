/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FOC_CONSOLE_H
#define FOC_CONSOLE_H
#include <stddef.h>
#include <stdbool.h>
#define FOC_CONSOLE_LINE_MAX 80u
void foc_console_init(void);
void foc_console_poll(void);
void foc_console_process_line(const char *line, char *reply, size_t capacity);
/* Nonblocking RX producer, called by the UART ISR. Errors discard a full line. */
void foc_console_rx_byte_isr(unsigned char byte);
void foc_console_rx_error_isr(void);
void foc_console_reset_input(void);
bool foc_console_read_response(char *reply, size_t capacity);
#endif
