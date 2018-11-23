/*
   Copyright (C) 2004-2007 Claudio Ciccani <klan@users.sf.net>

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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include <pthread.h>

#include <directfb.h>

#include <idirectfb.h>

#include <media/idirectfbvideoprovider.h>
#include <media/idirectfbdatabuffer.h>

#include <core/coredefs.h>
#include <core/coretypes.h>

#include <core/layers.h>
#include <core/surface.h>
#include <core/system.h>

#include <display/idirectfbsurface.h>

#include <direct/conf.h>
#include <direct/interface.h>
#include <direct/thread.h>

#include <xine.h>
#include <xine/xineutils.h>
#include <xine/video_out.h>

/* use new speed parameter if available */
#ifdef XINE_PARAM_FINE_SPEED
# undef  XINE_PARAM_SPEED
# undef  XINE_SPEED_NORMAL
# define XINE_PARAM_SPEED  XINE_PARAM_FINE_SPEED
# define XINE_SPEED_NORMAL XINE_FINE_SPEED_NORMAL
#endif


static DFBResult
Probe( IDirectFBVideoProvider_ProbeContext *ctx );

static DFBResult
Construct( IDirectFBVideoProvider *thiz,
           IDirectFBDataBuffer    *buffer );


#include <direct/interface_implementation.h>

DIRECT_INTERFACE_IMPLEMENTATION( IDirectFBVideoProvider, Xine )


/************************** Driver Specific Data ******************************/


typedef void (*DVOutputCallback) ( void                  *cdata,
                                   int                    width,
                                   int                    height,
                                   double                 ratio,
                                   DFBSurfacePixelFormat  format,
                                   DFBRectangle          *dest_rect );

typedef struct {
     IDirectFBSurface *destination;
     IDirectFBSurface *subpicture;

     DVOutputCallback  output_cb;
     void             *output_cdata;

     DVFrameCallback  frame_cb;
     void            *frame_cdata;
} dfb_visual_t;


/**************************** VideoProvider Data ******************************/

typedef struct {
     int                            ref;
     
     pthread_mutex_t                lock;

     char                          *mrl;
     int                            mrl_changed;
     
     char                          *cfg;
     char                          *pipe;

     xine_t                        *xine;
     xine_video_port_t             *vo;
     xine_audio_port_t             *ao;
     xine_post_t                   *post;
     xine_stream_t                 *stream;
     xine_event_queue_t            *queue;
     int                            start_time;
     int                            speed;

     dfb_visual_t                   visual;
    
     DFBSurfacePixelFormat          format; // video format
     int                            width;  // video width
     int                            height; // video height
     int                            length; // duration

     DFBVideoProviderStatus         status;
     DFBVideoProviderPlaybackFlags  flags;
     
     bool                           full_area;
     DFBRectangle                   dest_rect;
   
     int                            mouse_x;
     int                            mouse_y;
     
     IDirectFBDataBuffer           *buffer;
     DirectThread                  *buffer_thread;

     IDirectFBEventBuffer          *events;
} IDirectFBVideoProvider_Xine_data;


/***************************** Private Functions ******************************/

static DFBResult
get_stream_error( IDirectFBVideoProvider_Xine_data *data );

static void
frame_output( void *cdata, int width, int height, double ratio,
              DFBSurfacePixelFormat format, DFBRectangle *dest_rect );

static void
send_videoprovider_event( IDirectFBVideoProvider_Xine_data *data,
                          DFBVideoProviderEventType         type );

static void
event_listener( void *cdata, const xine_event_t *event );

static DFBResult
make_pipe( char **ret_path );

/***************************** DataBuffer Thread ******************************/

void*
BufferThread( DirectThread *self, void *arg )
{
     IDirectFBVideoProvider_Xine_data *data   = arg;
     IDirectFBDataBuffer              *buffer = data->buffer;
     int                               fd;

     fd = open( data->pipe, O_WRONLY );
     if (fd < 0) {
          D_PERROR( "IDirectFBVideoProvider_Xine: "
                    "failed to open fifo '%s'\n", data->pipe );
          return (void*)1;
     }

     while (!direct_thread_is_canceled( self )) {
          DFBResult     ret;
          char          buf[4096];
          unsigned int  len = 0;

          buffer->WaitForDataWithTimeout( buffer, sizeof(buf), 0, 1 );
          ret = buffer->GetData( buffer, sizeof(buf), buf, &len );
          if (ret == DFB_OK && len)
               write( fd, buf, len );

          if (ret == DFB_EOF)
               break;
     }

     close( fd );

     return (void*)0;
}     

/******************************* Public Methods *******************************/

static void
IDirectFBVideoProvider_Xine_Destruct( IDirectFBVideoProvider *thiz )
{
     IDirectFBVideoProvider_Xine_data *data = thiz->priv;

     if (data->xine) {
          if (data->stream) {              
               xine_stop( data->stream );

               xine_close( data->stream );

               if (data->queue)
                    xine_event_dispose_queue( data->queue );
               
               xine_dispose( data->stream );
          }

          if (data->post)
               xine_post_dispose( data->xine, data->post );

          if (data->vo)
               xine_close_video_driver( data->xine, data->vo );

          if (data->ao)
               xine_close_audio_driver( data->xine, data->ao );

          if (data->cfg) {
               xine_config_save( data->xine, data->cfg );
               D_FREE( data->cfg );
          }

          xine_exit( data->xine );
     }

     if (data->buffer_thread) {
          direct_thread_cancel( data->buffer_thread );
          direct_thread_join( data->buffer_thread );
          direct_thread_destroy( data->buffer_thread );
     }
 
     if (data->buffer)
          data->buffer->Release( data->buffer );

     if (data->pipe) {
          unlink( data->pipe );
          D_FREE( data->pipe );
     }

     if (data->events)
          data->events->Release( data->events );

     D_FREE( data->mrl );

     pthread_mutex_destroy( &data->lock );
     
     DIRECT_DEALLOCATE_INTERFACE( thiz );
}

