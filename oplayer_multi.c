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
#include <termios.h>
#include <fcntl.h>

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
    bool* seek_requested;
    size_t* current_sample;
    int total_seconds;
    pthread_mutex_t* mutex;
} ProgressData;

// Прототипы функций
AudioFormat detect_format(const char* filename);
void print_help(const char* program_name);
void* progress_bar_thread(void* arg);
void* input_thread(void* arg);
void display_progress_bar(int width, float progress, int elapsed_sec, int total_sec);
int get_console_width();
void set_nonblocking_mode(bool enable);

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

    if (audio->sample_rate <= 0 || audio->channels <= 0) {
        fprintf(stderr, "Invalid audio parameters after decoding: rate=%d, channels=%d\n",
               audio->sample_rate, audio->channels);
        free_audio(audio);
        dlclose(decoder_lib);
        return 1;
    }

    int total_seconds = audio->samples_count / (audio->sample_rate * audio->channels);

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

    // Создаем PulseAudio с буфером побольше
    pa_buffer_attr attr = {
        .maxlength = (uint32_t)-1,
        .tlength = (uint32_t)-1,
        .prebuf = (uint32_t)-1,
        .minreq = (uint32_t)-1,
        .fragsize = (uint32_t)-1
    };

    pa_simple *pa = pa_simple_new(NULL, "Player", PA_STREAM_PLAYBACK, 
                                 NULL, "Audio", &ss, NULL, &attr, NULL);

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
    printf("Controls: Left Arrow (-10s) | Right Arrow (+10s) | Q (quit)\n");

    bool playing = true;
    bool seek_requested = false;
    size_t current_sample = 0;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    ProgressData progress_data = {
        .audio = audio,
        .playing = &playing,
        .seek_requested = &seek_requested,
        .current_sample = &current_sample,
        .total_seconds = total_seconds,
        .mutex = &mutex
    };

    pthread_t progress_thread, input_thread_id;
    pthread_create(&progress_thread, NULL, progress_bar_thread, &progress_data);
    pthread_create(&input_thread_id, NULL, input_thread, &progress_data);

    int error;
    size_t total_samples = audio->samples_count;
    size_t samples_per_second = audio->sample_rate * audio->channels;
    size_t bytes_per_sample = sizeof(int16_t);

    // Включаем неблокирующий ввод
    set_nonblocking_mode(true);

    printf("\n"); // Отступ для прогресс-бара

    // Основной цикл воспроизведения
    while (playing && current_sample < total_samples) {
        pthread_mutex_lock(&mutex);
        
        if (seek_requested) {
            // Пересчитываем позицию в семплах
            int current_seconds = *progress_data.current_sample / samples_per_second;
            printf("\rSeek to: %d:%02d", current_seconds / 60, current_seconds % 60);
            fflush(stdout);
            seek_requested = false;
        }
        
        // Вычисляем сколько семплов осталось до конца
        size_t remaining_samples = total_samples - current_sample;
        
        // Отправляем большой chunk (0.5 секунды)
        size_t chunk_samples = samples_per_second / 2; // 0.5 секунды
        if (chunk_samples > remaining_samples) {
            chunk_samples = remaining_samples;
        }
        
        size_t chunk_size = chunk_samples * bytes_per_sample;
        int16_t* chunk_data = audio->pcm_data + current_sample;
        
        // Отправляем данные в PulseAudio
        if (pa_simple_write(pa, chunk_data, chunk_size, &error) < 0) {
            fprintf(stderr, "\nPlayback error: %s\n", strerror(error));
            playing = false;
        }
        
        // Обновляем позицию
        current_sample += chunk_samples;
        
        pthread_mutex_unlock(&mutex);
        
        // Небольшая пауза чтобы не перегружать систему
        usleep(10000); // 10ms
    }

    playing = false;
    
    // Дожидаемся окончания воспроизведения
    if (pa_simple_drain(pa, &error) < 0) {
        fprintf(stderr, "Drain error: %s\n", strerror(error));
    }
    
    pthread_join(progress_thread, NULL);
    pthread_join(input_thread_id, NULL);
    
    // Восстанавливаем нормальный режим ввода
    set_nonblocking_mode(false);
    
    pthread_mutex_destroy(&mutex);
    pa_simple_free(pa);
    free_audio(audio);
    dlclose(decoder_lib);

    printf("\n\nPlayback finished.\n");
    return 0;
}

