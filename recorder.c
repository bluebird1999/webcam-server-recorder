/*
 * recorder.c
 *
 *  Created on: Oct 6, 2020
 *      Author: ning
 */



/*
 * header
 */
//system header
#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <rtscamkit.h>
#include <rtsavapi.h>
#include <rtsvideo.h>
#include <malloc.h>
//program header
#include "../../manager/manager_interface.h"
#include "../../server/realtek/realtek_interface.h"
#include "../../tools/tools_interface.h"
#include "../../server/recorder/recorder_interface.h"
#include "../../server/miio/miio_interface.h"
#include "../../server/video/video_interface.h"
#include "../../server/audio/audio_interface.h"
#include "../../server/device/device_interface.h"
#include "../../server/video2/video2_interface.h"
//server header
#include "recorder.h"
#include "recorder_interface.h"
#include "config.h"

/*
 * static
 */
//variable
static 	message_buffer_t		message;
static 	server_info_t 			info;
static	recorder_config_t		config;
static 	message_buffer_t		video_buff[MAX_RECORDER_JOB];
static 	message_buffer_t		audio_buff[MAX_RECORDER_JOB];
static 	recorder_job_t			jobs[MAX_RECORDER_JOB];

//function
//common
static void *server_func(void);
static int server_message_proc(void);
static int server_release(void);
static void task_default(void);
static void task_error(void);
static void server_thread_termination(int sign);
//specific
static int recorder_main(void);
static int recorder_add_job( message_t* msg );
static int count_job_number(void);
static int *recorder_func(void *arg);
static int server_set_status(int type, int st, int value);
static int recorder_thread_init_mp4v2( recorder_job_t *ctrl);
static int recorder_thread_write_mp4_video( recorder_job_t *ctrl, message_t *msg );
static int recorder_thread_close( recorder_job_t *ctrl );
static int recorder_thread_check_finish( recorder_job_t *ctrl );
static int recorder_thread_error( recorder_job_t *ctrl);
static int recorder_thread_pause( recorder_job_t *ctrl);
static int recorder_thread_destroy( recorder_job_t *ctrl );
static int recorder_thread_start_stream( recorder_job_t *ctrl );
static int recorder_thread_stop_stream( recorder_job_t *ctrl );
static int recorder_thread_check_and_exit_stream( recorder_job_t *ctrl );
static int count_job_other_live(int myself);
static int recorder_start_init_recorder_job(void);
static int send_message(int receiver, message_t *msg);
static int recorder_get_property(message_t *msg);
static int recorder_set_property(message_t *msg);
static int recorder_quit_all(int id);
/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * helper
 */
static int send_message(int receiver, message_t *msg)
{
	int st = 0;
	switch(receiver) {
		case SERVER_DEVICE:
			st = server_device_message(msg);
			break;
		case SERVER_KERNEL:
	//		st = server_kernel_message(msg);
			break;
		case SERVER_REALTEK:
			st = server_realtek_message(msg);
			break;
		case SERVER_MIIO:
			st = server_miio_message(msg);
			break;
		case SERVER_MISS:
			st = server_miss_message(msg);
			break;
		case SERVER_MICLOUD:
	//		st = server_micloud_message(msg);
			break;
		case SERVER_VIDEO:
			st = server_video_message(msg);
			break;
		case SERVER_AUDIO:
			st = server_audio_message(msg);
			break;
		case SERVER_RECORDER:
			st = server_recorder_message(msg);
			break;
		case SERVER_PLAYER:
			st = server_player_message(msg);
			break;
		case SERVER_SPEAKER:
			st = server_speaker_message(msg);
			break;
		case SERVER_VIDEO2:
			st = server_video2_message(msg);
			break;
		case SERVER_SCANNER:
//			st = server_scanner_message(msg);
			break;
		case SERVER_MANAGER:
			st = manager_message(msg);
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "unknown message target! %d", receiver);
			break;
	}
	return st;
}

static int recorder_quit_all(int id)
{
	int ret = 0;
	int i;
	ret = pthread_rwlock_wrlock(&info.lock);
	if(ret)	{
		log_qcy(DEBUG_SERIOUS, "add message lock fail, ret = %d\n", ret);
		return ret;
	}
	for( i=0; i<MAX_RECORDER_JOB; i++ ) {
		if( id !=-1 && i!=id ) continue;
		misc_set_bit( &info.status2, i, 1);
	}
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d\n", ret);
	}
	return ret;
}

static int recorder_get_property(message_t *msg)
{
	int ret = 0, st;
	message_t send_msg;
	st = info.status;
    /********message body********/
	msg_init(&send_msg);
	memcpy(&(send_msg.arg_pass), &(msg->arg_pass),sizeof(message_arg_t));
	send_msg.message = msg->message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_RECORDER;
	send_msg.arg_in.cat = msg->arg_in.cat;
	/**************************/
	if( misc_get_bit( info.thread_exit, RECORDER_INIT_CONDITION_CONFIG)==0 )
		send_msg.result = -1;
	else {
		send_msg.result = 0;
		if( msg->arg_in.cat == RECORDER_PROPERTY_SAVE_MODE) {
			send_msg.arg = (void*)(&config.profile.enable);
			send_msg.arg_size = sizeof(config.profile.enable);
		}
		else if( msg->arg_in.cat == RECORDER_PROPERTY_RECORDING_MODE) {
			send_msg.arg = (void*)(&config.profile.mode);
			send_msg.arg_size = sizeof(config.profile.mode);
		}
		else if( msg->arg_in.cat == RECORDER_PROPERTY_CONFIG_STATUS) {
			int temp = 1;
			send_msg.arg = (void*)(&temp);
			send_msg.arg_size = sizeof(config.profile.mode);
		}
		else if( msg->arg_in.cat == RECORDER_PROPERTY_NORMAL_DIRECTORY) {
			char path[MAX_SYSTEM_STRING_SIZE*2];
			memset(path, 0, sizeof(path));
			sprintf(path, "%s%s/", config.profile.directory, config.profile.normal_prefix);
			send_msg.arg = path;
			send_msg.arg_size = strlen(path) + 1;
			send_msg.extra = config.profile.normal_prefix;
			send_msg.extra_size = strlen(config.profile.normal_prefix) + 1;
		}
	}
	ret = send_message(msg->receiver, &send_msg);
	return ret;
}

