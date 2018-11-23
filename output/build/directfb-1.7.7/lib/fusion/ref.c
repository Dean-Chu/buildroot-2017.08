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
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include <sys/param.h>
#include <sys/types.h>

#include <direct/map.h>
#include <direct/mem.h>


#include <fusion/build.h>
#include <fusion/conf.h>

#include <direct/debug.h>
#include <direct/messages.h>
#include <direct/util.h>

#include <fusion/types.h>
#include <fusion/ref.h>

#include "fusion_internal.h"

#include <signal.h>


D_DEBUG_DOMAIN( Fusion_Ref, "Fusion/Ref", "Fusion's Reference Counter" );


#if FUSION_BUILD_MULTI


#if FUSION_BUILD_KERNEL

DirectResult
fusion_ref_init (FusionRef         *ref,
                 const char        *name,
                 const FusionWorld *world)
{
     return fusion_ref_init2( ref, name, false, world );
}

DirectResult
fusion_ref_init2(FusionRef         *ref,
                 const char        *name,
                 bool               user,
                 const FusionWorld *world)
{
     FusionEntryInfo info;

     D_ASSERT( ref != NULL );
     D_ASSERT( name != NULL );
     D_MAGIC_ASSERT( world, FusionWorld );

     D_DEBUG_AT( Fusion_Ref, "fusion_ref_init( %p, '%s' )\n", ref, name ? : "" );

     if (user) {
          ref->multi.id   = (long) ref;
          ref->multi.user = true;

          direct_recursive_mutex_init( &ref->single.lock );
          direct_waitqueue_init( &ref->single.cond );

          ref->single.refs      = 0;
          ref->single.destroyed = false;
          ref->single.locked    = 0;
     }
     else {
          while (ioctl( world->fusion_fd, FUSION_REF_NEW, &ref->multi.id )) {
               if (errno == EINTR)
                    continue;

               D_PERROR( "FUSION_REF_NEW" );
               return DR_FUSION;
          }

          D_DEBUG_AT( Fusion_Ref, "  -> new ref %p [%d]\n", ref, ref->multi.id );

          info.type = FT_REF;
          info.id   = ref->multi.id;

          direct_snputs( info.name, name, sizeof(info.name) );

          ioctl( world->fusion_fd, FUSION_ENTRY_SET_INFO, &info );
     }

     /* Keep back pointer to shared world data. */
     ref->multi.shared  = world->shared;
     ref->multi.creator = fusion_id( world );

     return DR_OK;
}

DirectResult
fusion_ref_set_name (FusionRef  *ref,
                     const char *name)
{
     FusionEntryInfo info;

     D_ASSERT( ref != NULL );
     D_ASSERT( name != NULL );

     if (ref->multi.user)
          return DR_OK;

     info.type = FT_REF;
     info.id   = ref->multi.id;

     direct_snputs( info.name, name, sizeof(info.name) );

     while (ioctl (_fusion_fd( ref->multi.shared ), FUSION_ENTRY_SET_INFO, &info)) {
          switch (errno) {
               case EINTR:
                    continue;
               case EAGAIN:
                    return DR_LOCKED;
               case EINVAL:
                    D_ERROR ("Fusion/Reference: invalid reference\n");
                    return DR_DESTROYED;
               default:
                    break;
          }

          D_PERROR ("FUSION_ENTRY_SET_NAME");

          return DR_FAILURE;
     }

     return DR_OK;
}

DirectResult
fusion_ref_up (FusionRef *ref, bool global)
{
     DirectResult ret = DR_OK;

     D_ASSERT( ref != NULL );

     D_DEBUG_AT( Fusion_Ref, "fusion_ref_up( %p [%d]%s )\n", ref, ref->multi.id, global ? " GLOBAL" : "" );

     if (fusion_config->trace_ref == -1 || ref->multi.id == fusion_config->trace_ref) {
          D_INFO( "Fusion/Ref: 0x%08x up (%s), single refs %d\n", ref->multi.id, global ? "global" : "local", ref->single.refs );
          direct_trace_print_stack( NULL );
     }

     if (ref->multi.user) {
          FusionWorld *world = _fusion_world( ref->multi.shared );

          if (world->fusion_id == FUSION_ID_MASTER) {
               direct_mutex_lock (&ref->single.lock);

               if (ref->single.destroyed)
                    ret = DR_DESTROYED;
               else if (ref->single.locked)
                    ret = DR_LOCKED;
               else
                    ref->single.refs++;

               direct_mutex_unlock (&ref->single.lock);
          }
          else {
               FusionRefSlaveSlaveEntry *entry;

               direct_mutex_lock( &world->refs_lock );

               entry = direct_map_lookup( world->refs_map, &ref->multi.id );
               if (!entry) {
                    entry = D_CALLOC( 1, sizeof(FusionRefSlaveSlaveEntry) );
                    if (!entry) {
                         direct_mutex_unlock( &world->refs_lock );
                         return D_OOM();
                    }

                    entry->ref_id = ref->multi.id;

                    direct_map_insert( world->refs_map, &ref->multi.id, entry );
               }

               entry->refs_local++;

               direct_mutex_unlock( &world->refs_lock );
          }
     }
     else {
          while (ioctl (_fusion_fd( ref->multi.shared ), global ?
                        FUSION_REF_UP_GLOBAL : FUSION_REF_UP, &ref->multi.id))
          {
               switch (errno) {
                    case EINTR:
                         continue;
                    case EAGAIN:
                         return DR_LOCKED;
                    case EINVAL:
                         D_ERROR ("Fusion/Reference: invalid reference\n");
                         return DR_DESTROYED;
                    default:
                         break;
               }

               if (global)
                    D_PERROR ("FUSION_REF_UP_GLOBAL");
               else
                    D_PERROR ("FUSION_REF_UP");

               return DR_FAILURE;
          }

          D_DEBUG_AT( Fusion_Ref, "  -> %d references now\n",
                      ioctl( _fusion_fd( ref->multi.shared ), FUSION_REF_STAT, &ref->multi.id ) );
     }

     return ret;
}

