#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpg123.h>
#include "mp3_decoder.h"

AudioData* decode_mp3(const char* filename) {
    int err;
    mpg123_handle *mh = NULL;
    
    // Инициализация библиотеки
    err = mpg123_init();
    if (err != MPG123_OK) {
        fprintf(stderr, "mpg123_init failed: %s\n", mpg123_plain_strerror(err));
        return NULL;
    }
    
    // Создание дескриптора
    mh = mpg123_new(NULL, &err);
    if (!mh) {
        fprintf(stderr, "mpg123_new failed: %s\n", mpg123_plain_strerror(err));
        mpg123_exit();
        return NULL;
    }
    
    // Открытие файла
    if (mpg123_open(mh, filename) != MPG123_OK) {
        fprintf(stderr, "mpg123_open failed: %s\n", mpg123_strerror(mh));
        mpg123_delete(mh);
        mpg123_exit();
        return NULL;
    }
    
    // Получение информации о формате
    long sample_rate;
    int channels, encoding;
    if (mpg123_getformat(mh, &sample_rate, &channels, &encoding) != MPG123_OK) {
        fprintf(stderr, "mpg123_getformat failed: %s\n", mpg123_strerror(mh));
        mpg123_close(mh);
        mpg123_delete(mh);
        mpg123_exit();
        return NULL;
    }
    
    // Убедимся, что выходной формат 16-bit signed
    mpg123_format_none(mh);
    mpg123_format(mh, sample_rate, channels, MPG123_ENC_SIGNED_16);
    
    // Выделение памяти для AudioData
    AudioData* audio = malloc(sizeof(AudioData));
    if (!audio) {
        fprintf(stderr, "Memory allocation failed for AudioData\n");
        mpg123_close(mh);
        mpg123_delete(mh);
        mpg123_exit();
        return NULL;
    }
    
    audio->sample_rate = sample_rate;
    audio->channels = channels;
    
    // Определение размера данных
    off_t length = mpg123_length(mh);
    if (length == MPG123_ERR) {
        fprintf(stderr, "mpg123_length failed: %s\n", mpg123_strerror(mh));
        free(audio);
        mpg123_close(mh);
        mpg123_delete(mh);
        mpg123_exit();
        return NULL;
    }
    
    audio->samples_count = length * channels;
    size_t buffer_size = length * channels * sizeof(int16_t);
    audio->pcm_data = malloc(buffer_size);
    
    if (!audio->pcm_data) {
        fprintf(stderr, "Memory allocation failed for PCM data\n");
        free(audio);
        mpg123_close(mh);
        mpg123_delete(mh);
        mpg123_exit();
        return NULL;
    }
    
    // Декодирование всего файла
    size_t decoded_size = 0;
    int decode_result;
    
    do {
        size_t chunk_size = 0;
        unsigned char *audio_data;
        
        decode_result = mpg123_decode_frame(mh, NULL, &audio_data, &chunk_size);
        
        if (decode_result == MPG123_OK && chunk_size > 0) {
            // Копируем декодированные данные
            size_t samples_in_chunk = chunk_size / sizeof(int16_t);
            if (decoded_size + samples_in_chunk <= audio->samples_count) {
                memcpy(audio->pcm_data + decoded_size, audio_data, chunk_size);
                decoded_size += samples_in_chunk;
            }
        }
    } while (decode_result == MPG123_OK);
    
    // Проверяем, успешно ли завершилось декодирование
    if (decode_result != MPG123_DONE && decode_result != MPG123_OK) {
        fprintf(stderr, "MP3 decoding error: %s\n", mpg123_strerror(mh));
        free(audio->pcm_data);
        free(audio);
        mpg123_close(mh);
        mpg123_delete(mh);
        mpg123_exit();
        return NULL;
    }
    
    // Обновляем реальное количество семплов
    audio->samples_count = decoded_size;
    
    // Очистка
    mpg123_close(mh);
    mpg123_delete(mh);
    mpg123_exit();
    
    return audio;
}

void free_audio_data(AudioData* audio) {
    if (audio) {
        free(audio->pcm_data);
        free(audio);
    }
}
