/*
 * config_recorder.h
 *
 *  Created on: Aug 16, 2020
 *      Author: ning
 */

#ifndef SERVER_RECORDER_CONFIG_H_
#define SERVER_RECORDER_CONFIG_H_

/*
 * header
 */

/*
 * define
 */
#define		CONFIG_RECORDER_MODULE_NUM			1
#define		CONFIG_RECORDER_PROFILE				0

#define 	CONFIG_RECORDER_PROFILE_PATH				"config/recorder_profile.config"

/*
 * structure
 */
typedef struct recorder_quality_t {
	int	bitrate;
	int audio_sample;
} recorder_quality_t;

typedef struct recorder_profile_config_t {
	unsigned int		enable;
	unsigned int		mode;
	char				normal_start[MAX_SYSTEM_STRING_SIZE];
	char				normal_end[MAX_SYSTEM_STRING_SIZE];
	int					normal_repeat;
	int					normal_repeat_interval;
	int					normal_audio;
	int					normal_quality;
	recorder_quality_t	quality[3];
	unsigned int		max_length;		//in seconds
	unsigned int		min_length;
	char				directory[MAX_SYSTEM_STRING_SIZE];
	char				normal_prefix[MAX_SYSTEM_STRING_SIZE];
	char				motion_prefix[MAX_SYSTEM_STRING_SIZE];
	char				alarm_prefix[MAX_SYSTEM_STRING_SIZE];
} recorder_profile_config_t;

typedef struct recorder_config_t {
	int							status;
	recorder_profile_config_t	profile;
} recorder_config_t;

/*
 * function
 */
int config_recorder_read(recorder_config_t*);
int config_recorder_set(int module, void *arg);
int config_recorder_get_config_status(int module);

#endif /* CONFIG_RECORDER_CONFIG_H_ */