DirectResult
fusion_ref_down (FusionRef *ref, bool global)
{
     D_ASSERT( ref != NULL );

     D_DEBUG_AT( Fusion_Ref, "fusion_ref_down( %p [%d]%s )\n", ref, ref->multi.id, global ? " GLOBAL" : "" );

     if (fusion_config->trace_ref == -1 || ref->multi.id == fusion_config->trace_ref) {
          D_INFO( "Fusion/Ref: 0x%08x down (%s), single refs %d\n", ref->multi.id, global ? "global" : "local", ref->single.refs );
          direct_trace_print_stack( NULL );
     }

     if (ref->multi.user) {
          FusionWorld *world = _fusion_world( ref->multi.shared );

          if (world->fusion_id == FUSION_ID_MASTER) {
               direct_mutex_lock (&ref->single.lock);

               if (!ref->single.refs) {
                    D_BUG( "no more references" );
                    direct_mutex_unlock (&ref->single.lock);
                    return DR_BUG;
               }

               if (ref->single.destroyed) {
                    direct_mutex_unlock (&ref->single.lock);
                    return DR_DESTROYED;
               }

               if (! --ref->single.refs) {
                    ref->single.dead++;

                    if (fusion_config->trace_ref == -1 || ref->multi.id == fusion_config->trace_ref) {
                         D_INFO( "Fusion/Ref: 0x%08x down (%s), single refs got %d! -> call %p\n", ref->multi.id, global ? "global" : "local", ref->single.refs,
                                 ref->single.call );
                    }

                    if (ref->single.call) {
                         FusionCall *call = ref->single.call;

                         if (fusion_config->trace_ref == -1 || ref->multi.id == fusion_config->trace_ref) {
                              D_INFO( "Fusion/Ref: 0x%08x down (%s), call_id 0x%08x, handler %p/%p\n", ref->multi.id, global ? "global" : "local",
                                      call->call_id, call->handler, call->handler3 );
                         }

                         if (call->handler) {
                              FusionCall copy_call = *call;
                              int        copy_arg  = ref->single.call_arg;

                              direct_mutex_unlock( &ref->single.lock );

                              fusion_call_execute( &copy_call, FCEF_NODIRECT | FCEF_ONEWAY, copy_arg, NULL, NULL );

                              return DR_OK;
                         }
                         else if (call->handler3) {
                              fusion_call_execute3( call, FCEF_NODIRECT | FCEF_ONEWAY | FCEF_QUEUE,
                                                    ref->single.call_arg, NULL, 0, NULL, 0, NULL );

                              direct_mutex_unlock( &ref->single.lock );

                              fusion_world_flush_calls( world, 1 );

                              return DR_OK;
                         }
                    }
                    else
                         direct_waitqueue_broadcast (&ref->single.cond);
               }

               direct_mutex_unlock (&ref->single.lock);
          }
          else {
               FusionRefSlaveSlaveEntry *entry;

               direct_mutex_lock( &world->refs_lock );

               entry = direct_map_lookup( world->refs_map, &ref->multi.id );
               if (!entry) {
                    direct_mutex_unlock( &world->refs_lock );
                    D_WARN( "ref (%d) not found", ref->multi.id );
                    return DR_ITEMNOTFOUND;
               }

               if (!--entry->refs_local) {
                    int i;

                    for (i=0; i<entry->refs_catch; i++)
                         fusion_call_execute( &ref->multi.shared->refs_call, FCEF_ONEWAY, ref->multi.id, NULL, NULL );

                    entry->refs_catch = 0;
               }

               direct_mutex_unlock( &world->refs_lock );
          }
     }
     else {
          fusion_world_flush_calls( _fusion_world( ref->multi.shared ), 1 );

          while (ioctl (_fusion_fd( ref->multi.shared ), global ?
                        FUSION_REF_DOWN_GLOBAL : FUSION_REF_DOWN, &ref->multi.id))
          {
               switch (errno) {
                    case EINTR:
                         continue;
                    case EINVAL:
                         D_ERROR ("Fusion/Reference: invalid reference\n");
                         return DR_DESTROYED;
                    default:
                         break;
               }

               D_PERROR ("FUSION_REF_DOWN%s (ref id 0x%08x, creator %lu)",
                         global ? "_GLOBAL" : "", ref->multi.id, ref->multi.creator );

               return DR_FAILURE;
          }

          // FIMXE: the following had to be commented out as the ref down may cause a ref watcher to free the memory of 'ref' (via ioctl)
#if 0
          if (ref->multi.shared)
               D_DEBUG_AT( Fusion_Ref, "  -> %d references now\n",
                           ioctl( _fusion_fd( ref->multi.shared ), FUSION_REF_STAT, &ref->multi.id ) );
          else
               D_DEBUG_AT( Fusion_Ref, "  -> destroyed\n" );
#endif
     }

     return DR_OK;
}

DirectResult
fusion_ref_catch (FusionRef *ref)
{
     D_ASSERT( ref != NULL );

     D_DEBUG_AT( Fusion_Ref, "fusion_ref_catch( %p [%d] )\n", ref, ref->multi.id );

     if (fusion_config->trace_ref == -1 || ref->multi.id == fusion_config->trace_ref) {
          D_INFO( "Fusion/Ref: 0x%08x catch, single refs %d\n", ref->multi.id, ref->single.refs );
          direct_trace_print_stack( NULL );
     }

     if (ref->multi.user) {
          FusionWorld *world = _fusion_world( ref->multi.shared );

          if (world->fusion_id == FUSION_ID_MASTER) {
               /*
                * If catcher is master, then we are most likely running in always-indirect mode!
                */
               direct_mutex_lock (&ref->single.lock);

               if (ref->single.refs < 2) {
                    D_BUG( "master->master catch with less than two refs" );
                    direct_mutex_unlock (&ref->single.lock);
                    return DR_BUG;
               }

               ref->single.refs--;

               direct_mutex_unlock (&ref->single.lock);
          }
          else {
               FusionRefSlaveSlaveEntry *entry;

               direct_mutex_lock( &world->refs_lock );

               entry = direct_map_lookup( world->refs_map, &ref->multi.id );
               if (!entry) {
                    entry = D_CALLOC( 1, sizeof(FusionRefSlaveSlaveEntry) );
                    if (!entry) {
                         direct_mutex_unlock( &world->refs_lock );
                         return D_OOM();
                    }

                    entry->ref_id = ref->multi.id;

                    direct_map_insert( world->refs_map, &ref->multi.id, entry );
               }

               entry->refs_catch++;

               direct_mutex_unlock( &world->refs_lock );
          }
     }
     else {
          while (ioctl (_fusion_fd( ref->multi.shared ), FUSION_REF_CATCH, &ref->multi.id)) {
               switch (errno) {
                    case EINTR:
                         continue;
                    case EINVAL:
                         D_ERROR ("Fusion/Reference: invalid reference\n");
                         return DR_DESTROYED;
                    default:
                         break;
               }

               D_PERROR ("FUSION_REF_CATCH");

               return DR_FAILURE;
          }
     }

     return DR_OK;
}

