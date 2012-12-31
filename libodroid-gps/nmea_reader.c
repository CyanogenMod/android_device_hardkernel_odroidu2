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
 *
 * Author: Carl Nordbeck <carl.nordbeck@ericsson.com>
 *         Torgny Johansson <torgny.johansson@ericsson.com>
 *         Bas Flaton
 *         Robert Lukassen
 *         Thijs Raven
 */

#include <errno.h>
#include <pthread.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <math.h>
#include <time.h>
#include <time64.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

#include <cutils/sockets.h>
#include <cutils/properties.h>

#include "log.h"
#include "nmea_tokenizer.h"
#include "nmea_reader.h"

/* Just check this file */
#ifdef __GNUC__
#pragma GCC diagnostic warning "-pedantic"
#endif

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       N M E A   P A R S E R                           *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/

static int str2int(const char *p, const char *end)
{
    int result = 0;
    int len = end - p;
    ENTER;

    /* if (len == 0) { */
    /*      return -1; */
    /* } */

    for (; len > 0; len--, p++) {
        int c;

        if (p >= end)
            goto Fail;

        c = *p - '0';
        if ((unsigned) c >= 10)
            goto Fail;

        result = result * 10 + c;
    }
    EXIT;
    return result;

  Fail:
    EXIT;
    return -1;
}

static double str2float(const char *p, const char *end)
{
    /* int result = 0; */
    int len = end - p;
    char temp[16];

    ENTER;
    /* if (len == 0) { */
    /*      return -1.0; */
    /* } */

    if (len >= (int) sizeof(temp))
        return 0.;

    memcpy(temp, p, len);
    temp[len] = 0;
    EXIT;
    return strtod(temp, NULL);
}

static void nmea_reader_update_utc_diff(NmeaReader * r)
{
    time_t now = time(NULL);
    struct tm tm_local;
    struct tm tm_utc;
    long time_local, time_utc;

    ENTER;
    gmtime_r(&now, &tm_utc);
    localtime_r(&now, &tm_local);

    time_local = tm_local.tm_sec +
        60 * (tm_local.tm_min +
              60 * (tm_local.tm_hour +
                    24 * (tm_local.tm_yday + 365 * tm_local.tm_year)));

    time_utc = tm_utc.tm_sec +
        60 * (tm_utc.tm_min +
              60 * (tm_utc.tm_hour +
                    24 * (tm_utc.tm_yday + 365 * tm_utc.tm_year)));

    r->utc_diff = time_utc - time_local;
    EXIT;
}


void nmea_reader_init(NmeaReader * r)
{
    ENTER;
    memset(r, 0, sizeof(*r));

    r->pos = 0;
    r->overflow = 0;
    r->utc_year = -1;
    r->utc_mon = -1;
    r->utc_day = -1;
    r->callback = NULL;
    r->sv_status_callback = NULL;
    r->nmea_callback = NULL;
    r->update = 0;

    nmea_reader_update_utc_diff(r);
    EXIT;
}

void nmea_reader_set_callbacks(NmeaReader * r, GpsCallbacks * cbs)
{
    ENTER;
    if (cbs == NULL)
        return;

    r->callback = cbs->location_cb;
    if (cbs->location_cb != NULL && r->fix.flags != 0) {
        ALOGD("%s: sending latest fix to new callback", __FUNCTION__);
        //r->callback(&r->fix);
        r->set_pending_callback_cb(CMD_LOCATION_CB);
        r->fix.flags = 0;
    }

    r->sv_status_callback = cbs->sv_status_cb;
    if (cbs->sv_status_cb != NULL) {
        ALOGD("%s: sending latest sv_status to new callback", __FUNCTION__);
        //r->sv_status_callback(&r->sv_status);
        r->set_pending_callback_cb(CMD_SV_STATUS_CB);
        r->sv_status_changed = 0;
    }

    /*old data */
    r->nmea_callback = cbs->nmea_cb;
    if (cbs->nmea_cb != NULL) {
        ALOGD("%s: sending latest nmea sentence to new callback",
             __FUNCTION__);
        /* r->nmea_callback(time(NULL)*1000, r->in, r->pos + 1); */
        r->set_pending_callback_cb(CMD_NMEA_CB);
    }

    EXIT;
}

