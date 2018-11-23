/*
   (c) Copyright 2012-2013  DirectFB integrated media GmbH
   (c) Copyright 2001-2013  The world wide DirectFB Open Source Community (directfb.org)
   (c) Copyright 2000-2004  Convergence (integrated media) GmbH

   All rights reserved.

   Written by Denis Oliver Kropp <dok@directfb.org>,
              Andreas Shimokawa <andi@directfb.org>,
              Marek Pikarski <mass@directfb.org>,
              Sven Neumann <neo@directfb.org>,
              Ville Syrjälä <syrjala@sci.fi> and
              Claudio Ciccani <klan@users.sf.net>.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/


#include <config.h>

#include <pthread.h>

#include <fusionsound.h>

#include <direct/debug.h>
#include <direct/interface.h>
#include <direct/mem.h>
#include <direct/memcpy.h>
#include <direct/util.h>

#include <core/core_sound.h>
#include <core/playback.h>
#include <core/sound_buffer.h>

#include "ifusionsoundplayback.h"
#include "ifusionsoundstream.h"

/******/

static DirectResult   IFusionSoundStream_FillBuffer( IFusionSoundStream_data *data,
                                                     const void              *sample_data,
                                                     int                      length,
                                                     int                     *ret_bytes );

static ReactionResult IFusionSoundStream_React     ( const void              *msg_data,
                                                     void                    *ctx );

/******/

static void
IFusionSoundStream_Destruct( IFusionSoundStream *thiz )
{
     IFusionSoundStream_data *data = (IFusionSoundStream_data*)thiz->priv;

     D_ASSERT( data->buffer != NULL );
     D_ASSERT( data->playback != NULL );
     
     if (data->iplayback)
          data->iplayback->Release( data->iplayback );

     fs_playback_detach( data->playback, &data->reaction );

     fs_playback_stop( data->playback, true );

     fs_playback_unref( data->playback );

     fs_buffer_unref( data->buffer );

     pthread_cond_destroy( &data->wait );
     pthread_mutex_destroy( &data->lock );

     DIRECT_DEALLOCATE_INTERFACE( thiz );
}

static DirectResult
IFusionSoundStream_AddRef( IFusionSoundStream *thiz )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundStream)

     data->ref++;

     return DR_OK;
}

static DirectResult
IFusionSoundStream_Release( IFusionSoundStream *thiz )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundStream)

     if (--data->ref == 0)
          IFusionSoundStream_Destruct( thiz );

     return DR_OK;
}


static DirectResult
IFusionSoundStream_GetDescription( IFusionSoundStream  *thiz,
                                   FSStreamDescription *desc )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundStream)

     if (!desc)
          return DR_INVARG;

     desc->flags = FSSDF_BUFFERSIZE   | FSSDF_CHANNELS   |
                   FSSDF_SAMPLEFORMAT | FSSDF_SAMPLERATE | 
                   FSSDF_PREBUFFER    | FSSDF_CHANNELMODE;
     desc->buffersize   = data->size;
     desc->channels     = FS_CHANNELS_FOR_MODE(data->mode);
     desc->sampleformat = data->format;
     desc->samplerate   = data->rate;
     desc->prebuffer    = data->prebuffer;
     desc->channelmode  = data->mode;

     return DR_OK;
}

