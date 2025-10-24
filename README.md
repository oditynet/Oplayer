<div align="center">

  <h1>  Oplayer </h1>
</div>

<img src="https://github.com/oditynet/Oplayer/blob/main/screen1.png" height="auto" />

Music player with supports OGG, AIFF, WAV, MP3, FLAC in a console.

Player can only decoding a audio streams. The FLAC and MP3 formats are implemented through the mpg123 package, and the other formats are self-written

Version 0.3.1:
- add pause

Version 0.3:
- add support FLAC

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

Clean:

```
make clean
```

Key navigation:
j - Down
k - Up
n-next music play
p-prev music play
<spice> - Pause / Play
Left/Right array - -10sec / +10sec 

