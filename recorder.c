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
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <sys/mount.h>
#include <malloc.h>
#ifdef DMALLOC_ENABLE
#include <dmalloc.h>
#endif
//program header
#include "../../manager/manager_interface.h"
#include "../../server/realtek/realtek_interface.h"
#include "../../tools/tools_interface.h"
#include "../../server/recorder/recorder_interface.h"
#include "../../server/miio/miio_interface.h"
#include "../../server/audio/audio_interface.h"
#include "../../server/device/device_interface.h"
#include "../../server/micloud/micloud_interface.h"
#include "../../server/kernel/kernel_interface.h"
#include "../../server/video/video_interface.h"
#include "../../server/video2/video2_interface.h"
#include "../../server/video3/video3_interface.h"
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
static  pthread_rwlock_t		ilock = PTHREAD_RWLOCK_INITIALIZER;
static	pthread_mutex_t			mutex = PTHREAD_MUTEX_INITIALIZER;
static	pthread_cond_t			cond = PTHREAD_COND_INITIALIZER;
static	pthread_mutex_t			vmutex[MAX_RECORDER_JOB] = {PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER};
static	pthread_cond_t			vcond[MAX_RECORDER_JOB] = {PTHREAD_COND_INITIALIZER,PTHREAD_COND_INITIALIZER,PTHREAD_COND_INITIALIZER};
static 	char					hotplug;
static  int            			device_umount_flag = 0;

//function
//common
static void *server_func(void);
static int server_message_proc(void);
static void server_release_1(void);
static void server_release_2(void);
static void server_release_3(void);
static void task_default(void);
static void task_add_job(voide);
static void task_exit(void);
static void server_thread_termination(int sign);
//specific
static int recorder_add_job( message_t* msg );
static int count_job_number(void);
static int *recorder_func(void *arg);
static int recorder_thread_init_mp4v2( recorder_job_t *ctrl);
static int recorder_thread_write_mp4_video( recorder_job_t *ctrl, unsigned char* data, int length, av_data_info_t *info );
static int recorder_thread_close( recorder_job_t *ctrl );
static int recorder_thread_check_finish( recorder_job_t *ctrl );
static int recorder_thread_error( recorder_job_t *ctrl);
static int recorder_thread_pause( recorder_job_t *ctrl);
static int recorder_thread_destroy( recorder_job_t *ctrl );
static int recorder_thread_start_stream( recorder_job_t *ctrl );
static int recorder_thread_stop_stream( recorder_job_t *ctrl, int real_stop);
static int recorder_thread_check_and_exit_stream( recorder_job_t *ctrl, int real_stop);
static int recorder_start_init_recorder_job(void);
static int recorder_get_property(message_t *msg);
static int recorder_set_property(message_t *msg);
static int recorder_quit_all(int id);
static void recorder_broadcast_session_exit(void);
/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * helper
 */
static int recorder_check_sd(void)
{
	if( access("/mnt/media/normal", F_OK) ) {
		recorder_quit_all(-1);
		log_qcy(DEBUG_INFO, "SD card access failed!, quit all!----");
		return -1;
	}
	else
		return 0;
}

static int recorder_clean_disk(void)
{
	struct dirent **namelist;
	int 	n;
	char 	path[MAX_SYSTEM_STRING_SIZE*4];
	char 	name[MAX_SYSTEM_STRING_SIZE*4];
	char 	thisdate[MAX_SYSTEM_STRING_SIZE];
	unsigned long long int cutoff_date;
	char 	*p = NULL;
	unsigned long long int start, block_time, now;
	int		i = 0;
	int		deleted = 0;
	message_t msg;
	//***
	/****************************/
	msg_init(&msg);
	msg.message = MSG_RECORDER_CLEAN_DISK_START;
	msg.sender = msg.receiver = SERVER_RECORDER;
	manager_common_send_message(SERVER_PLAYER, &msg);
	/****************************/
	now = time_get_now_stamp();
	block_time = 0;
	memset(thisdate, 0, sizeof(thisdate));
	time_stamp_to_date_with_zone(now, thisdate, 80, _config_.timezone);
	log_qcy(DEBUG_INFO, "----------current time is UTC+8 %s-------", thisdate);
restart:
	log_qcy(DEBUG_INFO, "----------start sd cleanning job-------");
	cutoff_date = now - 1 * 86400 + block_time;
	memset(thisdate, 0, sizeof(thisdate));
	time_stamp_to_date_with_zone(cutoff_date, thisdate, 80, _config_.timezone);
	log_qcy(DEBUG_INFO, "----------delete media file before UTC+8 %s-------", thisdate);
	while( i < 3 ) {
		memset(path, 0, sizeof(path));
		if( i == 0 ) {
			log_qcy(DEBUG_INFO, "----------inside NORMAL directory-------");
			sprintf(path, "%s%s/", config.profile.directory, config.profile.normal_prefix);
		}
		else if( i==1 ) {
			log_qcy(DEBUG_INFO, "----------inside MOTION directory-------");
			sprintf(path, "%s%s/", config.profile.directory, config.profile.motion_prefix);
		}
		else if( i==2 ) {
			log_qcy(DEBUG_INFO, "----------inside ALARM directory-------");
			sprintf(path, "%s%s/", config.profile.directory, config.profile.alarm_prefix);
		}
		n = scandir(path, &namelist, 0, alphasort);
		if(n < 0) {
			log_qcy(DEBUG_SERIOUS, "Open dir error %s", path);
		}
		else {
			int index=0;
			while(index < n) {
				if(strcmp(namelist[index]->d_name,".") == 0 ||
				   strcmp(namelist[index]->d_name,"..") == 0 )
					goto exit;
				if(namelist[index]->d_type == 10) goto exit;
				if(namelist[index]->d_type == 4) goto exit;
				if( (strstr(namelist[index]->d_name,".mp4") == NULL) &&
						(strstr(namelist[index]->d_name,".jpg") == NULL)	) {
					//remove file here.
/*					memset(name, 0, sizeof(name));
					sprintf(name, "%s%s", path, namelist[index]->d_name);
					if( !recorder_check_sd() ) {
						remove( name );
						deleted++;
					}
					else {
						free(namelist[index]);
						goto exit;
					}
					log_qcy(DEBUG_INFO, "---removed %s---", name);
*/
					goto exit;
				}
				p = NULL;
				p = strstr( namelist[index]->d_name, "202");	//first
				if(p){
					memset(name, 0, sizeof(name));
					memcpy(name, p, 14);
					start = time_date_to_stamp_with_zone( name, 80, _config_.timezone );
					if( start < cutoff_date) {
						//remove
						memset(name, 0, sizeof(name));
						sprintf(name, "%s%s", path, namelist[index]->d_name);
						if( !recorder_check_sd() ) {
							remove( name );
							log_qcy(DEBUG_VERBOSE, "---removed %s---", name);
							deleted++;
						}
						else {
							goto exit;
						}
					}
				}
			exit:
				free(namelist[index]);
				index++;
			}
		}
		free(namelist);
		namelist = NULL;
		i++;
	}
	if( deleted == 0 ) {
		block_time += 3600*4;	//4 hours step
		if( block_time < 86400 ) {
			i = 0;
			goto restart;
		}
	}
	if( deleted ) {
		log_qcy(DEBUG_INFO, "---removed %d files from sd!!!---", deleted);
	/****************************/
		msg_init(&msg);
		msg.message = MSG_RECORDER_CLEAN_DISK_STOP;
		msg.sender = msg.receiver = SERVER_RECORDER;
		manager_common_send_message(SERVER_PLAYER, &msg);
	/****************************/
	}
    return 0;
}

