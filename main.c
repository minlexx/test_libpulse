#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include <pulse/simple.h>
#include <pulse/error.h>

#include <ogg/ogg.h>
#include <vorbis/vorbisenc.h>

// Vorbis encoding example took from
// https://svn.xiph.org/trunk/vorbis/examples/encoder_example.c
// but loop was modified.

// Pulseaudio simple lib record example took from
// http://freedesktop.org/software/pulseaudio/doxygen/parec-simple_8c-example.html
// almost with no changes, except for explicit buffer_attr request, as
// described in "Latency Control" document:
// http://www.freedesktop.org/wiki/Software/PulseAudio/Documentation/Developer/Clients/LatencyControl/

// Compile with pkg-config --cflags --libs libpulse-simple ogg vorbis vorbisenc


#define OUTPUT_OGGV "test_libpulse.ogg"
#define OUTPUT_WAVE "test_libpulse.wav"


void write_little_endian( unsigned int word, int num_bytes, FILE *wav_file) {
    unsigned int buf = 0;
    while( num_bytes > 0 ) {
        buf = word & 0xff;
        fwrite( &buf, 1, 1, wav_file );
        num_bytes--;
        word >>= 8;
    }
}


int main(int argc, char** argv) {
    
    // open output file for writing
    FILE *fout = fopen(OUTPUT_OGGV, "wb");
    if( !fout ) {
        fprintf(stderr, __FILE__": Failed to open %s for writing!\n", OUTPUT_OGGV);
        return EXIT_FAILURE;
    }
    FILE *fwave = fopen(OUTPUT_WAVE, "wb");
    if( !fwave ) {
        fprintf(stderr, __FILE__": Failed to open %s for writing!\n", OUTPUT_WAVE);
        fclose(fout);
        return EXIT_FAILURE;
    }

    // pulseaudio connection
    pa_simple *paconn = NULL;
    
    // format dpecifier
    static pa_sample_spec sspec;
    sspec.channels = 2;
    sspec.format = PA_SAMPLE_S16LE;
    sspec.rate = 44100;
    
    int error = 0;
    int ret = 0;
    
    // use maximum supported default values from server (recommended)
    static pa_buffer_attr buffer_attr;
    buffer_attr.fragsize = (uint32_t)-1;  // Recording only: fragment size.
    buffer_attr.maxlength = (uint32_t)-1; // Maximum length of the buffer in bytes.
    buffer_attr.minreq = (uint32_t)-1; // Playback only: minimum request.
    buffer_attr.prebuf = (uint32_t)-1; // Playback only: pre-buffering.
    buffer_attr.tlength = (uint32_t)-1; // Playback only: target length of the buffer.
    
    printf("Compiled with PulseAudio libray version: %s\n", pa_get_library_version());
    
    paconn = pa_simple_new(
            NULL, // default PA server
            "Test libpulse", // app name
            PA_STREAM_RECORD,  // stream direction
            NULL, // default device
            "record", // stream name
            &sspec,  // format spec
            NULL, // default channel map
            &buffer_attr, // may be NULL for defaults, but we want tweak!
            &error);
    if( !paconn ) {
        fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
        fclose(fout);
        fclose(fwave);
        return EXIT_FAILURE;
    }
    printf("Connected to PulseAudio server ok.\n");
    
    
    // Ogg/Vorbis encoder state vars
    ogg_stream_state os; /* take physical pages, weld into a logical stream of packets */
    ogg_page         og; /* one Ogg bitstream page.  Vorbis packets are inside */
    ogg_packet       op; /* one raw packet of data for decode */
    vorbis_info      vi; /* struct that stores all the static vorbis bitstream settings */
    vorbis_comment   vc; /* struct that stores all the user comments */
    vorbis_dsp_state vd; /* central working state for the packet->PCM decoder */
    vorbis_block     vb; /* local working space for packet->PCM decode */
    
    // init vorbis encoder
    vorbis_info_init( &vi );
    ret = vorbis_encode_init_vbr( &vi,
            (long)sspec.channels,
            (long)sspec.rate,
            0.6f ); // encode quality low < [0.1 .. 1.0] > hi
    if( ret ) {
        // error
        fprintf(stderr, __FILE__": vorbis_encode_init_vbr() failed: %d\n", ret);
        if( ret == OV_EIMPL ) fprintf(stderr, "OV_EIMPL - Unimplemented mode; unable to comply with quality level request.\n");
        if( ret == OV_EINVAL ) fprintf(stderr, "OV_EINVAL - Invalid setup request, eg, out of range argument.\n");
        if( paconn ) pa_simple_free(paconn);
        if( fout ) fclose(fout);
        if( fwave ) fclose(fwave);
        return EXIT_FAILURE;
    }
    // add a comment
    vorbis_comment_init( &vc );
    vorbis_comment_add_tag( &vc, "ENCODER", "Test libpulse" );
    vorbis_comment_add_tag( &vc, "ARTIST", "Test libpulse" );
    vorbis_comment_add_tag( &vc, "TITLE", "Record 01" );
    // set up the analysis state and auxiliary encoding storage
    vorbis_analysis_init( &vd, &vi );
    vorbis_block_init( &vd, &vb );
    // set up our packet->stream encoder
    // pick a random serial number; that way we can more likely build chained streams just by concatenation
    srand( time(NULL) );
    ogg_stream_init( &os, rand() );
    // Vorbis streams begin with three headers; the initial header (with
    // most of the codec setup parameters) which is mandated by the Ogg
    // bitstream spec.  The second header holds any comment fields.  The
    // third header holds the bitstream codebook.  We merely need to
    // make the headers, then pass them to libvorbis one at a time;
    // libvorbis handles the additional Ogg bitstream constraints
    {
        ogg_packet header;
        ogg_packet header_comm;
        ogg_packet header_code;
        vorbis_analysis_headerout( &vd, &vc, &header, &header_comm, &header_code );
        ogg_stream_packetin( &os, &header ); // automatically placed in its own page
        ogg_stream_packetin( &os, &header_comm );
        ogg_stream_packetin( &os, &header_code );
        // This ensures the actual audio data will start on a new page, as per spec
        while( 1 ) {
            printf(" ... flushing stream (headers) ...\n");
            int result = ogg_stream_flush( &os, &og );
            if( result == 0 )
                break;
            fwrite( og.header, 1, og.header_len, fout );
            fwrite( og.body,   1, og.body_len,   fout );
        }
    }
    
    int num_reads = 0;
    const int MAX_READS = 5000;
    const int NUM_SAMPLES_PER_READ = 1024;
    const int NUM_SAMPLES = NUM_SAMPLES_PER_READ * MAX_READS; // total number of samples
    const int PCM_DATA_SIZE = NUM_SAMPLES * sspec.channels * 2; // 2 bytes_per_sample
    const int PCM_BYTE_RATE = sspec.rate * sspec.channels * 2; // rate * 2ch * 2bytes_per_sample
    // buffer for one read operation
    const size_t BUFSIZE = NUM_SAMPLES_PER_READ * sspec.channels * 2; // 1024 samples * 2ch * 2 bytes_per_sample
    uint8_t buffer[BUFSIZE];
    
    // init WAV file header
    fwrite("RIFF", 1, 4, fwave);
    //write_little_endian(36 + bytes_per_sample*num_samples*num_channels, 4, fwave);
    write_little_endian(36 + PCM_DATA_SIZE, 4, fwave);
    fwrite("WAVE", 1, 4, fwave);
    // write fmt  subchunk
    fwrite("fmt ", 1, 4, fwave);
    write_little_endian(16, 4, fwave);   // "fmt " SubChunk1Size is 16
    write_little_endian(1, 2, fwave);    // PCM is format 1 (WAVE_FORMAT_PCM)
    write_little_endian(sspec.channels, 2, fwave); // num channels
    write_little_endian(sspec.rate, 4, fwave); // sample rate
    // byte_rate = sample_rate*num_channels*bytes_per_sample;
    // write_little_endian(byte_rate, 4, fwave); // byte rate
    write_little_endian(PCM_BYTE_RATE, 4, fwave); // byte rate
    write_little_endian(sspec.channels*2, 2, fwave);  // block align (channels*bytes_per_sample))
    write_little_endian(16, 2, fwave);  // bits per sample, PCM signed 16 LE
    // write data subchunk
    fwrite("data", 1, 4, fwave);
    write_little_endian(PCM_DATA_SIZE, 4, fwave);

    
    for(num_reads = 0; num_reads < MAX_READS; num_reads++) {
        ret = pa_simple_read(paconn, buffer, BUFSIZE, &error);
        if( ret < 0 ) {
            // error
            fprintf(stderr, __FILE__": pa_simple_read() failed: %s\n", pa_strerror(error));
        } else {
            // successfully read bytes from puseaudio
            printf( "%d/%d read %lu bytes from PA... ", num_reads, MAX_READS, BUFSIZE );
            //
            int i = 0;
            uint16_t *buffer16 = (uint16_t *)buffer;  // pointer to buffer as uint16 data
            short *buffer16i = (short *)buffer;  // pointer to buffer as signed int16 data
            // write to WAV file
            // fwrite(buffer, 1, BUFSIZE, fwave);
            for( i=0; i<BUFSIZE/4; i++ ) {
                uint16_t left = buffer16[i*2];
                uint16_t right = buffer16[i*2 + 1];
                write_little_endian( left, 2, fwave );
                write_little_endian( right, 2, fwave );
            }
            // now get buffer from libvorbis where we will write audio data
            // for libvorbis to encode
            float **vbuffer = vorbis_analysis_buffer( &vd, BUFSIZE/4 );
            // uninterleave samples
            for( i=0; i<BUFSIZE/4; i++){
                // PCM 16-bit WAVE can have value [-32768 .. 32767] ?
                // scale it accordingly to [-1.0.. 1.0] ???
                const float divider = 32768.0f; // 1.0f; // 32768.0f
                short left = buffer16i[i*2];
                short right = buffer16i[i*2 + 1];
                vbuffer[0][i] = (float)left / divider;
                vbuffer[1][i] = (float)right / divider;
                //vbuffer[0][i]=( ((0xff00 & (int)buffer[i*4+1]) << 8) | (0x00ff & (int)buffer[i*4])  ) / 32768.0f;
                //vbuffer[1][i]=( ((0xff00 & (int)buffer[i*4+3]) << 8) | (0x00ff & (int)buffer[i*4+2]) ) / 32768.0f;
                if( (vbuffer[0][i] > 1.0f) || (vbuffer[0][i] < -1.0f) ) {
                    printf("OOPS too large left value: %0.2f\n", vbuffer[0][i]);
                }
                if( (vbuffer[1][i] > 1.0f) || (vbuffer[1][i] < -1.0f) ) {
                    printf("OOPS too large right value: %0.2f\n", vbuffer[1][i]);
                }
            }
            // tell the library how much we actually submitted
            vorbis_analysis_wrote( &vd, i );
            printf( "wrote %d samples to libvorbis.\n", i );
            
            // vorbis does some data preanalysis, then divvies up blocks for
            // more involved (potentially parallel) processing.  Get a single
            // block for encoding now
            while( vorbis_analysis_blockout(&vd, &vb) == 1 ) {
                vorbis_analysis( &vb, NULL );
                vorbis_bitrate_addblock( &vb );
                while( vorbis_bitrate_flushpacket(&vd, &op) ) {
                    // weld the packet into the bitstream
                    ogg_stream_packetin( &os, &op );
                    // write out pages (if any)
                    while( 1 ) {
                        int result = ogg_stream_pageout(&os, &og);
                        if( result == 0 )
                            break;
                        fwrite( og.header, 1, og.header_len, fout );
                        fwrite( og.body,   1, og.body_len,   fout );
                        // this could be set above, but for illustrative purposes, I do
                        // it here (to show that vorbis does know where the stream ends)
                        if( ogg_page_eos(&og) )
                            break;
                    }
                }
            }
        }
    }
    
    // clean up libvorbis. vorbis_info_clear() must be called last
    ogg_stream_clear( &os );
    vorbis_block_clear( &vb );
    vorbis_dsp_clear( &vd );
    vorbis_comment_clear( &vc );
    vorbis_info_clear( &vi );
    // ogg_page and ogg_packet structs always point to storage in
    // libvorbis.  They're never freed or manipulated directly
    
    if( paconn )
        pa_simple_free( paconn );
    if( fout )
        fclose( fout );
    if( fwave )
        fclose(fwave);
    
    return EXIT_SUCCESS;
}
