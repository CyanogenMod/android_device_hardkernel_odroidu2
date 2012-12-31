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

typedef struct {
    const char *p;
    const char *end;
} Token;

#define  MAX_NMEA_TOKENS  20

typedef struct {
    int count;
    Token tokens[MAX_NMEA_TOKENS];
} NmeaTokenizer;

int nmea_tokenizer_init(NmeaTokenizer * t, const char *p, const char *end);

Token nmea_tokenizer_get(NmeaTokenizer * t, int index);
