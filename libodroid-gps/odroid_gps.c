/* 
 * Copyright (C) Ericsson AB 2009-2010
 * Copyright 2006, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Torgny Johansson <torgny.johansson@ericsson.com>
 */

#include <errno.h>
#include <pthread.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <math.h>
#include <time.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>

#define  LOG_TAG  "libmbm-gps"
#include <cutils/log.h>
#include <cutils/sockets.h>
#include <cutils/properties.h>
#include <hardware/gps.h>

#include "nmea_reader.h"
#include "version.h"

/* Just check this file */
#ifdef __GNUC__
#pragma GCC diagnostic warning "-pedantic"
#endif

#ifdef DEBUG
#  define  D(...)   ALOGD(__VA_ARGS__)
#else
#  define  D(...)   ((void)0)
#endif

#define INT_STATE_UNDEFINED 1
#define INT_STATE_FALLBACK_REQUIRED 2
#define INT_STATE_QUITTING 3

#define SUPLNI_VERIFY_ALLOW 0
#define SUPLNI_VERIFY_DENY 1
#define SUPLNI_NOTIFY 2
#define SUPLNI_NO_NOTIFY 3

#define ST_UNDEFINED        (0x0)    /* 0x00 */
#define ST_CONFIGURING      (1 << 0) /* 0x01 */
#define ST_CONFIGURED       (1 << 1) /* 0x02 */
#define ST_REGISTERED       (1 << 2) /* 0x04 */
#define ST_RUNNING      (1 << 3) /* 0x08 */
#define ST_FAILED       (1 << 4) /* 16 */
#define ST_STOPPING     (1 << 5) /* 32 */
#define ST_STOPPED      (1 << 6) /* 64 */

struct gps_state{
    int device_state;
    NmeaReader reader[1];
    gps_status_callback status_callback;
    gps_create_thread create_thread_callback;
    int have_supl_apn;
    GpsStatus gps_status;

    int int_state;
    int ctrl_state;

    int control_fd[2];

    pthread_t main_thread;
    pthread_t thread;

    int fd;
};

struct gps_state state;

static void set_pending_command (char cmd);
static void stop_gps ();
static void start_gps ();
static void nmea_received (void *line, void *data);
static void main_loop (void *arg);
static void* gps_state_thread( void*  arg );

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       M A I N  I N T E R F A C E                      *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/
static int jul_days(struct tm tm_day)
{
    return
	367 * (tm_day.tm_year + 1900) -
	floor(7 *
	      (floor((tm_day.tm_mon + 10) / 12) +
	       (tm_day.tm_year + 1900)) / 4) -
	floor(3 *
	      (floor
	       ((floor((tm_day.tm_mon + 10) / 12) +
		 (tm_day.tm_year + 1899)) / 100) + 1) / 4) +
	floor(275 * (tm_day.tm_mon + 1) / 9) + tm_day.tm_mday + 1721028 -
	2400000;
}

static void utc_to_gps(const time_t time, int *tow, int *week)
{
    struct tm tm_utc;
    struct tm tm_gps;
    int day, days_cnt;

    if (tow == NULL || week == NULL) {
	    ALOGE("%s: tow/week null", __FUNCTION__);
	    return;
    }
    gmtime_r(&time, &tm_utc);
    tm_gps.tm_year = 80;
    tm_gps.tm_mon = 0;
    tm_gps.tm_mday = 6;

    days_cnt = jul_days(tm_utc) - jul_days(tm_gps);
    day = days_cnt % 7;
    *week = floor(days_cnt / 7);
    *tow = (day * 86400) + ((tm_utc.tm_hour * 60) + 
		    tm_utc.tm_min) * 60 +
		    tm_utc.tm_sec;
}

static void set_pending_command (char cmd) {
    write(state.control_fd[0], &cmd, 1);
}

