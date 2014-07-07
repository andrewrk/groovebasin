# ![Groove Basin](http://groovebasin.com.s3.amazonaws.com/img/logo-text.png)

Music player server with a web-based user interface inspired by Amarok 1.4.

Run it on a server connected to some speakers in your home or office.
Guests can control the music player by connecting with a laptop, tablet,
or smart phone. Further, you can stream your music library remotely.

Groove Basin works with your personal music library; not an external music
service. Groove Basin will never support DRM content.

Try out the [live demo](http://demo.groovebasin.com/).

## Feature Highlights

* Fast and responsive. It feels like a desktop app, not a web app.

* Dynamic Mode which automatically queues random songs, favoring songs
  that have not been queued recently.

* Drag and drop upload. Drag and drop playlist editing. Keyboard shortcuts
  galore.

* Lazy multi-core
  [EBU R128 loudness scanning](http://tech.ebu.ch/loudness) (tags compatible
  with [ReplayGain](http://wiki.hydrogenaudio.org/index.php?title=ReplayGain_1.0_specification))
  and automatic switching between track and album mode.
  ["Loudness Zen"](http://www.youtube.com/watch?v=iuEtQqC-Sqo)

* Streaming support. You can listen to your music library - or share it with
  your friends - even when you are not physically near your home speakers.

* MPD protocol support. This means you already have a selection of
  [clients](http://mpd.wikia.com/wiki/Clients) which integrate with
  Groove Basin. For example [MPDroid](https://github.com/abarisain/dmix).
  If you're writing a new client, upgrade to the Groove Basin Protocol with
  the `protocolupgrade` command.

* [Last.fm](http://www.last.fm/) scrobbling.

* File system monitoring. Add songs anywhere inside your music directory and
  they instantly appear in your library in real time.

## Install

### Pre-Built Packages

#### Ubuntu

```
sudo apt-add-repository ppa:andrewrk/libgroove
sudo apt-get update
sudo apt-get install groovebasin
groovebasin
```

### From Source

1. Install [Node.js](http://nodejs.org) v0.10.x. Note that on Debian and
   Ubuntu, you also need the nodejs-dev and nodejs-legacy packages.  You may
   also choose to use [Chris Lea's PPA](https://launchpad.net/~chris-lea/+archive/node.js/)
   or compile from source.
2. Install [libgroove](https://github.com/andrewrk/libgroove).
3. Clone the source and cd to it.
4. `npm run build`
5. `npm start`

## Configuration

When Groove Basin starts it will look for `config.js` in the current directory.
If not found it creates one for you with default values.

Use this to set your music library location, permissions, and other settings.

## Screenshots

![Search + drag/drop support](http://superjoesoftware.com/temp/groove-basin-0.0.4.png)
![Multi-select and context menu](http://superjoesoftware.com/temp/groove-basin-0.0.4-lib-menu.png)
![Keyboard shortcuts](http://superjoesoftware.com/temp/groove-basin-0.0.4-shortcuts.png)
![Last.fm Scrobbling](http://superjoesoftware.com/temp/groove-basin-0.0.4-lastfm.png)

## Developing

```
$ npm run dev
```

This will install dependencies, build generated files, and then start the
sever. It is up to you to restart it when you modify assets or server files.

### Community

Pull requests, feature requests, and bug reports are welcome! Live discussion
in #libgroove on Freenode.

#### Articles

 * [My Quest to Build the Ultimate Music Player](http://andrewkelley.me/post/quest-build-ultimate-music-player.html)
 * [Turn Your Raspberry Pi into a Music Player Server](http://andrewkelley.me/post/raspberry-pi-music-player-server.html)

### Roadmap

 1. Tag Editing
 2. Music library organization
 3. Accoustid Integration
 4. Playlists
 5. User accounts / permissions rehaul
 6. Event history / chat
 7. Finalize GrooveBasin protocol spec

## Release Notes

See CHANGELOG.md
