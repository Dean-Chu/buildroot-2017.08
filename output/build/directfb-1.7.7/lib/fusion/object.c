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

#include <sys/param.h>

#include <direct/debug.h>
#include <direct/messages.h>
#include <direct/thread.h>

#include <fusion/Debug.h>

#include <fusion/conf.h>
#include <fusion/object.h>
#include <fusion/hash.h>
#include <fusion/shmalloc.h>

#include "fusion_internal.h"

D_DEBUG_DOMAIN( Fusion_Object, "Fusion/Object", "Fusion Objects and Pools" );
D_DEBUG_DOMAIN( Fusion_Object_Owner, "Fusion/Object/Owner", "Fusion Objects and Pools" );


#if 0
static FusionCallHandlerResult
object_reference_watcher( int           caller,   /* fusion id of the caller */
                          int           call_arg, /* optional call parameter */
                          void         *ptr, /* optional call parameter */
                          unsigned int  length,
                          void         *ctx,      /* optional handler context */
                          unsigned int  serial,
                          void         *ret_ptr,
                          unsigned int  ret_size,
                          unsigned int *ret_length )
#else
static FusionCallHandlerResult
object_reference_watcher( int caller, int call_arg, void *call_ptr, void *ctx, unsigned int serial, int *ret_val )
#endif
{
     FusionObject     *object;
     FusionObjectPool *pool = ctx;

     D_DEBUG_AT( Fusion_Object, "%s( %d, %d, %p, %p, %u, %p )\n",
#if 0
                 __FUNCTION__, caller, call_arg, ptr, ctx, serial, ret_ptr );
#else
                 __FUNCTION__, caller, call_arg, call_ptr, ctx, serial, ret_val );
#endif

#if FUSION_BUILD_KERNEL
     if (caller && !pool->secure) {
          D_BUG( "Call not from Fusion/Kernel (caller %d)", caller );
          return FCHR_RETURN;
     }
#endif

     D_MAGIC_ASSERT( pool, FusionObjectPool );

     /* Lock the pool. */
     if (fusion_skirmish_prevail( &pool->lock ))
          return FCHR_RETURN;

     D_MAGIC_ASSERT( pool, FusionObjectPool );

     /* Lookup the object. */
     object = fusion_hash_lookup( pool->objects, (void*)(long) call_arg );

     D_DEBUG_AT( Fusion_Object, "  -> lookup as %p\n", object );

     if (object) {
          D_MAGIC_ASSERT( object, FusionObject );

          D_DEBUG_AT( Fusion_Object, "  -> %s\n", ToString_FusionObject( object ) );

          if (object->ref.single.dead) {
               object->ref.single.dead--;

               if (object->ref.single.dead) {
                    D_DEBUG_AT( Fusion_Object, "  -> died multiple times (%d more), skipping...\n", object->ref.single.dead );

                    fusion_skirmish_dismiss( &pool->lock );
                    return FCHR_RETURN;
               }
          }


          switch (fusion_ref_zero_trylock( &object->ref )) {
               case DR_OK:
                    break;

               case DR_DESTROYED:
                    D_BUG( "already destroyed %p [%u] in '%s'", object, object->id, pool->name );

                    fusion_hash_remove( pool->objects, (void*)(long) object->id, NULL, NULL );
                    fusion_skirmish_dismiss( &pool->lock );
                    return FCHR_RETURN;


               default:
                    D_ERROR( "Fusion/ObjectPool: Error locking ref of %p [%u] in '%s'\n",
                             object, object->id, pool->name );
                    /* fall through */

               case DR_BUSY:
                    fusion_skirmish_dismiss( &pool->lock );
                    return FCHR_RETURN;
          }

          D_DEBUG_AT( Fusion_Object, "== %s ==\n", pool->name );
          D_DEBUG_AT( Fusion_Object, "  -> dead object %p [%u] (ref %x)\n", object, object->id, object->ref.multi.id );

          if (object->state == FOS_INIT) {
               D_BUG( "== %s == incomplete object: %d (%p)", pool->name, call_arg, object );
               D_WARN( "won't destroy incomplete object, leaking some memory" );
               fusion_hash_remove( pool->objects, (void*)(long) object->id, NULL, NULL );
               fusion_skirmish_dismiss( &pool->lock );
               return FCHR_RETURN;
          }

          /* Set "deinitializing" state. */
          object->state = FOS_DEINIT;

          /* Remove the object from the pool. */
          object->pool = NULL;
          fusion_hash_remove( pool->objects, (void*)(long) object->id, NULL, NULL );

          /* Unlock the pool. */
          fusion_skirmish_dismiss( &pool->lock );


          D_DEBUG_AT( Fusion_Object, "  -> calling destructor...\n" );

          /* Call the destructor. */
          pool->destructor( object, false, pool->ctx );

          D_DEBUG_AT( Fusion_Object, "  -> destructor done.\n" );

          return FCHR_RETURN;
     }

     D_BUG( "unknown object [%d] in '%s'", call_arg, pool->name );

     /* Unlock the pool. */
     fusion_skirmish_dismiss( &pool->lock );

     return FCHR_RETURN;
}

