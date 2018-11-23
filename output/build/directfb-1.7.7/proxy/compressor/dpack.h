/*
   (c) Copyright 2012-2013  DirectFB integrated media GmbH
   (c) Copyright 2001-2013  The world wide DirectFB Open Source Community (directfb.org)
   (c) Copyright 2000-2004  Convergence (integrated media) GmbH

   All rights reserved.

   Written by Claudio Ciccani <klan@users.sf.net>

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

#ifndef __DPACK_H__
#define __DPACK_H__

#include <fusionsound.h>

#define DPACK_SCALE   1
#define DPACK_FRAMES  576

#define DPACK_MAX_PACKET( length, channels, bytes_per_frames ) \
     ((length) * (bytes_per_frames) + (channels) * (((length) + DPACK_FRAMES - 1) / DPACK_FRAMES))
     
int dpack_encode( const void *source, FSSampleFormat format, int channels, int length, u8 *dest );
int dpack_decode( const u8 *source, FSSampleFormat format, int channels, int length, void *dest );

#endif
