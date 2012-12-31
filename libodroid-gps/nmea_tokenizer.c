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

#include <cutils/sockets.h>
#include <cutils/properties.h>

#include "log.h"
#include "nmea_tokenizer.h"

/* Just check this file */
#ifdef __GNUC__
#pragma GCC diagnostic warning "-pedantic"
#endif

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       N M E A   T O K E N I Z E R                     *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/

int nmea_tokenizer_init(NmeaTokenizer * t, const char *p, const char *end)
{
    int count = 0;
    /* char *q; */

    ENTER;
    /* the initial '$' is optional */
    if (p < end && p[0] == '$')
        p += 1;

    /* remove trailing newline */
    if (end > p && end[-1] == '\n') {
        end -= 1;
        if (end > p && end[-1] == '\r')
            end -= 1;
    }
    /* get rid of checksum at the end of the sentecne */
    if (end >= p + 3 && end[-3] == '*') {
        end -= 3;
    }

    while (p < end) {
        const char *q = p;

        q = (const char *) memchr(p, ',', end - p);
        if (q == NULL)
            q = end;

        if (count < MAX_NMEA_TOKENS) {
            t->tokens[count].p = p;
            t->tokens[count].end = q;
            count += 1;
        }
        if (q < end)
            q += 1;

        p = q;
    }

    t->count = count;
    EXIT;
    return count;
}

Token nmea_tokenizer_get(NmeaTokenizer * t, int index)
{
    Token tok;
    static const char *dummy = "";

    ENTER;
    if (index < 0 || index >= t->count) {
        tok.p = tok.end = dummy;
    } else
        tok = t->tokens[index];

    EXIT;
    return tok;
}