static int recorder_set_property(message_t *msg)
{
	int ret=0;
	message_t send_msg;
    /********message body********/
	msg_init(&send_msg);
	memcpy(&(send_msg.arg_pass), &(msg->arg_pass),sizeof(message_arg_t));
	send_msg.message = msg->message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_RECORDER;
	send_msg.arg_in.cat = msg->arg_in.cat;
	/**************************/
	if( msg->arg_in.cat == RECORDER_PROPERTY_SAVE_MODE ) {
		int temp = *((int*)(msg->arg));
		if( temp != config.profile.enable) {
			if( config.profile.enable == 1 ) {
				recorder_quit_all(-1);
				info.status = STATUS_WAIT;
			}
			config.profile.enable = temp;
			log_qcy(DEBUG_SERIOUS, "changed the enable = %d", config.profile.enable);
			config_recorder_set(CONFIG_RECORDER_PROFILE, &config.profile);
			send_msg.result = 0;
		}
	}
	else if( msg->arg_in.cat == RECORDER_PROPERTY_RECORDING_MODE ) {
		int temp = *((int*)(msg->arg));
		if( temp != config.profile.mode) {
			config.profile.mode = temp;
			log_qcy(DEBUG_SERIOUS, "changed the mode = %d", config.profile.mode);
			config_recorder_set(CONFIG_RECORDER_PROFILE, &config.profile);
		    /********message body********/
			message_t msg;
			msg_init(&msg);
			msg.message = MSG_VIDEO2_PROPERTY_SET;
			msg.arg_in.cat = VIDEO2_PROPERTY_QUALITY;
			msg.arg = &config.profile.mode;
			msg.arg_size = sizeof(config.profile.mode);
			msg.sender = msg.receiver = SERVER_RECORDER;
			server_video2_message(&msg);
			/****************************/
		}
		send_msg.result = 0;
	}
	ret = send_message(msg->receiver, &send_msg);
	return ret;
}

static int recorder_start_init_recorder_job(void)
{
	message_t msg;
	recorder_init_t init;
	int ret=0;
	/********message body********/
	msg_init(&msg);
	msg.message = MSG_RECORDER_ADD;
	msg.sender = msg.receiver = SERVER_RECORDER;
	init.video_channel = 1;
	init.mode = RECORDER_MODE_BY_TIME;
	init.type = RECORDER_TYPE_NORMAL;
	init.audio = config.profile.normal_audio;
	memcpy( &(init.start),config.profile.normal_start, strlen(config.profile.normal_start));
	memcpy( &(init.stop),config.profile.normal_end, strlen(config.profile.normal_end));
	init.repeat = config.profile.normal_repeat;
	init.repeat_interval = config.profile.normal_repeat_interval;
	init.quality = config.profile.normal_quality;
	msg.arg = &init;
	msg.arg_size = sizeof(recorder_init_t);
	ret = server_recorder_message(&msg);
	/********message body********/
	return ret;
}

static int recorder_thread_error( recorder_job_t *ctrl )
{
	int ret = 0;
	log_qcy(DEBUG_SERIOUS, "errors in this recorder thread, stop!");
	ctrl->run.exit = 1;
	return ret;
}

static int recorder_thread_start_stream( recorder_job_t *ctrl )
{
	message_t msg;
    /********message body********/
	memset(&msg,0,sizeof(message_t));
	msg.arg_in.cat = ctrl->init.type;
	if( ctrl->init.video_channel == 0)
		msg.message = MSG_VIDEO_START;
	else if( ctrl->init.video_channel == 1)
		msg.message = MSG_VIDEO2_START;
	msg.sender = msg.receiver = SERVER_RECORDER;
	if( ctrl->init.video_channel == 0) {
	    if( server_video_message(&msg)!=0 ) {
	    	log_qcy(DEBUG_SERIOUS, "video start failed from recorder!");
	    }
	}
	else if( ctrl->init.video_channel == 1) {
	    if( server_video2_message(&msg)!=0 ) {
	    	log_qcy(DEBUG_SERIOUS, "video2 start failed from recorder!");
	    }
	}
    if( ctrl->init.audio ) {
		memset(&msg,0,sizeof(message_t));
		msg.message = MSG_AUDIO_START;
		msg.sender = msg.receiver = SERVER_RECORDER;
		msg.arg_in.cat = ctrl->init.type;
		if( server_audio_message(&msg)!=0 ) {
			log_qcy(DEBUG_SERIOUS, "audio start failed from recorder!");
		}
    }
    /****************************/
}

static int recorder_thread_stop_stream( recorder_job_t *ctrl )
{
	message_t msg;
    /********message body********/
	memset(&msg,0,sizeof(message_t));
	msg.arg_in.cat = ctrl->init.type;
	if( ctrl->init.video_channel == 0)
		msg.message = MSG_VIDEO_STOP;
	else if( ctrl->init.video_channel == 1)
		msg.message = MSG_VIDEO2_STOP;
	msg.sender = msg.receiver = SERVER_RECORDER;
	if( ctrl->init.video_channel == 0) {
	    if( server_video_message(&msg)!=0 ) {
	    	log_qcy(DEBUG_WARNING, "video stop failed from recorder!");
	    }
	}
	else if( ctrl->init.video_channel == 1) {
	    if( server_video2_message(&msg)!=0 ) {
	    	log_qcy(DEBUG_WARNING, "video2 stop failed from recorder!");
	    }
	}
    if( ctrl->init.audio ) {
		memset(&msg,0,sizeof(message_t));
		msg.message = MSG_AUDIO_STOP;
		msg.sender = msg.receiver = SERVER_RECORDER;
		msg.arg_in.cat = ctrl->init.type;
		if( server_audio_message(&msg)!=0 ) {
			log_qcy(DEBUG_WARNING, "audio stop failed from recorder!");
		}
    }
    /****************************/
}

