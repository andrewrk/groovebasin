### Version 1.6.0 (UNRELEASED)

Contains breaking changes to the Groove Basin protocol. Since the Groove Basin
protocol is not officially stable yet, only the minor version number is bumped.

 * Andrew Kelley:
   - Users can now add labels to tracks, and labels show up in the play queue.
   - Client: Revert smoother track slider. The choppier track slider is worth
     the CPU cycles it saves.
   - Better SSL defaults.
     - Instead of defaulting to HTTPS with a self-signed certificate, default to
       using HTTP.
     - By default bind to 127.0.0.1 instead of 0.0.0.0.
     - If you bind to something other than localhost without enabling SSL, you
       get a warning message.
     - Users are encouraged to either use Groove Basin with a proxy and SSL
       enabled on that proxy, or enable SSL directly in Groove Basin.
     - MPD server defaults to 127.0.0.1 instead of 0.0.0.0 since the MPD
       protocol is insecure.
   - Add the first draft of the Groove Basin Protocol Specification
   - Groove Basin protocol no longer operates via the MPD protocol. Instead,
     clients must use the HTTP/HTTPS and WebSockets interface.
   - Groove Basin Protocol provides a way to obtain the stream URL.
   - The gbremote command line tool is an example client and can be used as a
     command line remote control for a Groove Basin server. Perfect for setting
     up keyboard shortcuts to control your music.
   - Add `ignoreExtensions` config setting.
   - Client: fix time column being cut off.
   - Recognize more compilation tags.

 * Josh Wolfe:
   - Label support.
   - Implement searching syntax for labels.

 * Caleb Morris:
   - Repeat order is Off, All, One, instead of Off, One, All.

### Version 1.5.1 (2015-05-15)

 * Andrew Kelley:
   - update dependencies
   - update to work with both io.js and node.js v0.12
   - fix error handling logic for attaching/detaching encoder
   - groovebasin protocol: add 'libraryQueue' subscription item. It gives you
     library information about the subset of items that are in the play queue.
   - groovebasin protocol: fix messages not being delimited by newlines.
   - MPD protocol: fix crash when queuing songs
   - add `encodeBitRate` config option defaulting to 256 which is the old
     hardcoded value.
   - add `--dump-users` CLI argument which can recover passwords. Note: next
     version of groove basin will hash passwords. See #434
   - fix the high CPU usage bug. Special thanks to Felipe Sateler for
     diagnosing the cause after all my failed attempts.

### Version 1.5.0 (2015-01-09)