static int recorder_quit_all(int id)
{
	int ret = 0;
	int i;
	pthread_rwlock_wrlock(&ilock);		//remove this for fast response
	for( i=0; i<MAX_RECORDER_JOB; i++ ) {
		if( id !=-1 && i!=id ) continue;
		if(misc_get_bit(info.thread_start, i))
			misc_set_bit( &info.thread_exit, i, 1);
	}
	info.status2 = 0;
	recorder_broadcast_session_exit();
	info.status = STATUS_NONE;
	pthread_rwlock_unlock(&ilock);
	pthread_mutex_lock(&mutex);
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&mutex);
	return ret;
}

static int recorder_get_property(message_t *msg)
{
	int ret = 0;
	message_t send_msg;
    /********message body********/
	msg_init(&send_msg);
	memcpy(&(send_msg.arg_pass), &(msg->arg_pass),sizeof(message_arg_t));
	send_msg.message = msg->message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_RECORDER;
	send_msg.arg_in.cat = msg->arg_in.cat;
	/**************************/
	if( misc_get_bit( info.init_status, RECORDER_INIT_CONDITION_CONFIG)==0 )
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
	ret = manager_common_send_message(msg->receiver, &send_msg);
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
	send_msg.arg_in.wolf = 0;
	/**************************/
	if( msg->arg_in.cat == RECORDER_PROPERTY_SAVE_MODE ) {
		int temp = *((int*)(msg->arg));
		if( temp != config.profile.enable) {
			if( config.profile.enable == 1 ) {
				recorder_quit_all(-1);
				info.status = STATUS_WAIT;
			}
			else {
				info.status = STATUS_WAIT;
			}
			config.profile.enable = temp;
			log_qcy(DEBUG_SERIOUS, "changed the enable = %d", config.profile.enable);
			config_recorder_set(CONFIG_RECORDER_PROFILE, &config.profile);
			send_msg.result = 0;
			send_msg.arg_in.wolf = 1;
			send_msg.arg = &temp;
			send_msg.arg_size = sizeof(temp);
		}
	}
	else if( msg->arg_in.cat == RECORDER_PROPERTY_RECORDING_MODE ) {
		int temp = *((int*)(msg->arg));
		if( temp != config.profile.mode) {
			config.profile.mode = temp;
			log_qcy(DEBUG_SERIOUS, "changed the mode = %d", config.profile.mode);
			config_recorder_set(CONFIG_RECORDER_PROFILE, &config.profile);
			send_msg.result = 0;
			send_msg.arg_in.wolf = 1;
			send_msg.arg = &temp;
			send_msg.arg_size = sizeof(temp);
		}
	}
	ret = manager_common_send_message(msg->receiver, &send_msg);
	return ret;
}

static int recorder_start_init_recorder_job(void)
{
	message_t msg;
	recorder_init_t init;
	int ret=0;
	if( !info.status2 ) {
		msg_init(&msg);
		msg.message = MSG_RECORDER_ADD;
		msg.sender = msg.receiver = SERVER_RECORDER;
		init.video_channel = 0;
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
		ret = recorder_add_job( &msg );
		if( !ret ) {
			info.status2 = 1;
		}
	}
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
	msg_init(&msg);
	msg.sender = msg.receiver = SERVER_RECORDER;
	msg.arg_in.wolf = ctrl->t_id;
	msg.arg_pass.wolf = ctrl->t_id;
	if( ctrl->init.video_channel == 0)
		msg.message = MSG_VIDEO_START;
	else if( ctrl->init.video_channel == 1)
		msg.message = MSG_VIDEO2_START;
	if( ctrl->init.video_channel == 0) {
	    if( manager_common_send_message(SERVER_VIDEO, &msg)!=0 ) {
	    	log_qcy(DEBUG_SERIOUS, "video start failed from recorder!");
	    }
	}
	else if( ctrl->init.video_channel == 1) {
	    if( manager_common_send_message(SERVER_VIDEO2, &msg)!=0 ) {
	    	log_qcy(DEBUG_SERIOUS, "video2 start failed from recorder!");
	    }
	}
    if( ctrl->init.audio ) {
		msg.message = MSG_AUDIO_START;
		if( manager_common_send_message(SERVER_AUDIO, &msg)!=0 ) {
			log_qcy(DEBUG_SERIOUS, "audio start failed from recorder!");
		}
    }
    /****************************/
    msg.arg_in.wolf = VIDEO3_RUN_MODE_SNAP;
    msg.message = MSG_VIDEO3_START;
    manager_common_send_message(SERVER_VIDEO3, &msg);
    /****************************/
}

