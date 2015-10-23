# test_libpulse

Uses libpulse-simple to record a few samples from default device and then stop.
Outputs results to `test_libpulse.ogg` file encoding on the fly with libvorbis
and to `test_libpulse.wav` as raw PCM signed 16-bit data.

Vorbis encoding example took from
https://svn.xiph.org/trunk/vorbis/examples/encoder_example.c
but loop was modified.

Pulseaudio simple lib record example took from
http://freedesktop.org/software/pulseaudio/doxygen/parec-simple_8c-example.html
almost with no changes, except for explicit buffer_attr request, as
described in "Latency Control" document:
http://www.freedesktop.org/wiki/Software/PulseAudio/Documentation/Developer/Clients/LatencyControl/

Compile with `pkg-config --cflags --libs libpulse-simple ogg vorbis vorbisenc`