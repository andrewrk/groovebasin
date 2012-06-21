# Groove Basin

No-nonsense music client and server for your home or office.

Run it on a server connected to your main speakers. Guests can connect with
their laptops, tablets, and phones, and play and share music.

Depends on [mpd](http://musicpd.org) version 0.17+ for the backend. Some might
call this project an mpd client. (Note, version 0.17 is only available from
source as of writing this; see below instructions regarding mpd installation.)

Live demo: [groovebasin.com](http://groovebasin.com/)

## Features

* Lightning-fast, responsive UI. You can hardly tell that the music server is
  on another computer.

* Dynamic playlist mode which automatically queues random songs, favoring
  songs that have not been played recently.

* Drag and drop upload. Drag and drop playlist editing. Rich keyboard
  shortcuts.

* Streaming support. You can listen to your music library - or share it with
  your friends - even when you are not physically near your home speakers.

* Last.fm scrobbling.

## Get Started

Make sure you have [Node](http://nodejs.org) and [npm](http://npmjs.org)
installed, then:

```
$ npm install groovebasin
$ npm start groovebasin
```

At this point, Groove Basin will issue warnings telling you what to do next.

## Screenshots

![Search + drag/drop support](http://www.superjoesoftware.com/temp/groove-basin-0.0.4.png)
![Multi-select and context menu](http://www.superjoesoftware.com/temp/groove-basin-0.0.4-lib-menu.png)
![Keyboard shortcuts](http://www.superjoesoftware.com/temp/groove-basin-0.0.4-shortcuts.png)
![Last.fm Scrobbling](http://www.superjoesoftware.com/temp/groove-basin-0.0.4-lastfm.png)

## Mpd

Groove Basin depends on [mpd](http://musicpd.org) version 0.17+.

To compile from source, start here

```
$ git clone git://git.musicpd.org/master/mpd.git
```

and follow mpd's instructions from there.

### Configuration

* `default_permissions` - Recommended to remove `admin` so that anonymous
  users can't do nefarious things.

* `password` - Recommended to add a password for yourself to give yourself `admin` permissions.

   * `read` - allows reading the library, current playlist, and playback status.

   * `add` - allows adding songs, loading playlists, and uploading songs. 

   * `control` - allows controlling playback state and manipulating playlists.

   * `admin` - allows updating the db, killing mpd, deleting songs from the
     library, and updating song tags.

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

See http://npmjs.org/doc/config.html#Per-Package-Config-Settings

See the "config" section of `package.json` for configuration options and
defaults.

Example:

```
$ npm config set groovebasin:mpd_conf ~/.mpd/mpd.conf
$ npm config set groovebasin:port 80
$ npm -g --groovebasin:port 80 start groovebasin
```

## Developing

```
$ npm test --groovebasin:development_mode true
```

## Release Notes

### 0.0.6

* Josh Wolfe:
  * fixing not queuing before random when pressing enter in the search box
  * fixing streaming hotkey not updating button ui
  * stopping and starting streaming in sync with mpd.status.state.
  * fixing weird bug with Stream button checked state
  * warning when bind_to_address is not also configured for localhost
  * fixing derpy log reference
  * fixing negative trackNumber scrobbling
  * directory urls download .zip files. #9
  * document dependency on mpd version 0.17
* Andrew Kelley:
  * fix regression: not queuing before random songs client side
  * uploaded songs are queued in the correct place
  * support restarting mpd without restarting daemon
  * ability to reconnect without refreshing
  * log.info instead of console.info for track uploaded msg
  * avoid the use of 'static' keyword
* David Banham:
  * Make jPlayer aware of which stream format is set
  * Removed extra constructor. Changed tabs to 2spaces


### 0.0.5

* Note: Requires you to pull from latest mpd git code and recompile.
* Andrew Kelley:
  * disable volume slider when mpd reports volume as -1. fixes #8
  * on last.fm callback, do minimal work then refresh. fixes #7
  * warnings output the actual mpd.conf path instead of "mpd conf". see #5
  * resize things *after* rendering things. fixes #6
  * put uploaded files in an intelligent place, and fix #2
  * ability to retain server state file even when structure changes
  * downgrade user permissions ASAP
  * label playlist items upon status update
  * use blank user_id to avoid error message
  * use jplayer for streaming
* Josh Wolfe:
  * do not show ugly "user_n" text after usernames in chat.

### 0.0.4

* Andrew Kelley:
  * update keyboard shortcuts dialog
  * fix enter not queuing library songs in firefox
  * ability to authenticate with last.fm, last.fm scrobbling
  * last.fm scrobbling works
  * fix issues with empty playlist. fixes #4
  * fix bug with dynamic mode when playlist is clear
* Josh Wolfe:
  * easter eggs
  * daemon uses a state file

### 0.0.3

* Andrew Kelley:
  * ability to select artists, albums, tracks in library
  * prevents sticker race conditions from crashing the server (#3)
  * escape clears the selection cursor too
  * ability to shift+click select in library
  * right-click queuing in library works
  * do not show download menu option since it is not supported yet
  * show selection on expanded elements
  * download button works for single tracks in right click library menu
  * library up/down to change selection
  * nextLibPos/prevLibPos respects whether tree items are expanded or collapse
  * library window scrolls down when you press up/down to move selection
  * double click artists and albums in library to queue
  * left/right expands/collapses library tree when lib has selection
  * handle enter in playlist and library
  * ability to drag artists, albums, tracks to playlist
* Josh Wolfe:
  * implement chat room
  * users can set their name in the chat room
  * users can change their name multiple times
  * storing username persistently. disambiguating conflicting usernames.
  * loading recent chat history on connect
  * normalizing usernames and sanitizing username display
  * canot send blank chats
  * supporting /nick renames in chat box
  * hotkey to focus chat box

### 0.0.2

* Andrew Kelley:
  * learn mpd host and port in mpd conf
  * render unknown albums and unknown artists the same in the playlist (blank)
  * auto-scroll playlist window and library window appropriately
  * fix server crash when no top-level files exist
  * fix some songs error message when uploading
  * edit file uploader spinny gif to fit the theme
  * move chat stuff to another tab
* Josh Wolfe:
  * tracking who is online