static int recorder_thread_stop_stream( recorder_job_t *ctrl, int real_stop )
{
	message_t msg;
    /********message body********/
	memset(&msg,0,sizeof(message_t));
	msg.arg_in.wolf = ctrl->t_id;
	msg.arg_in.duck = real_stop;
	if( ctrl->init.video_channel == 0)
		msg.message = MSG_VIDEO_STOP;
	else if( ctrl->init.video_channel == 1)
		msg.message = MSG_VIDEO2_STOP;
	msg.sender = msg.receiver = SERVER_RECORDER;
	if( ctrl->init.video_channel == 0) {
	    if( manager_common_send_message(SERVER_VIDEO, &msg)!=0 ) {
	    	log_qcy(DEBUG_WARNING, "video stop failed from recorder!");
	    }
	}
	else if( ctrl->init.video_channel == 1) {
	    if( manager_common_send_message(SERVER_VIDEO2, &msg)!=0 ) {
	    	log_qcy(DEBUG_WARNING, "video2 stop failed from recorder!");
	    }
	}
    if( ctrl->init.audio ) {
		memset(&msg,0,sizeof(message_t));
		msg.message = MSG_AUDIO_STOP;
		msg.sender = msg.receiver = SERVER_RECORDER;
		msg.arg_in.wolf = ctrl->t_id;
		msg.arg_in.duck = real_stop;
		if( manager_common_send_message(SERVER_AUDIO, &msg)!=0 ) {
			log_qcy(DEBUG_WARNING, "audio stop failed from recorder!");
		}
    }
    /****************************/
    msg.arg_in.wolf = 2;		//from video3.h for RUN_MODE_SNAP
    msg.arg_in.duck = real_stop;
    msg.message = MSG_VIDEO3_STOP;
    manager_common_send_message(SERVER_VIDEO3, &msg);
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
	char oldname[MAX_SYSTEM_STRING_SIZE*4];
	char snapname[MAX_SYSTEM_STRING_SIZE*4];
	char start[MAX_SYSTEM_STRING_SIZE*2];
	char stop[MAX_SYSTEM_STRING_SIZE*2];
	char prefix[MAX_SYSTEM_STRING_SIZE];
	char alltime[MAX_SYSTEM_STRING_SIZE*4];
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
	memset(oldname,0,sizeof(oldname));
	memset(snapname, 0, sizeof(snapname));
	strcpy(oldname, ctrl->run.file_path);
	sprintf(snapname, "%s%s", oldname, "-snap");
	if( (ctrl->run.last_write - ctrl->run.real_start) < ctrl->config.profile.min_length ) {
		log_qcy(DEBUG_WARNING, "Recording file %s is too short, removed!", ctrl->run.file_path);
		//remove file here.
		remove(ctrl->run.file_path);
		//remove snapshot file as well
		if( (access(snapname, F_OK)) == 0) {
			remove(snapname);
		}
		return -1;
	}
	ctrl->run.real_stop = ctrl->run.last_write;
	memset(start,0,sizeof(start));
	memset(stop,0,sizeof(stop));
	memset(prefix,0,sizeof(prefix));
	time_stamp_to_date_with_zone(ctrl->run.real_start, start, 80, _config_.timezone);
	time_stamp_to_date_with_zone(ctrl->run.last_write, stop, 80, _config_.timezone);
	if( ctrl->init.type == RECORDER_TYPE_NORMAL) {
		strcpy( &prefix, ctrl->config.profile.normal_prefix);
		sprintf( ctrl->run.file_path, "%s%s/%s-%s_%s.mp4",ctrl->config.profile.directory,prefix,prefix,start,stop);
		ret = rename(oldname, ctrl->run.file_path);
	}
	else if( ctrl->init.type == RECORDER_TYPE_MOTION_DETECTION) {
		strcpy( &prefix, ctrl->config.profile.motion_prefix);
		sprintf( ctrl->run.file_path, "%s.mp4", oldname);
		ret = rename(oldname, ctrl->run.file_path);
	}
	else if( ctrl->init.type == RECORDER_TYPE_ALARM) {
		strcpy( &prefix, ctrl->config.profile.alarm_prefix);
	}
	if(ret) {
		log_qcy(DEBUG_WARNING, "rename recording file %s to %s failed.\n", oldname, ctrl->run.file_path);
	}
	else {
		if( ctrl->init.type == RECORDER_TYPE_NORMAL) {
			/********message body********/
			msg_init(&msg);
			msg.message = MSG_RECORDER_ADD_FILE;
			msg.sender = msg.receiver = SERVER_RECORDER;
			msg.arg = &ctrl->run.real_start;
			msg.arg_size = sizeof(ctrl->run.real_start);
			msg.extra = &ctrl->run.last_write;
			msg.extra_size = sizeof(ctrl->run.last_write);
			ret = manager_common_send_message(SERVER_PLAYER, &msg);
		}
		/***************************/
		log_qcy(DEBUG_INFO, "Record file is %s\n", ctrl->run.file_path);
	}
	//snapshot
	if( ctrl->init.type == RECORDER_TYPE_NORMAL ) {
		if( (access(snapname, F_OK))== -1) {
			log_qcy(DEBUG_WARNING, "Can't find previously created snapshot %s", snapname);
		}
		else {
			memset(alltime, 0, sizeof(alltime));
//			sprintf( alltime, "%s%s/%s-%s_%s_f.jpg",ctrl->config.profile.directory,prefix,prefix,start,stop);
			sprintf( alltime, "%s%s/%s-%s_%s.jpg",ctrl->config.profile.directory,prefix,prefix,start,stop);
			ret = rename(snapname, alltime);
			if(ret) {
				log_qcy(DEBUG_WARNING, "rename recording snapshot file %s to %s failed.\n", snapname, alltime);
			}
			else {
				log_qcy(DEBUG_INFO, "Record snapshot file is %s\n", alltime);
			}
			/*
			else {
				log_qcy(DEBUG_INFO, "Record snapshot file is %s\n", alltime);
				msg_init(&msg);
				msg.message = MSG_VIDE0_SNAPSHOT_THUMB;
				msg.sender = msg.receiver = SERVER_RECORDER;
				msg.arg_in.cat = ctrl->init.type;
				msg.arg = alltime;
				msg.arg_size = strlen(alltime) + 1;
				ret = manager_common_send_message(SERVER_VIDEO, &msg);
			}
		*/
		}
	}
	return ret;
}

