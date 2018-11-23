/*
 * Copyright (C) 2005-2008 Claudio Ciccani <klan@users.sf.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>

#if defined(__linux__)
#include <asm/types.h>
#endif

#ifdef HAVE_CDDB
# include <cddb/cddb.h>
#endif

#include <fusionsound.h>
#include <fusionsound_limits.h>

#include <media/ifusionsoundmusicprovider.h>

#include <direct/types.h>
#include <direct/clock.h>
#include <direct/mem.h>
#include <direct/thread.h>
#include <direct/util.h>

static DirectResult
Probe( IFusionSoundMusicProvider_ProbeContext *ctx );

static DirectResult
Construct( IFusionSoundMusicProvider *thiz,
           const char                *filename,
           DirectStream              *stream );

#include <direct/interface_implementation.h>

DIRECT_INTERFACE_IMPLEMENTATION( IFusionSoundMusicProvider, CDDA )


struct cdda_track {
     unsigned int  start;  /* first frame   */
     unsigned int  length; /* total frames  */
     unsigned int  frame;  /* current frame */

     char         *artist;
     char         *title;
     char         *genre;
     char         *album;
     short         year;
};

/*
 * private data struct of IFusionSoundMusicProvider
 */
typedef struct {
     int                           ref;        /* reference counter */

     int                           fd;

     unsigned int                  current_track;
     unsigned int                  total_tracks;
     struct cdda_track            *tracks;

     FSMusicProviderStatus         status;
     FSMusicProviderPlaybackFlags  flags;

     DirectThread                 *thread;
     pthread_mutex_t               lock;
     pthread_cond_t                cond;
     int                           seeked;

     int                           buffered_frames;
     void                         *buffer;

     struct {
          IFusionSoundStream      *stream;
          IFusionSoundBuffer      *buffer;
          FSSampleFormat           format;
          int                      channels;
          int                      length;
     } dest;

     FMBufferCallback              callback;
     void                         *ctx;
} IFusionSoundMusicProvider_CDDA_data;

/*****************************************************************************/

#define CD_FRAMES_PER_SECOND  75
#define CD_BYTES_PER_FRAME    2352

#if defined(__linux__)

#include <linux/cdrom.h>

static DirectResult
cdda_probe( int fd )
{
     struct cdrom_tochdr tochdr;

     if (ioctl( fd, CDROM_DRIVE_STATUS, CDSL_CURRENT ) != CDS_DISC_OK)
          return DR_UNSUPPORTED;

     if (ioctl( fd, CDROMREADTOCHDR, &tochdr ) < 0)
          return DR_UNSUPPORTED;

     return DR_OK;
}

static DirectResult
cdda_build_tracklits( int fd, struct cdda_track **ret_tracks,
                              unsigned int       *ret_num )
{
     int                        total_tracks;
     struct cdda_track         *tracks, *track;
     struct cdrom_tochdr        tochdr;
     struct cdrom_tocentry      tocentry;
     struct cdrom_multisession  ms;
     struct cdrom_msf0          msf;
     int                        i;

     /* read TOC header */
     if (ioctl( fd, CDROMREADTOCHDR, &tochdr ) == -1) {
          D_PERROR( "IFusionSoundMusicProvide_CDDA: "
                    "ioctl( CDROMREADTOCHDR ) failed!\n" );
          return DR_IO;
     }

     ms.addr_format = CDROM_LBA;
     if (ioctl( fd, CDROMMULTISESSION, &ms ) == -1) {
          D_PERROR( "IFusionSoundMusicProvide_CDDA: "
                    "ioctl( CDROMMULTISESSION ) failed!\n" );
          return DR_IO;
     }

     total_tracks = tochdr.cdth_trk1 - tochdr.cdth_trk0 + 1;

     tracks = D_CALLOC( total_tracks+1, sizeof(struct cdda_track) );
     if (!tracks)
          return D_OOM();

     /* iterate through tracks */
     for (i = tochdr.cdth_trk0, track = tracks; i <= tochdr.cdth_trk1; i++) {
          memset( &tocentry, 0, sizeof(tocentry) );

          tocentry.cdte_track  = i;
          tocentry.cdte_format = CDROM_MSF;
          if (ioctl( fd, CDROMREADTOCENTRY, &tocentry ) == -1) {
               D_PERROR( "IFusionSoundMusicProvide_CDDA: "
                         "ioctl( CDROMREADTOCENTRY ) failed!\n" );
               D_FREE( tracks );
               return DR_IO;
          }

          /* skip data tracks */
          if (tocentry.cdte_ctrl & 0x04) {
               total_tracks--;
               continue;
          }

          msf = tocentry.cdte_addr.msf;
          track->start = (msf.minute * 60 * CD_FRAMES_PER_SECOND) +
                         (msf.second      * CD_FRAMES_PER_SECOND) +
                          msf.frame;
          track++;
     }

     if (total_tracks < 1) {
          D_FREE( tracks );
          return DR_FAILURE;
     }

     memset( &tocentry, 0, sizeof(tocentry) );

     /* get leadout track */
     tocentry.cdte_track  = 0xaa;
     tocentry.cdte_format = CDROM_MSF;
     if (ioctl( fd, CDROMREADTOCENTRY, &tocentry ) == -1) {
          D_PERROR( "IFusionSoundMusicProvide_CDDA: "
                    "ioctl( CDROMREADTOCENTRY ) failed!\n" );
          D_FREE( tracks );
          return DR_IO;
     }

     if (!ms.xa_flag) {
          msf = tocentry.cdte_addr.msf;
          tracks[total_tracks].start = (msf.minute * 60 * CD_FRAMES_PER_SECOND) +
                                       (msf.second      * CD_FRAMES_PER_SECOND) +
                                        msf.frame;
     } else {
          tracks[total_tracks].start = ms.addr.lba - ((60 + 90 + 2) * CD_FRAMES) + 150;
     }

     /* compute tracks' length */
     for (i = 0; i < total_tracks; i++)
          tracks[i].length = tracks[i+1].start - tracks[i].start;

     *ret_tracks = tracks;
     *ret_num    = total_tracks;

     return DR_OK;
}

