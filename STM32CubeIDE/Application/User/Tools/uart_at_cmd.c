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

	answer_lines_cnt = 0;
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

		answer_lines_cnt = lines_in_answer;
		while(HAL_UART_Transmit(&huart1, (uint8_t*)cmd_buf, cmd_len, timeout) == HAL_BUSY);
		if(answer != NULL){
			res = (xQueueReceive(answer_queue, answer, timeout) == pdPASS);
		}
		answer_lines_cnt = 0;

		xSemaphoreGive(atCmdMutex);
	}
	return res;
}

void UART_AT_stop()
{
	HAL_UART_AbortReceive(&huart1);
}

void UART_AT_start()
{
	HAL_UART_AbortReceive(&huart1);
	memset(buf,0,sizeof(buf));
	while(HAL_UARTEx_ReceiveToIdle_DMA(&huart1, buf, 250) != HAL_OK);
}

uint8_t get_status_update(MsgType* msg)
{
	return (xQueueReceive(status_queue, msg, portMAX_DELAY) == pdPASS);
}

uint8_t get_http_result(HttpResType* res, BaseType_t timeout)
{
	return (xQueueReceive(http_queue, res, timeout) == pdPASS);
}

void push_message(char* start, char* end)
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

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
	static char i=0;
	if(huart == &huart1) {
		char* _ptr = (char*)buf;
		if(Size > 2){
			_ptr[Size] = 0;
			if(memcmp("\r\n",_ptr,2) == 0){
				_ptr+=2;
			}
			char* crlf_ptr = strstr(_ptr,"\r\n");
			if(crlf_ptr != NULL && _ptr != crlf_ptr){
				push_message(_ptr, crlf_ptr);
			}
		}
		HAL_UARTEx_ReceiveToIdle_DMA(&huart1, buf, 250);
	}
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	if(huart == &huart1) {
		HAL_UARTEx_ReceiveToIdle_DMA(&huart1, buf, 250);
	}
}