static int recorder_thread_write_mp4_video( recorder_job_t *ctrl, unsigned char *p_data, int data_length, av_data_info_t *info)
{
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
					if( !recorder_check_sd() ) {
						ctrl->run.video_track = MP4AddH264VideoTrack(ctrl->run.mp4_file, 90000,
								MP4_INVALID_DURATION /*90000 / info->fps*/,
								info->width,
								info->height,
								nalu.data[1], nalu.data[2], nalu.data[3], 3);
					}
					else {
						return -1;
					}
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
				if( !recorder_check_sd() ) {
					ret = MP4WriteSample(ctrl->run.mp4_file, ctrl->run.video_track, data, nlength,
						duration, 0, key);
				}
				else {
					free(data);
					return -1;
				}
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
	time_stamp_to_date_with_zone(ctrl->run.start, timestr, 80, _config_.timezone);
	sprintf(fname,"%s%s/%s-%s",ctrl->config.profile.directory,prefix,prefix,timestr);
	if( !recorder_check_sd() ) {
		ctrl->run.mp4_file = MP4CreateEx(fname,	0, 1, 1, 0, 0, 0, 0);
	}
	else {
		return -1;
	}
	if ( ctrl->run.mp4_file == MP4_INVALID_FILE_HANDLE) {
		printf("MP4CreateEx file failed.\n");
		return -1;
	}
	MP4SetTimeScale( ctrl->run.mp4_file, 90000);
	if( ctrl->init.audio ) {
		if( !recorder_check_sd() ) {
			ctrl->run.audio_track = MP4AddALawAudioTrack( ctrl->run.mp4_file,
				ctrl->config.profile.quality[ctrl->init.quality].audio_sample);
		}
		else {
			return -1;
		}
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
	//snapshot
	if( ctrl->init.type == RECORDER_TYPE_NORMAL ) {
		memset( fname, 0, sizeof(fname));
		sprintf(fname,"%s%s/%s-%s-snap",ctrl->config.profile.directory,prefix,prefix,timestr);
		/**********************************************/
		message_t msg;
		msg_init(&msg);
		msg.sender = msg.receiver = SERVER_RECORDER;
		msg.arg_in.cat = 0;
		msg.arg_in.dog = 1;
		msg.arg_in.duck = 0;
		msg.arg_in.tiger = RTS_AV_CB_TYPE_ASYNC;
		msg.arg_in.chick = ctrl->init.type;
		msg.arg = fname;
		msg.arg_size = strlen(fname) + 1;
		msg.message = MSG_VIDEO3_SNAPSHOT;
		manager_common_send_message(SERVER_VIDEO3, &msg);
		/**********************************************/
	}
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
		if((temp1 + ctrl->init.repeat_interval) % 60 != 0)
		{
			temp2 = ctrl->config.profile.max_length - ((temp1 + ctrl->init.repeat_interval)%ctrl->config.profile.max_length);
		}else
		{
			temp2 = config.profile.max_length;
		}
		//temp2 = ctrl->run.stop - ctrl->run.start;
		memset( &ctrl->run, 0, sizeof( recorder_run_t));
		if( temp1 == 0)
			temp1 = time_get_now_stamp();
		ctrl->run.start = temp1 + ctrl->init.repeat_interval;
		ctrl->run.stop = ctrl->run.start + temp2;
		ctrl->status = RECORDER_THREAD_STARTED;
		log_qcy(DEBUG_SERIOUS, "-------------add recursive recorder---------------------");
		log_qcy(DEBUG_SERIOUS, "now=%ld", time_get_now_stamp());
		log_qcy(DEBUG_SERIOUS, "start=%ld", ctrl->run.start);
		log_qcy(DEBUG_SERIOUS, "end=%ld", ctrl->run.stop);
		log_qcy(DEBUG_SERIOUS, "--------------------------------------------------");
	}
	if( time_get_now_stamp() <= (ctrl->run.start - MAX_BETWEEN_RECODER_PAUSE) ) {
		recorder_thread_check_and_exit_stream(ctrl, 1);
	}
	else {
		recorder_thread_check_and_exit_stream(ctrl, 0);
	}
	return ret;
}

