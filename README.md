<div align="center">

  <h1>  Oplayer </h1>
</div>

<img src="https://github.com/oditynet/Oplayer/blob/main/screen1.png" height="auto" />

Music player with supports OGG,AIFF,WAV, MP3 in a console.

Player can only decoding a audio streams.

Version 0.2:
- add console GUI
- add navigation, loop play
- bug fix of sound noise

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

Clean:
```
make clean
```

Navigation:
 - j - Down
 - k - Up
 - r - loops mode
 - left/right arrow - +10sec / -10sec