static int
cdda_read_audio( int fd, u8 *buf, int pos, int len )
{
     struct cdrom_read_audio ra;

     ra.addr.msf.minute =  pos / (60 * CD_FRAMES_PER_SECOND);
     ra.addr.msf.second = (pos / CD_FRAMES_PER_SECOND) % 60;
     ra.addr.msf.frame  =  pos % CD_FRAMES_PER_SECOND;
     ra.addr_format     = CDROM_MSF;
     ra.nframes         = len;
     ra.buf             = buf;

     if (ioctl( fd, CDROMREADAUDIO, &ra ) < 0)
          return -1;

     return ra.nframes;
}

#elif defined(__FreeBSD__)

#include <sys/cdio.h>

static DirectResult
cdda_probe( int fd )
{
     struct ioc_toc_header tochdr;

     if (ioctl( fd, CDIOREADTOCHEADER, &tochdr ) < 0)
          return DR_UNSUPPORTED;

     return DR_OK;
}

static DirectResult
cdda_build_tracklits( int fd, struct cdda_track **ret_tracks,
                              unsigned int       *ret_num )
{
     int                               total_tracks;
     struct cdda_track                *tracks, *track;
     struct ioc_toc_header             tochdr;
     struct ioc_read_toc_single_entry  tocentry;
     union msf_lba                     msf_lba;
     int                               i;

     /* read TOC header */
     if (ioctl( fd, CDIOREADTOCHEADER, &tochdr ) == -1) {
          D_PERROR( "IFusionSoundMusicProvide_CDDA: "
                    "ioctl( CDIOREADTOCHEADER ) failed!\n" );
          return DR_IO;
     }

     total_tracks = tochdr.ending_track - tochdr.starting_track + 1;

     tracks = D_CALLOC( total_tracks+1, sizeof(struct cdda_track) );
     if (!tracks)
          return D_OOM();

     /* iterate through tracks */
     for (i = tochdr.starting_track, track = tracks; i <= tochdr.ending_track; i++) {
          memset( &tocentry, 0, sizeof(tocentry) );

          tocentry.track          = i;
          tocentry.address_format = CD_MSF_FORMAT;
          if (ioctl( fd, CDIOREADTOCENTRY, &tocentry ) == -1) {
               D_PERROR( "IFusionSoundMusicProvide_CDDA: "
                         "ioctl( CDIOREADTOCENTRY ) failed!\n" );
               D_FREE( tracks );
               return DR_IO;
          }

          /* skip data tracks */
          if (tocentry.entry.control & 0x04) {
               total_tracks--;
               continue;
          }

          msf_lba = tocentry.entry.addr;
          track->start = (msf_lba.msf.minute * 60 * CD_FRAMES_PER_SECOND) +
                         (msf_lba.msf.second      * CD_FRAMES_PER_SECOND) +
                          msf_lba.msf.frame;
          track++;
     }

     if (total_tracks < 1) {
          D_FREE( tracks );
          return DR_FAILURE;
     }

     memset( &tocentry, 0, sizeof(tocentry) );

     /* get leadout track */
     tocentry.track          = 0xaa;
     tocentry.address_format = CD_MSF_FORMAT;
     if (ioctl( fd, CDIOREADTOCENTRY, &tocentry ) == -1) {
          D_PERROR( "IFusionSoundMusicProvide_CDDA: "
                    "ioctl( CDIOREADTOCENTRY ) failed!\n" );
          D_FREE( tracks );
          return DR_IO;
     }

     msf_lba = tocentry.entry.addr;
     tracks[total_tracks].start = (msf_lba.msf.minute * 60 * CD_FRAMES_PER_SECOND) +
                                  (msf_lba.msf.second      * CD_FRAMES_PER_SECOND) +
                                   msf_lba.msf.frame;

     /* compute tracks' length */
     for (i = 0; i < total_tracks; i++)
          tracks[i].length = tracks[i+1].start - tracks[i].start;

     *ret_tracks = tracks;
     *ret_num    = total_tracks;

     return DR_OK;
}

