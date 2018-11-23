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

#include <core/playback.h>

#include <direct/interface.h>
#include <direct/util.h>

#include "ifusionsoundplayback.h"

/*
 * private data struct of IFusionSoundPlayback
 */
typedef struct {
     int                    ref;       /* reference counter */

     CorePlayback          *playback;
     bool                   stream;
     int                    length;
     Reaction               reaction;

     float                  volume;
     float                  pan;
     
     int                    pitch;
     int                    dir;

     pthread_mutex_t        lock;
     pthread_cond_t         wait;
} IFusionSoundPlayback_data;

/******/

static ReactionResult IFusionSoundPlayback_React( const void *msg_data,
                                                  void       *ctx );

static DirectResult
IFusionSoundPlayback_UpdateVolume( IFusionSoundPlayback_data* data );

/******/

static void
IFusionSoundPlayback_Destruct( IFusionSoundPlayback *thiz )
{
     IFusionSoundPlayback_data *data = (IFusionSoundPlayback_data*)thiz->priv;

     D_ASSERT( data->playback != NULL );

     fs_playback_detach( data->playback, &data->reaction );

     if (!data->stream)
          fs_playback_stop( data->playback, false );

     fs_playback_unref( data->playback );

     pthread_cond_destroy( &data->wait );
     pthread_mutex_destroy( &data->lock );

     DIRECT_DEALLOCATE_INTERFACE( thiz );
}

static DirectResult
IFusionSoundPlayback_AddRef( IFusionSoundPlayback *thiz )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundPlayback)

     data->ref++;

     return DR_OK;
}

static DirectResult
IFusionSoundPlayback_Release( IFusionSoundPlayback *thiz )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundPlayback)

     if (--data->ref == 0)
          IFusionSoundPlayback_Destruct( thiz );

     return DR_OK;
}

static DirectResult
IFusionSoundPlayback_Start( IFusionSoundPlayback *thiz,
                            int                   start,
                            int                   stop )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundPlayback)

     D_DEBUG( "%s (%p, %d -> %d)\n",
              __FUNCTION__, data->playback, start, stop );

     if (data->stream)
          return DR_UNSUPPORTED;

     if (start < 0 || start >= data->length)
          return DR_INVARG;

     if (stop >= data->length)
          return DR_INVARG;

     pthread_mutex_lock( &data->lock );

     fs_playback_set_position( data->playback, start );
     fs_playback_set_stop( data->playback, stop );
     fs_playback_start( data->playback, false );

     pthread_mutex_unlock( &data->lock );

     return DR_OK;
}

static DirectResult
IFusionSoundPlayback_Stop( IFusionSoundPlayback *thiz )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundPlayback)

     D_DEBUG( "%s (%p)\n", __FUNCTION__, data->playback );

     return fs_playback_stop( data->playback, false );
}

static DirectResult
IFusionSoundPlayback_Continue( IFusionSoundPlayback *thiz )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundPlayback)

     D_DEBUG( "%s (%p)\n", __FUNCTION__, data->playback );

     return fs_playback_start( data->playback, false );
}

static DirectResult
IFusionSoundPlayback_Wait( IFusionSoundPlayback *thiz )
{
     DirectResult ret;

     DIRECT_INTERFACE_GET_DATA(IFusionSoundPlayback)

     D_DEBUG( "%s (%p)\n", __FUNCTION__, data->playback );

     pthread_mutex_lock( &data->lock );

     for (;;) {
          CorePlaybackStatus status;

          ret = fs_playback_get_status( data->playback, &status, NULL );
          if (ret)
               break;

          if (status & CPS_PLAYING) {
               if (status & CPS_LOOPING) {
                    ret = DR_UNSUPPORTED;
                    break;
               }
               else {
                    pthread_cleanup_push( (void (*)(void *))pthread_mutex_unlock, &data->lock );
                    pthread_cond_wait( &data->wait, &data->lock );
                    pthread_cleanup_pop( 0 );
               }
          }
          else
               break;
     }

     pthread_mutex_unlock( &data->lock );

     return ret;
}

static DirectResult
IFusionSoundPlayback_GetStatus( IFusionSoundPlayback *thiz,
                                bool                 *ret_playing,
                                int                  *ret_position )
{
     DirectResult       ret;
     CorePlaybackStatus status;
     int                position;

     DIRECT_INTERFACE_GET_DATA(IFusionSoundPlayback)

     D_DEBUG( "%s (%p)\n", __FUNCTION__, data->playback );

     ret = fs_playback_get_status( data->playback, &status, &position );
     if (ret)
          return ret;

     if (ret_playing)
          *ret_playing = !!(status & CPS_PLAYING);

     if (ret_position)
          *ret_position = position;

     return DR_OK;
}

static DirectResult
IFusionSoundPlayback_SetVolume( IFusionSoundPlayback *thiz,
                                float                 level )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundPlayback)

     D_DEBUG( "%s (%p, %.3f)\n", __FUNCTION__, data->playback, level );

     if (level < 0.0f)
          return DR_INVARG;
          
     if (level > 64.0f)
          return DR_UNSUPPORTED;

     data->volume = level;

     return IFusionSoundPlayback_UpdateVolume( data );
}

static DirectResult
IFusionSoundPlayback_SetPan( IFusionSoundPlayback *thiz,
                             float                 value )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundPlayback)

     D_DEBUG( "%s (%p, %.3f)\n", __FUNCTION__, data->playback, value );

     if (value < -1.0f || value > 1.0f)
          return DR_INVARG;

     data->pan = value;

     return IFusionSoundPlayback_UpdateVolume( data );
}

