/*
 * recorder.h
 *
 *  Created on: Oct 6, 2020
 *      Author: ning
 */

#ifndef SERVER_RECORDER_RECORDER_H_
#define SERVER_RECORDER_RECORDER_H_

/*
 * header
 */

/*
 * define
 */
#define		MAX_BETWEEN_RECODER_PAUSE		5		//5s
#define		TIMEOUT							3		//3s
#define		MIN_SD_SIZE_IN_MB				64		//64M

#define		ERR_NONE						0
#define		ERR_NO_DATA						-1
#define		ERR_TIME_OUT					-2
#define		ERR_LOCK						-3
#define		ERR_ERROR						-4

#define		RECORDER_INIT_CONDITION_NUM				3
#define		RECORDER_INIT_CONDITION_CONFIG			0
#define		RECORDER_INIT_CONDITION_MIIO_TIME		1
#define		RECORDER_INIT_CONDITION_DEVICE_CONFIG	2

#define		RECORDER_EXIT_CONDITION					0

/*
 * structure
 */

/*
 * function
 */

#endif /* SERVER_RECORDER_RECORDER_H_ */