static int
cdda_read_audio( int fd, u8 *buf, int pos, int len )
{
     struct ioc_read_audio ra;

     ra.address.msf.minute =  pos / (60 * CD_FRAMES_PER_SECOND);
     ra.address.msf.second = (pos / CD_FRAMES_PER_SECOND) % 60;
     ra.address.msf.frame  =  pos % CD_FRAMES_PER_SECOND;
     ra.address_format     = CD_MSF_FORMAT;
     ra.nframes            = len;
     ra.buffer             = buf;

     if (ioctl( fd, CDIOCREADAUDIO, &ra ) < 0)
          return -1;

     return ra.nframes;
}

#else

static DirectResult
cdda_probe( int fd )
{
     D_WARN( "unsupported system" );
     return DR_UNSUPPORTED;
}

static DirectResult
cdda_build_tracklits( int fd, struct cdda_track **ret_tracks,
                              unsigned int       *ret_num )
{
     return DR_UNSUPPORTED;
}

static int
cdda_read_audio( int fd, u8 *buf, int pos, int frames )
{
     return -1;
}

#endif

/*****************************************************************************/

#ifdef HAVE_CDDB
static void
cdda_get_metadata( struct cdda_track *tracks,
                   unsigned int       total_tracks )
{
     const char   *cddb_cats[] = { "blues", "classical", "country", "data",
                                   "folk", "jazz", "misc", "newage", "reggae",
                                   "rock", "soundtrack" };
     cddb_conn_t  *conn;
     cddb_disc_t  *disc;
     cddb_track_t *track;
     unsigned int  disclen;
     unsigned int  discid  = 0;
     int           i;

     /* init libcddb */
     libcddb_init();

     /* create a new connection */
     conn = cddb_new();
     if (!conn)
          return;
     /* suppress messages */
     cddb_log_set_level( CDDB_LOG_NONE );
     /* set timeout to 10 seconds */
     cddb_set_timeout( conn, 10 );

     /* compute disc length */
     disclen = tracks[total_tracks].start/CD_FRAMES_PER_SECOND -
               tracks[0].start/CD_FRAMES_PER_SECOND;

     /* compute disc id */
     for (i = 0; i < total_tracks; i++) {
          unsigned int start = tracks[i].start/CD_FRAMES_PER_SECOND;

          while (start) {
               discid += start % 10;
               start  /= 10;
          }
     }
     discid = ((discid % 0xff) << 24) | (disclen << 8) | total_tracks;

     D_DEBUG( "IFusionSoundMusicProvider_CDDA: CDDB Disc ID = 0x%08x.\n", discid );

     /* create a new disc */
     disc = cddb_disc_new();
     if (!disc) {
          cddb_destroy( conn );
          return;
     }

     /* set disc id */
     cddb_disc_set_discid( disc, discid );

     /* search through categories */
     for (i = 0; i < D_ARRAY_SIZE( cddb_cats ); i++) {
          cddb_disc_set_category_str( disc, cddb_cats[i] );

          /* retrieve informations from the server */
          if (cddb_read( conn, disc )) {
               const char *artist;
               const char *title;
               const char *genre  = cddb_disc_get_genre( disc );
               const char *album  = cddb_disc_get_title( disc );
               short       year   = cddb_disc_get_year ( disc );

               /* iterate through tracks */
               for (track = cddb_disc_get_track_first( disc );
                    track != NULL;
                    track = cddb_disc_get_track_next( disc ))
               {
                    i = cddb_track_get_number( track ) - 1;

                    if (i < total_tracks) {
                         artist = cddb_track_get_artist( track );
                         title  = cddb_track_get_title( track );

                         if (artist)
                              tracks[i].artist = D_STRDUP( artist );
                         if (title)
                              tracks[i].title  = D_STRDUP( title );
                         if (genre)
                              tracks[i].genre  = D_STRDUP( genre );
                         if (album)
                              tracks[i].album  = D_STRDUP( album );
                         tracks[i].year = year;
                    }
               }

               break;
          }
     }

     /* release resources */
     cddb_disc_destroy( disc );
     cddb_destroy( conn );
     libcddb_shutdown();
}
#endif