static int recorder_thread_run( recorder_job_t *ctrl)
{
	message_t		vmsg, amsg;
	int 			ret_video = 1, 	ret_audio = 1, ret;
	av_packet_t 	*packet;
	unsigned char	*p;
	av_data_info_t	*info;
	MP4Duration 	duration, offset;
	//condition
	pthread_mutex_lock(&vmutex[ctrl->t_id]);
	if( (video_buff[ctrl->t_id].head == video_buff[ctrl->t_id].tail) &&
			(audio_buff[ctrl->t_id].head == audio_buff[ctrl->t_id].tail) ) {
		pthread_cond_wait(&vcond[ctrl->t_id], &vmutex[ctrl->t_id]);
	}
	msg_init(&vmsg);
	ret_video = msg_buffer_pop(&video_buff[ctrl->t_id], &vmsg);
    //read audio frame
	msg_init(&amsg);
	ret_audio = msg_buffer_pop(&audio_buff[ctrl->t_id], &amsg);
	pthread_mutex_unlock(&vmutex[ctrl->t_id]);
	if ( !ret_audio ) {
		if( _config_.memory_mode == MEMORY_MODE_SHARED ) {
			packet = (av_packet_t*)(amsg.arg);
			p = (unsigned char*)packet->data;
			info = (av_data_info_t*)(&packet->info);
			pthread_rwlock_wrlock(packet->lock);
			if( ( (*(packet->init))==0 ) ) {
				av_packet_sub(packet);
				pthread_rwlock_unlock(packet->lock);
				return ERR_NO_DATA;
			}
			if( p == NULL ) {
				pthread_rwlock_unlock(packet->lock);
				return ERR_NO_DATA;
			}
		}
		else {
			p = (unsigned char*)(amsg.arg);
			info = (av_data_info_t*)(amsg.extra);
		}
		if( ctrl->run.first_frame ) {
			if( ctrl->run.first_audio ) {
				duration 	= ( info->timestamp - ctrl->run.last_aframe_stamp) * 90000 / 1000;
				offset 		= ( info->timestamp - ctrl->run.first_frame_stamp) * 90000 / 1000;
			}
			else {
				duration	= 32 * 90000 / 1000;	//8k mono channel
				offset		= 0;
				ctrl->run.first_audio = 1;
			}
			if( !recorder_check_sd() ) {
				ret = MP4WriteSample( ctrl->run.mp4_file, ctrl->run.audio_track,
						p, info->size ,
						256, 0, 1);
			}
			else {
				if( _config_.memory_mode == MEMORY_MODE_SHARED) {
					av_packet_sub(packet);
					pthread_rwlock_unlock(packet->lock);
				}
				ret = ERR_ERROR;
				log_qcy(DEBUG_WARNING, "SD protection happened!---audio write aborted!\n");
				goto close_exit;
			}
			if( !ret ) {
				if( _config_.memory_mode == MEMORY_MODE_SHARED) {
					av_packet_sub(packet);
					pthread_rwlock_unlock(packet->lock);
				}
				ret = ERR_ERROR;
				log_qcy(DEBUG_WARNING, "MP4WriteSample audio failed.\n");
				goto close_exit;
			}
			ctrl->run.last_aframe_stamp = info->timestamp;
		}
		if( _config_.memory_mode == MEMORY_MODE_SHARED) {
			av_packet_sub(packet);
			pthread_rwlock_unlock(packet->lock);
		}
	}
	if( !ret_video ) {
		if( _config_.memory_mode == MEMORY_MODE_SHARED) {
			packet = (av_packet_t*)(vmsg.arg);
			p = (unsigned char*)packet->data;
			info = (av_data_info_t*)(&packet->info);
			pthread_rwlock_wrlock(packet->lock);
			if( ( *(packet->init)==0  ) ) {
				av_packet_sub(packet);
				pthread_rwlock_unlock(packet->lock);
				return ERR_NO_DATA;
			}
			if( packet->data == NULL ) {
				pthread_rwlock_unlock(packet->lock);
				return ERR_NO_DATA;
			}
		}
		else {
			p = (unsigned char*)(vmsg.arg);
			info = (av_data_info_t*)(vmsg.extra);
		}
		if( ctrl->run.first_frame && info->fps != ctrl->run.fps) {
			log_qcy(DEBUG_SERIOUS, "the video fps has changed, stop recording!");
			ret = ERR_ERROR;
			if( _config_.memory_mode == MEMORY_MODE_SHARED) {
				av_packet_sub(packet);
				pthread_rwlock_unlock(packet->lock);
			}
			goto close_exit;
		}
		if(  ctrl->run.first_frame && (info->width != ctrl->run.width || info->height != ctrl->run.height) ) {
			log_qcy(DEBUG_SERIOUS, "the video dimention has changed, stop recording!");
			ret = ERR_ERROR;
			if( _config_.memory_mode == MEMORY_MODE_SHARED) {
				av_packet_sub(packet);
				pthread_rwlock_unlock(packet->lock);
			}
			goto close_exit;
		}
		ret = recorder_thread_write_mp4_video( ctrl, p, info->size, info);
		if(ret ==-1 ) {
			log_qcy(DEBUG_WARNING, "MP4WriteSample video failed.\n");
			ret = ERR_NO_DATA;
			if( _config_.memory_mode == MEMORY_MODE_SHARED) {
				av_packet_sub(packet);
				pthread_rwlock_unlock(packet->lock);
			}
			goto exit;
		}
		if( recorder_thread_check_finish(ctrl) ) {
			log_qcy(DEBUG_INFO, "------------stop=%d------------", time_get_now_stamp());
			log_qcy(DEBUG_INFO, "recording finished!");
			if( _config_.memory_mode == MEMORY_MODE_SHARED) {
				av_packet_sub(packet);
				pthread_rwlock_unlock(packet->lock);
			}
			goto close_exit;
		}
		if( _config_.memory_mode == MEMORY_MODE_SHARED) {
			av_packet_sub(packet);
			pthread_rwlock_unlock(packet->lock);
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
	return ret;
}

static int *recorder_func(void *arg)
{
	recorder_job_t ctrl;
	int i, finish_bit;
	message_t send_msg;
	char fname[MAX_SYSTEM_STRING_SIZE];
	memcpy(&ctrl, (recorder_job_t*)arg, sizeof(recorder_job_t));
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
    sprintf(fname, "%d%d-%d",ctrl.t_id,ctrl.init.video_channel, time_get_now_stamp());
    misc_set_thread_name(fname);
    pthread_detach(pthread_self());
	msg_buffer_init2(&video_buff[ctrl.t_id], _config_.recorder_avmsg_overrun, &vmutex[ctrl.t_id]);
	msg_buffer_init2(&audio_buff[ctrl.t_id], _config_.recorder_avmsg_overrun, &vmutex[ctrl.t_id]);
	if (ctrl.init.start[0] == '0')
		ctrl.run.start = time_get_now_stamp();
	else {
		ctrl.run.start = time_date_to_stamp(ctrl.init.start);// - _config_.timezone * 3600;
	}
	if (ctrl.init.stop[0] == '0')
		ctrl.run.stop = 60 - (ctrl.run.start % 60) + ctrl.run.start;//ctrl.run.start + ctrl.config.profile.max_length;
	else {
		ctrl.run.stop = time_date_to_stamp( ctrl.init.stop);// - _config_.timezone * 3600;
	}
	if( (ctrl.run.stop - ctrl.run.start) < ctrl.config.profile.min_length ||
			(ctrl.run.stop - ctrl.run.start) > ctrl.config.profile.max_length )
		ctrl.run.stop = ctrl.run.start + ctrl.config.profile.max_length;
	log_qcy(DEBUG_INFO, "-------------add new recorder---------------------");
	log_qcy(DEBUG_INFO, "now=%ld", time_get_now_stamp());
	log_qcy(DEBUG_INFO, "start=%ld", ctrl.run.start);
	log_qcy(DEBUG_INFO, "end=%ld", ctrl.run.stop);
	log_qcy(DEBUG_INFO, "recorder channel=%d", ctrl.t_id);
	log_qcy(DEBUG_INFO, "--------------------------------------------------");
    ctrl.status = RECORDER_THREAD_STARTED;
    while( 1 ) {
    	if( info.exit ) break;
    	if( ctrl.run.exit ) break;
    	if( misc_get_bit(info.thread_exit, ctrl.t_id) ) break;
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
    	usleep(1000);
    }
    //release
    recorder_thread_destroy(&ctrl);
    manager_common_send_dummy(SERVER_RECORDER);
    log_qcy(DEBUG_INFO, "-----------thread exit: record %s-----------", fname);
    if(device_umount_flag){
       finish_bit = 0;
       for( i=0; i<MAX_RECORDER_JOB; i++ ) {
    	   finish_bit |= misc_get_bit(info.thread_exit, i);
       }
       log_qcy(DEBUG_INFO, "-----------thread exit: finish_bit %d-----------", finish_bit);
       if(!finish_bit){
		   msg_init(&send_msg);
		   send_msg.sender = send_msg.receiver = SERVER_RECORDER;
		   send_msg.message = MSG_DEVICE_ACTION;
		   send_msg.arg_in.cat = DEVICE_ACTION_SD_EJECTED_ACK;
		   server_device_message(&send_msg);
		   device_umount_flag = 0;
       }
    }
    pthread_exit(0);
}

static int recorder_thread_check_and_exit_stream( recorder_job_t *ctrl, int real_stop)
{
	int ret=0;
	ret = recorder_thread_stop_stream( ctrl, real_stop);
	return ret;
}

static int recorder_thread_destroy( recorder_job_t *ctrl )
{
	int ret=0,ret1;
	message_t msg;
	ret = recorder_thread_close( ctrl );
	msg_buffer_release2(&video_buff[ctrl->t_id], &vmutex[ctrl->t_id]);
	msg_buffer_release2(&audio_buff[ctrl->t_id], &vmutex[ctrl->t_id]);
	recorder_thread_stop_stream( ctrl, 1);

	pthread_rwlock_wrlock(&ilock);
	misc_set_bit( &info.thread_start, ctrl->t_id, 0);
	misc_set_bit( &info.thread_exit,ctrl->t_id, 0);
	memset(&jobs[ctrl->t_id], 0, sizeof(recorder_job_t));
	pthread_rwlock_unlock(&ilock);
	return ret;
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
	pthread_rwlock_wrlock(&ilock);
	if( count_job_number() == MAX_RECORDER_JOB) {
		send_msg.result = -1;
		ret = manager_common_send_message(msg->receiver, &send_msg);
		pthread_rwlock_unlock(&ilock);
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
				misc_set_bit(&info.thread_start, i, 1);
				misc_set_bit(&info.thread_exit, i, 0);
				log_qcy(DEBUG_INFO, "recorder thread create successful!");
				jobs[i].status = RECORDER_THREAD_STARTED;
			}
			break;
		}
	}
	pthread_rwlock_unlock(&ilock);
	send_msg.result = 0;
	ret = manager_common_send_message(msg->receiver, &send_msg);
	return ret;
}

