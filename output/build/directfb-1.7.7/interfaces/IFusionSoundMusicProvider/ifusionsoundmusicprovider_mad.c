/*
 * Copyright (C) 2005-2008 Claudio Ciccani <klan@users.sf.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>

#include <fusionsound.h>
#include <fusionsound_limits.h>

#include <media/ifusionsoundmusicprovider.h>

#include <direct/types.h>
#include <direct/clock.h>
#include <direct/mem.h>
#include <direct/memcpy.h>
#include <direct/stream.h>
#include <direct/thread.h>
#include <direct/util.h>

#include <mad.h>


static DirectResult
Probe( IFusionSoundMusicProvider_ProbeContext *ctx );

static DirectResult
Construct( IFusionSoundMusicProvider *thiz,
           const char                *filename,
           DirectStream              *stream );

#include <direct/interface_implementation.h>

DIRECT_INTERFACE_IMPLEMENTATION( IFusionSoundMusicProvider, Mad )

/*
 * private data struct of IFusionSoundMusicProvider
 */
typedef struct {
     int                           ref;       /* reference counter */

     DirectStream                 *s;

     struct mad_synth              synth;
     struct mad_stream             stream;
     struct mad_frame              frame;

     double                        length;
     int                           samplerate;
     int                           channels;
     unsigned int                  frames;
     FSTrackDescription            desc;

     FSMusicProviderPlaybackFlags  flags;

     DirectThread                 *thread;
     pthread_mutex_t               lock;
     pthread_cond_t                cond;

     FSMusicProviderStatus         status;
     int                           finished;
     int                           seeked;

     void                         *read_buffer;
     int                           read_size;

     struct {
          IFusionSoundStream      *stream;
          IFusionSoundBuffer      *buffer;
          FSSampleFormat           format;
          FSChannelMode            mode;
          int                      length;
     } dest;

     FMBufferCallback              callback;
     void                         *ctx;
} IFusionSoundMusicProvider_Mad_data;


#define PREBUFFER_SIZE     1 /* in seconds */

#define XING_MAGIC         (('X' << 24) | ('i' << 16) | ('n' << 8) | 'g')


struct id3_tag {
     s8 tag[3];
     s8 title[30];
     s8 artist[30];
     s8 album[30];
     s8 year[4];
     s8 comment[30];
     u8 genre;
};

static const char *id3_genres[] = {
     "Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge",
     "Hip-Hop", "Jazz", "Metal", "New Age", "Oldies", "Other", "Pop", "R&B",
     "Rap", "Reggae", "Rock", "Techno", "Industrial", "Alternative", "Ska",
     "Death Metal", "Pranks", "Soundtrack", "Euro-Techno", "Ambient",
     "Trip-Hop", "Vocal", "Jazz+Funk", "Fusion", "Trance", "Classical",
     "Instrumental", "Acid", "House", "Game", "Sound Clip", "Gospel", "Noise",
     "AlternRock", "Bass", "Soul", "Punk", "Space", "Meditative",
     "Instrumental Pop", "Instrumental Rock", "Ethnic", "Gothic", "Darkwave",
     "Techno-Industrial", "Electronic", "Pop-Folk", "Eurodance", "Dream",
     "Southern Rock", "Comedy", "Cult", "Gangsta Rap", "Top 40",
     "Christian Rap", "Pop/Funk", "Jungle", "Native American", "Cabaret",
     "New Wave", "Psychedelic", "Rave", "Showtunes", "Trailer", "Lo-Fi",
     "Tribal", "Acid Punk", "Acid Jazz", "Polka", "Retro", "Musical",
     "Rock & Roll", "Hard Rock", "Folk", "Folk/Rock", "National Folk", "Swing",
     "Fast-Fusion", "Bebob", "Latin", "Revival", "Celtic", "Bluegrass",
     "Avantgarde", "Gothic Rock", "Progressive Rock", "Psychedelic Rock",
     "Symphonic Rock", "Slow Rock", "Big Band", "Chorus", "Easy Listening",
     "Acoustic", "Humour", "Speech", "Chanson", "Opera", "Chamber Music",
     "Sonata", "Symphony", "Booty Bass", "Primus", "Porn Groove", "Satire",
     "Slow Jam", "Club", "Tango", "Samba", "Folklore", "Ballad",
     "Power Ballad", "Rhythmic Soul", "Freestyle", "Duet", "Punk Rock",
     "Drum Solo", "A Cappella", "Euro-House", "Dance Hall", "Goa",
     "Drum & Bass", "Club-House", "Hardcore", "Terror", "Indie", "BritPop",
     "Negerpunk", "Polsk Punk", "Beat", "Christian Gangsta Rap", "Heavy Metal",
     "Black Metal", "Crossover", "Contemporary Christian", "Christian Rock",
     "Merengue", "Salsa", "Thrash Metal", "Anime", "JPop", "Synthpop"
};