Contains breaking changes to the Groove Basin protocol. Since the Groove Basin
protocol is not officially stable yet, only the minor version number is bumped.

 * Andrew Kelley:
   - playlist support
   - support for MPD stored_playlist commands
   - imported songs automatically added to an Incoming playlist
   - client: double quotes can be used to include spaces in search terms.
   - import by name feature - searches YouTube for the name and imports into
     your library.
   - client: fix security bug where title display was not escaping HTML
   - fix symlink behavior in music library
   - fix spacebar keyboard shortcut in Firefox
   - fix crash when zip files contain non music files
   - rename "Dynamic Mode" to "Auto DJ"
   - rename "Events" pane to "Chat"
   - MPD users show up in events pane, and MPD commands show up as events for
     MPD users.
   - MPD lsinfo command allows '/' to mean root directory. Fixes compatibility
     with gmpc.
   - MPD list command supports a single argument to return all tags of that
     type. Fixes compatibility with gmpc.
   - MPD: when appending tracks with Auto DJ on, insert them before the random
     ones.
   - (breaking change) protocolupgrade UUID has changed
   - (breaking change) slashes and spaces disallowed in user names
   - (breaking change) clear and shuffle commands removed from protocol
   - HTTP redirect to HTTPS on the same port
   - client: clear and shuffle buttons removed from UI
   - client: Ctrl+A to select all. Works on queue, library, and playlists.
   - client: shuffle works on the selection instead of the entire queue
     and also works on playlists and playlist items.
   - client: shuffle context menu item added to playlists and queue.
   - client: ability to click in the empty queue area to get rid of
     context menu
   - client: scroll to cursor instead of selection
   - client: clear selection when focusing search box
   - client: disable context menu items lacking permission
   - client: fix context menu popping up outside document boundary
   - client: fix playlist item movement not anticipating correctly
   - client: 'E' keyboard shortcut to edit tags
   - client: prettier edit tags dialog
   - client: no longer depends on jQuery, jQuery UI, and cssreset. The code is
     now smaller, less buggy, and more performant.
   - client: fix not all DOM elements resizing correctly
   - client: `>` and `<` keyboard shortcuts require pressing shift
   - client: ability to show keyboard shortcuts from settings
   - client: add to playlist UI allows better keyboard interaction
   - fix crash when encountering paid YouTube videos
   - fix not respecting verbose logging for play queue
   - `--config` option so config file can be in a different place
   - db path is relative to config file instead of CWD
   - save `playCount` in the db
   - make importing files more efficient by disabling fs watching during
   - server handles many commands more efficiently
   - use `xdg-user-dir` if available for default music directory
   - add `--delete-all-events` CLI argument
   - chat: invalid commands do not get cleared or sent
   - fix MPD/groovebasin ID mapping memory leak
   - groovebasin protocol has stricter and safer argument parsing, resulting in
     several security vulnerabilities fixed.
   - deleting multiple tracks from the library is more efficient and results in
     `O(1)` messages to connected clients instead of `O(n)`.
   - ability to add SSL CA certificates
   - better shuffle behavior
     * When Auto DJ is on, it re-rolls all the random songs without
       interrupting history or current track.
     * When Auto DJ is off, it preserves sort keys; just shuffling them around.
   - rescan command deletes mtimes in the db instead of relying on a flag in
     memory. This way if you kill the server in the middle of a rescan, it
     picks up where it left off.
   - add `--spawn`, `--print-url`, and `--start` command line options
   - client: smoother track slider
   - client: improved page performance due to no longer gzipping assets which
     are already compressed.

 * Melissa Noelle:
   - chat: support /me events
   - chat: convert URLs to links
   - client: style the slider widget

 * Josh Wolfe:
   - sort keys for queueing up large number of songs at once requires
     `O(n*log(n))` size over the network instead of `O(n^2)`.
   - play queue total never displays a negative number and shows '?' if some
     tracks need scanning.

 * moshev
   - Fixed a regression in uploading.

### Version 1.4.0 (2014-10-16)

 * Andrew Kelley:
   - client: fix showing filter without filtered results when server restarts
   - fix auto pause behavior and add event for it
   - fix symlink behavior in music library
   - import by url: respect content-disposition header
   - fix serving invalid content-disposition header
   - no longer accidentally shipping config.json in npm module
   - uploaded files are imported in a streaming fashion instead of after all
     files are finishing uploading.
   - fix an uploading crash
   - ability to import and upload .zip files.
   - auto queue happens server side.
   - play queue displays total duration and selection duration
   - add progress reporting for ongoing imports
   - fix aborted uploads getting stuck
   - Remove the easter eggs. It was fun while it lasted. Maybe someday we will
     live in a society where nothing is copyrighted.
   - add Cache-Control header to static assets to help enforce caching rules.

 * Josh Wolfe:
   - fix crash when uploading 0 byte .zip file

 * Felipe Sateler:
   - open stream and homepage links in new tabs/windows

 * Melissa Noelle:
   - client supports /nick command to change name

### Version 1.3.2 (2014-10-06)

 * Andrew Kelley:
   - style: fix messed up menus and volume slider from upgrading jquery ui
   - config file is config.json instead of config.js

### Version 1.3.1 (2014-10-03)

 * Andrew Kelley:
   - update to jquery 2.1.1; include unminified source
   - correctly report error when fail to parse config
   - use cssreset source instead of minified file
   - update to jquery ui 1.11.1; include source

