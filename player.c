#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pulse/simple.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <sys/ioctl.h>
#include <pthread.h>

typedef enum {
    FORMAT_UNKNOWN,
    FORMAT_WAV,
    FORMAT_AIFF,
    FORMAT_OGG,
    FORMAT_MP3
} AudioFormat;

typedef struct {
    int16_t* pcm_data;
    uint32_t samples_count;
    int sample_rate;
    int channels;
} AudioData;

typedef struct {
    AudioData* audio;
    bool* playing;
    int total_seconds;
} ProgressData;

// Прототипы функций
AudioFormat detect_format(const char* filename);
void print_help(const char* program_name);
void* progress_bar_thread(void* arg);
void display_progress_bar(int width, float progress, int elapsed_sec, int total_sec);
int get_console_width();

int main(int argc, char** argv) {
    if (argc != 2) {
        print_help(argv[0]);
        return 1;
    }

    const char* filename = argv[1];
    AudioFormat format = detect_format(filename);
    
    if (format == FORMAT_UNKNOWN) {
        fprintf(stderr, "Error: Unsupported file format\n");
        return 1;
    }

    const char* libname;
    const char* decode_func_name;

    switch (format) {
        case FORMAT_WAV: libname = "./libwavdecoder.so"; decode_func_name = "decode_wav"; break;
        case FORMAT_AIFF: libname = "./libaiffdecoder.so"; decode_func_name = "decode_aiff"; break;
        case FORMAT_OGG: libname = "./liboggdecoder.so"; decode_func_name = "decode_ogg"; break;
        case FORMAT_MP3: libname = "./libmp3decoder.so"; decode_func_name = "decode_mp3"; break;
        default: fprintf(stderr, "Error: Format not implemented\n"); return 1;
    }

    void* decoder_lib = dlopen(libname, RTLD_LAZY);
    if (!decoder_lib) {
        fprintf(stderr, "Error loading decoder: %s\n", dlerror());
        return 1;
    }

    AudioData* (*decode)(const char*) = dlsym(decoder_lib, decode_func_name);
    void (*free_audio)(AudioData*) = dlsym(decoder_lib, "free_audio_data");

    if (!decode || !free_audio) {
        fprintf(stderr, "Error loading decoder functions: %s\n", dlerror());
        dlclose(decoder_lib);
        return 1;
    }

    AudioData* audio = decode(filename);
    if (!audio) {
        fprintf(stderr, "Error decoding audio file\n");
        dlclose(decoder_lib);
        return 1;
    }
    // Дополнительная проверка параметров
    if (audio->sample_rate <= 0 || audio->channels <= 0) {
        fprintf(stderr, "Invalid audio parameters after decoding: rate=%d, channels=%d\n",
               audio->sample_rate, audio->channels);
        free_audio(audio);
        dlclose(decoder_lib);
        return 1;
    }


    int total_seconds = audio->samples_count / (audio->sample_rate * audio->channels);

if (!audio || audio->sample_rate <= 0 || audio->channels <= 0) {
    fprintf(stderr, "Invalid audio parameters\n");
    if (audio) free_audio(audio);
    dlclose(decoder_lib);
    return 1;
}

// Инициализация PulseAudio с проверкой:
pa_sample_spec ss = {
    .format = PA_SAMPLE_S16LE,
    .rate = audio->sample_rate,
    .channels = (uint8_t)audio->channels
};

if (!pa_sample_spec_valid(&ss)) {
    fprintf(stderr, "Invalid sample spec: rate=%d, channels=%d\n", 
            audio->sample_rate, audio->channels);
    free_audio(audio);
    dlclose(decoder_lib);
    return 1;
}

pa_simple *pa = pa_simple_new(NULL, "Player", PA_STREAM_PLAYBACK, 
                             NULL, "Audio", &ss, NULL, NULL, NULL);
    /*pa_simple* pa = pa_simple_new(
        NULL,
        "Audio Player",
        PA_STREAM_PLAYBACK,
        NULL,
        "Music",
        &(pa_sample_spec){
            .format = PA_SAMPLE_S16LE,
            .rate = audio->sample_rate,
            .channels = audio->channels
        },
        NULL,
        NULL,
        NULL
    );*/

    if (!pa) {
        fprintf(stderr, "Error initializing PulseAudio\n");
        free_audio(audio);
        dlclose(decoder_lib);
        return 1;
    }

    printf("Playing: %s\n", filename);
    printf("Sample rate: %d Hz, Channels: %d, Duration: %d:%02d\n", 
           audio->sample_rate, audio->channels, 
           total_seconds / 60, total_seconds % 60);

    bool playing = true;
    ProgressData progress_data = {audio, &playing, total_seconds};
    pthread_t progress_thread;
    pthread_create(&progress_thread, NULL, progress_bar_thread, &progress_data);

    int error;
    
    size_t data_size = audio->samples_count * sizeof(int16_t) * audio->channels;
if (pa_simple_write(pa, audio->pcm_data, data_size, NULL) < 0) {
    fprintf(stderr, "PulseAudio write error\n");
}
    
    if (pa_simple_write(pa, audio->pcm_data, 
                       audio->samples_count * sizeof(int16_t), &error) < 0) {
        fprintf(stderr, "Playback error: %s\n", strerror(error));
    }

    playing = false;
    pa_simple_drain(pa, &error);
    pthread_join(progress_thread, NULL);

    pa_simple_free(pa);
    free_audio(audio);
    dlclose(decoder_lib);

    return 0;
}

