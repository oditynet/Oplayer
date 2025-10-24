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
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#define MAX_FILES 1000
#define MAX_FILENAME 512
#define MAX_PATH 1024

typedef enum {
    FORMAT_UNKNOWN,
    FORMAT_WAV,
    FORMAT_AIFF,
    FORMAT_OGG,
    FORMAT_MP3,
    FORMAT_FLAC
} AudioFormat;

typedef enum {
    MODE_SEQUENTIAL,    // Проигрывать до конца списка
    MODE_PLAYLIST_LOOP, // Зациклить папку
    MODE_SINGLE_LOOP    // Зациклить текущий трек
} PlayMode;

typedef struct {
    int16_t* pcm_data;
    uint32_t samples_count;
    int sample_rate;
    int channels;
} AudioData;

typedef struct {
    AudioData* audio;
    bool playing;
    bool paused;
    bool seek_requested;
    bool next_track_requested;
    size_t current_sample;
    int total_seconds;
    pthread_mutex_t mutex;
    pa_simple* pa;
} ProgressData;

typedef struct {
    char name[MAX_FILENAME];
    char full_path[MAX_PATH];
    bool is_directory;
    bool is_parent_dir;
    bool is_audio_file;
    AudioFormat format;
} FileEntry;

typedef struct {
    FileEntry* files;
    int file_count;
    int selected_index;
    int scroll_offset;
    char current_path[MAX_PATH];
    PlayMode play_mode;
    bool show_help;
} FileManager;

// Глобальные переменные для управления состоянием
FileManager file_manager = {0};
ProgressData* current_progress_data = NULL;
pthread_t playback_thread = 0;
bool global_playing = false;
bool global_paused = false;
char current_playing_file[MAX_PATH] = "";
float global_volume = 0.7f;

// Прототипы функций
AudioFormat detect_format(const char* filename);
bool is_audio_file(const char* filename);
void print_help(const char* program_name);
void* progress_bar_thread(void* arg);
void* playback_worker(void* arg);
void* input_thread(void* arg);
void display_interface();
void display_progress_bar(int width, float progress, int elapsed_sec, int total_sec);
void display_file_list(int width, int height);
int get_console_width();
int get_console_height();
void set_nonblocking_mode(bool enable);
void clear_screen();
void move_cursor(int row, int col);
bool load_directory(const char* path);
int compare_files(const void* a, const void* b);
void play_audio_file(const char* filename);
void stop_current_playback();
void play_next_track();
void play_previous_track();
void seek_forward();
void seek_backward();
void toggle_pause();
void adjust_volume(float change);
const char* get_play_mode_name(PlayMode mode);
const char* get_format_name(AudioFormat format);

// Основная функция
int main(int argc, char** argv) {
    // Инициализация файлового менеджера
    file_manager.files = malloc(MAX_FILES * sizeof(FileEntry));
    file_manager.file_count = 0;
    file_manager.selected_index = 0;
    file_manager.scroll_offset = 0;
    file_manager.play_mode = MODE_SEQUENTIAL;
    file_manager.show_help = false;
    
    // Получаем текущую директорию
    if (getcwd(file_manager.current_path, sizeof(file_manager.current_path)) == NULL) {
        strcpy(file_manager.current_path, ".");
    }
    
    // Загружаем файлы текущей директории
    if (!load_directory(file_manager.current_path)) {
        fprintf(stderr, "Error loading directory: %s\n", file_manager.current_path);
        free(file_manager.files);
        return 1;
    }
    
    // Настраиваем терминал
    set_nonblocking_mode(true);
    clear_screen();
    
    printf("Audio Player File Manager\n");
    printf("Controls: j/k - navigate, Enter - play, Space - pause, ←/→ - seek, q - quit, r - change mode, h - help\n");
    printf("Current mode: %s\n\n", get_play_mode_name(file_manager.play_mode));
    
    // Запускаем поток ввода
    pthread_t input_thread_id;
    pthread_create(&input_thread_id, NULL, input_thread, NULL);
    
    // Основной цикл отрисовки
    while (1) {
        display_interface();
        usleep(50000); // 50ms
        
        // Автоматическое воспроизведение следующего трека
        if (global_playing && current_progress_data && !global_paused) {
            pthread_mutex_lock(&current_progress_data->mutex);
            bool track_finished = current_progress_data->current_sample >= 
                                current_progress_data->audio->samples_count - 1000;
            pthread_mutex_unlock(&current_progress_data->mutex);
            
            if (track_finished) {
                if (file_manager.play_mode == MODE_SINGLE_LOOP) {
                    // Перезапуск текущего трека
                    stop_current_playback();
                    play_audio_file(current_playing_file);
                } else {
                    // Следующий трек
                    play_next_track();
                }
            }
        }
    }
    
    // Завершение (эта часть никогда не выполняется в бесконечном цикле)
    set_nonblocking_mode(false);
    pthread_join(input_thread_id, NULL);
    free(file_manager.files);
    
    printf("\nGoodbye!\n");
    return 0;
}