typedef struct {
#ifdef WORDS_BIGENDIAN
     s8 c;
     u8 b;
     u8 a;
#else
     u8 a;
     u8 b;
     s8 c;
#endif
} __attribute__((packed)) s24;

static inline u8
FtoU8( mad_fixed_t sample )
{
     /* round */
     sample += (1 << (MAD_F_FRACBITS - 8));

     /* clip */
     if (sample >= MAD_F_ONE)
          sample = MAD_F_ONE - 1;
     else if (sample < -MAD_F_ONE)
          sample = -MAD_F_ONE;

     /* quantize */
     return (sample >> (MAD_F_FRACBITS - 7)) + 128;
}

static inline s16
FtoS16( mad_fixed_t sample )
{
     /* round */
     sample += (1 << (MAD_F_FRACBITS - 16));

     /* clip */
     if (sample >= MAD_F_ONE)
          sample = MAD_F_ONE - 1;
     else if (sample < -MAD_F_ONE)
          sample = -MAD_F_ONE;

     /* quantize */
     return sample >> (MAD_F_FRACBITS - 15);
}

static inline s24
FtoS24( mad_fixed_t sample )
{
     /* round */
     sample += (1 << (MAD_F_FRACBITS - 24));

     /* clip */
     if (sample >= MAD_F_ONE)
          sample = MAD_F_ONE - 1;
     else if (sample < -MAD_F_ONE)
          sample = -MAD_F_ONE;

     /* quantize */
     sample >>= (MAD_F_FRACBITS - 23);
     
     return (s24){ a:sample, b:sample>>8, c:sample>>16 };
}

static inline s32
FtoS32( mad_fixed_t sample )
{
     /* clip */
     if (sample >= MAD_F_ONE)
          sample = MAD_F_ONE - 1;
     else if (sample < -MAD_F_ONE)
          sample = -MAD_F_ONE;

     /* quantize */
     return sample << (31 - MAD_F_FRACBITS);
}

static inline float
FtoF32( mad_fixed_t sample )
{
     /* clip */
     if (sample >= MAD_F_ONE)
          sample = MAD_F_ONE - 1;
     else if (sample < -MAD_F_ONE)
          sample = -MAD_F_ONE;

     /* quantize */
     return (float)sample/(float)MAD_F_ONE;
}

#define MAD_MIX_LOOP() \
 do { \
     TYPE *d = (TYPE *)dst; \
     int   i; \
     if (src_channels == 2) { \
          switch (dst_mode) { \
               case FSCM_MONO: \
                    for (i = 0; i < len; i++) \
                         d[i] = CONV((left[i]+right[i])>>1); \
                    break; \
               case FSCM_STEREO: \
                    for (i = 0; i < len; i++) { \
                         d[0] = CONV(left[i]); \
                         d[1] = CONV(right[i]); \
                         d += 2; \
                    } \
                    break; \
               default: \
                    for (i = 0; i < len; i++) { \
                         *d++ = CONV(left[i]); \
                         if (FS_MODE_HAS_CENTER(dst_mode)) \
                              *d++ = CONV((left[i]+right[i])>>1); \
                         *d++ = CONV(right[i]); \
                         switch (FS_MODE_NUM_REARS(dst_mode)) { \
                              case 2: \
                                   *d++ = MUTE; \
                              case 1: \
                                   *d++ = MUTE; \
                         } \
                         if (FS_MODE_HAS_LFE(dst_mode)) \
                              *d++ = MUTE; \
                    } \
                    break; \
          } \
     } \
     else { \
          switch (dst_mode) { \
               case FSCM_MONO: \
                    for (i = 0; i < len; i++) \
                         d[i] = CONV(left[i]); \
                    break; \
               case FSCM_STEREO: \
                    for (i = 0; i < len; i++) { \
                         d[0] = d[1] = CONV(left[i]); \
                         d += 2; \
                    } \
                    break; \
               default: \
                    for (i = 0; i < len; i++) { \
                         if (FS_MODE_HAS_CENTER(dst_mode)) { \
                              d[0] = d[1] = d[2] = CONV(left[i]); \
                              d += 3; \
                         } else { \
                              d[0] = d[1] = CONV(left[i]); \
                              d += 2; \
                         } \
                         switch (FS_MODE_NUM_REARS(dst_mode)) { \
                              case 2: \
                                   *d++ = MUTE; \
                              case 1: \
                                   *d++ = MUTE; \
                         } \
                         if (FS_MODE_HAS_LFE(dst_mode)) \
                              *d++ = MUTE; \
                    } \
                    break; \
          } \
     } \
 } while (0)