static int recorder_thread_check_finish( recorder_job_t *ctrl )
{
	int ret = 0;
	long long int now = 0;
	now = time_get_now_stamp();
	if( now >= ctrl->run.stop )
		ret = 1;
	return ret;
}

static int recorder_thread_close( recorder_job_t *ctrl )
{
	char oldname[MAX_SYSTEM_STRING_SIZE*2];
	char start[MAX_SYSTEM_STRING_SIZE*2];
	char stop[MAX_SYSTEM_STRING_SIZE*2];
	char prefix[MAX_SYSTEM_STRING_SIZE];
	char alltime[MAX_SYSTEM_STRING_SIZE];
	message_t msg;
	int ret = 0;
	if(ctrl->run.mp4_file != MP4_INVALID_FILE_HANDLE) {
		log_qcy(DEBUG_INFO, "+++MP4Close\n");
		MP4Close(ctrl->run.mp4_file, MP4_CLOSE_DO_NOT_COMPUTE_BITRATE);
		ctrl->run.mp4_file = MP4_INVALID_FILE_HANDLE;
	}
	else {
		return -1;
	}
	if( (ctrl->run.last_write - ctrl->run.real_start) < ctrl->config.profile.min_length ) {
		log_qcy(DEBUG_WARNING, "Recording file %s is too short, removed!", ctrl->run.file_path);
		//remove file here.
		remove(ctrl->run.file_path);
		return -1;
	}
	ctrl->run.real_stop = ctrl->run.last_write;
	memset(oldname,0,sizeof(oldname));
	memset(start,0,sizeof(start));
	memset(stop,0,sizeof(stop));
	memset(prefix,0,sizeof(prefix));
	time_stamp_to_date(ctrl->run.real_start, start);
	time_stamp_to_date(ctrl->run.last_write, stop);
	strcpy(oldname, ctrl->run.file_path);
	if( ctrl->init.type == RECORDER_TYPE_NORMAL)
		strcpy( &prefix, ctrl->config.profile.normal_prefix);
	else if( ctrl->init.type == RECORDER_TYPE_MOTION_DETECTION)
		strcpy( &prefix, ctrl->config.profile.motion_prefix);
	else if( ctrl->init.type == RECORDER_TYPE_ALARM)
		strcpy( &prefix, ctrl->config.profile.alarm_prefix);
	sprintf( ctrl->run.file_path, "%s%s/%s-%s_%s.mp4",ctrl->config.profile.directory,prefix,prefix,start,stop);
	ret = rename(oldname, ctrl->run.file_path);
	if(ret) {
		log_qcy(DEBUG_WARNING, "rename recording file %s to %s failed.\n", oldname, ctrl->run.file_path);
	}
	else {
	    /********message body********/
		msg_init(&msg);
		msg.message = MSG_RECORDER_ADD_FILE;
		msg.sender = msg.receiver = SERVER_RECORDER;
		memset(alltime, 0, sizeof(alltime));
		sprintf(alltime, "%s%s", start, stop);
		msg.arg = alltime;
		msg.arg_size = strlen(alltime) + 1;
		ret = send_message(SERVER_PLAYER, &msg);
		/***************************/
		log_qcy(DEBUG_INFO, "Record file is %s\n", ctrl->run.file_path);
	}
	return ret;
}

static int recorder_thread_write_mp4_video( recorder_job_t *ctrl, message_t *msg)
{
	unsigned char *p_data = (unsigned char*)msg->extra;
	unsigned int data_length = msg->extra_size;
	av_data_info_t *info = (av_data_info_t*)(msg->arg);
	nalu_unit_t nalu;
	int ret;
	int			frame_len;
	MP4Duration duration, offset;
	memset(&nalu, 0, sizeof(nalu_unit_t));
	int pos = 0, len = 0;
	while ( (len = h264_read_nalu(p_data, data_length, pos, &nalu)) != 0) {
		switch ( nalu.type) {
			case 0x07:
				if ( !ctrl->run.sps_read ) {
					ctrl->run.video_track = MP4AddH264VideoTrack(ctrl->run.mp4_file, 90000,
							MP4_INVALID_DURATION /*90000 / info->fps*/,
							info->width,
							info->height,
							nalu.data[1], nalu.data[2], nalu.data[3], 3);
	//					ctrl->run.video_track = MP4AddH264VideoTrack(ctrl->run.mp4_file, 90000, 90000/15, 800, 600,0x4d, 0x40, 0x1f, 3);
						if( ctrl->run.video_track == MP4_INVALID_TRACK_ID ) {
							return -1;
						}
						ctrl->run.sps_read = 1;
						MP4SetVideoProfileLevel( ctrl->run.mp4_file, 0x7F);
						MP4AddH264SequenceParameterSet( ctrl->run.mp4_file, ctrl->run.video_track, nalu.data, nalu.size);
					}
					break;
			case 0x08:
				if ( ctrl->run.pps_read ) break;
				if ( !ctrl->run.sps_read ) break;
				ctrl->run.pps_read = 1;
				MP4AddH264PictureParameterSet(ctrl->run.mp4_file, ctrl->run.video_track, nalu.data, nalu.size);
				break;
			case 0x1:
			case 0x5:
				if ( !ctrl->run.sps_read || !ctrl->run.pps_read ) {
					return -2;
				}
				int nlength = nalu.size + 4;
				unsigned char *data = (unsigned char *)malloc(nlength);
				if(!data) {
					log_qcy(DEBUG_SERIOUS, "mp4_video_frame_write malloc failed\n");
					return -1;
				}
				data[0] = nalu.size >> 24;
				data[1] = nalu.size >> 16;
				data[2] = nalu.size >> 8;
				data[3] = nalu.size & 0xff;
				memcpy(data + 4, nalu.data, nalu.size);
				int key = (nalu.type == 0x5?1:0);
				if( !key && !ctrl->run.first_frame ) {
					free(data);
					return -2;
				}
				if( ctrl->run.first_frame ) {
					duration 	= ( info->timestamp - ctrl->run.last_vframe_stamp) * 90000 /1000;
					offset 		= ( info->timestamp - ctrl->run.first_frame_stamp) * 90000 / 1000;
				}
				else {
					duration	= 90000 / info->fps;
					offset		= 0;
				}
				ret = MP4WriteSample(ctrl->run.mp4_file, ctrl->run.video_track, data, nlength,
						duration, 0, key);
				if( !ret ) {
				  free(data);
				  return -1;
				}
				if( !ctrl->run.first_frame && key) {
					ctrl->run.real_start = time_get_now_stamp();
					ctrl->run.fps = info->fps;
					ctrl->run.width = info->width;
					ctrl->run.height = info->height;
					ctrl->run.first_frame_stamp = info->timestamp;
					ctrl->run.first_frame = 1;
				}
				ctrl->run.last_write = time_get_now_stamp();
				ctrl->run.last_vframe_stamp = info->timestamp;
				free(data);
			break;
			  default :
				  break;
		}
		pos += len;
	}
	return 0;
}