// Функция сравнения для сортировки файлов
int compare_files(const void* a, const void* b) {
    const FileEntry* fileA = (const FileEntry*)a;
    const FileEntry* fileB = (const FileEntry*)b;
    
    // Сначала папки, потом файлы
    if (fileA->is_directory && !fileB->is_directory) return -1;
    if (!fileA->is_directory && fileB->is_directory) return 1;
    
    // Затем ".." всегда в самом верху
    if (fileA->is_parent_dir && !fileB->is_parent_dir) return -1;
    if (!fileA->is_parent_dir && fileB->is_parent_dir) return 1;
    
    // Сортировка по имени для одинаковых типов
    return strcasecmp(fileA->name, fileB->name);
}

// Загрузка директории
bool load_directory(const char* path) {
    DIR* dir = opendir(path);
    if (!dir) {
        return false;
    }
    
    file_manager.file_count = 0;
    file_manager.selected_index = 0;
    file_manager.scroll_offset = 0;
    
    // Добавляем ".." для перехода наверх
    if (strcmp(path, "/") != 0) {
        FileEntry* entry = &file_manager.files[file_manager.file_count++];
        strcpy(entry->name, "..");
        strcpy(entry->full_path, "..");
        entry->is_directory = true;
        entry->is_parent_dir = true;
        entry->is_audio_file = false;
        entry->format = FORMAT_UNKNOWN;
    }
    
    struct dirent* dp;
    while ((dp = readdir(dir)) != NULL && file_manager.file_count < MAX_FILES) {
        // Пропускаем скрытые файлы и специальные директории
        if (dp->d_name[0] == '.') {
            continue;
        }
        
        char full_path[MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, dp->d_name);
        
        struct stat statbuf;
        if (stat(full_path, &statbuf) == -1) {
            continue; // Пропускаем файлы без прав доступа
        }
        
        bool is_dir = S_ISDIR(statbuf.st_mode);
        bool is_audio = !is_dir && is_audio_file(dp->d_name);
        
        // Показываем только папки и аудио файлы
        if (!is_dir && !is_audio) {
            continue;
        }
        
        FileEntry* entry = &file_manager.files[file_manager.file_count];
        strncpy(entry->name, dp->d_name, MAX_FILENAME - 1);
        strncpy(entry->full_path, full_path, MAX_PATH - 1);
        entry->is_directory = is_dir;
        entry->is_parent_dir = false;
        entry->is_audio_file = is_audio;
        entry->format = is_audio ? detect_format(full_path) : FORMAT_UNKNOWN;
        
        file_manager.file_count++;
    }
    
    closedir(dir);
    
    // Сортируем файлы: папки сверху, затем аудио файлы
    qsort(file_manager.files, file_manager.file_count, sizeof(FileEntry), compare_files);
    
    strcpy(file_manager.current_path, path);
    return true;
}

// Проверка является ли файл аудио
bool is_audio_file(const char* filename) {
    const char* ext = strrchr(filename, '.');
    if (!ext) return false;
    
    ext++; // Пропускаем точку
    
    return (strcasecmp(ext, "wav") == 0 ||
            strcasecmp(ext, "aiff") == 0 ||
            strcasecmp(ext, "aif") == 0 ||
            strcasecmp(ext, "ogg") == 0 ||
            strcasecmp(ext, "mp3") == 0 ||
            strcasecmp(ext, "flac") == 0);
}

