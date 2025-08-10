#ifndef WAV_DECODER_H
#define WAV_DECODER_H

typedef struct {
    int16_t* pcm_data;
    uint32_t samples_count;
    int sample_rate;
    int channels;
} AudioData;

#ifdef __cplusplus
extern "C" {
#endif

AudioData* decode_wav(const char* filename);
void free_audio_data(AudioData* audio);

#ifdef __cplusplus
}
#endif

#endif
