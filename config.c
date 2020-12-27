/*
 * config_recorder.c
 *
 *  Created on: Aug 16, 2020
 *      Author: ning
 */

/*
 * header
 */
//system header
#include <pthread.h>
#include <stdio.h>
#include <malloc.h>

//program header
#include "../../tools/tools_interface.h"
#include "../../manager/manager_interface.h"
//server header
#include "config.h"

/*
 * static
 */
//variable
static int						dirty;
static recorder_config_t		recorder_config;
static config_map_t recorder_config_profile_map[] = {
	{"enable",     				&(recorder_config.profile.enable),      				cfg_u32, 	0,0,0,1,  	},
	{"mode",     				&(recorder_config.profile.mode),      					cfg_u32, 	0,0,0,10,  	},
	{"normal_start",      		&(recorder_config.profile.normal_start),       			cfg_string, "0",0,0,32,},
	{"normal_end",      		&(recorder_config.profile.normal_end),   				cfg_string, "0",0,0,32,},
	{"normal_repeat",     		&(recorder_config.profile.normal_repeat),      			cfg_u32, 	0,0,0,1,  	},
	{"normal_repeat_interval",  &(recorder_config.profile.normal_repeat_interval),      cfg_u32, 	0,0,0,1000000,  	},
	{"normal_audio",     		&(recorder_config.profile.normal_audio),      			cfg_u32, 	0,0,0,1,  	},
	{"normal_quality",     		&(recorder_config.profile.normal_quality),      		cfg_u32, 	0,0,0,2,  	},
	{"low_bitrate",     	&(recorder_config.profile.quality[0].bitrate),      	cfg_u32, 	512,0,0,10000,  	},
	{"low_audio_sample",	&(recorder_config.profile.quality[0].audio_sample),		cfg_u32, 	8,0,0,1000000,},
	{"medium_bitrate",     	&(recorder_config.profile.quality[1].bitrate),      	cfg_u32, 	1024,0,0,10000,  	},
	{"medium_audio_sample",	&(recorder_config.profile.quality[1].audio_sample),		cfg_u32, 	8,0,0,1000000,},
	{"high_bitrate",     	&(recorder_config.profile.quality[2].bitrate),      	cfg_u32, 	2048,0,0,10000,  	},
	{"high_audio_sample",	&(recorder_config.profile.quality[2].audio_sample),		cfg_u32, 	8,0,0,1000000,},
	{"max_length",      	&(recorder_config.profile.max_length),      cfg_u32, 	600,0,0,36000,},
	{"min_length",      	&(recorder_config.profile.min_length),      cfg_u32, 	3,0,0,36000,},
	{"directory",      		&(recorder_config.profile.directory),       cfg_string, "/mnt/nfs/sd/",0,0,32,},
	{"normal_prefix",      	&(recorder_config.profile.normal_prefix),   cfg_string, "normal",0,0,32,},
	{"motion_prefix", 		&(recorder_config.profile.motion_prefix),   cfg_string, "motion",0,0,32,},
	{"alarm_prefix",      	&(recorder_config.profile.alarm_prefix),   	cfg_string, "alarm",0,0,32,},
    {NULL,},
};
//function
static int recorder_config_save(void);

/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * interface
 */
static int recorder_config_save(void)
{
	int ret = 0;
	message_t msg;
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	if( misc_get_bit(dirty, CONFIG_RECORDER_PROFILE) ) {
		memset(fname,0,sizeof(fname));
		sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_RECORDER_PROFILE_PATH);
		ret = write_config_file(&recorder_config_profile_map, fname);
		if(!ret)
			misc_set_bit(&dirty, CONFIG_RECORDER_PROFILE, 0);
	}
	if( !dirty ) {
		/********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_TIMER_REMOVE;
		msg.arg_in.handler = recorder_config_save;
		/****************************/
		manager_common_send_message(SERVER_MANAGER, &msg);
	}
	return ret;
}

int config_recorder_read(recorder_config_t *rconfig)
{
	int ret,ret1=0;
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_RECORDER_PROFILE_PATH);
	ret = read_config_file(&recorder_config_profile_map, fname);
	if(!ret)
		misc_set_bit(&recorder_config.status, CONFIG_RECORDER_PROFILE,1);
	else
		misc_set_bit(&recorder_config.status, CONFIG_RECORDER_PROFILE,0);
	ret1 |= ret;
	memcpy(rconfig,&recorder_config,sizeof(recorder_config_t));
	return ret1;
}

int config_recorder_set(int module, void *arg)
{
	int ret = 0;
	if(dirty==0) {
		message_t msg;
	    /********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_TIMER_ADD;
		msg.sender = SERVER_RECORDER;
		msg.arg_in.cat = FILE_FLUSH_TIME;	//1min
		msg.arg_in.dog = 0;
		msg.arg_in.duck = 0;
		msg.arg_in.handler = &recorder_config_save;
		/****************************/
		manager_common_send_message(SERVER_MANAGER, &msg);
	}
	misc_set_bit(&dirty, module, 1);
	if( module == CONFIG_RECORDER_PROFILE) {
		memcpy( (recorder_profile_config_t*)(&recorder_config.profile), arg, sizeof(recorder_profile_config_t));
	}
	return ret;
}