static int nmea_reader_update_time(NmeaReader * r, Token tok)
{
    int hour, minute;
    double seconds;
    double milliseconds;
    struct tm tm;
    time64_t fix_time;

    ENTER;
    if (tok.p + 6 > tok.end)
        return -1;

    if (r->utc_year < 0) {
        // no date yet, get current one
        time_t now = time(NULL);
        gmtime_r(&now, &tm);
        r->utc_year = tm.tm_year + 1900;
        r->utc_mon = tm.tm_mon + 1;
        r->utc_day = tm.tm_mday;
    }

    hour = str2int(tok.p, tok.p + 2);
    minute = str2int(tok.p + 2, tok.p + 4);
    seconds = str2float(tok.p + 4, tok.end);
    milliseconds = seconds * 1000.0;    /* This is seconds + milliseconds */

    /* initialize all values inside the tm-struct to 0,
       otherwise mktime(&tm) will return -1 */
    memset(&tm, 0, sizeof(tm));

    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = 0;              /* Don't set seconds; we will add them later (together with milliseconds) */
    tm.tm_year = r->utc_year - 1900;
    tm.tm_mon = r->utc_mon - 1;
    tm.tm_mday = r->utc_day;

    /* We use the timegm64() here, as there is no timegm() in Bionic.
     * The timesteamp field in the fix is 64-bit anyway, so we don't need
     * to convert back to 32-bit after this (which would be tricky to get safe).
     * timegm64() returns seconds since epoch.
     *
     * We immediately convert to milliseconds, as that is what is needed
     * in location fix struct.
     */
    fix_time = timegm64(&tm) * 1000LL;
    /* Now add the seconds+millliseconds */
    fix_time = fix_time + ((long) milliseconds);

    /* Assign calculated value to fix */
    r->fix.timestamp = (time64_t) fix_time;
    EXIT;
    return 0;
}


static int nmea_reader_update_date(NmeaReader * r, Token date, Token time)
{
    Token tok = date;
    int day, mon, year;

    ENTER;
    if (tok.p + 6 != tok.end) {
        ALOGD("date not properly formatted: '%.*s'", tok.end - tok.p,
             tok.p);
        return -1;
    }
    day = str2int(tok.p, tok.p + 2);
    mon = str2int(tok.p + 2, tok.p + 4);
    year = str2int(tok.p + 4, tok.p + 6) + 2000;

    if ((day | mon | year) < 0) {
        ALOGD("date not properly formatted: '%.*s'", tok.end - tok.p,
             tok.p);
        return -1;
    }

    r->utc_year = year;
    r->utc_mon = mon;
    r->utc_day = day;

    EXIT;
    return nmea_reader_update_time(r, time);
}


static double convert_from_hhmm(Token tok)
{
    double val = str2float(tok.p, tok.end);
    int degrees = (int) (floor(val) / 100);
    double minutes = val - degrees * 100.;
    double dcoord = degrees + minutes / 60.0;
    return dcoord;
}


static int
nmea_reader_update_latlong(NmeaReader * r,
                           Token latitude,
                           char latitudeHemi,
                           Token longitude, char longitudeHemi)
{
    double lat, lon;
    Token tok;

    ENTER;
    tok = latitude;
    if (tok.p + 6 > tok.end) {
        ALOGD("latitude is too short: '%.*s'", tok.end - tok.p, tok.p);
        return -1;
    }
    lat = convert_from_hhmm(tok);
    if (latitudeHemi == 'S')
        lat = -lat;

    tok = longitude;
    if (tok.p + 6 > tok.end) {
        ALOGD("longitude is too short: '%.*s'", tok.end - tok.p, tok.p);
        return -1;
    }
    lon = convert_from_hhmm(tok);
    if (longitudeHemi == 'W')
        lon = -lon;

    r->fix.flags |= GPS_LOCATION_HAS_LAT_LONG;
    r->fix.latitude = lat;
    r->fix.longitude = lon;
    EXIT;
    return 0;
}