FusionObjectPool *
fusion_object_pool_create( const char             *name,
                           int                     object_size,
                           int                     message_size,
                           FusionObjectDestructor  destructor,
                           void                   *ctx,
                           const FusionWorld      *world )
{
     FusionObjectPool  *pool;
     FusionWorldShared *shared;

     D_ASSERT( name != NULL );
     D_ASSERT( object_size >= sizeof(FusionObject) );
     D_ASSERT( destructor != NULL );
     D_MAGIC_ASSERT( world, FusionWorld );

     D_DEBUG_AT( Fusion_Object, "%s( '%s' )\n", __FUNCTION__, name );

     shared = world->shared;

     D_MAGIC_ASSERT( shared, FusionWorldShared );

     /* Allocate shared memory for the pool. */
     pool = SHCALLOC( shared->main_pool, 1, sizeof(FusionObjectPool) );
     if (!pool) {
          D_OOSHM();
          return NULL;
     }

     /* Initialize the pool lock. */
     fusion_skirmish_init2( &pool->lock, name, world, false );

     fusion_skirmish_add_permissions( &pool->lock, 0, FUSION_SKIRMISH_PERMIT_PREVAIL | FUSION_SKIRMISH_PERMIT_DISMISS );

     /* Fill information. */
     pool->shared       = shared;
     pool->name         = SHSTRDUP( shared->main_pool, name );
     pool->object_size  = object_size;
     pool->message_size = message_size;
     pool->destructor   = destructor;
     pool->ctx          = ctx;
     pool->secure       = fusion_config->secure_fusion;

     fusion_hash_create( shared->main_pool, HASH_INT, HASH_PTR, 17, &pool->objects );

     /* Destruction call from Fusion. */
     fusion_call_init( &pool->call, object_reference_watcher, pool, world );
     fusion_call_set_name( &pool->call, "object_reference_watcher" );

     D_MAGIC_SET( pool, FusionObjectPool );

     return pool;
}

