/*
 * Tracker.h
 *
 *  Created on: Aug 22, 2021
 *      Author: gl
 */

#ifndef APPLICATION_USER_TASKS_TRACKER_H_
#define APPLICATION_USER_TASKS_TRACKER_H_

#include "uart_at_cmd.h"

void TrackerInit();
void TrackerMainTask(void *argument);
void TrackerStatusTask();
void PeriodicCheckTask();

void power_on();
void power_off();

uint8_t at_check();
uint8_t gps_powerup();
uint8_t gps_reset();
uint8_t gps_get_status(MsgType* msg);
uint8_t gprs_prepare();
uint8_t gsm_get_csq(MsgType* msg, char *csq);
uint8_t gsm_get_imei(MsgType* msg, char *imei);
uint8_t gsm_get_opname(MsgType* msg, char *name);
uint8_t gsm_get_gprs_status(MsgType* msg);
uint8_t gprs_get_status(MsgType* msg);
uint8_t http_get(char* url);
uint8_t gps_get_rmc(char* rmc);
uint8_t gps_get_gga(char* gga);

#endif /* APPLICATION_USER_TASKS_TRACKER_H_ */
