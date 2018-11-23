#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <fusionsound.h>

static void
feed_stream (IFusionSoundStream *stream)
{
     DirectResult ret;
     int       i;
     s16       buf[16384];

     /* Generate woops ;) */
     for (i=0; i<16384; i++)
          buf[i] = (s16)( sin(i*i/(16384.0*16384.0)*M_PI*200) * 10000 );

     /* Write eight times (~3 seconds of playback). */
     for (i=0; i<8; i++) {
          /*
           * Wait for a larger chunk of free space. Avoids a blocking write
           * with very small partially writes each time there's new space.
           *
           * This wait is for demonstrational purpose, in practice blocking
           * writes are no overhead and always keeping the buffer at the
           * highest fill level is more safe.
           */
          ret = stream->Wait (stream, 16384);
          if (ret) {
               FusionSoundError ("IFusionSoundStream::Write", ret);
               return;
          }

          /* This write won't block anymore. */
          ret = stream->Write (stream, buf, 16384);
          if (ret) {
               FusionSoundError ("IFusionSoundStream::Write", ret);
               return;
          }
     }
}

int
main (int argc, char *argv[])
{
     DirectResult         ret;
     IFusionSound        *sound;
     IFusionSoundStream  *stream;
     FSStreamDescription  desc;

     ret = FusionSoundInit (&argc, &argv);
     if (ret)
          FusionSoundErrorFatal ("FusionSoundInit", ret);

     /* Retrieve the main sound interface. */
     ret = FusionSoundCreate (&sound);
     if (ret)
          FusionSoundErrorFatal ("FusionSoundCreate", ret);

     /* Fill stream description. */
     desc.flags        = FSSDF_SAMPLERATE | FSSDF_BUFFERSIZE |
                         FSSDF_CHANNELS   | FSSDF_SAMPLEFORMAT;
     desc.samplerate   = 44100;
     desc.buffersize   = 32768;
     desc.channels     = 1;
     desc.sampleformat = FSSF_S16;

     /* Create the sound stream and feed it. */
     ret = sound->CreateStream (sound, &desc, &stream);
     if (ret) {
          FusionSoundError ("IFusionSound::CreateStream", ret);
     }
     else {
          /* Fill the ring buffer with our generated data. */
          feed_stream (stream);

          /* Wait for end of stream (ring buffer holds ~3/4 sec). */
          stream->Wait (stream, 0);

          stream->Release (stream);
     }

     sound->Release (sound);

     return 0;
}

