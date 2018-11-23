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



//#define DIRECT_ENABLE_DEBUG

#include <config.h>

#include <direct/Types++.h>

#include <directfb.h>    // include here to prevent it being included indirectly causing nested extern "C"


extern "C" {
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef USE_ZLIB
#include <zlib.h>
#endif

#include <directfb_util.h>

#include <direct/debug.h>
#include <direct/memcpy.h>

#include <fusion/shmalloc.h>

#include <core/CoreSurface.h>

#include <core/gfxcard.h>
#include <core/palette.h>
#include <core/surface.h>
#include <core/surface_buffer.h>
#include <core/surface_pool.h>
#include <core/surface_pool_bridge.h>

#include <misc/conf.h>

#include <gfx/convert.h>
}


#include <direct/Lists.h>

#include <core/SurfaceTask.h>


D_DEBUG_DOMAIN( Core_SurfAllocation, "Core/SurfAllocation", "DirectFB Core Surface Allocation" );

/**********************************************************************************************************************/

static void
surface_allocation_destructor( FusionObject *object, bool zombie, void *ctx )
{
     CoreSurfaceAllocation *allocation = (CoreSurfaceAllocation*) object;

     D_DEBUG_AT( Core_SurfAllocation, "destroying %p (size %d)\n", allocation, allocation->size );

     D_MAGIC_ASSERT( allocation, CoreSurfaceAllocation );

     if (!D_FLAGS_IS_SET(allocation->flags, CSALF_INITIALIZING)) {
          if (allocation->surface)
               dfb_surface_lock( allocation->surface );

          CORE_SURFACE_ALLOCATION_ASSERT( allocation );

          dfb_surface_pool_deallocate( allocation->pool, allocation );

          if (allocation->surface)
               dfb_surface_unlock( allocation->surface );
     }

     if (allocation->data)
          SHFREE( allocation->pool->shmpool, allocation->data );

     if (allocation->read_tasks)
          delete allocation->read_tasks;

     direct_serial_deinit( &allocation->serial );

     D_MAGIC_CLEAR( allocation );

     fusion_object_destroy( object );
}

FusionObjectPool *
dfb_surface_allocation_pool_create( const FusionWorld *world )
{
     FusionObjectPool *pool;

     pool = fusion_object_pool_create( "Surface Allocation Pool",
                                       sizeof(CoreSurfaceAllocation),
                                       sizeof(CoreSurfaceAllocationNotification),
                                       surface_allocation_destructor, NULL, world );

     return pool;
}

/**********************************************************************************************************************/

DFBResult
dfb_surface_allocation_create( CoreDFB                *core,
                               CoreSurfaceBuffer      *buffer,
                               CoreSurfacePool        *pool,
                               CoreSurfaceAllocation **ret_allocation )
{
     DFBResult              ret;
     CoreSurface           *surface;
     CoreSurfaceAllocation *allocation;

     D_MAGIC_ASSERT( buffer, CoreSurfaceBuffer );
     D_ASSERT( pool != NULL );
     D_ASSERT( ret_allocation != NULL );

     D_DEBUG_AT( Core_SurfAllocation, "%s( %dx%d %s )\n", __FUNCTION__,
                 buffer->config.size.w, buffer->config.size.h, dfb_pixelformat_name( buffer->config.format ) );

     surface = buffer->surface;
     D_MAGIC_ASSERT( surface, CoreSurface );

     allocation = dfb_core_create_surface_allocation( core );
     if (!allocation)
          return DFB_FUSION;

     allocation->buffer      = buffer;
     allocation->surface     = surface;
     allocation->pool        = pool;
     allocation->access      = pool->desc.access;
     allocation->config      = buffer->config;
     allocation->type        = buffer->type;
     allocation->resource_id = buffer->resource_id;
     allocation->flags       = CSALF_INITIALIZING;
     allocation->index       = buffer->index;

     if (pool->alloc_data_size) {
          allocation->data = SHCALLOC( pool->shmpool, 1, pool->alloc_data_size );
          if (!allocation->data) {
               ret = (DFBResult) D_OOSHM();
               goto error;
          }
     }

     direct_serial_init( &allocation->serial );

     fusion_ref_add_permissions( &allocation->object.ref, 0, FUSION_REF_PERMIT_REF_UNREF_LOCAL );

     D_MAGIC_SET( allocation, CoreSurfaceAllocation );

     fusion_object_activate( &allocation->object );

     *ret_allocation = allocation;

     D_DEBUG_AT( Core_SurfAllocation, "%s(): allocation=%p\n", __FUNCTION__, (void *)allocation );

     return DFB_OK;


error:
     if (allocation->data)
          SHFREE( pool->shmpool, allocation->data );

     fusion_object_destroy( &allocation->object );

     return ret;
}

