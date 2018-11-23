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


#ifndef __FUSIONSOUND_CORE_SOUND_DEVICE_H__
#define __FUSIONSOUND_CORE_SOUND_DEVICE_H__

#include <direct/modules.h>

#include <fusion/fusion.h>

#include <fusionsound.h>

#include <core/types_sound.h>


DECLARE_MODULE_DIRECTORY( fs_sound_drivers );

/*
 * Increase this number when changes result in binary incompatibility!
 */
#define FS_SOUND_DRIVER_ABI_VERSION  5


typedef struct {
     int          major;        /* major version */
     int          minor;        /* minor version */
} SoundDriverVersion;           /* major.minor, e.g. 0.1 */

typedef struct {
     SoundDriverVersion version;

     char               name[FS_SOUND_DRIVER_INFO_NAME_LENGTH];
                                /* Name of driver, e.g. 'OSS' */

     char               vendor[FS_SOUND_DRIVER_INFO_VENDOR_LENGTH];
                                /* Vendor (or author) of the driver,
                                   e.g. 'directfb.org' */

     char               url[FS_SOUND_DRIVER_INFO_URL_LENGTH];
                                /* URL for driver updates,
                                   e.g. 'http://www.directfb.org/' */

     char               license[FS_SOUND_DRIVER_INFO_LICENSE_LENGTH];
                                /* License, e.g. 'LGPL' or 'proprietary' */

     unsigned int       device_data_size;
} SoundDriverInfo;


typedef enum {
     DCF_NONE        = 0x00000000, /* None of these. */
     DCF_VOLUME      = 0x00000001  /* This device supports adjusting volume level */
} DeviceCapabilities;

typedef struct {
     char                name[FS_SOUND_DEVICE_INFO_NAME_LENGTH];
                                   /* Device name, e.g 'Intel 8x0' */
     
     /* Flags representing device capabilities */                             
     DeviceCapabilities  caps;  
} SoundDeviceInfo;  


/* Device Configuration. */
typedef struct {
     FSChannelMode   mode;
     FSSampleFormat  format;
     unsigned int    rate;       /* only suggested, the driver can modify it */
     unsigned int    buffersize; /* only suggested, the driver can modify it */
} CoreSoundDeviceConfig;


/* Device funcs. */
typedef struct {
     DirectResult (*Probe)         ( void );
     
     /* Get device driver information. */
     void         (*GetDriverInfo) ( SoundDriverInfo       *info);

     /* Open the device, get device information and apply given configuration. */
     DirectResult (*OpenDevice)    ( void                  *device_data,
                                     SoundDeviceInfo       *device_info,
                                     CoreSoundDeviceConfig *config );
     
     /* Begin access to the ring buffer, return buffer pointer and available frames. */
     DirectResult (*GetBuffer)     ( void                  *device_data,
                                     u8                   **addr,
                                     unsigned int          *avail );
     
     /* Finish access to the ring buffer, commit specified amout of frames. */
     DirectResult (*CommitBuffer)  ( void                  *device_data,
                                     unsigned int           frames );
     
     /* Get output delay in frames. */                             
     void         (*GetOutputDelay)( void                  *device_data,
                                     int                   *delay );
                                     
     /* Get volume level */
     DirectResult (*GetVolume)     ( void                  *device_data,
                                     float                 *level );
                                     
     /* Set volume level */
     DirectResult (*SetVolume)     ( void                  *device_data,
                                     float                  level );
                                     
     /* Suspend the device */
     DirectResult (*Suspend)       ( void                  *device_data );
     
     /* Resume the device */
     DirectResult (*Resume)        ( void                  *device_data );
     
     /* Handle fork */
     void         (*HandleFork)    ( void                  *device_data,
                                     FusionForkAction       action,
                                     FusionForkState        state );
     
     /* Close device. */
     void         (*CloseDevice)   ( void                  *device_data );
} SoundDriverFuncs;
     
     
DirectResult fs_device_initialize( CoreSound              *core, 
                                   CoreSoundDeviceConfig  *config,
                                   CoreSoundDevice       **ret_device );
void         fs_device_shutdown  ( CoreSoundDevice        *device );

void         fs_device_get_description( CoreSoundDevice     *device,
                                        FSDeviceDescription *desc );

DeviceCapabilities fs_device_get_capabilities( CoreSoundDevice *device );
                                                                                                          
DirectResult fs_device_get_buffer( CoreSoundDevice  *device, 
                                   u8              **addr,
                                   unsigned int     *avail );

DirectResult fs_device_commit_buffer( CoreSoundDevice *device, unsigned int frames );
                           
void         fs_device_get_output_delay( CoreSoundDevice *device, int *delay );

DirectResult fs_device_get_volume( CoreSoundDevice *device, float *level );

DirectResult fs_device_set_volume( CoreSoundDevice *device, float  level );

DirectResult fs_device_suspend( CoreSoundDevice *device );

DirectResult fs_device_resume( CoreSoundDevice *device );

void         fs_device_handle_fork( CoreSoundDevice  *device,
                                    FusionForkAction  action,
                                    FusionForkState   state );
                   
#endif                                   
