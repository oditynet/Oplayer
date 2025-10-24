#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <FLAC/stream_decoder.h>

typedef struct {
    int16_t* pcm_data;
    uint32_t samples_count;
    int sample_rate;
    int channels;
    int bits_per_sample;
} AudioData;

typedef struct {
    AudioData* audio;
    FLAC__StreamDecoder* decoder;
    uint32_t current_position;
    float volume_scale;
} FlacDecodeState;

static FLAC__StreamDecoderWriteStatus write_callback(
    const FLAC__StreamDecoder* decoder,
    const FLAC__Frame* frame,
    const FLAC__int32* const buffer[],
    void* client_data) {
    
    FlacDecodeState* state = (FlacDecodeState*)client_data;
    AudioData* audio = state->audio;
    
    uint32_t samples_needed = frame->header.blocksize * audio->channels;
    
    // Проверяем, нужно ли увеличить буфер
    if (state->current_position + samples_needed > audio->samples_count) {
        uint32_t new_size = state->current_position + samples_needed;
        audio->pcm_data = realloc(audio->pcm_data, new_size * sizeof(int16_t));
        audio->samples_count = new_size;
    }
    
    // Определяем коэффициент масштабирования в зависимости от битности
    int shift_bits = audio->bits_per_sample - 16;
    float scale_factor = state->volume_scale;
    
    // Конвертируем FLAC samples в 16-bit PCM с нормализацией
    for (uint32_t i = 0; i < frame->header.blocksize; i++) {
        for (int channel = 0; channel < audio->channels; channel++) {
            FLAC__int32 sample = buffer[channel][i];
            
            // Масштабируем с учетом битности и коэффициента громкости
            float scaled_sample;
            
            if (shift_bits > 0) {
                // Для 24-бит и выше: сдвигаем вправо и применяем коэффициент
                scaled_sample = (sample >> shift_bits) * scale_factor;
            } else if (shift_bits < 0) {
                // Для менее чем 16 бит (маловероятно для FLAC): сдвигаем влево
                scaled_sample = (sample << (-shift_bits)) * scale_factor;
            } else {
                // Для 16 бит: просто применяем коэффициент
                scaled_sample = sample * scale_factor;
            }
            
            // Ограничиваем диапазон
            if (scaled_sample > 32767.0f) scaled_sample = 32767.0f;
            else if (scaled_sample < -32768.0f) scaled_sample = -32768.0f;
            
            audio->pcm_data[state->current_position++] = (int16_t)scaled_sample;
        }
    }
    
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void metadata_callback(
    const FLAC__StreamDecoder* decoder,
    const FLAC__StreamMetadata* metadata,
    void* client_data) {
    
    FlacDecodeState* state = (FlacDecodeState*)client_data;
    AudioData* audio = state->audio;
    
    if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
        audio->sample_rate = metadata->data.stream_info.sample_rate;
        audio->channels = metadata->data.stream_info.channels;
        audio->bits_per_sample = metadata->data.stream_info.bits_per_sample;
        
        // Устанавливаем коэффициент громкости в зависимости от битности
        if (audio->bits_per_sample > 16) {
            // Для 24-бит уменьшаем громкость (24 бит = 144 dB динамического диапазона)
            state->volume_scale = 0.5f; // Уменьшаем на 6 dB
        } else {
            // Для 16 бит оставляем нормальную громкость
            state->volume_scale = 1.0f;
        }
        
        // Предварительно выделяем память для PCM данных
        uint32_t total_samples = metadata->data.stream_info.total_samples * audio->channels;
        audio->samples_count = total_samples;
        audio->pcm_data = malloc(total_samples * sizeof(int16_t));
        
        if (!audio->pcm_data) {
            fprintf(stderr, "Memory allocation failed\n");
        }
    }
}

static void error_callback(
    const FLAC__StreamDecoder* decoder,
    FLAC__StreamDecoderErrorStatus status,
    void* client_data) {
    
    fprintf(stderr, "FLAC decoder error: %s\n", FLAC__StreamDecoderErrorStatusString[status]);
}

AudioData* decode_flac(const char* filename) {
    FlacDecodeState state = {0};
    AudioData* audio = calloc(1, sizeof(AudioData));
    if (!audio) return NULL;
    
    state.audio = audio;
    state.current_position = 0;
    state.volume_scale = 1.0f;
    
    // Создаем декодер
    state.decoder = FLAC__stream_decoder_new();
    if (!state.decoder) {
        free(audio);
        return NULL;
    }
    
    // Инициализируем декодер
    FLAC__StreamDecoderInitStatus init_status = FLAC__stream_decoder_init_file(
        state.decoder, filename, write_callback, metadata_callback, 
        error_callback, &state);
    
    if (init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        FLAC__stream_decoder_delete(state.decoder);
        free(audio);
        return NULL;
    }
    
    // Запускаем декодирование
    if (!FLAC__stream_decoder_process_until_end_of_stream(state.decoder)) {
        FLAC__stream_decoder_delete(state.decoder);
        if (audio->pcm_data) free(audio->pcm_data);
        free(audio);
        return NULL;
    }
    
    // Завершаем декодирование
    FLAC__stream_decoder_finish(state.decoder);
    FLAC__stream_decoder_delete(state.decoder);
    
    return audio;
}

void free_audio_data(AudioData* audio) {
    if (audio) {
        if (audio->pcm_data) {
            free(audio->pcm_data);
        }
        free(audio);
    }
}