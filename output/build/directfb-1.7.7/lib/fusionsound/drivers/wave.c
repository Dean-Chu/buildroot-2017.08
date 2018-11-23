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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <direct/mem.h>
#include <direct/messages.h>
#include <direct/util.h>

#include <fusionsound.h>

#include <core/sound_device.h>
#include <core/sound_driver.h>

#include <misc/sound_conf.h>


FS_SOUND_DRIVER( wave )

/******************************************************************************/

typedef struct {
     int   fd;
     
     int   bits;
     int   channels;
     
     void *buffer;
     int   buffersize;
} WaveDeviceData;

/******************************************************************************/

typedef struct {
     u32 ChunkID;
     u32 ChunkSize;
     u32 Format;
     u32 Subchunk1ID;
     u32 Subchunk1Size;
     u16 AudioFormat;
     u16 NumChannels;
     u32 SampleRate;
     u32 ByteRate;
     u16 BlockAlign;
     u16 BitsPerSample;
     u32 Subchunk2ID;
     u32 Subchunk2Size;
} WaveHeader;

#define FCC( a, b, c, d ) ((a) | ((b)<<8) | ((c)<<16) | ((d)<<24))

/******************************************************************************/


static DirectResult
device_probe( void )
{
     int  fd;
     char path[4096];

     /* load only when requested */
     if (!fs_config->driver)
          return DR_UNSUPPORTED;

     if (fs_config->device) {
          snprintf( path, sizeof(path), "%s", fs_config->device );
     }
     else {
          snprintf( path, sizeof(path),
                    "./fusionsound-%d.wav", fs_config->session );
     }

     fd = open( path, O_WRONLY | O_CREAT | O_NOCTTY | O_NONBLOCK, 0644 );
     if (fd < 0)
          return DR_UNSUPPORTED;

     close( fd );

     return DR_OK;
}

static void
device_get_driver_info( SoundDriverInfo *info )
{
     snprintf( info->name,
               FS_SOUND_DRIVER_INFO_NAME_LENGTH,
               "Wave" );

     snprintf( info->vendor,
               FS_SOUND_DRIVER_INFO_VENDOR_LENGTH,
               "directfb.org" );

     snprintf( info->url,
               FS_SOUND_DRIVER_INFO_URL_LENGTH,
               "http://www.directfb.org" );

     snprintf( info->license,
               FS_SOUND_DRIVER_INFO_LICENSE_LENGTH,
               "LGPL" );

     info->version.major = 0;
     info->version.minor = 2;

     info->device_data_size = sizeof(WaveDeviceData);
}

static DirectResult
device_open( void                  *device_data,
             SoundDeviceInfo       *device_info,
             CoreSoundDeviceConfig *config )
{
     WaveDeviceData *data   = device_data;
     WaveHeader      header;
     char            path[4096];

     if (config->format == FSSF_FLOAT)
          return DR_UNSUPPORTED;

     if (fs_config->device) {
          snprintf( path, sizeof(path), "%s", fs_config->device );
     }
     else {
          snprintf( path, sizeof(path),
                    "./fusionsound-%d.wav", fs_config->session );
     }

     data->fd = open( path, O_WRONLY | O_CREAT | O_TRUNC | O_NOCTTY, 0644 );
     if (data->fd < 0) {
          D_ERROR( "FusionSound/Device/Wave: "
                   "couldn't open '%s' for writing!\n", path );
          return DR_IO;
     }

     /* close file descriptor on exec */
     fcntl( data->fd, F_SETFD, FD_CLOEXEC );

     /* device name */
     snprintf( device_info->name,
               FS_SOUND_DEVICE_INFO_NAME_LENGTH,
               "%s", strrchr( path, '/' ) ? (strrchr( path, '/' )+1) : path );

     /* device capabilities */
     device_info->caps = 0;
     
     /* allocate output buffer */
     data->buffer = D_MALLOC( config->buffersize *
                              FS_CHANNELS_FOR_MODE(config->mode) *
                              FS_BYTES_PER_SAMPLE(config->format) );
     if (!data->buffer) {
          close( data->fd );
          return D_OOM();
     }
     
     data->buffersize = config->buffersize;


     header.ChunkID       = FCC('R','I','F','F');
     header.ChunkSize     = 0;
     header.Format        = FCC('W','A','V','E');
     header.Subchunk1ID   = FCC('f','m','t',' ');
     header.Subchunk1Size = 16;
     header.AudioFormat   = 1;
     header.NumChannels   = FS_CHANNELS_FOR_MODE(config->mode);
     header.SampleRate    = config->rate;
     header.ByteRate      = config->rate *
                            FS_CHANNELS_FOR_MODE(config->mode) *
                            FS_BYTES_PER_SAMPLE(config->format);
     header.BlockAlign    = FS_CHANNELS_FOR_MODE(config->mode) *
                            FS_BYTES_PER_SAMPLE(config->format);
     header.BitsPerSample = FS_BITS_PER_SAMPLE(config->format);
     header.Subchunk2ID   = FCC('d','a','t','a');
     header.Subchunk2Size = 0;
     
#ifdef WORDS_BIGENDIAN
     header.ChunkID       = BSWAP32(header.ChunkID);
     header.ChunkSize     = BSWAP32(header.ChunkSize);
     header.Format        = BSWAP32(header.Format);
     header.Subchunk1ID   = BSWAP32(header.Subchunk1ID);
     header.Subchunk1Size = BSWAP32(header.Subchunk1Size);
     header.AudioFormat   = BSWAP16(header.AudioFormat);
     header.NumChannels   = BSWAP16(header.NumChannels);
     header.SampleRate    = BSWAP32(header.SampleRate);
     header.ByteRate      = BSWAP32(header.ByteRate);
     header.BlockAlign    = BSWAP16(header.BlockAlign);
     header.BitsPerSample = BSWAP16(header.BitsPerSample);
     header.Subchunk2ID   = BSWAP32(header.Subchunk2ID);
     header.Subchunk2Size = BSWAP32(header.Subchunk2Size);
#endif

     if (write( data->fd, &header, sizeof(header) ) < sizeof(header)) {
          D_ERROR( "FusionSound/Device/Wave: write error!\n" );
          return DR_IO;
     }

     data->bits = FS_BITS_PER_SAMPLE(config->format);
     data->channels = FS_CHANNELS_FOR_MODE(config->mode);

     return DR_OK;
}