DirectResult
fusion_ref_throw (FusionRef *ref, FusionID catcher)
{
     D_ASSERT( ref != NULL );

     D_DEBUG_AT( Fusion_Ref, "fusion_ref_throw( %p [%d] )\n", ref, ref->multi.id );

     if (fusion_config->trace_ref == -1 || ref->multi.id == fusion_config->trace_ref) {
          D_INFO( "Fusion/Ref: 0x%08x throw, single refs %d\n", ref->multi.id, ref->single.refs );
          direct_trace_print_stack( NULL );
     }

     if (ref->multi.user) {
          FusionWorld *world = _fusion_world( ref->multi.shared );

          if (world->fusion_id == FUSION_ID_MASTER) {
               /*
                * If catcher is master, then we are most likely running in always-indirect mode!
                */
               if (catcher != FUSION_ID_MASTER) {
                    FusionRefSlaveKey    key;
                    FusionRefSlaveEntry *slave;

                    key.fusion_id = catcher;
                    key.ref_id    = ref->multi.id;

                    direct_mutex_lock( &world->refs_lock );

                    slave = direct_map_lookup( world->refs_map, &key );
                    if (!slave) {
                         slave = D_CALLOC( 1, sizeof(FusionRefSlaveEntry) );
                         if (!slave) {
                              direct_mutex_unlock( &world->refs_lock );
                              return D_OOM();
                         }

                         slave->key = key;
                         slave->ref = ref;

                         direct_map_insert( world->refs_map, &key, slave );
                    }

                    slave->refs++;

                    direct_mutex_unlock( &world->refs_lock );
               }
          }
          else {
               D_UNIMPLEMENTED();
          }
     }
     else {
          FusionRefThrow throw_;

          throw_.id      = ref->multi.id;
          throw_.catcher = catcher;

          while (ioctl (_fusion_fd( ref->multi.shared ), FUSION_REF_THROW, &throw_)) {
               switch (errno) {
                    case EINTR:
                         continue;
                    case EINVAL:
                         D_ERROR ("Fusion/Reference: invalid reference\n");
                         return DR_DESTROYED;
                    default:
                         break;
               }

               D_PERROR ("FUSION_REF_THROW");

               return DR_FAILURE;
          }
     }

     return DR_OK;
}

DirectResult
fusion_ref_stat (FusionRef *ref, int *refs)
{
     int val;

     D_ASSERT( ref != NULL );
     D_ASSERT( refs != NULL );

     if (ref->multi.user) {
          if (ref->single.destroyed)
               return DR_DESTROYED;

          val = ref->single.refs;
     }
     else {
          while ((val = ioctl (_fusion_fd( ref->multi.shared ), FUSION_REF_STAT, &ref->multi.id)) < 0) {
               switch (errno) {
                    case EINTR:
                         continue;
                    case EINVAL:
                         D_ERROR ("Fusion/Reference: invalid reference\n");
                         return DR_DESTROYED;
                    default:
                         break;
               }

               D_PERROR ("FUSION_REF_STAT");

               return DR_FAILURE;
          }
     }

     *refs = val;

     return DR_OK;
}

DirectResult
fusion_ref_zero_lock (FusionRef *ref)
{
     DirectResult ret = DR_OK;

     D_ASSERT( ref != NULL );

     if (ref->multi.user) {
          FusionWorld *world = _fusion_world( ref->multi.shared );

          if (world->fusion_id == FUSION_ID_MASTER) {
               direct_mutex_lock (&ref->single.lock);

               do {
                    if (ref->single.destroyed)
                         ret = DR_DESTROYED;
                    else if (ref->single.locked)
                         ret = DR_LOCKED;
                    else if (ref->single.refs)
                         direct_waitqueue_wait (&ref->single.cond, &ref->single.lock);
                    else {
                         ref->single.locked = direct_gettid();
                         break;
                    }
               } while (ret == DR_OK);

               direct_mutex_unlock (&ref->single.lock);
          }
          else {
               D_UNIMPLEMENTED();

               return DR_UNIMPLEMENTED;
          }
     }
     else {
          while (ioctl (_fusion_fd( ref->multi.shared ), FUSION_REF_ZERO_LOCK, &ref->multi.id)) {
               switch (errno) {
                    case EINTR:
                         continue;
                    case EINVAL:
                         D_ERROR ("Fusion/Reference: invalid reference\n");
                         return DR_DESTROYED;
                    default:
                         break;
               }

               D_PERROR ("FUSION_REF_ZERO_LOCK");

               return DR_FAILURE;
          }
     }

     return ret;
}

DirectResult
fusion_ref_zero_trylock (FusionRef *ref)
{
     DirectResult ret = DR_OK;

     D_ASSERT( ref != NULL );

     if (ref->multi.user) {
          FusionWorld *world = _fusion_world( ref->multi.shared );

          if (world->fusion_id == FUSION_ID_MASTER) {
               direct_mutex_lock (&ref->single.lock);

               if (ref->single.destroyed)
                    ret = DR_DESTROYED;
               else if (ref->single.locked)
                    ret = DR_LOCKED;
               else if (ref->single.refs)
                    ret = DR_BUSY;
               else
                    ref->single.locked = direct_gettid();

               direct_mutex_unlock (&ref->single.lock);
          }
          else {
               D_UNIMPLEMENTED();

               return DR_UNIMPLEMENTED;
          }
     }
     else {
          while (ioctl (_fusion_fd( ref->multi.shared ), FUSION_REF_ZERO_TRYLOCK, &ref->multi.id)) {
               switch (errno) {
                    case EINTR:
                         continue;
                    case ETOOMANYREFS:
                         return DR_BUSY;
                    case EINVAL:
                         D_ERROR ("Fusion/Reference: invalid reference\n");
                         return DR_DESTROYED;
                    default:
                         break;
               }

               D_PERROR ("FUSION_REF_ZERO_TRYLOCK");

               return DR_FAILURE;
          }
     }

     return ret;
}

DirectResult
fusion_ref_unlock (FusionRef *ref)
{
     DirectResult ret = DR_OK;

     D_ASSERT( ref != NULL );

     if (ref->multi.user) {
          FusionWorld *world = _fusion_world( ref->multi.shared );

          if (world->fusion_id == FUSION_ID_MASTER) {
               direct_mutex_lock (&ref->single.lock);

               if (ref->single.locked == direct_gettid()) {
                    ref->single.locked = 0;

                    direct_waitqueue_broadcast (&ref->single.cond);
               }
               else
                    ret = DR_ACCESSDENIED;

               direct_mutex_unlock (&ref->single.lock);
          }
          else {
               D_UNIMPLEMENTED();

               return DR_UNIMPLEMENTED;
          }
     }
     else {
          while (ioctl (_fusion_fd( ref->multi.shared ), FUSION_REF_UNLOCK, &ref->multi.id)) {
               switch (errno) {
                    case EINTR:
                         continue;
                    case EINVAL:
                         D_ERROR ("Fusion/Reference: invalid reference\n");
                         return DR_DESTROYED;
                    default:
                         break;
               }

               D_PERROR ("FUSION_REF_UNLOCK");

               return DR_FAILURE;
          }
     }

     return ret;
}

