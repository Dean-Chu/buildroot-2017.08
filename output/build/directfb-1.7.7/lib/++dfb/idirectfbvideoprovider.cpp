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

DFBVideoProviderCapabilities IDirectFBVideoProvider::GetCapabilities()
{
     DFBVideoProviderCapabilities caps;

     DFBCHECK( iface->GetCapabilities (iface, &caps) );

     return caps;
}

void IDirectFBVideoProvider::GetSurfaceDescription (DFBSurfaceDescription *dsc)
{
     DFBCHECK( iface->GetSurfaceDescription (iface, dsc) );
}

void IDirectFBVideoProvider::GetStreamDescription (DFBStreamDescription *dsc)
{
     DFBCHECK( iface->GetStreamDescription (iface, dsc) );
}

void IDirectFBVideoProvider::PlayTo (IDirectFBSurface *destination,
                                     DFBRectangle     *destination_rect,
                                     DVFrameCallback   callback,
                                     void             *ctx)
{
     DFBCHECK( iface->PlayTo (iface, destination->get_iface(),
                              destination_rect, callback, ctx) );
}

void IDirectFBVideoProvider::Stop()
{
     DFBCHECK( iface->Stop (iface) );
}

DFBVideoProviderStatus IDirectFBVideoProvider::GetStatus()
{
     DFBVideoProviderStatus status;

     DFBCHECK( iface->GetStatus (iface, &status) );

     return status;
}

void IDirectFBVideoProvider::SeekTo (double seconds)
{
     DFBCHECK( iface->SeekTo (iface, seconds) );
}

double IDirectFBVideoProvider::GetPos()
{
     double pos;

     DFBCHECK( iface->GetPos (iface, &pos) );

     return pos;
}

double IDirectFBVideoProvider::GetLength()
{
     double length;

     DFBCHECK( iface->GetPos (iface, &length) );

     return length;
}

void IDirectFBVideoProvider::GetColorAdjustment (DFBColorAdjustment *adj)
{
     DFBCHECK( iface->GetColorAdjustment (iface, adj) );
}

void IDirectFBVideoProvider::SetColorAdjustment (DFBColorAdjustment &adj)
{
     DFBCHECK( iface->SetColorAdjustment (iface, &adj) );
}

void IDirectFBVideoProvider::SendEvent (DFBEvent &evt)
{
     DFBCHECK( iface->SendEvent (iface, &evt) );
}

void IDirectFBVideoProvider::SetPlaybackFlags (DFBVideoProviderPlaybackFlags flags)
{
     DFBCHECK( iface->SetPlaybackFlags (iface, flags) );
}

void IDirectFBVideoProvider::SetSpeed (double multiplier)
{
     DFBCHECK( iface->SetSpeed (iface, multiplier) );
}

double IDirectFBVideoProvider::GetSpeed()
{
     double multiplier = -1;

     DFBCHECK( iface->GetSpeed (iface, &multiplier) );

     return multiplier;
}

void IDirectFBVideoProvider::SetVolume (float level)
{
     DFBCHECK( iface->SetVolume (iface, level) );
}

float IDirectFBVideoProvider::GetVolume()
{
     float level = -1;
     
     DFBCHECK( iface->GetVolume (iface, &level) );

     return level;
 }
