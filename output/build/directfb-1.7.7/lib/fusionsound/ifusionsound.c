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

#include <unistd.h>

#include <fusionsound.h>

#include <direct/interface.h>
#include <direct/mem.h>

#include <fusionsound_limits.h>

#include <core/core_sound.h>
#include <core/sound_buffer.h>

#include <ifusionsound.h>
#include <ifusionsoundbuffer.h>
#include <ifusionsoundstream.h>

#include <media/ifusionsoundmusicprovider.h>

#include <misc/sound_conf.h>
#include <misc/sound_util.h>


static void
IFusionSound_Destruct( IFusionSound *thiz )
{
     IFusionSound_data *data = (IFusionSound_data*)thiz->priv;

     fs_core_destroy( data->core, false );

     DIRECT_DEALLOCATE_INTERFACE( thiz );
     
     if (ifusionsound_singleton == thiz)
          ifusionsound_singleton = NULL;
}

static DirectResult
IFusionSound_AddRef( IFusionSound *thiz )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSound)

     data->ref++;

     return DR_OK;
}

static DirectResult
IFusionSound_Release( IFusionSound *thiz )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSound)

     if (--data->ref == 0)
          IFusionSound_Destruct( thiz );

     return DR_OK;
}

static DirectResult
IFusionSound_GetDeviceDescription( IFusionSound        *thiz,
                                   FSDeviceDescription *desc )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSound)
     
     if (!desc)
          return DR_INVARG;
          
     *desc = *fs_core_device_description( data->core );
     
     return DR_OK;
}

static DirectResult
IFusionSound_CreateBuffer( IFusionSound               *thiz,
                           const FSBufferDescription  *desc,
                           IFusionSoundBuffer        **ret_interface )
{
     DirectResult              ret;
     CoreSoundDeviceConfig    *config;
     FSChannelMode             mode;
     FSSampleFormat            format;
     int                       rate;
     int                       length;
     FSBufferDescriptionFlags  flags;
     CoreSoundBuffer          *buffer;
     IFusionSoundBuffer       *interface;

     DIRECT_INTERFACE_GET_DATA(IFusionSound)

     if (!desc || !ret_interface)
          return DR_INVARG;
          
     config = fs_core_device_config( data->core );
     mode   = config->mode;
     format = config->format;
     rate   = config->rate;
     length = 0;
          
     flags = desc->flags;

     if (flags & ~FSBDF_ALL)
          return DR_INVARG;

     
     if (flags & FSBDF_CHANNELMODE) {
          switch (desc->channelmode) {
               case FSCM_MONO:
               case FSCM_STEREO:
#if FS_MAX_CHANNELS > 2
               case FSCM_STEREO21:
               case FSCM_STEREO30:
               case FSCM_STEREO31:
               case FSCM_SURROUND30:
               case FSCM_SURROUND31:
               case FSCM_SURROUND40_2F2R:
               case FSCM_SURROUND41_2F2R:
               case FSCM_SURROUND40_3F1R:
               case FSCM_SURROUND41_3F1R:
               case FSCM_SURROUND50:
               case FSCM_SURROUND51:
#endif
                    mode = desc->channelmode;
                    break;
                    
               default:
                    return DR_INVARG;
          }
     }
     else if (flags & FSBDF_CHANNELS) {
          switch (desc->channels) {
               case 1 ... FS_MAX_CHANNELS:
                    mode = fs_mode_for_channels( desc->channels );
                    break;

               default:
                    return DR_INVARG;
          }
     }
     
     if (flags & FSBDF_SAMPLEFORMAT) {
          switch (format) {
               case FSSF_U8:
               case FSSF_S16:
               case FSSF_S24:
               case FSSF_S32:
               case FSSF_FLOAT:
                    format = desc->sampleformat;
                    break;

               default:
                    return DR_INVARG;
          }
     }
     
     if (flags & FSBDF_SAMPLERATE) {
          if (desc->samplerate < 100)
               return DR_UNSUPPORTED;
          rate = desc->samplerate;
     }
          
     if (flags & FSBDF_LENGTH)
          length = desc->length;
          
     if (length < 1)
          return DR_INVARG;
          
     if (length > FS_MAX_FRAMES)
          return DR_LIMITEXCEEDED;

     ret = fs_buffer_create( data->core, length, mode, format, rate, &buffer );
     if (ret)
          return ret;

     DIRECT_ALLOCATE_INTERFACE( interface, IFusionSoundBuffer );

     ret = IFusionSoundBuffer_Construct( interface, data->core, buffer,
                                         length, mode, format, rate );
     fs_buffer_unref( buffer );

     if (!ret)
          *ret_interface = interface;

     return ret;
}