DirectResult
fusion_ref_watch (FusionRef *ref, FusionCall *call, int call_arg)
{
     DirectResult ret = DR_OK;

     D_ASSERT( ref != NULL );
     D_ASSERT( call != NULL );

     if (ref->multi.user) {
          FusionWorld *world = _fusion_world( ref->multi.shared );

          if (world->fusion_id == FUSION_ID_MASTER) {
               direct_mutex_lock (&ref->single.lock);

               if (ref->single.destroyed)
                    ret = DR_DESTROYED;
               else if (!ref->single.refs)
                    ret = DR_BUG;
               else if (ref->single.call)
                    ret = DR_BUSY;
               else {
                    ref->single.call     = call;
                    ref->single.call_arg = call_arg;
               }

               direct_mutex_unlock (&ref->single.lock);
          }
          else {
               D_UNIMPLEMENTED();

               return DR_UNIMPLEMENTED;
          }
     }
     else {
          FusionRefWatch watch;

          watch.id       = ref->multi.id;
          watch.call_id  = call->call_id;
          watch.call_arg = call_arg;

          while (ioctl (_fusion_fd( ref->multi.shared ), FUSION_REF_WATCH, &watch)) {
               switch (errno) {
                    case EINTR:
                         continue;
                    case EINVAL:
                         D_ERROR ("Fusion/Reference: invalid reference\n");
                         return DR_DESTROYED;
                    default:
                         break;
               }

               D_PERROR ("FUSION_REF_WATCH");

               return DR_FAILURE;
          }
     }

     return ret;
}

DirectResult
fusion_ref_inherit (FusionRef *ref, FusionRef *from)
{
     D_ASSERT( ref != NULL );
     D_ASSERT( from != NULL );

     if (ref->multi.user) {
          D_UNIMPLEMENTED();

          return DR_UNIMPLEMENTED;
     }
     else {
          FusionRefInherit inherit;

          inherit.id   = ref->multi.id;
          inherit.from = from->multi.id;

          while (ioctl (_fusion_fd( ref->multi.shared ), FUSION_REF_INHERIT, &inherit)) {
               switch (errno) {
                    case EINTR:
                         continue;
                    case EINVAL:
                         D_ERROR ("Fusion/Reference: invalid reference\n");
                         return DR_DESTROYED;
                    default:
                         break;
               }

               D_PERROR ("FUSION_REF_INHERIT");

               return DR_FAILURE;
          }
     }

     return DR_OK;
}

DirectResult
fusion_ref_set_sync (FusionRef *ref)
{
     D_ASSERT( ref != NULL );

     if (ref->multi.user) {
          D_UNIMPLEMENTED();
     }
     else {
          while (ioctl (_fusion_fd( ref->multi.shared ), FUSION_REF_SET_SYNC, &ref->multi.id)) {
               switch (errno) {
                    case EINTR:
                         continue;
                    case EINVAL:
                         D_ERROR ("Fusion/Reference: invalid reference\n");
                         return DR_DESTROYED;
                    default:
                         break;
               }

               D_PERROR ("FUSION_REF_SET_SYNC");

               return DR_FAILURE;
          }
     }

     return DR_OK;
}

DirectResult
fusion_ref_destroy (FusionRef *ref)
{
     D_ASSERT( ref != NULL );

     D_DEBUG_AT( Fusion_Ref, "fusion_ref_destroy( %p [%d] )\n", ref, ref->multi.id );

     if (ref->multi.user) {
          FusionWorld *world = _fusion_world( ref->multi.shared );

          if (world->fusion_id == FUSION_ID_MASTER) {
               ref->single.destroyed = true;

               direct_waitqueue_broadcast (&ref->single.cond);

               direct_mutex_deinit( &ref->single.lock );
               direct_waitqueue_deinit( &ref->single.cond );
          }
          else {
               D_UNIMPLEMENTED();

               return DR_UNIMPLEMENTED;
          }
     }
     else {
          fusion_world_flush_calls( _fusion_world( ref->multi.shared ), 1 );

          while (ioctl (_fusion_fd( ref->multi.shared ), FUSION_REF_DESTROY, &ref->multi.id)) {
               switch (errno) {
                    case EINTR:
                         continue;
                    case EINVAL:
                         D_ERROR ("Fusion/Reference: invalid reference\n");
                         return DR_DESTROYED;
                    default:
                         break;
               }

               D_PERROR ("FUSION_REF_DESTROY");

               return DR_FAILURE;
          }
     }

     return DR_OK;
}

DirectResult
fusion_ref_add_permissions( FusionRef            *ref,
                            FusionID              fusion_id,
                            FusionRefPermissions  ref_permissions )
{
     if (ref->multi.user) {

     }
     else {
          FusionEntryPermissions permissions;

          permissions.type        = FT_REF;
          permissions.id          = ref->multi.id;
          permissions.fusion_id   = fusion_id;
          permissions.permissions = 0;

          if (ref_permissions & FUSION_REF_PERMIT_REF_UNREF_LOCAL) {
               FUSION_ENTRY_PERMISSIONS_ADD( permissions.permissions, FUSION_REF_UP );
               FUSION_ENTRY_PERMISSIONS_ADD( permissions.permissions, FUSION_REF_DOWN );
          }

          if (ref_permissions & FUSION_REF_PERMIT_REF_UNREF_GLOBAL) {
               FUSION_ENTRY_PERMISSIONS_ADD( permissions.permissions, FUSION_REF_UP_GLOBAL );
               FUSION_ENTRY_PERMISSIONS_ADD( permissions.permissions, FUSION_REF_DOWN_GLOBAL );
          }

          if (ref_permissions & FUSION_REF_PERMIT_ZERO_LOCK_UNLOCK) {
               FUSION_ENTRY_PERMISSIONS_ADD( permissions.permissions, FUSION_REF_ZERO_LOCK );
               FUSION_ENTRY_PERMISSIONS_ADD( permissions.permissions, FUSION_REF_ZERO_TRYLOCK );
               FUSION_ENTRY_PERMISSIONS_ADD( permissions.permissions, FUSION_REF_UNLOCK );
          }

          if (ref_permissions & FUSION_REF_PERMIT_WATCH)
               FUSION_ENTRY_PERMISSIONS_ADD( permissions.permissions, FUSION_REF_WATCH );

          if (ref_permissions & FUSION_REF_PERMIT_INHERIT)
               FUSION_ENTRY_PERMISSIONS_ADD( permissions.permissions, FUSION_REF_INHERIT );

          if (ref_permissions & FUSION_REF_PERMIT_DESTROY)
               FUSION_ENTRY_PERMISSIONS_ADD( permissions.permissions, FUSION_REF_DESTROY );

          if (ref_permissions & FUSION_REF_PERMIT_CATCH)
               FUSION_ENTRY_PERMISSIONS_ADD( permissions.permissions, FUSION_REF_CATCH );

          if (ref_permissions & FUSION_REF_PERMIT_THROW)
               FUSION_ENTRY_PERMISSIONS_ADD( permissions.permissions, FUSION_REF_THROW );

          while (ioctl( _fusion_fd( ref->multi.shared ), FUSION_ENTRY_ADD_PERMISSIONS, &permissions ) < 0) {
               if (errno != EINTR) {
                    D_PERROR( "Fusion/Reactor: FUSION_ENTRY_ADD_PERMISSIONS( id %d ) failed!\n", ref->multi.id );
                    return DR_FAILURE;
               }
          }
     }

     return DR_OK;
}