static DirectResult
IDirectFBVideoProvider_Xine_AddRef( IDirectFBVideoProvider *thiz )
{
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )

     pthread_mutex_lock( &data->lock );

     data->ref++;

     pthread_mutex_unlock( &data->lock );

     return DR_OK;
}

static DirectResult
IDirectFBVideoProvider_Xine_Release( IDirectFBVideoProvider *thiz )
{
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )

     pthread_mutex_lock( &data->lock );

     if (--data->ref == 0)
          IDirectFBVideoProvider_Xine_Destruct( thiz );
     else
          pthread_mutex_unlock( &data->lock );

     return DR_OK;
}

static DFBResult
IDirectFBVideoProvider_Xine_CreateEventBuffer( IDirectFBVideoProvider  *thiz,
                                                 IDirectFBEventBuffer   **ret_buffer )
{
     IDirectFBEventBuffer *buffer;
     DFBResult             ret;
     
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )
     
     if (!ret_buffer)
          return DFB_INVARG;
          
     ret = idirectfb_singleton->CreateEventBuffer( idirectfb_singleton, &buffer );
     if (ret)
          return ret;
          
     ret = thiz->AttachEventBuffer( thiz, buffer );
     
     buffer->Release( buffer );
     
     *ret_buffer = (ret == DFB_OK) ? buffer : NULL;
     
     return ret;
}

static DFBResult
IDirectFBVideoProvider_Xine_AttachEventBuffer( IDirectFBVideoProvider *thiz,
                                               IDirectFBEventBuffer   *events )
{
     DFBResult ret;

     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )

     pthread_mutex_lock( &data->lock );

     if (data->events) {
          pthread_mutex_unlock( &data->lock );
          return DFB_BUSY;
     }

     ret = events->AddRef( events );
     if (ret == DFB_OK)
          data->events = events;

     pthread_mutex_unlock( &data->lock );

     return ret;
}

static DFBResult
IDirectFBVideoProvider_Xine_DetachEventBuffer( IDirectFBVideoProvider *thiz,
                                               IDirectFBEventBuffer   *events )
{
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )

     pthread_mutex_lock( &data->lock );

     if (!data->events) {
          pthread_mutex_unlock( &data->lock );
          return DFB_BUFFEREMPTY;
     }

     if (data->events != events) {
          pthread_mutex_unlock( &data->lock );
          return DFB_INVARG;
     }

     data->events = NULL;

     events->Release( events );

     pthread_mutex_unlock( &data->lock );

     return DFB_OK;
}

static DFBResult
IDirectFBVideoProvider_Xine_GetCapabilities( IDirectFBVideoProvider       *thiz,
                                             DFBVideoProviderCapabilities *caps )
{
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )
     
     if (!caps)
          return DFB_INVARG;

     *caps = DVCAPS_BASIC       | DVCAPS_SCALE    | DVCAPS_SPEED      |
             DVCAPS_BRIGHTNESS  | DVCAPS_CONTRAST | DVCAPS_SATURATION |
             DVCAPS_INTERACTIVE | DVCAPS_VOLUME   | DVCAPS_EVENT;

     if (xine_get_stream_info( data->stream, XINE_STREAM_INFO_SEEKABLE ))
          *caps |= DVCAPS_SEEK;

     return DFB_OK;
}

static DFBResult
IDirectFBVideoProvider_Xine_GetSurfaceDescription( IDirectFBVideoProvider *thiz,
                                                   DFBSurfaceDescription  *desc )
{
     
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )

     if (!desc)
          return DFB_INVARG;
     
     if (!data->width || !data->height) {
          data->width  = xine_get_stream_info( data->stream,
                                               XINE_STREAM_INFO_VIDEO_WIDTH );
          data->height = xine_get_stream_info( data->stream,
                                               XINE_STREAM_INFO_VIDEO_HEIGHT );

          if (data->width < 1 || data->height < 1) {
               data->width  = 320;
               data->height = 240;
          }
     }

     desc->flags       = (DSDESC_WIDTH | DSDESC_HEIGHT | DSDESC_PIXELFORMAT);
     desc->width       = data->width;
     desc->height      = data->height;
     desc->pixelformat = data->format;

     return DFB_OK;
}