static DirectResult
IFusionSoundStream_Write( IFusionSoundStream *thiz,
                          const void         *sample_data,
                          int                 length )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundStream)

     if (!sample_data || length < 1)
          return DR_INVARG;

     pthread_mutex_lock( &data->lock );

     data->pending = length;
     
     while (data->pending) {
          DirectResult ret;
          int       num;
          int       bytes;

          D_DEBUG( "%s: length %d, read pos %d, write pos %d, filled %d/%d (%splaying)\n",
                   __FUNCTION__, data->pending, data->pos_read, data->pos_write,
                   data->filled, data->size, data->playing ? "" : "not " );

          D_ASSERT( data->filled <= data->size );

          /* Wait for at least one free sample. */
          while (data->filled == data->size) {
               pthread_cleanup_push( (void (*)(void *))pthread_mutex_unlock, &data->lock );
               pthread_cond_wait( &data->wait, &data->lock );
               pthread_cleanup_pop( 0 );
               
               /* Drop could have been called while waiting */
               if (!data->pending) {
                    pthread_mutex_unlock( &data->lock );
                    return DR_OK;
               }
          }

          /* Calculate number of free samples in the buffer. */
          num = data->size - data->filled;

          /* Do not write more than requested. */
          if (num > data->pending)
               num = data->pending;

          /* Fill free space with automatic wrap around. */
          ret = IFusionSoundStream_FillBuffer( data, sample_data, num, &bytes);
          if (ret) {
               pthread_mutex_unlock( &data->lock );
               return ret;
          }

          /* (Re)start if playback had stopped (buffer underrun). */
          if (!data->playing && data->prebuffer >= 0 && data->filled >= data->prebuffer) {
               D_DEBUG( "%s: starting playback now!\n", __FUNCTION__ );

               fs_playback_start( data->playback, true );
          }

          /* Update input parameters. */
          sample_data += bytes;           
          /* Update amount of pending frames. */
          if (data->pending)
               data->pending -= num;
     }

     pthread_mutex_unlock( &data->lock );

     return DR_OK;
}

static DirectResult
IFusionSoundStream_Wait( IFusionSoundStream *thiz,
                         int                 length )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundStream)

     if (length < 0 || length > data->size)
          return DR_INVARG;

     pthread_mutex_lock( &data->lock );

     while (true) {
          if (length) {
               int num;

               /* Calculate number of free samples in the buffer. */
               num = data->size - data->filled;

               if (num >= length)
                    break;
          }
          else if (!data->playing)
               break;

          pthread_cleanup_push( (void (*)(void *))pthread_mutex_unlock, &data->lock );
          pthread_cond_wait( &data->wait, &data->lock );
          pthread_cleanup_pop( 0 );
     }

     pthread_mutex_unlock( &data->lock );

     return DR_OK;
}

static DirectResult
IFusionSoundStream_GetStatus( IFusionSoundStream *thiz,
                              int                *filled,
                              int                *total,
                              int                *read_position,
                              int                *write_position,
                              bool               *playing )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundStream)

     pthread_mutex_lock( &data->lock );

     if (filled)
          *filled = data->filled;

     if (total)
          *total = data->size;

     if (read_position)
          *read_position = data->pos_read;

     if (write_position)
          *write_position = data->pos_write;

     if (playing)
          *playing = data->playing;

     pthread_mutex_unlock( &data->lock );

     return DR_OK;
}

static DirectResult
IFusionSoundStream_Flush( IFusionSoundStream *thiz )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundStream)

     /* Stop the playback. */
     fs_playback_stop( data->playback, true );
     
     pthread_mutex_lock( &data->lock );

     while (data->playing) {
          pthread_cleanup_push( (void (*)(void *))pthread_mutex_unlock, &data->lock );
          pthread_cond_wait( &data->wait, &data->lock );
          pthread_cleanup_pop( 0 );
     }

     /* Reset the buffer. */
     data->pos_write = data->pos_read;
     data->filled    = 0;

     pthread_mutex_unlock( &data->lock );

     return DR_OK;
}

static DirectResult
IFusionSoundStream_Drop( IFusionSoundStream *thiz )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundStream)

     pthread_mutex_lock( &data->lock );

     /* Discard pending data. */
     data->pending = 0;

     /* Wake up any write threads that may be pending. */
     pthread_cond_broadcast( &data->wait );

     pthread_mutex_unlock( &data->lock );

     return DR_OK;
}

static DirectResult
IFusionSoundStream_GetPresentationDelay( IFusionSoundStream *thiz,
                                         int                *delay )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundStream)

     if (!delay)
          return DR_INVARG;

     pthread_mutex_lock( &data->lock );

     *delay = fs_core_output_delay( data->core ) +
              (data->filled + data->pending) * 1000 / data->rate;

     pthread_mutex_unlock( &data->lock );

     return DR_OK;
}

static DirectResult
IFusionSoundStream_GetPlayback( IFusionSoundStream    *thiz,
                                IFusionSoundPlayback **ret_interface )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundStream)

     if (!ret_interface)
          return DR_INVARG;

     if (!data->iplayback) {
          IFusionSoundPlayback *interface;
          DirectResult          ret;
          
          DIRECT_ALLOCATE_INTERFACE( interface, IFusionSoundPlayback );

          ret = IFusionSoundPlayback_Construct( interface, data->playback, -1 );
          if (ret) {
               *ret_interface = NULL;
               return ret;
          }
          
          data->iplayback = interface;
     }

     data->iplayback->AddRef( data->iplayback );
     
     *ret_interface = data->iplayback;

     return DR_OK;
}

