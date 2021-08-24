/*
 * uart_at_cmd.c
 *
 *  Created on: Aug 22, 2021
 *      Author: gl
 */

#include "uart_at_cmd.h"

extern UART_HandleTypeDef huart1;

QueueHandle_t answer_queue;
QueueHandle_t status_queue;
QueueHandle_t http_queue;
SemaphoreHandle_t atCmdMutex;

uint8_t buf[256];
volatile uint8_t answer_lines_cnt;


void UART_AT_Init()
{
	answer_queue = xQueueCreate(2, sizeof(MsgType));
	status_queue = xQueueCreate(6, sizeof(MsgType));
	http_queue = xQueueCreate(2, sizeof(HttpResType));
	atCmdMutex = xSemaphoreCreateMutex();

	HAL_UART_Receive(&huart1, buf, 256, 1);

	memset(buf,0,sizeof(buf));
	answer_lines_cnt = 0;

	HAL_UART_Receive_IT(&huart1, buf, 1);
}

uint8_t AT(char* cmd, MsgType* answer, uint8_t lines_in_answer, TickType_t timeout)
{
	static char cmd_buf[256];
	uint16_t cmd_len = sprintf(cmd_buf,"AT%s\r\n",cmd);
	uint8_t res = 0;

	if(xSemaphoreTake(atCmdMutex, timeout) == pdTRUE)
	{
		osDelay(100); // Just in case
		xQueueReset(answer_queue);


		//if(HAL_UART_GetState(&huart1) == (HAL_UART_STATE_READY || HAL_UART_STATE_BUSY_RX)){
		answer_lines_cnt = lines_in_answer;
		while(HAL_UART_Transmit(&huart1, (uint8_t*)cmd_buf, cmd_len, timeout) == HAL_BUSY);
		res = (xQueueReceive(answer_queue, answer, timeout) == pdPASS);
		answer_lines_cnt = 0;
		//}

		xSemaphoreGive(atCmdMutex);
	}
	return res;
}

uint8_t get_status_update(MsgType* msg)
{
	return (xQueueReceive(status_queue, msg, portMAX_DELAY) == pdPASS);
}

uint8_t get_http_result(HttpResType* res, BaseType_t timeout)
{
	return (xQueueReceive(http_queue, res, timeout) == pdPASS);
}

void push_message(uint8_t* start, uint8_t* end)
{
	static MsgType msg;

	msg.len = (end-start);

	memset(msg.data, 0, sizeof(msg.data));
	memcpy(msg.data, start, msg.len);

	if(answer_lines_cnt){
		xQueueSendToBackFromISR(answer_queue, &msg, NULL);
	}else{
		if(msg.len == 2 && (strcmp(msg.data, "OK") == 0)){ // Skip unknown garbage
			return;
		}
		if(strstr(msg.data,"+HTTPACTION:") != NULL){ // HTTP action result
			HttpResType res;
			memcpy(res.code, &msg.data[14],3);
			strcpy(res.len, &msg.data[18]);
			xQueueSendToBackFromISR(http_queue, &res, NULL);
			return;
		}
		xQueueSendToBackFromISR(status_queue, &msg, NULL);
	}
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	static uint8_t* wr_ptr = buf;
	static uint8_t* msg_start_ptr = buf;

	if(huart == &huart1) {
		if(wr_ptr-buf >= 2)	{
			uint8_t crlf_condition = (memcmp("\r\n",--wr_ptr,2) == 0);
			wr_ptr++;

			if(crlf_condition){
				if(wr_ptr - msg_start_ptr > 2 && msg_start_ptr != buf){
					if(answer_lines_cnt <= 1){
						push_message(msg_start_ptr, wr_ptr-1);
						msg_start_ptr = buf;
						wr_ptr = buf;
						answer_lines_cnt = 0;
					}else{
						answer_lines_cnt--;
					}
				}else{
					msg_start_ptr = wr_ptr+1;
				}
			}
		}

		if((++wr_ptr) - buf >= sizeof(buf))	{
			push_message(msg_start_ptr, wr_ptr);
			wr_ptr = buf;
			msg_start_ptr = buf;
		}
		HAL_UART_Receive_IT(&huart1, wr_ptr, 1);
	}
}
