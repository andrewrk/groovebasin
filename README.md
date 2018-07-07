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

## Installation on Ubuntu

For Ubuntu 17.04 Zesty:

1. `sudo apt-get install nodejs libgrooveloudness-dev libgroovefingerprinter-dev libgrooveplayer-dev libgroove-dev`
2. Clone this repo and cd to it.
3. `npm run build`
4. `npm start`

For Ubuntu 18.04 Bionic:

* Install node-groove and its dependencies from source by following these instructions:
  https://github.com/andrewrk/node-groove/blob/2.x/README.md#ubuntu-1804
* Edit `pckage.json`, and change the `"groove"` dependency to point to the directory where node-groove is installed.
  (The path is instead of a version number.)
* Resume step 2 above.

## Configuration

When Groove Basin starts it will look for `config.json` in the current
directory. If not found it creates one for you with default values.

Use this to set your music library location and other settings.

It is recommended that you generate a self-signed certificate and use that
instead of using the public one bundled with this source code.

## Screenshots

![Search + drag/drop support](http://groovebasin.com/img/groovebasin-1.3.2-searchdragdrop.png)
![Multi-select and context menu](http://groovebasin.com/img/groovebasin-1.3.2-libmenu.png)
![Keyboard shortcuts](http://groovebasin.com/img/groovebasin-1.3.2-shortcuts.png)
![Settings](http://groovebasin.com/img/groovebasin-1.3.2-settings.png)
![Import](http://groovebasin.com/img/groovebasin-1.3.2-import.png)
![Events](http://groovebasin.com/img/groovebasin-1.3.2-events.png)

## Developing

```
$ npm run dev
```

This will install dependencies, build generated files, and then start the
sever. It is up to you to restart it when you modify assets or server files.

### Community

Pull requests, feature requests, and bug reports are welcome!
Live discussion in #libgroove on Freenode.

#### Articles

 * [My Quest to Build the Ultimate Music Player](http://andrewkelley.me/post/quest-build-ultimate-music-player.html)
 * [Turn Your Raspberry Pi into a Music Player Server](http://andrewkelley.me/post/raspberry-pi-music-player-server.html)

### Roadmap

 0. Music library organization
 0. Accoustid Integration
 0. Finalize GrooveBasin protocol spec