// Определение формата файла
AudioFormat detect_format(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) return FORMAT_UNKNOWN;
    
    unsigned char magic[4];
    if (fread(magic, 1, 4, file) != 4) {
        fclose(file);
        return FORMAT_UNKNOWN;
    }
    fclose(file);
    
    if ((magic[0] == 0x49 && magic[1] == 0x44 && magic[2] == 0x33) ||
        (magic[0] == 0xFF && (magic[1] & 0xE0) == 0xE0)) {
        return FORMAT_MP3;
    }
    
    if (memcmp(magic, "RIFF", 4) == 0) return FORMAT_WAV;
    if (memcmp(magic, "FORM", 4) == 0) return FORMAT_AIFF;
    if (memcmp(magic, "OggS", 4) == 0) return FORMAT_OGG;
    if (memcmp(magic, "fLaC", 4) == 0) return FORMAT_FLAC;
    
    return FORMAT_UNKNOWN;
}

// Отображение интерфейса
void display_interface() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int width = w.ws_col;
    int height = w.ws_row;
    
    clear_screen();
    move_cursor(0, 0);
    
    // Заголовок с информацией о состоянии
    const char* state_text = "";
    if (global_playing) {
        state_text = global_paused ? "PAUSED" : "PLAYING";
    } else {
        state_text = "STOPPED";
    }
    
    printf("Audio Player - %s | Mode: %s | State: %s | Volume: %d%%\n", 
           file_manager.current_path, 
           get_play_mode_name(file_manager.play_mode),
           state_text,
           (int)(global_volume * 100));
    
    printf("Controls: j/k: Navigate | Enter: Play | Space: Pause | ←/→: Seek ±10s | +/-: Volume | m: Mute | r: Mode | n/p: Next/Prev | h: Help | q: Quit\n");
    
    // Разделительная линия
    for (int i = 0; i < width; i++) printf("=");
    printf("\n");
    
    // Основное содержимое
    int content_height = height - 6; // Оставляем место для заголовка и прогресс-бара
    int list_width = width / 2;
    int progress_width = width - list_width - 1;
    
    display_file_list(list_width, content_height);
    
    // Прогресс-бар и информация о текущем треке
    move_cursor(content_height + 1, 0);
    for (int i = 0; i < width; i++) printf("-");
    printf("\n");
    
    if (global_playing && current_progress_data) {
        pthread_mutex_lock(&current_progress_data->mutex);
        int current_sec = current_progress_data->current_sample / 
                         (current_progress_data->audio->sample_rate * current_progress_data->audio->channels);
        float progress = (float)current_sec / current_progress_data->total_seconds;
        bool paused = current_progress_data->paused;
        pthread_mutex_unlock(&current_progress_data->mutex);
        
        display_progress_bar(progress_width, progress, current_sec, current_progress_data->total_seconds);
        
        // Информация о текущем треке
        move_cursor(content_height + 3, list_width + 2);
        const char* filename = strrchr(current_playing_file, '/') ? 
                              strrchr(current_playing_file, '/') + 1 : current_playing_file;
        printf("Now Playing: %s %s", filename, paused ? "[PAUSED]" : "");
        
        // Информация о перемотке
        move_cursor(content_height + 4, list_width + 2);
        printf("Use ← and → arrows to seek ±10 seconds");
    } else {
        move_cursor(content_height + 3, list_width + 2);
        printf("No track playing");
    }
    
    fflush(stdout);
}

// Отображение списка файлов
void display_file_list(int width, int height) {
    int visible_items = height - 2;
    
    // Корректируем скролл
    if (file_manager.selected_index < file_manager.scroll_offset) {
        file_manager.scroll_offset = file_manager.selected_index;
    } else if (file_manager.selected_index >= file_manager.scroll_offset + visible_items) {
        file_manager.scroll_offset = file_manager.selected_index - visible_items + 1;
    }
    
    // Отображаем файлы
    for (int i = 0; i < visible_items && i + file_manager.scroll_offset < file_manager.file_count; i++) {
        int idx = i + file_manager.scroll_offset;
        FileEntry* entry = &file_manager.files[idx];
        
        // Очистка строки
        printf("\r");
        for (int j = 0; j < width; j++) printf(" ");
        printf("\r");
        
        // Выделение выбранного элемента
        if (idx == file_manager.selected_index) {
            printf("> ");
        } else {
            printf("  ");
        }
        
        // Иконка типа файла
        if (entry->is_directory) {
            if (entry->is_parent_dir) {
                printf("[UP] ");
            } else {
                printf("[DIR] ");
            }
        } else if (entry->is_audio_file) {
            printf("[%s] ", get_format_name(entry->format));
        } else {
            printf("[   ] ");
        }
        
        // Имя файла (обрезаем если слишком длинное)
        int max_name_len = width - 10;
        if (strlen(entry->name) > max_name_len) {
            printf("%.*s...", max_name_len - 3, entry->name);
        } else {
            printf("%s", entry->name);
        }
        
        printf("\n");
    }
}

