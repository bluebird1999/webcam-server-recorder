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
#include "../../server/config/config_recorder_interface.h"

/*
 * define
 */
#define		SERVER_RECORDER_VERSION_STRING		"alpha-3.0"

#define		MSG_RECORDER_BASE						(SERVER_RECORDER<<16)
#define		MSG_RECORDER_SIGINT						MSG_RECORDER_BASE | 0x0000
#define		MSG_RECORDER_SIGINT_ACK					MSG_RECORDER_BASE | 0x1000
#define		MSG_RECORDER_START						MSG_RECORDER_BASE | 0x0010
#define		MSG_RECORDER_START_ACK					MSG_RECORDER_BASE | 0x1010

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
    char   	start[MAX_SYSTEM_STRING_SIZE];
    char   	stop[MAX_SYSTEM_STRING_SIZE];
    HANDLER	func;
} recorder_init_t;

typedef struct recorder_run_t {
	char   				file_path[MAX_SYSTEM_STRING_SIZE*2];
	pthread_rwlock_t 	lock;
	pthread_t 			pid;
	MP4FileHandle 		mp4_file;
	MP4TrackId 			video_track;
	MP4TrackId 			audio_track;
    FILE    			*file;
	unsigned long long 	start;
	unsigned long long 	stop;
	unsigned long long 	real_start;
	unsigned long long 	real_stop;
	unsigned long long	last_write;
	char				i_frame_read;
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
