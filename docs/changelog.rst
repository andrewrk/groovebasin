Changelog
=========

1.0.1 (March 18, 2014)
----------------------

.. What does this mean? 
* Default import path includes artist directory.
* Groove Basin now recognizes the `TCMP`_ ID3 tag as a compilation album flag. 

.. _TCMP: http://id3.org/iTunes%20Compilation%20Flag

Fixes:

* Fixed Last.fm authentication.
* Fixed a race condition when removing tracks from playlist.

1.0.0 (March 15, 2014)
----------------------

In the 1.0.0 release, Groove Basin has removed its dependency on MPD, using 
`libgroove`_ for audio playback and streaming support. Groove Basin is also not 
written in `coco`_ anymore. Hopefully this will encourage more contributors to
join the project!

Major features include `ReplayGain`_ style automatic loudness detection using the 
`EBU R128`_ recommendation. Scanning takes place on the fly, taking advantage of 
multi-core systems. Groove Basin automatically switches between album and track 
mode depending on the next item in the play queue.

Chat and playlist functionality have been removed as they are not quite ready 
yet. These features will be reimplemented better in a future release.

.. _libgroove: https://github.com/andrewrk/libgroove
.. _coco: https://github.com/satyr/coco
.. _ReplayGain: https://en.wikipedia.org/wiki/ReplayGain
.. _EBU R128: https://tech.ebu.ch/loudness

Other features:

* Groove Basin now functions as an MPD server. MPD clients can connect to port 
  6600 by default.
* The config file is simpler and should survive new version releases.
* Client and server communications now use a simpler and more efficient protocol.
* Rebuilding the album index is faster.
* The HTTP audio stream buffers much more quickly and flushes the buffer on seek.
* Streaming shows when it is buffering.
* The web UI now specifies a `UTF-8`_ character set.
* Groove Basin's music library now updates automatically by watching the music 
  folder for changes.
* HTTP streaming now uses native HTML5 audio, instead of `SoundManager 2`_
* `jQuery`_ and `jQuery UI`_ have been updated to the latest stable version, fixing
  some UI glitches.
* Static assets are gzipped and held permanently in memory, making the web 
  interface load faster.
* Now routing Dynamic mode through the permissions framework.
* Better default password generation.

.. _UTF-8: https://en.wikipedia.org/wiki/UTF-8
.. _SoundManager 2: http://www.schillmania.com/projects/soundmanager2/
.. _jQuery: https://jquery.com/
.. _jQuery UI: https://jqueryui.com/

Fixes:

* Fixed a regression for handling unknown artists or albums.
* Fixed play queue to display the artist name of tracks.
* Plugged an upload security hole.
* Pressing the previous track button on the first track in the play queue when
  "repeat all" is turned on now plays the last track in the play queue.
* The volume widget no longer goes higher than 100%.
* Changing the volume now shows up on other clients.
* The volume keyboard shortcuts now work in Firefox.
* Ensured that no-cache headers are set for the stream.
* Fixed an issue in the Web UI where the current track was sometimes not 
  displayed.

Thanks to Josh Wolfe, who worked to fix some issues around deleting library
items, ensuring that deleting library items removes them from the play queue, 
and that the play queue correctly reacts to deleted library entries.

In addition, he worked to:

* Convert Groove Basin to not use MPD.
* fix multiselect shiftIds.
* fix shift click going up in the queue. 
.. What does this mean?


0.2.0 (October 16, 2012)
-------------------------

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


0.1.2 (July 12, 2012)
---------------------

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

0.0.6 (April 27, 2012)
----------------------

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


0.0.5 (March 11, 2012)
----------------------

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

0.0.4 (March 6, 2012)
---------------------

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

0.0.3 (March 4, 2012)
---------------------

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

0.0.2 (March 1, 2012)
-------------------------

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
