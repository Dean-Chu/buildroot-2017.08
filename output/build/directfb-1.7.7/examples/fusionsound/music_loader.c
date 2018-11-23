#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fusionsound.h>


static FMBufferCallbackResult
callback( int length, void *ctx )
{
     *((int*)ctx) = length;
     
     return FMBCR_BREAK;
}

static IFusionSoundBuffer*
load_sample( IFusionSound *sound, const char *file )
{
     IFusionSoundMusicProvider *provider;
     IFusionSoundBuffer        *buffer;
     FSBufferDescription        dsc;
     void                      *data;
     int                        bytes;
     int                        frames = 0;
     DirectResult               ret;
     
     ret = sound->CreateMusicProvider( sound, file, &provider );
     if (ret) {
          FusionSoundError( "IFusionSound::CreateMusicProvider()", ret );
          return NULL;
     }
     
     provider->GetBufferDescription( provider, &dsc );
     
     ret = sound->CreateBuffer( sound, &dsc, &buffer );
     if (ret) {
          FusionSoundError( "IFusionSound::CreateBuffer()", ret );
          provider->Release( provider );
          return NULL;
     }
     
     /* Clear the buffer. */
     buffer->Lock( buffer, &data, NULL, &bytes );
     memset( data, 0, bytes );
     buffer->Unlock( buffer );
    
     ret = provider->PlayToBuffer( provider, buffer, callback, &frames );
     if (ret) {
          FusionSoundError( "IFusionSoundMusicProvider::PlayToBuffer()", ret );
          provider->Release( provider );
          buffer->Release( buffer );
          return NULL;
     }
     
     /* Wait until provider has finished. */
     provider->WaitStatus( provider, FMSTATE_STOP | FMSTATE_FINISHED, 0 );
     
     provider->Release( provider );
     
     printf( "Loaded %d frames of %d total.\n", frames, dsc.length );
     
     return buffer;
}     


int
main( int argc, char **argv )
{
     IFusionSound       *sound;
     IFusionSoundBuffer *buffer;
     DirectResult        ret;
     
     ret = FusionSoundInit( &argc, &argv );
     if (ret)
          FusionSoundErrorFatal( "FusionSoundInit()", ret );
          
     if (argc < 2) {
          printf( "Usage: %s <filename>\n", argv[0] );
          return 1;
     }
     
     ret = FusionSoundCreate( &sound );
     if (ret)
          FusionSoundErrorFatal( "FusionSoundCreate()", ret );
          
     buffer = load_sample( sound, argv[1] );
     if (!buffer) {
          sound->Release( sound );
          return 1;
     }
     
     buffer->Play( buffer, FSPLAY_LOOPING );
     
     getchar();
     
     buffer->Stop( buffer );
     
     buffer->Release( buffer );
     sound->Release( sound );
     
     return 0;
}
     
