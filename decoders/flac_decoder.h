
#ifndef FLAC_DECODER_H
#define FLAC_DECODER_H

#include <stdint.h>

typedef struct {
    void* pcm_data;         // Указатель на декодированные PCM данные
    uint32_t samples_count; // Общее количество сэмплов (на канал)
    int sample_rate;        // Частота дискретизации (Гц)
    int channels;           // Количество каналов
    int bits_per_sample;    // Битность (16, 24, 32)
    size_t frame_size;      // Размер одного фрейма в байтах (channels * bits/8)
} AudioData;

AudioData* decode_flac(const char *filename);
void free_audio_data(AudioData *audio);

#endif