static DirectResult
IFusionSound_CreateStream( IFusionSound               *thiz,
                           const FSStreamDescription  *desc,
                           IFusionSoundStream        **ret_interface )
{
     DirectResult              ret;
     CoreSoundDeviceConfig    *config;
     FSChannelMode             mode;
     FSSampleFormat            format;
     int                       rate;
     int                       size;
     int                       prebuffer;
     FSStreamDescriptionFlags  flags;
     CoreSoundBuffer          *buffer;
     IFusionSoundStream       *interface;

     DIRECT_INTERFACE_GET_DATA(IFusionSound)

     if (!ret_interface)
          return DR_INVARG;
     
     config    = fs_core_device_config( data->core );
     mode      = config->mode;
     format    = config->format;
     rate      = config->rate;
     size      = 0;
     prebuffer = 0; /* no prebuffer by default */

     if (desc) {
          flags = desc->flags;

          if (flags & ~FSSDF_ALL)
               return DR_INVARG;

          if (flags & FSSDF_CHANNELMODE) {
               switch (desc->channelmode) {
                    case FSCM_MONO:
                    case FSCM_STEREO:
#if FS_MAX_CHANNELS > 2
                    case FSCM_STEREO21:
                    case FSCM_STEREO30:
                    case FSCM_STEREO31:
                    case FSCM_SURROUND30:
                    case FSCM_SURROUND31:
                    case FSCM_SURROUND40_2F2R:
                    case FSCM_SURROUND41_2F2R:
                    case FSCM_SURROUND40_3F1R:
                    case FSCM_SURROUND41_3F1R:
                    case FSCM_SURROUND50:
                    case FSCM_SURROUND51:
#endif
                         mode = desc->channelmode;
                         break;
                    
                    default:
                         return DR_INVARG;
               }
          }
          else if (flags & FSSDF_CHANNELS) {
               switch (desc->channels) {
                    case 1 ... FS_MAX_CHANNELS:
                         mode = fs_mode_for_channels( desc->channels );
                         break;

                    default:
                         return DR_INVARG;
               }
          }               

          if (flags & FSSDF_SAMPLEFORMAT) {
               switch (desc->sampleformat) {
                    case FSSF_U8:
                    case FSSF_S16:
                    case FSSF_S24:
                    case FSSF_S32:
                    case FSSF_FLOAT:
                         format = desc->sampleformat;
                         break;

                    default:
                         return DR_INVARG;
               }
          }    

          if (flags & FSSDF_SAMPLERATE) {
               if (desc->samplerate < 100)
                    return DR_UNSUPPORTED;
               rate = desc->samplerate;
          }
               
          if (flags & FSSDF_BUFFERSIZE) {
               if (desc->buffersize < 1)
                    return DR_INVARG;
               size = desc->buffersize;
          }

          if (flags & FSSDF_PREBUFFER) {
               if (desc->prebuffer >= size)
                    return DR_INVARG;
               prebuffer = desc->prebuffer;
          }
     }
     
     if (!size)
          size = rate / 5; /* defaults to 1/5 second */

     /* Limit ring buffer size to five seconds. */
     if (size > rate * 5)
          return DR_LIMITEXCEEDED;

     ret = fs_buffer_create( data->core, size, mode, format, rate, &buffer );
     if (ret)
          return ret;

     DIRECT_ALLOCATE_INTERFACE( interface, IFusionSoundStream );

     ret = IFusionSoundStream_Construct( interface, data->core, buffer, size,
                                         mode, format, rate, prebuffer );
     fs_buffer_unref( buffer );

     if (!ret)
          *ret_interface = interface;

     return ret;
}

