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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fusionsound.h>

#include <direct/debug.h>
#include <direct/interface.h>
#include <direct/mem.h>
#include <direct/messages.h>

#include <voodoo/conf.h>
#include <voodoo/server.h>

/*****************************************************************************/

static bool         parse_command_line( int argc, char *argv[] );
static DirectResult server_run( void );

static bool keep_alive;

/*****************************************************************************/

int
main( int argc, char *argv[] )
{
     DirectResult ret;

     /* Initialize FusionSound including command line parsing. */
     ret = FusionSoundInit( &argc, &argv );
     if (ret) {
          FusionSoundError( "FusionSoundInit() failed", ret );
          return -1;
     }

     /* Parse the command line. */
     if (!parse_command_line( argc, argv ))
          return -2;

     /* Run the server. */
     return server_run();
}

/*****************************************************************************/

static DirectResult
ConstructDispatcher( VoodooServer     *server,
                     VoodooManager    *manager,
                     const char       *name,
                     void             *ctx,
                     VoodooInstanceID *ret_instance )
{
     DirectResult          ret;
     DirectInterfaceFuncs *funcs;
     void                 *interface;
     VoodooInstanceID      instance;

     D_ASSERT( server != NULL );
     D_ASSERT( manager != NULL );
     D_ASSERT( name != NULL );
     D_ASSERT( ret_instance != NULL );

     ret = DirectGetInterface( &funcs, name, "Dispatcher", NULL, NULL );
     if (ret)
          return ret;

     ret = funcs->Allocate( &interface );
     if (ret)
          return ret;

     ret = funcs->Construct( interface, manager, &instance );
     if (ret)
          return ret;

     *ret_instance = instance;

     return DR_OK;
}

/*****************************************************************************/

static void
usage( const char *progname )
{
     fprintf( stderr, 
              "fsproxy v%s\n"
              "\n"
              "Usage: %s [options]\n"
              "\n"
              "Options:\n"
              "   -h, --help        Show this help\n"
              "   -v, --version     Print version and quit\n"
              "   -k, --keep-alive  Keep listening for new connections\n"
              "\n",
              DIRECTFB_VERSION, progname );
}

static bool
parse_command_line( int argc, char *argv[] )
{
     int i;

     for (i = 1; i < argc; i++) {
          if (!strcmp( argv[i], "-h" ) || !strcmp( argv[i], "--help" )) {
               usage( argv[0] );
               return false;
          }
          else if (!strcmp( argv[i], "-v" ) || !strcmp( argv[i], "--version" )) {
               puts( DIRECTFB_VERSION );
               exit( 0 );
          }
          else if (!strcmp( argv[i], "-k" ) || !strcmp( argv[i], "--keep-alive" )) {
               keep_alive = true;
          }
          else {
               fprintf( stderr, "Unsupported option '%s'!\n", argv[i] );
               return false;
          }
     }
     
     return true;
}

static DirectResult
server_run( void )
{
     DirectResult  ret;
     VoodooServer *server;

     ret = voodoo_server_create( NULL, 0, voodoo_config->server_fork, &server );
     if (ret) {
          D_ERROR( "Voodoo/Proxy: Could not create the server (%s)!\n", FusionSoundErrorString(ret) );
          return ret;
     }

     ret = voodoo_server_register( server, "IFusionSound", ConstructDispatcher, NULL );
     if (ret) {
          D_ERROR( "Voodoo/Proxy: Could not register super interface 'IDirectFB'!\n" );
          voodoo_server_destroy( server );
          return ret;
     }

     do {
          ret = voodoo_server_run( server );
          if (ret) {
               D_ERROR( "Voodoo/Proxy: Server exiting with error (%s)!\n", FusionSoundErrorString(ret) );
               break;
          }
     } while (keep_alive);

     voodoo_server_destroy( server );

     return DR_OK;
}