DirectResult
fusion_object_pool_destroy( FusionObjectPool *pool,
                            FusionWorld      *world )
{
     DirectResult        ret;
     FusionObject       *object;
     FusionWorldShared  *shared;
     FusionHashIterator  it;

     D_MAGIC_ASSERT( pool, FusionObjectPool );
     D_MAGIC_ASSERT( world, FusionWorld );

     D_DEBUG_AT( Fusion_Object, "%s( %p '%s' )\n", __FUNCTION__, pool, pool->name );

     shared = world->shared;

     D_MAGIC_ASSERT( shared, FusionWorldShared );
     D_ASSERT( shared == pool->shared );

     D_DEBUG_AT( Fusion_Object, "== %s ==\n", pool->name );
     D_DEBUG_AT( Fusion_Object, "  -> destroying pool...\n" );

     fusion_world_flush_calls( world, 1 );

     D_DEBUG_AT( Fusion_Object, "  -> syncing...\n" );

     /* Wait for processing of pending messages. */
     if (fusion_hash_size(pool->objects))
          fusion_sync( world );

     D_DEBUG_AT( Fusion_Object, "  -> locking...\n" );

     /* Lock the pool. */
     ret = fusion_skirmish_prevail( &pool->lock );
     if (ret)
          return ret;

     /* Destroy the call. */
     fusion_call_destroy( &pool->call );

     /* Destroy zombies */
     fusion_hash_foreach (object, it, pool->objects) {
          int refs;

          fusion_ref_stat( &object->ref, &refs );

          if (refs > 0) {
               if (direct_config_get_int_value( "shutdown-info" )) {
                    D_WARN( "zombie %p [%u], refs %d (in %s) => ref id 0x%x", object, object->id, refs, pool->name, object->ref.multi.id );

                    direct_trace_print_stack( object->create_stack );
               }
          }

          D_DEBUG_AT( Fusion_Object, "== %s ==\n", pool->name );
          D_DEBUG_AT( Fusion_Object, "  -> %p [%u], refs %d\n", object, object->id, refs );

          /* Set "deinitializing" state. */
          object->state = FOS_DEINIT;

          D_DEBUG_AT( Fusion_Object, "  -> calling destructor...\n" );

          /* Call the destructor. */
          pool->destructor( object, refs > 0, pool->ctx );

          D_DEBUG_AT( Fusion_Object, "  -> destructor done.\n" );
     }

     fusion_hash_destroy( pool->objects );

     D_MAGIC_CLEAR( pool );

     D_DEBUG_AT( Fusion_Object, "  -> pool destroyed (%s)\n", pool->name );

     /* Destroy the pool lock. */
     fusion_skirmish_dismiss( &pool->lock );
     fusion_skirmish_destroy( &pool->lock );

     /* Deallocate shared memory. */
     SHFREE( shared->main_pool, pool->name );
     SHFREE( shared->main_pool, pool );

     return DR_OK;
}

DirectResult
fusion_object_pool_set_describe( FusionObjectPool     *pool,
                                 FusionObjectDescribe  func )
{
     D_MAGIC_ASSERT( pool, FusionObjectPool );

     pool->describe = func;

     return DR_OK;
}

typedef struct {
     FusionObjectPool     *pool;
     FusionObjectCallback  callback;
     void                 *ctx;
} ObjectIteratorContext;

static bool
object_iterator( FusionHash *hash,
                 void       *key,
                 void       *value,
                 void       *ctx )
{
     ObjectIteratorContext *context = ctx;
     FusionObject          *object  = value;

     D_MAGIC_ASSERT( object, FusionObject );

     return !context->callback( context->pool, object, context->ctx );
}

DirectResult
fusion_object_pool_enum( FusionObjectPool     *pool,
                         FusionObjectCallback  callback,
                         void                 *ctx )
{
     ObjectIteratorContext iterator_context;

     D_MAGIC_ASSERT( pool, FusionObjectPool );
     D_ASSERT( callback != NULL );

     D_DEBUG_AT( Fusion_Object, "%s( %p '%s' )\n", __FUNCTION__, pool, pool->name );

     /* Lock the pool. */
     if (fusion_skirmish_prevail( &pool->lock ))
          return DR_FUSION;

     iterator_context.pool     = pool;
     iterator_context.callback = callback;
     iterator_context.ctx      = ctx;

     fusion_hash_iterate( pool->objects, object_iterator, &iterator_context );

     /* Unlock the pool. */
     fusion_skirmish_dismiss( &pool->lock );

     return DR_OK;
}

