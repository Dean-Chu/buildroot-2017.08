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
#include <string.h>
#include <unistd.h>

#include <pthread.h>

#include <direct/direct.h>
#include <direct/build.h>
#include <direct/list.h>
#include <direct/mem.h>
#include <direct/memcpy.h>
#include <direct/messages.h>
#include <direct/signals.h>
#include <direct/thread.h>
#include <direct/util.h>

#include <fusion/arena.h>
#include <fusion/build.h>
#include <fusion/conf.h>
#include <fusion/fusion.h>
#include <fusion/shmalloc.h>
#include <fusion/object.h>

#include <fusionsound_limits.h>

#include <core/fs_types.h>
#include <core/core_sound.h>
#include <core/playback.h>
#include <core/sound_buffer.h>
#include <core/sound_device.h>

#include <misc/sound_conf.h>


typedef enum {
     CSCID_GET_VOLUME,
     CSCID_SET_VOLUME,
     CSCID_SUSPEND,
     CSCID_RESUME,
} CoreSoundCommandID;

typedef struct {
     DirectLink           link;

     CorePlayback        *playback;
} CorePlaylistEntry;

struct __FS_CoreSoundShared {
     FusionObjectPool      *buffer_pool;
     FusionObjectPool      *playback_pool;

     FusionSHMPoolShared   *shmpool;

     struct {
          DirectLink       *entries;
          FusionSkirmish    lock;
     } playlist;

     FSDeviceDescription    description;
     
     CoreSoundDeviceConfig  config;

     int                    output_delay;      /* output delay in ms */
     
     __fsf                  soft_volume;       /* software volume level */
     
     FusionCall             call;              /* master calls */
     float                  call_arg;          /* call argument */
     FusionSkirmish         call_lock;

     __fsf                  master_feedback_left;
     __fsf                  master_feedback_right;
};

struct __FS_CoreSound {
     int                   refs;

     FusionID              fusion_id;

     FusionWorld          *world;
     FusionArena          *arena;

     CoreSoundShared      *shared;

     CoreSoundDevice      *device;
     DirectThread         *sound_thread;
     
     void                 *mixing_buffer;

     DirectSignalHandler  *signal_handler;
     
     DirectCleanupHandler *cleanup_handler;
     
     float                 volume;
     
     bool                  master;
     
     bool                  suspended;
     
     bool                  detached;

     bool                  shutdown;
};

/******************************************************************************/

static void fs_core_deinit_check( void *ctx );

static DirectSignalHandlerResult fs_core_signal_handler( int   num,
                                                         void *addr,
                                                         void *ctx );

/******************************************************************************/

static int fs_core_arena_initialize( FusionArena *arena,
                                     void        *ctx );
static int fs_core_arena_join      ( FusionArena *arena,
                                     void        *ctx );
static int fs_core_arena_leave     ( FusionArena *arena,
                                     void        *ctx,
                                     bool         emergency);
static int fs_core_arena_shutdown  ( FusionArena *arena,
                                     void        *ctx,
                                     bool         emergency);

/******************************************************************************/

static DirectResult fs_core_detach( CoreSound *core );

/******************************************************************************/

static CoreSound       *core_sound      = NULL;
static pthread_mutex_t  core_sound_lock = PTHREAD_MUTEX_INITIALIZER;

DirectResult
fs_core_create( CoreSound **ret_core )
{
     int        ret;
     CoreSound *core;

     D_ASSERT( ret_core != NULL );

     D_DEBUG( "FusionSound/Core: %s...\n", __FUNCTION__ );

     /* Lock the core singleton mutex. */
     pthread_mutex_lock( &core_sound_lock );

     /* Core already created? */
     if (core_sound) {
          /* Increase its references. */
          core_sound->refs++;

          /* Return the core. */
          *ret_core = core_sound;

          /* Unlock the core singleton mutex. */
          pthread_mutex_unlock( &core_sound_lock );

          return DR_OK;
     }

     /* Allocate local core structure. */
     core = D_CALLOC( 1, sizeof(CoreSound) );
     if (!core) {
          pthread_mutex_unlock( &core_sound_lock );
          return DR_NOLOCALMEMORY;
     }

     bool old_secure = fusion_config->secure_fusion;
     fusion_config->secure_fusion = false;

     ret = fusion_enter( fs_config->session, FUSIONSOUND_CORE_ABI, FER_ANY, &core->world );
     if (ret) {
          D_FREE( core );
          pthread_mutex_unlock( &core_sound_lock );
          fusion_config->secure_fusion = old_secure;

          return ret;
     }

     core->fusion_id = fusion_id( core->world );
     
     core->volume = 1.0f;
     
     fs_config->session = fusion_world_index( core->world );

#if FUSION_BUILD_MULTI
     D_DEBUG( "FusionSound/Core: world %d, fusion id %ld\n",
              fs_config->session, core->fusion_id );
#endif

     /* Initialize the references. */
     core->refs = 1;

     direct_signal_handler_add( -1, fs_core_signal_handler, core, &core->signal_handler );

     /* Enter the FusionSound core arena. */
     if (fusion_arena_enter( core->world, "FusionSound/Core",
                             fs_core_arena_initialize, fs_core_arena_join,
                             core, &core->arena, &ret ) || ret)
     {
          direct_signal_handler_remove( core->signal_handler );
          fusion_exit( core->world, false );
          D_FREE( core );
          pthread_mutex_unlock( &core_sound_lock );

          fusion_config->secure_fusion = old_secure;

          return ret ? ret : DR_FUSION;
     }

     fusion_config->secure_fusion = old_secure;

     if (fs_config->deinit_check)
          direct_cleanup_handler_add( fs_core_deinit_check, core, &core->cleanup_handler );

     /* Return the core and store the singleton. */
     *ret_core = core_sound = core;

     /* Unlock the core singleton mutex. */
     pthread_mutex_unlock( &core_sound_lock );

     return DR_OK;
}