static DirectResult
IFusionSoundPlayback_SetPitch( IFusionSoundPlayback *thiz,
                               float                 value )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundPlayback)

     D_DEBUG( "%s (%p, %.3f)\n", __FUNCTION__, data->playback, value );

     if (value < 0.0f)
          return DR_INVARG;
          
     if (value > 64.0f)
          return DR_UNSUPPORTED;
          
     data->pitch = (value * FS_PITCH_ONE + 0.5f);

     fs_playback_set_pitch( data->playback, data->pitch*data->dir );

     return DR_OK;
}

static DirectResult
IFusionSoundPlayback_SetDirection( IFusionSoundPlayback *thiz,
                                   FSPlaybackDirection   direction )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSoundPlayback)
     
     D_DEBUG( "%s (%p, %d)\n", __FUNCTION__, data->playback, direction );
     
     switch (direction) {
          case FSPD_FORWARD:
          case FSPD_BACKWARD:
               data->dir = direction;
               break;
          default:
               return DR_INVARG;
     }
     
     fs_playback_set_pitch( data->playback, data->pitch*data->dir );
     
     return DR_OK;
}

static DirectResult
IFusionSoundPlayback_SetDownmixLevels( IFusionSoundPlayback *thiz,
                                       float                 center,
                                       float                 rear )
{
     DirectResult ret;
     
     DIRECT_INTERFACE_GET_DATA(IFusionSoundPlayback)
     
     D_DEBUG( "%s (%p, %.3f, %.3f)\n", __FUNCTION__, data->playback, center, rear );
     
     if (center < 0.0f || center > 1.0f)
          return DR_INVARG;
          
     if (rear < 0.0f || rear > 1.0f)
          return DR_INVARG;
          
     ret = fs_playback_set_downmix( data->playback, center, rear );
     if (ret)
          return ret;
     
     return IFusionSoundPlayback_UpdateVolume( data );
}

/******/

DirectResult
IFusionSoundPlayback_Construct( IFusionSoundPlayback *thiz,
                                CorePlayback         *playback,
                                int                   length )
{
     DIRECT_ALLOCATE_INTERFACE_DATA(thiz, IFusionSoundPlayback)

     /* Increase reference counter of the playback. */
     if (fs_playback_ref( playback )) {
          DIRECT_DEALLOCATE_INTERFACE( thiz );

          return DR_FUSION;
     }

     /* Attach our listener to the playback. */
     if (fs_playback_attach( playback, IFusionSoundPlayback_React,
                             data, &data->reaction ))
     {
          fs_playback_unref( playback );

          DIRECT_DEALLOCATE_INTERFACE( thiz );

          return DR_FUSION;
     }

     /* Initialize private data. */
     data->ref      = 1;
     data->playback = playback;
     data->stream   = (length < 0);
     data->length   = length;
     data->volume   = 1.0f;
     data->pitch    = FS_PITCH_ONE;
     data->dir      = +1;

     /* Initialize lock and condition. */
     direct_util_recursive_pthread_mutex_init( &data->lock );
     pthread_cond_init( &data->wait, NULL );

     /* Initialize method table. */
     thiz->AddRef           = IFusionSoundPlayback_AddRef;
     thiz->Release          = IFusionSoundPlayback_Release;

     thiz->Start            = IFusionSoundPlayback_Start;
     thiz->Stop             = IFusionSoundPlayback_Stop;
     thiz->Continue         = IFusionSoundPlayback_Continue;
     thiz->Wait             = IFusionSoundPlayback_Wait;
 
     thiz->GetStatus        = IFusionSoundPlayback_GetStatus;

     thiz->SetVolume        = IFusionSoundPlayback_SetVolume;
     thiz->SetPan           = IFusionSoundPlayback_SetPan;
     thiz->SetPitch         = IFusionSoundPlayback_SetPitch;
     thiz->SetDirection     = IFusionSoundPlayback_SetDirection;
     thiz->SetDownmixLevels = IFusionSoundPlayback_SetDownmixLevels;

     return DR_OK;
}

/******/

static ReactionResult
IFusionSoundPlayback_React( const void *msg_data,
                            void       *ctx )
{
     const CorePlaybackNotification *notification = msg_data;
     IFusionSoundPlayback_data      *data         = ctx;

     if (notification->flags & CPNF_START)
          D_DEBUG( "%s: playback started at position %d\n", __FUNCTION__, notification->pos );

     if (notification->flags & CPNF_STOP)
          D_DEBUG( "%s: playback stopped at position %d!\n", __FUNCTION__, notification->pos );

     if (notification->flags & CPNF_ADVANCE)
          D_DEBUG( "%s: playback advanced to position %d\n", __FUNCTION__, notification->pos );

     /* Notify any Wait()er on start/stop. */
     if (notification->flags & (CPNF_START | CPNF_STOP))
          pthread_cond_broadcast( &data->wait );

     return RS_OK;
}

static DirectResult
IFusionSoundPlayback_UpdateVolume( IFusionSoundPlayback_data* data )
{
                       /* L     R     C     Rl    Rr   LFE  */
     float levels[6] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

     if (data->pan != 0.0f) {
          if (data->pan < 0.0f)
               levels[1] = levels[4] = 1.0f + data->pan; /* right */
          else if (data->pan > 0.0f)
               levels[0] = levels[3] = 1.0f - data->pan; /* left */
     }

     if (data->volume != 1.0f) {
          int i;
          for (i = 0; i < 6; i++) {
               levels[i] *= data->volume;
               if (levels[i] > 64.0f)
                    levels[i] = 64.0f;
          }
     }

     return fs_playback_set_volume( data->playback, levels );
}