/*****************************************************************************/

static void
cdda_mix_audio( s16 *src, u8 *dst, int len,
                FSSampleFormat format, int channels )
{
     int i;

     switch (format) {
          case FSSF_U8:
               if (channels == 1) {
                    for (i = 0; i < len*2; i += 2) {
                         *dst = ((src[i] + src[i+1]) >> 9) + 128;
                         dst++;
                    }
               } else {
                    for (i = 0; i < len*2; i++)
                         dst[i] = (src[i] >> 8) + 128;
               }
               break;
          case FSSF_S16:
               if (channels == 1) {
                    for (i = 0; i < len*2; i += 2) {
                         *((s16*)dst) = (src[i] + src[i+1]) >> 1;
                         dst += 2;
                    }
               }
               /* no conversion needed */
               break;
          case FSSF_S24:
               if (channels == 1) {
                    for (i = 0; i < len*2; i += 2) {
                         int d = (src[i] + src[i+1]) << 7;
#ifdef WORDS_BIGENDIAN
                         dst[0] = d >> 16;
                         dst[1] = d >> 8;
                         dst[2] = d;
#else
                         dst[0] = d;
                         dst[1] = d >> 8;
                         dst[2] = d >> 16;
#endif
                         dst += 3;
                    }
               } else {
                    for (i = 0; i < len*2; i++) {
                         int d = src[i] << 8;
#ifdef WORDS_BIGENDIAN
                         dst[0] = d >> 16;
                         dst[1] = d >> 8;
                         dst[2] = d;
#else
                         dst[0] = d;
                         dst[1] = d >> 8;
                         dst[2] = d >> 16;
#endif
                         dst += 3;
                    }
               }
               break;
          case FSSF_S32:
               if (channels == 1) {
                    for (i = 0; i < len*2; i += 2) {
                         *((s32*)dst) = (src[i] + src[i+1]) << 15;
                         dst += 4;
                    }
               } else {
                    for (i = 0; i < len*2; i++)
                         ((s32*)dst)[i] = src[i] << 16;
               }
               break;
          case FSSF_FLOAT:
               if (channels == 1) {
                    for (i = 0; i < len*2; i += 2) {
                         *((float*)dst) = (float)((src[i]+src[i+1])>>1)/32768.0f;
                         dst += sizeof(float);
                    }
               } else {
                    for (i = 0; i < len*2; i++)
                         ((float*)dst)[i] = (float)src[i]/32768.0f;
               }
               break;
          default:
               D_BUG( "unexpected sampleformat" );
               break;
     }
}

/*****************************************************************************/