#else /* FUSION_BUILD_KERNEL */

DirectResult
fusion_ref_init (FusionRef         *ref,
                 const char        *name,
                 const FusionWorld *world)
{
     return fusion_ref_init2( ref, name, false, world );
}

DirectResult
fusion_ref_init2(FusionRef         *ref,
                 const char        *name,
                 bool               user,
                 const FusionWorld *world)
{
     ref->multi.id      = ++world->shared->ref_ids;
     ref->multi.shared  = world->shared;
     ref->multi.creator = fusion_id( world );
     ref->multi.user    = user;

     if (user) {
          direct_recursive_mutex_init( &ref->single.lock );
          direct_waitqueue_init( &ref->single.cond );

          ref->single.refs      = 0;
          ref->single.destroyed = false;
          ref->single.locked    = 0;
     }
     else {
          ref->multi.builtin.local  = 0;
          ref->multi.builtin.global = 0;

          fusion_skirmish_init( &ref->multi.builtin.lock, name, world );

          ref->multi.builtin.call = NULL;
     }

     return DR_OK;
}

DirectResult
fusion_ref_set_name (FusionRef  *ref,
                     const char *name)
{
     return DR_OK;
}

DirectResult
_fusion_ref_change (FusionRef *ref, int add, bool global)
{
     DirectResult ret;

     D_ASSERT( ref != NULL );
     D_ASSERT( add != 0 );

     ret = fusion_skirmish_prevail( &ref->multi.builtin.lock );
     if (ret)
          return ret;

     if (global) {
          if (ref->multi.builtin.global+add < 0) {
               D_BUG( "ref has no global references" );
               fusion_skirmish_dismiss( &ref->multi.builtin.lock );
               return DR_BUG;
          }

          ref->multi.builtin.global += add;
     }
     else {
          if (ref->multi.builtin.local+add < 0) {
               D_BUG( "ref has no local references" );
               fusion_skirmish_dismiss( &ref->multi.builtin.lock );
               return DR_BUG;
          }

          ref->multi.builtin.local += add;

          _fusion_add_local( _fusion_world(ref->multi.shared), ref, add );
     }

     if (ref->multi.builtin.local+ref->multi.builtin.global == 0) {
          fusion_skirmish_notify( &ref->multi.builtin.lock );

          if (ref->multi.builtin.call) {
               fusion_skirmish_dismiss( &ref->multi.builtin.lock );
               return fusion_call_execute( ref->multi.builtin.call, FCEF_ONEWAY,
                                           ref->multi.builtin.call_arg, NULL, NULL );
          }
     }

     fusion_skirmish_dismiss( &ref->multi.builtin.lock );

     return DR_OK;
}

DirectResult
fusion_ref_up (FusionRef *ref, bool global)
{
     DirectResult ret = DR_OK;

     D_ASSERT( ref != NULL );

     D_DEBUG_AT( Fusion_Ref, "fusion_ref_up( %p [%d]%s )\n", ref, ref->multi.id, global ? " GLOBAL" : "" );

     if (fusion_config->trace_ref == -1 || ref->multi.id == fusion_config->trace_ref) {
          D_INFO( "Fusion/Ref: 0x%08x up (%s), single refs %d\n", ref->multi.id, global ? "global" : "local", ref->single.refs );
          direct_trace_print_stack( NULL );
     }

     if (ref->multi.user) {
          FusionWorld *world = _fusion_world( ref->multi.shared );

          if (world->fusion_id == FUSION_ID_MASTER) {
               direct_mutex_lock (&ref->single.lock);

               if (ref->single.destroyed)
                    ret = DR_DESTROYED;
               else if (ref->single.locked)
                    ret = DR_LOCKED;
               else
                    ref->single.refs++;

               direct_mutex_unlock (&ref->single.lock);
          }
          else {
               FusionRefSlaveSlaveEntry *entry;

               direct_mutex_lock( &world->refs_lock );

               entry = direct_map_lookup( world->refs_map, &ref->multi.id );
               if (!entry) {
                    entry = D_CALLOC( 1, sizeof(FusionRefSlaveSlaveEntry) );
                    if (!entry) {
                         direct_mutex_unlock( &world->refs_lock );
                         return D_OOM();
                    }

                    entry->ref_id = ref->multi.id;

                    direct_map_insert( world->refs_map, &ref->multi.id, entry );
               }

               entry->refs_local++;

               direct_mutex_unlock( &world->refs_lock );
          }
     }
     else
          return _fusion_ref_change( ref, +1, global );

     return ret;
}

DirectResult
fusion_ref_down (FusionRef *ref, bool global)
{
     D_ASSERT( ref != NULL );

     D_DEBUG_AT( Fusion_Ref, "fusion_ref_down( %p [%d]%s )\n", ref, ref->multi.id, global ? " GLOBAL" : "" );

     if (fusion_config->trace_ref == -1 || ref->multi.id == fusion_config->trace_ref) {
          D_INFO( "Fusion/Ref: 0x%08x down (%s), single refs %d\n", ref->multi.id, global ? "global" : "local", ref->single.refs );
          direct_trace_print_stack( NULL );
     }

     if (ref->multi.user) {
          FusionWorld *world = _fusion_world( ref->multi.shared );

          if (world->fusion_id == FUSION_ID_MASTER) {
               direct_mutex_lock (&ref->single.lock);

               if (!ref->single.refs) {
                    D_BUG( "no more references" );
                    direct_mutex_unlock (&ref->single.lock);
                    return DR_BUG;
               }

               if (ref->single.destroyed) {
                    direct_mutex_unlock (&ref->single.lock);
                    return DR_DESTROYED;
               }

               if (! --ref->single.refs) {
                    ref->single.dead++;

                    if (fusion_config->trace_ref == -1 || ref->multi.id == fusion_config->trace_ref) {
                         D_INFO( "Fusion/Ref: 0x%08x down (%s), single refs got %d! -> call %p\n", ref->multi.id, global ? "global" : "local", ref->single.refs,
                                 ref->single.call );
                    }

                    if (ref->single.call) {
                         FusionCall *call = ref->single.call;

                         if (fusion_config->trace_ref == -1 || ref->multi.id == fusion_config->trace_ref) {
                              D_INFO( "Fusion/Ref: 0x%08x down (%s), call_id 0x%08x, handler %p/%p\n", ref->multi.id, global ? "global" : "local",
                                      call->call_id, call->handler, call->handler3 );
                         }

                         if (call->handler) {
                              FusionCall copy_call = *call;
                              int        copy_arg  = ref->single.call_arg;

                              direct_mutex_unlock( &ref->single.lock );

                              fusion_call_execute( &copy_call, FCEF_NODIRECT | FCEF_ONEWAY, copy_arg, NULL, NULL );

                              return DR_OK;
                         }
                         else if (call->handler3) {
                              fusion_call_execute3( call, FCEF_NODIRECT | FCEF_ONEWAY | FCEF_QUEUE,
                                                    ref->single.call_arg, NULL, 0, NULL, 0, NULL );

                              direct_mutex_unlock( &ref->single.lock );

                              fusion_world_flush_calls( world, 1 );

                              return DR_OK;
                         }
                    }
                    else
                         direct_waitqueue_broadcast (&ref->single.cond);
               }

               direct_mutex_unlock (&ref->single.lock);
          }
          else {
               FusionRefSlaveSlaveEntry *entry;

               direct_mutex_lock( &world->refs_lock );

               entry = direct_map_lookup( world->refs_map, &ref->multi.id );
               if (!entry) {
                    direct_mutex_unlock( &world->refs_lock );
                    D_WARN( "ref (%d) not found", ref->multi.id );
                    return DR_ITEMNOTFOUND;
               }

               if (!--entry->refs_local) {
                    int i;

                    for (i=0; i<entry->refs_catch; i++)
                         fusion_call_execute( &ref->multi.shared->refs_call, FCEF_ONEWAY, ref->multi.id, NULL, NULL );

                    entry->refs_catch = 0;
               }

               direct_mutex_unlock( &world->refs_lock );
          }
     }
     else
          return _fusion_ref_change( ref, -1, global );

     return DR_OK;
}

