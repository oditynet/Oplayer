FROM alt:latest

# Устанавливаем зависимости
RUN apt-get update && \
    apt-get install -y \
    git \
    gcc \
    make \
    libmpg123-devel libflac-devel libvorbis-devel gcc  libpulseaudio-devel  pulseaudio mplayer\
    && rm -rf /var/lib/apt/lists/*

# Клонируем репозиторий
RUN git clone https://github.com/oditynet/Oplayer.git /app

WORKDIR /app

# Компилируем проект через make
RUN make

# Создаем пользователя для безопасности
RUN useradd -m -u 1000 audioapp && \
    chown -R audioapp:audioapp /app
USER audioapp

# Создаем точку монтирования для музыки
VOLUME /app/music

# Запускаем программу
CMD ["./audio_player"]
