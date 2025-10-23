#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vorbis/vorbisfile.h>

typedef struct {
    int16_t* pcm_data;
    uint32_t samples_count;
    int sample_rate;
    int channels;
} AudioData;

AudioData* decode_ogg(const char* filename) {
    OggVorbis_File vf;
    FILE* file = fopen(filename, "rb");
    if (!file) return NULL;

    // Инициализация vorbis файла
    if (ov_open_callbacks(file, &vf, NULL, 0, OV_CALLBACKS_DEFAULT) < 0) {
        fclose(file);
        return NULL;
    }

    // Получение информации о потоке
    vorbis_info* vi = ov_info(&vf, -1);
    if (!vi) {
        ov_clear(&vf);
        return NULL;
    }

    AudioData* audio = malloc(sizeof(AudioData));
    if (!audio) {
        ov_clear(&vf);
        return NULL;
    }

    audio->sample_rate = vi->rate;
    audio->channels = vi->channels;
    audio->samples_count = ov_pcm_total(&vf, -1) * vi->channels;
    
    // Выделение памяти под PCM данные
    audio->pcm_data = malloc(audio->samples_count * sizeof(int16_t));
    if (!audio->pcm_data) {
        free(audio);
        ov_clear(&vf);
        return NULL;
    }

    // Декодирование данных
    int current_section = 0;
    long total_read = 0;
    while (1) {
        long ret = ov_read(&vf, 
                         (char*)(audio->pcm_data + total_read),
                         (audio->samples_count - total_read) * sizeof(int16_t),
                         0,  // Little endian
                         2,  // 16-bit samples
                         1,  // Signed
                         &current_section);

        if (ret <= 0) break;
        total_read += ret / sizeof(int16_t);
    }

    ov_clear(&vf);
    return audio;
}

void free_audio_data(AudioData* audio) {
    if (audio) {
        free(audio->pcm_data);
        free(audio);
    }
}