DirectResult
fusion_ref_stat (FusionRef *ref, int *refs)
{
     int val;

     D_ASSERT( ref != NULL );
     D_ASSERT( refs != NULL );

     if (ref->multi.user) {
          if (ref->single.destroyed)
               return DR_DESTROYED;

          val = ref->single.refs;
     }
     else
          val = ref->multi.builtin.local + ref->multi.builtin.global;

     *refs = val;

     return DR_OK;
}

DirectResult
fusion_ref_catch (FusionRef *ref)
{
     D_ASSERT( ref != NULL );

     D_DEBUG_AT( Fusion_Ref, "fusion_ref_catch( %p [%d] )\n", ref, ref->multi.id );

     if (fusion_config->trace_ref == -1 || ref->multi.id == fusion_config->trace_ref) {
          D_INFO( "Fusion/Ref: 0x%08x catch, single refs %d\n", ref->multi.id, ref->single.refs );
          direct_trace_print_stack( NULL );
     }

     if (ref->multi.user) {
          FusionWorld *world = _fusion_world( ref->multi.shared );

          if (world->fusion_id == FUSION_ID_MASTER) {
               /*
                * If catcher is master, then we are most likely running in always-indirect mode!
                */
               direct_mutex_lock (&ref->single.lock);

               if (ref->single.refs < 2) {
                    D_BUG( "master->master catch with less than two refs" );
                    direct_mutex_unlock (&ref->single.lock);
                    return DR_BUG;
               }

               ref->single.refs--;

               direct_mutex_unlock (&ref->single.lock);
          }
          else {
               FusionRefSlaveSlaveEntry *entry;

               direct_mutex_lock( &world->refs_lock );

               entry = direct_map_lookup( world->refs_map, &ref->multi.id );
               if (!entry) {
                    entry = D_CALLOC( 1, sizeof(FusionRefSlaveSlaveEntry) );
                    if (!entry) {
                         direct_mutex_unlock( &world->refs_lock );
                         return D_OOM();
                    }

                    entry->ref_id = ref->multi.id;

                    direct_map_insert( world->refs_map, &ref->multi.id, entry );
               }

               entry->refs_catch++;

               direct_mutex_unlock( &world->refs_lock );
          }
     }
     else
          return fusion_ref_down( ref, false );

     return DR_OK;
}

DirectResult
fusion_ref_throw (FusionRef *ref, FusionID catcher)
{
     D_ASSERT( ref != NULL );

     D_DEBUG_AT( Fusion_Ref, "fusion_ref_throw( %p [%d] )\n", ref, ref->multi.id );

     if (fusion_config->trace_ref == -1 || ref->multi.id == fusion_config->trace_ref) {
          D_INFO( "Fusion/Ref: 0x%08x throw, single refs %d\n", ref->multi.id, ref->single.refs );
          direct_trace_print_stack( NULL );
     }

     if (ref->multi.user) {
          FusionWorld *world = _fusion_world( ref->multi.shared );

          if (world->fusion_id == FUSION_ID_MASTER) {
               /*
                * If catcher is master, then we are most likely running in always-indirect mode!
                */
               if (catcher != FUSION_ID_MASTER) {
                    FusionRefSlaveKey    key;
                    FusionRefSlaveEntry *slave;

                    key.fusion_id = catcher;
                    key.ref_id    = ref->multi.id;

                    direct_mutex_lock( &world->refs_lock );

                    slave = direct_map_lookup( world->refs_map, &key );
                    if (!slave) {
                         slave = D_CALLOC( 1, sizeof(FusionRefSlaveEntry) );
                         if (!slave) {
                              direct_mutex_unlock( &world->refs_lock );
                              return D_OOM();
                         }

                         slave->key = key;
                         slave->ref = ref;

                         direct_map_insert( world->refs_map, &key, slave );
                    }

                    slave->refs++;

                    direct_mutex_unlock( &world->refs_lock );
               }
          }
          else {
               D_UNIMPLEMENTED();
          }
     }

     return DR_OK;
}

DirectResult
fusion_ref_zero_lock (FusionRef *ref)
{
     DirectResult ret = DR_OK;

     D_ASSERT( ref != NULL );

     if (ref->multi.user) {
          FusionWorld *world = _fusion_world( ref->multi.shared );

          if (world->fusion_id == FUSION_ID_MASTER) {
               direct_mutex_lock (&ref->single.lock);

               do {
                    if (ref->single.destroyed)
                         ret = DR_DESTROYED;
                    else if (ref->single.locked)
                         ret = DR_LOCKED;
                    else if (ref->single.refs)
                         direct_waitqueue_wait (&ref->single.cond, &ref->single.lock);
                    else {
                         ref->single.locked = direct_gettid();
                         break;
                    }
               } while (ret == DR_OK);

               direct_mutex_unlock (&ref->single.lock);
          }
          else {
               D_UNIMPLEMENTED();

               return DR_UNIMPLEMENTED;
          }
     }
     else {
          ret = fusion_skirmish_prevail( &ref->multi.builtin.lock );
          if (ret)
               return ret;

          if (ref->multi.builtin.call) {
               ret = DR_ACCESSDENIED;
          }
          else {
               if (ref->multi.builtin.local)
                    _fusion_check_locals( _fusion_world(ref->multi.shared), ref );

               while (ref->multi.builtin.local+ref->multi.builtin.global) {
                    ret = fusion_skirmish_wait( &ref->multi.builtin.lock, 1000 ); /* 1 second */
                    if (ret && ret != DR_TIMEOUT);
                         return ret;

                    if (ref->multi.builtin.call) {
                         ret = DR_ACCESSDENIED;
                         break;
                    }

                    if (ref->multi.builtin.local)
                         _fusion_check_locals( _fusion_world(ref->multi.shared), ref );
               }
          }

          if (ret)
               fusion_skirmish_dismiss( &ref->multi.builtin.lock );
     }

     return ret;
}

