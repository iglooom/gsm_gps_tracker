/*
 * Tracker.c
 *
 *  Created on: Aug 22, 2021
 *      Author: gl
 */

#include "main.h"
#include "Tracker.h"
#include "uart_at_cmd.h"

struct flags {
	uint8_t pwr_on:1;
	uint8_t hw_pwr_on:1;
	uint8_t sim_ready:1;
	uint8_t gps_ready:1;
	uint8_t gsm_ready:1;
	uint8_t cfun:3;
	uint8_t creg:3;
	uint8_t gps_runing:1;
	uint8_t gps_status:3;
	uint8_t gsm_registered:1;
	uint8_t gprs_available:1;
	uint8_t gprs_active:1;
	char csq[5];
	char imei[20];
	char op_name[30];
	uint8_t try_cnt;
};

struct gps_data {
	char rmc[256];
	char gga[256];
	uint8_t valid:1;
};

struct gps_data gp;

struct flags f = {};
char tmp_buf[512] = {};
char url_buf[512] = {};

SemaphoreHandle_t opMutex;

void TrackerInit()
{
	UART_AT_Init();
	opMutex = xSemaphoreCreateMutex();
}

uint8_t retry(uint8_t cnt)
{
	if(++f.try_cnt >= cnt){
		return 1;
	}
	osDelay(1000);
	return 0;
}

void TrackerMainTask(void *argument)
{
	static HttpResType http;
	static MsgType msg = {};

	osDelay(1000);
	power_off();
	osDelay(10000);

	while(1)
	{
		power_on();

		/* Wait modem init */
		f.try_cnt = 0;
		while(!(f.gps_ready && f.gsm_ready)){
			if(retry(30)){
				goto restart;
			}
		}

		xSemaphoreTake(opMutex, portMAX_DELAY);

		/* Check interface and get IMEI */
		osDelay(300);
		f.try_cnt = 0;
		while(!gsm_get_imei(&msg, f.imei)){
			if(retry(5)){
				goto restart;
			}
		}

		/* Power up GPS core */
		osDelay(300);
		f.try_cnt = 0;
		while(!gps_powerup(&msg)){
			if(retry(5)){
				goto restart;
			}
		}

		/* Perform GPS cold start */
		osDelay(300);
		f.try_cnt = 0;
		while(!gps_reset(&msg)){
			if(retry(5)){
				goto restart;
			}
		}
		f.gps_runing = 1;

		/* Wait cellular registration */
		f.try_cnt = 0;
		while(f.creg == 2){
			if(retry(200)){
				goto restart;
			}
		}
		osDelay(1000);
		if(f.creg == 0){
			goto restart;
		}
		f.gsm_registered = 1;

		/* Wait GPRS available */
		while(!gprs_is_attached(&msg)){
			if(retry(100)){
				goto restart;
			}
		}

		/* Open GPRS connection */
		osDelay(1000);
		f.try_cnt = 0;
		while(!gprs_prepare(&msg)){
			if(retry(30)){
				goto restart;
			}
		}
		xSemaphoreGive(opMutex);

		/* Tracker cycle */
		f.try_cnt = 0;
		while(1)
		{
			osDelay(30000);

			if(f.gps_status > 1 && gp.valid){
				xSemaphoreTake(opMutex, portMAX_DELAY);
				sprintf(url_buf,
					"http://xtlt.ru:5159/?id=%s&operator=%s&csq=%s&gprmc=$GPRMC,%s&gpgga=$GPGGA,%s",
					f.imei,
					f.op_name,
					f.csq,
					gp.rmc,
					gp.gga
				);

				if(http_get(&msg, url_buf)){
					if(get_http_result(&http, 10000)){
						if(strcmp(http.code, "200") == 0){
							f.try_cnt = 0;
						}
					}
				}
				xSemaphoreGive(opMutex);
				if(++f.try_cnt > 10){
					goto restart;
				}
			}
		}

restart:
		power_off();
		osDelay(30000);
	}
}