static void
CDDA_Stop( IFusionSoundMusicProvider_CDDA_data *data, bool now )
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

     /* release buffer */
     if (data->buffer) {
          D_FREE( data->buffer );
          data->buffer = NULL;
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
IFusionSoundMusicProvider_CDDA_Destruct( IFusionSoundMusicProvider *thiz )
{
     IFusionSoundMusicProvider_CDDA_data *data = thiz->priv;

     CDDA_Stop( data, true );

     if (data->tracks) {
          int i;

          for (i = 0; i < data->total_tracks; i++) {
               if (data->tracks[i].artist)
                    D_FREE( data->tracks[i].artist );
               if (data->tracks[i].title)
                    D_FREE( data->tracks[i].title );
               if (data->tracks[i].genre)
                    D_FREE( data->tracks[i].genre );
               if (data->tracks[i].album)
                    D_FREE( data->tracks[i].album );
          }

          D_FREE( data->tracks );
     }

     if (data->fd > 0)
          close( data->fd );

     pthread_cond_destroy( &data->cond );
     pthread_mutex_destroy( &data->lock );

     DIRECT_DEALLOCATE_INTERFACE( thiz );
}

static DirectResult
IFusionSoundMusicProvider_CDDA_AddRef( IFusionSoundMusicProvider *thiz )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_CDDA )

     data->ref++;

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_CDDA_Release( IFusionSoundMusicProvider *thiz )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_CDDA )

     if (--data->ref == 0)
          IFusionSoundMusicProvider_CDDA_Destruct( thiz );

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_CDDA_GetCapabilities( IFusionSoundMusicProvider   *thiz,
                                                FSMusicProviderCapabilities *caps )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_CDDA )

     if (!caps)
          return DR_INVARG;

     *caps = FMCAPS_BASIC | FMCAPS_SEEK;

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_CDDA_EnumTracks( IFusionSoundMusicProvider *thiz,
                                           FSTrackCallback            callback,
                                           void                      *callbackdata )
{
     FSTrackDescription desc;
     int                i;

     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_CDDA )

     if (!callback)
          return DR_INVARG;

     for (i = 0; i < data->total_tracks; i++) {
          struct cdda_track *track = &data->tracks[i];

          memset( &desc, 0, sizeof(desc) );

          if (track->artist)
               direct_snputs( desc.artist, track->artist, FS_TRACK_DESC_ARTIST_LENGTH );
          if (track->title)
               direct_snputs( desc.title, track->title, FS_TRACK_DESC_TITLE_LENGTH );
          if (track->genre)
               direct_snputs( desc.genre, track->genre, FS_TRACK_DESC_GENRE_LENGTH );
          if (track->album)
               direct_snputs( desc.album, track->album, FS_TRACK_DESC_ALBUM_LENGTH );
          desc.year = track->year;

          direct_snputs( desc.encoding, "PCM 16 bit", FS_TRACK_DESC_ENCODING_LENGTH );
          desc.bitrate = CD_FRAMES_PER_SECOND * CD_BYTES_PER_FRAME * 8;

          if (callback( i, desc, callbackdata ))
               break;
     }

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_CDDA_GetTrackID( IFusionSoundMusicProvider *thiz,
                                           FSTrackID                 *ret_track_id )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_CDDA )

     if (!ret_track_id)
          return DR_INVARG;

     *ret_track_id = data->current_track;

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_CDDA_GetTrackDescription( IFusionSoundMusicProvider *thiz,
                                                    FSTrackDescription        *desc )
{
     struct cdda_track *track;

     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_CDDA )

     if (!desc)
          return DR_INVARG;

     memset( desc, 0, sizeof(FSTrackDescription) );

     track = &data->tracks[data->current_track];

     if (track->artist)
          direct_snputs( desc->artist, track->artist, FS_TRACK_DESC_ARTIST_LENGTH );
     if (track->title)
          direct_snputs( desc->title, track->title, FS_TRACK_DESC_TITLE_LENGTH );
     if (track->genre)
          direct_snputs( desc->genre, track->genre, FS_TRACK_DESC_GENRE_LENGTH );
     if (track->album)
          direct_snputs( desc->album, track->album, FS_TRACK_DESC_ALBUM_LENGTH );
     desc->year = track->year;

     direct_snputs( desc->encoding, "PCM 16 bit", FS_TRACK_DESC_ENCODING_LENGTH );
     desc->bitrate = CD_FRAMES_PER_SECOND * CD_BYTES_PER_FRAME * 8;

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_CDDA_GetStreamDescription( IFusionSoundMusicProvider *thiz,
                                                     FSStreamDescription       *desc )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_CDDA )

     if (!desc)
          return DR_INVARG;

     desc->flags        = FSSDF_SAMPLERATE   | FSSDF_CHANNELS  |
                          FSSDF_SAMPLEFORMAT | FSSDF_BUFFERSIZE;
     desc->samplerate   = 44100;
     desc->channels     = 2;
     desc->sampleformat = FSSF_S16;
     desc->buffersize   = 4704;

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_CDDA_GetBufferDescription( IFusionSoundMusicProvider *thiz,
                                                     FSBufferDescription       *desc )
{
     struct cdda_track *track;
     
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_CDDA )

     if (!desc)
          return DR_INVARG;
          
     track = &data->tracks[data->current_track];

     desc->flags        = FSBDF_LENGTH       | FSBDF_CHANNELS  |
                          FSBDF_SAMPLEFORMAT | FSBDF_SAMPLERATE;
     desc->samplerate   = 44100;
     desc->channels     = 2;
     desc->sampleformat = FSSF_S16;
     desc->length       = track->length * CD_BYTES_PER_FRAME / 4;
     if (desc->length > FS_MAX_FRAMES)
          desc->length = FS_MAX_FRAMES;

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_CDDA_SelectTrack( IFusionSoundMusicProvider *thiz,
                                            FSTrackID                  track_id )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_CDDA )

     if (track_id > data->total_tracks)
          return DR_INVARG;

     pthread_mutex_lock( &data->lock );
     data->current_track = track_id;
     pthread_mutex_unlock( &data->lock );

     return DR_OK;
}