static int recorder_thread_init_mp4v2( recorder_job_t *ctrl)
{
	int ret = 0;
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	char prefix[MAX_SYSTEM_STRING_SIZE];
	char timestr[MAX_SYSTEM_STRING_SIZE];
	memset( fname, 0, sizeof(fname));
	if( ctrl->init.type == RECORDER_TYPE_NORMAL)
		strcpy( &prefix, ctrl->config.profile.normal_prefix);
	else if( ctrl->init.type == RECORDER_TYPE_MOTION_DETECTION)
		strcpy( &prefix, ctrl->config.profile.motion_prefix);
	else if( ctrl->init.type == RECORDER_TYPE_ALARM)
		strcpy( &prefix, ctrl->config.profile.alarm_prefix);
	time_stamp_to_date(ctrl->run.start, timestr);
	sprintf(fname,"%s%s/%s-%s",ctrl->config.profile.directory,prefix,prefix,timestr);
	ctrl->run.mp4_file = MP4CreateEx(fname,	0, 1, 1, 0, 0, 0, 0);
	if ( ctrl->run.mp4_file == MP4_INVALID_FILE_HANDLE) {
		printf("MP4CreateEx file failed.\n");
		return -1;
	}
	MP4SetTimeScale( ctrl->run.mp4_file, 90000);
	if( ctrl->init.audio ) {
		ctrl->run.audio_track = MP4AddALawAudioTrack( ctrl->run.mp4_file,
				ctrl->config.profile.quality[ctrl->init.quality].audio_sample);
		if ( ctrl->run.audio_track == MP4_INVALID_TRACK_ID) {
			printf("add audio track failed.\n");
			return -1;
		}
		MP4SetTrackIntegerProperty( ctrl->run.mp4_file, ctrl->run.audio_track, "mdia.minf.stbl.stsd.alaw.channels", 1);
		MP4SetTrackIntegerProperty( ctrl->run.mp4_file, ctrl->run.audio_track, "mdia.minf.stbl.stsd.alaw.sampleSize", 16);
	    MP4SetAudioProfileLevel(ctrl->run.mp4_file, 0x02);
	}
	memset( ctrl->run.file_path, 0, sizeof(ctrl->run.file_path));
	strcpy(ctrl->run.file_path, fname);
	return ret;
}

static int recorder_thread_pause( recorder_job_t *ctrl)
{
	int ret = 0;
	long long int temp1 = 0, temp2 = 0;
	if( ctrl->init.repeat==0 ) {
		ctrl->run.exit = 1;
		return 0;
	}
	else {
		temp1 = ctrl->run.real_stop;
		temp2 = ctrl->run.stop - ctrl->run.start;
		memset( &ctrl->run, 0, sizeof( recorder_run_t));
		ctrl->run.start = temp1 + ctrl->init.repeat_interval;
		ctrl->run.stop = ctrl->run.start + temp2;
		ctrl->status = RECORDER_THREAD_STARTED;
		log_qcy(DEBUG_SERIOUS, "-------------add recursive recorder---------------------");
		log_qcy(DEBUG_SERIOUS, "now=%ld", time_get_now_stamp());
		log_qcy(DEBUG_SERIOUS, "start=%ld", ctrl->run.start);
		log_qcy(DEBUG_SERIOUS, "end=%ld", ctrl->run.stop);
		log_qcy(DEBUG_SERIOUS, "--------------------------------------------------");
	}
	if( time_get_now_stamp() < (ctrl->run.start - MAX_BETWEEN_RECODER_PAUSE) ) {
		recorder_thread_check_and_exit_stream(ctrl);
	}
	return ret;
}