DirectResult
fs_core_destroy( CoreSound *core, bool emergency )
{
     D_ASSERT( core != NULL );
     D_ASSERT( core == core_sound );

     D_DEBUG( "FusionSound/Core: %s...\n", __FUNCTION__ );

     /* Lock the core singleton mutex. */
     pthread_mutex_lock( &core_sound_lock );

     /* Decrement and check references. */
     if (--core->refs) {
          /* Unlock the core singleton mutex. */
          pthread_mutex_unlock( &core_sound_lock );

          return DR_OK;
     }

     direct_signal_handler_remove( core->signal_handler );
     
     if (core->cleanup_handler)
          direct_cleanup_handler_remove( core->cleanup_handler ); 
     
     /* Exit the FusionSound core arena. */
     if (fusion_arena_exit( core->arena, fs_core_arena_shutdown,
                            core->master ? NULL : fs_core_arena_leave,
                            core, emergency, NULL ) == DR_BUSY)
     {
          if (core->master) {
               if (emergency) {
                    fusion_kill( core->world, 0, SIGKILL, 1000 );
               }
               else if (fs_config->wait) {
                    fs_core_detach( core );
               }
               else {
                    fusion_kill( core->world, 0, SIGTERM, 5000 );
                    fusion_kill( core->world, 0, SIGKILL, 2000 );
               }
          }
          
          while (fusion_arena_exit( core->arena, fs_core_arena_shutdown,
                                    core->master ? NULL : fs_core_arena_leave,
                                    core, emergency, NULL ) == DR_BUSY)
          {
               D_ONCE( "waiting for FusionSound slaves to terminate" );
               usleep( 100000 );
          }
     }

     fusion_exit( core->world, emergency );

     /* Deallocate local core structure. */
     D_FREE( core );

     /* Clear the singleton. */
     core_sound = NULL;
     
     /* Quit if this is a forked master. */
     if (core->detached)
          _exit( 0 );

     /* Unlock the core singleton mutex. */
     pthread_mutex_unlock( &core_sound_lock );

     return DR_OK;
}

CoreSoundBuffer *
fs_core_create_buffer( CoreSound *core )
{
     D_ASSERT( core != NULL );
     D_ASSERT( core->shared != NULL );
     D_ASSERT( core->shared->buffer_pool != NULL );

     /* Create a new object in the buffer pool. */
     return (CoreSoundBuffer*) fusion_object_create( core->shared->buffer_pool, core->world, 1 );
}

CorePlayback *
fs_core_create_playback( CoreSound *core )
{
     D_ASSERT( core != NULL );
     D_ASSERT( core->shared != NULL );
     D_ASSERT( core->shared->playback_pool != NULL );

     /* Create a new object in the playback pool. */
     return (CorePlayback*) fusion_object_create( core->shared->playback_pool, core->world, 1 );
}

DirectResult
fs_core_enum_buffers( CoreSound            *core,
                      FusionObjectCallback  callback,
                      void                 *ctx )
{
     D_ASSERT( core != NULL );
     D_ASSERT( core->shared != NULL );

     /* Enumerate objects in the buffer pool. */
     return fusion_object_pool_enum( core->shared->buffer_pool, callback, ctx );
}

DirectResult
fs_core_enum_playbacks( CoreSound            *core,
                        FusionObjectCallback  callback,
                        void                 *ctx )
{
     D_ASSERT( core != NULL );
     D_ASSERT( core->shared != NULL );

     /* Enumerate objects in the playback pool. */
     return fusion_object_pool_enum( core->shared->playback_pool, callback, ctx );
}

DirectResult
fs_core_playlist_lock( CoreSound *core )
{
     D_ASSERT( core != NULL );
     D_ASSERT( core->shared != NULL );

     return fusion_skirmish_prevail( &core->shared->playlist.lock );
}

DirectResult
fs_core_playlist_unlock( CoreSound *core )
{
     D_ASSERT( core != NULL );
     D_ASSERT( core->shared != NULL );

     return fusion_skirmish_dismiss( &core->shared->playlist.lock );
}