static void
mad_mix_audio( mad_fixed_t const *left, mad_fixed_t const *right,
               char *dst, int len, FSSampleFormat format,
               int src_channels, FSChannelMode dst_mode )
{
     switch (format) {
          case FSSF_U8:
               #define TYPE u8
               #define MUTE 128
               #define CONV FtoU8
               MAD_MIX_LOOP();
               #undef CONV
               #undef MUTE
               #undef TYPE
               break;
               
          case FSSF_S16:
               #define TYPE s16
               #define MUTE 0
               #define CONV FtoS16
               MAD_MIX_LOOP();
               #undef CONV
               #undef MUTE
               #undef TYPE
               break;
               
          case FSSF_S24:
               #define TYPE s24
               #define MUTE (s24){ 0, 0, 0 }
               #define CONV FtoS24
               MAD_MIX_LOOP();
               #undef CONV
               #undef MUTE
               #undef TYPE
               break;
               
          case FSSF_S32:
               #define TYPE s32
               #define MUTE 0
               #define CONV FtoS32
               MAD_MIX_LOOP();
               #undef CONV
               #undef MUTE
               #undef TYPE
               break;
               
          case FSSF_FLOAT:
               #define TYPE float
               #define MUTE 0
               #define CONV FtoF32
               MAD_MIX_LOOP();
               #undef CONV
               #undef MUTE
               #undef TYPE
               break;
               
          default:
               D_BUG( "unexpected sample format" );
               break;
     }
}

/****************************************************************************/

static void
Mad_Stop( IFusionSoundMusicProvider_Mad_data *data, bool now )
{
     data->status = FMSTATE_STOP;

     /* stop thread */
     if (data->thread) {
          if (!direct_thread_is_joined( data->thread )) {
               if (now) {
                    direct_thread_cancel( data->thread );
                    direct_thread_join( data->thread );
               }
               else {
                    /* mutex must be already locked */
                    pthread_mutex_unlock( &data->lock );
                    direct_thread_join( data->thread );
                    pthread_mutex_lock( &data->lock );
               }
          }
          direct_thread_destroy( data->thread );
          data->thread = NULL;
     }

     /* free read buffer */
     if (data->read_buffer) {
          D_FREE( data->read_buffer );
          data->read_buffer  = NULL;
     }

     /* release previous destination stream */
     if (data->dest.stream) {
          data->dest.stream->Release( data->dest.stream );
          data->dest.stream = NULL;
     }

     /* release previous destination buffer */
     if (data->dest.buffer) {
          data->dest.buffer->Release( data->dest.buffer );
          data->dest.buffer = NULL;
     }
}

static void
IFusionSoundMusicProvider_Mad_Destruct( IFusionSoundMusicProvider *thiz )
{
     IFusionSoundMusicProvider_Mad_data *data = thiz->priv;

     Mad_Stop( data, true );

     mad_synth_finish( &data->synth );
     mad_frame_finish( &data->frame );
     mad_stream_finish( &data->stream );

     if (data->s)
          direct_stream_destroy( data->s );

     pthread_cond_destroy( &data->cond );
     pthread_mutex_destroy( &data->lock );

     DIRECT_DEALLOCATE_INTERFACE( thiz );
}

static DirectResult
IFusionSoundMusicProvider_Mad_AddRef( IFusionSoundMusicProvider *thiz )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_Mad )

     data->ref++;

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_Mad_Release( IFusionSoundMusicProvider *thiz )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_Mad )

     if (--data->ref == 0)
          IFusionSoundMusicProvider_Mad_Destruct( thiz );

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_Mad_GetCapabilities( IFusionSoundMusicProvider   *thiz,
                                               FSMusicProviderCapabilities *caps )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_Mad )

     if (!caps)
          return DR_INVARG;

     *caps = FMCAPS_BASIC | FMCAPS_HALFRATE;
     if (direct_stream_seekable( data->s ))
          *caps |= FMCAPS_SEEK;

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_Mad_GetTrackDescription( IFusionSoundMusicProvider *thiz,
                                                   FSTrackDescription        *desc )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_Mad )

     if (!desc)
          return DR_INVARG;

     *desc = data->desc;

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_Mad_GetStreamDescription( IFusionSoundMusicProvider *thiz,
                                                    FSStreamDescription       *desc )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_Mad )

     if (!desc)
          return DR_INVARG;

     desc->flags        = FSSDF_SAMPLERATE   | FSSDF_CHANNELS  |
                          FSSDF_SAMPLEFORMAT | FSSDF_BUFFERSIZE;
     desc->samplerate   = data->samplerate;
     desc->channels     = data->channels;
     desc->sampleformat = FSSF_S32;
     desc->buffersize   = data->samplerate/8;

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_Mad_GetBufferDescription( IFusionSoundMusicProvider *thiz,
                                                    FSBufferDescription       *desc )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_Mad )

     if (!desc)
          return DR_INVARG;

     desc->flags        = FSBDF_SAMPLERATE   | FSBDF_CHANNELS | 
                          FSBDF_SAMPLEFORMAT | FSBDF_LENGTH;
     desc->samplerate   = data->samplerate;
     desc->channels     = data->channels;
     desc->sampleformat = FSSF_S32;
     desc->length       = MIN(data->frames, FS_MAX_FRAMES);

     return DR_OK;
}