DirectResult
fusion_object_pool_size( FusionObjectPool *pool,
                         size_t           *ret_size )
{
     D_MAGIC_ASSERT( pool, FusionObjectPool );

     if (!ret_size)
          return DR_INVARG;

     *ret_size = fusion_hash_size( pool->objects );

     return DR_OK;
}

FusionObject *
fusion_object_create( FusionObjectPool  *pool,
                      const FusionWorld *world,
                      FusionID           identity )
{
     FusionObject      *object;
     FusionWorldShared *shared;

     D_MAGIC_ASSERT( pool, FusionObjectPool );
     D_MAGIC_ASSERT( world, FusionWorld );

     D_DEBUG_AT( Fusion_Object, "%s( %p '%s', identity %lu )\n", __FUNCTION__, pool, pool->name, identity );

     shared = world->shared;

     D_MAGIC_ASSERT( shared, FusionWorldShared );
     D_ASSERT( shared == pool->shared );

     /* Lock the pool. */
     if (fusion_skirmish_prevail( &pool->lock ))
          return NULL;

     /* Allocate shared memory for the object. */
     object = SHCALLOC( shared->main_pool, 1, pool->object_size );
     if (!object) {
          D_OOSHM();
          fusion_skirmish_dismiss( &pool->lock );
          return NULL;
     }

     /* Set "initializing" state. */
     object->state = FOS_INIT;

     /* Set object id. */
     object->id = ++pool->id_pool;

     object->identity = identity;

     if (pool->secure || world->fusion_id == FUSION_ID_MASTER)
          object->create_stack = direct_trace_copy_buffer( NULL );

     /* Initialize the reference counter. */
     if (fusion_ref_init2( &object->ref, pool->name, pool->secure, world )) {
          SHFREE( shared->main_pool, object );
          fusion_skirmish_dismiss( &pool->lock );
          return NULL;
     }

     /* Increase the object's reference counter. */
     fusion_ref_up( &object->ref, false );

     /* Install handler for automatic destruction. */
     if (fusion_ref_watch( &object->ref, &pool->call, object->id )) {
          fusion_ref_destroy( &object->ref );
          SHFREE( shared->main_pool, object );
          fusion_skirmish_dismiss( &pool->lock );
          return NULL;
     }

     /* Create a reactor for message dispatching. */
     object->reactor = fusion_reactor_new( pool->message_size, pool->name, world );
     if (!object->reactor) {
          fusion_ref_destroy( &object->ref );
          SHFREE( shared->main_pool, object );
          fusion_skirmish_dismiss( &pool->lock );
          return NULL;
     }

     fusion_reactor_set_lock( object->reactor, &pool->lock );

     fusion_vector_init( &object->access, 1, shared->main_pool );
     fusion_vector_init( &object->owners, 1, shared->main_pool );

     /* Set pool/world back pointer. */
     object->pool   = pool;
     object->shared = shared;

     /* Add the object to the pool. */
     fusion_hash_insert( pool->objects, (void*)(long) object->id, object );

     D_DEBUG_AT( Fusion_Object, "== %s ==\n", pool->name );
     D_DEBUG_AT( Fusion_Object, "  -> added object %p [%u] (ref %x)\n", object, object->id, object->ref.multi.id );

     D_MAGIC_SET( object, FusionObject );

     /* Unlock the pool. */
     fusion_skirmish_dismiss( &pool->lock );

     return object;
}