DirectResult
fs_core_add_playback( CoreSound    *core,
                      CorePlayback *playback )
{
     CorePlaylistEntry *entry;
     CoreSoundShared   *shared;

     D_ASSERT( core != NULL );
     D_ASSERT( core->shared != NULL );
     D_ASSERT( playback != NULL );

     D_DEBUG( "FusionSound/Core: %s (%p)\n", __FUNCTION__, playback );

     shared = core->shared;

     /* Allocate playlist entry. */
     entry = SHCALLOC( shared->shmpool, 1, sizeof(CorePlaylistEntry) );
     if (!entry)
          return DR_NOLOCALMEMORY;

     /* Link playback to playlist entry. */
     if (fs_playback_link( &entry->playback, playback )) {
          SHFREE( shared->shmpool, entry );
          return DR_FUSION;
     }
     
     /* Add it to the playback list. */     
     direct_list_prepend( &shared->playlist.entries, &entry->link );
     
     /* Notify new playlist entry to the sound thread. */
     fusion_skirmish_notify( &shared->playlist.lock );

     return DR_OK;
}

DirectResult
fs_core_remove_playback( CoreSound    *core,
                         CorePlayback *playback )
{
     DirectLink      *l, *next;
     CoreSoundShared *shared;

     D_ASSERT( core != NULL );
     D_ASSERT( core->shared != NULL );
     D_ASSERT( playback != NULL );

     D_DEBUG( "FusionSound/Core: %s (%p)\n", __FUNCTION__, playback );

     shared = core->shared;

     /* Lookup playback in the list. */     
     direct_list_foreach_safe (l, next, shared->playlist.entries) {
          CorePlaylistEntry *entry = (CorePlaylistEntry*) l;

          /* Remove any matches. */
          if (entry->playback == playback) {
               direct_list_remove( &shared->playlist.entries, l );

               fs_playback_unlink( &entry->playback );

               SHFREE( shared->shmpool, entry );
          }
     }

     return DR_OK;
}

int
fs_core_output_delay( CoreSound *core )
{
     D_ASSERT( core != NULL );
     D_ASSERT( core->shared != NULL );

     /* Return the delay produced by the device buffer. */
     return core->shared->output_delay;
}

FusionWorld *
fs_core_world( CoreSound *core )
{
     D_ASSERT( core != NULL );

     return core->world;
}

FusionSHMPoolShared *
fs_core_shmpool( CoreSound *core )
{
     D_ASSERT( core != NULL );
     D_ASSERT( core->shared != NULL );

     return core->shared->shmpool;
}

FSDeviceDescription *
fs_core_device_description( CoreSound *core )
{
     D_ASSERT( core != NULL );
     D_ASSERT( core->shared != NULL );
     
     return &(core->shared->description);
}

CoreSoundDeviceConfig *
fs_core_device_config( CoreSound *core )
{
     D_ASSERT( core != NULL );
     D_ASSERT( core->shared != NULL );
     
     return &core->shared->config;
}

DirectResult
fs_core_get_master_volume( CoreSound *core, float *level )
{
     CoreSoundShared *shared;
     DirectResult     ret;
     int              retval;
     
     D_ASSERT( core != NULL );
     D_ASSERT( core->shared != NULL );
     D_ASSERT( level != NULL );
     
     shared = core->shared;
     
     ret = fusion_skirmish_prevail( &shared->call_lock );
     if (ret)
          return ret;
     
     ret = fusion_call_execute( &core->shared->call, FCEF_NONE,
                                CSCID_GET_VOLUME, &shared->call_arg, (int*)&retval );
     if (!ret)
          ret = retval;
          
     *level = shared->call_arg;
     
     fusion_skirmish_dismiss( &shared->call_lock );
          
     return ret;
}

DirectResult
fs_core_set_master_volume( CoreSound *core, float level )
{
     CoreSoundShared *shared;
     DirectResult     ret;
     int              retval;
     
     D_ASSERT( core != NULL );
     D_ASSERT( core->shared != NULL );
     
     shared = core->shared;
     
     ret = fusion_skirmish_prevail( &shared->call_lock );
     if (ret)
          return ret;
     
     shared->call_arg = level;
     
     ret = fusion_call_execute( &core->shared->call, FCEF_NONE,
                                CSCID_SET_VOLUME, &shared->call_arg, (int*)&retval );
     if (!ret)
          ret = retval;
          
     fusion_skirmish_dismiss( &shared->call_lock );
          
     return ret;
}

DirectResult
fs_core_get_local_volume( CoreSound *core, float *level )
{
     D_ASSERT( core != NULL );
     D_ASSERT( level != NULL );
     
     pthread_mutex_lock( &core_sound_lock );
     
     *level = core->volume;
     
     pthread_mutex_unlock( &core_sound_lock );
     
     return DR_OK;
}

static bool
volume_callback( FusionObjectPool *pool, FusionObject *object, void *ctx )
{
     CoreSound *core = ctx;
     
#if FUSION_BUILD_MULTI
     if (object->ref.multi.creator != core->fusion_id)
          return true;
#endif
     
     fs_playback_set_local_volume( (CorePlayback*)object, core->volume );
     
     return true;
}     