AudioFormat detect_format(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) return FORMAT_UNKNOWN;
    
    unsigned char magic[3];
    if (fread(magic, 1, 3, file) != 3) {
        fclose(file);
        return FORMAT_UNKNOWN;
    }
    fseek(file, 0, SEEK_SET);
    
    // Проверка MP3 (ID3 или frame sync)
    if ((magic[0] == 0x49 && magic[1] == 0x44 && magic[2] == 0x33) || // ID3
        ((magic[0] & 0xFF) == 0xFF && (magic[1] & 0xE0) == 0xE0)) {   // Frame sync
        return FORMAT_MP3;
    }
    
    char magic4[4];
    if (fread(magic4, 1, 4, file) != 4) {
        fclose(file);
        return FORMAT_UNKNOWN;
    }
    
    if (memcmp(magic4, "RIFF", 4) == 0) return FORMAT_WAV;
    if (memcmp(magic4, "FORM", 4) == 0) return FORMAT_AIFF;
    if (memcmp(magic4, "OggS", 4) == 0) return FORMAT_OGG;
    
    fclose(file);
    return FORMAT_UNKNOWN;
}

// Функция для потока отображения прогресс-бара
void* progress_bar_thread(void* arg) {
    ProgressData* data = (ProgressData*)arg;
    time_t start_time = time(NULL);
    
    while (*data->playing) {
        time_t current_time = time(NULL);
        int elapsed_seconds = (int)difftime(current_time, start_time);
        
        if (elapsed_seconds > data->total_seconds) {
            break;
        }
        
        float progress = (float)elapsed_seconds / data->total_seconds;
        display_progress_bar(get_console_width() - 20, progress, elapsed_seconds, data->total_seconds);
        
        // Обновляем прогресс-бар каждые 100 мс
        usleep(100000);
    }
    
    // Очищаем строку после завершения
    printf("\r%*s\r", get_console_width(), "");
    fflush(stdout);
    
    return NULL;
}

// Функция отображения прогресс-бара
void display_progress_bar(int width, float progress, int elapsed_sec, int total_sec) {
    int bar_width = width - 10; // Оставляем место для временной информации
    int pos = (int)(bar_width * progress);
    
    printf("\r[");
    for (int i = 0; i < bar_width; i++) {
        if (i < pos) printf("=");
        else if (i == pos) printf(">");
        else printf(" ");
    }
    printf("] %3d%% ", (int)(progress * 100));
    
    // Выводим время в формате MM:SS / MM:SS
    printf("%d:%02d / %d:%02d", 
           elapsed_sec / 60, elapsed_sec % 60,
           total_sec / 60, total_sec % 60);
    
    fflush(stdout);
}

// Функция определения ширины консоли
int get_console_width() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col;
}

// Функция вывода помощи
void print_help(const char* program_name) {
    printf("Simple Modular Audio Player\n");
    printf("Usage: %s <audio_file>\n", program_name);
    printf("\nSupported formats:\n");
    printf("  WAV (.wav)\n");
    printf("  AIFF (.aiff, .aif)\n");
    printf("  Ogg Vorbis (.ogg)\n");
}
