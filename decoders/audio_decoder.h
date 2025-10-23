#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int16_t* pcm_data;
    uint32_t samples_count;
    int sample_rate;
    int channels;
} AudioData;

AudioData* decode_mp3(const char* filename);
void free_audio_data(AudioData* audio);

#endif