void TrackerStatusTask() // Handle unsolicited reports
{
	static MsgType un_msg;
	while(1)
	{
		if(get_status_update(&un_msg)){
			printf("Len: %d, Data: %s\r\n", un_msg.len, un_msg.data);

			if(strcmp(un_msg.data, "Call Ready") == 0){
				f.gsm_ready = 1;
			}else if(strcmp(un_msg.data, "GPS Ready") == 0){
				f.gps_ready = 1;
			}else if(strcmp(un_msg.data, "+CPIN: READY") == 0){
				f.sim_ready = 1;
			}else if(strcmp(un_msg.data, "+CPIN: NOT READY") == 0){
				f.sim_ready = 0;

			}else if(strcmp(un_msg.data, "+CFUN: 0") == 0){
				f.cfun = 0;
			}else if(strcmp(un_msg.data, "+CFUN: 1") == 0){
				f.cfun = 1;
			}else if(strcmp(un_msg.data, "+CFUN: 4") == 0){
				f.cfun = 4;

			}else if(strcmp(un_msg.data, "+CREG: 1") == 0){
				f.creg = 1;
			}else if(strcmp(un_msg.data, "+CREG: 2") == 0){
				f.creg = 2;
			}else if(strcmp(un_msg.data, "+CREG: 3") == 0){
				f.creg = 3;
			}else if(strcmp(un_msg.data, "+CREG: 5") == 0){
				f.creg = 5;

			}else if(strcmp(un_msg.data, "NORMAL POWER DOWN") == 0){
				f.sim_ready = 0;
				f.gps_ready = 0;
				f.gps_runing = 0;
				f.gsm_ready = 0;
				f.gprs_available = 0;
				f.gprs_active = 0;
				f.pwr_on = 0;
				f.cfun = 0;
				f.creg = 0;
			}else if(strcmp(un_msg.data, "RDY") == 0){
				f.pwr_on = 1;
			}

		}
	}
}


void PeriodicCheckTask() // Check modem health
{
	static MsgType msg = {};

	while(1)
	{
		osDelay(10000);

		xSemaphoreTake(opMutex, portMAX_DELAY);
		if(f.gps_runing){
			HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
			osDelay(200);
			f.gps_status = gps_get_status(&msg);
		}
		if(f.gps_status > 1){
			gp.valid = (gps_get_rmc(&msg, gp.rmc) && gps_get_gga(&msg, gp.gga));
		}

		if(f.gsm_registered){
			HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
			osDelay(200);
			gsm_get_csq(&msg, f.csq);

			osDelay(200);
			gsm_get_opname(&msg, f.op_name);

			osDelay(200);
			f.gprs_available = gprs_is_attached(&msg);

			if(f.gprs_available){
				osDelay(200);
				f.gprs_active = gprs_get_status(&msg);
				if(!f.gprs_active){
					osDelay(200);
					gprs_prepare(&msg);
				}
			}
		}
		xSemaphoreGive(opMutex);

		HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
	}
}

uint8_t IS_OK(MsgType* msg)
{
	return (strcmp(msg->data, "OK") == 0);
}

uint8_t at_check(MsgType* msg)
{
	if(AT("", msg, 1, 500)){
		return IS_OK(msg);
	}
	return 0;
}

uint8_t gps_powerup(MsgType* msg)
{
	if(AT("+CGPSPWR=1", msg, 1, 500)){
		return IS_OK(msg);
	}
	return 0;
}

uint8_t gps_reset(MsgType* msg)
{
	if(AT("+CGPSRST=0", msg, 1, 500)){
		return IS_OK(msg);
	}
	return 0;
}

uint8_t gps_get_status(MsgType* msg)
{
	if(AT("+CGPSSTATUS?", msg, 1, 500)){
		if(strcmp(msg->data, "+CGPSSTATUS: Location Unknown") == 0){
			return 1;
		}else if(strcmp(msg->data, "+CGPSSTATUS: Location Not Fix") == 0){
			return 2;
		}else if(strcmp(msg->data, "+CGPSSTATUS: Location 2D Fix") == 0){
			return 3;
		}else if(strcmp(msg->data, "+CGPSSTATUS: Location 3D Fix") == 0){
			return 4;
		}
	}
	return 0;
}

uint8_t gprs_is_attached(MsgType* msg)
{
	osDelay(100);
	if(!AT("+CGATT?", msg, 1, 500)){
		return 0;
	}
	if(strcmp(msg->data, "+CGATT: 0") == 0){ // Not attached
		osDelay(100);
		AT("+CGATT=1", msg, 1, 2500); // Try to attach

		return 0;
	}
	return 1;
}