static DFBResult
IDirectFBVideoProvider_Xine_GetStreamDescription( IDirectFBVideoProvider *thiz,
                                                  DFBStreamDescription   *desc )
{
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )

     if (!desc)
          return DFB_INVARG;

     desc->caps = DVSCAPS_NONE;

     if (xine_get_stream_info( data->stream, XINE_STREAM_INFO_HAS_VIDEO )) {
          desc->caps |= DVSCAPS_VIDEO;
     
          snprintf( desc->video.encoding,
                    DFB_STREAM_DESC_ENCODING_LENGTH,
                    xine_get_meta_info( data->stream, XINE_META_INFO_VIDEOCODEC ) ?:"" );
          desc->video.framerate = xine_get_stream_info( data->stream, 
                                                        XINE_STREAM_INFO_FRAME_DURATION );
          if (desc->video.framerate)
               desc->video.framerate = 90000.0 / desc->video.framerate;
          desc->video.aspect    = xine_get_stream_info( data->stream,
                                                        XINE_STREAM_INFO_VIDEO_RATIO ) / 10000.0;
          desc->video.bitrate   = xine_get_stream_info( data->stream,
                                                        XINE_STREAM_INFO_VIDEO_BITRATE );
     }

     if (xine_get_stream_info( data->stream, XINE_STREAM_INFO_HAS_AUDIO )) {
          desc->caps |= DVSCAPS_AUDIO;

          snprintf( desc->audio.encoding,
                    DFB_STREAM_DESC_ENCODING_LENGTH,
                    xine_get_meta_info( data->stream, XINE_META_INFO_AUDIOCODEC ) ?:"" );
          desc->audio.samplerate = xine_get_stream_info( data->stream,
                                                         XINE_STREAM_INFO_AUDIO_SAMPLERATE );
          desc->audio.channels   = xine_get_stream_info( data->stream,
                                                         XINE_STREAM_INFO_AUDIO_CHANNELS );
          desc->audio.bitrate    = xine_get_stream_info( data->stream,
                                                         XINE_STREAM_INFO_AUDIO_BITRATE );
     }
               
     snprintf( desc->title,
               DFB_STREAM_DESC_TITLE_LENGTH,
               xine_get_meta_info( data->stream, XINE_META_INFO_TITLE ) ?:"" );
     snprintf( desc->author,
               DFB_STREAM_DESC_AUTHOR_LENGTH,
               xine_get_meta_info( data->stream, XINE_META_INFO_ARTIST ) ?:"" );
     snprintf( desc->album,
               DFB_STREAM_DESC_ALBUM_LENGTH,
               xine_get_meta_info( data->stream, XINE_META_INFO_ALBUM ) ?:"" );
     snprintf( desc->genre,
               DFB_STREAM_DESC_GENRE_LENGTH,
               xine_get_meta_info( data->stream, XINE_META_INFO_GENRE ) ?:"" );
     snprintf( desc->comment,
               DFB_STREAM_DESC_COMMENT_LENGTH,
               xine_get_meta_info( data->stream, XINE_META_INFO_COMMENT ) ?:"" );
     desc->year = atoi( xine_get_meta_info( data->stream, XINE_META_INFO_YEAR ) ?:"" );

     return DFB_OK;
}

static DFBResult
IDirectFBVideoProvider_Xine_PlayTo( IDirectFBVideoProvider *thiz,
                                    IDirectFBSurface       *dest,
                                    const DFBRectangle     *dest_rect,
                                    DVFrameCallback         callback,
                                    void                   *ctx )
{
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )

     if (!dest)
          return DFB_INVARG;

     if (!dest->priv)
          return DFB_DESTROYED;
     
     pthread_mutex_lock( &data->lock );

     memset( &data->dest_rect, 0, sizeof( DFBRectangle ) );

     if (dest_rect) {
          if (dest_rect->w < 1 || dest_rect->h < 1) {
               pthread_mutex_unlock( &data->lock );
               return DFB_INVARG;
          }

          data->dest_rect = *dest_rect;
          data->full_area = false;
     } else {
          data->full_area = true;
     }

     /* update visual */
     data->visual.destination = dest;
     data->visual.frame_cb    = callback;
     data->visual.frame_cdata = ctx;

     /* notify visual changes */
     if (!xine_port_send_gui_data( data->vo, 
                                   XINE_GUI_SEND_SELECT_VISUAL,
                                   (void*) &data->visual ))
     {
          pthread_mutex_unlock( &data->lock );
          return DFB_UNSUPPORTED;
     }

     if (data->status != DVSTATE_PLAY) {
          pthread_mutex_unlock( &data->lock );

          /* Holding lock here causes lock failures (avoided dead lock) in callbacks. */
          if (!xine_play( data->stream, 0, data->start_time ))
               return get_stream_error( data );

          pthread_mutex_lock( &data->lock );

          xine_set_param( data->stream, XINE_PARAM_SPEED, data->speed );
          usleep( 100 );
               
          xine_get_pos_length( data->stream,
                               NULL, NULL, &data->length );
                               
          data->status = DVSTATE_PLAY;

          send_videoprovider_event( data, DVPET_STARTED );
     }
     
     pthread_mutex_unlock( &data->lock );

     return DFB_OK;
}

static DFBResult
IDirectFBVideoProvider_Xine_Stop( IDirectFBVideoProvider *thiz )
{
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )

     pthread_mutex_lock( &data->lock );

     if (data->status == DVSTATE_STOP) {
          pthread_mutex_unlock( &data->lock );
          return DFB_OK;
     }

     if (data->status == DVSTATE_PLAY) {
          data->speed = xine_get_param( data->stream, XINE_PARAM_SPEED );

          xine_get_pos_length( data->stream, NULL, &data->start_time, NULL );
          xine_stop( data->stream );

          usleep( 50 );
     }

     data->status = DVSTATE_STOP;

     send_videoprovider_event( data, DVPET_STOPPED );

     pthread_mutex_unlock( &data->lock );

     return DFB_OK;
}

static DFBResult
IDirectFBVideoProvider_Xine_GetStatus( IDirectFBVideoProvider *thiz,
                                       DFBVideoProviderStatus *status )
{
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )

     if (!status)
          return DFB_INVARG;

     *status = data->status;

     return DFB_OK;
}

static DFBResult
IDirectFBVideoProvider_Xine_SeekTo( IDirectFBVideoProvider *thiz,
                                    double                  seconds )
{
     int offset;
     
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )
         
     if (seconds < 0.0)
          return DFB_INVARG;
          
     pthread_mutex_lock( &data->lock );

     if (!xine_get_stream_info( data->stream, XINE_STREAM_INFO_SEEKABLE )) {
          pthread_mutex_unlock( &data->lock );
          return DFB_UNSUPPORTED;
     }

     offset = (int) (seconds * 1000.0);
     if (data->length > 0 && offset > data->length) {
          pthread_mutex_unlock( &data->lock );
          return DFB_OK;
     }

     if (data->status == DVSTATE_PLAY) {
          data->speed = xine_get_param( data->stream, XINE_PARAM_SPEED );

          pthread_mutex_unlock( &data->lock );

          if (!xine_play( data->stream, 0, offset ))
               return get_stream_error( data );

          pthread_mutex_lock( &data->lock );

          xine_set_param( data->stream, XINE_PARAM_SPEED, data->speed );

          usleep( 100 );
     }
     
     data->start_time = offset;

     pthread_mutex_unlock( &data->lock );

     return DFB_OK;
}