DFBResult
dfb_surface_allocation_decouple( CoreSurfaceAllocation *allocation )
{
     int                    i;
     int                    locks;
     CoreSurfaceBuffer     *buffer;
     CoreSurfaceAllocation *alloc;

     D_DEBUG_AT( Core_SurfAllocation, "%s( %p )\n", __FUNCTION__, allocation );

     D_MAGIC_ASSERT( allocation, CoreSurfaceAllocation );

     buffer = allocation->buffer;
     D_MAGIC_ASSERT( buffer, CoreSurfaceBuffer );

     D_MAGIC_ASSERT( allocation->surface, CoreSurface );
     D_ASSERT( allocation->surface == allocation->buffer->surface );

     /* Indicate that this surface buffer pool allocation is about to be destroyed. */
     dfb_surface_pool_notify( allocation->surface, buffer, allocation, CSNF_BUFFER_ALLOCATION_DESTROY );

     allocation->buffer  = NULL;
     allocation->surface = NULL;

     fusion_vector_remove( &buffer->allocs, fusion_vector_index_of( &buffer->allocs, allocation ) );

     locks = dfb_surface_allocation_locks( allocation );
     if (!locks) {
          if (allocation->accessed[CSAID_GPU] & (CSAF_READ | CSAF_WRITE))
               /* ...wait for the operation to finish. */
               dfb_gfxcard_wait_serial( &allocation->gfx_serial );

          dfb_surface_pool_deallocate( allocation->pool, allocation );
     }

     /* Reset 'read' allocation pointer of buffer */
     if (buffer->read == allocation)
          buffer->read = NULL;

     /* Update 'written' allocation pointer of buffer */
     if (buffer->written == allocation) {
          /* Reset pointer first */
          buffer->written = NULL;

          /* Iterate through remaining allocations */
          fusion_vector_foreach (alloc, i, buffer->allocs) {
               CORE_SURFACE_ALLOCATION_ASSERT( alloc );

               /* Check if allocation is up to date and set it as 'written' allocation */
               if (direct_serial_check( &alloc->serial, &buffer->serial )) {
                    buffer->written = alloc;
                    break;
               }
          }
     }

     dfb_surface_allocation_unlink( &allocation );

     return DFB_OK;
}

/**********************************************************************************************************************/