static void nmea_received(void *line, void *data)
{
    if (line == NULL) {
        ALOGE("%s: line null", __FUNCTION__);
        return;
    }

    //D("%s: %s", __FUNCTION__, (char *) line);

    nmea_reader_add(state.reader, (char *) line);
}

static void start_gps ()
{
    int ret;

    D("%s: enter", __FUNCTION__);

    if (state.ctrl_state == ST_STOPPED ||
        state.ctrl_state == ST_FAILED ||
	    state.ctrl_state == ST_UNDEFINED) {
        state.gps_status.status = GPS_STATUS_SESSION_BEGIN;

#ifdef EXTERNAL_GPS
        state.fd = open("/dev/ttyACM0", O_RDONLY); 
		if (state.fd < 0)
        	state.fd = open("/dev/ttyUSB0", O_RDONLY); 
#else
        state.fd = open("/dev/ttySAC2", O_RDONLY); 
#endif

        if (state.fd < 0)
            return;

        set_pending_command(CMD_STATUS_CB);

        if (pthread_create( &state.thread, NULL, gps_state_thread, &state) != 0) {
            ALOGE("could not create gps thread: %s", strerror(errno));
        }

    } else
        D("Stop the GPS before starting");

    // disable echo on serial lines
    if ( isatty( state.fd ) ) {
        struct termios  ios;
        tcgetattr( state.fd, &ios );
        ios.c_lflag = 0;  					/* disable ECHO, ICANON, etc... */
        ios.c_oflag &= (~ONLCR); 			/* Stop \n -> \r\n translation on output */
        ios.c_iflag &= (~(ICRNL | INLCR)); 	/* Stop \r -> \n & \n -> \r translation on input */
        ios.c_iflag |= (IGNCR | IXOFF);  	/* Ignore \r & XON/XOFF on input */
        tcsetattr( state.fd, TCSANOW, &ios );
    }

    D("%s: exit", __FUNCTION__);
}

static void stop_gps ()
{
    int ret;

    D("%s: enter", __FUNCTION__);

    if (state.ctrl_state == ST_STOPPING || state.ctrl_state == ST_STOPPED)
        D("GPS is already stopped or stopping");
    else {
        close(state.fd);        
        state.fd = -1;
        state.gps_status.status = GPS_STATUS_SESSION_END;
        set_pending_command(CMD_STATUS_CB);
    }

    D("%s: exit", __FUNCTION__);
}

static int epoll_register(int epoll_fd, int fd)
{
    struct epoll_event ev;
    int ret, flags;

    /* important: make the fd non-blocking */
    flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    ev.events = EPOLLIN;
    ev.data.fd = fd;
    do {
	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    } while (ret < 0 && errno == EINTR);
    
    return ret;
}

static int epoll_deregister(int epoll_fd, int fd)
{
    int ret;
    
    do {
        ret = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    } while (ret < 0 && errno == EINTR);
    
    return ret;
}

/* this loop is needed to be able to run callbacks in
 * the correct thread, created with the create_thread callback
 */
static void main_loop (void *arg)
{
    int epoll_fd = epoll_create(1);
    int control_fd = state.control_fd[1];

    D("Starting main loop");

    epoll_register(epoll_fd, control_fd);

    for (;;) {
        struct epoll_event events[1];
        int nevents;

        nevents = epoll_wait(epoll_fd, events, 1, -1);
        if (nevents < 0 && errno != EINTR) {
            ALOGE("epoll_wait() unexpected error: %s", strerror(errno));
            continue;
        }

        if ((events[0].events & (EPOLLERR | EPOLLHUP)) != 0) {
            ALOGE("EPOLLERR or EPOLLHUP after epoll_wait()!");
            goto error;
        }

        if ((events[0].events & EPOLLIN) != 0) {
            int fd = events[0].data.fd;

            if (fd == control_fd) {
                char cmd = 255;
                int ret;

                do {
                    ret = read(fd, &cmd, 1);
                } while (ret < 0 && errno == EINTR);

		switch (cmd) {
		    D("%s cmd %d", __FUNCTION__, (int) cmd); 
		case CMD_STATUS_CB:
		   state.status_callback(&state.gps_status);
		   break;
		case CMD_AGPS_STATUS_CB:
		    break;
		case CMD_SV_STATUS_CB:
                    state.reader->sv_status_callback(&state.reader->sv_status);
                    break;
                case CMD_LOCATION_CB:
                    state.reader->callback(&state.reader->fix);
                    break;
                case CMD_NMEA_CB:
		    state.reader->nmea_callback(time(NULL)*1000,
			state.reader->in, state.reader->pos + 1);
                    break;
                case CMD_QUIT:
                    goto exit;
                    break;
                default:
                    break;
                }
            } else {
                ALOGE("epoll_wait() returned unkown fd %d ?", fd);
            }
        }
    }

error:
    ALOGE("main loop terminated unexpectedly!");
exit:
    epoll_deregister(epoll_fd, control_fd);
}