static DFBResult
IDirectFBVideoProvider_Xine_GetPos( IDirectFBVideoProvider *thiz,
                                    double                 *seconds )
{
     int pos = 0;
     int i;
     
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )
          
     if (!seconds)
          return DFB_INVARG;

     for (i = 5; i--;) {
          if (xine_get_pos_length( data->stream, NULL, &pos, NULL ))
               break;
               
          usleep( 1000 );
     }

     *seconds = (double)pos / 1000.0;

     return DFB_OK;
}

static DFBResult
IDirectFBVideoProvider_Xine_GetLength( IDirectFBVideoProvider *thiz,
                                       double                 *seconds )
{
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )
          
     if (!seconds)
          return DFB_INVARG;

     xine_get_pos_length( data->stream, NULL, NULL, &data->length );
          
     *seconds = (double)data->length / 1000.0;

     return DFB_OK;
}

static DFBResult
IDirectFBVideoProvider_Xine_GetColorAdjustment( IDirectFBVideoProvider *thiz,
                                                DFBColorAdjustment     *adj )
{
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )

     if (!adj)
          return DFB_INVARG;

     adj->flags      = (DCAF_BRIGHTNESS | DCAF_CONTRAST | DCAF_SATURATION);
     adj->brightness = xine_get_param( data->stream,
                                       XINE_PARAM_VO_BRIGHTNESS );
     adj->contrast   = xine_get_param( data->stream,
                                       XINE_PARAM_VO_CONTRAST );
     adj->saturation = xine_get_param( data->stream,
                                       XINE_PARAM_VO_SATURATION );

     return DFB_OK;
}

static DFBResult
IDirectFBVideoProvider_Xine_SetColorAdjustment( IDirectFBVideoProvider   *thiz,
                                                const DFBColorAdjustment *adj )
{
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )

     if (!adj)
          return DFB_INVARG;

     if (adj->flags & DCAF_BRIGHTNESS)
          xine_set_param( data->stream,
                          XINE_PARAM_VO_BRIGHTNESS,
                          adj->brightness );

     if (adj->flags & DCAF_CONTRAST)
          xine_set_param( data->stream,
                          XINE_PARAM_VO_CONTRAST,
                          adj->contrast );

     if (adj->flags & DCAF_SATURATION)
          xine_set_param( data->stream,
                          XINE_PARAM_VO_SATURATION,
                          adj->saturation );

     return DFB_OK;
}

static int
translate_key( DFBInputDeviceKeySymbol key )
{
     switch (key) {
          case DIKS_F1:
               return XINE_EVENT_INPUT_MENU1;
          case DIKS_F2:
               return XINE_EVENT_INPUT_MENU2;
          case DIKS_F3:
               return XINE_EVENT_INPUT_MENU3;
          case DIKS_F4:
               return XINE_EVENT_INPUT_MENU4;
          case DIKS_F5:
               return XINE_EVENT_INPUT_MENU5;
          case DIKS_F6:
               return XINE_EVENT_INPUT_MENU6;
          case DIKS_F7:
               return XINE_EVENT_INPUT_MENU7;
          case DIKS_CURSOR_UP:
               return XINE_EVENT_INPUT_UP;
          case DIKS_CURSOR_DOWN:
               return XINE_EVENT_INPUT_DOWN;
          case DIKS_CURSOR_LEFT:
               return XINE_EVENT_INPUT_LEFT;
          case DIKS_CURSOR_RIGHT:
               return XINE_EVENT_INPUT_RIGHT;
          case DIKS_ENTER:
               return XINE_EVENT_INPUT_SELECT;
          case DIKS_PAGE_DOWN:
               return XINE_EVENT_INPUT_NEXT;
          case DIKS_PAGE_UP:
               return XINE_EVENT_INPUT_PREVIOUS;
          case DIKS_END:
               return XINE_EVENT_INPUT_ANGLE_NEXT;
          case DIKS_HOME:
               return XINE_EVENT_INPUT_ANGLE_PREVIOUS;
          case DIKS_BACKSPACE:
               return XINE_EVENT_INPUT_BUTTON_FORCE;
          case DIKS_0:
               return XINE_EVENT_INPUT_NUMBER_0;
          case DIKS_1:
               return XINE_EVENT_INPUT_NUMBER_1;
          case DIKS_2:
               return XINE_EVENT_INPUT_NUMBER_2;
          case DIKS_3:
               return XINE_EVENT_INPUT_NUMBER_3;
          case DIKS_4:
               return XINE_EVENT_INPUT_NUMBER_4;
          case DIKS_5:
               return XINE_EVENT_INPUT_NUMBER_5;
          case DIKS_6:
               return XINE_EVENT_INPUT_NUMBER_6;
          case DIKS_7:
               return XINE_EVENT_INPUT_NUMBER_7;
          case DIKS_8:
               return XINE_EVENT_INPUT_NUMBER_8;
          case DIKS_9:
               return XINE_EVENT_INPUT_NUMBER_9;
          case DIKS_PLUS_SIGN:
               return XINE_EVENT_INPUT_NUMBER_10_ADD;
          default:
               break;
     }
          
     return 0;
}

