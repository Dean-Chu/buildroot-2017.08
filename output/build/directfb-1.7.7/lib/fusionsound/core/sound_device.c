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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <direct/mem.h>
#include <direct/messages.h>
#include <direct/modules.h>

#include <core/sound_device.h>

#include <misc/sound_conf.h>


DEFINE_MODULE_DIRECTORY( fs_sound_drivers, "snddrivers", FS_SOUND_DRIVER_ABI_VERSION );


struct __FS_CoreSoundDevice {
     DirectModuleEntry      *module;
     const SoundDriverFuncs *funcs;
     SoundDriverInfo         info;

     void                   *device_data;
     SoundDeviceInfo         device_info;
};


DirectResult 
fs_device_initialize( CoreSound               *core, 
                      CoreSoundDeviceConfig   *config,
                      CoreSoundDevice        **ret_device )
{
     DirectResult     ret;
     CoreSoundDevice *device;
     DirectLink      *link;
     
     D_ASSERT( core != NULL );
     D_ASSERT( ret_device != NULL );
     
     device = D_CALLOC( 1, sizeof(CoreSoundDevice) );
     if (!device)
          return D_OOM();
          
     snprintf( device->info.name, 
               FS_SOUND_DRIVER_INFO_NAME_LENGTH, "none" );
     snprintf( device->info.vendor,
               FS_SOUND_DRIVER_INFO_VENDOR_LENGTH, "directfb.org" );
          
     if (!fs_config->driver || strcmp( fs_config->driver, "none" )) {
          /* Build a list of available drivers. */
          direct_modules_explore_directory( &fs_sound_drivers );
     
          /* Load driver */
          direct_list_foreach( link, fs_sound_drivers.entries ) {
               DirectModuleEntry *module = (DirectModuleEntry*) link;
          
               const SoundDriverFuncs *funcs = direct_module_ref( module );
          
               if (!funcs)
                    continue;
               
               if (!device->module && 
                  (!fs_config->driver || !strcmp( module->name, fs_config->driver ))) {
                    if (funcs->Probe() == DR_OK) {
                         device->module = module;
                         device->funcs  = funcs;
               
                         funcs->GetDriverInfo( &device->info );
                         
                         direct_module_ref( module );
                    }
               }
          
               direct_module_unref( module );
          }
     
          if (!device->module) {
               if (fs_config->driver) {
                    D_ERROR( "FusionSound/Device: driver '%s' not found!\n", 
                              fs_config->driver );
               } else {
                    D_ERROR( "FusionSound/Device: no driver found!\n" );
               }
          
               D_FREE( device );
               return DR_FAILURE;
          }
     
          if (device->info.device_data_size) {
               device->device_data = D_CALLOC( 1, device->info.device_data_size );
               if (!device->device_data) {
                    direct_module_unref( device->module );
                    D_FREE( device );
                    return D_OOM();
               }
          }
         
          /* Open sound device. */
          ret = device->funcs->OpenDevice( device->device_data, &device->device_info, config );
          if (ret) {
               D_ERROR( "FusionSound/Device: could not open device!\n" );
               direct_module_unref( device->module );
               D_FREE( device );
               return ret;
          }
     }
     
     D_INFO( "FusionSound/Device: %s %d.%d (%s)\n",
             device->info.name, 
             device->info.version.major, device->info.version.minor,
             device->info.vendor );
             
     D_INFO( "FusionSound/Device: %d Hz, %d channel(s), %d bits, %.1f ms.\n",
             config->rate, FS_CHANNELS_FOR_MODE(config->mode),
             FS_BITS_PER_SAMPLE(config->format),
             (float)config->buffersize/config->rate*1000.f  );
     
     *ret_device = device;
     
     return DR_OK;
}

void
fs_device_get_description( CoreSoundDevice     *device,
                           FSDeviceDescription *desc )
{
     D_ASSERT( device != NULL );
     D_ASSERT( desc != NULL );
     
     strcpy( desc->name, device->device_info.name );
     memcpy( &desc->driver, &device->info, sizeof(FSSoundDriverInfo) );
} 

DeviceCapabilities
fs_device_get_capabilities( CoreSoundDevice *device )
{
     D_ASSERT( device != NULL );
     
     return device->device_info.caps;
}
   
DirectResult 
fs_device_get_buffer( CoreSoundDevice  *device,
                      u8              **addr,
                      unsigned int     *avail )
{
     D_ASSERT( device != NULL );
     D_ASSERT( addr != NULL );
     D_ASSERT( avail != NULL );
     
     if (device->funcs)
          return device->funcs->GetBuffer( device->device_data, addr, avail );
          
     return DR_UNSUPPORTED;
}

DirectResult
fs_device_commit_buffer( CoreSoundDevice *device, unsigned int frames )
{
     D_ASSERT( device != NULL );
     
     if (device->funcs)
          return device->funcs->CommitBuffer( device->device_data, frames );
          
     return DR_UNSUPPORTED;
}

void
fs_device_get_output_delay( CoreSoundDevice *device, int *delay )
{
     D_ASSERT( device != NULL );
     D_ASSERT( delay != NULL );
     
     if (device->funcs)
          device->funcs->GetOutputDelay( device->device_data, delay );
     else
          *delay = 0;
}

DirectResult
fs_device_get_volume( CoreSoundDevice *device, float *level )
{
     D_ASSERT( device != NULL );
     D_ASSERT( level != NULL );
     
     if (device->funcs)
          return device->funcs->GetVolume( device->device_data, level );
     
     return DR_UNSUPPORTED;
}

DirectResult
fs_device_set_volume( CoreSoundDevice *device, float level )
{
     D_ASSERT( device != NULL );
     
     if (device->funcs)
          return device->funcs->SetVolume( device->device_data, level );
          
     return DR_UNSUPPORTED;
}

DirectResult
fs_device_suspend( CoreSoundDevice *device )
{
     D_ASSERT( device != NULL );
     
     if (device->funcs)
          return device->funcs->Suspend( device->device_data );
          
     return DR_OK;
}

DirectResult
fs_device_resume( CoreSoundDevice *device )
{
     D_ASSERT( device != NULL );
     
     if (device->funcs)
          return device->funcs->Resume( device->device_data );
          
     return DR_OK;
}

void
fs_device_handle_fork( CoreSoundDevice  *device,
                       FusionForkAction  action,
                       FusionForkState   state )
{
     D_ASSERT( device != NULL );
     
     if (device->funcs)
          device->funcs->HandleFork( device->device_data, action, state );
}

void
fs_device_shutdown( CoreSoundDevice *device )
{
     D_ASSERT( device != NULL );
     
     if (device->funcs)
          device->funcs->CloseDevice( device->device_data );
     
     if (device->module)
          direct_module_unref( device->module );
     
     if (device->device_data)
          D_FREE( device->device_data );
     
     D_FREE( device );
}

