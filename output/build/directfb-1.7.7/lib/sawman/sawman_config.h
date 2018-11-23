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

#ifndef __SAWMAN__SAWMAN_CONFIG_H__
#define __SAWMAN__SAWMAN_CONFIG_H__

#include <directfb.h>
#include <sawman.h>

typedef struct {
     int                   thickness;
     DFBDimension          resolution;
     DFBSurfacePixelFormat format;
     DFBColor              focused[4];
     DFBColor              unfocused[4];
     int                   focused_index[4];
     int                   unfocused_index[4];
} SaWManBorderInit;

typedef struct {
     SaWManBorderInit     *border;
     SaWManBorderInit      borders[3];

     bool                  show_empty;  /* Don't hide layer when no window is visible. */

     unsigned int          flip_once_timeout;

     struct {
          bool                     hw;
          DFBDisplayLayerID        layer_id;
     }                     cursor;

     DFBDimension          resolution;

     bool                  static_layer;

     int                   update_region_mode;

     bool                  keep_implicit_key_grabs;

     DFBDimension          passive3d_mode;

     bool                  hide_cursor_without_window;
} SaWManConfig;


extern SaWManConfig *sawman_config;


/*
 * Allocate Config struct, fill with defaults and parse command line options
 * for overrides. Options identified as SaWMan options are stripped out
 * of the array.
 */
DirectResult sawman_config_init( int *argc, char **argv[] );

/* remove config structure, if there is any */
DirectResult sawman_config_shutdown( void );

/*
 * Set individual option. Used by sawman_config_init(), and SaWManSetOption()
 */
DirectResult sawman_config_set( const char *name, const char *value );

const char *sawman_config_usage( void );


#endif /* __SAWMAN__SAWMAN_CONFIG_H__ */