static DFBResult
IDirectFBVideoProvider_Xine_SendEvent( IDirectFBVideoProvider *thiz,
                                       const DFBEvent         *evt )
{
     xine_input_data_t  i;
     xine_event_t      *e = &i.event;
     int                dw, dh;
     
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )
     
     if (!evt)
          return DFB_INVARG;

     pthread_mutex_lock( &data->lock );

     if (data->status == DVSTATE_FINISHED) {
          pthread_mutex_unlock( &data->lock );
          return DFB_OK;
     }

     if (data->full_area) {
          IDirectFBSurface *s = data->visual.destination;
          s->GetSize( s, &dw, &dh );
     } else {
          dw = data->dest_rect.w;
          dh = data->dest_rect.h;
     }    

     pthread_mutex_unlock( &data->lock );

     e->type = 0;

     switch (evt->clazz) {
          case DFEC_INPUT:
               switch (evt->input.type) {
                    case DIET_KEYPRESS:
                         e->type = translate_key( evt->input.key_symbol );
                         break;
                    case DIET_BUTTONPRESS:
                         e->type = XINE_EVENT_INPUT_MOUSE_BUTTON;
                         switch (evt->input.button) {
                              case DIBI_LEFT:
                                   i.button = 1;
                                   break;
                              case DIBI_MIDDLE:
                                   i.button = 2;
                                   break;
                              case DIBI_RIGHT:
                                   i.button = 3;
                                   break;
                              default:
                                   e->type = 0;
                                   break;
                         }
                         i.x = data->mouse_x;
                         i.y = data->mouse_y;
                         break;
                    case DIET_AXISMOTION: 
                         e->type = XINE_EVENT_INPUT_MOUSE_MOVE;
                         switch (evt->input.axis) {
                              case DIAI_X:
                                   if (evt->input.flags & DIEF_AXISREL)
                                        data->mouse_x += evt->input.axisrel *
                                                         data->width / dw;
                                   if (evt->input.flags & DIEF_AXISABS)
                                        data->mouse_x = evt->input.axisabs *
                                                         data->width / dw;
                                   break;
                              case DIAI_Y:
                                   if (evt->input.flags & DIEF_AXISREL)
                                        data->mouse_y += evt->input.axisabs *
                                                         data->height / dh;
                                   if (evt->input.flags & DIEF_AXISABS)
                                        data->mouse_y = evt->input.axisabs *
                                                         data->height / dh;
                                   break;
                              default:
                                   e->type = 0;
                                   break;
                         }
                         i.x = data->mouse_x;
                         i.y = data->mouse_y;
                         break;
                    default:
                         break;
               }
               break;

          case DFEC_WINDOW:
               switch (evt->window.type) {
                    case DWET_KEYDOWN:
                         e->type = translate_key( evt->input.key_symbol );
                         break;
                    case DWET_BUTTONDOWN:
                         e->type = XINE_EVENT_INPUT_MOUSE_BUTTON;
                         switch (evt->window.button) {
                              case DIBI_LEFT:
                                   i.button = 1;
                                   break;
                              case DIBI_MIDDLE:
                                   i.button = 2;
                                   break;
                              case DIBI_RIGHT:
                                   i.button = 3;
                                   break;
                              default:
                                   e->type = 0;
                                   break;
                         }
                         i.x = evt->window.x * data->width  / dw;
                         i.y = evt->window.y * data->height / dh;
                         break;
                    case DWET_MOTION:
                         e->type = XINE_EVENT_INPUT_MOUSE_MOVE;
                         i.x = evt->window.x * data->width  / dw;
                         i.y = evt->window.y * data->height / dh;
                         break;
                    default:
                         break;
               }
               break;

          default:
               break;
     }
   
     if (e->type) {
          e->stream      = data->stream;
          e->data        = NULL;
          e->data_length = 0;
          gettimeofday( &e->tv, NULL );

          if (e->type == XINE_EVENT_INPUT_MOUSE_MOVE  ||
              e->type == XINE_EVENT_INPUT_MOUSE_BUTTON) {
               e->data        = (void*) e;
               e->data_length = sizeof(xine_input_data_t);
          }

          xine_event_send( data->stream, e );
     }

     return DFB_OK;
}

static DFBResult
IDirectFBVideoProvider_Xine_SetPlaybackFlags( IDirectFBVideoProvider        *thiz,
                                              DFBVideoProviderPlaybackFlags  flags )
{
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )
     
     if (flags & ~DVPLAY_LOOPING)
          return DFB_UNSUPPORTED;
          
     data->flags = flags;
     
     return DFB_OK;
}

static DFBResult
IDirectFBVideoProvider_Xine_SetSpeed( IDirectFBVideoProvider *thiz,
                                      double                  multiplier )
{    
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )
     
     if (multiplier < 0.0)
          return DFB_INVARG;
          
     if (multiplier > 32.0)
          return DFB_UNSUPPORTED;
     
     xine_set_param( data->stream, XINE_PARAM_SPEED,
                     (multiplier*XINE_SPEED_NORMAL+.5) );

     return DFB_OK;
}

static DFBResult
IDirectFBVideoProvider_Xine_GetSpeed( IDirectFBVideoProvider *thiz,
                                      double                 *ret_multiplier )
{
     int speed;
     
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )
     
     if (!ret_multiplier)
          return DFB_INVARG;
          
     speed = xine_get_param( data->stream, XINE_PARAM_SPEED );

     *ret_multiplier = (double)speed / (double)XINE_SPEED_NORMAL;

     return DFB_OK;
}

static DFBResult
IDirectFBVideoProvider_Xine_SetVolume( IDirectFBVideoProvider *thiz,
                                       float                   level )
{
     int vol, amp;
     
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )
     
     if (level < 0.0)
          return DFB_INVARG;
          
     if (level > 2.0)
          return DFB_UNSUPPORTED;
          
     if (level > 1.0) {
          vol = 100;
          amp = (level*100.0);
     }
     else {
          vol = (level*100.0);
          amp = 100;
     }
     
     xine_set_param( data->stream, XINE_PARAM_AUDIO_VOLUME, vol );
     xine_set_param( data->stream, XINE_PARAM_AUDIO_AMP_LEVEL, amp );

     return DFB_OK;
}