static void*
MadStreamThread( DirectThread *thread, void *ctx )
{
     IFusionSoundMusicProvider_Mad_data *data   = ctx;
     IFusionSoundStream                 *stream = data->dest.stream;

     data->stream.next_frame = NULL;

     direct_stream_wait( data->s, data->read_size, NULL );

     while (data->status == FMSTATE_PLAY) {
          DirectResult   ret    = DR_OK;
          unsigned int   len    = data->read_size;
          int            offset = 0;
          struct timeval tv     = { 0, 500 };

          pthread_mutex_lock( &data->lock );

          if (data->status != FMSTATE_PLAY) {
               pthread_mutex_unlock( &data->lock );
               break;
          }

          if (data->seeked) {
               stream->Flush( stream );
               data->seeked = false;
          }

          if (data->stream.next_frame) {
               offset = data->stream.bufend - data->stream.next_frame;
               direct_memmove( data->read_buffer,
                               data->stream.next_frame, offset );
          }

          if (offset < data->read_size) {
               ret = direct_stream_wait( data->s, data->read_size, &tv );
               if (ret != DR_TIMEOUT) {
                    ret = direct_stream_read( data->s,
                                              data->read_size-offset,
                                              data->read_buffer+offset, &len );
               }
          }

          if (ret) {
               if (ret == DR_EOF) {
                    if (data->flags & FMPLAY_LOOPING) {
                         direct_stream_seek( data->s, 0 );
                    }
                    else {
                         data->finished = true;
                         data->status = FMSTATE_FINISHED;
                         pthread_cond_broadcast( &data->cond );
                    }
               }
               pthread_mutex_unlock( &data->lock );
               continue;
          }

          pthread_mutex_unlock( &data->lock );

          mad_stream_buffer( &data->stream, data->read_buffer, len+offset );

          while (data->status == FMSTATE_PLAY && !data->seeked) {
               struct mad_pcm *pcm = &data->synth.pcm;
               int             pos = 0;

               if (mad_frame_decode( &data->frame, &data->stream ) == -1) {
                    if (!MAD_RECOVERABLE(data->stream.error))
                         break;
                    continue;
               }

               mad_synth_frame( &data->synth, &data->frame );
               
               while (pos < pcm->length) {
                    void *dst;
                    int   len;
                    
                    if (stream->Access( stream, &dst, &len ))
                         break;
                         
                    if (len > pcm->length - pos)
                         len = pcm->length - pos;

                    mad_mix_audio( pcm->samples[0]+pos, pcm->samples[1]+pos,
                                   dst, len, data->dest.format, 
                                   pcm->channels, data->dest.mode );
                                   
                    stream->Commit( stream, len );
                    
                    pos += len;
               }
          }
     }

     return NULL;
}

static DirectResult
IFusionSoundMusicProvider_Mad_PlayToStream( IFusionSoundMusicProvider *thiz,
                                            IFusionSoundStream        *destination )
{
     FSStreamDescription desc;

     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_Mad )

     if (!destination)
          return DR_INVARG;
          
     if (data->dest.stream == destination)
          return DR_OK;

     destination->GetDescription( destination, &desc );

     /* check whether destination samplerate is supported */
     if (desc.samplerate != data->samplerate  &&
         desc.samplerate != data->samplerate/2)
          return DR_UNSUPPORTED;

     /* check whether number of channels is supported */
     if (desc.channels > 6)
          return DR_UNSUPPORTED;

     /* check whether destination format is supported */
     switch (desc.sampleformat) {
          case FSSF_U8:
          case FSSF_S16:
          case FSSF_S24:
          case FSSF_S32:
          case FSSF_FLOAT:
               break;
          default:
               return DR_UNSUPPORTED;
     }

     pthread_mutex_lock( &data->lock );

     Mad_Stop( data, false );

     if (desc.samplerate == data->samplerate/2)
          mad_stream_options( &data->stream, MAD_OPTION_IGNORECRC      |
                                             MAD_OPTION_HALFSAMPLERATE );
     else
          mad_stream_options( &data->stream, MAD_OPTION_IGNORECRC );

     /* allocate read buffer */
     data->read_size = (data->desc.bitrate ? : 256000) * PREBUFFER_SIZE / 8;
     data->read_buffer = D_MALLOC( data->read_size );
     if (!data->read_buffer) {
          pthread_mutex_unlock( &data->lock );
          return D_OOM();
     }

     /* reference destination stream */
     destination->AddRef( destination );
     data->dest.stream   = destination;
     data->dest.format   = desc.sampleformat;
     data->dest.mode     = desc.channelmode;
     data->dest.length   = desc.buffersize;

     if (data->finished) {
          direct_stream_seek( data->s, 0 );
          data->finished = false;
     }
          
     data->status = FMSTATE_PLAY;
     pthread_cond_broadcast( &data->cond );

     /* start thread */
     data->thread = direct_thread_create( DTT_DEFAULT, MadStreamThread, data, "Mad" );

     pthread_mutex_unlock( &data->lock );

     return DR_OK;
}