### Version 1.3.0 (2014-10-03)

 * Andrew Kelley:
   - if songs have no track numbers then never use album loudness
   - fix YouTube import
   - fix streaming not pausing and playing reliably
   - fix glitch in streaming when resuming after a long pause
   - add client side volume slider
   - use SSL by default with a public self signed cert
   - import URL allows downloading from https with invalid certs
   - replace uuid dependency with a simpler, faster, and more robust
     random string
   - rewrite user login and permissions support. MPD users can log in with
     (username) + '/' + (password)
   - user accounts and permissions are managed via the browser interface
     instead of with the configuration file
   - add events tab which tells what actions have happened recently, supports
     chat, and displays which users are streaming
   - fix permissions checking for downloading anonymous requests
   - remove 'l' hotkey for library and add 'e' hotkey for settings
   - rename legacy protocol message names
   - quieter log by default; ability to run with --verbose
   - fix bug where all files on play queue would be preloaded; now only the
     next and previous few files are preloaded
   - client: shift+delete only attempts to delete tracks when you have the
     necessary permissions
   - stream endpoint obeys permission settings
   - stream count is number of logged in users with an activated stream button
     plus number of anonymous users connected to the http endpoint
   - auto-pause is now instant instead of half second timer
   - client: fix cutting/pasting text filter box behavior
   - fix crash when removing a nested directory in the music directory
   - client: fix player preduction when currently playing track is removed
   - fix handling of slashes when importing from YouTube
   - client: disable hardware playback toggle button when not admin
   - cut the client javascript bundle size in half
   - build: /bin/sh instead of /bin/bash

 * Josh Wolfe:
   - implement and switch to more robust zip generating module. Fixes unicode
     file names in zips and enables the download progress bar.
   - implement and switch to simpler object diffing module. Reduces client-side
     JavaScript bundle size as well as bandwidth needed to stay connected to
     Groove Basin when other users are making edits.
   - Multi-file downloads use a GET request. This lets you copy a download URL
     which downloads multiple files to the clipboard.

 * David Renshaw:
   - Fix crash when import URL fails to download.

### Version 1.2.1 (2014-07-04)

 * Andrew Kelley:
   - fix ytdl-core version locking. Fixes YouTube import.