static int recorder_thread_run( recorder_job_t *ctrl)
{
	message_t		vmsg, amsg;
	int 			ret_video = 1, 	ret_audio = 1, ret;
	av_data_info_t *info;
	unsigned char	*p;
	MP4Duration duration, offset;
    //read video frame
	ret = pthread_rwlock_wrlock(&video_buff[ctrl->t_id].lock);
	if(ret)	{
		log_qcy(DEBUG_SERIOUS, "add message lock fail, ret = %d\n", ret);
		return ERR_LOCK;
	}
	msg_init(&vmsg);
	ret_video = msg_buffer_pop(&video_buff[ctrl->t_id], &vmsg);
	ret = pthread_rwlock_unlock(&video_buff[ctrl->t_id].lock);
	if (ret) {
		log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d\n", ret);
		ret = ERR_LOCK;
		goto exit;
	}
    //read audio frame
	if( ctrl->init.audio ) {
		ret = pthread_rwlock_wrlock(&audio_buff[ctrl->t_id].lock);
		if(ret)	{
			log_qcy(DEBUG_SERIOUS, "add message lock fail, ret = %d\n", ret);
			ret = ERR_LOCK;
			goto exit;
		}
		msg_init(&amsg);
		ret_audio = msg_buffer_pop(&audio_buff[ctrl->t_id], &amsg);
		ret = pthread_rwlock_unlock(&audio_buff[ctrl->t_id].lock);
		if (ret) {
			log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d\n", ret);
			ret = ERR_LOCK;
			goto exit;
		}
	}
	if( ret_audio && ret_video ) {	//no data
//		usleep(10000);
		ret = ERR_NO_DATA;
		goto exit;
	}
	if ( !ret_audio ) {
		info = (av_data_info_t*)(amsg.arg);
		p = (unsigned char*)amsg.extra;
		if( ctrl->run.first_frame ) {
			if( ctrl->run.first_audio ) {
				duration 	= ( info->timestamp - ctrl->run.last_aframe_stamp) * 90000 /1000;
				offset 		= ( info->timestamp - ctrl->run.first_frame_stamp) * 90000 / 1000;
			}
			else {
				duration	= 32 * 90000 / 1000;	//8k mono channel
				offset		= 0;
				ctrl->run.first_audio = 1;
			}
			if( !MP4WriteSample( ctrl->run.mp4_file, ctrl->run.audio_track, p, amsg.extra_size ,
					256, 0, 1) ) {
				log_qcy(DEBUG_WARNING, "MP4WriteSample audio failed.\n");
				ret = ERR_NO_DATA;
			}
			ctrl->run.last_aframe_stamp = info->timestamp;
		}
	}
	if( !ret_video ) {
		info = (av_data_info_t*)(vmsg.arg);
		if( ctrl->run.first_frame && info->fps != ctrl->run.fps) {
			log_qcy(DEBUG_SERIOUS, "the video fps has changed, stop recording!");
			ret = ERR_ERROR;
			goto close_exit;
		}
		if(  ctrl->run.first_frame && (info->width != ctrl->run.width || info->height != ctrl->run.height) ) {
			log_qcy(DEBUG_SERIOUS, "the video dimention has changed, stop recording!");
			ret = ERR_ERROR;
			goto close_exit;
		}
		ret = recorder_thread_write_mp4_video( ctrl, &vmsg);
		if(ret ==-1 ) {
			log_qcy(DEBUG_WARNING, "MP4WriteSample video failed.\n");
			ret = ERR_NO_DATA;
			goto exit;
		}
		if( recorder_thread_check_finish(ctrl) ) {
			log_qcy(DEBUG_INFO, "------------stop=%d------------", time_get_now_stamp());
			log_qcy(DEBUG_INFO, "recording finished!");
			goto close_exit;
		}
	}
exit:
	if( !ret_video )
		msg_free(&vmsg);
    if( !ret_audio )
    	msg_free(&amsg);
    return ret;
close_exit:
	ret = recorder_thread_close(ctrl);
	ctrl->status = RECORDER_THREAD_PAUSE;
	if( !ret_video )
		msg_free(&vmsg);
    if( !ret_audio )
    	msg_free(&amsg);
    return ret;
}

static int recorder_thread_started( recorder_job_t *ctrl )
{
	int ret;
	if( time_get_now_stamp() >= ctrl->run.start ) {
		log_qcy(DEBUG_SERIOUS, "------------start=%ld------------", time_get_now_stamp());
		ret = recorder_thread_init_mp4v2( ctrl );
		if( ret ) {
			log_qcy(DEBUG_SERIOUS, "init mp4v2 failed!");
			ctrl->status = RECORDER_THREAD_ERROR;
		}
		else {
			ctrl->status = RECORDER_THREAD_RUN;
			recorder_thread_start_stream( ctrl );
		}
	}
	else
		usleep(1000);
	return ret;
}

static int server_set_status(int type, int st, int value)
{
	int ret=-1;

	return ret;
}

static int *recorder_func(void *arg)
{
	recorder_job_t ctrl;
	char fname[MAX_SYSTEM_STRING_SIZE];
	memcpy(&ctrl, (recorder_job_t*)arg, sizeof(recorder_job_t));
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
    sprintf(fname, "%d%d-%d",ctrl.t_id,ctrl.init.video_channel, time_get_now_stamp());
    misc_set_thread_name(fname);
    pthread_detach(pthread_self());
	if( !video_buff[ctrl.t_id].init ) {
		msg_buffer_init(&video_buff[ctrl.t_id], MSG_BUFFER_OVERFLOW_YES);
	}
	if( !audio_buff[ctrl.t_id].init ) {
		msg_buffer_init(&audio_buff[ctrl.t_id], MSG_BUFFER_OVERFLOW_YES);
	}
	pthread_rwlock_init(&ctrl.run.lock, NULL);
	if (ctrl.init.start[0] == '0') ctrl.run.start = time_get_now_stamp();
	else ctrl.run.start = time_date_to_stamp(ctrl.init.start);// - _config_.timezone * 3600;
	if (ctrl.init.stop[0] == '0') ctrl.run.stop = ctrl.run.start + ctrl.config.profile.max_length;
	else ctrl.run.stop = time_date_to_stamp( ctrl.init.stop);// - _config_.timezone * 3600;
	if( (ctrl.run.stop - ctrl.run.start) < ctrl.config.profile.min_length ||
			(ctrl.run.stop - ctrl.run.start) > ctrl.config.profile.max_length )
		ctrl.run.stop = ctrl.run.start + ctrl.config.profile.max_length;
	log_qcy(DEBUG_INFO, "-------------add new recorder---------------------");
	log_qcy(DEBUG_INFO, "now=%ld", time_get_now_stamp());
	log_qcy(DEBUG_INFO, "start=%ld", ctrl.run.start);
	log_qcy(DEBUG_INFO, "end=%ld", ctrl.run.stop);
	log_qcy(DEBUG_INFO, "video channel=%d", ctrl.init.video_channel);
	log_qcy(DEBUG_INFO, "--------------------------------------------------");
    ctrl.status = RECORDER_THREAD_STARTED;
    while( !info.exit && !ctrl.run.exit && !misc_get_bit(info.status2, ctrl.t_id) ) {
    	switch( ctrl.status ) {
    		case RECORDER_THREAD_STARTED:
    			recorder_thread_started(&ctrl);
    			break;
    		case RECORDER_THREAD_RUN:
    			recorder_thread_run(&ctrl);
    			break;
    		case RECORDER_THREAD_PAUSE:
    			recorder_thread_pause(&ctrl);
    			break;
    		case RECORDER_THREAD_ERROR:
    			recorder_thread_error(&ctrl);
    			break;
    	}
    }
    //release
    recorder_thread_destroy(&ctrl);
    log_qcy(DEBUG_SERIOUS, "-----------thread exit: record %s-----------", fname);
    pthread_exit(0);
}