DirectResult
fusion_ref_zero_trylock (FusionRef *ref)
{
     DirectResult ret = DR_OK;

     D_ASSERT( ref != NULL );

     if (ref->multi.user) {
          FusionWorld *world = _fusion_world( ref->multi.shared );

          if (world->fusion_id == FUSION_ID_MASTER) {
               direct_mutex_lock (&ref->single.lock);

               if (ref->single.destroyed)
                    ret = DR_DESTROYED;
               else if (ref->single.locked)
                    ret = DR_LOCKED;
               else if (ref->single.refs)
                    ret = DR_BUSY;
               else
                    ref->single.locked = direct_gettid();

               direct_mutex_unlock (&ref->single.lock);
          }
          else {
               D_UNIMPLEMENTED();

               return DR_UNIMPLEMENTED;
          }
     }
     else {
          ret = fusion_skirmish_prevail( &ref->multi.builtin.lock );
          if (ret)
               return ret;

          if (ref->multi.builtin.local)
               _fusion_check_locals( _fusion_world(ref->multi.shared), ref );

          if (ref->multi.builtin.local+ref->multi.builtin.global)
               ret = DR_BUSY;

          if (ret)
               fusion_skirmish_dismiss( &ref->multi.builtin.lock );
     }

     return ret;
}

DirectResult
fusion_ref_unlock (FusionRef *ref)
{
     DirectResult ret = DR_OK;

     D_ASSERT( ref != NULL );

     if (ref->multi.user) {
          FusionWorld *world = _fusion_world( ref->multi.shared );

          if (world->fusion_id == FUSION_ID_MASTER) {
               direct_mutex_lock (&ref->single.lock);

               if (ref->single.locked == direct_gettid()) {
                    ref->single.locked = 0;

                    direct_waitqueue_broadcast (&ref->single.cond);
               }
               else
                    ret = DR_ACCESSDENIED;

               direct_mutex_unlock (&ref->single.lock);
          }
          else {
               D_UNIMPLEMENTED();

               return DR_UNIMPLEMENTED;
          }
     }
     else
          fusion_skirmish_dismiss( &ref->multi.builtin.lock );

     return ret;
}

DirectResult
fusion_ref_watch (FusionRef *ref, FusionCall *call, int call_arg)
{
     DirectResult ret = DR_OK;

     D_ASSERT( ref != NULL );
     D_ASSERT( call != NULL );

     if (ref->multi.user) {
          FusionWorld *world = _fusion_world( ref->multi.shared );

          if (world->fusion_id == FUSION_ID_MASTER) {
               direct_mutex_lock (&ref->single.lock);

               if (ref->single.destroyed)
                    ret = DR_DESTROYED;
               else if (!ref->single.refs)
                    ret = DR_BUG;
               else if (ref->single.call)
                    ret = DR_BUSY;
               else {
                    ref->single.call     = call;
                    ref->single.call_arg = call_arg;
               }

               direct_mutex_unlock (&ref->single.lock);
          }
          else {
               D_UNIMPLEMENTED();

               return DR_UNIMPLEMENTED;
          }
     }
     else {
          ret = fusion_skirmish_prevail( &ref->multi.builtin.lock );
          if (ret)
               return ret;

          if (ref->multi.builtin.local+ref->multi.builtin.global == 0) {
               D_BUG( "ref has no references" );
               ret = DR_BUG;
          }
          else if (ref->multi.builtin.call) {
               ret = DR_BUSY;
          }
          else {
               ref->multi.builtin.call = call;
               ref->multi.builtin.call_arg = call_arg;
               fusion_skirmish_notify( &ref->multi.builtin.lock );
          }

          fusion_skirmish_dismiss( &ref->multi.builtin.lock );
     }

     return ret;
}

DirectResult
fusion_ref_inherit (FusionRef *ref, FusionRef *from)
{
     D_ASSERT( ref != NULL );
     D_ASSERT( from != NULL );

     D_UNIMPLEMENTED();

     return fusion_ref_up( ref, true );
}

DirectResult
fusion_ref_destroy (FusionRef *ref)
{
     FusionSkirmish *skirmish;

     D_ASSERT( ref != NULL );

     D_DEBUG_AT( Fusion_Ref, "fusion_ref_destroy( %p )\n", ref );

     skirmish = &ref->multi.builtin.lock;
     if (skirmish->multi.builtin.destroyed)
          return DR_DESTROYED;

     _fusion_remove_all_locals( _fusion_world(ref->multi.shared), ref );

     fusion_skirmish_destroy( skirmish );

     return DR_OK;
}

DirectResult
fusion_ref_add_permissions( FusionRef            *ref,
                            FusionID              fusion_id,
                            FusionRefPermissions  ref_permissions )
{
     D_UNIMPLEMENTED();

     return DR_UNIMPLEMENTED;
}

#endif /* FUSION_BUILD_KERNEL */

#else /* FUSION_BUILD_MULTI */

DirectResult
fusion_ref_init (FusionRef         *ref,
                 const char        *name,
                 const FusionWorld *world)
{
     D_ASSERT( ref != NULL );
     D_ASSERT( name != NULL );

     D_DEBUG_AT( Fusion_Ref, "%s( %p )\n", __FUNCTION__, ref );

     direct_recursive_mutex_init( &ref->single.lock );
     direct_waitqueue_init( &ref->single.cond );

     ref->single.refs      = 0;
     ref->single.destroyed = false;
     ref->single.locked    = 0;

     ref->multi.id         = (long) ref;

     return DR_OK;
}

DirectResult
fusion_ref_init2(FusionRef         *ref,
                 const char        *name,
                 bool               user,
                 const FusionWorld *world)
{
     return fusion_ref_init( ref, name, world );
}

DirectResult
fusion_ref_set_name (FusionRef  *ref,
                     const char *name)
{
     D_DEBUG_AT( Fusion_Ref, "%s( %p, '%s' )\n", __FUNCTION__, ref, name );

     return DR_OK;
}

DirectResult
fusion_ref_up (FusionRef *ref, bool global)
{
     DirectResult ret = DR_OK;

     D_ASSERT( ref != NULL );

     D_DEBUG_AT( Fusion_Ref, "%s( %p, %d )\n", __FUNCTION__, ref, global );

     if (fusion_config->trace_ref == -1 || ref->multi.id == fusion_config->trace_ref) {
          D_INFO( "Fusion/Ref: 0x%08x up (%s), single refs %d\n", ref->multi.id, global ? "global" : "local", ref->single.refs );
          direct_trace_print_stack( NULL );
     }

     direct_mutex_lock (&ref->single.lock);

     if (ref->single.destroyed)
          ret = DR_DESTROYED;
     else if (ref->single.locked)
          ret = DR_LOCKED;
     else
          ref->single.refs++;

     direct_mutex_unlock (&ref->single.lock);

     return ret;
}