static void*
gps_state_thread( void*  arg )
{
    int         epoll_fd   = epoll_create(1);
    int         started    = 0;
    int         gps_fd     = 0;
    gps_fd = state.fd;

    // register control file descriptors for polling
    epoll_register( epoll_fd, gps_fd );

    D("gps thread running gps_fd = %d", gps_fd);
	
    for (;;) {
        struct epoll_event   events[1];
        int nevents;

        nevents = epoll_wait( epoll_fd, events, 1, -1 );
        if (nevents < 0) {
            if (errno != EINTR)
                ALOGE("epoll_wait() unexpected error: %s", strerror(errno));
            continue;
        }
        D("gps thread received %d events", nevents);
        if ((events[0].events & (EPOLLERR|EPOLLHUP)) != 0) {
            ALOGE("EPOLLERR or EPOLLHUP after epoll_wait() !?");
            goto Exit;
        }
        if ((events[0].events & EPOLLIN) != 0) {
            int  fd = events[0].data.fd;

            if (fd == gps_fd)
            {
                char  buff[512];
                int  nn, ret;
                
                D("gps fd event");
               
                do	{
                    ret = read(fd, buff, sizeof(buff));
                }	while(ret < 0 && errno == EINTR); 
                
                if (ret > 0)	{
                    for (nn = 0; nn < ret; nn++)	
                        nmea_reader_addc(&state.reader, buff[nn]);
                }


                D("gps fd event end");
            }
            else
            {
                ALOGE("epoll_wait() returned unkown fd %d ?", fd);
            }
        }
    }

Exit:
    return NULL;
}


static int odroid_gps_init(GpsCallbacks * callbacks)
{   
    int ret;
    char prop[PROPERTY_VALUE_MAX];

    if (callbacks == NULL) {
	    ALOGE("%s, callbacks null", __FUNCTION__);
	    return -1;
    }

    if(callbacks->set_capabilities_cb) {
        callbacks->set_capabilities_cb(GPS_CAPABILITY_SCHEDULING | GPS_CAPABILITY_MSB);
    }
    else {
	    ALOGE("%s capabilities_cb is null",  __FUNCTION__);
    }

    memset(&state, 0, sizeof(struct gps_state));
    state.int_state = INT_STATE_UNDEFINED;
    state.have_supl_apn = 0;
     
    if (property_get("mbm.gps.config.gps_ctrl", prop, "") == 0) {
        D("No gps ctrl device set, using the default instead.");
        snprintf(prop, PROPERTY_VALUE_MAX, "%s", "/dev/bus/usb/002/049");
    } else {
        D("Using gps ctrl device: %s", prop);
    }

    if (property_get("mbm.gps.config.gps_nmea", prop, "") == 0) {
        D("No gps nmea device set, using the default instead.");
        snprintf(prop, PROPERTY_VALUE_MAX, "%s", "/dev/ttyACM2");
    } else {
        D("Using gps nmea device: %s", prop);
    }

    nmea_reader_init(state.reader);

    state.control_fd[0] = -1;
    state.control_fd[1] = -1;
    if (socketpair(AF_LOCAL, SOCK_STREAM, 0, state.control_fd) < 0) {
        ALOGE("could not create thread control socket pair: %s",
             strerror(errno));
        return -1;
    }

    state.status_callback = callbacks->status_cb;

    state.create_thread_callback = callbacks->create_thread_cb;

    /* main thread must be started prior to setting nmea_reader callbacks */
    state.main_thread = state.create_thread_callback("odroid_main_thread",
		    main_loop, NULL);

    state.reader->set_pending_callback_cb = set_pending_command;
    nmea_reader_set_callbacks(state.reader, callbacks);

    D("%s: exit", __FUNCTION__);
    return 0;
}