DirectResult
fusion_object_get( FusionObjectPool  *pool,
                   FusionObjectID     object_id,
                   FusionObject     **ret_object )
{
     DirectResult  ret;
     FusionObject *object = NULL;

     D_MAGIC_ASSERT( pool, FusionObjectPool );
     D_ASSERT( ret_object != NULL );

     D_DEBUG_AT( Fusion_Object, "%s( %p '%s', object_id %u )\n", __FUNCTION__, pool, pool->name, object_id );

     /* Lock the pool. */
     ret = fusion_skirmish_prevail( &pool->lock );
     if (ret == DR_OK) {
          object = fusion_hash_lookup( pool->objects, (void*)(long) object_id );
          if (object) {
               int refs;

               D_DEBUG_AT( Fusion_Object, "  -> %s\n", ToString_FusionObject(object) );

               ret = fusion_ref_stat( &object->ref, &refs );
               if (ret == DR_OK) {
                    D_DEBUG_AT( Fusion_Object, "  -> refs %d\n", refs );

                    if (refs > 0)
                         ret = fusion_object_ref( object );
                    else
                         ret = DR_DEAD;
               }
          }
          else {
               D_DEBUG_AT( Fusion_Object, "  -> NOT FOUND\n" );
               ret = DR_IDNOTFOUND;
          }
     }

     if (ret == DR_OK)
          *ret_object = object;

     /* Unlock the pool. */
     fusion_skirmish_dismiss( &pool->lock );

     return ret;
}

DirectResult
fusion_object_lookup( FusionObjectPool  *pool,
                      FusionObjectID     object_id,
                      FusionObject     **ret_object )
{
     DirectResult  ret    = DR_IDNOTFOUND;
     FusionObject *object = NULL;

     D_MAGIC_ASSERT( pool, FusionObjectPool );
     D_ASSERT( ret_object != NULL );

     D_DEBUG_AT( Fusion_Object, "%s( %p '%s', object_id %u )\n", __FUNCTION__, pool, pool->name, object_id );

     /* Lock the pool. */
     if (fusion_skirmish_prevail( &pool->lock ))
          return DR_FUSION;

     object = fusion_hash_lookup( pool->objects, (void*)(long) object_id );
     if (object) {
          D_DEBUG_AT( Fusion_Object, "  -> %s\n", ToString_FusionObject(object) );

          ret = DR_OK;
     }
     else
          D_DEBUG_AT( Fusion_Object, "  -> NOT FOUND\n" );

     *ret_object = object;

     /* Unlock the pool. */
     fusion_skirmish_dismiss( &pool->lock );

     return ret;
}

DirectResult
fusion_object_set_lock( FusionObject   *object,
                        FusionSkirmish *lock )
{
     D_MAGIC_ASSERT( object, FusionObject );

     D_ASSERT( lock != NULL );

     D_ASSUME( object->state == FOS_INIT );

     return fusion_reactor_set_lock_only( object->reactor, lock );
}

DirectResult
fusion_object_activate( FusionObject *object )
{
     D_MAGIC_ASSERT( object, FusionObject );

     D_DEBUG_AT( Fusion_Object, "%s( %s )\n", __FUNCTION__, ToString_FusionObject(object) );

     /* Set "active" state. */
     object->state = FOS_ACTIVE;

     return DR_OK;
}

DirectResult
fusion_object_destroy( FusionObject *object )
{
     FusionObjectPool  *pool;
     FusionWorldShared *shared;
     char              *access;
     int                index;

     D_MAGIC_ASSERT( object, FusionObject );
     D_ASSERT( object->state != FOS_ACTIVE );

     D_DEBUG_AT( Fusion_Object, "%s( %s )\n", __FUNCTION__, ToString_FusionObject(object) );

     shared = object->shared;

     D_MAGIC_ASSERT( shared, FusionWorldShared );

     pool = object->pool;

//     D_ASSUME( pool != NULL );

     /* Set "deinitializing" state. */
     object->state = FOS_DEINIT;

     /* Remove the object from the pool. */
     if (pool) {
          D_MAGIC_ASSERT( pool, FusionObjectPool );

          /* Lock the pool. */
          if (fusion_skirmish_prevail( &pool->lock ))
               return DR_FAILURE;

          D_MAGIC_ASSERT( pool, FusionObjectPool );

          D_ASSUME( object->pool != NULL );

          /* Remove the object from the pool. */
          if (object->pool) {
               D_ASSERT( object->pool == pool );

               object->pool = NULL;

               fusion_hash_remove( pool->objects, (void*)(long) object->id, NULL, NULL );
          }

          /* Unlock the pool. */
          fusion_skirmish_dismiss( &pool->lock );
     }

     fusion_vector_foreach (access, index, object->access) {
          SHFREE( object->shared->main_pool, access );
     }

     fusion_vector_destroy( &object->access );
     fusion_vector_destroy( &object->owners );

     fusion_ref_destroy( &object->ref );

     fusion_reactor_free( object->reactor );

     if ( object->properties )
          fusion_hash_destroy(object->properties);

     if (object->create_stack)
          direct_trace_free_buffer( object->create_stack );

     D_MAGIC_CLEAR( object );
     SHFREE( shared->main_pool, object );
     return DR_OK;
}