static void
transfer_buffer( const CoreSurfaceConfig *config,
                 const char              *src,
                 char                    *dst,
                 int                      srcpitch,
                 int                      dstpitch )
{
     int i;

     D_DEBUG_AT( Core_SurfAllocation, "%s( %p, %p [%d] -> %p [%d] ) * %d\n",
                 __FUNCTION__, config, src, srcpitch, dst, dstpitch, config->size.h );

     D_ASSERT( src != NULL );
     D_ASSERT( dst != NULL );
     D_ASSERT( srcpitch > 0 );
     D_ASSERT( dstpitch > 0 );

     D_ASSERT( srcpitch >= DFB_BYTES_PER_LINE( config->format, config->size.w ) );
     D_ASSERT( dstpitch >= DFB_BYTES_PER_LINE( config->format, config->size.w ) );

     for (i=0; i<config->size.h; i++) {
          direct_memcpy( dst, src, DFB_BYTES_PER_LINE( config->format, config->size.w ) );

          src += srcpitch;
          dst += dstpitch;
     }

     switch (config->format) {
          case DSPF_YV12:
          case DSPF_I420:
               for (i=0; i<config->size.h; i++) {
                    direct_memcpy( dst, src,
                                   DFB_BYTES_PER_LINE( config->format, config->size.w / 2 ) );
                    src += srcpitch / 2;
                    dst += dstpitch / 2;
               }
               break;

          case DSPF_YV16:
               for (i=0; i<config->size.h*2; i++) {
                    direct_memcpy( dst, src,
                                   DFB_BYTES_PER_LINE( config->format, config->size.w / 2 ) );
                    src += srcpitch / 2;
                    dst += dstpitch / 2;
               }
               break;

          case DSPF_NV12:
          case DSPF_NV21:
               for (i=0; i<config->size.h/2; i++) {
                    direct_memcpy( dst, src,
                                   DFB_BYTES_PER_LINE( config->format, config->size.w ) );
                    src += srcpitch;
                    dst += dstpitch;
               }
               break;

          case DSPF_NV16:
               for (i=0; i<config->size.h; i++) {
                    direct_memcpy( dst, src,
                                   DFB_BYTES_PER_LINE( config->format, config->size.w ) );
                    src += srcpitch;
                    dst += dstpitch;
               }
               break;

          case DSPF_YUV444P:
               for (i=0; i<config->size.h*2; i++) {
                    direct_memcpy( dst, src,
                                   DFB_BYTES_PER_LINE( config->format, config->size.w ) );
                    src += srcpitch;
                    dst += dstpitch;
               }
               break;

          default:
               break;
     }
}

static DFBResult
allocation_update_copy( CoreSurfaceAllocation *allocation,
                        CoreSurfaceAllocation *source )
{
     DFBResult              ret;
     CoreSurfaceBufferLock  src;
     CoreSurfaceBufferLock  dst;

     D_DEBUG_AT( Core_SurfAllocation, "%s( %p )\n", __FUNCTION__, (void *)allocation);

     D_ASSERT( allocation != source );

     D_MAGIC_ASSERT( allocation, CoreSurfaceAllocation );
     D_MAGIC_ASSERT( source, CoreSurfaceAllocation );

     /* Lock the source allocation. */
     dfb_surface_buffer_lock_init( &src, CSAID_CPU, CSAF_READ );

     dfb_surface_pool_prelock( source->pool, source, CSAID_CPU, CSAF_READ );

     ret = dfb_surface_pool_lock( source->pool, source, &src );
     if (ret) {
          D_DERROR( ret, "Core/SurfBuffer: Could not lock source for transfer!\n" );
          dfb_surface_buffer_lock_deinit( &src );
          return ret;
     }

     /* Lock the destination allocation. */
     dfb_surface_buffer_lock_init( &dst, CSAID_CPU, CSAF_WRITE );

     dfb_surface_pool_prelock( allocation->pool, allocation, CSAID_CPU, CSAF_WRITE );

     /*
      * Track that the CPU wrote to the destination buffer allocation and that it read
      * from the source buffer allocation so that proper cache flushing will occur.
      */
     if (!dfb_config->task_manager) {
          allocation->accessed[CSAID_CPU] = (CoreSurfaceAccessFlags)(allocation->accessed[CSAID_CPU] | CSAF_WRITE);
          source->accessed[CSAID_CPU]     = (CoreSurfaceAccessFlags)(source->accessed[CSAID_CPU]     | CSAF_READ);
     }

     ret = dfb_surface_pool_lock( allocation->pool, allocation, &dst );
     if (ret) {
          D_DERROR( ret, "Core/SurfBuffer: Could not lock destination for transfer!\n" );
          dfb_surface_pool_unlock( source->pool, source, &src );
          dfb_surface_buffer_lock_deinit( &dst );
          dfb_surface_buffer_lock_deinit( &src );
          return ret;
     }

     transfer_buffer( &allocation->config, (char*) src.addr, (char*) dst.addr, src.pitch, dst.pitch );

     dfb_surface_pool_unlock( allocation->pool, allocation, &dst );
     dfb_surface_pool_unlock( source->pool, source, &src );

     dfb_surface_buffer_lock_deinit( &dst );
     dfb_surface_buffer_lock_deinit( &src );

     return DFB_OK;
}

