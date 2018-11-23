#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>

#include <fusionsound.h>

#include <direct/mem.h>
#include <direct/list.h>


static IFusionSound              *sound    = NULL;
static IFusionSoundMusicProvider *provider = NULL;
static IFusionSoundStream        *stream   = NULL;
static IFusionSoundPlayback      *playback = NULL;
static struct termios             term;


static void
usage( const char *progname )
{
     fprintf( stderr, "\nUsage: %s <filename>\n", progname );
     fprintf( stderr, "\nPlayback Control:\n" );
     fprintf( stderr, "  [p] start playback\n" );
     fprintf( stderr, "  [s] stop playback\n" );
     fprintf( stderr, "  [+] seek forward (+15s)\n" );
     fprintf( stderr, "  [-] seek backward (-15s)\n" );
     fprintf( stderr, "  [ ] switch to next track\n" );
     fprintf( stderr, "  [r] toggle track repeat\n" );
     fprintf( stderr, "  [v] decrease volume level\n" );
     fprintf( stderr, "  [V] increase volume level\n" );
     fprintf( stderr, "  [q] quit\n\n" );
     exit( 1 );
}
     
static void
cleanup( int s )
{
     puts ("\nQuit.");
     
     if (provider)
          provider->Release( provider );
     if (playback)
          playback->Release( playback );
     if (stream)
          stream->Release( stream );
     if (sound)
          sound->Release( sound );
     
     tcsetattr( fileno(stdin), TCSADRAIN, &term );
     
     exit( s );
}

static DirectEnumerationResult
track_display_callback( FSTrackID id, FSTrackDescription desc, void *ctx )
{
     printf( "  Track %2d: %s - %s\n", id,
             *desc.artist ? desc.artist : "Unknown",
             *desc.title  ? desc.title  : "Unknown" );
     
     return DENUM_OK;
}