static void server_thread_termination(int sign)
{
    /********message body********/
	message_t msg;
	msg_init(&msg);
	msg.message = MSG_RECORDER_SIGINT;
	msg.sender = msg.receiver = SERVER_RECORDER;
	manager_common_send_message(SERVER_MANAGER, &msg);
	/****************************/
}

static void recorder_broadcast_session_exit(void)
{
	for(int i=0;i<MAX_RECORDER_JOB;i++) {
		pthread_mutex_lock(&vmutex[i]);
		pthread_cond_signal(&vcond[i]);
		pthread_mutex_unlock(&vmutex[i]);
	}
}

static void server_release_1(void)
{
	recorder_broadcast_session_exit();
}

static void server_release_2(void)
{
	msg_buffer_release2(&message, &mutex);
	memset(&config,0,sizeof(recorder_config_t));
	memset(&jobs,0,sizeof(recorder_job_t));
}

static void server_release_3(void)
{
	msg_free(&info.task.msg);
	memset(&info, 0, sizeof(server_info_t));
}

/*
 *
 *
 */
static int recorder_message_filter(message_t  *msg)
{
	int ret = 0;
	if( info.task.func == task_exit) { //only system message
		if( !msg_is_system(msg->message) && !msg_is_response(msg->message) )
			return 1;
	}
	return ret;
}