DirectResult
fusion_ref_down (FusionRef *ref, bool global)
{
     D_ASSERT( ref != NULL );

     D_DEBUG_AT( Fusion_Ref, "%s( %p, %d )\n", __FUNCTION__, ref, global );

     if (fusion_config->trace_ref == -1 || ref->multi.id == fusion_config->trace_ref) {
          D_INFO( "Fusion/Ref: 0x%08x down (%s), single refs %d\n", ref->multi.id, global ? "global" : "local", ref->single.refs );
          direct_trace_print_stack( NULL );
     }

     direct_mutex_lock (&ref->single.lock);

     if (!ref->single.refs) {
          D_BUG( "no more references" );
          direct_mutex_unlock (&ref->single.lock);
          return DR_BUG;
     }

     if (ref->single.destroyed) {
          direct_mutex_unlock (&ref->single.lock);
          return DR_DESTROYED;
     }

     if (! --ref->single.refs) {
          if (ref->single.call) {
               FusionCall *call = ref->single.call;

               if (call->handler) {
                    FusionCall copy_call = *call;
                    int        copy_arg  = ref->single.call_arg;

                    direct_mutex_unlock( &ref->single.lock );

                    fusion_call_execute( &copy_call, FCEF_NODIRECT | FCEF_ONEWAY, copy_arg, NULL, NULL );

                    return DR_OK;
               }
          }
          else
               direct_waitqueue_broadcast (&ref->single.cond);
     }

     direct_mutex_unlock (&ref->single.lock);

     return DR_OK;
}

DirectResult
fusion_ref_catch (FusionRef *ref)
{
     D_DEBUG_AT( Fusion_Ref, "%s( %p )\n", __FUNCTION__, ref );

     if (fusion_config->trace_ref == -1 || ref->multi.id == fusion_config->trace_ref) {
          D_INFO( "Fusion/Ref: 0x%08x catch, single refs %d\n", ref->multi.id, ref->single.refs );
          direct_trace_print_stack( NULL );
     }

     return fusion_ref_down( ref, false );
}

DirectResult
fusion_ref_throw (FusionRef *ref, FusionID catcher)
{
     D_DEBUG_AT( Fusion_Ref, "%s( %p )\n", __FUNCTION__, ref );

     if (fusion_config->trace_ref == -1 || ref->multi.id == fusion_config->trace_ref) {
          D_INFO( "Fusion/Ref: 0x%08x throw, single refs %d\n", ref->multi.id, ref->single.refs );
          direct_trace_print_stack( NULL );
     }

     return DR_OK;
}

DirectResult
fusion_ref_stat (FusionRef *ref, int *refs)
{
     D_ASSERT( ref != NULL );
     D_ASSERT( refs != NULL );

     if (ref->single.destroyed)
          return DR_DESTROYED;

     *refs = ref->single.refs;

     return DR_OK;
}

DirectResult
fusion_ref_zero_lock (FusionRef *ref)
{
     DirectResult ret = DR_OK;

     D_ASSERT( ref != NULL );

     D_DEBUG_AT( Fusion_Ref, "%s( %p )\n", __FUNCTION__, ref );

     direct_mutex_lock (&ref->single.lock);

     do {
          if (ref->single.destroyed)
               ret = DR_DESTROYED;
          else if (ref->single.locked)
               ret = DR_LOCKED;
          else if (ref->single.refs)
               direct_waitqueue_wait (&ref->single.cond, &ref->single.lock);
          else {
               ref->single.locked = direct_gettid();
               break;
          }
     } while (ret == DR_OK);

     direct_mutex_unlock (&ref->single.lock);

     return ret;
}

DirectResult
fusion_ref_zero_trylock (FusionRef *ref)
{
     DirectResult ret = DR_OK;

     D_ASSERT( ref != NULL );

     D_DEBUG_AT( Fusion_Ref, "%s( %p )\n", __FUNCTION__, ref );

     direct_mutex_lock (&ref->single.lock);

     if (ref->single.destroyed)
          ret = DR_DESTROYED;
     else if (ref->single.locked)
          ret = DR_LOCKED;
     else if (ref->single.refs)
          ret = DR_BUSY;
     else
          ref->single.locked = direct_gettid();

     direct_mutex_unlock (&ref->single.lock);

     return ret;
}

DirectResult
fusion_ref_unlock (FusionRef *ref)
{
     DirectResult ret = DR_OK;

     D_ASSERT( ref != NULL );

     D_DEBUG_AT( Fusion_Ref, "%s( %p )\n", __FUNCTION__, ref );

     direct_mutex_lock (&ref->single.lock);

     if (ref->single.locked == direct_gettid()) {
          ref->single.locked = 0;

          direct_waitqueue_broadcast (&ref->single.cond);
     }
     else
          ret = DR_ACCESSDENIED;

     direct_mutex_unlock (&ref->single.lock);

     return ret;
}

DirectResult
fusion_ref_watch (FusionRef *ref, FusionCall *call, int call_arg)
{
     DirectResult ret = DR_OK;

     D_ASSERT( ref != NULL );
     D_ASSERT( call != NULL );

     D_DEBUG_AT( Fusion_Ref, "%s( %p )\n", __FUNCTION__, ref );

     direct_mutex_lock (&ref->single.lock);

     if (ref->single.destroyed)
          ret = DR_DESTROYED;
     else if (!ref->single.refs)
          ret = DR_BUG;
     else if (ref->single.call)
          ret = DR_BUSY;
     else {
          ref->single.call     = call;
          ref->single.call_arg = call_arg;
     }

     direct_mutex_unlock (&ref->single.lock);

     return ret;
}

DirectResult
fusion_ref_inherit (FusionRef *ref, FusionRef *from)
{
     D_ASSERT( ref != NULL );
     D_ASSERT( from != NULL );

     D_DEBUG_AT( Fusion_Ref, "%s( %p, %p )\n", __FUNCTION__, ref, from );

     D_UNIMPLEMENTED();

     /* FIXME */
     return fusion_ref_up( ref, true );
}

DirectResult
fusion_ref_destroy (FusionRef *ref)
{
     D_ASSERT( ref != NULL );

     D_DEBUG_AT( Fusion_Ref, "%s( %p )\n", __FUNCTION__, ref );

     ref->single.destroyed = true;

     direct_waitqueue_broadcast (&ref->single.cond);

     direct_mutex_deinit( &ref->single.lock );
     direct_waitqueue_deinit( &ref->single.cond );

     return DR_OK;
}

DirectResult
fusion_ref_add_permissions( FusionRef            *ref,
                            FusionID              fusion_id,
                            FusionRefPermissions  ref_permissions )
{
     return DR_OK;
}

#endif