static int
nmea_reader_update_altitude(NmeaReader * r, Token altitude, Token units)
{
    /* double alt; */
    Token tok = altitude;
    (void) units;

    ENTER;
    if (tok.p >= tok.end)
        return -1;

    r->fix.flags |= GPS_LOCATION_HAS_ALTITUDE;
    r->fix.altitude = str2float(tok.p, tok.end);
    EXIT;
    return 0;
}

static int nmea_reader_update_accuracy(NmeaReader * r, Token accuracy)
{
    /* double acc; */
    Token tok = accuracy;

    ENTER;
    if (tok.p >= tok.end)
        return -1;

    r->fix.accuracy = str2float(tok.p, tok.end);

    if (r->fix.accuracy == 99.99) {
        return 0;
    }

    r->fix.flags |= GPS_LOCATION_HAS_ACCURACY;
    EXIT;
    return 0;
}

static int nmea_reader_update_bearing(NmeaReader * r, Token bearing)
{
    /* double alt; */
    Token tok = bearing;

    ENTER;
    if (tok.p >= tok.end)
        return -1;

    r->fix.flags |= GPS_LOCATION_HAS_BEARING;
    r->fix.bearing = str2float(tok.p, tok.end);
    EXIT;
    return 0;
}


static int nmea_reader_update_speed(NmeaReader * r, Token speed)
{
    /* double alt; */
    Token tok = speed;

    ENTER;
    if (tok.p >= tok.end)
        return -1;

    r->fix.flags |= GPS_LOCATION_HAS_SPEED;
    /*android requires speed in m/s, but nmea gives knots
     * -> convert..
     */
    r->fix.speed = str2float(tok.p, tok.end) * 0.514444;
    EXIT;
    return 0;
}