static DirectResult
IFusionSoundStream_Access( IFusionSoundStream  *thiz,
                           void               **ret_data,
                           int                 *ret_avail )
{
     DirectResult ret;
     int       num;
     int       bytes;
     
     DIRECT_INTERFACE_GET_DATA(IFusionSoundStream)

     if (!ret_data || !ret_avail)
          return DR_INVARG;

     pthread_mutex_lock( &data->lock );
     
     D_DEBUG( "%s: read pos %d, write pos %d, filled %d/%d (%splaying)\n",
              __FUNCTION__, data->pos_read, data->pos_write,
              data->filled, data->size, data->playing ? "" : "not " );
              
     D_ASSERT( data->filled <= data->size );
     
     /* Wait for at least one free sample. */
     while (data->filled == data->size) {
          pthread_cleanup_push( (void (*)(void *))pthread_mutex_unlock, &data->lock );
          pthread_cond_wait( &data->wait, &data->lock );
          pthread_cleanup_pop( 0 );
     }
     
     /* Calculate number of free samples in the buffer. */
     num = data->size - data->filled;
     if (num > data->size - data->pos_write)
          num = data->size - data->pos_write;
          
     ret = fs_buffer_lock( data->buffer, data->pos_write, num, ret_data, &bytes );
     
     *ret_avail = (ret) ? 0 : num;
     
     pthread_mutex_unlock( &data->lock );
     
     return ret;
}

static DirectResult
IFusionSoundStream_Commit( IFusionSoundStream  *thiz,
                           int                  length )
{
     DirectResult ret;
     
     DIRECT_INTERFACE_GET_DATA(IFusionSoundStream)

     if (length < 0)
          return DR_INVARG;

     pthread_mutex_lock( &data->lock );
     
     if (length > data->size - data->filled) {
          pthread_mutex_unlock( &data->lock );
          return DR_INVARG;
     }
     
     D_DEBUG( "%s: length %d, filled %d/%d (%splaying)\n",
              __FUNCTION__, length, data->filled, data->size, data->playing ? "" : "not " );
     
     /* Unlock buffer */
     fs_buffer_unlock( data->buffer );
  
     if (length) {   
          /* Update write position. */
          data->pos_write += length;

          /* Handle wrap around. */
          if (data->pos_write == data->size)
               data->pos_write = 0;

          /* Set new stop position. */
          ret = fs_playback_set_stop( data->playback, data->pos_write );
          if (ret) {     
               pthread_mutex_unlock( &data->lock );
               return ret;
          }

          /* (Re)enable playback if buffer has been empty. */
          fs_playback_enable( data->playback );

          /* Update the fill level. */
          data->filled += length;
     
          /* (Re)start if playback had stopped (buffer underrun). */
          if (!data->playing && data->prebuffer >= 0 && data->filled >= data->prebuffer) {
               D_DEBUG( "%s: starting playback now!\n", __FUNCTION__ );

               fs_playback_start( data->playback, true );
          }
     }
     
     pthread_mutex_unlock( &data->lock );
     
     return DR_OK;
}


/******/

