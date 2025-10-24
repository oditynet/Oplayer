<div align="center">

  <h1>  Oplayer </h1>
</div>

<img src="https://github.com/oditynet/Oplayer/blob/main/screen1.png" height="auto" />

Music player with supports OGG,AIFF,WAV, MP3 in a console.

Player can only decoding a audio streams.

Version 0.2:
- add console GUI
- add navigation, loop play

Build prepare:
```
# Ubuntu/Debian
sudo apt-get install libmpg123-dev

# CentOS/RHEL
sudo yum install mpg123-devel

# macOS (brew)
brew install mpg123
```

Build:

```
make
```

TODO: Remove the last seconds of sound noise


Tests Multi (you can rewind music)
```
gcc -o oplayer_multi oplayer_multi.c.c -lpulse-simple -lpulse -lpthread -ldl
```