static void odroid_gps_cleanup(void)
{
    int i;
    D("%s: enter", __FUNCTION__);

    i = 0;
    if (state.ctrl_state != ST_STOPPED && state.ctrl_state != ST_UNDEFINED) {
        while (state.ctrl_state != ST_STOPPED) {
            if (i > 4) {
                ALOGD ("Gps ctrl did not stop within 5 seconds. Continuing anyway.");
                break;
            }
            ALOGD ("Waiting for gps ctrl to stop");
            sleep (1);
            i++;
        }
    }

    set_pending_command(CMD_QUIT);

    close(state.control_fd[0]);
    state.control_fd[0] = -1;
    close(state.control_fd[1]);
    state.control_fd[1] = -1;

    ALOGD ("Cleaning up gps ctrl.");
    D("%s: exit", __FUNCTION__);
}


static int odroid_gps_start()
{
    D("%s: enter", __FUNCTION__);
    
    state.gps_status.status = GPS_STATUS_ENGINE_ON;
    set_pending_command(CMD_STATUS_CB);

    start_gps();

    D("%s: exit 0", __FUNCTION__);
    return 0;
}

static int odroid_gps_stop()
{
    D("%s: enter", __FUNCTION__);
    stop_gps();    
    D("%s: exit 0", __FUNCTION__);
    return 0;
}


/* Not implemented just debug*/
static int
odroid_gps_inject_time(GpsUtcTime time, int64_t timeReference,
		    int uncertainty)
{
    char buff[100];
    int tow, week;
    time_t s_time;

    ALOGD("%s: enter", __FUNCTION__);

    s_time = time / 1000;
    utc_to_gps(s_time, &tow, &week);
    memset(buff, 0, 100);
    snprintf(buff, 98, "AT*E2GPSTIME=%d,%d\r", tow, week);
    ALOGD("%s", buff);

    ALOGD("%s: exit 0", __FUNCTION__);
    return 0;
}

/** Injects current location from another location provider
 *  (typically cell ID).
 *  latitude and longitude are measured in degrees
 *  expected accuracy is measured in meters
 */
static int
odroid_gps_inject_location(double latitude, double longitude, float accuracy)
{
  
    ALOGD("%s: lat = %f , lon = %f , acc = %f", __FUNCTION__, latitude,
	 longitude, accuracy);
    return 0;
}

static void odroid_gps_delete_aiding_data(GpsAidingData flags)
{
    ALOGD("%s", __FUNCTION__);
}

static char* get_mode_name(int mode)
{
	switch (mode) {
	case GPS_POSITION_MODE_STANDALONE:
		return "GPS_POSITION_MODE_STANDALONE";
	case GPS_POSITION_MODE_MS_BASED:
		return "GPS_POSITION_MODE_MS_BASED";
	case GPS_POSITION_MODE_MS_ASSISTED:
		return "GPS_POSITION_MODE_MS_ASSISTED";
	default:
		return "UNKNOWN MODE";
	}
}