static void*
MadBufferThread( DirectThread *thread, void *ctx )
{
     IFusionSoundMusicProvider_Mad_data *data   = ctx;
     IFusionSoundBuffer                 *buffer = data->dest.buffer;

     int  blocksize = FS_CHANNELS_FOR_MODE(data->dest.mode) *
                      FS_BYTES_PER_SAMPLE(data->dest.format);
     int  written   = 0;

     data->stream.next_frame = NULL;

     direct_stream_wait( data->s, data->read_size, NULL );

     while (data->status == FMSTATE_PLAY) {
          DirectResult   ret    = DR_OK;
          unsigned int   len    = data->read_size;
          int            offset = 0;
          struct timeval tv     = { 0, 500 };
          int            size;

          pthread_mutex_lock( &data->lock );

          if (data->status != FMSTATE_PLAY) {
               pthread_mutex_unlock( &data->lock );
               break;
          }

          data->seeked = false;

          if (data->stream.next_frame) {
               offset = data->stream.bufend - data->stream.next_frame;
               direct_memmove( data->read_buffer,
                               data->stream.next_frame, offset );
          }

          if (offset < data->read_size) {
               ret = direct_stream_wait( data->s, data->read_size, &tv );
               if (ret != DR_TIMEOUT) {
                    ret = direct_stream_read( data->s,
                                              data->read_size-offset,
                                              data->read_buffer+offset, &len );
               }
          }

          if (ret) {
               if (ret == DR_EOF) {
                    if (data->flags & FMPLAY_LOOPING) {
                         direct_stream_seek( data->s, 0 );
                    }
                    else {
                         data->finished = true;
                         data->status = FMSTATE_FINISHED;
                         if (data->callback && written) {
                              if (data->callback( written, data->ctx ))
                                   data->status = FMSTATE_STOP;
                         }
                         pthread_cond_broadcast( &data->cond );
                    }
               }
               pthread_mutex_unlock( &data->lock );
               continue;
          }

          pthread_mutex_unlock( &data->lock );

          mad_stream_buffer( &data->stream, data->read_buffer, len+offset );

          while (data->status == FMSTATE_PLAY && !data->seeked) {
               struct mad_pcm *pcm   = &data->synth.pcm;
               mad_fixed_t    *left  = (mad_fixed_t*)pcm->samples[0];
               mad_fixed_t    *right = (mad_fixed_t*)pcm->samples[1];
               char           *dst;
               int             len, n;

               if (mad_frame_decode( &data->frame, &data->stream ) == -1) {
                    if (!MAD_RECOVERABLE(data->stream.error))
                         break;
                    continue;
               }

               mad_synth_frame( &data->synth, &data->frame );
               len = pcm->length;
               
               if (buffer->Lock( buffer, (void*)&dst, &size, 0 ) != DR_OK) {
                    D_ERROR( "IFusionSoundMusicProvider_Mad: Couldn't lock buffer!\n" );
                    break;
               }

               do {
                    n = MIN( size-written, len );

                    mad_mix_audio( left, right, &dst[written*blocksize], n,
                                   data->dest.format, pcm->channels, data->dest.mode );
                    left    += n;
                    right   += n;
                    len     -= n;
                    written += n;

                    if (written >= size) {
                         if (data->callback) {
                              buffer->Unlock( buffer );
                              if (data->callback( written, data->ctx )) {
                                   data->status = FMSTATE_STOP;
                                   pthread_cond_broadcast( &data->cond );
                                   break;
                              }
                              buffer->Lock( buffer, (void*)&dst, &size, 0 );
                         }
                         written = 0;
                    }
               } while (len > 0);

               buffer->Unlock( buffer );
          }
     }

     return NULL;
}