static DFBResult
allocation_update_write( CoreSurfaceAllocation *allocation,
                         CoreSurfaceAllocation *source )
{
     DFBResult              ret;
     CoreSurfaceBufferLock  src;

     D_DEBUG_AT( Core_SurfAllocation, "%s( %p )\n", __FUNCTION__, (void *)allocation );

     D_ASSERT( allocation != source );

     D_MAGIC_ASSERT( allocation, CoreSurfaceAllocation );
     D_MAGIC_ASSERT( source, CoreSurfaceAllocation );

     /* Lock the source allocation. */
     dfb_surface_buffer_lock_init( &src, CSAID_CPU, CSAF_READ );

     dfb_surface_pool_prelock( source->pool, source, CSAID_CPU, CSAF_READ );

     /*
      * Track that the CPU wrote to the destination buffer allocation and that it read
      * from the source buffer allocation so that proper cache flushing will occur.
      */
     if (!dfb_config->task_manager)
          source->accessed[CSAID_CPU] = (CoreSurfaceAccessFlags)(source->accessed[CSAID_CPU] | CSAF_READ);

     ret = dfb_surface_pool_lock( source->pool, source, &src );
     if (ret) {
          D_DERROR( ret, "Core/SurfBuffer: Could not lock source for transfer!\n" );
          dfb_surface_buffer_lock_deinit( &src );
          return ret;
     }

     /* Write to the destination allocation. */
     ret = dfb_surface_pool_write( allocation->pool, allocation, (char*) src.addr, src.pitch, NULL );
     if (ret)
          D_DERROR( ret, "Core/SurfBuffer: Could not write from destination allocation!\n" );

     dfb_surface_pool_unlock( source->pool, source, &src );

     dfb_surface_buffer_lock_deinit( &src );

     return ret;
}

static DFBResult
allocation_update_read( CoreSurfaceAllocation *allocation,
                        CoreSurfaceAllocation *source )
{
     DFBResult              ret;
     CoreSurfaceBufferLock  dst;

     D_DEBUG_AT( Core_SurfAllocation, "%s( %p )\n", __FUNCTION__, (void *)allocation );

     D_ASSERT( allocation != source );

     D_MAGIC_ASSERT( allocation, CoreSurfaceAllocation );
     D_MAGIC_ASSERT( source, CoreSurfaceAllocation );

     /* Lock the destination allocation. */
     dfb_surface_buffer_lock_init( &dst, CSAID_CPU, CSAF_WRITE );

     dfb_surface_pool_prelock( allocation->pool, allocation, CSAID_CPU, CSAF_WRITE );

     /*
      * Track that the CPU wrote to the destination buffer allocation and that it read
      * from the source buffer allocation so that proper cache flushing will occur.
      */
     if (!dfb_config->task_manager)
          allocation->accessed[CSAID_CPU] = (CoreSurfaceAccessFlags)(allocation->accessed[CSAID_CPU] | CSAF_READ);

     ret = dfb_surface_pool_lock( allocation->pool, allocation, &dst );
     if (ret) {
          D_DERROR( ret, "Core/SurfBuffer: Could not lock destination for transfer!\n" );
          dfb_surface_buffer_lock_deinit( &dst );
          return ret;
     }

     /* Read from the source allocation. */
     ret = dfb_surface_pool_read( source->pool, source, dst.addr, dst.pitch, NULL );
     if (ret)
          D_DERROR( ret, "Core/SurfBuffer: Could not read from source allocation!\n" );

     dfb_surface_pool_unlock( allocation->pool, allocation, &dst );

     dfb_surface_buffer_lock_deinit( &dst );

     return ret;
}


