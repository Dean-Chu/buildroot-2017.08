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

#ifndef __SAWMAN_WINDOW_H__
#define __SAWMAN_WINDOW_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <directfb.h>

#include "sawman_internal.h"


DirectResult  sawman_switch_focus              ( SaWMan                  *sawman,
                                                 SaWManWindow            *to,
                                                 SaWManChangeFocusReason  reason );

DirectResult  sawman_post_event                ( SaWMan                *sawman,
                                                 SaWManWindow          *sawwin,
                                                 DFBWindowEvent        *event );

DirectResult  sawman_update_window             ( SaWMan                *sawman,
                                                 SaWManWindow          *sawwin,
                                                 const DFBRegion       *region,
                                                 DFBSurfaceFlipFlags    flags,
                                                 SaWManUpdateFlags      update_flags );

DirectResult  sawman_showing_window            ( SaWMan                *sawman,
                                                 SaWManWindow          *sawwin,
                                                 bool                  *ret_showing );

DirectResult  sawman_insert_window             ( SaWMan                *sawman,
                                                 SaWManWindow          *sawwin,
                                                 SaWManWindow          *relative,
                                                 bool                   top );

DirectResult  sawman_remove_window             ( SaWMan                *sawman,
                                                 SaWManWindow          *sawwin );

DirectResult  sawman_withdraw_window           ( SaWMan                *sawman,
                                                 SaWManWindow          *sawwin );

DirectResult  sawman_update_geometry           ( SaWManWindow          *sawwin );

DFBResult     sawman_restack_window            ( SaWMan                 *sawman,
                                                 SaWManWindow           *sawwin,
                                                 SaWManWindow           *relative,
                                                 int                     relation,
                                                 DFBWindowStackingClass  stacking );

DirectResult  sawman_set_opacity               ( SaWMan                *sawman,
                                                 SaWManWindow          *sawwin,
                                                 u8                     opacity );

DirectResult  sawman_set_stereo_depth          ( SaWMan                *sawman,
                                                 SaWManWindow          *sawwin,
                                                 int                    z );

DirectResult  sawman_window_set_cursor_flags   ( SaWMan                *sawman,
                                                 SaWManWindow          *sawwin,
                                                 DFBWindowCursorFlags   flags );

DirectResult  sawman_window_apply_cursor_flags ( SaWMan                *sawman,
                                                 SaWManWindow          *sawwin );

bool          sawman_update_focus              ( SaWMan                *sawman,
                                                 CoreWindowStack       *stack,
                                                 int                    x,
                                                 int                    y );

SaWManWindow *sawman_window_at_pointer         ( SaWMan                *sawman,
                                                 CoreWindowStack       *stack,
                                                 int                    x,
                                                 int                    y );

void          sawman_window_get_cursor_position( SaWMan                *sawman,
                                                 CoreWindowStack       *stack,
                                                 SaWManWindow          *sawwin,
                                                 int                   *ret_x,
                                                 int                   *ret_y,
                                                 int                   *ret_cx,
                                                 int                   *ret_cy );

int           sawman_window_border             ( const SaWManWindow    *sawwin );


void          sawman_update_visible            ( SaWMan                *sawman );

#ifdef __cplusplus
}
#endif

#endif