/*
 * Sets a value for a key.
 * If the key currently has a value the old value is returned
 * in old_value.
 * If old_value is null the object is freed with SHFREE.
 * If this is not the correct semantics for your data, if for example
 * its reference counted  you must pass in a old_value.
 */
DirectResult
fusion_object_set_property( FusionObject  *object,
                            const char    *key,
                            void          *value,
                            void         **old_value )
{
     DirectResult  ret;
     char         *sharedkey;

     D_MAGIC_ASSERT( object, FusionObject );
     D_ASSERT( object->shared != NULL );
     D_ASSERT( key != NULL );
     D_ASSERT( value != NULL );

     /* Create property hash on demand. */
     if (!object->properties) {
          ret = fusion_hash_create( object->shared->main_pool,
                                    HASH_STRING, HASH_PTR,
                                    FUSION_HASH_MIN_SIZE,
                                    &object->properties );
          if (ret)
               return ret;
     }

     /* Create a shared copy of the key. */
     sharedkey = SHSTRDUP( object->shared->main_pool, key );
     if (!sharedkey)
          return D_OOSHM();

     /* Put it into the hash. */
     ret = fusion_hash_replace( object->properties, sharedkey,
                                value, NULL, old_value );
     if (ret)
          SHFREE( object->shared->main_pool, sharedkey );

     return ret;
}

/*
 * Helper function for int values
 */
DirectResult
fusion_object_set_int_property( FusionObject *object,
                                const char   *key,
                                int           value )
{
     DirectResult  ret;
     int          *iptr;

     D_MAGIC_ASSERT( object, FusionObject );
     D_ASSERT( key != NULL );

     iptr = SHMALLOC( object->shared->main_pool, sizeof(int) );
     if (!iptr)
          return D_OOSHM();

     *iptr = value;

     ret = fusion_object_set_property( object, key, iptr, NULL );
     if (ret)
          SHFREE( object->shared->main_pool, iptr );

     return ret;
}

/*
 * Helper function for char* values use if the string 
 * is not in shared memory
 * Assumes that the old value was a string and frees it.
 */
DirectResult
fusion_object_set_string_property( FusionObject *object,
                                   const char   *key,
                                   char         *value )
{
     DirectResult  ret;
     char         *copy;

     D_MAGIC_ASSERT( object, FusionObject );
     D_ASSERT( key != NULL );
     D_ASSERT( value != NULL );

     copy = SHSTRDUP( object->shared->main_pool, value );
     if (!copy)
          return D_OOSHM();

     ret = fusion_object_set_property( object, key, copy, NULL );
     if (ret)
          SHFREE( object->shared->main_pool, copy );

     return ret;
}

void *
fusion_object_get_property( FusionObject *object, const char *key )
{
     D_MAGIC_ASSERT( object, FusionObject );
     D_ASSERT( key != NULL );

     if (!object->properties)
          return NULL;

     return fusion_hash_lookup( object->properties, key );
}

