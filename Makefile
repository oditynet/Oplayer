CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -lpulse-simple -lpulse -ldl
TARGET = audio_player
SRC = player.c

# Декодеры
DECODERS = decoders/wav_decoder.c decoders/ogg_decoder.c decoders/flac_decoder.c decoders/mp3_decoder.c
DECODER_LIBS = libwavdecoder.so liboggdecoder.so libflacdecoder.so libmp3decoder.so

all: $(TARGET) $(DECODER_LIBS)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

# Правила для сборки декодеров
libmp3decoder.so: decoders/mp3_decoder.c
	$(CC) -fPIC -shared  -Wall -Wextra -lm -o $@ $<


libwavdecoder.so: decoders/wav_decoder.c
	$(CC) -fPIC -shared -o $@ $<
	
libflacdecoder.so: decoders/flac_decoder.c
	$(CC) -fPIC -shared -shared -lFLAC -o $@ $<

#libaiffdecoder.so: decoders/aiff_decoder.c
#	$(CC) -fPIC -shared -o $@ $<

liboggdecoder.so: decoders/ogg_decoder.c
	$(CC) -fPIC -shared -o $@ $< -lvorbisfile -lvorbis -logg

clean:
	rm -f $(TARGET) $(DECODER_LIBS)

install: all
	cp $(TARGET) /usr/local/bin/
	cp $(DECODER_LIBS) /usr/local/lib/

uninstall:
	rm -f /usr/local/bin/$(TARGET)
	rm -f /usr/local/lib/lib*decoder.so