static DFBResult
IDirectFBVideoProvider_Xine_GetVolume( IDirectFBVideoProvider *thiz,
                                       float                  *ret_level )
{
     int vol, amp;
     
     DIRECT_INTERFACE_GET_DATA( IDirectFBVideoProvider_Xine )
     
     if (!ret_level)
          return DFB_INVARG;

     vol = xine_get_param( data->stream, XINE_PARAM_AUDIO_VOLUME );
     amp = xine_get_param( data->stream, XINE_PARAM_AUDIO_AMP_LEVEL );

     *ret_level = (float)vol/100.0 * (float)amp/100.0;
     
     return DFB_OK;
}
     

/****************************** Exported Symbols ******************************/

static char *filename_to_mrl( const char *filename )
{
     struct stat st;
     
     if (!filename || !strncmp( filename, "stdin:", 6 ))
          return NULL; /* force data buffer */
          
     if (stat( filename, &st ) == 0 && S_ISFIFO( st.st_mode ))
          return NULL; /* force data buffer */
        
     if (!strcmp ( filename, "/dev/cdrom" )     ||
         !strncmp( filename, "/dev/cdroms/", 12 ))
          return D_STRDUP( "cdda:/1" );

     if (!strcmp( filename, "/dev/dvd" ))
          return D_STRDUP( "dvd:/" );
          
     if (!strcmp( filename, "/dev/vcd" ))
          return D_STRDUP( "vcd:/" );
        
     return D_STRDUP( filename );
}

static DFBResult
Probe( IDirectFBVideoProvider_ProbeContext *ctx )
{
     char              *mrl;
     char              *xinerc;
     xine_t            *xine;
     xine_video_port_t *vo;
     xine_audio_port_t *ao;
     xine_stream_t     *stream;
     DFBResult          result;

     if (dfb_system_type() == CORE_X11VDPAU)
          return DFB_UNSUPPORTED;
     
     mrl = filename_to_mrl( ctx->filename );
     if (!mrl)
          return DFB_OK; /* avoid probe in this case */
          
     /* Ignore GIFs */
     if (!strcmp( strrchr( mrl, '.' ) ? : "", ".gif" )) {
          D_FREE( mrl );
          return DFB_UNSUPPORTED;
     }
     
     xine = xine_new();
     if (!xine) {
          D_FREE( mrl );
          return DFB_INIT;
     }

     xinerc = getenv( "XINERC" );
     if (!xinerc || !*xinerc) {
          xinerc = alloca( 2048 );
          snprintf( xinerc, 2048, 
                    "%s/.xine/config", xine_get_homedir() );
     }
          
     xine_config_load( xine, xinerc );

     xine_init( xine );

     vo = xine_open_video_driver( xine, "none", XINE_VISUAL_TYPE_NONE, NULL );
     if (!vo) {
          xine_exit( xine );
          D_FREE( mrl );
          return DFB_INIT;
     }

     ao = xine_open_audio_driver( xine, "none", NULL );
     if (!ao) {
          xine_close_video_driver( xine, vo );
          xine_exit( xine );
          D_FREE( mrl );
          return DFB_INIT;
     }
     
     stream = xine_stream_new( xine, ao, vo );
     if (!stream) {
          xine_close_audio_driver( xine, ao );
          xine_close_video_driver( xine, vo );
          xine_exit( xine );
          D_FREE( mrl );
          return DFB_INIT;
     }
          
     result = xine_open( stream, mrl ) ? DFB_OK : DFB_UNSUPPORTED;

     xine_close( stream );
     xine_dispose( stream );
     xine_close_video_driver( xine, vo );
     xine_close_audio_driver( xine, ao );
     xine_exit( xine );
     D_FREE( mrl );

     return result;
}

static DFBResult make_pipe( char **ret_path )
{
     char path[512];
     int  i, len;

     len = snprintf( path, sizeof(path), 
                     "%s/xine-vp-", getenv("TEMP") ? : "/tmp" );

     for (i = 0; i <= 0xffff; i++) {
          snprintf( path+len, sizeof(path)-len, "%04x", i );

          if (mkfifo( path, 0600 ) < 0) {
               if (errno == EEXIST)
                    continue;
               return errno2result( errno );
          }

          if (ret_path)
               *ret_path = D_STRDUP( path );
          
          return DFB_OK;
     }

     return DFB_FAILURE;
}