uint8_t gprs_prepare(MsgType* msg)
{
	osDelay(100);
	if(!AT("+SAPBR=2,3", msg, 1, 500)){ // Query bearer
		return 0;
	}
	if(strstr(msg->data, "+SAPBR: 3,1,") == NULL){ // Bearer not open
		if(strstr(msg->data, "+SAPBR: 3,3,") != NULL){ // Bearer closed
			osDelay(100);
			AT("+SAPBR=1,3", msg, 1, 500); // Open bearer
		}
		return 0;
	}

	osDelay(300);
	if(!AT("+HTTPINIT", msg, 1, 500)){
		return 0;
	}

	osDelay(300);
	if(!AT("+HTTPPARA=\"CID\",3", msg, 1, 500)){
		return 0;
	}

	return 1;
}

uint8_t gsm_get_csq(MsgType* msg, char *csq)
{
	if(!AT("+CSQ", msg, 1, 500)){
		return 0;
	}

	if(strstr(msg->data, "+CSQ: ") == NULL || msg->len > 12){
		return 0;
	}

	msg->data[6+5] = 0;
	strcpy(csq,&msg->data[6]);
	return 1;
}

uint8_t gsm_get_imei(MsgType* msg, char *imei)
{
	if(!AT("+GSN", msg, 1, 500)){
		return 0;
	}

	if(msg->len == 15){
		msg->data[15] = 0;
		strcpy(imei,msg->data);
		return 1;
	}
	return 0;
}

uint8_t gsm_get_opname(MsgType* msg, char *name)
{
	if(!AT("+COPS?", msg, 1, 500)){
		return 0;
	}

	if(strstr(msg->data, "+COPS: 0,0,") == NULL || msg->len < 12){
		return 0;
	}

	msg->data[12+29] = 0;
	strcpy(name,&msg->data[12]);
	char* c = strstr(name, "\"");
	if(c != NULL){
		*c = 0; // trim quotes
	}

	c = strstr(name, " ");
	if(c != NULL){
		*c = '_'; // trim spaces
	}
	return 1;
}

uint8_t gsm_get_gprs_status(MsgType* msg)
{
	if(!AT("+CGATT?", msg, 1, 500)){
		return 0;
	}

	if(strcmp(msg->data, "+CGATT: 1") == 0){
		return 1;
	}
	return 0;
}

uint8_t gprs_get_status(MsgType* msg)
{
	if(!AT("+SAPBR=2,3", msg, 1, 500)){ // Query bearer
		return 0;
	}
	if(strstr(msg->data, "+SAPBR: 3,1,") != NULL){ // Bearer open
		return 1;
	}

	return 0;
}

uint8_t gps_get_rmc(MsgType* msg, char* rmc)
{
	if(!AT("+CGPSINF=32", msg, 1, 500)){
		return 0;
	}

	if(strstr(msg->data, "32,") != NULL){
		strcpy(rmc, &msg->data[3]);
		return 1;
	}
	return 0;
}

uint8_t gps_get_gga(MsgType* msg, char* gga)
{
	if(!AT("+CGPSINF=2", msg, 1, 500)){
		return 0;
	}

	if(strstr(msg->data, "2,") != NULL){
		strcpy(gga, &msg->data[2]);
		return 1;
	}
	return 0;
}

uint8_t http_get(MsgType* msg, char* url)
{
	sprintf(tmp_buf, "+HTTPPARA=\"URL\",\"%s\"", url);
	if(!AT(tmp_buf, msg, 1, 500)){
		return 0;
	}

	if(!AT("+HTTPACTION=0", msg, 1, 500)){
		return 0;
	}
	return 1;
}

void power_on()
{
	printf("power_on\r\n");
	UART_AT_start();
	HAL_GPIO_WritePin(MODEM_PWR_GPIO_Port, MODEM_PWR_Pin, GPIO_PIN_RESET);
	f.hw_pwr_on = 1;
	osDelay(500);
	HAL_GPIO_WritePin(MODEM_PWR_GPIO_Port, MODEM_PWR_Pin, GPIO_PIN_SET);
}

void power_off()
{
	printf("power_off\r\n");
	UART_AT_stop();
	HAL_GPIO_WritePin(MODEM_PWR_GPIO_Port, MODEM_PWR_Pin, GPIO_PIN_RESET);
	osDelay(500);
	HAL_GPIO_WritePin(MODEM_PWR_GPIO_Port, MODEM_PWR_Pin, GPIO_PIN_SET);
	osDelay(5000);
	AT("+CPOWD=1", NULL, 1, 500);
	AT("+CPOWD=1", NULL, 1, 500);
	memset(&f,0,sizeof(f));
}