static DirectResult
IFusionSoundMusicProvider_Mad_PlayToBuffer( IFusionSoundMusicProvider *thiz,
                                            IFusionSoundBuffer        *destination,
                                            FMBufferCallback           callback,
                                            void                      *ctx )
{
     FSBufferDescription desc;

     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_Mad )

     if (!destination)
          return DR_INVARG;
          
     if (data->dest.buffer == destination)
          return DR_OK;

     destination->GetDescription( destination, &desc );

     /* check whether destination samplerate is supported */
     if (desc.samplerate != data->samplerate  &&
         desc.samplerate != data->samplerate/2)
          return DR_UNSUPPORTED;

     /* check whether number of channels is supported */
     if (desc.channels > 6)
          return DR_UNSUPPORTED;

     /* check whether destination format is supported */
     switch (desc.sampleformat) {
          case FSSF_U8:
          case FSSF_S16:
          case FSSF_S24:
          case FSSF_S32:
          case FSSF_FLOAT:
               break;
          default:
               return DR_UNSUPPORTED;
     }
     
     pthread_mutex_lock( &data->lock );
     
     Mad_Stop( data, false );

     if (desc.samplerate == data->samplerate/2)
          mad_stream_options( &data->stream, MAD_OPTION_IGNORECRC      |
                                             MAD_OPTION_HALFSAMPLERATE );
     else
          mad_stream_options( &data->stream, MAD_OPTION_IGNORECRC );

     /* allocate read buffer */
     data->read_size = (data->desc.bitrate ? : 256000) * PREBUFFER_SIZE / 8;
     data->read_buffer = D_MALLOC( data->read_size );
     if (!data->read_buffer) {
          pthread_mutex_unlock( &data->lock );
          return D_OOM();
     }

     /* reference destination stream */
     destination->AddRef( destination );
     data->dest.buffer = destination;
     data->dest.format = desc.sampleformat;
     data->dest.mode   = desc.channelmode;
     data->dest.length = desc.length;

     /* register new callback */
     data->callback = callback;
     data->ctx      = ctx;

     if (data->finished) {
          direct_stream_seek( data->s, 0 );
          data->finished = false;
     }

     data->status = FMSTATE_PLAY;
     pthread_cond_broadcast( &data->cond );

     /* start thread */
     data->thread = direct_thread_create( DTT_DEFAULT, MadBufferThread, data, "Mad" );

     pthread_mutex_unlock( &data->lock );

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_Mad_Stop( IFusionSoundMusicProvider *thiz )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_Mad )

     pthread_mutex_lock( &data->lock );
     
     Mad_Stop( data, false );
     
     pthread_cond_broadcast( &data->cond );

     pthread_mutex_unlock( &data->lock );

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_Mad_GetStatus( IFusionSoundMusicProvider *thiz,
                                         FSMusicProviderStatus     *status )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_Mad )

     if (!status)
          return DR_INVARG;

     *status = data->status;

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_Mad_SeekTo( IFusionSoundMusicProvider *thiz,
                                      double                     seconds )
{
     DirectResult ret  = DR_FAILURE;
     double       rate;
     unsigned int off;

     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_Mad )

     if (seconds < 0.0)
          return DR_INVARG;

     pthread_mutex_lock( &data->lock );

     rate = (data->desc.bitrate ? : data->frame.header.bitrate) >> 3;
     if (rate) {
          off = (seconds*rate);
          ret = direct_stream_seek( data->s, off );
          if (ret == DR_OK) {
               data->seeked   = true;
               data->finished = false;
          }
     }

     pthread_mutex_unlock( &data->lock );

     return ret;
}