static void*
CDDAStreamThread( DirectThread *thread, void *ctx )
{
     IFusionSoundMusicProvider_CDDA_data *data = ctx;

     while (data->status == FMSTATE_PLAY) {
          void *src = data->buffer;
          int   len, pos;

          pthread_mutex_lock( &data->lock );

          if (data->status != FMSTATE_PLAY) {
               pthread_mutex_unlock( &data->lock );
               break;
          }
          
          if (data->seeked) {
               data->dest.stream->Flush( data->dest.stream );
               data->seeked = false;
          }

          pos = data->tracks[data->current_track].frame;
          len = MIN( data->buffered_frames,
                     data->tracks[data->current_track].length - pos );
          pos += data->tracks[data->current_track].start;

          if (len > 0) {
               if (!data->buffer) {
                    /* direct copy */
                    int avail = 0;
                    
                    data->dest.stream->Access( data->dest.stream, &src, &avail );
                    avail = (avail << 2) / CD_BYTES_PER_FRAME;
                    if (len > avail)
                         len = avail;
               }
               
               len = cdda_read_audio( data->fd, src, pos, len );
               len = len * CD_BYTES_PER_FRAME / 4;
               
               if (!data->buffer) {
                    /* direct copy */
                    data->dest.stream->Commit( data->dest.stream, MAX(len,0) );
               }
          }              

          if (len < 1) {
               if (data->flags & FMPLAY_LOOPING) {
                    data->tracks[data->current_track].frame = 0;
               }
               else {
                    data->status = FMSTATE_FINISHED;
                    pthread_cond_broadcast( &data->cond );
               }
               pthread_mutex_unlock( &data->lock );
               continue;
          }

          data->tracks[data->current_track].frame += len;

          pthread_mutex_unlock( &data->lock );
          
          if (data->buffer) {
               void *dst;
               int   num;
               
               while (len) {
                    if (data->dest.stream->Access( data->dest.stream, &dst, &num ))
                         break;

                    num = MIN( num, len );
                    cdda_mix_audio( src, dst, num, data->dest.format, data->dest.channels);

                    data->dest.stream->Commit( data->dest.stream, num );
                    
                    len -= num;
                    src += num << 2;
               }
          }
          else {
               /* Avoid blocking while the mutex is locked. */
               data->dest.stream->Wait( data->dest.stream, 1 );
          }
     }

     return NULL;
}

static DirectResult
IFusionSoundMusicProvider_CDDA_PlayToStream( IFusionSoundMusicProvider *thiz,
                                             IFusionSoundStream        *destination )
{
     FSStreamDescription  desc;
     struct cdda_track   *track;

     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_CDDA )

     if (!destination)
          return DR_INVARG;

     destination->GetDescription( destination, &desc );

     /* check whether destination samplerate is supported */
     if (desc.samplerate != 44100)
          return DR_UNSUPPORTED;

     /* check whether number of channels is supported */
     if (desc.channels > 2)
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

     /* check destination buffer size */
     if (desc.buffersize < CD_BYTES_PER_FRAME/4)
          return DR_UNSUPPORTED;

     pthread_mutex_lock( &data->lock );
     
     CDDA_Stop( data, false );

     data->buffered_frames = (desc.buffersize << 2) / CD_BYTES_PER_FRAME;

     if (desc.sampleformat != FSSF_S16 || desc.channels != 2) {
          data->buffer = D_MALLOC( data->buffered_frames * CD_BYTES_PER_FRAME );
          if (!data->buffer) {
               pthread_mutex_unlock( &data->lock );
               return D_OOM();
          }
     }

     /* reference destination stream */
     destination->AddRef( destination );
     data->dest.stream   = destination;
     data->dest.format   = desc.sampleformat;
     data->dest.channels = desc.channels;
     data->dest.length   = desc.buffersize;

     track = &data->tracks[data->current_track];
     if (track->frame == track->length)
          track->frame = 0;
          
     data->status = FMSTATE_PLAY;
     pthread_cond_broadcast( &data->cond );

     /* start thread */
     data->thread = direct_thread_create( DTT_DEFAULT, CDDAStreamThread, data, "CDDA" );

     pthread_mutex_unlock( &data->lock );

     return DR_OK;
}

