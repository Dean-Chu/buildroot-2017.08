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


#include "++dfb.h"
#include "++dfb_internal.h"

int IDirectFBFont::GetAscender() const
{
     int ascender;

     DFBCHECK( iface->GetAscender (iface, &ascender) );

     return ascender;
}

int IDirectFBFont::GetDescender() const
{
     int descender;

     DFBCHECK( iface->GetDescender (iface, &descender) );

     return descender;
}

int IDirectFBFont::GetHeight() const
{
     int height;

     DFBCHECK( iface->GetHeight (iface, &height) );

     return height;
}

int IDirectFBFont::GetMaxAdvance() const
{
     int max_advance;

     DFBCHECK( iface->GetMaxAdvance (iface, &max_advance) );

     return max_advance;
}

void IDirectFBFont::GetKerning (unsigned int  prev_index,
                                unsigned int  current_index,
                                int          *kern_x,
                                int          *kern_y) const
{
     DFBCHECK( iface->GetKerning (iface, prev_index, current_index, kern_x, kern_y) );
}

void IDirectFBFont::GetStringBreak (const char  *text,
                                    int          bytes,
                                    int          max_width,
                                    int         *ret_width,
                                    int         *ret_str_length,
                                    const char **ret_next_line) const
{
     DFBCHECK( iface->GetStringBreak(iface, text, bytes, max_width, ret_width, ret_str_length, ret_next_line) );
}

int IDirectFBFont::GetStringWidth (const char *text, int bytes) const
{
     int width;

     DFBCHECK( iface->GetStringWidth (iface, text, bytes, &width) );

     return width;
}

void IDirectFBFont::GetStringExtents (const char   *text,
                                      int           bytes,
                                      DFBRectangle *logical_rect,
                                      DFBRectangle *ink_rect) const
{
     DFBCHECK( iface->GetStringExtents (iface, text, bytes,
                                        logical_rect, ink_rect) );
}

void IDirectFBFont::GetGlyphExtents  (unsigned int  index,
                                      DFBRectangle *rect,
                                      int          *advance) const
{
     DFBCHECK( iface->GetGlyphExtents (iface, index, rect, advance) );
}

void IDirectFBFont::SetEncoding (DFBTextEncodingID encoding)
{
     DFBCHECK( iface->SetEncoding (iface, encoding) );
}

void IDirectFBFont::EnumEncodings (DFBTextEncodingCallback  callback,
                                   void                    *callbackdata)
{
     DFBCHECK( iface->EnumEncodings (iface, callback, callbackdata) );
}

void IDirectFBFont::FindEncoding (const char        *name,
                                  DFBTextEncodingID *encoding)
{
     DFBCHECK( iface->FindEncoding (iface, name, encoding) );
}

