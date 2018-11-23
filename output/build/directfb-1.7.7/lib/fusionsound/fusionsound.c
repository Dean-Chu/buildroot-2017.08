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

#include <fusionsound.h>
#include <fusionsound_version.h>

#include <direct/debug.h>
#include <direct/interface.h>
#include <direct/log.h>
#include <direct/messages.h>
#include <direct/util.h>

#include <ifusionsound.h>

#include <misc/sound_conf.h>


IFusionSound *ifusionsound_singleton = NULL;

/**************************************************************************************************/

static DirectResult CreateRemote( const char    *host,
                               int            session,
                               IFusionSound **ret_interface );

/**************************************************************************************************/

/*
 * Version checking
 */
const unsigned int fusionsound_major_version = FUSIONSOUND_MAJOR_VERSION;
const unsigned int fusionsound_minor_version = FUSIONSOUND_MINOR_VERSION;
const unsigned int fusionsound_micro_version = FUSIONSOUND_MICRO_VERSION;
const unsigned int fusionsound_binary_age    = FUSIONSOUND_BINARY_AGE;
const unsigned int fusionsound_interface_age = FUSIONSOUND_INTERFACE_AGE;

const char *
FusionSoundCheckVersion( unsigned int required_major,
                         unsigned int required_minor,
                         unsigned int required_micro )
{
     if (required_major > FUSIONSOUND_MAJOR_VERSION)
          return "FusionSound version too old (major mismatch)";
     if (required_major < FUSIONSOUND_MAJOR_VERSION)
          return "FusionSound version too new (major mismatch)";
     if (required_minor > FUSIONSOUND_MINOR_VERSION)
          return "FusionSound version too old (minor mismatch)";
     if (required_minor < FUSIONSOUND_MINOR_VERSION)
          return "FusionSound version too new (minor mismatch)";
     if (required_micro < FUSIONSOUND_MICRO_VERSION - FUSIONSOUND_BINARY_AGE)
          return "FusionSound version too new (micro mismatch)";
     if (required_micro > FUSIONSOUND_MICRO_VERSION)
          return "FusionSound version too old (micro mismatch)";

     return NULL;
}

const char *
FusionSoundUsageString( void )
{
     return fs_config_usage();
}

DirectResult
FusionSoundInit( int *argc, char **argv[] )
{
     return fs_config_init( argc, argv );
}

DirectResult
FusionSoundSetOption( const char *name, const char *value )
{
     if (fs_config == NULL) {
          D_ERROR( "FusionSoundSetOption: FusionSoundInit has to be called first!\n" );
          return DR_INIT;
     }

     if (ifusionsound_singleton) {
          D_ERROR( "FusionSoundSetOption: FusionSoundCreate has already been called!\n" );
          return DR_INIT;
     }

     if (!name)
          return DR_INVARG;

     return fs_config_set( name, value );
}

DirectResult
FusionSoundCreate( IFusionSound **ret_interface )
{
     DirectResult ret;
     
     if (!fs_config) {
          D_ERROR( "FusionSoundCreate: FusionSoundInit has to be called first!\n" );
          return DR_INIT;
     }

     if (!ret_interface)
          return DR_INVARG;
          
     if (ifusionsound_singleton) {
          ifusionsound_singleton->AddRef( ifusionsound_singleton );
          *ret_interface = ifusionsound_singleton;
          return DR_OK;
     }

     if (!direct_config->quiet && fs_config->banner) {
          direct_log_printf( NULL,
               "\n"
               "       --------------------- FusionSound v%d.%d.%d -------------------\n"
               "             (c) 2000-2002  convergence integrated media GmbH  \n"
               "             (c) 2002-2006  convergence GmbH                   \n"
               "        -----------------------------------------------------------\n"
               "\n",
               FUSIONSOUND_MAJOR_VERSION, FUSIONSOUND_MINOR_VERSION, FUSIONSOUND_MICRO_VERSION );
     }

     if (fs_config->remote.host)
          return CreateRemote( fs_config->remote.host, fs_config->remote.session, ret_interface );
          
     DIRECT_ALLOCATE_INTERFACE( ifusionsound_singleton, IFusionSound );
     
     ret = IFusionSound_Construct( ifusionsound_singleton );
     if (ret != DR_OK)
          ifusionsound_singleton = NULL;
          
     *ret_interface = ifusionsound_singleton;

     return ret;
}

DirectResult
FusionSoundError( const char *msg, DirectResult error )
{
     if (msg)
          fprintf( stderr, "(#) FusionSound Error [%s]: %s\n", msg, DirectResultString( error ) );
     else
          fprintf( stderr, "(#) FusionSound Error: %s\n", DirectResultString( error ) );

     return error;
}

DirectResult
FusionSoundErrorFatal( const char *msg, DirectResult error )
{
     FusionSoundError( msg, error );
     
     exit( error );
}

const char *
FusionSoundErrorString( DirectResult error )
{
     return DirectResultString( error );
}

/**************************************************************************************************/

static DirectResult
CreateRemote( const char *host, int session, IFusionSound **ret_interface )
{
     DirectResult          ret;
     DirectInterfaceFuncs *funcs;
     void                 *interface;

     D_ASSERT( host != NULL );
     D_ASSERT( ret_interface != NULL );

     ret = DirectGetInterface( &funcs, "IFusionSound", "Requestor", NULL, NULL );
     if (ret)
          return ret;

     ret = funcs->Allocate( &interface );
     if (ret)
          return ret;

     ret = funcs->Construct( interface, host, session );
     if (ret)
          return ret;

     *ret_interface = interface;

     return DR_OK;
}