static void*
CDDABufferThread( DirectThread *thread, void *ctx )
{
     IFusionSoundMusicProvider_CDDA_data *data =
          (IFusionSoundMusicProvider_CDDA_data*) ctx;

     IFusionSoundBuffer *buffer = data->dest.buffer;

     while (data->status == FMSTATE_PLAY) {
          int  len, pos;
          u8  *dst;
          int  size;

          pthread_mutex_lock( &data->lock );

          if (data->status != FMSTATE_PLAY) {
               pthread_mutex_unlock( &data->lock );
               break;
          }

          pos = data->tracks[data->current_track].frame;
          len = MIN( data->buffered_frames,
                     data->tracks[data->current_track].length - pos );
          pos += data->tracks[data->current_track].start;

          if (buffer->Lock( buffer, (void*)&dst, &size, 0 ) != DR_OK) {
               D_ERROR( "IFusionSoundMusicProvider_CDDA: "
                        "Couldn't lock destination buffer!\n" );
               data->status = FMSTATE_FINISHED;
               pthread_cond_broadcast( &data->cond );
               pthread_mutex_unlock( &data->lock );
               break;
          }

          size <<= 2;
          if (size < CD_BYTES_PER_FRAME) {
               D_WARN( "buffer too small, need at least %d bytes", CD_BYTES_PER_FRAME );
               len = 0;
          }
          else {
               size /= CD_BYTES_PER_FRAME;
               if (len > size)
                    len = size;
          }

          if (len > 0)
               len = cdda_read_audio( data->fd, data->buffer ? : dst, pos, len );

          if (len < 1) {
               if (data->flags & FMPLAY_LOOPING) {
                    data->tracks[data->current_track].frame = 0;
               }
               else {
                    data->status = FMSTATE_FINISHED;
                    pthread_cond_broadcast( &data->cond );
               }
               buffer->Unlock( buffer );
               pthread_mutex_unlock( &data->lock );
               continue;
          }

          data->tracks[data->current_track].frame += len;

          pthread_mutex_unlock( &data->lock );

          len = len * CD_BYTES_PER_FRAME / 4;

          if (data->buffer) {
               cdda_mix_audio( data->buffer, dst, len,
                               data->dest.format, data->dest.channels );
          }

          buffer->Unlock( buffer );

          if (data->callback) {
               if (data->callback( len, data->ctx )) {
                    data->status = FMSTATE_STOP;
                    pthread_cond_broadcast( &data->cond );
               }
          }
     }

     return NULL;
}

static DirectResult
IFusionSoundMusicProvider_CDDA_PlayToBuffer( IFusionSoundMusicProvider *thiz,
                                             IFusionSoundBuffer        *destination,
                                             FMBufferCallback           callback,
                                             void                      *ctx )
{
     FSBufferDescription  desc;
     struct cdda_track   *track;

     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_CDDA )

     if (!destination)
          return DR_INVARG;

     destination->GetDescription( destination, &desc );

     /* check whether destination samplerate is supported */
     if (desc.samplerate != 44100)
          return DR_UNSUPPORTED;

     /* check whether number of channels is supported */
     if (desc.channels > 2)
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

     /* check destination buffer size */
     if (desc.length < CD_BYTES_PER_FRAME/4)
          return DR_UNSUPPORTED;

     pthread_mutex_lock( &data->lock );
     
     CDDA_Stop( data, false );

     data->buffered_frames = (desc.length << 2) / CD_BYTES_PER_FRAME;

     if (desc.sampleformat != FSSF_S16 || desc.channels != 2) {
          data->buffer = D_MALLOC( data->buffered_frames * CD_BYTES_PER_FRAME );
          if (!data->buffer) {
               pthread_mutex_unlock( &data->lock );
               return D_OOM();
          }
     }

     /* reference destination stream */
     destination->AddRef( destination );
     data->dest.buffer   = destination;
     data->dest.format   = desc.sampleformat;
     data->dest.channels = desc.channels;
     data->dest.length   = desc.length;
     data->callback      = callback;
     data->ctx           = ctx;

     track = &data->tracks[data->current_track];
     if (track->frame == track->length)
          track->frame = 0;
          
     data->status = FMSTATE_PLAY;
     pthread_cond_broadcast( &data->cond );

     /* start thread */
     data->thread = direct_thread_create( DTT_DEFAULT, CDDABufferThread, data, "CDDA" );

     pthread_mutex_unlock( &data->lock );

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_CDDA_Stop( IFusionSoundMusicProvider *thiz )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_CDDA )

     pthread_mutex_lock( &data->lock );
     
     CDDA_Stop( data, false );
     
     pthread_cond_broadcast( &data->cond );

     pthread_mutex_unlock( &data->lock );

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_CDDA_GetStatus( IFusionSoundMusicProvider *thiz,
                                          FSMusicProviderStatus     *status )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_CDDA )

     if (!status)
          return DR_INVARG;

     *status = data->status;

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_CDDA_SeekTo( IFusionSoundMusicProvider *thiz,
                                       double                     seconds )
{
     struct cdda_track *track;
     unsigned int       frame;

     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_CDDA )

     if (seconds < 0.0)
          return DR_INVARG;

     track = &data->tracks[data->current_track];
     frame = seconds * CD_FRAMES_PER_SECOND;
     if (frame >= track->length)
          return DR_UNSUPPORTED;

     pthread_mutex_lock( &data->lock );
     track->frame = frame;
     data->seeked = true;
     pthread_mutex_unlock( &data->lock );

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_CDDA_GetPos( IFusionSoundMusicProvider *thiz,
                                       double                    *seconds )
{
     struct cdda_track *track;

     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_CDDA )

     if (!seconds)
          return DR_INVARG;

     track = &data->tracks[data->current_track];
     *seconds = (double)track->frame / CD_FRAMES_PER_SECOND;

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_CDDA_GetLength( IFusionSoundMusicProvider *thiz,
                                          double                    *seconds )
{
     struct cdda_track *track;

     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_CDDA )

     if (!seconds)
          return DR_INVARG;

     track = &data->tracks[data->current_track];
     *seconds = (double)track->length / CD_FRAMES_PER_SECOND;

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_CDDA_SetPlaybackFlags( IFusionSoundMusicProvider    *thiz,
                                                 FSMusicProviderPlaybackFlags  flags )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_CDDA )

     if (flags & ~FMPLAY_LOOPING)
          return DR_UNSUPPORTED;

     data->flags = flags;

     return DR_OK;
}