DirectResult
fs_core_set_local_volume( CoreSound *core, float level )
{
     D_ASSERT( core != NULL );
     D_ASSERT( core->shared != NULL );
     
     pthread_mutex_lock( &core_sound_lock );
     
     core->volume = level;
     
     fusion_object_pool_enum( core->shared->playback_pool, volume_callback, core );
     
     pthread_mutex_unlock( &core_sound_lock );
     
     return DR_OK;
}

DirectResult
fs_core_get_master_feedback( CoreSound *core,
                             float     *ret_left,
                             float     *ret_right )
{
     CoreSoundShared *shared;

     D_ASSERT( core != NULL );

     shared = core->shared;
     D_ASSERT( shared != NULL );

     if (ret_left)
          *ret_left = fsf_to_float( shared->master_feedback_left );

     if (ret_right)
          *ret_right = fsf_to_float( shared->master_feedback_right );

     return DR_OK;
}

DirectResult
fs_core_suspend( CoreSound *core )
{
     DirectResult ret;
     int       retval;
     
     D_ASSERT( core != NULL );
     D_ASSERT( core->shared != NULL );
     
     ret =  fusion_call_execute( &core->shared->call,
                                 FCEF_NONE, CSCID_SUSPEND, NULL, (int*)&retval );
     if (!ret)
          ret = retval;
          
     return ret;
}

DirectResult
fs_core_resume( CoreSound *core )
{
     DirectResult ret;
     int       retval;
     
     D_ASSERT( core != NULL );
     D_ASSERT( core->shared != NULL );
     
     ret =  fusion_call_execute( &core->shared->call,
                                 FCEF_NONE, CSCID_RESUME, NULL, (int*)&retval );
     if (!ret)
          ret = retval;
          
     return ret;
}

/******************************************************************************/

static void
fs_core_deinit_check( void *ctx )
{
     CoreSound *core = (CoreSound*) ctx;
     
     D_ASSERT( core != NULL );
     D_ASSERT( core == core_sound );
     
     D_WARN( "Application exited without deinitialization of FusionSound!" );

     fs_core_destroy( core, true );
}

static DirectSignalHandlerResult
fs_core_signal_handler( int num, void *addr, void *ctx )
{
     CoreSound *core = (CoreSound*) ctx;

     D_ASSERT( core != NULL );
     D_ASSERT( core == core_sound );

     fs_core_destroy( core, true );

     return DR_OK;
}

/******************************************************************************/

/*
 * Mixing buffer uses the following channels mapping:
 *   1. (L)eft
 *   2. (R)ight
 *   3. (C)enter
 *   4. (R)ear (L)eft
 *   5. (R)ear (R)ight
 *   6. (S)ubwoofer (aka LFE)
 * This differs from Surround 5.1 because of Center channel's position.
 *
 * According to AC-3 specs, downmixing from 5.1 to stereo is achieved
 * as following:
 *   L = L + clev * C + slev * RL
 *   R = R + clev * C + slev * RR
 * where clev and slev are -3dB (0.707) by default.
 * 
 * In FusionSound we premultiply center and rear channels by the 
 * respective levels (set by the user) during mixing and then
 * sum channels when converting to output format.
 */

