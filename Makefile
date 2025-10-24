CC = gcc
CFLAGS = -Wall -O2 -fPIC
LDFLAGS = -lpulse-simple -lpulse -ldl -lpthread -lm

# Декодеры
DECODER_LIBS = -lmpg123 -lFLAC -lvorbisfile -lvorbis -logg

all: decoders player

decoders: libwavdecoder.so libmp3decoder.so libflacdecoder.so liboggdecoder.so

libwavdecoder.so: decoders/wav_decoder.c
	$(CC) $(CFLAGS) -shared -o $@ $<

libmp3decoder.so: decoders/mp3_decoder.c
	$(CC) $(CFLAGS) -shared -o $@ $< $(DECODER_LIBS)

libflacdecoder.so: decoders/flac_decoder.c
	$(CC) $(CFLAGS) -shared -o $@ $< $(DECODER_LIBS)

liboggdecoder.so: decoders/ogg_decoder.c
	$(CC) $(CFLAGS) -shared -o $@ $< $(DECODER_LIBS)

player: player.c
	$(CC) $(CFLAGS) -o audio_player $< $(LDFLAGS)

clean:
	rm -f *.so audio_player

install-deps:
	sudo apt-get install libmpg123-dev libflac-dev libvorbis-dev libpulse-dev

.PHONY: all decoders player clean install-deps
