
<div align="center">
  <h1>  Oplayer </h1>
  
</div>

<img alt="GitHub code size in bytes" src="https://img.shields.io/github/languages/code-size/oditynet/Oplayer"></img>
<img alt="GitHub license" src="https://img.shields.io/github/license/oditynet/Oplayer"></img>
<img alt="GitHub commit activity" src="https://img.shields.io/github/commit-activity/m/oditynet/Oplayer"></img>
<img alt="GitHub Repo stars" src="https://img.shields.io/github/stars/oditynet/Oplayer"></img>

<img src="https://github.com/oditynet/Oplayer/blob/main/screen2.png" height="auto" />

## 📋 I can...

Music player with supports OGG, AIFF, WAV, MP3, FLAC in a console and Docker

Player can only decoding a audio streams. The FLAC and MP3 formats are implemented through the mpg123 package, and the other formats are self-written

Version 0.4:
- add radio ( mplayer )

Version 0.3.1:
- add pause
- add volume
- add mute

Version 0.3:
- add support FLAC

Version 0.2:
- add console GUI
- add navigation, loop play

## 🛠️ Prepare 

Build prepare:
```
# Astra linux
sudo apt-get install libmpg123-devel libflac-devel libvorbis-devel gcc  libpulseaudio-devel  pulseaudio mplayer

# Pacman
yay -S mpg123-dev mplayer
```

Build:

```
make
```

Clean:

```
make clean
```

Key navigation:
- j - Down
- k - Up
- n - next music play
- p - prev music play
- spice - Pause / Play
- Left/Right array - -10sec / +10sec
- +/- - Volume
- m - mute
- e - radio

# Docker
 /home/odity/Music - replace to your
```
docker build -t oplayer .

docker run -it --rm \
    -v /home/odity/Music:/app/music \
    -v /run/user/$(id -u)/pulse:/run/user/1000/pulse \
    -e PULSE_RUNTIME_PATH=/run/user/1000/pulse \
    oplayer
```

If you have a problem with network in Docker, then used:  --network=host

Out:

```
 odity@viva  /tmp/Oplayer   main ± docker build -t oplayer .                                                                              
[+] Building 1.9s (11/11) FINISHED                                                                                                                             docker:default
 => [internal] load build definition from Dockerfile                                                                                                                     0.0s
 => => transferring dockerfile: 811B                                                                                                                                     0.0s
 => [internal] load metadata for docker.io/library/alt:latest                                                                                                            1.7s
 => [auth] library/alt:pull token for registry-1.docker.io                                                                                                               0.0s
 => [internal] load .dockerignore                                                                                                                                        0.0s
 => => transferring context: 2B                                                                                                                                          0.0s
 => [1/6] FROM docker.io/library/alt:latest@sha256:a32f2a8a3aaa0bde41263bbdf9fd23264fc846feb280f5adb961ab4f34e5f5c5                                                      0.0s
 => CACHED [2/6] RUN apt-get update &&     apt-get install -y     git     gcc     make     libmpg123-devel libflac-devel libvorbis-devel gcc  libpulseaudio-devel  puls  0.0s
 => CACHED [3/6] RUN git clone https://github.com/oditynet/Oplayer.git /app                                                                                              0.0s
 => CACHED [4/6] WORKDIR /app                                                                                                                                            0.0s
 => CACHED [5/6] RUN make                                                                                                                                                0.0s
 => CACHED [6/6] RUN useradd -m -u 1000 audioapp &&     chown -R audioapp:audioapp /app                                                                                  0.0s
 => exporting to image                                                                                                                                                   0.0s
 => => exporting layers                                                                                                                                                  0.0s
 => => writing image sha256:4bc921cba82893548f6bfc525f4b52ad1076a513c4649116a489495e929b264c                                                                             0.0s
 => => naming to docker.io/library/oplayer   
```