static DirectResult
IFusionSoundMusicProvider_Mad_GetPos( IFusionSoundMusicProvider *thiz,
                                      double                    *seconds )
{
     double rate;
     int    pos;

     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_Mad )

     if (!seconds)
          return DR_INVARG;

     if (!data->desc.bitrate)
          return DR_UNSUPPORTED;

     pos = direct_stream_offset( data->s );
     if (data->status == FMSTATE_PLAY && data->stream.this_frame) {
          pos -= data->stream.bufend - data->stream.this_frame;
          pos  = (pos < 0) ? 0 : pos;
     }

     rate = (data->desc.bitrate ? : data->frame.header.bitrate) >> 3;
     *seconds = (double)pos / rate;

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_Mad_GetLength( IFusionSoundMusicProvider *thiz,
                                         double                    *seconds )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_Mad )

     if (!seconds)
          return DR_INVARG;

     *seconds = data->length;

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_Mad_SetPlaybackFlags( IFusionSoundMusicProvider    *thiz,
                                                FSMusicProviderPlaybackFlags  flags )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_Mad )

     if (flags & ~FMPLAY_LOOPING)
          return DR_UNSUPPORTED;

     if (flags & FMPLAY_LOOPING && !direct_stream_seekable( data->s ))
          return DR_UNSUPPORTED;

     data->flags = flags;

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_Mad_WaitStatus( IFusionSoundMusicProvider *thiz,
                                          FSMusicProviderStatus      mask,
                                          unsigned int               timeout )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_Mad )
     
     if (!mask || mask & ~FMSTATE_ALL)
          return DR_INVARG;
          
     if (timeout) {
          struct timespec t;
          long long       s;
          
          s = direct_clock_get_abs_micros() + timeout * 1000ll;
          t.tv_sec  = (s / 1000000);
          t.tv_nsec = (s % 1000000) * 1000;
          
#ifdef HAVE_PTHREAD_MUTEX_TIMEDLOCK
          if (pthread_mutex_timedlock( &data->lock, &t ))
               return DR_TIMEOUT;
#else          
          while (pthread_mutex_trylock( &data->lock )) {
               usleep( 1000 );
               if (direct_clock_get_abs_micros() >= s)
                    return DR_TIMEOUT;
          }
#endif   
          while (!(data->status & mask)) {
               if (pthread_cond_timedwait( &data->cond, &data->lock, &t ) == ETIMEDOUT) {
                    pthread_mutex_unlock( &data->lock );
                    return DR_TIMEOUT;
               }
          }
     }
     else {
          pthread_mutex_lock( &data->lock );
          
          while (!(data->status & mask))
               pthread_cond_wait( &data->cond, &data->lock );
     }
     
     pthread_mutex_unlock( &data->lock );
     
     return DR_OK;
}

/* exported symbols */

static DirectResult
Probe( IFusionSoundMusicProvider_ProbeContext *ctx )
{
     char *ext;

     if (ctx->mimetype && !strcmp( ctx->mimetype, "audio/mpeg" ))
          return DR_OK;

     ext = strrchr( ctx->filename, '.' );
     if (ext) {
          if (!strcasecmp( ext, ".mp1" ) ||
              !strcasecmp( ext, ".mp2" ) ||
              !strcasecmp( ext, ".mp3" ))
               return DR_OK;
     }

     /* Detect by contents ? */

     return DR_UNSUPPORTED;
}