static DirectEnumerationResult
track_playback_callback( FSTrackID id, FSTrackDescription desc, void *ctx )
{
     DirectResult          ret;
     FSMusicProviderStatus status = FMSTATE_UNKNOWN;
     double                len    = 0;
     static int            flags  = FMPLAY_NOFX;
     static float          volume = 1.0;
     FSStreamDescription   s_dsc;
          
     /* Select current track in playlist. */
     ret = provider->SelectTrack( provider, id );
     if (ret) {
          FusionSoundError( "IFusionSoundMusicProvider::SelectTrack", ret );
          return DENUM_OK;
     }
     
     provider->GetStreamDescription( provider, &s_dsc );
     if (stream) {
          FSStreamDescription dsc;
          /* Check whether stream format changed. */
          stream->GetDescription( stream, &dsc );
          if (dsc.samplerate   != s_dsc.samplerate ||
              dsc.channels     != s_dsc.channels   ||
              dsc.sampleformat != s_dsc.sampleformat)
          {
               if (playback) {
                    playback->Release( playback ); 
                    playback = NULL;
               }
               stream->Release( stream );
               stream = NULL;
          }
     }
     if (!stream) {
          /* Create the sound stream and feed it. */
          ret = sound->CreateStream( sound, &s_dsc, &stream );
          if (ret) {
               FusionSoundError( "IFusionSound::CreateStream", ret );
               return DENUM_CANCEL;
          }
          stream->GetDescription( stream, &s_dsc );
          stream->GetPlayback( stream, &playback );
     }

     /* Reset volume level. */
     playback->SetVolume( playback, volume );
          
     /* Query provider for track length. */
     provider->GetLength( provider, &len );
          
     /* Let the provider play the music using our stream. */
     ret = provider->PlayToStream( provider, stream );
     if (ret) {
          FusionSoundError( "IFusionSoundMusicProvider::PlayTo", ret );
          return DENUM_CANCEL;
     }
     
     /* Update track's description. */
     provider->GetTrackDescription( provider, &desc );
          
     /* Print some informations about the track. */
     printf( "\nTrack %d:\n"
             "  Artist:     %s\n"
             "  Title:      %s\n"
             "  Album:      %s\n"
             "  Year:       %d\n"
             "  Genre:      %s\n"
             "  Encoding:   %s\n"
             "  Bitrate:    %d Kbits/s\n" 
             "  ReplayGain: %.2f (track), %.2f (album)\n"
             "  Output:     %d Hz, %d channel(s), %d bits\n\n\n",
             id, desc.artist, desc.title, desc.album, (int)desc.year, 
             desc.genre, desc.encoding, desc.bitrate/1000, 
             desc.replaygain, desc.replaygain_album,
             s_dsc.samplerate, s_dsc.channels,
             FS_BITS_PER_SAMPLE(s_dsc.sampleformat) );
     fflush( stdout );

     do {
          int    filled = 0;
          int    total  = 0;
          double pos    = 0;
          
          /* Query ring buffer status. */
          stream->GetStatus( stream, &filled, &total, NULL, NULL, NULL );
          /* Query elapsed seconds. */
          provider->GetPos( provider, &pos );
          /* Query playback status. */
          provider->GetStatus( provider, &status );

          /* Print playback status. */
          printf( "\rTime: %02d:%02d,%02d of %02d:%02d,%02d  Ring Buffer: %02d%%",
                  (int)pos/60, (int)pos%60, (int)(pos*100.0)%100,
                  (int)len/60, (int)len%60, (int)(len*100.0)%100,
                  filled * 100 / total );
          fflush( stdout );

          if (isatty( fileno(stdin) )) {
               int c;

               while ((c = getc( stdin )) > 0) {
                    switch (c) {
                         case 's':
                              provider->Stop( provider );
                              break;
                         case 'p':
                              provider->PlayToStream( provider, stream );
                              break;
                         case '+':
                              provider->GetPos( provider, &pos );
                              provider->SeekTo( provider, pos+15.0 );
                              break;
                         case '-':
                              provider->GetPos( provider, &pos );
                              provider->SeekTo( provider, pos-15.0 );
                              break;
                         case ' ':
                              provider->Stop( provider );
                              status = FMSTATE_FINISHED;
                              break;
                         case 'r':
                              flags ^= FMPLAY_LOOPING;
                              provider->SetPlaybackFlags( provider, flags );
                              break;
                         case 'v':
                              volume -= 0.1;
                              playback->SetVolume( playback, volume );
                              break;
                         case 'V':
                              volume += 0.1;
                              playback->SetVolume( playback, volume );
                              break;
                         case 'q':
                         case 'Q':
                         case '\033': // Escape
                              return DENUM_CANCEL;
                         default:
                              break;
                    }
               }
          }
               
          usleep( 20000 );
     } while (status != FMSTATE_FINISHED);
     
     printf( "\n" );
     
     return DENUM_OK;
}     

int
main( int argc, char *argv[] )
{
     DirectResult ret;

     ret = FusionSoundInit( &argc, &argv );
     if (ret)
          FusionSoundErrorFatal( "FusionSoundInit", ret );

     if (argc != 2)
          usage( argv[0] );
         
     /* Retrieve the main sound interface. */
     ret = FusionSoundCreate( &sound );
     if (ret)
          FusionSoundErrorFatal( "FusionSoundCreate", ret );

     /* Create a music provider for the specified file. */
     ret = sound->CreateMusicProvider( sound, argv[1], &provider );
     if (ret) {
          FusionSoundError( "IFusionSound::CreateMusicProvider", ret );
          sound->Release( sound );
          return 1;
     }

     /* Get terminal attributes. */
     tcgetattr( fileno(stdin), &term );
     
     /* Register clean-up handlers. */
     signal( SIGINT, cleanup );
     signal( SIGTERM, cleanup );
     
     if (isatty( fileno(stdin) )) {
          struct termios cur;
          /* Set terminal attributes */
          cur = term;
          cur.c_cc[VTIME] = 0;
          cur.c_cc[VMIN]  = 0;
          cur.c_lflag    &= ~(ICANON | ECHO);
          tcsetattr( fileno(stdin), TCSAFLUSH, &cur );
     }
     
     /* Show contents. */
     printf( "\n%s:\n", argv[1] );
     provider->EnumTracks( provider, track_display_callback, NULL );
     printf( "\n" );
     
     /* Play tracks. */
     provider->EnumTracks( provider, track_playback_callback, NULL );
     
     puts( "\nFinished." );
     cleanup( 0 );

     return 0;
}

