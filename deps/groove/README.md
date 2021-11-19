# libgroove

This library provides decoding and encoding of audio on a playlist. It is
intended to be used as a backend for music player applications. That said, it is
also generic enough to be used as a backend for any streaming audio processing
utility.

## Features

* Uses [ffmpeg](http://ffmpeg.org/) for robust decoding and encoding. See
  [supported file formats and codecs](http://ffmpeg.org/ffmpeg-formats.html).
* Add and remove entries on a playlist for gapless playback.
* Supports idempotent pause, play, and seek.
* Per-playlist-item gain adjustment so you can implement loudness compensation
  without audio glitches.
* Read and write metadata tags.
* Choose between smooth mode and exact mode during playback.
  * **smooth mode** - open the audio device once and resample everything to
    fit that sample rate and format.
  * **exact mode** - open and close the audio device as necessary in effort
    to open the audio device with parameters matching the incoming audio data.
* Extensible sink-based interface. A sink provides resampling and keeps its
  buffer full. Types of sinks:
  * **raw sink** - Provides reference-counted raw audio buffers you can do
    whatever you like with. For example a real-time audio visualization. All
    other sink types are built on top of this one.
  * **player sink** - Sends frames to a sound device.
  * **encoder sink** - Provides encoded audio buffers. For example, you could
    use this to create an HTTP audio stream.
  * **loudness scanner sink** - Uses the [EBU R 128](http://tech.ebu.ch/loudness)
    standard to detect loudness. The values it produces are compatible with the
    [ReplayGain](http://wiki.hydrogenaudio.org/index.php?title=ReplayGain_1.0_specification)
    specification.
  * **fingerprint sink** - Uses [chromaprint](http://acoustid.org/chromaprint)
    to generate unique song IDs that can be used with the acoustid service.
  * **waveform sink** - Generates a visual representation of a song.
* Example programs included:
  * `playlist` - Play a series of songs with gapless playback.
  * `metadata` - Read or update song metadata.
  * `replaygain` - Report the suggested replaygain for a set of files.
  * `transcode` - Transcode one or more files into one output file.
  * `fingerprint` - Generate acoustid fingerprints for one or more files.
  * `metadata_checksum` - Read or update song metadata. This program scans the
    audio of the file before the metadata change, changes the metadata in a
    temporary file, scans the audio of the temporary file to make sure it
    matches the original, and then atomically renames the temporary file over
    the original file.

## Building From Source

Dependencies:

* [cmake](http://www.cmake.org/) >= 2.8.5
* [ffmpeg](http://ffmpeg.org/) >= 3.0
  * suggested flags: `--enable-shared --disable-static --enable-libmp3lame --enable-libvorbis --enable-gpl`
* [libebur128](https://github.com/jiixyj/libebur128)
  * make sure it is compiled with the speex dependency so that true peak
    functions are available.
* [libsoundio](http://libsound.io/)
* [libchromaprint-dev](http://acoustid.org/chromaprint)

```
mkdir build
cd build
cmake ..
make
sudo make install
```

## Documentation

[API Reference](http://andrewrk.github.io/libgroove/)

Join #libgroove on irc.freenode.org and ask questions.

To build the documentation:

```
make doc
```

## Projects Using libgroove

Feel free to make a pull request adding yours to this list.

* [Groove Basin](https://github.com/andrewrk/groovebasin) is a music player with
  lazy multi-core replaygain scanning, a web interface inspired by Amarok 1.4,
  http streaming, upload, download and a dynamic playlist mode.
* [waveform](https://github.com/andrewrk/waveform) generates PNG waveform
  visualizations.
* [node-groove](https://github.com/andrewrk/node-groove) provides
  [Node.js](http://nodejs.org/) bindings to libgroove.
* [playa](https://github.com/moonwave99/playa) OS X Audio Player that thinks
  in albums.
* [groove-rs](https://github.com/andrewrk/groove-rs) provides
  [rust](http://rust-lang.org) bindings to libgroove.
* [ruby-groove](https://github.com/johnmuhl/ruby-groove) provides Ruby FFI
  bindings to libgroove.
* [TrenchBowl](https://github.com/andrewrk/TrenchBowl) is a simple Qt GUI
  on top of libgroove.

## Upgrading FFmpeg

First run `./configure --disable-x86asm --enable-libmp3lame` in the new FFmpeg
source directory, to generate config.h. Next, delete all the files is
`deps/ffmpeg/*` and replace them with the files from FFmpeg source directory.
Then:

```
cd deps/ffmpeg
rm -rf ./*
cp -r ~/Downloads/ffmpeg/* ./
rm -rf $(find -name .gitignore) $(find -name tests -type d) $(find -name "*.version") Makefile configure doc/ ffbuild/
```

Finally look at the diff to config.h and fix it.
