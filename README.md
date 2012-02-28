# Groove Basin

No-nonsense music client and server for your home or office.

Run it on a server connected to your main speakers. Guests can connect with
their laptops, tablets, and phones, and play and share music.

Depends on [mpd](http://musicpd.org) for the backend. Some might call this
project an mpd client.

## Features

* Lightning-fast, responsive UI. You can hardly tell that the music server is
  on another computer.

* Dynamic playlist mode which automatically queues random songs, favoring
  songs that have not been played recently.

* Drag and drop upload. Drag and drop playlist editing. Rich keyboard
  shortcuts.

* Streaming support. You can listen to your music library - or share it with
  your friends - even when you are not physically near your home speakers.

## Get Started

Make sure you have [Node](http://nodejs.org) and [npm](http://npmjs.org)
installed, then:

```
$ npm install groovebasin
$ npm start groovebasin
```

At this point, Groove Basin will issue warnings telling you what to do next.

## Screenshots

![Searching library](http://www.superjoesoftware.com/temp/groove-basin-5.png)
![Multi-select and context menu](http://www.superjoesoftware.com/temp/groove-basin-2.png)
![Drag/drop support](http://www.superjoesoftware.com/temp/groove-basin-3.png)
![Keyboard shortcuts](http://www.superjoesoftware.com/temp/groove-basin-4.png)
![Drag and drop / multiselect upload](http://www.superjoesoftware.com/temp/groove-basin-1.png)

## Mpd

Groove Basin depends on [mpd](http://musicpd.org).

Some new features are only available when you compile from source:

```
$ git clone git://git.musicpd.org/master/mpd.git
```

### Configuration

* `audio_output` - Uncomment the "httpd" one and configure the port to enable
  streaming. Recommended "vorbis" encoder for better browser support.

* `sticker_file` - Groove Basin will not run without one set.

* `gapless_mp3_playback` - "yes" recommended. <3 gapless playback.

* `volume_normalization` - "yes" recommended. Replaygain scanners are not
  implemented for all the formats that can be played back. Volume normalization
  works on all formats.

* `max_command_list_size` - "16384" recommended. You do not want mpd crashing
  when you try to remove a ton of songs from the playlist at once.

* `auto_update` - "yes" recommended. Required for uploaded songs to show up
  in your library.

## Configuring Groove Basin

See http://npmjs.org/doc/config.html

See the "config" section of `package.json` for configuration options and
defaults.

## Developing

```
$ sudo npm link
$ make watch
$ npm -g start groovebasin
```