static DirectResult
IFusionSoundMusicProvider_CDDA_WaitStatus( IFusionSoundMusicProvider *thiz,
                                           FSMusicProviderStatus      mask,
                                           unsigned int               timeout )
{
     DIRECT_INTERFACE_GET_DATA( IFusionSoundMusicProvider_CDDA )
     
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
     return cdda_probe( direct_stream_fileno( ctx->stream ) );
}

static DirectResult
Construct( IFusionSoundMusicProvider *thiz,
           const char                *filename,
           DirectStream              *stream )
{
     DirectResult err;

     DIRECT_ALLOCATE_INTERFACE_DATA( thiz, IFusionSoundMusicProvider_CDDA )

     data->ref = 1;
     data->status = FMSTATE_STOP;

     data->fd = dup( direct_stream_fileno( stream ) );
     if (data->fd < 0) {
          IFusionSoundMusicProvider_CDDA_Destruct( thiz );
          return DR_IO;
     }          

     /* reset to blocking mode */
     fcntl( data->fd, F_SETFL,
               fcntl( data->fd, F_GETFL ) & ~O_NONBLOCK );

     err = cdda_build_tracklits( data->fd, &data->tracks, &data->total_tracks );
     if (err != DR_OK) {
          IFusionSoundMusicProvider_CDDA_Destruct( thiz );
          return err;
     }

#ifdef HAVE_CDDB
     cdda_get_metadata( data->tracks, data->total_tracks );
#endif

     direct_util_recursive_pthread_mutex_init( &data->lock );
     pthread_cond_init( &data->cond, NULL );

     /* initialize function pointers */
     thiz->AddRef               = IFusionSoundMusicProvider_CDDA_AddRef;
     thiz->Release              = IFusionSoundMusicProvider_CDDA_Release;
     thiz->GetCapabilities      = IFusionSoundMusicProvider_CDDA_GetCapabilities;
     thiz->EnumTracks           = IFusionSoundMusicProvider_CDDA_EnumTracks;
     thiz->GetTrackID           = IFusionSoundMusicProvider_CDDA_GetTrackID;
     thiz->GetTrackDescription  = IFusionSoundMusicProvider_CDDA_GetTrackDescription;
     thiz->GetStreamDescription = IFusionSoundMusicProvider_CDDA_GetStreamDescription;
     thiz->GetBufferDescription = IFusionSoundMusicProvider_CDDA_GetBufferDescription;
     thiz->SelectTrack          = IFusionSoundMusicProvider_CDDA_SelectTrack;
     thiz->PlayToStream         = IFusionSoundMusicProvider_CDDA_PlayToStream;
     thiz->PlayToBuffer         = IFusionSoundMusicProvider_CDDA_PlayToBuffer;
     thiz->Stop                 = IFusionSoundMusicProvider_CDDA_Stop;
     thiz->GetStatus            = IFusionSoundMusicProvider_CDDA_GetStatus;
     thiz->SeekTo               = IFusionSoundMusicProvider_CDDA_SeekTo;
     thiz->GetPos               = IFusionSoundMusicProvider_CDDA_GetPos;
     thiz->GetLength            = IFusionSoundMusicProvider_CDDA_GetLength;
     thiz->SetPlaybackFlags     = IFusionSoundMusicProvider_CDDA_SetPlaybackFlags;
     thiz->WaitStatus           = IFusionSoundMusicProvider_CDDA_WaitStatus;

     return DR_OK;
}