static void nmea_reader_parse(NmeaReader * r)
{
    /* we received a complete sentence, now parse it to generate
     * a new GPS fix...
     */
    NmeaTokenizer tzer[1];
    Token tok;

    ENTER;
    ALOGD("Received: %.*s", r->pos, r->in);
    if (r->pos < 9) {
        ALOGD("Too short. discarded.");
        return;
    }

    nmea_tokenizer_init(tzer, r->in, r->in + r->pos);
#if 0
    {
        int n;
        ALOGD("Found %d tokens", tzer->count);
        for (n = 0; n < tzer->count; n++) {
            Token tok = nmea_tokenizer_get(tzer, n);
            ALOGD("%2d: '%.*s'", n, tok.end - tok.p, tok.p);
        }
    }
#endif
    tok = nmea_tokenizer_get(tzer, 0);

    if (tok.p + 5 > tok.end) {
        ALOGD("sentence id '%.*s' too short, ignored.", tok.end - tok.p,
             tok.p);
        return;
    }
    /* ignore first two characters. */
    tok.p += 2;

/*
**     GGA          Global Positioning System Fix Data
**1     123519       Fix taken at 12:35:19 UTC
**2     4807.038,N   Latitude 48 deg 07.038' N
**4     01131.000,E  Longitude 11 deg 31.000' E
**6     1            Fix quality: 0 = invalid
**                               1 = GPS fix (SPS)
**                               2 = DGPS fix
**                               3 = PPS fix
**			         4 = Real Time Kinematic
**			         5 = Float RTK
**                               6 = estimated (dead reckoning) (2.3 feature)
**			         7 = Manual input mode
**			         8 = Simulation mode
**7     08           Number of satellites being tracked
**8     0.9          Horizontal dilution of position
**9     545.4,M      Altitude, Meters, above mean sea level
**11     46.9,M       Height of geoid (mean sea level) above WGS84
**                      ellipsoid
**13   (empty field) time in seconds since last DGPS update
**14   (empty field) DGPS station ID number
**     *47          the checksum data, always begins with *
*/
/* GGA,214258.00,5740.857675,N,01159.649523,E,1,08,3.0,104.0,M,,,,*32 */
    if (!memcmp(tok.p, "GGA", 3)) {
        ALOGD("GGA");
        // GPS fix
        Token tok_fixstaus = nmea_tokenizer_get(tzer, 6);
        if (tok_fixstaus.p[0] > '0') {
            Token tok_altitude = nmea_tokenizer_get(tzer, 9);
            Token tok_altitudeUnits = nmea_tokenizer_get(tzer, 10);

            nmea_reader_update_altitude(r, tok_altitude,
                                        tok_altitudeUnits);
        }

/*
**      GSA      Satellite status
**1     A        Auto selection of 2D or 3D fix (M = manual) 
**2     3        3D fix - values include: 1 = no fix
**                                       2 = 2D fix
**                                       3 = 3D fix
**3     04,05... PRNs of satellites used for fix (space for 12) 
**15     2.5      PDOP (dilution of precision) 
**16     1.3      Horizontal dilution of precision (HDOP) 
**17     2.1      Vertical dilution of precision (VDOP)
**18     *39      the checksum data, always begins with *
*/
/* GSA,A,3,02,04,07,13,20,23,,,,,,,6.7,3.0,6.0*36 */
    } else if (!memcmp(tok.p, "GSA", 3)) {
        ALOGD("GSA");
        Token tok_fixStatus = nmea_tokenizer_get(tzer, 2);
        int i;

        if (tok_fixStatus.p[0] != '\0' && tok_fixStatus.p[0] != '1') {

            Token tok_accuracy = nmea_tokenizer_get(tzer, 15);

            nmea_reader_update_accuracy(r, tok_accuracy);

            r->sv_status.used_in_fix_mask = 0ul;

            for (i = 3; i <= 14; ++i) {

                Token tok_prn = nmea_tokenizer_get(tzer, i);
                int prn = str2int(tok_prn.p, tok_prn.end);

                if (prn > 0) {
                    r->sv_status.used_in_fix_mask |= (1ul << (prn - 1));
                    r->sv_status_changed = 1;
                    ALOGD("%s: fix mask is %d", __FUNCTION__,
                         r->sv_status.used_in_fix_mask);
                }

            }

        }
/*
** GLL           Geographic Position, Latitude / Longitude and time.
**  4916.46,N    Latitude 49 deg. 16.45 min. North
**  12311.12,W   Longitude 123 deg. 11.12 min. West
**  225444       Fix taken at 22:54:44 UTC
**  A            Data valid
**
**  $GPGLL,4916.45,N,12311.12,W,225444,A*31
*/
    } else if (!memcmp(tok.p, "GLL", 3)) {
        ALOGD("GLL");
        Token tok_fixStatus = nmea_tokenizer_get(tzer, 6);

        if (tok_fixStatus.p[0] == 'A') {
            Token tok_time = nmea_tokenizer_get(tzer, 5);
            Token tok_fixStatus = nmea_tokenizer_get(tzer, 6);
            Token tok_latitude = nmea_tokenizer_get(tzer, 1);
            Token tok_latitudeHemi = nmea_tokenizer_get(tzer, 2);
            Token tok_longitude = nmea_tokenizer_get(tzer, 3);
            Token tok_longitudeHemi = nmea_tokenizer_get(tzer, 4);

            ALOGD("in GGL, fixStatus=%c", tok_fixStatus.p[0]);
            if (tok_fixStatus.p[0] == 'A') {
                nmea_reader_update_latlong(r, tok_latitude,
                                           tok_latitudeHemi.p[0],
                                           tok_longitude,
                                           tok_longitudeHemi.p[0]);

                r->update = 1;
            }
        }
/*
**     RMC          Recommended Minimum sentence C
**1     123519       Fix taken at 12:35:19 UTC
**2     A            Status A=active or V=Void.
**3     4807.038,N   Latitude 48 deg 07.038' N
**5     01131.000,E  Longitude 11 deg 31.000' E
**7     022.4        Speed over the ground in knots
**8     084.4        Track angle in degrees True
**9     230394       Date - 23rd of March 1994
**10     003.1,W      Magnetic Variation
**     *6A          The checksum data, always begins with *
*/
/* RMC,232401.00,A,5740.841023,N,01159.626002,E,000.0,244.0,031109,,,A*56 */
    } else if (!memcmp(tok.p, "RMC", 3)) {
        ALOGD("RMC");
        Token tok_fixStatus = nmea_tokenizer_get(tzer, 2);

        if (tok_fixStatus.p[0] == 'A') {
            Token tok_time = nmea_tokenizer_get(tzer, 1);
            Token tok_fixStatus = nmea_tokenizer_get(tzer, 2);
            Token tok_latitude = nmea_tokenizer_get(tzer, 3);
            Token tok_latitudeHemi = nmea_tokenizer_get(tzer, 4);
            Token tok_longitude = nmea_tokenizer_get(tzer, 5);
            Token tok_longitudeHemi = nmea_tokenizer_get(tzer, 6);
            Token tok_speed = nmea_tokenizer_get(tzer, 7);
            Token tok_bearing = nmea_tokenizer_get(tzer, 8);
            Token tok_date = nmea_tokenizer_get(tzer, 9);

            ALOGD("in RMC, fixStatus=%c", tok_fixStatus.p[0]);
            if (tok_fixStatus.p[0] == 'A') {
                nmea_reader_update_date(r, tok_date, tok_time);

                nmea_reader_update_latlong(r, tok_latitude,
                                           tok_latitudeHemi.p[0],
                                           tok_longitude,
                                           tok_longitudeHemi.p[0]);

                nmea_reader_update_bearing(r, tok_bearing);
                nmea_reader_update_speed(r, tok_speed);
                r->update = 1;
            }
        }

/*
**      GSV          Satellites in view
**1      2            Number of sentences for full data
**2      1            sentence 1 of 2
**3      08           Number of satellites in view
**
**4      01           Satellite PRN number
**      40           Elevation, degrees
**      083          Azimuth, degrees
**      46           SNR - higher is better
**           for up to 4 satellites per sentence
**      *75          the checksum data, always begins with *
*/
/* GSV,1,1,01,07,,,49,,,,,,,,,,,,*72 */
    } else if (!memcmp(tok.p, "GSV", 3)) {
        ALOGD("GSV");

        Token tok_noSatellites = nmea_tokenizer_get(tzer, 3);
        ALOGD("NR sat: '%.*s'", tok.end - tok.p, tok.p);
        int noSatellites =
            str2int(tok_noSatellites.p, tok_noSatellites.end);

        if (noSatellites > 0) {

            Token tok_noSentences = nmea_tokenizer_get(tzer, 1);
            Token tok_sentence = nmea_tokenizer_get(tzer, 2);

            int sentence = str2int(tok_sentence.p, tok_sentence.end);
            int totalSentences =
                str2int(tok_noSentences.p, tok_noSentences.end);
            int curr;
            int i;

            if (sentence == 1) {
                r->sv_status_changed = 0;
                r->sv_status.num_svs = 0;
            }

            curr = r->sv_status.num_svs;

            i = 0;

            while ((i < 4) && (r->sv_status.num_svs < noSatellites)) {

                Token tok_prn = nmea_tokenizer_get(tzer, i * 4 + 4);
                Token tok_elevation = nmea_tokenizer_get(tzer, i * 4 + 5);
                Token tok_azimuth = nmea_tokenizer_get(tzer, i * 4 + 6);
                Token tok_snr = nmea_tokenizer_get(tzer, i * 4 + 7);
                int prn;
                double snr;

                prn = str2int(tok_prn.p, tok_prn.end);
                snr = str2float(tok_snr.p, tok_snr.end);

                //if (prn > 0 && snr > 0) {
                if (prn > 0) {
                    r->sv_status.sv_list[curr].prn = prn;
                    r->sv_status.sv_list[curr].elevation =
                        str2float(tok_elevation.p, tok_elevation.end);
                    r->sv_status.sv_list[curr].azimuth =
                        str2float(tok_azimuth.p, tok_azimuth.end);
                    r->sv_status.sv_list[curr].snr = snr;
                    r->sv_status.num_svs += 1;

                    curr += 1;
                }
                i += 1;
            }

            if (sentence == totalSentences) {
                r->sv_status_changed = 1;
                ALOGD("%s: GSV message with total satellites %d",
                     __FUNCTION__, r->sv_status.num_svs);

            }
        }

    } else {
        tok.p -= 2;
        ALOGD("unknown sentence '%.*s", tok.end - tok.p, tok.p);
    }

    if ((r->fix.flags != 0) && r->update) {
#if 0
        char temp[256];
        char *p = temp;
        char *end = p + sizeof(temp);
        struct tm utc;

        p += snprintf(p, end - p, "sending fix");
        if (r->fix.flags & GPS_LOCATION_HAS_LAT_LONG) {
            p += snprintf(p, end - p, " lat=%g lon=%g", r->fix.latitude,
                          r->fix.longitude);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_ALTITUDE) {
            p += snprintf(p, end - p, " altitude=%g", r->fix.altitude);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_SPEED) {
            p += snprintf(p, end - p, " speed=%g", r->fix.speed);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_BEARING) {
            p += snprintf(p, end - p, " bearing=%g", r->fix.bearing);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_ACCURACY) {
            p += snprintf(p, end - p, " accuracy=%g", r->fix.accuracy);
        }
        gmtime_r((time_t *) & r->fix.timestamp, &utc);
        p += snprintf(p, end - p, " time=%s", asctime(&utc));
        ALOGD("%s", temp);
#endif
        if (r->callback) {
            //r->callback(&r->fix);
            r->set_pending_callback_cb(CMD_LOCATION_CB);
            r->fix.flags = 0;
            r->update = 0;
        } else {
            ALOGE("no callback, keeping data until needed !");
        }
    }

    if (r->sv_status_changed != 0) {
        if (r->sv_status_callback) {
            ALOGD("update sv status");
            //r->sv_status_callback(&r->sv_status);
            r->set_pending_callback_cb(CMD_SV_STATUS_CB);
            r->sv_status_changed = 0;
        }
    }
    EXIT;

}

void nmea_reader_add(NmeaReader * r, char *nmea)
{
    unsigned int i;

    ENTER;
    ALOGD("%s: %s", __FUNCTION__, nmea);

    for (i = 0; i < strlen(nmea); i++)
        nmea_reader_addc(r, nmea[i]);
    nmea_reader_addc(r, '\n');
    EXIT;
}

void nmea_reader_addc(NmeaReader * r, int c)
{
    ENTER;
    ALOGV("%s: %c", __FUNCTION__, (char) c);
    if (r->overflow) {
        r->overflow = (c != '\n');
        return;
    }

    if (r->pos >= (int) sizeof(r->in) - 1) {
        r->overflow = 1;
        r->pos = 0;
        return;
    }

    r->in[r->pos] = (char) c;
    r->pos += 1;

    if (c == '\n') {
        ALOGD("Got an nmea string, parsing.");
        nmea_reader_parse(r);
        if (r->nmea_callback) {
            struct timeval tv;
            int64_t now;
            gettimeofday(&tv, (struct timezone *) NULL);
            now = tv.tv_sec * 1000LL + tv.tv_usec / 1000;
            r->set_pending_callback_cb(CMD_NMEA_CB);
        }
        r->pos = 0;
    }
    EXIT;
}
