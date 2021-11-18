#ifndef fooesoundhfoo
#define fooesoundhfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

/* Most of the following is blatantly stolen from esound. */

/* path and name of the default EsounD domain socket */
#define ESD_UNIX_SOCKET_DIR     "/tmp/.esd"
#define ESD_UNIX_SOCKET_NAME    "/tmp/.esd/socket"

/* length of the audio buffer size */
#define ESD_BUF_SIZE (4 * 1024)
/* maximum size we can write().  Otherwise we might overflow */
#define ESD_MAX_WRITE_SIZE (21 * 4096)

/* length of the authentication key, octets */
#define ESD_KEY_LEN (16)

/* default port for the EsounD server */
#define ESD_DEFAULT_PORT (16001)

/* default sample rate for the EsounD server */
#define ESD_DEFAULT_RATE (44100)

/* maximum length of a stream/sample name */
#define ESD_NAME_MAX (128)

/* a magic number to identify the relative endianness of a client */
#define ESD_ENDIAN_KEY ((uint32_t) (('E' << 24) + ('N' << 16) + ('D' << 8) + ('N')))

#define ESD_VOLUME_BASE (256)

/*************************************/
/* what can we do to/with the EsounD */
enum esd_proto {
    ESD_PROTO_CONNECT,      /* implied on initial client connection */

    /* pseudo "security" functionality */
    ESD_PROTO_LOCK,         /* disable "foreign" client connections */
    ESD_PROTO_UNLOCK,       /* enable "foreign" client connections */

    /* stream functionality: play, record, monitor */
    ESD_PROTO_STREAM_PLAY,  /* play all following data as a stream */
    ESD_PROTO_STREAM_REC,   /* record data from card as a stream */
    ESD_PROTO_STREAM_MON,   /* send mixed buffer output as a stream */

    /* sample functionality: cache, free, play, loop, EOL, kill */
    ESD_PROTO_SAMPLE_CACHE, /* cache a sample in the server */
    ESD_PROTO_SAMPLE_FREE,  /* release a sample in the server */
    ESD_PROTO_SAMPLE_PLAY,  /* play a cached sample */
    ESD_PROTO_SAMPLE_LOOP,  /* loop a cached sample, til eoloop */
    ESD_PROTO_SAMPLE_STOP,  /* stop a looping sample when done */
    ESD_PROTO_SAMPLE_KILL,  /* stop the looping sample immediately */

    /* free and reclaim /dev/dsp functionality */
    ESD_PROTO_STANDBY,      /* release /dev/dsp and ignore all data */
    ESD_PROTO_RESUME,       /* reclaim /dev/dsp and play sounds again */

    /* TODO: move these to a more logical place. NOTE: will break the protocol */
    ESD_PROTO_SAMPLE_GETID, /* get the ID for an already-cached sample */
    ESD_PROTO_STREAM_FILT,  /* filter mixed buffer output as a stream */

    /* esd remote management */
    ESD_PROTO_SERVER_INFO,  /* get server info (ver, sample rate, format) */
    ESD_PROTO_ALL_INFO,     /* get all info (server info, players, samples) */
    ESD_PROTO_SUBSCRIBE,    /* track new and removed players and samples */
    ESD_PROTO_UNSUBSCRIBE,  /* stop tracking updates */

    /* esd remote control */
    ESD_PROTO_STREAM_PAN,   /* set stream panning */
    ESD_PROTO_SAMPLE_PAN,   /* set default sample panning */

    /* esd status */
    ESD_PROTO_STANDBY_MODE, /* see if server is in standby, autostandby, etc */

    /* esd latency */
    ESD_PROTO_LATENCY,      /* retrieve latency between write()'s and output */

    ESD_PROTO_MAX           /* for bounds checking */
};

/******************/
/* The EsounD api */

/* the properties of a sound buffer are logically or'd */

/* bits of stream/sample data */
#define ESD_MASK_BITS   ( 0x000F )
#define ESD_BITS8       ( 0x0000 )
#define ESD_BITS16      ( 0x0001 )

/* how many interleaved channels of data */
#define ESD_MASK_CHAN   ( 0x00F0 )
#define ESD_MONO        ( 0x0010 )
#define ESD_STEREO      ( 0x0020 )

/* whether it's a stream or a sample */
#define ESD_MASK_MODE   ( 0x0F00 )
#define ESD_STREAM      ( 0x0000 )
#define ESD_SAMPLE      ( 0x0100 )
#define ESD_ADPCM       ( 0x0200 )      /* TODO: anyone up for this? =P */

/* the function of the stream/sample, and common functions */
#define ESD_MASK_FUNC   ( 0xF000 )
#define ESD_PLAY        ( 0x1000 )
/* functions for streams only */
#define ESD_MONITOR     ( 0x0000 )
#define ESD_RECORD      ( 0x2000 )
/* functions for samples only */
#define ESD_STOP        ( 0x0000 )
#define ESD_LOOP        ( 0x2000 )

typedef int esd_format_t;
typedef int esd_proto_t;

/*******************************************************************/
/* esdmgr.c - functions to implement a "sound manager" for esd */

/* structures to retrieve information about streams/samples from the server */
typedef struct esd_server_info {

    int version;                /* server version encoded as an int */
    esd_format_t format;        /* magic int with the format info */
    int rate;                   /* sample rate */

} esd_server_info_t;

typedef struct esd_player_info {

    struct esd_player_info *next; /* point to next entry in list */
    esd_server_info_t *server;  /* the server that contains this stream */

    int source_id;              /* either a stream fd or sample id */
    char name[ ESD_NAME_MAX ];  /* name of stream for remote control */
    int rate;                   /* sample rate */
    int left_vol_scale;         /* volume scaling */
    int right_vol_scale;

    esd_format_t format;        /* magic int with the format info */

} esd_player_info_t;

typedef struct esd_sample_info {

    struct esd_sample_info *next; /* point to next entry in list */
    esd_server_info_t *server;  /* the server that contains this sample */

    int sample_id;              /* either a stream fd or sample id */
    char name[ ESD_NAME_MAX ];  /* name of stream for remote control */
    int rate;                   /* sample rate */
    int left_vol_scale;         /* volume scaling */
    int right_vol_scale;

    esd_format_t format;        /* magic int with the format info */
    int length;                 /* total buffer length */

} esd_sample_info_t;

typedef struct esd_info {

    esd_server_info_t *server;
    esd_player_info_t *player_list;
    esd_sample_info_t *sample_list;

} esd_info_t;

enum esd_standby_mode {
    ESM_ERROR, ESM_ON_STANDBY, ESM_ON_AUTOSTANDBY, ESM_RUNNING
};
typedef int esd_standby_mode_t;

enum esd_client_state {
    ESD_STREAMING_DATA,         /* data from here on is streamed data */
    ESD_CACHING_SAMPLE,         /* midway through caching a sample */
    ESD_NEEDS_REQDATA,          /* more data needed to complete request */
    ESD_NEXT_REQUEST,           /* proceed to next request */
    ESD_CLIENT_STATE_MAX        /* place holder */
};
typedef int esd_client_state_t;

/* the endian key is transferred in binary, if it's read into int, */
/* and matches ESD_ENDIAN_KEY (ENDN), then the endianness of the */
/* server and the client match; if it's SWAP_ENDIAN_KEY, swap data */
#define ESD_SWAP_ENDIAN_KEY (PA_UINT32_SWAP(ESD_ENDIAN_KEY))

#endif
