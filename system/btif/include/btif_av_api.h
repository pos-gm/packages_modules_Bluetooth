/******************************************************************************
 *
 *  Copyright (C) 2009-2012 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

/*****************************************************************************
 **
 **  Name:           btif_av_api.h
 **
 **  Description:    This is the public interface file for the advanced
 **                  audio/video streaming (AV) subsystem of BTIF, Broadcom's
 **                  Bluetooth application layer for mobile phones.
 **
 *****************************************************************************/

#ifndef BTIF_AV_API_H
#define BTIF_AV_API_H

#include "bt_target.h"
#include "bta_av_api.h"
#include "uipc.h"

#include "btif_media.h"
#include "a2d_api.h"
#include "a2d_sbc.h"


/*****************************************************************************
 **  Constants and data types
 *****************************************************************************/

/* Codec type */
#define BTIF_AV_CODEC_NONE       0xFF
#define BTIF_AV_CODEC_PCM        0x5                     /* Raw PCM */
typedef uint8_t tBTIF_AV_CODEC_ID;


#define BTIF_AV_FEEDING_ASYNCHRONOUS 0   /* asynchronous feeding, use tx av timer */
#define BTIF_AV_FEEDING_SYNCHRONOUS  1   /* synchronous feeding, no av tx timer */
typedef uint8_t tBTIF_AV_FEEDING_MODE;

/**
 * Structure used to configure the AV codec capabilities/config
 */
typedef struct
{
    tBTIF_AV_CODEC_ID id;            /* Codec ID (in terms of BTIF) */
    uint8_t info[AVDT_CODEC_SIZE];     /* Codec info (can be config or capabilities) */
} tBTIF_AV_CODEC_INFO;

/**
 * Structure used to configure the AV media feeding
 */
typedef struct
{
    uint16_t sampling_freq;   /* 44100, 48000 etc */
    uint16_t num_channel;     /* 1 for mono or 2 stereo */
    uint8_t  bit_per_sample;  /* Number of bits per sample (8, 16) */
} tBTIF_AV_MEDIA_FEED_CFG_PCM;

typedef union
{
    tBTIF_AV_MEDIA_FEED_CFG_PCM pcm;     /* Raw PCM feeding format */
}tBTIF_AV_MEDIA_FEED_CFG;

typedef struct
{
    tBTIF_AV_CODEC_ID format;        /* Media codec identifier */
    tBTIF_AV_MEDIA_FEED_CFG cfg;     /* Media codec configuration */
} tBTIF_AV_MEDIA_FEEDINGS;

#endif /* BTIF_AV_API_H */