#if FS_MAX_CHANNELS >= 5
# define FS_MIX_OUTPUT_LOOP( BODY ) {                               \
     int n;                                                         \
     switch (mode) {                                                \
          case FSCM_MONO:                                           \
               for (n = count; n; n--) {                            \
                    register __fsf s;                               \
                    const int      c = 0;                           \
                    s = src[c] + src[1] + src[2] +                  \
                        src[2] + src[3] + src[4];                   \
                    s = fsf_shr( s, 1 );                            \
                    BODY                                            \
                    src += FS_MAX_CHANNELS;                         \
               }                                                    \
               break;                                               \
          case FSCM_STEREO:                                         \
               for (n = count; n; n--) {                            \
                    register __fsf s;                               \
                    int            c;                               \
                    c = 0;                                          \
                    s = src[c] + src[2] + src[3];                   \
                    BODY                                            \
                    c = 1;                                          \
                    s = src[c] + src[2] + src[4];                   \
                    BODY                                            \
                    src += FS_MAX_CHANNELS;                         \
               }                                                    \
               break;                                               \
          case FSCM_STEREO21:                                       \
          case FSCM_STEREO30:                                       \
          case FSCM_STEREO31:                                       \
               for (n = count; n; n--) {                            \
                    register __fsf s;                               \
                    int            c;                               \
                    if (FS_MODE_HAS_CENTER(mode)) {                 \
                         c = 0;                                     \
                         s = src[c] + src[3];                       \
                         BODY                                       \
                         c = 2;                                     \
                         s = src[c];                                \
                         BODY                                       \
                         c = 1;                                     \
                         s = src[c] + src[4];                       \
                         BODY                                       \
                    } else {                                        \
                         c = 0;                                     \
                         s = src[c] + src[2] + src[3];              \
                         BODY                                       \
                         c = 1;                                     \
                         s = src[c] + src[2] + src[4];              \
                         BODY                                       \
                    }                                               \
                    if (FS_MODE_HAS_LFE(mode)) {                    \
                         c = 5;                                     \
                         s = src[c];                                \
                         BODY                                       \
                    }                                               \
                    src += FS_MAX_CHANNELS;                         \
               }                                                    \
               break;                                               \
          case FSCM_SURROUND30:                                     \
          case FSCM_SURROUND31:                                     \
          case FSCM_SURROUND40_2F2R:                                \
          case FSCM_SURROUND41_2F2R:                                \
          case FSCM_SURROUND40_3F1R:                                \
          case FSCM_SURROUND41_3F1R:                                \
          case FSCM_SURROUND50:                                     \
               for (n = count; n; n--) {                            \
                    register __fsf s;                               \
                    int            c;                               \
                    if (FS_MODE_HAS_CENTER(mode)) {                 \
                         c = 0;                                     \
                         s = src[c];                                \
                         BODY                                       \
                         c = 2;                                     \
                         s = src[c];                                \
                         BODY                                       \
                         c = 1;                                     \
                         s = src[c];                                \
                         BODY                                       \
                    } else {                                        \
                         c = 0;                                     \
                         s = src[c] + src[2];                       \
                         BODY                                       \
                         c = 1;                                     \
                         s = src[c] + src[2];                       \
                         BODY                                       \
                    }                                               \
                    if (FS_MODE_NUM_REARS(mode) == 1) {             \
                         c = 3;                                     \
                         s = fsf_shr( src[c] + src[c+1], 1 );       \
                         BODY                                       \
                    } else {                                        \
                         c = 3;                                     \
                         s = src[c];                                \
                         BODY                                       \
                         c = 4;                                     \
                         s = src[c];                                \
                         BODY                                       \
                    }                                               \
                    if (FS_MODE_HAS_LFE(mode)) {                    \
                         c = 5;                                     \
                         s = src[c];                                \
                         BODY                                       \
                    }                                               \
                    src += FS_MAX_CHANNELS;                         \
               }                                                    \
               break;                                               \
          case FSCM_SURROUND51:                                     \
               for (n = count; n; n--) {                            \
                    register __fsf s;                               \
                    int            c;                               \
                    c = 0;                                          \
                    s = src[c];                                     \
                    BODY                                            \
                    c = 2;                                          \
                    s = src[c];                                     \
                    BODY                                            \
                    c = 1;                                          \
                    s = src[c];                                     \
                    BODY                                            \
                    c = 3;                                          \
                    s = src[c];                                     \
                    BODY                                            \
                    c = 4;                                          \
                    s = src[c];                                     \
                    BODY                                            \
                    c = 5;                                          \
                    s = src[c];                                     \
                    BODY                                            \
                    src += FS_MAX_CHANNELS;                         \
               }                                                    \
               break;                                               \
          default:                                                  \
               D_BUG( "unexpected channel mode" );                  \
               break;                                               \
     }                                                              \
}
#else /* FS_MAX_CHANNELS == 2 */
# define FS_MIX_OUTPUT_LOOP( BODY ) {                               \
     int n;                                                         \
     if (mode == FSCM_MONO) {                                       \
          for (n = count; n; n--) {                                 \
               register __fsf s;                                    \
               const int      c = 0;                                \
               s = fsf_shr( src[c] + src[c+1], 1 );                 \
               BODY                                                 \
               src += FS_MAX_CHANNELS;                              \
          }                                                         \
     }                                                              \
     else {                                                         \
          for (n = count; n; n--) {                                 \
               register __fsf s;                                    \
               int            c;                                    \
               c = 0;                                               \
               s = src[c];                                          \
               BODY                                                 \
               c = 1;                                               \
               s = src[c];                                          \
               BODY                                                 \
               src += FS_MAX_CHANNELS;                              \
          }                                                         \
     }                                                              \
}
#endif /* FS_MAX_CHANNELS */      