// Отображение прогресс-бара
void display_progress_bar(int width, float progress, int elapsed_sec, int total_sec) {
    if (progress > 1.0f) progress = 1.0f;
    if (progress < 0.0f) progress = 0.0f;
    
    int bar_width = width - 20;
    int pos = (int)(bar_width * progress);
    
    printf("[");
    for (int i = 0; i < bar_width; i++) {
        if (i < pos) printf("=");
        else if (i == pos) printf(">");
        else printf(" ");
    }
    printf("] %3d%% ", (int)(progress * 100));
    
    printf("%d:%02d / %d:%02d", 
           elapsed_sec / 60, elapsed_sec % 60,
           total_sec / 60, total_sec % 60);
}

// Воспроизведение аудио файла
void play_audio_file(const char* filename) {
    stop_current_playback();
    
    AudioFormat format = detect_format(filename);
    if (format == FORMAT_UNKNOWN) {
        printf("Unsupported format: %s\n", filename);
        return;
    }
    
    const char* libname;
    const char* decode_func_name;
    
    switch (format) {
        case FORMAT_WAV: libname = "./libwavdecoder.so"; decode_func_name = "decode_wav"; break;
        case FORMAT_AIFF: libname = "./libaiffdecoder.so"; decode_func_name = "decode_aiff"; break;
        case FORMAT_OGG: libname = "./liboggdecoder.so"; decode_func_name = "decode_ogg"; break;
        case FORMAT_MP3: libname = "./libmp3decoder.so"; decode_func_name = "decode_mp3"; break;
        case FORMAT_FLAC: libname = "./libflacdecoder.so"; decode_func_name = "decode_flac"; break;
        default: return;
    }
    
    void* decoder_lib = dlopen(libname, RTLD_LAZY);
    if (!decoder_lib) {
        printf("Error loading decoder: %s\n", dlerror());
        return;
    }
    
    AudioData* (*decode)(const char*) = dlsym(decoder_lib, decode_func_name);
    void (*free_audio)(AudioData*) = dlsym(decoder_lib, "free_audio_data");
    
    if (!decode || !free_audio) {
        printf("Error loading decoder functions: %s\n", dlerror());
        dlclose(decoder_lib);
        return;
    }
    
    AudioData* audio = decode(filename);
    if (!audio || audio->sample_rate <= 0 || audio->channels <= 0 || audio->samples_count == 0) {
        printf("Error decoding audio file\n");
        if (audio) free_audio(audio);
        dlclose(decoder_lib);
        return;
    }
    
    pa_sample_spec ss = {
        .format = PA_SAMPLE_S16LE,
        .rate = (uint32_t)audio->sample_rate,
        .channels = (uint8_t)audio->channels
    };
    
    if (!pa_sample_spec_valid(&ss)) {
        printf("Invalid audio format\n");
        free_audio(audio);
        dlclose(decoder_lib);
        return;
    }
    
    pa_simple* pa = pa_simple_new(NULL, "Player", PA_STREAM_PLAYBACK, 
                                 NULL, "Audio", &ss, NULL, NULL, NULL);
    if (!pa) {
        printf("Error initializing audio\n");
        free_audio(audio);
        dlclose(decoder_lib);
        return;
    }
    
    // Создаем структуру для прогресса
    ProgressData* progress_data = malloc(sizeof(ProgressData));
    int total_seconds = audio->samples_count / (audio->sample_rate * audio->channels);
    
    *progress_data = (ProgressData){
        .audio = audio,
        .playing = true,
        .paused = false,
        .seek_requested = false,
        .next_track_requested = false,
        .current_sample = 0,
        .total_seconds = total_seconds,
        .pa = pa
    };
    
    pthread_mutex_init(&progress_data->mutex, NULL);
    
    current_progress_data = progress_data;
    global_playing = true;
    global_paused = false;
    strcpy(current_playing_file, filename);
    
    // Запускаем поток воспроизведения
    pthread_create(&playback_thread, NULL, playback_worker, progress_data);
}