static int server_message_proc(void)
{
	int ret = 0;
	int i;
	int finish_flag = 0;
	message_t msg;
	message_t send_msg;
	//condition
	pthread_mutex_lock(&mutex);
	if( message.head == message.tail ) {
		if( (info.status == info.old_status ) ) {
			pthread_cond_wait(&cond,&mutex);
		}
	}
	if( info.msg_lock ) {
		pthread_mutex_unlock(&mutex);
		return 0;
	}
	msg_init(&msg);
	msg_init(&send_msg);
	ret = msg_buffer_pop(&message, &msg);
	pthread_mutex_unlock(&mutex);
	if( ret == 1)
		return 0;
	if( recorder_message_filter(&msg) ) {
		msg_free(&msg);
		log_qcy(DEBUG_VERBOSE, "RECORDER message--- sender=%d, message=%x, ret=%d, head=%d, tail=%d was screened, the current task is %p", msg.sender, msg.message,
				ret, message.head, message.tail, info.task.func);
		return -1;
	}
	log_qcy(DEBUG_VERBOSE, "-----pop out from the RECORDER message queue: sender=%d, message=%x, ret=%d, head=%d, tail=%d", msg.sender, msg.message,
			ret, message.head, message.tail);
	switch(msg.message) {
		case MSG_RECORDER_ADD:
			msg_init(&info.task.msg);
			msg_deep_copy(&info.task.msg, &msg);
			info.task.func = task_add_job;
			if(info.status2)
				info.msg_lock = 1;
			break;
		case MSG_RECORDER_ADD_ACK:
			break;
		case MSG_MANAGER_EXIT:
			msg_init(&info.task.msg);
			msg_copy(&info.task.msg, &msg);
			info.task.func = task_exit;
			info.status = EXIT_INIT;
			info.msg_lock = 0;
			break;
		case MSG_MANAGER_TIMER_ACK:
			((HANDLER)msg.arg_in.handler)();
			break;
		case MSG_DEVICE_GET_PARA_ACK: {
			device_iot_config_t dev_iot;
			char fname[MAX_SYSTEM_STRING_SIZE*4];
			if( !msg.result ) {
				memcpy(&dev_iot, msg.arg, sizeof(device_iot_config_t));
				if( dev_iot.sd_iot_info.plug == SD_STATUS_PLUG ) {
					memset(fname, 0, sizeof(fname));
					sprintf(fname, "%s%s", config.profile.directory, config.profile.normal_prefix);
					if( access(fname, F_OK) )
						mkdir(fname,0777);
					memset(fname, 0, sizeof(fname));
					sprintf(fname, "%s%s", config.profile.directory, config.profile.motion_prefix);
					if( access(fname, F_OK) )
						mkdir(fname,0777);
					memset(fname, 0, sizeof(fname));
					sprintf(fname, "%s%s", config.profile.directory, config.profile.alarm_prefix);
					if( access(fname, F_OK) )
						mkdir(fname,0777);
					misc_set_bit( &info.init_status, RECORDER_INIT_CONDITION_DEVICE_CONFIG, 1);
					hotplug = 0;
				}
			}
			break;
		}
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
					misc_set_bit( &info.init_status, RECORDER_INIT_CONDITION_MIIO_TIME, 1);
			}
			break;
		case MSG_MANAGER_EXIT_ACK:
			misc_set_bit(&info.error, msg.sender, 0);
			break;
		case MSG_MANAGER_DUMMY:
			break;
		case MSG_DEVICE_ACTION:
			if(msg.arg_in.cat == DEVICE_ACTION_SD_CAP_ALARM) {
				recorder_clean_disk();
			}
			else if(msg.arg_in.cat == DEVICE_ACTION_SD_EJECTED){
				recorder_quit_all(-1);
				misc_set_bit( &info.init_status, RECORDER_INIT_CONDITION_DEVICE_CONFIG, 0);
				info.status = STATUS_NONE;
				info.status2 = 0;
				info.tick = 10;
				if(msg.arg_in.wolf) {
					for( i=0; i<MAX_RECORDER_JOB; i++ ) {
						finish_flag |= misc_get_bit(info.thread_exit, i);
					}
					if(!finish_flag) {
					   send_msg.sender = send_msg.receiver = SERVER_RECORDER;
					   send_msg.message = MSG_DEVICE_ACTION;
					   send_msg.arg_in.cat = DEVICE_ACTION_SD_EJECTED_ACK;
					   server_device_message(&send_msg);
					}
					else {
						device_umount_flag = 1;
					}
				}
			}
			else if(msg.arg_in.cat == DEVICE_ACTION_SD_INSERT) {
				hotplug = 0;
				info.tick = 0;	//restart the device check
			}
 			break;
		case MSG_KERNEL_TIMEZONE_CHANGE:
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "not processed message = %x", msg.message);
			break;
	}
	msg_free(&msg);
	return ret;
}

/*
 *
 */
static int server_none(void)
{
	int ret = 0;
	message_t msg;
	if( !misc_get_bit( info.init_status, RECORDER_INIT_CONDITION_CONFIG ) ) {
		ret = config_recorder_read(&config);
		if( !ret && misc_full_bit(config.status, CONFIG_RECORDER_MODULE_NUM) ) {
			misc_set_bit(&info.init_status, RECORDER_INIT_CONDITION_CONFIG, 1);
		}
		else {
			info.status = STATUS_ERROR;
			return -1;
		}
	}
	if( !misc_get_bit( info.init_status, RECORDER_INIT_CONDITION_DEVICE_CONFIG) && !device_umount_flag ) {
		if( info.tick < MESSAGE_RESENT) {
			/********message body********/
			msg_init(&msg);
			msg.message = MSG_DEVICE_GET_PARA;
			msg.sender = msg.receiver = SERVER_RECORDER;
			msg.arg_in.cat = DEVICE_CTRL_SD_INFO;
			ret = manager_common_send_message(SERVER_DEVICE, &msg);
			/***************************/
			info.tick++;
		}
		usleep(MESSAGE_RESENT_SLEEP);
	}
	if( !misc_get_bit( info.init_status, RECORDER_INIT_CONDITION_MIIO_TIME)) {
		/********message body********/
		msg_init(&msg);
		msg.message = MSG_MIIO_PROPERTY_GET;
		msg.sender = msg.receiver = SERVER_RECORDER;
		msg.arg_in.cat = MIIO_PROPERTY_TIME_SYNC;
		ret = manager_common_send_message(SERVER_MIIO, &msg);
		/***************************/
		usleep(MESSAGE_RESENT_SLEEP);
	}
	if( misc_full_bit( info.init_status, RECORDER_INIT_CONDITION_NUM ) ) {
		info.status = STATUS_WAIT;
		info.tick = 0;
//		recorder_init_hotplug_socket();
	}
	return ret;
}
/*
 * task
 */