void 
fusion_object_remove_property( FusionObject  *object,
                               const char    *key,
                               void         **old_value)
{
     D_MAGIC_ASSERT( object, FusionObject );
     D_ASSERT( key != NULL );

     if (!object->properties)
          return;

     fusion_hash_remove( object->properties, key, NULL, old_value );

     if (fusion_hash_should_resize( object->properties ))
          fusion_hash_resize( object->properties );
}

DirectResult
fusion_object_add_access( FusionObject *object,
                          const char   *executable )
{
     DirectResult  ret;
     char         *copy;

     D_DEBUG_AT( Fusion_Object, "%s( %p, '%s' )\n", __FUNCTION__, object, executable );

     D_MAGIC_ASSERT( object, FusionObject );
     D_ASSERT( executable != NULL );

     copy = SHSTRDUP( object->shared->main_pool, executable );
     if (!copy)
          return D_OOM();

     ret = fusion_vector_add( &object->access, copy );
     if (ret) {
          SHFREE( object->shared->main_pool, copy );
          return ret;
     }

     return DR_OK;
}

DirectResult
fusion_object_has_access( FusionObject *object,
                          const char   *executable )
{
     char *access;
     int   index;

     D_DEBUG_AT( Fusion_Object, "%s( %p, '%s' )\n", __FUNCTION__, object, executable );

     D_MAGIC_ASSERT( object, FusionObject );
     D_ASSERT( executable != NULL );

     fusion_vector_foreach (access, index, object->access) {
          int len = strlen( access );

          if (access[len-1] == '*') {
               if (!strncmp( access, executable, len-1 ))
                    return DR_OK;
          }
          else
               if (!strcmp( access, executable ))
                    return DR_OK;
     }

     return DR_ACCESSDENIED;
}

DirectResult
fusion_object_add_owner( FusionObject *object,
                         FusionID      owner )
{
     FusionID  id;
     int       i;

     D_DEBUG_AT( Fusion_Object, "%s( %p, %lu )\n", __FUNCTION__, object, owner );

     D_MAGIC_ASSERT( object, FusionObject );

     fusion_vector_foreach (id, i, object->owners) {
          if (id == owner)
               return DR_OK;
     }

     D_DEBUG_AT( Fusion_Object_Owner, "  = add %lu (object %p id %u)\n", owner, object, object->id );

     return fusion_vector_add( &object->owners, (void*) owner );
}

DirectResult
fusion_object_check_owner( FusionObject *object,
                           FusionID      owner,
                           bool          succeed_if_not_owned )
{
     FusionID  id;
     int       i;

     D_DEBUG_AT( Fusion_Object, "%s( %p, %lu )\n", __FUNCTION__, object, owner );

     D_MAGIC_ASSERT( object, FusionObject );

     D_DEBUG_AT( Fusion_Object_Owner, "  = check %lu and %ssucceed if not owned (object %p id %u)\n",
                 owner, succeed_if_not_owned ? "" : "DON'T ", object, object->id );

     if (succeed_if_not_owned && object->owners.count == 0) {
          D_DEBUG_AT( Fusion_Object_Owner, "   -> SUCCESS (no owner)\n" );
          return DR_OK;
     }

     fusion_vector_foreach (id, i, object->owners) {
          if (id == owner) {
               D_DEBUG_AT( Fusion_Object_Owner, "   -> SUCCESS (found as owner with index %d)\n", i );
               return DR_OK;
          }
     }

     D_DEBUG_AT( Fusion_Object_Owner, "   -> FAIL (not found)\n" );

     return DR_IDNOTFOUND;
}

DirectResult
fusion_object_catch( FusionObject *object )
{
     DirectResult ret;

     D_MAGIC_ASSERT( object, FusionObject );

     ret = fusion_ref_up( &object->ref, false );
     if (ret)
          return ret;

     ret = fusion_ref_catch( &object->ref );
     if (ret) {
          D_DERROR( ret, "Fusion/Object: Failed to catch reference 0x%08x!\n", object->ref.multi.id );
          fusion_ref_down( &object->ref, false );
          return ret;
     }

     return DR_OK;
}

