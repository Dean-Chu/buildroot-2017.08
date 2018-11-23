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

DFBScreenID IDirectFBScreen::GetID()
{
     DFBScreenID screen_id;

     DFBCHECK( iface->GetID (iface, &screen_id) );

     return screen_id;
}

DFBScreenDescription IDirectFBScreen::GetDescription()
{
     DFBScreenDescription desc;

     DFBCHECK( iface->GetDescription (iface, &desc) );

     return desc;
}

void IDirectFBScreen::GetSize (int *width,
                               int *height)
{
     DFBCHECK( iface->GetSize (iface, width, height) );
}

void IDirectFBScreen::EnumDisplayLayers (DFBDisplayLayerCallback  callback,
                                         void                    *callbackdata)
{
     DFBCHECK( iface->EnumDisplayLayers (iface, callback, callbackdata) );
}

void IDirectFBScreen::WaitForSync()
{
     DFBCHECK( iface->WaitForSync (iface) );
}

void IDirectFBScreen::SetPowerMode (DFBScreenPowerMode mode)
{
     DFBCHECK( iface->SetPowerMode (iface, mode) );
}

void IDirectFBScreen::GetMixerDescriptions (DFBScreenMixerDescription *descriptions)
{
     DFBCHECK( iface->GetMixerDescriptions (iface, descriptions) );
}

void IDirectFBScreen::GetMixerConfiguration (int                   mixer,
                                             DFBScreenMixerConfig *config)
{
     DFBCHECK( iface->GetMixerConfiguration (iface, mixer, config) );
}

void IDirectFBScreen::TestMixerConfiguration (int                         mixer,
                                              const DFBScreenMixerConfig &config,
                                              DFBScreenMixerConfigFlags  *failed)
{
     DFBCHECK( iface->TestMixerConfiguration (iface, mixer, &config, failed) );
}

void IDirectFBScreen::SetMixerConfiguration (int                         mixer,
                                             const DFBScreenMixerConfig &config)
{
     DFBCHECK( iface->SetMixerConfiguration (iface, mixer, &config) );
}


void IDirectFBScreen::GetEncoderDescriptions (DFBScreenEncoderDescription *descriptions)
{
     DFBCHECK( iface->GetEncoderDescriptions (iface, descriptions) );
}

void IDirectFBScreen::GetEncoderConfiguration (int                     encoder,
                                               DFBScreenEncoderConfig *config)
{
     DFBCHECK( iface->GetEncoderConfiguration (iface, encoder, config) );
}

void IDirectFBScreen::TestEncoderConfiguration (int                           encoder,
                                                const DFBScreenEncoderConfig &config,
                                                DFBScreenEncoderConfigFlags  *failed)
{
     DFBCHECK( iface->TestEncoderConfiguration (iface, encoder, &config, failed) );
}

void IDirectFBScreen::SetEncoderConfiguration (int                           encoder,
                                               const DFBScreenEncoderConfig &config)
{
     DFBCHECK( iface->SetEncoderConfiguration (iface, encoder, &config) );
}


void IDirectFBScreen::GetOutputDescriptions (DFBScreenOutputDescription *descriptions)
{
     DFBCHECK( iface->GetOutputDescriptions (iface, descriptions) );
}

void IDirectFBScreen::GetOutputConfiguration (int                    output,
                                              DFBScreenOutputConfig *config)
{
     DFBCHECK( iface->GetOutputConfiguration (iface, output, config) );
}

void IDirectFBScreen::TestOutputConfiguration (int                          output,
                                               const DFBScreenOutputConfig &config,
                                               DFBScreenOutputConfigFlags  *failed)
{
     DFBCHECK( iface->TestOutputConfiguration (iface, output, &config, failed) );
}

void IDirectFBScreen::SetOutputConfiguration (int                          output,
                                              const DFBScreenOutputConfig &config)
{
     DFBCHECK( iface->SetOutputConfiguration (iface, output, &config) );
}

