#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char     riff[4];
    uint32_t file_size;
    char     wave[4];
    char     fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char     data[4];
    uint32_t data_size;
} WavHeader;

typedef struct {
    int16_t* pcm_data;
    uint32_t samples_count;
    int sample_rate;
    int channels;
} AudioData;

AudioData* decode_wav(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) return NULL;

    WavHeader header;
    if (fread(&header, sizeof(WavHeader), 1, file) != 1) {
        fclose(file);
        return NULL;
    }

    // Validate WAV format
    if (memcmp(header.riff, "RIFF", 4) != 0 || 
        memcmp(header.wave, "WAVE", 4) != 0 ||
        memcmp(header.fmt, "fmt ", 4) != 0 ||
        memcmp(header.data, "data", 4) != 0 ||
        header.audio_format != 1) {
        fclose(file);
        return NULL;
    }

    AudioData* audio = malloc(sizeof(AudioData));
    if (!audio) {
        fclose(file);
        return NULL;
    }

    audio->sample_rate = header.sample_rate;
    audio->channels = header.num_channels;
    audio->samples_count = header.data_size / (header.bits_per_sample/8);
    
    audio->pcm_data = malloc(header.data_size);
    if (!audio->pcm_data) {
        free(audio);
        fclose(file);
        return NULL;
    }

    fseek(file, sizeof(WavHeader), SEEK_SET);
    fread(audio->pcm_data, 1, header.data_size, file);
    fclose(file);

    return audio;
}

void free_audio_data(AudioData* audio) {
    if (audio) {
        free(audio->pcm_data);
        free(audio);
    }
}
