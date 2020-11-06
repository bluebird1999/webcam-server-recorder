/*
 * recorder_interface.h
 *
 *  Created on: Oct 6, 2020
 *      Author: ning
 */

#ifndef SERVER_RECORDER_RECORDER_INTERFACE_H_
#define SERVER_RECORDER_RECORDER_INTERFACE_H_

/*
 * header
 */
#include <mp4v2/mp4v2.h>
#include <pthread.h>
#include "../../manager/global_interface.h"
#include "../../manager/manager_interface.h"
#include "config.h"

/*
 * define
 */
#define		SERVER_RECORDER_VERSION_STRING		"alpha-3.7"

#define		MSG_RECORDER_BASE						(SERVER_RECORDER<<16)
#define		MSG_RECORDER_SIGINT						(MSG_RECORDER_BASE | 0x0000)
#define		MSG_RECORDER_SIGINT_ACK					(MSG_RECORDER_BASE | 0x1000)
#define		MSG_RECORDER_START						(MSG_RECORDER_BASE | 0x0010)
#define		MSG_RECORDER_START_ACK					(MSG_RECORDER_BASE | 0x1010)
#define		MSG_RECORDER_STOP						(MSG_RECORDER_BASE | 0x0011)
#define		MSG_RECORDER_STOP_ACK					(MSG_RECORDER_BASE | 0x1011)
#define		MSG_RECORDER_PROPERTY_GET				(MSG_RECORDER_BASE | 0x0012)
#define		MSG_RECORDER_PROPERTY_GET_ACK			(MSG_RECORDER_BASE | 0x1012)
#define		MSG_RECORDER_PROPERTY_SET				(MSG_RECORDER_BASE | 0x0013)
#define		MSG_RECORDER_PROPERTY_SET_ACK			(MSG_RECORDER_BASE | 0x1013)
#define		MSG_RECORDER_PROPERTY_NOTIFY			(MSG_RECORDER_BASE | 0x0014)
#define		MSG_RECORDER_ADD						(MSG_RECORDER_BASE | 0x0015)
#define		MSG_RECORDER_ADD_ACK					(MSG_RECORDER_BASE | 0x1015)
#define		MSG_RECORDER_VIDEO_DATA					(MSG_RECORDER_BASE | 0x0100)
#define		MSG_RECORDER_AUDIO_DATA					(MSG_RECORDER_BASE | 0x0101)
#define		MSG_RECORDER_ADD_FILE					(MSG_RECORDER_BASE | 0x0102)

#define		RECORDER_AUDIO_YES						0x00
#define		RECORDER_AUDIO_NO						0x01

#define		RECORDER_QUALITY_LOW					0x00
#define		RECORDER_QUALITY_MEDIUM					0x01
#define		RECORDER_QUALITY_HIGH					0x02

#define		RECORDER_TYPE_NORMAL					0x00
#define		RECORDER_TYPE_MOTION_DETECTION			0x01
#define		RECORDER_TYPE_ALARM						0x02

#define		RECORDER_MODE_BY_TIME					0x00
#define		RECORDER_MODE_BY_SIZE					0x01

#define		MAX_RECORDER_JOB						3

#define		RECORDER_PROPERTY_SERVER_STATUS				(0x000 | PROPERTY_TYPE_GET)
#define		RECORDER_PROPERTY_CONFIG_STATUS				(0x001 | PROPERTY_TYPE_GET)
#define		RECORDER_PROPERTY_SAVE_MODE					(0x002 | PROPERTY_TYPE_GET | PROPERTY_TYPE_SET)
#define		RECORDER_PROPERTY_RECORDING_MODE			(0x003 | PROPERTY_TYPE_GET | PROPERTY_TYPE_SET)
#define		RECORDER_PROPERTY_NORMAL_DIRECTORY			(0x004 | PROPERTY_TYPE_GET)

/*
 * structure
 */
typedef enum {
	RECORDER_THREAD_NONE = 0,
	RECORDER_THREAD_INITED,
	RECORDER_THREAD_STARTED,
	RECORDER_THREAD_RUN,
	RECORDER_THREAD_PAUSE,
	RECORDER_THREAD_ERROR,
};

typedef struct recorder_init_t {
	int		type;
	int		mode;
	int		repeat;
	int		repeat_interval;
    int		audio;
    int		quality;
    int		video_channel;
    char   	start[MAX_SYSTEM_STRING_SIZE];
    char   	stop[MAX_SYSTEM_STRING_SIZE];
    HANDLER	func;
} recorder_init_t;

typedef struct recorder_run_t {
	char   				file_path[MAX_SYSTEM_STRING_SIZE*2];
	pthread_rwlock_t 	lock;
	MP4FileHandle 		mp4_file;
	MP4TrackId 			video_track;
	MP4TrackId 			audio_track;
    FILE    			*file;
	unsigned long long 	start;
	unsigned long long 	stop;
	unsigned long long 	real_start;
	unsigned long long 	real_stop;
	unsigned long long	last_write;
	unsigned long long	last_vframe_stamp;
	unsigned long long	last_aframe_stamp;
	unsigned long long 	first_frame_stamp;
	char				first_audio;
	char				sps_read;
	char				pps_read;
	char				first_frame;
    int					fps;
    int					width;
    int					height;
    char				exit;
} recorder_run_t;

typedef struct recorder_job_t {
	char				status;
	char				t_id;
	recorder_init_t		init;
	recorder_run_t		run;
	recorder_config_t 	config;
} recorder_job_t;
/*
 * function
 */
int server_recorder_start(void);
int server_recorder_message(message_t *msg);
int server_recorder_video_message(message_t *msg);
int server_recorder_audio_message(message_t *msg);

#endif /* SERVER_RECORDER_RECORDER_INTERFACE_H_ */