namespace DirectFB {


class TransferTask : public SurfaceTask
{
public:
     TransferTask( CoreSurfaceAllocation *allocation,
                   CoreSurfaceAllocation *source )
          :
          SurfaceTask( CSAID_CPU ), // FIXME
          allocation( allocation ),
          source( source )
     {
          D_ASSUME( allocation != source );
          D_ASSERT( source->buffer == allocation->buffer );

          buffer = source->buffer;

          dfb_surface_buffer_ref( buffer );
     }

     virtual ~TransferTask()
     {
          dfb_surface_buffer_unref( buffer );
     }

     static DFBResult Generate( CoreSurfaceAllocation *allocation,
                                CoreSurfaceAllocation *source )
     {
          TransferTask *task = new TransferTask( allocation, source );

          task->AddAccess( allocation, CSAF_WRITE );
          task->AddAccess( source, CSAF_READ );

          task->Flush();

          return DFB_OK;
     }

protected:
     virtual DFBResult Run()
     {
          DFBResult ret;

          D_DEBUG_AT( Core_SurfAllocation, "  -> updating allocation %p from %p...\n", allocation, source );

          D_MAGIC_ASSERT( source, CoreSurfaceAllocation );

          ret = dfb_surface_pool_bridges_transfer( buffer, source, allocation, NULL, 0 );
          if (ret) {
               if ((source->access[CSAID_CPU] & CSAF_READ) && (allocation->access[CSAID_CPU] & CSAF_WRITE))
                    ret = allocation_update_copy( allocation, source );
               else if (source->access[CSAID_CPU] & CSAF_READ)
                    ret = allocation_update_write( allocation, source );
               else if (allocation->access[CSAID_CPU] & CSAF_WRITE)
                    ret = allocation_update_read( allocation, source );
               else {
                    D_UNIMPLEMENTED();
                    ret = DFB_UNSUPPORTED;
               }
          }

          if (ret) {
               D_DERROR( ret, "Core/SurfaceBuffer: Updating allocation failed!\n" );
               return ret;
          }

          Done();

          return DFB_OK;
     }

private:
     CoreSurfaceAllocation *allocation;
     CoreSurfaceAllocation *source;
     CoreSurfaceBuffer     *buffer;
};



extern "C" {

DFBResult
dfb_surface_allocation_update( CoreSurfaceAllocation  *allocation,
                               CoreSurfaceAccessFlags  access )
{
     DFBResult              ret;
     int                    i;
     CoreSurfaceAllocation *alloc;
     CoreSurfaceBuffer     *buffer;

     D_DEBUG_AT( Core_SurfAllocation, "%s( %p )\n", __FUNCTION__, (void *)allocation );

     D_MAGIC_ASSERT( allocation, CoreSurfaceAllocation );
     D_FLAGS_ASSERT( access, CSAF_ALL );

     buffer = allocation->buffer;
     D_MAGIC_ASSERT( buffer, CoreSurfaceBuffer );

     D_MAGIC_ASSERT( buffer->surface, CoreSurface );
     FUSION_SKIRMISH_ASSERT( &buffer->surface->lock );

     if (direct_serial_update( &allocation->serial, &buffer->serial ) && buffer->written) {
          CoreSurfaceAllocation *source = buffer->written;

          D_ASSUME( allocation != source );

          D_MAGIC_ASSERT( source, CoreSurfaceAllocation );
          D_ASSERT( source->buffer == allocation->buffer );

          if (dfb_config->task_manager) {
               DirectFB::TransferTask::Generate( allocation, source );
          }
          else {
               D_DEBUG_AT( Core_SurfAllocation, "  -> updating allocation %p from %p...\n", allocation, source );

               ret = dfb_surface_pool_bridges_transfer( buffer, source, allocation, NULL, 0 );
               if (ret) {
                    if ((source->access[CSAID_CPU] & CSAF_READ) && (allocation->access[CSAID_CPU] & CSAF_WRITE))
                         ret = allocation_update_copy( allocation, source );
                    else if (source->access[CSAID_CPU] & CSAF_READ)
                         ret = allocation_update_write( allocation, source );
                    else if (allocation->access[CSAID_CPU] & CSAF_WRITE)
                         ret = allocation_update_read( allocation, source );
                    else {
                         D_WARN( "[%s] -> [%s]", source->pool->desc.name, allocation->pool->desc.name );
                         D_UNIMPLEMENTED();
                         ret = DFB_UNSUPPORTED;
                    }
               }

               if (ret) {
                    D_DERROR( ret, "Core/SurfaceBuffer: Updating allocation failed!\n" );
                    return ret;
               }
          }
     }

     if (access & CSAF_WRITE) {
          D_DEBUG_AT( Core_SurfAllocation, "  -> increasing serial...\n" );

          direct_serial_increase( &buffer->serial );

          direct_serial_copy( &allocation->serial, &buffer->serial );

          buffer->written = allocation;
          buffer->read    = NULL;

          /* Zap volatile allocations (freed when no longer up to date). */
          fusion_vector_foreach (alloc, i, buffer->allocs) {
               D_MAGIC_ASSERT( alloc, CoreSurfaceAllocation );

               if (alloc != allocation && (alloc->flags & CSALF_VOLATILE)) {
                    dfb_surface_allocation_decouple( alloc );
                    i--;
               }
          }
     }
     else
          buffer->read = allocation;

     /* Zap all other allocations? */
     if (dfb_config->thrifty_surface_buffers) {
          buffer->written = buffer->read = allocation;

          fusion_vector_foreach (alloc, i, buffer->allocs) {
               D_MAGIC_ASSERT( alloc, CoreSurfaceAllocation );

               /* Don't zap preallocated which would not really free up memory, but just loose the handle. */
               if (alloc != allocation && !(alloc->flags & (CSALF_PREALLOCATED | CSALF_MUCKOUT))) {
                    dfb_surface_allocation_decouple( alloc );
                    i--;
               }
          }
     }

     return DFB_OK;
}

DFBResult
dfb_surface_allocation_dump( CoreSurfaceAllocation *allocation,
                             const char            *directory,
                             const char            *prefix,
                             bool                   raw )
{
     DFBResult        ret = DFB_OK;
     CoreSurfacePool *pool;

     D_MAGIC_ASSERT( allocation, CoreSurfaceAllocation );

     pool = allocation->pool;
     D_MAGIC_ASSERT( pool, CoreSurfacePool );

     D_ASSERT( directory != NULL );

     D_DEBUG_AT( Core_SurfAllocation, "%s( %p, '%s', '%s' )\n", __FUNCTION__, (void *)allocation, directory, prefix );

     if (D_FLAGS_IS_SET( pool->desc.caps, CSPCAPS_READ )) {
          int   pitch;
          int   size;
          void *buf;

          dfb_surface_calc_buffer_size( allocation->surface, 4, 1, &pitch, &size );

          buf = D_MALLOC( size );
          if (!buf)
               return (DFBResult) D_OOM();

          ret = dfb_surface_pool_read( pool, allocation, buf, pitch, NULL );
          if (ret == DFB_OK)
               ret = dfb_surface_buffer_dump_type_locked2( allocation->buffer, directory, prefix, raw, buf, pitch );

          D_FREE( buf );
     }
     else {
          CoreSurfaceBufferLock lock;

          dfb_surface_buffer_lock_init( &lock, CSAID_CPU, CSAF_READ );

          /* Lock the surface buffer, get the data pointer and pitch. */
          ret = dfb_surface_pool_lock( pool, allocation, &lock );
          if (ret) {
               dfb_surface_buffer_lock_deinit( &lock );
               return ret;
          }

          ret = dfb_surface_buffer_dump_type_locked( allocation->buffer, directory, prefix, raw, &lock );

          /* Unlock the surface buffer. */
          dfb_surface_pool_unlock( allocation->pool, allocation, &lock );

          dfb_surface_buffer_lock_deinit( &lock );
     }

     return ret;
}


}


}