static DFBResult
Construct( IDirectFBVideoProvider *thiz,
           IDirectFBDataBuffer    *buffer  )
{
     const char               *xinerc;
     int                       verbosity;
     IDirectFBDataBuffer_data *buffer_data;
     
     DIRECT_ALLOCATE_INTERFACE_DATA( thiz, IDirectFBVideoProvider_Xine );
          
     data->ref    = 1;
     data->speed  = XINE_SPEED_NORMAL;
     data->status = DVSTATE_STOP;
     data->format = DSPF_YUY2;

     buffer_data = (IDirectFBDataBuffer_data*) buffer->priv;

     data->mrl = filename_to_mrl( buffer_data->filename );
     if (!data->mrl) { /* data buffer mode */
          DFBResult ret;

          ret = make_pipe( &data->pipe );
          if (ret)
               return ret;

          buffer->AddRef( buffer );
          
          data->buffer = buffer;
          data->buffer_thread = direct_thread_create( DTT_DEFAULT,
                                                      BufferThread, data, "Xine Input" );
               
          data->mrl = D_MALLOC( strlen( data->pipe ) + 7 );
          sprintf( data->mrl, "fifo:/%s", data->pipe );
     }
     
     data->xine = xine_new();
     if (!data->xine) {
          D_ERROR( "DirectFB/VideoProvider_Xine: xine_new() failed.\n" );
          IDirectFBVideoProvider_Xine_Destruct( thiz );
          return DFB_INIT;
     }

     xinerc = getenv( "XINERC" );
     if (!xinerc || !*xinerc) {
          const char *home = xine_get_homedir();
          char        path[2048];
          
          snprintf( path, sizeof(path), "%s/.xine", home );
          mkdir( path , 0755 );
          snprintf( path, sizeof(path), "%s/.xine/config", home );
          data->cfg = D_STRDUP( path );
     } 
     else {
          data->cfg = D_STRDUP( xinerc );
     }

     if (data->cfg)
          xine_config_load( data->xine, data->cfg );

     xine_init( data->xine );

     if (direct_config->quiet) {
          verbosity = XINE_VERBOSITY_NONE;
     }
     else if (direct_config->debug) {
          verbosity = XINE_VERBOSITY_DEBUG;
     }
     else {
          verbosity = XINE_VERBOSITY_LOG;
     }
     
     xine_engine_set_param( data->xine,
                            XINE_ENGINE_PARAM_VERBOSITY,
                            verbosity );

     /* prepare the visual */
     data->visual.output_cb    = frame_output;
     data->visual.output_cdata = (void*) data;
     /* open the video driver */
     data->vo = xine_open_video_driver( data->xine, "DFB",
                                        XINE_VISUAL_TYPE_DFB,
                                        (void*) &data->visual );
     if (!data->vo) {
          D_ERROR( "DirectFB/VideoProvider_Xine: "
                   "failed to load video driver 'DFB'.\n" );
          IDirectFBVideoProvider_Xine_Destruct( thiz );
          return DFB_FAILURE;
     }
     
     /* open audio driver */
     data->ao = xine_open_audio_driver( data->xine, NULL, NULL );
     if (!data->ao) {
          D_ERROR( "DirectFB/VideoProvider_Xine: "
                   "failed to load audio driver.\n" );
          data->ao = xine_open_audio_driver( data->xine, "none", NULL );
     }
     
     /* create a new stream */
     data->stream = xine_stream_new( data->xine, data->ao, data->vo );
     if (!data->stream) {
          D_ERROR( "DirectFB/VideoProvider_Xine: "
                   "failed to create a new stream.\n" );
          IDirectFBVideoProvider_Xine_Destruct( thiz );
          return DFB_FAILURE;
     }

     xine_set_param( data->stream, 
                     XINE_PARAM_VERBOSITY,
                     verbosity );
     xine_set_param( data->stream,
                     XINE_PARAM_AUDIO_CHANNEL_LOGICAL,
                     -1 );

     direct_util_recursive_pthread_mutex_init( &data->lock );

     pthread_mutex_lock( &data->lock );

     /* create a event queue for end-of-playback notification */
     data->queue = xine_event_new_queue( data->stream );
     if (data->queue)
          xine_event_create_listener_thread( data->queue,
                                             event_listener, (void*) data );

     /* open the MRL */
     if (!xine_open( data->stream, data->mrl )) {
          DFBResult ret = get_stream_error( data );
          pthread_mutex_unlock( &data->lock );
          pthread_mutex_destroy( &data->lock );
          IDirectFBVideoProvider_Xine_Destruct( thiz );
          return ret;
     }

     xine_get_pos_length( data->stream, NULL, NULL, &data->length );

     /* init a post plugin if no video */
     if (!xine_get_stream_info( data->stream, XINE_STREAM_INFO_HAS_VIDEO ) &&
          xine_get_stream_info( data->stream, XINE_STREAM_INFO_HAS_AUDIO )) {
          const char* const *post_list;
          const char        *post_plugin;
          xine_post_out_t   *audio_source;

          post_list = xine_list_post_plugins_typed( data->xine,
                                        XINE_POST_TYPE_AUDIO_VISUALIZATION );

          post_plugin = xine_config_register_string( data->xine,
                                        "gui.post_audio_plugin", post_list[0],
                                        "Audio visualization plugin",
                                        NULL, 0, NULL, NULL );

          data->post = xine_post_init( data->xine, post_plugin,
                                       0, &data->ao, &data->vo );
          if (data->post) {
               audio_source = xine_get_audio_source( data->stream );
               xine_post_wire_audio_port( audio_source,
                                          data->post->audio_input[0] );
          }
     }

     pthread_mutex_unlock( &data->lock );

     thiz->AddRef                = IDirectFBVideoProvider_Xine_AddRef;
     thiz->Release               = IDirectFBVideoProvider_Xine_Release;
     thiz->CreateEventBuffer     = IDirectFBVideoProvider_Xine_CreateEventBuffer;
     thiz->AttachEventBuffer     = IDirectFBVideoProvider_Xine_AttachEventBuffer;
     thiz->DetachEventBuffer     = IDirectFBVideoProvider_Xine_DetachEventBuffer;
     thiz->GetCapabilities       = IDirectFBVideoProvider_Xine_GetCapabilities;
     thiz->GetSurfaceDescription = IDirectFBVideoProvider_Xine_GetSurfaceDescription;
     thiz->GetStreamDescription  = IDirectFBVideoProvider_Xine_GetStreamDescription;
     thiz->PlayTo                = IDirectFBVideoProvider_Xine_PlayTo;
     thiz->Stop                  = IDirectFBVideoProvider_Xine_Stop;
     thiz->GetStatus             = IDirectFBVideoProvider_Xine_GetStatus;
     thiz->SeekTo                = IDirectFBVideoProvider_Xine_SeekTo;
     thiz->GetPos                = IDirectFBVideoProvider_Xine_GetPos;
     thiz->GetLength             = IDirectFBVideoProvider_Xine_GetLength;
     thiz->GetColorAdjustment    = IDirectFBVideoProvider_Xine_GetColorAdjustment;
     thiz->SetColorAdjustment    = IDirectFBVideoProvider_Xine_SetColorAdjustment;
     thiz->SendEvent             = IDirectFBVideoProvider_Xine_SendEvent;
     thiz->SetPlaybackFlags      = IDirectFBVideoProvider_Xine_SetPlaybackFlags;
     thiz->SetSpeed              = IDirectFBVideoProvider_Xine_SetSpeed;
     thiz->GetSpeed              = IDirectFBVideoProvider_Xine_GetSpeed;
     thiz->SetVolume             = IDirectFBVideoProvider_Xine_SetVolume;
     thiz->GetVolume             = IDirectFBVideoProvider_Xine_GetVolume;
     
     return DFB_OK;
}


