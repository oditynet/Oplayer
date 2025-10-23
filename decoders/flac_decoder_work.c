#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <FLAC/stream_decoder.h>
#include <sys/stat.h>

typedef struct {
    void* pcm_data;
    uint32_t samples_count;
    int sample_rate;
    int channels;
    int bits_per_sample;
    size_t frame_size;
} AudioData;

typedef struct {
    void* pcm_data;
    uint32_t samples_allocated;
    uint32_t samples_written;
    int sample_rate;
    int channels;
    int bits_per_sample;
    size_t frame_size;
    int got_error;
    FILE* file;
} FLACDecoderState;

static FLAC__StreamDecoderReadStatus read_cb(
    const FLAC__StreamDecoder *decoder,
    FLAC__byte buffer[],
    size_t *bytes,
    void *client_data) {
    
    FLACDecoderState *state = (FLACDecoderState*)client_data;
    
    if(*bytes > 0) {
        *bytes = fread(buffer, sizeof(FLAC__byte), *bytes, state->file);
        if(ferror(state->file)) {
            state->got_error = 1;
            return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
        }
        else if(*bytes == 0) {
            return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
        }
    }
    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

static FLAC__StreamDecoderWriteStatus write_cb(
    const FLAC__StreamDecoder *decoder,
    const FLAC__Frame *frame,
    const FLAC__int32 *const buffer[],
    void *client_data) {
    
    FLACDecoderState *state = (FLACDecoderState*)client_data;
    const uint32_t channels = state->channels;
    const uint32_t samples = frame->header.blocksize;
    const uint32_t needed = samples * channels;
    
    if(state->samples_allocated < state->samples_written + needed) {
        uint32_t new_size = state->samples_allocated * 2;
        if(new_size < state->samples_written + needed) {
            new_size = state->samples_written + needed + 4096;
        }
        
        void* new_buf = realloc(state->pcm_data, new_size * state->frame_size);
        if(!new_buf) {
            state->got_error = 1;
            return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
        }
        
        state->pcm_data = new_buf;
        state->samples_allocated = new_size;
    }

    if(state->bits_per_sample > 16) {
        int32_t *out = (int32_t*)state->pcm_data;
        for(uint32_t i = 0; i < samples; i++) {
            for(uint32_t ch = 0; ch < channels; ch++) {
                out[state->samples_written + i * channels + ch] = buffer[ch][i];
            }
        }
    } else {
        int16_t *out = (int16_t*)state->pcm_data;
        for(uint32_t i = 0; i < samples; i++) {
            for(uint32_t ch = 0; ch < channels; ch++) {
                out[state->samples_written + i * channels + ch] = (int16_t)buffer[ch][i];
            }
        }
    }
    state->samples_written += needed;
    
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void metadata_cb(
    const FLAC__StreamDecoder *decoder,
    const FLAC__StreamMetadata *metadata,
    void *client_data) {
    
    FLACDecoderState *state = (FLACDecoderState*)client_data;
    
    if(metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
        state->sample_rate = metadata->data.stream_info.sample_rate;
        state->channels = metadata->data.stream_info.channels;
        state->bits_per_sample = metadata->data.stream_info.bits_per_sample;
        
        state->frame_size = (state->bits_per_sample > 16) ? 
            sizeof(int32_t) * state->channels : sizeof(int16_t) * state->channels;
        
        // Allocate initial buffer for 1 second of audio
        state->samples_allocated = state->sample_rate * state->channels;
        state->pcm_data = malloc(state->samples_allocated * state->frame_size);
        
        if(!state->pcm_data) {
            state->got_error = 1;
            return;
        }
    }
}

static void error_cb(
    const FLAC__StreamDecoder *decoder,
    FLAC__StreamDecoderErrorStatus status,
    void *client_data) {
    
    FLACDecoderState *state = (FLACDecoderState*)client_data;
    state->got_error = 1;
    fprintf(stderr, "FLAC error: %s\n", FLAC__StreamDecoderErrorStatusString[status]);
}

AudioData* decode_flac(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if(!file) {
        fprintf(stderr, "Cannot open file: %s\n", filename);
        return NULL;
    }

    FLAC__StreamDecoder *decoder = FLAC__stream_decoder_new();
    if(!decoder) {
        fclose(file);
        fprintf(stderr, "Failed to create FLAC decoder\n");
        return NULL;
    }

    FLACDecoderState state = {0};
    state.file = file;
    state.got_error = 0;

    FLAC__StreamDecoderInitStatus init_status = FLAC__stream_decoder_init_stream(
        decoder,
        read_cb,
        NULL, NULL, NULL, NULL,
        write_cb,
        metadata_cb,
        error_cb,
        &state
    );

    if(init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        fprintf(stderr, "FLAC init failed: %s\n", 
               FLAC__StreamDecoderInitStatusString[init_status]);
        FLAC__stream_decoder_delete(decoder);
        fclose(file);
        return NULL;
    }

    // First process metadata
    if(!FLAC__stream_decoder_process_until_end_of_metadata(decoder)) {
        fprintf(stderr, "Failed to process metadata. Decoder state: %s\n",
               FLAC__StreamDecoderStateString[FLAC__stream_decoder_get_state(decoder)]);
        FLAC__stream_decoder_delete(decoder);
        fclose(file);
        return NULL;
    }

    // Check if we got valid metadata
    if(state.got_error || !state.pcm_data || state.sample_rate <= 0 || state.channels <= 0) {
        fprintf(stderr, "Invalid FLAC metadata\n");
        FLAC__stream_decoder_delete(decoder);
        fclose(file);
        if(state.pcm_data) free(state.pcm_data);
        return NULL;
    }

    // Then process audio data
    if(!FLAC__stream_decoder_process_until_end_of_stream(decoder)) {
        fprintf(stderr, "FLAC decoding failed. Decoder state: %s\n",
               FLAC__StreamDecoderStateString[FLAC__stream_decoder_get_state(decoder)]);
        FLAC__stream_decoder_delete(decoder);
        fclose(file);
        free(state.pcm_data);
        return NULL;
    }

    FLAC__stream_decoder_delete(decoder);
    fclose(file);

    if(state.got_error) {
        free(state.pcm_data);
        fprintf(stderr, "FLAC decoding aborted due to errors\n");
        return NULL;
    }

    // Trim the buffer to actual size
    if(state.samples_written < state.samples_allocated) {
        void* tmp = realloc(state.pcm_data, state.samples_written * state.frame_size);
        if(tmp) state.pcm_data = tmp;
    }

    AudioData *audio = malloc(sizeof(AudioData));
    if(!audio) {
        free(state.pcm_data);
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }

    audio->pcm_data = state.pcm_data;
    audio->samples_count = state.samples_written / state.channels;
    audio->sample_rate = state.sample_rate;
    audio->channels = state.channels;
    audio->bits_per_sample = state.bits_per_sample;
    audio->frame_size = state.frame_size;

    printf("Decoded: %u samples, %d Hz, %d channels, %d bits\n",
           audio->samples_count, audio->sample_rate, audio->channels,
           audio->bits_per_sample);

    return audio;
}

void free_audio_data(AudioData *audio) {
    if(audio) {
        if(audio->pcm_data) free(audio->pcm_data);
        free(audio);
    }
}