static DirectResult
Construct( IFusionSoundMusicProvider *thiz,
           const char                *filename,
           DirectStream              *stream )
{
     DirectResult       ret;
     u8                 buf[16384];
     unsigned int       pos = 0;
     unsigned int       len;
     unsigned int       size;
     struct mad_header  header;
     int                error  = 1;
     const char        *version;
     struct id3_tag     id3;
     int                i;

     DIRECT_ALLOCATE_INTERFACE_DATA( thiz, IFusionSoundMusicProvider_Mad )

     data->ref    = 1;
     data->s      = direct_stream_dup( stream );
     data->status = FMSTATE_STOP;

     size = direct_stream_length( data->s );

     mad_stream_init( &data->stream );
     mad_frame_init( &data->frame );
     mad_synth_init( &data->synth );
     mad_stream_options( &data->stream, MAD_OPTION_IGNORECRC );

     /* find first valid frame */
     for (i = 0; i < 100; i++) {
          if (data->stream.error == MAD_ERROR_BUFLEN ||
              data->stream.error == MAD_ERROR_BUFPTR || i == 0)
          {
               direct_stream_wait( data->s, sizeof(buf), NULL );
               
               ret = direct_stream_peek( data->s, sizeof(buf), pos, buf, &len );
               if (ret) {
                    IFusionSoundMusicProvider_Mad_Destruct( thiz );
                    return ret;
               }
               pos += len;
               
               mad_stream_buffer( &data->stream, buf, len );
          }
          
          error = mad_frame_decode( &data->frame, &data->stream );
          if (!error) {
               /* get number of frames from Xing headers */
               if (data->stream.anc_bitlen >= 128 &&
                   mad_bit_read( &data->stream.anc_ptr, 32 ) == XING_MAGIC)
               {
                    D_DEBUG( "IFusionSoundMusicProvider_Mad: Found Xing header.\n" );
                    if (mad_bit_read( &data->stream.anc_ptr, 32 ) & 1)
                         data->frames = mad_bit_read( &data->stream.anc_ptr, 32 );
               }
               break;
          }
     }

     if (error) {
          D_ERROR( "IFusionSoundMusicProvider_Mad: Couldn't find a valid frame!\n" );
          IFusionSoundMusicProvider_Mad_Destruct( thiz );
          return DR_FAILURE;
     }

     header           = data->frame.header;
     data->samplerate = header.samplerate;
     data->channels   = MAD_NCHANNELS( &header );

     /* get ID3 tag */
     if (direct_stream_seekable( data->s ) && !direct_stream_remote( data->s )) {
          direct_stream_peek( data->s, sizeof(id3),
                              direct_stream_length( data->s ) - sizeof(id3),
                              &id3, NULL );

          if (!strncmp( (char*)id3.tag, "TAG", 3 )) {
               size -= sizeof(id3);

               strncpy( data->desc.artist, (char*)id3.artist,
                        MIN( FS_TRACK_DESC_ARTIST_LENGTH-1, sizeof(id3.artist) ) );
               strncpy( data->desc.title, (char*)id3.title,
                        MIN( FS_TRACK_DESC_TITLE_LENGTH-1, sizeof(id3.title) ) );
               strncpy( data->desc.album, (char*)id3.album,
                        MIN( FS_TRACK_DESC_ALBUM_LENGTH-1, sizeof(id3.album) ) );
               data->desc.year = strtol( (char*)id3.year, NULL, 10 );

               if (id3.genre < D_ARRAY_SIZE( id3_genres )) {
                    const char *genre = id3_genres[(int)id3.genre];
                    strncpy( data->desc.genre, genre,
                             MIN( FS_TRACK_DESC_GENRE_LENGTH-1, strlen(genre) ) );
               }
          }
     }

     switch (header.flags & (MAD_FLAG_MPEG_2_5_EXT | MAD_FLAG_LSF_EXT)) {
          case (MAD_FLAG_MPEG_2_5_EXT | MAD_FLAG_LSF_EXT):
               version = "2.5";
               break;
          case MAD_FLAG_LSF_EXT:
               version = "2";
               break;
          default:
               version = "1";
               break;
     }

     if (data->frames) {
          /* compute avarage bitrate for VBR stream */
          switch (header.layer) {
               case MAD_LAYER_I:
                    data->frames *= 384;
                    break;
               case MAD_LAYER_II:
                    data->frames *= 1152;
                    break;
               case MAD_LAYER_III:
               default:
                    if (header.flags & (MAD_FLAG_LSF_EXT | MAD_FLAG_MPEG_2_5_EXT))
                         data->frames *= 576;
                    else
                         data->frames *= 1152;
                    break;
          }

          data->length = (double)data->frames / header.samplerate;
          data->desc.bitrate = (double)size * 8 / data->length;

          snprintf( data->desc.encoding,
                    FS_TRACK_DESC_ENCODING_LENGTH,
                    "MPEG-%s Layer %d (VBR)", version, header.layer );
     }
     else {
          if (header.bitrate < 8000)
               header.bitrate = 8000;

          data->length = (double)size * 8 / header.bitrate;
          data->frames = ceil(data->length * header.samplerate);
          data->desc.bitrate = header.bitrate;

          snprintf( data->desc.encoding,
                    FS_TRACK_DESC_ENCODING_LENGTH,
                    "MPEG-%s Layer %d", version, header.layer );
     }

     direct_util_recursive_pthread_mutex_init( &data->lock );
     pthread_cond_init( &data->cond, NULL );

     /* initialize function pointers */
     thiz->AddRef               = IFusionSoundMusicProvider_Mad_AddRef;
     thiz->Release              = IFusionSoundMusicProvider_Mad_Release;
     thiz->GetCapabilities      = IFusionSoundMusicProvider_Mad_GetCapabilities;
     thiz->GetTrackDescription  = IFusionSoundMusicProvider_Mad_GetTrackDescription;
     thiz->GetStreamDescription = IFusionSoundMusicProvider_Mad_GetStreamDescription;
     thiz->GetBufferDescription = IFusionSoundMusicProvider_Mad_GetBufferDescription;
     thiz->PlayToStream         = IFusionSoundMusicProvider_Mad_PlayToStream;
     thiz->PlayToBuffer         = IFusionSoundMusicProvider_Mad_PlayToBuffer;
     thiz->Stop                 = IFusionSoundMusicProvider_Mad_Stop;
     thiz->GetStatus            = IFusionSoundMusicProvider_Mad_GetStatus;
     thiz->SeekTo               = IFusionSoundMusicProvider_Mad_SeekTo;
     thiz->GetPos               = IFusionSoundMusicProvider_Mad_GetPos;
     thiz->GetLength            = IFusionSoundMusicProvider_Mad_GetLength;
     thiz->SetPlaybackFlags     = IFusionSoundMusicProvider_Mad_SetPlaybackFlags;
     thiz->WaitStatus           = IFusionSoundMusicProvider_Mad_WaitStatus;

     return DR_OK;
}