static DirectResult
device_get_buffer( void *device_data, u8 **addr, unsigned int *avail )
{
     WaveDeviceData *data = device_data;
     
     *addr = data->buffer;
     *avail = data->buffersize;
     
     return DR_OK;
}

static DirectResult
device_commit_buffer( void *device_data, unsigned int frames )
{
     WaveDeviceData *data = device_data;
     void           *buf  = data->buffer;
     
#ifdef WORDS_BIGENDIAN
     unsigned int    i;
     
     switch (data->bits) {
          case 16:
               for (i = 0; i < frames*data->channels; i++)
                    ((u16*)buf)[i] = BSWAP16(((u16*)buf)[i]);
               break;
          case 24:
               for (i = 0; i < frames*data->channels; i++) {
                    u8 tmp = ((u8*)buf)[i*3+0];
                    ((u8*)buf)[i*3+0] = ((u8*)buf)[i*3+2];
                    ((u8*)buf)[i*3+2] = tmp;
               }
               break;
          case 32:
               for (i = 0; i < frames*data->channels; i++)
                    ((u32*)buf)[i] = BSWAP32(((u32*)buf)[i]);
               break;
          default:
               break;
     }
#endif

     write( data->fd, buf, frames * data->channels * data->bits >> 3 );
     
     return DR_OK;
}

static void
device_get_output_delay( void *device_data, int *delay )
{
     *delay = 0;
}

static DirectResult
device_get_volume( void *device_data, float *level )
{
     return DR_UNSUPPORTED;
}

static DirectResult
device_set_volume( void *device_data, float level )
{
     return DR_UNSUPPORTED;
}

static DirectResult
device_suspend( void *device_data )
{
     WaveDeviceData *data = device_data;
     struct stat     st;
     
     if (fstat( data->fd, &st ) == 0 && !S_ISFIFO(st.st_mode)) {
          close( data->fd );
          data->fd = -1;
     }
     
     return DR_OK;
}

static DirectResult
device_resume( void *device_data )
{
     WaveDeviceData *data = device_data;
     char            path[4096];
     
     if (data->fd < 0) {             
          if (fs_config->device) {
               snprintf( path, sizeof(path), "%s", fs_config->device );
          }
          else {
               snprintf( path, sizeof(path),
                         "./fusionsound-%d.wav", fs_config->session );
          }

          data->fd = open( path, O_WRONLY | O_APPEND | O_NOCTTY );
          if (data->fd < 0) {
               D_ERROR( "FusionSound/Device/Wave: couldn't reopen '%s'!\n", path );
               return DR_IO;
          }

          fcntl( data->fd, F_SETFD, FD_CLOEXEC );
     }
     
     return DR_OK;
}

static void
device_handle_fork( void             *device_data,
                    FusionForkAction  action,
                    FusionForkState   state )
{
     WaveDeviceData *data = device_data;
     
     if (action == FFA_CLOSE && state == FFS_CHILD) {
          close( data->fd );
          data->fd = -1;
     }
}

static void
device_close( void *device_data )
{
     WaveDeviceData *data = device_data;
     
     if (data->buffer)
          D_FREE( data->buffer);

     if (data->fd >= 0) {
          off_t pos = lseek( data->fd, 0, SEEK_CUR );
          if (pos > 0) {
               u32 ChunkSize     = pos - 8;
               u32 Subchunk2Size = pos - sizeof(WaveHeader);
          
#ifdef WORDS_BIGENDIAN
               ChunkSize     = BSWAP32(ChunkSize);
               Subchunk2Size = BSWAP32(Subchunk2Size);
#endif
               if (lseek( data->fd, 4, SEEK_SET ) == 4)
                    write( data->fd, &ChunkSize, 4 );

               if (lseek( data->fd, 40, SEEK_SET ) == 40)
                    write( data->fd, &Subchunk2Size, 4 );
          }

          close( data->fd );
     }
}
