# Groove Basin

Music player server with a web-based user interface inspired by Amarok 1.4.

Run it on a server (such as a 
[Raspberry Pi](http://www.raspberrypi.org/)) connected to some speakers
in your home or office. Guests can control the music player by connecting
with a laptop, tablet, or smart phone. Further, you can stream your music
library remotely.

Groove Basin works with your personal music library; not an external music
service. Groove Basin will never support DRM content.

Groove Basin is undergoing heavy restructuring. Some features are disabled
while they are being rebuilt.

Live discussion in #libgroove on Freenode.

## Features

* Fast, responsive UI. It feels like a desktop app, not a web app.

* Dynamic playlist mode which automatically queues random songs, favoring
  songs that have not been queued recently.

* Drag and drop upload. Drag and drop playlist editing. Rich keyboard
  shortcuts.

* Lazy multi-core
  [EBU R128 loudness scanning](http://tech.ebu.ch/loudness) (tags compatible
  with [ReplayGain](http://wiki.hydrogenaudio.org/index.php?title=ReplayGain_1.0_specification))
  and automatic switching between track and album mode.

* Streaming support. You can listen to your music library - or share it with
  your friends - even when you are not physically near your home speakers.

* Last.fm scrobbling.

* (work in progress) Supports the MPD protocol

## Install

Groove Basin is undergoing heavy restructuring. When this gets to a good
checkpoint, we will cut a release. Until then, use these instructions to
run from source:

1. Make sure you have the latest stable [Node.js](http://nodejs.org) installed.
2. Install [libgroove](https://github.com/andrewrk/libgroove).
3. Clone the source.
4. `npm run build`
5. `npm start`

## Screenshots

![Search + drag/drop support](http://superjoesoftware.com/temp/groove-basin-0.0.4.png)
![Multi-select and context menu](http://superjoesoftware.com/temp/groove-basin-0.0.4-lib-menu.png)
![Keyboard shortcuts](http://superjoesoftware.com/temp/groove-basin-0.0.4-shortcuts.png)
![Last.fm Scrobbling](http://superjoesoftware.com/temp/groove-basin-0.0.4-lastfm.png)

## Configuration

When Groove Basin starts it will look for `config.js` in the current directory.
If not found it creates one for you with default values.

## Developing

```
$ npm run dev
```

## Release Notes

### 0.2.0 (Oct 16 2012)

* Andrew Kelley:
  * ability to import songs by pasting a URL
  * improve build and development setup
  * update style to not resize on selection. closes #23
  * better connection error messages. closes #21
  * separate [mpd.js](https://github.com/andrewrk/mpd.js) into an open source module. closes #25
  * fix dynamicmode; use higher level sticker api. closes #22
  * search uses ascii folding so that 'jonsi' matches 'Jónsi'. closes #29
  * server restarts if it crashes
  * server runs as daemon
  * server logs to rotating log files
  * remove setuid feature. use authbind if you want to run as port 80
  * ability to download albums and artists as zip. see #9
  * ability to download arbitrary selection as zip. closes #9
  * fix track 08 and 09 displaying as 0. closes #65
  * fix right click for IE
  * better error reporting when state json file is corrupted
  * log chats
  * fix edge case with unicode characters. closes #67
  * fix next and previous while stopped behavior. closes #19
  * handle uploading errors. fixes #59
  * put link to stream URL in settings. closes #69
  * loads faster and renders faster
  * send a 404 when downloading can't find artist or album. closes #70
  * read-only stored playlist support
  * fix playlist display when empty
  * add uploaded songs to "Incoming" playlist. closes #80
  * fix resize weirdness when you click library tab. closes #75
  * don't bold menu option text
  * add color to the first part of the track slider. closes #15
* Josh Wolfe:
  * fix dynamic mode glitch
  * fix dynamic mode with no library or no tags file
  * uploading with mpd <0.17 falls back to upload name


### 0.1.2 (Jul 12 2012)

* Andrew Kelley:
  * lock in the major versions of dependencies
  * more warnings about mpd conf settings
  * remove "alert" text on no connection
  * better build system
  * move dynamic mode configuration to server
  * server handles permissions in mpd.conf correctly
  * clients can set a password
  * ability to delete from library
  * use soundmanager2 instead of jplayer for streaming
  * buffering status on stream button
  * stream button has a paused state
  * use .npmignore to only deploy generated files
  * update to work with node 0.8.2
* Josh Wolfe:
  * pointing at mpd's own repository in readme. #12
  * fixing null pointer error for when streaming is disabled
  * fixing blank search on library update
  * fixing username on reconnect
  * backend support for configurable dynamic history and future sizes
  * ui for configuring dynamic mode history and future sizes
  * coloring yourself different in chat
  * scrubbing stale user ids in my_user_ids
  * better chat name setting ui
  * scrolling chat window properly
  * moar chat history
  * formatting the state file
  * fixing chat window resize on join/left
  * validation on dynamic mode settings
  * clearer wording in Get Started section and louder mpd version dependency
    documentation

### 0.0.6 (Apr 27 2012)

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


### 0.0.5 (Mar 11 2012)

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

### 0.0.4 (Mar 6 2012)

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

### 0.0.3 (Mar 4 2012)

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

### 0.0.2 (Mar 1 2012)

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
