# ![Groove Basin](http://groovebasin.com.s3.amazonaws.com/img/logo-text.png)

Music player server with a web-based user interface.

Run it on a server connected to some speakers in your home or office.
Guests can control the music player by connecting with a laptop, tablet,
or smart phone. Further, you can stream your music library remotely.

Groove Basin works with your personal music library; not an external music
service. Groove Basin will never support DRM content.

## Feature Highlights

* The web client feels like a desktop app, not a web app. It predicts what the
  server will do in order to hide network lag from the user.

* Auto DJ which automatically queues random songs, favoring songs
  that have not been queued recently.

* Drag and drop upload. Drag and drop playlist editing. Keyboard shortcuts
  for everything.

* Lazy multi-core
  [EBU R128 loudness scanning](http://tech.ebu.ch/loudness) (tags compatible
  with [ReplayGain](http://wiki.hydrogenaudio.org/index.php?title=ReplayGain_1.0_specification))
  and automatic switching between track and album mode.
  ["Loudness Zen"](http://www.youtube.com/watch?v=iuEtQqC-Sqo)

* Streaming support. You can listen to your music library - or share it with
  your friends - even when you are not physically near your home speakers.

* Groove Basin protocol. Write your own client using the
  [protocol specification](doc/protocol.md), or check out
  [gbremote](https://github.com/andrewrk/gbremote), a simple command-line
  remote control.

* MPD protocol support. This means you already have a selection of
  [clients](http://mpd.wikia.com/wiki/Clients) which integrate with
  Groove Basin. For example [MPDroid](https://github.com/abarisain/dmix).

* [Last.fm](http://www.last.fm/) scrobbling.

* File system monitoring. Add songs anywhere inside your music directory and
  they instantly appear in your library.

## Installation

This project is being actively developed, so the installation instructions are
the same as the development instructions.

First, [download zig master branch](https://ziglang.org/download/#release-master).
Then you can use this command to build and run the project:

```
zig build run
```

Have a look at `zig build --help` for more options.

## Configuration

When Groove Basin starts it will look for `config.json` in the current
directory. If not found it creates one for you with default values.

Use this to set your music library location and other settings.

## Screenshots

![Search + drag/drop support](https://s3.amazonaws.com/groovebasin.com/img/groovebasin-1.3.2-searchdragdrop.png)
![Multi-select and context menu](https://s3.amazonaws.com/groovebasin.com/img/groovebasin-1.3.2-libmenu.png)
![Keyboard shortcuts](https://s3.amazonaws.com/groovebasin.com/img/groovebasin-1.3.2-shortcuts.png)
![Settings](https://s3.amazonaws.com/groovebasin.com/img/groovebasin-1.3.2-settings.png)
![Import](https://s3.amazonaws.com/groovebasin.com/img/groovebasin-1.3.2-import.png)
![Events](https://s3.amazonaws.com/groovebasin.com/img/groovebasin-1.3.2-events.png)

## Developing

```
zig build run
```

This will install dependencies, build generated files, and then start the
sever. It is up to you to restart it when you modify assets or server files.

### Community

Pull requests, feature requests, and bug reports are welcome!

#### Articles

 * [My Quest to Build the Ultimate Music Player](http://andrewkelley.me/post/quest-build-ultimate-music-player.html)

### Roadmap

* oh god my ears, please let me turn it down
* basic playback QA: play/pause/stop/next/prev/seek
* the web server hangs doesn't serve the request fast
* replaygain
* rearranging queue items
* Give the server access to music library index (what's an album, list of
  artists, etc)
* Auto DJ
* File system rescan button
* Delete from library
* Edit tags
* Playlist support
* File system watching
* Uploading files
* repeat mode
* Torrent integration
* Last.fm scrobbling
* Acoustid Integration
* Zip file upload and download
* Finalize GrooveBasin protocol spec