### Version 1.2.0 (2014-07-04)

 * Andrew Kelley:
   - client uses relative stream URL so reverse proxies can work.
   - client uses wss if protocol is https.
   - client UI indicates how many people are streaming
   - automatically pause when last streamer disconnects
   - client: remove dotted outline of links.
   - uploading is permission add, not control
   - rename the Upload tab to the Import tab
   - fix not being able to see client with anonymous read-only permissions set
   - fix library scan errors deleting songs from database.
   - streaming: less chance of glitches
   - streaming: no hiccup sound on skip

 * Josh Wolfe:
   - fix unable to download songs with hashtags in the URL
     (but first, let me take a #selfie)

### Version 1.1.0 (2014-06-20)

 * Andrew Kelley:
   - Serve static assets gzipped from memory and use etags. Client loads faster.
   - Fix upload for multiple files.
   - Uploading has a progress bar and queues things in the correct order.
   - Client: UI renders faster. No longer depends on handlebars HTML templating.
   - Client: Status update no longer interfere with user input in settings pane.
   - Client: Fix incorrectly displaying songs as random
   - Client: Use textContent instead of innerText. Fixes incompatibility with
     some browsers.
   - Client: Fix incorrect expand icon shown sometimes.
   - Update duration info in DB when loudness scan finishes.
   - Default streaming buffer size tuned carefully to work well with browsers.
   - Fix crash - writing to closed web socket.
   - Prevent imported track filenames from ending directory names with '.'.
   - Import by URL: Fix race condition.
   - Import by URL: Prevent needless file copy operation when importing in
     situations where the music directory is in a different device than /tmp.
   - Import by URL: Support importing from YouTube.
   - Import by URL: URI decode filename
   - Fix not watching music root folder
   - Client: Fix filenames with percent (%) having invalid download URL.
   - Client: Fix displaying incorrect track number when track number is unknown
   - Client: Fix library items not always expanding consistently
   - Recognize TPA and TCM tags.
   - Fix queue failing to persist on shuffle.
   - Ability to edit tags. Note these edits are currently only saved to the DB
     and not written to the music files.
   - Client: Fix selection behaving erradically for albums in a list.
   - Client: Keyboard shortcuts window scrollable with arrows.
   - Client: Fix UI issues with buttons
   - Client: Default selected queue item is the current track.
   - Client: Fix repeat one and repeat all behavior swapped.
   - Fix Dynamic Mode not weighting last queue date properly when selecting
     random songs.
   - Fix potential crash when users disconnect from client.
   - Fix segfault when deleting tracks.
   - Save CPU cycles by only encoding audio when streamers are connected.
   - Ability to toggle server-side audio playback.
   - Loudness adjustment: Avoid soft limiting when possible based on looking
     at the true peak of the song.
   - Add check for correct version of libgroove on startup. This prevents
     users from accidentally using an outdated version and getting bugs.
   - Ability to start even if MPD protocol port cannot be bound.
   - Preserve volume over application restarts.
   - Improved streaming playback reliability.
   - Fix downloading zip for artist and album.
   - Deleting currently playing track goes to next song.
   - Client: Fix stream button not always in correct state.
   - Add header so that downloading always results in download.
   - Start at last play position on server restart.
   - Various improvements to how tracks are filed in the library browser.
   - When playlist changes, reprioritize scanning queue.
   - Scanning progress is reported to the client.
   - HTTP commands go through permissions framework.
   - Fix sometimes player stops and does not go to next track automatically.
   - Ignore folders in music directory beginning with a dot.
   - Client: Fix freezing and stuttering when many library or playlist updates
     happen quickly.
   - Client: Preserve library selection state on library update.

 * Josh Wolfe:
   - Client: Fix client side crash when 2 clients delete the same queue item.
   - Client: Fix cursor selection not showing up.
   - Client: Ctrl+Space to toggle selection under the cursor.
   - Client: Queue now uses Ctrl to move the cursor without selecting, and Alt
     to bump selected tracks up or down.
   - Client: Ctrl+Arrows and Ctrl+Space in library now work like in the queue.
   - Client: Shift+Arrows in queue now works as expected.
   - Client: Fix Shift Up/Down behavior in library.
   - Seeking no longer automatically starts playing.
   - Client: Hide the password in the UI.
   - Client: Library deletions are anticipated.

 * Caleb Morris:
   - Add filter delay to wait for user to finish typing before beginning search.

 * jeffrom:
   - Fix disabled menu item focus jumping.

 * jimmy:
   - MPD: Make "search" a substring match.
   - MPD: Support "any" as a search type in find and search.

 * jprjr:
   - Fix hardware playback fallback behavior.

 * seansaleh:
   - encodeQueueDuration is now a configurable option.

 * Ronak Buch:
   - Client style: Add margin to URL upload bar.

 * Jeff Epler:
   - README: Mention nodejs-legacy Debian package.

### Version 1.0.1 (2014-03-18)

* Andrew Kelley:
  * Fix race condition when removing tracks from playlist. Closes #160
  * Default import path includes artist directory.
  * Also recognize "TCMP" ID3 tag as compilation album flag
  * Fix Last.fm authentication

### Version 1.0.0 (2014-03-15)

* Andrew Kelley:
  * Remove dependency on MPD. Groove Basin now works independently of MPD.
    It uses [libgroove](https://github.com/andrewrk/libgroove) for audio
    playback and streaming support.
  * Support MPD protocol on (default) port 6600. Groove Basin now functions as
    an MPD server.
  * Fix regression for handling unknown artist/album
  * Fix playlist to display artist name
  * Plug upload security hole
  * Groove Basin is no longer written in coco. Hopefully this will enable more
    code contributions.
  * Simpler config file that can survive new version releases.
  * Simpler and more efficient protocol between client and server.
  * Pressing prev on first track with repeat all on goes to end
  * Automatic loudness detection (ReplayGain) using EBU R128.
    - Lazy playlist scanning.
    - Automatic switching between album and track mode.
    - Takes advantage of multi-core systems.
  * Faster rebuilding of album table index
  * HTTP audio stream buffers much more quickly and flushes the buffer on seek.
  * Fix volume ui going higher than 1.0.
  * Fix changing volume not showing up on other clients.
  * Native html5 audio streaming instead of soundmanager 2
  * Streaming shows when it is buffering
  * add meta charset=utf8 to index.html.
  * fix volume keyboard shortcuts in firefox.
  * Watches music library for updates and quickly updates library.
  * Route dynamicmode through permissions framework
  * Better default password generation
  * web ui: fix current track not displayed sometimes
  * upgrade jquery and jquery ui to latest stable. Fixes some UI glitches.
  * static assets are gzipped and held permanently in memory. Makes the
    web interface load faster.
  * player: set "don't cache this" headers on stream
  * Remove chat. It's not quite ready yet. Chat will be reimplemented better
    in a future release.
  * Remove stored playlist stub from UI. Stored playlists will be reimplemented
    better in a future release.
* Josh Wolfe:
  * Converting the code to not use MPD
  * fix multiselect shiftIds
  * deleting library items removes them from the queue as well.
  * fix shift click going up in the queue
  * after deleting tracks, select the next one, not some random one.

### Version 0.2.0 (2012-10-16)

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


### Version 0.1.2 (2012-07-12)

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

### Version 0.0.6 (2012-04-27)

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


### Version 0.0.5 (2012-03-11)

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

### Version 0.0.4 (2012-03-06)

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

### Version 0.0.3 (2012-03-04)

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

### Version 0.0.2 (2012-03-01)

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