// Поток воспроизведения
void* playback_worker(void* arg) {
    ProgressData* data = (ProgressData*)arg;
    AudioData* audio = data->audio;
    size_t total_samples = audio->samples_count;
    size_t samples_per_second = audio->sample_rate * audio->channels;
    size_t bytes_per_sample = sizeof(int16_t);
    
    int error;
    
    while (data->playing && data->current_sample < total_samples) {
        pthread_mutex_lock(&data->mutex);
        
        // Проверяем паузу
        if (data->paused) {
            pthread_mutex_unlock(&data->mutex);
            usleep(100000); // 100ms при паузе
            continue;
        }
        
        if (data->seek_requested) {
            pa_simple_flush(data->pa, &error);
            data->seek_requested = false;
        }
        
        if (data->next_track_requested) {
            data->next_track_requested = false;
            pthread_mutex_unlock(&data->mutex);
            break;
        }
        
        size_t remaining_samples = total_samples - data->current_sample;
        size_t chunk_samples = samples_per_second / 10;
        if (chunk_samples > remaining_samples) {
            chunk_samples = remaining_samples;
        }
        
        size_t chunk_size = chunk_samples * bytes_per_sample;
        int16_t* chunk_data = audio->pcm_data + data->current_sample;
        
        // Применяем громкость
        if (global_volume != 1.0f) {
            int16_t* volume_adjusted = malloc(chunk_size);
            for (size_t i = 0; i < chunk_samples; i++) {
                volume_adjusted[i] = (int16_t)(chunk_data[i] * global_volume);
            }
            
            if (pa_simple_write(data->pa, volume_adjusted, chunk_size, &error) < 0) {
                data->playing = false;
            }
            
            free(volume_adjusted);
        } else {
            if (pa_simple_write(data->pa, chunk_data, chunk_size, &error) < 0) {
                data->playing = false;
            }
        }
        
        data->current_sample += chunk_samples;
        pthread_mutex_unlock(&data->mutex);
        usleep(5000);
    }
    
    // Завершение воспроизведения
    pthread_mutex_lock(&data->mutex);
    data->playing = false;
    if (!data->paused && pa_simple_drain(data->pa, &error) < 0) {
        // Игнорируем ошибки drain
    }
    pthread_mutex_unlock(&data->mutex);
    
    // Очистка ресурсов
    pa_simple_free(data->pa);
    
    void* decoder_lib = dlopen("./libwavdecoder.so", RTLD_LAZY);
    if (decoder_lib) {
        void (*free_audio)(AudioData*) = dlsym(decoder_lib, "free_audio_data");
        if (free_audio) {
            free_audio(audio);
        }
        dlclose(decoder_lib);
    }
    
    pthread_mutex_destroy(&data->mutex);
    free(data);
    
    global_playing = false;
    global_paused = false;
    current_progress_data = NULL;
    
    return NULL;
}

// Остановка текущего воспроизведения
void stop_current_playback() {
    if (current_progress_data && (global_playing || global_paused)) {
        pthread_mutex_lock(&current_progress_data->mutex);
        current_progress_data->playing = false;
        current_progress_data->paused = false;
        current_progress_data->next_track_requested = true;
        pthread_mutex_unlock(&current_progress_data->mutex);
        
        pthread_join(playback_thread, NULL);
        global_playing = false;
        global_paused = false;
        current_progress_data = NULL;
    }
}

// Перемотка вперед на 10 секунд
void seek_forward() {
    if (!current_progress_data || !global_playing) return;
    
    pthread_mutex_lock(&current_progress_data->mutex);
    size_t samples_per_second = current_progress_data->audio->sample_rate * current_progress_data->audio->channels;
    size_t seek_samples = 10 * samples_per_second;
    
    if (current_progress_data->current_sample + seek_samples < current_progress_data->audio->samples_count) {
        current_progress_data->current_sample += seek_samples;
    } else {
        current_progress_data->current_sample = current_progress_data->audio->samples_count - 1;
    }
    
    current_progress_data->seek_requested = true;
    pthread_mutex_unlock(&current_progress_data->mutex);
    
    printf("\rSeek +10s        ");
    fflush(stdout);
}