static void *
sound_thread( DirectThread *thread, void *arg )
{
     CoreSound          *core    = arg;
     CoreSoundShared    *shared  = core->shared;
     
     __fsf              *mixing  = core->mixing_buffer;
     int                 frames  = shared->config.buffersize;
     FSChannelMode       mode    = shared->config.mode;
     
     fsf_dither_profiles(dither, FS_MAX_CHANNELS);
     
     while (!core->shutdown) {
          __fsf      *src    = mixing;
          int         length = 0;
          int         delay;
          int         i;
          __fsf       l_min = FSF_MAX, l_max = FSF_MIN;
          __fsf       r_min = FSF_MAX, r_max = FSF_MIN;
          DirectLink *next, *l;
          
          direct_thread_testcancel( thread );

          fs_device_get_output_delay( core->device, &delay );
          shared->output_delay = delay * 1000 / shared->config.rate;                   

          /* Clear mixing buffer. */
          memset( mixing, 0, frames * FS_MAX_CHANNELS * sizeof(__fsf) );

          /* Iterate through running playbacks, mixing them together. */
          fusion_skirmish_prevail( &shared->playlist.lock );
          
          if (!shared->playlist.entries) {
               shared->master_feedback_left  = 0;
               shared->master_feedback_right = 0;

               if (fusion_skirmish_wait( &shared->playlist.lock, delay ? 1 : 0 )) {
                    fusion_skirmish_dismiss( &shared->playlist.lock );
                    continue;
               }
          }

          direct_list_foreach_safe (l, next, shared->playlist.entries) {
               DirectResult       ret;
               CorePlaylistEntry *entry    = (CorePlaylistEntry *) l;
               CorePlayback      *playback = entry->playback;
               int                num;

               ret = fs_playback_mixto( playback, mixing, 
                                        shared->config.rate, shared->config.mode,
                                        frames, shared->soft_volume, &num );
               if (ret) {
                    direct_list_remove( &shared->playlist.entries, l );

                    fs_playback_unlink( &entry->playback );

                    SHFREE( shared->shmpool, entry );
               }

               if (num > length)
                    length = num;
          }

          fusion_skirmish_dismiss( &shared->playlist.lock );

          if (FS_CHANNELS_FOR_MODE(shared->config.mode) == 1) {
               for (i=0; i<length; i++) {
                    if (mixing[i] < l_min)
                         l_min = mixing[i];

                    if (mixing[i] > l_max)
                         l_max = mixing[i];
               }

               r_min = l_min;
               r_max = l_max;
          }
          else {
               for (i=0; i<length*FS_CHANNELS_FOR_MODE(shared->config.mode); i+=FS_CHANNELS_FOR_MODE(shared->config.mode)) {
                    if (mixing[i] < l_min)
                         l_min = mixing[i];

                    if (mixing[i] > l_max)
                         l_max = mixing[i];

                    if (mixing[i+1] < r_min)
                         r_min = mixing[i];

                    if (mixing[i+1] > r_max)
                         r_max = mixing[i];
               }
          }

          shared->master_feedback_left  = l_max - l_min;
          shared->master_feedback_right = r_max - r_min;

          while (length) {
               u8           *dst;
               unsigned int  avail;
               unsigned int  count;
               
               /* Get access to the output buffer. */
               if (fs_device_get_buffer( core->device, &dst, &avail ))
                    break;
                    
               count = MIN( avail, length );

               /* Convert mixing buffer to output format, clipping each sample. */
               switch (shared->config.format) {
                    case FSSF_U8:
                         FS_MIX_OUTPUT_LOOP(
                              if (fs_config->dither)
                                   s = fsf_dither( s, 8, dither[c] );
                              s = fsf_clip( s );                              
                              *dst++ = fsf_to_u8( s );
                         )
                         break;
                    case FSSF_S16:
                         FS_MIX_OUTPUT_LOOP(
                              if (fs_config->dither)
                                   s = fsf_dither( s, 16, dither[c] );
                              s = fsf_clip( s );                              
                              *((u16*)dst) = fsf_to_s16( s );
                              dst += 2;
                         )
                         break;
                    case FSSF_S24:
#ifdef WORDS_BIGENDIAN
                         FS_MIX_OUTPUT_LOOP({
                              int d;
                              s = fsf_clip( s );
                              d = fsf_to_s24( s );
                              dst[0] = d >> 16;
                              dst[1] = d >>  8;
                              dst[2] = d      ;
                              dst += 3;
                         })
#else
                         FS_MIX_OUTPUT_LOOP({
                              int d;
                              s = fsf_clip( s );
                              d = fsf_to_s24( s );
                              dst[0] = d      ;
                              dst[1] = d >>  8;
                              dst[2] = d >> 16;
                              dst += 3;
                         })           
#endif
                         break;
                    case FSSF_S32:
                         FS_MIX_OUTPUT_LOOP(
                              s = fsf_clip( s );
                              *((u32*)dst) = fsf_to_s32( s );
                              dst += 4;
                         )    
                         break;
                    case FSSF_FLOAT:
                         FS_MIX_OUTPUT_LOOP(
                              s = fsf_clip( s );
                              *((float*)dst) = fsf_to_float( s );
                              dst += 4;
                         ) 
                         break;
                    default:
                         D_BUG( "unexpected sample format" );
                         break;
               }
               
               /* Commit output buffer. */
               fs_device_commit_buffer( core->device, count );
               
               /* Update parameters. */
               length -= count;
          }
     }

     return NULL;
}

/******************************************************************************/