static int recorder_thread_check_and_exit_stream( recorder_job_t *ctrl )
{
	int ret=0,ret1;
	int i;
	ret = pthread_rwlock_trywrlock(&info.lock);
	if(ret)	{
		log_qcy(DEBUG_SERIOUS, "add message lock fail, ret = %d\n", ret);
		return ret;
	}
	if( !count_job_other_live(ctrl->t_id) ) {
		recorder_thread_stop_stream( ctrl );
	}
	ret1 = pthread_rwlock_unlock(&info.lock);
	if (ret1)
		log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d\n", ret1);
	return ret;
}

static int recorder_thread_destroy( recorder_job_t *ctrl )
{
	int ret=0,ret1;
	recorder_thread_close( ctrl );
	msg_buffer_release(&video_buff[ctrl->t_id]);
	msg_buffer_release(&audio_buff[ctrl->t_id]);
	if( !count_job_other_live(ctrl->t_id) ) {
		recorder_thread_stop_stream( ctrl );
	}
	ret = pthread_rwlock_trywrlock(&info.lock);
	if(ret)	{
		log_qcy(DEBUG_SERIOUS, "add lock fail, ret = %d\n", ret);
		return ret;
	}
	misc_set_bit( &info.thread_start, ctrl->t_id, 0);
	misc_set_bit( &info.status2,ctrl->t_id, 0);
	memset(&jobs[ctrl->t_id], 0, sizeof(recorder_job_t));
	ret1 = pthread_rwlock_unlock(&info.lock);
	if (ret1)
		log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d\n", ret1);
	return ret;
}

static int count_job_other_live(int myself)
{
	int i,num=0;
	for( i=0; (i<MAX_RECORDER_JOB) && (i!=myself); i++ ) {
		if( jobs[i].status>0  ) {
			if( jobs[i].init.video_channel == jobs[myself].init.video_channel )
				num++;
		}
	}
	return num;
}

static int count_job_number(void)
{
	int i,num=0;
	for( i=0; i<MAX_RECORDER_JOB; i++ ) {
		if( jobs[i].status>0 )
			num++;
	}
	return num;
}

static int recorder_add_job( message_t* msg )
{
	message_t send_msg;
	int i=-1;
	int ret = 0;
	pthread_t 			pid;
    /********message body********/
	msg_init(&send_msg);
	send_msg.message = msg->message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_RECORDER;
	/***************************/
	if( count_job_number() == MAX_RECORDER_JOB) {
		send_msg.result = -1;
		ret = send_message(msg->receiver, &send_msg);
		return -1;
	}
	for(i = 0;i<MAX_RECORDER_JOB;i++) {
		if( jobs[i].status == RECORDER_THREAD_NONE ) {
			memset( &jobs[i], 0, sizeof(recorder_job_t));
			jobs[i].t_id = i;
			jobs[i].status = RECORDER_THREAD_INITED;
			//start the thread
			memcpy( &(jobs[i].config), &config, sizeof(recorder_config_t));
			memcpy( &(jobs[i].init), msg->arg, sizeof(recorder_init_t));
			ret = pthread_create(&pid, NULL, recorder_func, (void*)&jobs[i]);
			if(ret != 0) {
				log_qcy(DEBUG_SERIOUS, "recorder thread create error! ret = %d",ret);
				jobs[i].status = RECORDER_THREAD_NONE;
				jobs[i].t_id = -1;
			 }
			else {
				ret = pthread_rwlock_trywrlock(&info.lock);
				if(ret)	{
					log_qcy(DEBUG_SERIOUS, "add lock fail, ret = %d", ret);
					return ret;
				}
				misc_set_bit(&info.thread_start, i, 1);
				ret = pthread_rwlock_unlock(&info.lock);
				if (ret)
					log_qcy(DEBUG_SERIOUS, "add unlock fail, ret = %d", ret);
				log_qcy(DEBUG_INFO, "recorder thread create successful!");
				jobs[i].status = RECORDER_THREAD_STARTED;
			}
			break;
		}
	}
	send_msg.result = 0;
	ret = send_message(msg->receiver, &send_msg);
	return ret;
}

static int recorder_main(void)
{
	int ret = 0, i;
	if( !config.profile.enable )
		return ret;
	return ret;
}

static void server_thread_termination(int sign)
{
    /********message body********/
	message_t msg;
	msg_init(&msg);
	msg.message = MSG_RECORDER_SIGINT;
	msg.sender = msg.receiver = SERVER_RECORDER;
	manager_message(&msg);
	/****************************/
}

static int server_release(void)
{
	int ret = 0;
	int retain_exit = info.exit;
	msg_buffer_release(&message);
	msg_free(&info.task.msg);
	memset(&info,0,sizeof(server_info_t));
	info.exit = retain_exit;
	memset(&config,0,sizeof(recorder_config_t));
	memset(&jobs,0,sizeof(recorder_job_t));
	return ret;
}