// Функция для обработки ввода с клавиатуры
void* input_thread(void* arg) {
    ProgressData* data = (ProgressData*)arg;
    size_t samples_per_second = data->audio->sample_rate * data->audio->channels;
    
    while (*data->playing) {
        int ch = getchar();
        
        if (ch == EOF) {
            usleep(50000); // 50ms
            continue;
        }
        
        pthread_mutex_lock(data->mutex);
        
        // Обработка клавиш
        if (ch == 27) { // Escape sequence
            int ch2 = getchar();
            if (ch2 == 91) { // [
                int ch3 = getchar();
                if (ch3 == 68) { // Left arrow
                    // Перемотка назад на 10 секунд
                    int current_seconds = *data->current_sample / samples_per_second;
                    int new_seconds = current_seconds - 10;
                    if (new_seconds < 0) new_seconds = 0;
                    *data->current_sample = new_seconds * samples_per_second;
                    *data->seek_requested = true;
                } else if (ch3 == 67) { // Right arrow
                    // Перемотка вперед на 10 секунд
                    int current_seconds = *data->current_sample / samples_per_second;
                    int new_seconds = current_seconds + 10;
                    if (new_seconds > data->total_seconds) new_seconds = data->total_seconds;
                    *data->current_sample = new_seconds * samples_per_second;
                    *data->seek_requested = true;
                }
            }
        } else if (ch == 'q' || ch == 'Q') {
            *data->playing = false;
            printf("\rQuitting...     \n");
        }
        
        fflush(stdout);
        pthread_mutex_unlock(data->mutex);
    }
    
    return NULL;
}

// Функция для установки неблокирующего режима
void set_nonblocking_mode(bool enable) {
    static struct termios oldt, newt;
    static int oldflags;
    
    if (enable) {
        // Сохраняем текущие настройки
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        
        oldflags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, oldflags | O_NONBLOCK);
    } else {
        // Восстанавливаем оригинальные настройки
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        fcntl(STDIN_FILENO, F_SETFL, oldflags);
    }
}

AudioFormat detect_format(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) return FORMAT_UNKNOWN;
    
    unsigned char magic[4];
    if (fread(magic, 1, 4, file) != 4) {
        fclose(file);
        return FORMAT_UNKNOWN;
    }
    fclose(file);
    
    // Проверка MP3 (ID3v2 или frame sync)
    if ((magic[0] == 0x49 && magic[1] == 0x44 && magic[2] == 0x33) || // ID3v2
        (magic[0] == 0xFF && (magic[1] & 0xE0) == 0xE0)) {           // MPEG frame sync
        return FORMAT_MP3;
    }
    
    if (memcmp(magic, "RIFF", 4) == 0) return FORMAT_WAV;
    if (memcmp(magic, "FORM", 4) == 0) return FORMAT_AIFF;
    if (memcmp(magic, "OggS", 4) == 0) return FORMAT_OGG;
    
    return FORMAT_UNKNOWN;
}

// Функция для потока отображения прогресс-бара
void* progress_bar_thread(void* arg) {
    ProgressData* data = (ProgressData*)arg;
    size_t samples_per_second = data->audio->sample_rate * data->audio->channels;
    
    while (*data->playing) {
        pthread_mutex_lock(data->mutex);
        
        int current_sec = *data->current_sample / samples_per_second;
        int total_sec = data->total_seconds;
        
        if (current_sec > total_sec) {
            pthread_mutex_unlock(data->mutex);
            break;
        }
        
        float progress = (float)current_sec / total_sec;
        display_progress_bar(get_console_width() - 20, progress, current_sec, total_sec);
        
        pthread_mutex_unlock(data->mutex);
        
        usleep(100000); // 100ms
    }
    
    printf("\r%*s\r", get_console_width(), "");
    fflush(stdout);
    
    return NULL;
}

// Функция отображения прогресс-бара
void display_progress_bar(int width, float progress, int elapsed_sec, int total_sec) {
    int bar_width = width - 10;
    int pos = (int)(bar_width * progress);
    
    printf("\r[");
    for (int i = 0; i < bar_width; i++) {
        if (i < pos) printf("=");
        else if (i == pos) printf(">");
        else printf(" ");
    }
    printf("] %3d%% ", (int)(progress * 100));
    
    printf("%d:%02d / %d:%02d", 
           elapsed_sec / 60, elapsed_sec % 60,
           total_sec / 60, total_sec % 60);
    
    fflush(stdout);
}

// Функция определения ширины консоли
int get_console_width() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        return 80; // default width
    }
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
    printf("  MP3 (.mp3)\n");
    printf("\nControls during playback:\n");
    printf("  Left Arrow  - Rewind 10 seconds\n");
    printf("  Right Arrow - Forward 10 seconds\n");
    printf("  Q           - Quit\n");
}