// Перемотка назад на 10 секунд
void seek_backward() {
    if (!current_progress_data || !global_playing) return;
    
    pthread_mutex_lock(&current_progress_data->mutex);
    size_t samples_per_second = current_progress_data->audio->sample_rate * current_progress_data->audio->channels;
    size_t seek_samples = 10 * samples_per_second;
    
    if (current_progress_data->current_sample > seek_samples) {
        current_progress_data->current_sample -= seek_samples;
    } else {
        current_progress_data->current_sample = 0;
    }
    
    current_progress_data->seek_requested = true;
    pthread_mutex_unlock(&current_progress_data->mutex);
    
    printf("\rSeek -10s        ");
    fflush(stdout);
}

// Следующий трек
void play_next_track() {
    if (!global_playing && !global_paused) return;
    
    int start_index = file_manager.selected_index;
    int current_index = start_index;
    
    do {
        current_index++;
        if (current_index >= file_manager.file_count) {
            if (file_manager.play_mode == MODE_PLAYLIST_LOOP) {
                current_index = 0;
            } else {
                stop_current_playback();
                return;
            }
        }
        
        FileEntry* entry = &file_manager.files[current_index];
        if (entry->is_audio_file && !entry->is_directory) {
            file_manager.selected_index = current_index;
            play_audio_file(entry->full_path);
            return;
        }
    } while (current_index != start_index);
}

// Предыдущий трек
void play_previous_track() {
    if (!global_playing && !global_paused) return;
    
    int start_index = file_manager.selected_index;
    int current_index = start_index;
    
    do {
        current_index--;
        if (current_index < 0) {
            if (file_manager.play_mode == MODE_PLAYLIST_LOOP) {
                current_index = file_manager.file_count - 1;
            } else {
                return;
            }
        }
        
        FileEntry* entry = &file_manager.files[current_index];
        if (entry->is_audio_file && !entry->is_directory) {
            file_manager.selected_index = current_index;
            play_audio_file(entry->full_path);
            return;
        }
    } while (current_index != start_index);
}

// Пауза/продолжение
void toggle_pause() {
    if (!current_progress_data || !global_playing) return;
    
    pthread_mutex_lock(&current_progress_data->mutex);
    current_progress_data->paused = !current_progress_data->paused;
    global_paused = current_progress_data->paused;
    pthread_mutex_unlock(&current_progress_data->mutex);
    
    if (global_paused) {
        printf("\rPaused        ");
    } else {
        printf("\rPlaying        ");
    }
    fflush(stdout);
}

// Регулировка громкости
void adjust_volume(float change) {
    global_volume += change;
    if (global_volume > 1.0f) global_volume = 1.0f;
    if (global_volume < 0.0f) global_volume = 0.0f;
    
    printf("\rVolume: %d%%        ", (int)(global_volume * 100));
    fflush(stdout);
}