static int server_message_proc(void)
{
	int ret = 0, ret1 = 0;
	message_t msg,send_msg;
	msg_init(&msg);
	ret = pthread_rwlock_wrlock(&message.lock);
	if(ret)	{
		log_qcy(DEBUG_SERIOUS, "add message lock fail, ret = %d\n", ret);
		return ret;
	}
	if( info.msg_lock ) {
		ret1 = pthread_rwlock_unlock(&message.lock);
		return 0;
	}
	ret = msg_buffer_pop(&message, &msg);
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1) {
		log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d\n", ret1);
	}
	if( ret == -1) {
		msg_free(&msg);
		return -1;
	}
	else if( ret == 1)
		return 0;
    /********message body********/
	msg_init(&send_msg);
	send_msg.message = msg.message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_RECORDER;
	/***************************/
	switch(msg.message) {
		case MSG_RECORDER_ADD:
			if( recorder_add_job(&msg) ) ret = -1;
			else ret = 0;
			send_msg.result = ret;
			send_message(msg.receiver, &send_msg);
			break;
		case MSG_RECORDER_ADD_ACK:
			break;
		case MSG_MANAGER_EXIT:
			info.exit = 1;
			break;
		case MSG_MANAGER_TIMER_ACK:
			((HANDLER)msg.arg_in.handler)();
			break;
		case MSG_DEVICE_GET_PARA_ACK:
			if( !msg.result ) {
				if( ((device_iot_config_t*)msg.arg)->sd_iot_info.plug &&
						( (((device_iot_config_t*)msg.arg)->sd_iot_info.freeBytes) > MIN_SD_SIZE_IN_MB) ) {
					if( info.status <= STATUS_WAIT ) {
						misc_set_bit( &info.thread_exit, RECORDER_INIT_CONDITION_DEVICE_CONFIG, 1);
					}
					else if( info.status == STATUS_IDLE )
						info.status = STATUS_START;
				}
				else {
					if( info.status == STATUS_RUN ) {
						recorder_quit_all(-1);
						info.status = STATUS_WAIT;
					}
				}
			}
			break;
		case MSG_RECORDER_PROPERTY_SET:
			ret = recorder_set_property(&msg);
			break;
		case MSG_RECORDER_PROPERTY_GET:
			ret = recorder_get_property(&msg);
			break;
		case MSG_MIIO_PROPERTY_NOTIFY:
		case MSG_MIIO_PROPERTY_GET_ACK:
			if( msg.arg_in.cat == MIIO_PROPERTY_TIME_SYNC ) {
				if( msg.arg_in.dog == 1 )
					misc_set_bit( &info.thread_exit, RECORDER_INIT_CONDITION_MIIO_TIME, 1);
			}
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "not processed message = %x", msg.message);
			break;
	}
	msg_free(&msg);
	return ret;
}

static int heart_beat_proc(void)
{
	int ret = 0;
	message_t msg;
	long long int tick = 0;
	tick = time_get_now_stamp();
	if( (tick - info.tick3) > SERVER_HEARTBEAT_INTERVAL ) {
		info.tick3 = tick;
	    /********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_HEARTBEAT;
		msg.sender = msg.receiver = SERVER_RECORDER;
		msg.arg_in.cat = info.status;
		msg.arg_in.dog = info.thread_start;
		msg.arg_in.duck = info.thread_exit;
		ret = manager_message(&msg);
		/***************************/
	}
	return ret;
}

/*
 * task
 */
/*
 * task error: error->5 seconds->shut down server->msg manager
 */
static void task_error(void)
{
	unsigned int tick=0;
	switch( info.status ) {
		case STATUS_ERROR:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!!error in recorder, restart in 5 s!");
			info.tick = time_get_now_stamp();
			info.status = STATUS_NONE;
			break;
		case STATUS_NONE:
			tick = time_get_now_stamp();
			if( (tick - info.tick) > SERVER_RESTART_PAUSE ) {
				info.exit = 1;
				info.tick = tick;
			}
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_error = %d", info.status);
			break;
	}
	usleep(1000);
	return;
}

/*
 * default task: none->run
 */
static void task_default(void)
{
	message_t msg;
	int ret = 0;
	switch( info.status){
		case STATUS_NONE:
			if( !misc_get_bit( info.thread_exit, RECORDER_INIT_CONDITION_CONFIG ) ) {
				ret = config_recorder_read(&config);
				if( !ret && misc_full_bit(config.status, CONFIG_RECORDER_MODULE_NUM) ) {
					misc_set_bit(&info.thread_exit, RECORDER_INIT_CONDITION_CONFIG, 1);
				}
				else {
					info.status = STATUS_ERROR;
					break;
				}
			}
			if( !misc_get_bit( info.thread_exit, RECORDER_INIT_CONDITION_DEVICE_CONFIG)
					&& ((time_get_now_stamp() - info.tick2 ) > MESSAGE_RESENT) ) {
					info.tick2 = time_get_now_stamp();
				/********message body********/
				msg_init(&msg);
				msg.message = MSG_DEVICE_GET_PARA;
				msg.sender = msg.receiver = SERVER_RECORDER;
				msg.arg_in.cat = DEVICE_CTRL_SD_INFO;
				ret = server_device_message(&msg);
				/****************************/;
			}
			if( !misc_get_bit( info.thread_exit, RECORDER_INIT_CONDITION_MIIO_TIME )
					&& ((time_get_now_stamp() - info.tick2 ) > MESSAGE_RESENT) ) {
					info.tick2 = time_get_now_stamp();
			    /********message body********/
				msg_init(&msg);
				msg.message = MSG_MIIO_PROPERTY_GET;
				msg.sender = msg.receiver = SERVER_RECORDER;
				msg.arg_in.cat = MIIO_PROPERTY_TIME_SYNC;
				server_miio_message(&msg);
				/****************************/
			}
			if( misc_full_bit( info.thread_exit, RECORDER_INIT_CONDITION_NUM ) )
				info.status = STATUS_WAIT;
			break;
		case STATUS_WAIT:
			info.status = STATUS_SETUP;
			break;
		case STATUS_SETUP:
			info.status = STATUS_IDLE;
			break;
		case STATUS_IDLE:
			if( config.profile.enable )
				info.status = STATUS_START;
			break;
		case STATUS_START:
			recorder_start_init_recorder_job();
			info.status = STATUS_RUN;
			break;
		case STATUS_RUN:
			if( recorder_main() ) info.status = STATUS_ERROR;
			break;
		case STATUS_STOP:
			info.status = STATUS_IDLE;
			break;
		case STATUS_ERROR:
			info.task.func = task_error;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_default = %d", info.status);
			break;
		}
	return;
}

