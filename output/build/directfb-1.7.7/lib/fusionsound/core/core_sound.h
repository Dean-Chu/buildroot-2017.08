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


#ifndef __FUSIONSOUND_CORE_H__
#define __FUSIONSOUND_CORE_H__

#include <fusionsound.h>

#include <fusion/object.h>

#include <core/types_sound.h>
#include <core/sound_device.h>

#define FUSIONSOUND_CORE_ABI     0x4653000


/*
 * Core initialization and deinitialization
 */
DirectResult fs_core_create ( CoreSound **ret_core );
DirectResult fs_core_destroy( CoreSound  *core, bool emergency );

/*
 * Object creation
 */
CoreSoundBuffer *fs_core_create_buffer  ( CoreSound *core );
CorePlayback    *fs_core_create_playback( CoreSound *core );

/*
 * Object enumeration
 */
DirectResult fs_core_enum_buffers   ( CoreSound            *core,
                                      FusionObjectCallback  callback,
                                      void                 *ctx );
DirectResult fs_core_enum_playbacks ( CoreSound            *core,
                                      FusionObjectCallback  callback,
                                      void                 *ctx );

/*
 * Playback list management
 */
DirectResult fs_core_playlist_lock  ( CoreSound *core );
DirectResult fs_core_playlist_unlock( CoreSound *core );

DirectResult fs_core_add_playback   ( CoreSound    *core,
                                      CorePlayback *playback );

DirectResult fs_core_remove_playback( CoreSound    *core,
                                      CorePlayback *playback );

/*
 * Returns the amount of audio data buffered by the device in ms.
 */
int fs_core_output_delay( CoreSound *core );

/*
 * Returns the Fusion World of the sound core.
 */
FusionWorld *fs_core_world( CoreSound *core );

/*
 * Returns the Fusion Shared Memory Pool of the sound core.
 */
FusionSHMPoolShared *fs_core_shmpool( CoreSound *core );

/*
 * Returns device information.
 */ 
FSDeviceDescription *fs_core_device_description( CoreSound *core );

/*
 * Returns device configuration.
 */
CoreSoundDeviceConfig *fs_core_device_config( CoreSound *core );

/*
 * Returns the master volume.
 */
DirectResult fs_core_get_master_volume( CoreSound *core, float *level );

/*
 * Sets the master volume.
 */
DirectResult fs_core_set_master_volume( CoreSound *core, float level );

/*
 * Returns the local volume.
 */
DirectResult fs_core_get_local_volume( CoreSound *core, float *level );

/*
 * Sets the local volume.
 */
DirectResult fs_core_set_local_volume( CoreSound *core, float level );

/*
 * Returns the master feedback.
 */
DirectResult fs_core_get_master_feedback( CoreSound *core,
                                          float     *ret_left,
                                          float     *ret_right );

/*
 * Suspends playback.
 */
DirectResult fs_core_suspend( CoreSound *core );

/*
 * Resumes playback.
 */
DirectResult fs_core_resume( CoreSound *core );


#endif
