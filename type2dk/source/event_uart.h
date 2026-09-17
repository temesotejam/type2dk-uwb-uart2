#ifndef EVENT_UART_H
#define EVENT_UART_H
#include "event_protocol.h"
bool event_uart_start(uint32_t boot);
bool event_uart_submit(event_t *event);
void event_uart_log(void);
#endif