static FusionCallHandlerResult
core_call_handler( int caller, int call_arg, void *call_ptr,
                   void *ctx, unsigned int serial, int *retval )
{
     CoreSound       *core   = ctx;
     CoreSoundShared *shared = core->shared;
     float            volume;
       
     switch (call_arg) {               
          case CSCID_GET_VOLUME:
               if (core->suspended) {
                    *retval = DR_SUSPENDED;
               }
               else {
                    if (!(fs_device_get_capabilities( core->device ) & DCF_VOLUME) ||
                        fs_device_get_volume( core->device, &volume ) != DR_OK)
                         volume = fsf_to_float( shared->soft_volume ); 

                    *((float*)call_ptr) = volume;
                    
                    *retval = DR_OK;
               }
               break;
               
          case CSCID_SET_VOLUME:
               if (core->suspended) {
                    *retval = DR_SUSPENDED;
               }
               else {
                    volume = *((float*)call_ptr);
                    if (!(fs_device_get_capabilities( core->device ) & DCF_VOLUME) ||
                        fs_device_set_volume( core->device, volume ) != DR_OK)
                         shared->soft_volume = fsf_from_float( volume );
                    else
                         shared->soft_volume = FSF_ONE;
                         
                    *retval = DR_OK;
               }
               break;
               
          case CSCID_SUSPEND:
               if (core->suspended) {
                    *retval = DR_BUSY;
               }
               else {
                    DirectResult ret;
                    
                    direct_thread_cancel( core->sound_thread );
                    direct_thread_join( core->sound_thread );
                    direct_thread_destroy( core->sound_thread );
                    core->sound_thread = NULL;
                    
                    ret = fs_device_suspend( core->device );
                    if (ret) {
                         core->sound_thread = direct_thread_create( DTT_OUTPUT, 
                                                                    sound_thread, core, "Sound Mixer" );
                         *retval = ret;
                         break;
                    }
                    
                    core->suspended = true;
                    
                    *retval = DR_OK;
               }
               break;
               
          case CSCID_RESUME:
               if (!core->suspended) {
                    *retval = DR_BUSY;
               }
               else {
                    DirectResult ret;
                    
                    ret = fs_device_resume( core->device );
                    if (ret) {
                         *retval = ret;
                         break;
                    }
                    
                    core->sound_thread = direct_thread_create( DTT_OUTPUT, sound_thread, core, "Sound Mixer" );
                    
                    core->suspended = false;
                    
                    *retval = DR_OK;
               }
               break;
               
          default:
               D_BUG( "unexpected call" );
               break;
     }
     
     return FCHR_RETURN;
}

static DirectResult
fs_core_initialize( CoreSound *core )
{
     CoreSoundShared *shared = core->shared;
     DirectResult     ret;
     
     /* Set default device configuration. */  
     shared->config.mode       = fs_config->channelmode;
     shared->config.format     = fs_config->sampleformat;
     shared->config.rate       = fs_config->samplerate;
     shared->config.buffersize = fs_config->samplerate * fs_config->buffertime / 1000;
     /* No more than 65535 frames. */
     if (shared->config.buffersize > 65535)
          shared->config.buffersize = 65535;
     
     /* Open output device. */
     ret = fs_device_initialize( core, &shared->config, &core->device );
     if (ret)
          return ret;
          
     /* Get device description. */
     fs_device_get_description( core->device, &shared->description );
     
     /* Initialize playlist lock. */
     fusion_skirmish_init( &shared->playlist.lock, "FusionSound Playlist", core->world );

     /* Create a pool for sound buffer objects. */
     shared->buffer_pool = fs_buffer_pool_create( core->world );

     /* Create a pool for playback objects. */
     shared->playback_pool = fs_playback_pool_create( core->world );
     
     /* Initialize call. */
     fusion_call_init( &shared->call, core_call_handler, core, core->world );
     
     /* Initializa call lock. */
     fusion_skirmish_init( &shared->call_lock, "FusionSound Call", core->world );
     
     /* Allocate mixing buffer. */
     core->mixing_buffer = D_MALLOC( shared->config.buffersize * 
                                     FS_MAX_CHANNELS * sizeof(__fsf) );
     if (!core->mixing_buffer)
          return D_OOM();
          
     /* Initialize software volume level. */
     shared->soft_volume = FSF_ONE;
     
     /* Start sound mixer. */
     core->sound_thread = direct_thread_create( DTT_OUTPUT, sound_thread, core, "Sound Mixer" );

     return DR_OK;
}

static DirectResult
fs_core_join( CoreSound *core )
{
     /* really nothing to be done here, yet ;) */

     return DR_OK;
}

static DirectResult
fs_core_leave( CoreSound *core )
{
     /* really nothing to be done here, yet ;) */

     return DR_OK;
}

static DirectResult
fs_core_shutdown( CoreSound *core, bool local )
{
     DirectLink      *l, *next;
     CoreSoundShared *shared;

     D_ASSERT( core != NULL );
     D_ASSERT( core->shared != NULL );

     shared = core->shared;

     D_ASSERT( shared->buffer_pool != NULL );
     D_ASSERT( shared->playback_pool != NULL );

     /* Stop sound thread. */

     core->shutdown = true;
     if (core->sound_thread) {
          fusion_skirmish_prevail( &core->shared->playlist.lock );
          fusion_skirmish_notify( &core->shared->playlist.lock );
          fusion_skirmish_dismiss( &core->shared->playlist.lock );
          direct_thread_join( core->sound_thread );
          direct_thread_destroy( core->sound_thread );
     }

     if (!local) {
          /* Close output device. */
          fs_device_shutdown( core->device );
          
          /* Clear playlist. */
          fusion_skirmish_prevail( &shared->playlist.lock );

          direct_list_foreach_safe (l, next, shared->playlist.entries) {
               CorePlaylistEntry *entry = (CorePlaylistEntry*) l;

               fs_playback_unlink( &entry->playback );

               SHFREE( shared->shmpool, entry );
          }
          
          /* Destroy call lock. */
          fusion_skirmish_destroy( &shared->call_lock );
          
          /* Destroy call handler. */
          fusion_call_destroy( &shared->call );

          /* Destroy playback object pool. */
          fusion_object_pool_destroy( shared->playback_pool, core->world );

          /* Destroy buffer object pool. */
          fusion_object_pool_destroy( shared->buffer_pool, core->world );

          /* Destroy playlist lock. */
          fusion_skirmish_destroy( &shared->playlist.lock );
     }
     
     /* Release mixing buffer. */
     D_FREE( core->mixing_buffer );

     return DR_OK;
}

