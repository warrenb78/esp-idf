#ifndef __UART_COMMANDER_H__
#define __UART_COMMANDER_H__

void create_uart_task(void);

int send_uart_bytes(const uint8_t *buf, size_t size);

#endif /* __UART_COMMANDER_H__ */