/***************************** Private Functions ******************************/

static DFBResult
get_stream_error( IDirectFBVideoProvider_Xine_data *data )
{
     DFBResult ret;
     int       err = 0;

     if (data->stream)     
          err = xine_get_error( data->stream );

     switch (err) {
          case XINE_ERROR_NO_INPUT_PLUGIN:
               D_ERROR( "DirectFB/VideoProvider_Xine: "
                        "there is no input plugin to handle '%s'.\n",
                        data->mrl );
               ret = DFB_UNSUPPORTED;
               break;

          case XINE_ERROR_NO_DEMUX_PLUGIN:
               D_ERROR( "DirectFB/VideoProvider_Xine: "
                        "there is no demuxer plugin to decode '%s'.\n",
                        data->mrl );
               ret = DFB_UNSUPPORTED;
               break;

          case XINE_ERROR_DEMUX_FAILED:
               D_ERROR( "DirectFB/VideoProvider_Xine: "
                        "demuxer plugin failed; probably '%s' is corrupted.\n",
                        data->mrl );
               ret = DFB_FAILURE;
               break;

          case XINE_ERROR_MALFORMED_MRL:
               D_ERROR( "DirectFB/VideoProvider_Xine: "
                        "mrl '%s' is corrupted.\n",
                        data->mrl );
               ret = DFB_FAILURE;
               break;

          default:
               D_ERROR( "DirectFB/VideoProvider_Xine: "
                        "xine engine generic error !!\n" );
               ret = DFB_FAILURE;
               break;
     }
     
     return ret;
}

static void
send_videoprovider_event( IDirectFBVideoProvider_Xine_data *data,
                          DFBVideoProviderEventType         type )
{
     DFBEvent event = { videoprovider: { DFEC_VIDEOPROVIDER, type } };

     if (data->events)
          data->events->PostEvent( data->events, &event );
}

static void
frame_output( void *cdata, int width, int height, double ratio,
              DFBSurfacePixelFormat format, DFBRectangle *dest_rect )
{
     IDirectFBVideoProvider_Xine_data *data = cdata;
     IDirectFBSurface                 *surface;

     if (!data)
          return;
          
     if (data->format != format || data->width != width || data->height != height) {
          data->format = format;
          data->width  = width;
          data->height = height;
          
          send_videoprovider_event( data, DVPET_SURFACECHANGE );
     }

     if (data->full_area) {
          surface = data->visual.destination;
          surface->GetSize( surface, &dest_rect->w, &dest_rect->h );
          dest_rect->x = 0;
          dest_rect->y = 0;
     }
     else {
          *dest_rect = data->dest_rect;
     }
}

static void
event_listener( void *cdata, const xine_event_t *event )
{
     int                               lock = 10;
     IDirectFBVideoProvider_Xine_data *data = cdata;

     if (!data)
          return;

     switch (event->type) {
          case XINE_EVENT_MRL_REFERENCE:
          case XINE_EVENT_UI_PLAYBACK_FINISHED:
               break;

          default:
               return;
     }

     while (pthread_mutex_trylock( &data->lock )) {
          if (!--lock) {
               D_WARN( "could not lock provider data" );
               break;
          }

          usleep( 1000 );
     }

     switch (event->type) {
          case XINE_EVENT_MRL_REFERENCE:
               if (!data->mrl_changed) {
                    xine_mrl_reference_data_t *ref = event->data;
                    
                    D_FREE( data->mrl );
                    data->mrl = D_STRDUP( ref->mrl );
                    data->mrl_changed = 1;
               }
               break;
               
          case XINE_EVENT_UI_PLAYBACK_FINISHED:
               data->speed = xine_get_param( data->stream, XINE_PARAM_SPEED );

               if (data->mrl_changed) {
                    data->mrl_changed = 0;

                    send_videoprovider_event( data, DVPET_STREAMCHANGE );

                    if (!xine_open( data->stream, data->mrl )) {
                         data->status = DVSTATE_FINISHED;

                         send_videoprovider_event( data, DVPET_FATALERROR );
                         break;
                    }
                    if (data->status == DVSTATE_PLAY) {
                         pthread_mutex_unlock( &data->lock );

                         if (!xine_play( data->stream, 0, data->start_time )) {
                              pthread_mutex_lock( &data->lock );

                              data->status = DVSTATE_STOP;

                              send_videoprovider_event( data, DVPET_FATALERROR );
                              break;
                         }

                         pthread_mutex_lock( &data->lock );

                         xine_set_param( data->stream, 
                                         XINE_PARAM_SPEED, data->speed );

                         send_videoprovider_event( data, DVPET_STARTED );
                    }
               }
               else {
                    if (data->flags & DVPLAY_LOOPING) {
                         xine_play( data->stream, 0, 0 );
                         xine_set_param( data->stream, 
                                         XINE_PARAM_SPEED, data->speed );

                         send_videoprovider_event( data, DVPET_STARTED );
                    }
                    else {
                         xine_stop( data->stream );
                         data->status = DVSTATE_FINISHED;

                         send_videoprovider_event( data, DVPET_FINISHED );
                    }
                    data->start_time = 0;
               }
               break;
        
          case XINE_EVENT_FRAME_FORMAT_CHANGE: /* aspect ratio */
               send_videoprovider_event( data, DVPET_STREAMCHANGE );
               break;

          default:
               break;
     }

     if (lock)
          pthread_mutex_unlock( &data->lock );
}