static DirectResult
IFusionSound_CreateMusicProvider( IFusionSound               *thiz,
                                  const char                 *filename,
                                  IFusionSoundMusicProvider **ret_interface )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSound)

     /* Check arguments */
     if (!ret_interface || !filename)
          return DR_INVARG;

     return IFusionSoundMusicProvider_Create( filename, ret_interface );
}

static DirectResult
IFusionSound_GetMasterVolume( IFusionSound *thiz, 
                              float        *level )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSound)
     
     /* Check arguments */
     if (!level)
          return DR_INVARG;
          
     return fs_core_get_master_volume( data->core, level );
}

static DirectResult
IFusionSound_SetMasterVolume( IFusionSound *thiz,
                              float         level )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSound)
     
     /* Check arguments */
     if (level < 0.0f || level > 1.0f)
          return DR_INVARG;
          
     return fs_core_set_master_volume( data->core, level );
}

static DirectResult
IFusionSound_GetLocalVolume( IFusionSound *thiz, 
                             float        *level )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSound)
     
     /* Check arguments */
     if (!level)
          return DR_INVARG;
          
     return fs_core_get_local_volume( data->core, level );
}

static DirectResult
IFusionSound_SetLocalVolume( IFusionSound *thiz,
                             float         level )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSound)
     
     /* Check arguments */
     if (level < 0.0f || level > 1.0f)
          return DR_INVARG;
          
     return fs_core_set_local_volume( data->core, level );
}

static DirectResult
IFusionSound_Suspend( IFusionSound *thiz )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSound)
     
     return fs_core_suspend( data->core );
}

static DirectResult
IFusionSound_Resume( IFusionSound *thiz )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSound)
     
     return fs_core_resume( data->core );
}

static DirectResult
IFusionSound_GetMasterFeedback( IFusionSound *thiz,
                                float        *ret_left,
                                float        *ret_right )
{
     DIRECT_INTERFACE_GET_DATA(IFusionSound)

     return fs_core_get_master_feedback( data->core, ret_left, ret_right );
}
     

DirectResult
IFusionSound_Construct( IFusionSound *thiz )
{
     DirectResult ret;

     /* Allocate interface data. */
     DIRECT_ALLOCATE_INTERFACE_DATA( thiz, IFusionSound );

     /* Initialize interface data. */
     data->ref = 1;

     /* Create the core instance. */
     ret = fs_core_create( &data->core );
     if (ret) {
          D_DERROR( ret, "FusionSound: fs_core_create() failed!\n" );

          DIRECT_DEALLOCATE_INTERFACE( thiz );

          return ret;
     }

     /* Assign interface pointers. */
     thiz->AddRef               = IFusionSound_AddRef;
     thiz->Release              = IFusionSound_Release;
     thiz->GetDeviceDescription = IFusionSound_GetDeviceDescription;
     thiz->CreateBuffer         = IFusionSound_CreateBuffer;
     thiz->CreateStream         = IFusionSound_CreateStream;
     thiz->CreateMusicProvider  = IFusionSound_CreateMusicProvider;
     thiz->GetMasterVolume      = IFusionSound_GetMasterVolume;
     thiz->SetMasterVolume      = IFusionSound_SetMasterVolume;
     thiz->GetLocalVolume       = IFusionSound_GetLocalVolume;
     thiz->SetLocalVolume       = IFusionSound_SetLocalVolume;
     thiz->Suspend              = IFusionSound_Suspend;
     thiz->Resume               = IFusionSound_Resume;
     thiz->GetMasterFeedback    = IFusionSound_GetMasterFeedback;

     return DR_OK;
}
