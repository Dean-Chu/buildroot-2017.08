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

   This file is subject to the terms and conditions of the MIT License:

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without restriction,
   including without limitation the rights to use, copy, modify, merge,
   publish, distribute, sublicense, and/or sell copies of the Software,
   and to permit persons to whom the Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/


#include <iostream>

#include "dfbapp.h"

DFBApp::DFBApp()
{
     m_width         = 0;
     m_height        = 0;
     m_flipping      = true;
     m_premultiplied = true;
}

DFBApp::~DFBApp()
{
}

bool
DFBApp::Init( int argc, char *argv[] )
{
     DFBSurfaceDescription dsc;

     /* parse application arguments */
     if (!ParseArgs( argc, argv ))
          return false;

     /* create super interface */
     m_dfb = DirectFB::Create();

     try {
          /* try fullscreen mode */
          m_dfb.SetCooperativeLevel( DFSCL_FULLSCREEN );
     }
     catch (DFBException *ex) {
          if (ex->GetResultCode() == DFB_ACCESSDENIED)
               std::cerr << "Warning: " << ex << std::endl;
          else
               throw;
     }

     /* fill surface description */
     dsc.flags = DSDESC_CAPS;
     dsc.caps  = DSCAPS_PRIMARY;

     if (m_flipping)
          DFB_ADD_SURFACE_CAPS( dsc.caps, DSCAPS_FLIPPING );

     if (m_premultiplied)
          DFB_ADD_SURFACE_CAPS( dsc.caps, DSCAPS_PREMULTIPLIED );

     if (m_premultiplied)
          DFB_ADD_SURFACE_CAPS( dsc.caps, DSCAPS_PREMULTIPLIED );

     if (m_width) {
          DFB_ADD_SURFACE_DESC( dsc.flags, DSDESC_WIDTH );
          dsc.width  = m_width;
     }

     if (m_height) {
          DFB_ADD_SURFACE_DESC( dsc.flags, DSDESC_HEIGHT );
          dsc.height = m_height;
     }

     /* create the primary surface */
     m_primary = m_dfb.CreateSurface( dsc );

     /* create an event buffer with all devices attached */
     m_events = m_dfb.CreateInputEventBuffer( DICAPS_ALL );


     /* get the screen resolution */
     int width;
     int height;

     m_primary.GetSize( &width, &height );

     /* call setup method */
     return Setup( width, height );
}

void
DFBApp::Run()
{
     while (true) {
          DFBInputEvent event;

          /* render to the screen */
          Render( m_primary );

          /* flip the screen */
          if (m_flipping)
               m_primary.Flip();

          /* wait for an event if Update() returns true */
          if (Update())
               m_events.WaitForEvent();

          /* handle all events, exit if HandleEvent() returns true */
          while (m_events.GetEvent( DFB_EVENT(&event) ))
               if (HandleEvent( event ))
                    return;
     }
}

void
DFBApp::SetMode( int width, int height )
{
     m_width  = width;
     m_height = height;
}

void
DFBApp::SetFlipping( bool flipping )
{
     m_flipping = flipping;
}

bool
DFBApp::ParseArgs( int argc, char *argv[] )
{
     return true;
}

bool
DFBApp::Setup( int width, int height )
{
     return true;
}

void
DFBApp::Render( IDirectFBSurface &surface )
{
     surface.Clear();
}

bool
DFBApp::Update()
{
     return true;
}

bool
DFBApp::HandleEvent( DFBInputEvent &event )
{
     switch (event.type) {
          case DIET_KEYPRESS:
               if (event.key_symbol == DIKS_ESCAPE)
                    return true;
               break;

          default:
               break;
     }

     return false;
}