static DirectResult
fs_core_detach( CoreSound *core )
{
     FusionForkAction action;
     int              ret;
     
     D_DEBUG( "FusionSound/Core: Detaching...\n" );
     
     D_ASSUME( !core->detached );
     
     /* Detach core from the controlling process. */
     action = fusion_world_get_fork_action( core->world );
     fusion_world_set_fork_action( core->world, FFA_FORK );
     ret = fork();
     fusion_world_set_fork_action( core->world, action );
     
     switch (ret) {
          case -1:
               D_PERROR( "fork()" );
               fusion_kill( core->world, 0, SIGTERM, 5000 );
               fusion_kill( core->world, 0, SIGKILL, 2000 );
               return DR_FAILURE;
          
          case 0:
               D_DEBUG( "FusionSound/Core: ... detached.\n" ); 
               core->detached = true;
               /* Restart sound thread. */
               if (core->sound_thread)
                    core->sound_thread = direct_thread_create( DTT_OUTPUT, sound_thread, core, "Sound Mixer" );
               break;
          
          default:
               core->master = false;
               /* Release local resources. */
               fs_core_shutdown( core, true );
               break;
     }
     
     return DR_OK;
}

/******************************************************************************/

static void
fs_fork_callback( FusionForkAction action, FusionForkState state )
{
     D_DEBUG( "FusionSound/Core: %s( %d, %d )\n", __FUNCTION__, action, state );
     
     if (core_sound)
          fs_device_handle_fork( core_sound->device, action, state );
}

/******************************************************************************/

static int
fs_core_arena_initialize( FusionArena *arena,
                          void        *ctx )
{
     DirectResult         ret;
     CoreSound           *core   = ctx;
     CoreSoundShared     *shared;
     FusionSHMPoolShared *pool;

     D_DEBUG( "FusionSound/Core: Initializing...\n" );
     
     /* Create the shared memory pool first! */
     ret = fusion_shm_pool_create( core->world, "FusionSound Main Pool", 0x1000000,
                                   fusion_config->debugshm, &pool );
     if (ret)
          return ret;

     /* Allocate shared structure in the new pool. */
     shared = SHCALLOC( pool, 1, sizeof(CoreSoundShared) );
     if (!shared) {
          fusion_shm_pool_destroy( core->world, pool );
          return D_OOSHM();
     }

     core->shared = shared;
     core->master = true;

     shared->shmpool = pool;

     /* Initialize. */
     ret = fs_core_initialize( core );
     if (ret) {
          SHFREE( pool, shared );
          fusion_shm_pool_destroy( core->world, pool );
          return ret;
     }

     /* Register shared data. */
     fusion_arena_add_shared_field( arena, "Core/Shared", shared );
     
     /* Register fork() callback. */
     fusion_world_set_fork_callback( core->world, fs_fork_callback );

     fusion_world_activate( core->world );

     return DR_OK;
}

static int
fs_core_arena_shutdown( FusionArena *arena,
                        void        *ctx,
                        bool         emergency)
{
     DirectResult         ret;
     CoreSound           *core = ctx;
     CoreSoundShared     *shared;
     FusionSHMPoolShared *pool;

     shared = core->shared;

     pool = shared->shmpool;

     D_DEBUG( "FusionSound/Core: Shutting down...\n" );

     if (!core->master) {
          D_DEBUG( "FusionSound/Core: Refusing shutdown in slave.\n" );
          return fs_core_leave( core );
     }
     
     /* Unregister fork() callback. */
     fusion_world_set_fork_callback( core->world, NULL );

     /* Shutdown. */
     ret = fs_core_shutdown( core, false );
     if (ret)
          return ret;

     SHFREE( pool, shared );

     fusion_shm_pool_destroy( core->world, pool );

     return DR_OK;
}

static int
fs_core_arena_join( FusionArena *arena,
                    void        *ctx )
{
     DirectResult     ret;
     CoreSound       *core   = ctx;
     CoreSoundShared *shared;

     D_DEBUG( "FusionSound/Core: Joining...\n" );

     /* Get shared data. */
     if (fusion_arena_get_shared_field( arena, "Core/Shared", (void*)&shared ))
          return DR_FUSION;

     core->shared = shared;

     /* Join. */
     ret = fs_core_join( core );
     if (ret)
          return ret;

     return DR_OK;
}

static int
fs_core_arena_leave( FusionArena *arena,
                     void        *ctx,
                     bool         emergency)
{
     DirectResult  ret;
     CoreSound *core = ctx;

     D_DEBUG( "FusionSound/Core: Leaving...\n" );

     /* Leave. */
     ret = fs_core_leave( core );
     if (ret)
          return ret;

     return DR_OK;
}

