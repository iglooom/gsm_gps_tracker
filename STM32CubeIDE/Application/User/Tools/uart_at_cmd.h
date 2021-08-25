/*
 * uart_at_cmd.h
 *
 *  Created on: Aug 22, 2021
 *      Author: gl
 */

#ifndef APPLICATION_USER_TOOLS_UART_AT_CMD_H_
#define APPLICATION_USER_TOOLS_UART_AT_CMD_H_

#include "main.h"
#include "cmsis_os.h"
#include "queue.h"
#include "semphr.h"
#include <stdio.h>
#include <stm32f1xx_hal_gpio.h>
#include <stm32f1xx_hal_uart.h>
#include <string.h>
#include <sys/_stdint.h>

typedef struct {
	uint8_t len;
	char data[256];
} MsgType;

typedef struct {
	char len[7];
	char code[3];
} HttpResType;

void UART_AT_Init();
void UART_AT_stop();
void UART_AT_start();
uint8_t AT(char* cmd, MsgType* answer, uint8_t lines_in_answer, TickType_t timeout);
uint8_t get_status_update(MsgType* msg);
uint8_t get_http_result(HttpResType* res, BaseType_t timeout);

#endif /* APPLICATION_USER_TOOLS_UART_AT_CMD_H_ */
