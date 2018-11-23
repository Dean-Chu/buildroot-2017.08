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


#include "config.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>

#include <fcntl.h>

#include <directfb.h>

#include <core/core.h>
#include <core/input.h>

#include <direct/mem.h>
#include <direct/messages.h>
#include <direct/thread.h>

#include <fusion/fusion.h>

#define DFB_INPUTDRIVER_HAS_AXIS_INFO

#include <core/input_driver.h>
#include <directfb_version.h>

DFB_INPUT_DRIVER( divine )


#define PIPE_PATH "/tmp/divine"

/*
 * declaration of private data
 */
typedef struct {
     char                pipename[256];
     int                 fd;
     CoreInputDevice    *device;
     DirectThread       *thread;
} DiVineData;


/*
 * Input thread reading from pipe.
 * Directly passes read events to input core.
 */
static void*
divineEventThread( DirectThread *thread, void *driver_data )
{
     DiVineData     *data = (DiVineData*) driver_data;
     DFBInputEvent   event;
     struct pollfd   pfd;

     /* fill poll info */
     pfd.fd     = data->fd;
     pfd.events = POLLIN;

     /* wait for the next event */
     while (poll( &pfd, 1, -1 ) > 0 || errno == EINTR) {
          /* read the next event from the pipe */
          if (read( data->fd, &event, sizeof(DFBInputEvent) ) == sizeof(DFBInputEvent)) {
               /* directly dispatch the event */
               dfb_input_dispatch( data->device, &event );
          }
          else {
               if (!strcmp( (const char*) &event, "STOP" ))
                    return NULL;

               // FIXME: handle poll/read errors
               usleep( 20000 ); /* avoid 100% CPU usage in case poll() doesn't work */
          }
     }

     D_PERROR( "divine thread died\n" );

     return NULL;
}

/* exported symbols */

/*
 * Return the number of available devices.
 * Called once during initialization of DirectFB.
 */
static int
driver_get_available()
{
     int  fd;
     int  world_index;
     char buf[256];

     /* avoid clash with other DirectFB sessions, but keep compatible for default session */
     world_index = fusion_world_index( dfb_core_world( NULL ) );
     if (world_index != 0)
          snprintf( buf, sizeof(buf), "%s.%d", PIPE_PATH, world_index );
     else
          snprintf( buf, sizeof(buf), "%s", PIPE_PATH );

     /* create the pipe if not already existent */
     if (mkfifo( buf, 0660 ) && errno != EEXIST) {
          D_PERROR( "DirectFB/DiVine: could not create pipe '%s'\n", buf );
          return 0;
     }

     /* try to open pipe */
     fd = open( buf, O_RDONLY | O_NONBLOCK );
     if (fd < 0) {
          D_PERROR( "DirectFB/DiVine: could not open pipe '%s'\n", buf );
          return 0;
     }

     /* close pipe */
     close( fd );

     return 1;
}

/*
 * Fill out general information about this driver.
 * Called once during initialization of DirectFB.
 */
static void
driver_get_info( InputDriverInfo *info )
{
     /* fill driver info structure */
     snprintf( info->name,
               DFB_INPUT_DRIVER_INFO_NAME_LENGTH, "DiVine Driver" );
     snprintf( info->vendor,
               DFB_INPUT_DRIVER_INFO_VENDOR_LENGTH, "Convergence GmbH" );

     info->version.major = DIRECTFB_MAJOR_VERSION;
     info->version.minor = DIRECTFB_MINOR_VERSION;
}

/*
 * Open the pipe, fill out information about device,
 * allocate and fill private data, start input thread.
 * Called during initialization, resuming or taking over mastership.
 */
static DFBResult
driver_open_device( CoreInputDevice  *device,
                    unsigned int      number,
                    InputDeviceInfo  *info,
                    void            **driver_data )
{
     DiVineData *data;
     int         world_index;

     /* allocate and fill private data */
     data = D_CALLOC( 1, sizeof(DiVineData) );
     if (!data)
          return D_OOM();

     data->device = device;

     /* avoid clash with other DirectFB sessions, but keep compatible for default session */
     world_index = fusion_world_index( dfb_core_world( NULL ) );
     if (world_index != 0)
          snprintf( data->pipename, sizeof(data->pipename), "%s.%d", PIPE_PATH, world_index );
     else
          snprintf( data->pipename, sizeof(data->pipename), "%s", PIPE_PATH );

     /* open pipe */
     data->fd = open( data->pipename, O_RDWR | O_NONBLOCK );
     if (data->fd < 0) {
          D_PERROR( "DirectFB/DiVine: could not open pipe '%s'\n", data->pipename );
          D_FREE( data );
          return DFB_INIT;
     }

     /* set device name */
     snprintf( info->desc.name,
               DFB_INPUT_DEVICE_DESC_NAME_LENGTH, "Virtual Input" );

     /* set device vendor */
     snprintf( info->desc.vendor,
               DFB_INPUT_DEVICE_DESC_VENDOR_LENGTH, "DirectFB" );

     /* set one of the primary input device IDs */
     info->prefered_id = DIDID_ANY;

     /* set type flags */
     info->desc.type = DIDTF_KEYBOARD | DIDTF_MOUSE |
                       DIDTF_JOYSTICK | DIDTF_REMOTE | DIDTF_VIRTUAL;

     /* set capabilities */
     info->desc.caps     = DICAPS_ALL;
     info->desc.max_axis = DIAI_LAST;

     /* start input thread */
     data->thread = direct_thread_create( DTT_INPUT, divineEventThread, data, "Virtual Input" );

     /* set private data pointer */
     *driver_data = data;

     return DFB_OK;
}

/*
 * Fetch one entry from the device's keymap if supported.
 */
static DFBResult
driver_get_keymap_entry( CoreInputDevice               *device,
                         void                      *driver_data,
                         DFBInputDeviceKeymapEntry *entry )
{
     return DFB_UNSUPPORTED;
}

static DFBResult
driver_get_axis_info( CoreInputDevice              *device,
                      void                         *driver_data,
                      DFBInputDeviceAxisIdentifier  axis,
                      DFBInputDeviceAxisInfo       *ret_info )
{
     ret_info->flags   = DIAIF_ABS_MIN | DIAIF_ABS_MAX;
     ret_info->abs_min = 0;
     ret_info->abs_max = 65535;

     return DFB_OK;
}

/*
 * End thread, close device and free private data.
 */
static void
driver_close_device( void *driver_data )
{
     DiVineData *data = (DiVineData*) driver_data;

     write( data->fd, "STOP", 5 );

     /* stop input thread */
     direct_thread_join( data->thread );
     direct_thread_destroy( data->thread );

     /* close pipe */
     close( data->fd );

     /* free private data */
     D_FREE( data );
}