/*
 * server entry point
 */
static void *server_func(void)
{
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
	misc_set_thread_name("server_recorder");
	pthread_detach(pthread_self());
	if( !message.init ) {
		msg_buffer_init(&message, MSG_BUFFER_OVERFLOW_NO);
	}
	//default task
	info.task.func = task_default;
	info.task.start = STATUS_NONE;
	info.task.end = STATUS_RUN;
	while( !info.exit ) {
		info.task.func();
		server_message_proc();
		heart_beat_proc();
	}
	if( info.exit ) {
		while( info.thread_start ) {
			log_qcy(DEBUG_INFO, "---------------locked recorder---- %d", info.thread_start);
		}
		server_release();
	    /********message body********/
		message_t msg;
		msg_init(&msg);
		msg.message = MSG_MANAGER_EXIT_ACK;
		msg.sender = SERVER_RECORDER;
		manager_message(&msg);
		/***************************/
	}
	log_qcy(DEBUG_SERIOUS, "-----------thread exit: server_recorder-----------");
	pthread_exit(0);
}

/*
 * internal interface
 */

/*
 * external interface
 */
int server_recorder_start(void)
{
	int ret=-1;
	info.exit = 0;	//!!!important for retain exiting.
	ret = pthread_create(&info.id, NULL, server_func, NULL);
	if(ret != 0) {
		log_qcy(DEBUG_SERIOUS, "recorder server create error! ret = %d",ret);
		 return ret;
	 }
	else {
		log_qcy(DEBUG_INFO, "recorder server create successful!");
		return 0;
	}
}

int server_recorder_message(message_t *msg)
{
	int ret=0,ret1;
	if( !message.init ) {
		log_qcy(DEBUG_SERIOUS, "recorder server is not ready for message processing!");
		return -1;
	}
	ret = pthread_rwlock_wrlock(&message.lock);
	if(ret)	{
		log_qcy(DEBUG_SERIOUS, "add message lock fail, ret = %d\n", ret);
		return ret;
	}
	ret = msg_buffer_push(&message, msg);
	log_qcy(DEBUG_VERBOSE, "push into the recorder message queue: sender=%d, message=%x, ret=%d, head=%d, tail=%d", msg->sender, msg->message, ret,
			message.head, message.tail);
	if( ret!=0 )
		log_qcy(DEBUG_WARNING, "message push in recorder error =%d", ret);
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1)
		log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d\n", ret1);
	return ret;
}

int server_recorder_video_message(message_t *msg)
{
	int ret = 0,ret1, j, id=-1;
	for(j=0;j<MAX_RECORDER_JOB;j++) {
		if( (msg->sender == SERVER_VIDEO) && (jobs[j].init.video_channel == 0)
				&& ( msg->arg_in.cat == jobs[j].init.type) ) {
			id = j;
			break;
		}
		if( (msg->sender == SERVER_VIDEO2) && (jobs[j].init.video_channel == 1)
				&& ( msg->arg_in.cat == jobs[j].init.type) ) {
			id = j;
			break;
		}
	}
	if( id==-1 ) {
		log_qcy(DEBUG_WARNING, "recording channel mismatch!");
		return -1;
	}
	if( (jobs[id].status != RECORDER_THREAD_STARTED) ||
			(!video_buff[id].init) ) {
		log_qcy(DEBUG_WARNING, "recorder video [ch=%d] is not ready for message processing!", id);
		return -1;
	}
	ret = pthread_rwlock_wrlock(&video_buff[id].lock);
	if(ret)	{
		log_qcy(DEBUG_SERIOUS, "add message lock fail, ret = %d\n", ret);
		return ret;
	}
	ret = msg_buffer_push(&video_buff[id], msg);
	if( ret!=0 )
		log_qcy(DEBUG_WARNING, "message push in recorder error =%d", ret);
	ret1 = pthread_rwlock_unlock(&video_buff[id].lock);
	if (ret1)
		log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d\n", ret1);
	return ret;
}

int server_recorder_audio_message(message_t *msg)
{
	int ret = 0,ret1, j, id=-1;
	for(j=0;j<MAX_RECORDER_JOB;j++) {
		if( msg->arg_in.cat == jobs[j].init.type ) {
			id = j;
			break;
		}
	}
	if( id==-1 ) {
		log_qcy(DEBUG_SERIOUS, "recording channel mismatch!");
		return -1;
	}
	if( (jobs[id].status != RECORDER_THREAD_STARTED) ||
			(!audio_buff[id].init) ) {
		log_qcy(DEBUG_INFO, "recorder audio [ch=%d] is not ready for message processing!", id);
		return -1;
	}
	ret = pthread_rwlock_wrlock(&audio_buff[id].lock);
	if(ret)	{
		log_qcy(DEBUG_SERIOUS, "add message lock fail, ret = %d\n", ret);
		return ret;
	}
	ret = msg_buffer_push(&audio_buff[id], msg);
	if( ret!=0 )
		log_qcy(DEBUG_WARNING, "message push in recorder error =%d", ret);
	ret1 = pthread_rwlock_unlock(&audio_buff[id].lock);
	if (ret1)
		log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d\n", ret1);
	return ret;
}