DirectResult
IFusionSoundStream_Construct( IFusionSoundStream *thiz,
                              CoreSound          *core,
                              CoreSoundBuffer    *buffer,
                              int                 size,
                              FSChannelMode       mode,
                              FSSampleFormat      format,
                              int                 rate,
                              int                 prebuffer )
{
     DirectResult  ret;
     CorePlayback *playback;

     DIRECT_ALLOCATE_INTERFACE_DATA(thiz, IFusionSoundStream)

     /* Increase reference counter of the buffer. */
     if (fs_buffer_ref( buffer )) {
          ret = DR_FUSION;
          goto error_ref;
     }

     /* Create a playback object for the buffer. */
     ret = fs_playback_create( core, buffer, true, &playback );
     if (ret)
          goto error_create;

     /* Attach our listener to the playback object. */
     if (fs_playback_attach( playback, IFusionSoundStream_React, data, &data->reaction )) {
          ret = DR_FUSION;
          goto error_attach;
     }

     /* Disable the playback. */
     fs_playback_stop( playback, true );

     /* Initialize private data. */
     data->ref       = 1;
     data->core      = core;
     data->buffer    = buffer;
     data->playback  = playback;
     data->size      = size;
     data->mode      = mode;
     data->format    = format;
     data->rate      = rate;
     data->prebuffer = prebuffer;

     /* Initialize lock and condition. */
     direct_util_recursive_pthread_mutex_init( &data->lock );
     pthread_cond_init( &data->wait, NULL );

     /* Initialize method table. */
     thiz->AddRef               = IFusionSoundStream_AddRef;
     thiz->Release              = IFusionSoundStream_Release;

     thiz->GetDescription       = IFusionSoundStream_GetDescription;

     thiz->Write                = IFusionSoundStream_Write;
     thiz->Wait                 = IFusionSoundStream_Wait;
     thiz->GetStatus            = IFusionSoundStream_GetStatus;
     thiz->Flush                = IFusionSoundStream_Flush;
     thiz->Drop                 = IFusionSoundStream_Drop;
     
     thiz->GetPresentationDelay = IFusionSoundStream_GetPresentationDelay;

     thiz->GetPlayback          = IFusionSoundStream_GetPlayback;
     
     thiz->Access               = IFusionSoundStream_Access;
     thiz->Commit               = IFusionSoundStream_Commit;

     return DR_OK;

error_attach:
     fs_playback_unref( playback );

error_create:
     fs_buffer_unref( buffer );

error_ref:
     DIRECT_DEALLOCATE_INTERFACE( thiz );

     return ret;
}

/******/

static DirectResult
IFusionSoundStream_FillBuffer( IFusionSoundStream_data *data,
                               const void              *sample_data,
                               int                      length,
                               int                     *ret_bytes )
{
     DirectResult     ret;
     void            *lock_data;
     int              lock_bytes;
     int              offset = 0;

     D_DEBUG( "%s: length %d\n", __FUNCTION__, length );

     D_ASSERT( length <= data->size - data->filled );

     while (length) {
          int num = MIN( length, data->size - data->pos_write );

          /* Write data. */
          ret = fs_buffer_lock( data->buffer, data->pos_write, num, &lock_data, &lock_bytes );
          if (ret)
               return ret;

          direct_memcpy( lock_data, sample_data + offset, lock_bytes );

          fs_buffer_unlock( data->buffer );

          /* Update input parameters. */
          length -= num;
          offset += lock_bytes;

          /* Update write position. */
          data->pos_write += num;

          /* Handle wrap around. */
          if (data->pos_write == data->size)
               data->pos_write = 0;

          /* Set new stop position. */
          ret = fs_playback_set_stop( data->playback, data->pos_write );
          if (ret)
               return ret;

          /* (Re)enable playback if buffer has been empty. */
          fs_playback_enable( data->playback );

          /* Update the fill level. */
          data->filled += num;
     }

     if (ret_bytes)
          *ret_bytes = offset;

     return DR_OK;
}

static ReactionResult
IFusionSoundStream_React( const void *msg_data,
                          void       *ctx )
{
     const CorePlaybackNotification *notification = msg_data;
     CorePlaybackNotificationFlags   flags        = notification->flags;
     IFusionSoundStream_data        *data         = ctx;

     if (flags & CPNF_START) {
          D_DEBUG( "%s: playback started at %d\n", __FUNCTION__, notification->pos );

          /* No locking here to avoid dead possible dead lock with IFusionSoundStream_Write(). */
          data->playing = true;

          return RS_OK;
     }

     pthread_mutex_lock( &data->lock );

     if (notification->flags & CPNF_ADVANCE) {
          D_DEBUG( "%s: playback advanced by %d from %d to %d\n",
                   __FUNCTION__, notification->num, data->pos_read, notification->pos );

          D_ASSERT( data->filled >= notification->num );
          
          data->filled -= notification->num;
     }

     data->pos_read = notification->pos;

     if (flags & CPNF_STOP) {
          D_DEBUG( "%s: playback stopped at %d!\n", __FUNCTION__, notification->pos );

          data->playing = false;
     }

     pthread_cond_broadcast( &data->wait );

     pthread_mutex_unlock( &data->lock );

     return RS_OK;
}