static void task_add_job(void)
{
	message_t msg;
	switch( info.status){
		case STATUS_NONE:
			server_none();
			break;
		case STATUS_WAIT:
		case STATUS_SETUP:
		case STATUS_IDLE:
		case STATUS_START:
		case STATUS_RUN:
			if( config.profile.enable ) {
				recorder_start_init_recorder_job();
				recorder_add_job( &info.task.msg );
				/**************************/
				msg_init(&msg);
				memcpy(&(msg.arg_pass), &(info.task.msg.arg_pass),sizeof(message_arg_t));
				msg.message = info.task.msg.message | 0x1000;
				msg.sender = msg.receiver = SERVER_RECORDER;
				msg.result = 0;
				/***************************/
				goto exit;
			}
			else {
				/**************************/
				msg_init(&msg);
				memcpy(&(msg.arg_pass), &(info.task.msg.arg_pass),sizeof(message_arg_t));
				msg.message = info.task.msg.message | 0x1000;
				msg.sender = msg.receiver = SERVER_RECORDER;
				msg.result = -1;
				/***************************/
				goto exit;
			}
			break;
		case STATUS_ERROR:
			/**************************/
			msg_init(&msg);
			memcpy(&(msg.arg_pass), &(info.task.msg.arg_pass),sizeof(message_arg_t));
			msg.message = info.task.msg.message | 0x1000;
			msg.sender = msg.receiver = SERVER_RECORDER;
			msg.result = -1;
			/***************************/
			goto exit;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_default = %d", info.status);
			break;
		}
	return;
exit:
	manager_common_send_message(info.task.msg.receiver, &msg);
	msg_free(&info.task.msg);
	info.task.func = &task_default;
	info.msg_lock = 0;
	return;
}

/*
 * default task: none->run
 */
static void task_default(void)
{
	switch( info.status){
		case STATUS_NONE:
			server_none();
			break;
		case STATUS_WAIT:
			info.status = STATUS_SETUP;
			break;
		case STATUS_SETUP:
			if( config.profile.enable ) {
				recorder_start_init_recorder_job();
			}
			info.status = STATUS_IDLE;
			break;
		case STATUS_IDLE:
			info.status = STATUS_RUN;
			break;
		case STATUS_RUN:
			break;
		case STATUS_ERROR:
			info.task.func = task_exit;
			info.status = EXIT_INIT;
			info.msg_lock = 0;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_default = %d", info.status);
			break;
		}
	return;
}

/*
 * default exit: *->exit
 */
static void task_exit(void)
{
	switch( info.status ){
		case EXIT_INIT:
			log_qcy(DEBUG_INFO,"RECORDER: switch to exit task!");
			if( info.task.msg.sender == SERVER_MANAGER) {
				info.error = RECORDER_EXIT_CONDITION;
				info.error &= (info.task.msg.arg_in.cat);
			}
			else {
				info.error = 0;
			}
			info.status = EXIT_SERVER;
			break;
		case EXIT_SERVER:
			if( !info.error )
				info.status = EXIT_STAGE1;
			break;
		case EXIT_STAGE1:
			server_release_1();
			info.status = EXIT_THREAD;
			break;
		case EXIT_THREAD:
			info.thread_exit = info.thread_start;
			recorder_broadcast_session_exit();
			if( !info.thread_start )
				info.status = EXIT_STAGE2;
			break;
			break;
		case EXIT_STAGE2:
			server_release_2();
			info.status = EXIT_FINISH;
			break;
		case EXIT_FINISH:
			info.exit = 1;
		    /********message body********/
			message_t msg;
			msg_init(&msg);
			msg.message = MSG_MANAGER_EXIT_ACK;
			msg.sender = SERVER_RECORDER;
			manager_common_send_message(SERVER_MANAGER, &msg);
			/***************************/
			info.status = STATUS_NONE;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_exit = %d", info.status);
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
	msg_buffer_init2(&message, _config_.msg_overrun, &mutex);
	info.init = 1;
	//default task
	info.task.func = task_default;
	while( !info.exit ) {
		info.old_status = info.status;
		info.task.func();
		server_message_proc();
	}
	server_release_3();
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
	int ret=0;
	pthread_mutex_lock(&mutex);
	if( !message.init ) {
		log_qcy(DEBUG_SERIOUS, "recorder server is not ready for message processing!");
		pthread_mutex_unlock(&mutex);
		return -1;
	}
	ret = msg_buffer_push(&message, msg);
	log_qcy(DEBUG_VERBOSE, "push into the recorder message queue: sender=%d, message=%x, ret=%d, head=%d, tail=%d", msg->sender, msg->message, ret,
			message.head, message.tail);
	if( ret!=0 )
		log_qcy(DEBUG_WARNING, "message push in recorder error =%d", ret);
	else {
		pthread_cond_signal(&cond);
	}
	pthread_mutex_unlock(&mutex);
	return ret;
}

int server_recorder_video_message(message_t *msg)
{
	int ret = 0, id = -1;
	id = msg->arg_in.wolf;
	pthread_mutex_lock(&vmutex[id]);
	if( (!video_buff[id].init) ) {
		log_qcy(DEBUG_WARNING, "recorder video [ch=%d] is not ready for message processing!", id);
		pthread_mutex_unlock(&vmutex[id]);
		return -1;
	}
	ret = msg_buffer_push(&video_buff[id], msg);
	if( ret!=0 )
		log_qcy(DEBUG_WARNING, "message push in recorder video error =%d", ret);
	else {
		pthread_cond_signal(&vcond[id]);
	}
	pthread_mutex_unlock(&vmutex[id]);
	return ret;
}

int server_recorder_audio_message(message_t *msg)
{
	int ret = 0, id = -1;
	id = msg->arg_in.wolf;
	pthread_mutex_lock(&vmutex[id]);
	if( (!audio_buff[id].init) ) {
		log_qcy(DEBUG_WARNING, "recorder audio [ch=%d] is not ready for message processing!", id);
		pthread_mutex_unlock(&vmutex[id]);
		return -1;
	}
	ret = msg_buffer_push(&audio_buff[id], msg);
	if( ret!=0 )
		log_qcy(DEBUG_WARNING, "message push in recorder audio error =%d", ret);
	else {
		pthread_cond_signal(&vcond[id]);
	}
	pthread_mutex_unlock(&vmutex[id]);
	return ret;
}

void server_recorder_interrupt_routine(int param)
{
	if( param == 1) {
		info.msg_lock = 0;
		info.tick = 0;
		hotplug = 1;
		misc_set_bit( &info.init_status, RECORDER_INIT_CONDITION_DEVICE_CONFIG, 0);
		recorder_quit_all(-1);
		log_qcy(DEBUG_SERIOUS, "RECORDER: hotplug happened, recorder roll back to none state--------------");
	}
}