static int odroid_gps_set_position_mode(GpsPositionMode mode,
				GpsPositionRecurrence recurrence,
				uint32_t min_interval,
				uint32_t preferred_accuracy,
				uint32_t preferred_time)
{
	ALOGD("%s:enter  %s min_interval = %d pref=%d", __FUNCTION__,
			get_mode_name(mode), min_interval, preferred_time);

	switch (mode) {
	case GPS_POSITION_MODE_MS_BASED:
		ALOGE("MS_BASED mode setting SUPL");
		/* We handle the connection inside GPS so no need fr this */
		/*
		set_pending_command(CMD_AGPS_STATUS_CB);
		*/
		break;
	case GPS_POSITION_MODE_MS_ASSISTED:
		ALOGE("MS_ASSISTED mode setting SUPL");
		break;
	case GPS_POSITION_MODE_STANDALONE:
	default:
		break;
	}

	ALOGD("%s: exit 0", __FUNCTION__);
	return 0;
}

static const void *odroid_gps_get_extension(const char *name)
{
  ALOGD("%s: enter name=%s", __FUNCTION__, name);
 
  if (name == NULL)
    return NULL;

    ALOGD("%s, querying %s", __FUNCTION__, name);

    //codewalker
    return NULL;

#if 0
    /* Not supported */
    if(strstr (name, GPS_NI_INTERFACE)) {
      ALOGD("%s: exit &mbmGpsNiInterface", __FUNCTION__);
        return &mbmGpsNiInterface;
    }
#endif

    ALOGD("%s: exit NULL", __FUNCTION__);
    return NULL;
}

/** Represents the standard GPS interface. */
static const GpsInterface mbmGpsInterface = {
	sizeof(GpsInterface),
	/**
	 * Opens the interface and provides the callback routines
	 * to the implemenation of this interface.
	 */
    odroid_gps_init,

	/** Starts navigating. */
    odroid_gps_start,

	/** Stops navigating. */
    odroid_gps_stop,

	/** Closes the interface. */
    odroid_gps_cleanup,

	/** Injects the current time. */
    odroid_gps_inject_time,

	/** Injects current location from another location provider
	 *  (typically cell ID).
	 *  latitude and longitude are measured in degrees
	 *  expected accuracy is measured in meters
	 */
    odroid_gps_inject_location,

	/**
	 * Specifies that the next call to start will not use the
	 * information defined in the flags. GPS_DELETE_ALL is passed for
	 * a cold start.
	 */

    odroid_gps_delete_aiding_data,
	/**
	 * fix_frequency represents the time between fixes in seconds.
	 * Set fix_frequency to zero for a single-shot fix.
	 */
    odroid_gps_set_position_mode,

	/** Get a pointer to extension information. */
    odroid_gps_get_extension,
};

const GpsInterface *gps_get_hardware_interface()
{
    ALOGD("gps_get_hardware_interface");
    return &mbmGpsInterface;
}

/* This is for Gingerbread */
const GpsInterface* odroid_get_gps_interface(struct gps_device_t* dev)
{
    ALOGD("odroid_gps_get_hardware_interface");
    return &mbmGpsInterface;
}

static int odroid_open_gps(const struct hw_module_t* module,
			char const* name,
			struct hw_device_t** device)
{
  struct gps_device_t *dev;
  
    D("%s: enter", __FUNCTION__);

    if (module == NULL) {
	    ALOGE("%s: module null", __FUNCTION__);
	    return -1;
    }

    dev = malloc(sizeof(struct gps_device_t));
    if (!dev) {
	    ALOGE("%s: malloc fail", __FUNCTION__);
	    return -1;
    }
    memset(dev, 0, sizeof(*dev));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t*) module;
    dev->get_gps_interface = odroid_get_gps_interface;

    *device = (struct hw_device_t*)dev;

    D("%s: exit", __FUNCTION__);
    return 0;
}

static struct hw_module_methods_t odroid_gps_module_methods = {
    .open = odroid_open_gps
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = GPS_HARDWARE_MODULE_ID,
    .name = "Odoroid GPS",
    .author = "HARDKERNEL",
    .methods = &odroid_gps_module_methods,
};