// Поток ввода
void* input_thread(void* arg) {
    static float previous_volume = 0.7f; // Выносим объявление из switch
    
    while (1) {
        int ch = getchar();
        
        if (ch == EOF) {
            usleep(10000);
            continue;
        }
        
        switch (ch) {
            case 'j': // Вниз
                if (file_manager.selected_index < file_manager.file_count - 1) {
                    file_manager.selected_index++;
                }
                break;
                
            case 'k': // Вверх
                if (file_manager.selected_index > 0) {
                    file_manager.selected_index--;
                }
                break;
                
            case '\n': // Enter - воспроизведение
                if (file_manager.selected_index < file_manager.file_count) {
                    FileEntry* entry = &file_manager.files[file_manager.selected_index];
                    if (entry->is_directory) {
                        char new_path[MAX_PATH];
                        if (entry->is_parent_dir) {
                            // Переход на уровень выше
                            char* last_slash = strrchr(file_manager.current_path, '/');
                            if (last_slash) {
                                if (last_slash == file_manager.current_path) {
                                    // Мы в корневой директории
                                    strcpy(file_manager.current_path, "/");
                                } else {
                                    *last_slash = '\0';
                                }
                            }
                            load_directory(file_manager.current_path);
                        } else {
                            snprintf(new_path, sizeof(new_path), "%s/%s", 
                                   file_manager.current_path, entry->name);
                            load_directory(new_path);
                        }
                    } else if (entry->is_audio_file) {
                        play_audio_file(entry->full_path);
                    }
                }
                break;
                
            case ' ': // Пробел - пауза/продолжение
                toggle_pause();
                break;
                
            case 'r': // Смена режима воспроизведения
                file_manager.play_mode = (file_manager.play_mode + 1) % 3;
                break;
                
            case 'n': // Следующий трек
                play_next_track();
                break;
                
            case 'p': // Предыдущий трек
                play_previous_track();
                break;
                
            case '+': // Увеличить громкость
            case '=': // На той же клавише что и +
                adjust_volume(0.1f);
                break;
                
            case '-': // Уменьшить громкость
                adjust_volume(-0.1f);
                break;
                
            case 'm': // Mute/Unmute
                {
                    if (global_volume > 0.0f) {
                        previous_volume = global_volume;
                        global_volume = 0.0f;
                        printf("\rMuted        ");
                    } else {
                        global_volume = previous_volume;
                        printf("\rVolume: %d%%        ", (int)(global_volume * 100));
                    }
                    fflush(stdout);
                }
                break;
                
            case 27: // Escape sequence (стрелки)
                {
                    int ch2 = getchar();
                    if (ch2 == 91) { // [
                        int ch3 = getchar();
                        if (ch3 == 68) { // Left arrow
                            seek_backward();
                        } else if (ch3 == 67) { // Right arrow
                            seek_forward();
                        }
                    }
                }
                break;
                
            case 'h': // Помощь
                file_manager.show_help = !file_manager.show_help;
                break;
                
            case 'q': // Выход
                stop_current_playback();
                global_playing = false;
                global_paused = false;
                exit(0);
                break;
        }
    }
    
    return NULL;
}

// Вспомогательные функции
const char* get_play_mode_name(PlayMode mode) {
    switch (mode) {
        case MODE_SEQUENTIAL: return "Sequential";
        case MODE_PLAYLIST_LOOP: return "Playlist Loop";
        case MODE_SINGLE_LOOP: return "Single Loop";
        default: return "Unknown";
    }
}

const char* get_format_name(AudioFormat format) {
    switch (format) {
        case FORMAT_WAV: return "WAV";
        case FORMAT_AIFF: return "AIFF";
        case FORMAT_OGG: return "OGG";
        case FORMAT_MP3: return "MP3";
        case FORMAT_FLAC: return "FLAC";
        default: return "UNK";
    }
}

void clear_screen() {
    printf("\033[2J");
}

void move_cursor(int row, int col) {
    printf("\033[%d;%dH", row + 1, col + 1);
}

int get_console_width() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col;
}

int get_console_height() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_row;
}

void set_nonblocking_mode(bool enable) {
    static struct termios oldt, newt;
    static int oldflags = -1;
    
    if (enable) {
        if (oldflags == -1) {
            tcgetattr(STDIN_FILENO, &oldt);
            oldflags = fcntl(STDIN_FILENO, F_GETFL, 0);
        }
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        fcntl(STDIN_FILENO, F_SETFL, oldflags | O_NONBLOCK);
    } else {
        if (oldflags != -1) {
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            fcntl(STDIN_FILENO, F_SETFL, oldflags);
        }
    }
}

void print_help(const char* program_name) {
    printf("Audio Player with File Manager\n");
    printf("Usage: %s\n", program_name);
    printf("\nControls:\n");
    printf("  j/k    - Navigate up/down\n");
    printf("  Enter  - Play selected/Open directory\n");
    printf("  Space  - Pause/Resume\n");
    printf("  ←/→    - Seek backward/forward 10 seconds\n");
    printf("  n/p    - Next/Previous track\n");
    printf("  +/-    - Increase/decrease volume\n");
    printf("  m      - Mute/Unmute\n");
    printf("  r      - Change play mode\n");
    printf("  h      - Toggle help\n");
    printf("  q      - Quit\n");
    printf("\nPlay Modes:\n");
    printf("  Sequential    - Play through list and stop\n");
    printf("  Playlist Loop - Loop through current folder\n");
    printf("  Single Loop   - Loop current track\n");
}
