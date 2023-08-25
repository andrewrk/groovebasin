# Groove Basin Protocol Specification

Version 0.0.1, published on 2015-08-18.

Note: The Groove Basin protocol is not stable yet. When a version of
Groove Basin is released which stabilizes the protocol, the major version will
be bumped to 1.0.0. Until then, a minor version bump may break compatibility
with older clients.

In effort to make upgrading easier, when compatibility is broken (even before
1.0.0), this document will provide [version history](#version-history) with
instructions for updating to the new protocol version.

Other music players are invited to implement and extend the Groove Basin
protocol. In order to make this practical, metadata is available which
describes what messages are available to send and what information is available
to retrieve. Using this, a client could simultaneously support music players
with fewer features and music players with more features than Groove Basin.

- [Overview](#overview)
  - [Establishing a Connection](#establishing-a-connection)
  - [Authentication](#authentication)
  - [Generating UUIDs](#generating-uuids)
- [Client-to-Server Control Messages](#client-to-server-control-messages)
  - [approve](#approve)
  - [chat](#chat)
  - [deleteTracks](#deletetracks)
  - [deleteUsers](#deleteusers)
  - [autoDjOn](#autodjon)
  - [autoDjHistorySize](#autodjhistorysize)
  - [autoDjFutureSize](#autodjfuturesize)
  - [ensureAdminUser](#ensureadminuser)
  - [hardwarePlayback](#hardwareplayback)
  - [importNames](#importnames)
  - [importUrl](#importurl)
  - [login](#login)
  - [logout](#logout)
  - [subscribe](#subscribe)
  - [updateTags](#updatetags)
  - [updateUser](#updateuser)
  - [unsubscribe](#unsubscribe)
  - [move](#move)
  - [pause](#pause)
  - [play](#play)
  - [queue](#queue)
  - [seek](#seek)
  - [setStreaming](#setstreaming)
  - [remove](#remove)
  - [repeat](#repeat)
  - [requestApproval](#requestapproval)
  - [setVolume](#setvolume)
  - [stop](#stop)
  - [playlistCreate](#playlistcreate)
  - [playlistRename](#playlistrename)
  - [playlistDelete](#playlistdelete)
  - [playlistAddItems](#playlistadditems)
  - [playlistRemoveItems](#playlistremoveitems)
  - [playlistMoveItems](#playlistmoveitems)
  - [labelCreate](#labelcreate)
  - [labelRename](#labelrename)
  - [labelColorUpdate](#labelcolorupdate)
  - [labelDelete](#labeldelete)
  - [labelAdd](#labeladd)
  - [labelRemove](#labelremove)
  - [lastFmGetSession](#lastfmgetsession)
  - [lastFmScrobblersAdd](#lastfmscrobblersadd)
  - [lastFmScrobblersRemove](#lastfmscrobblersremove)
- [Server-to-Client Control Messages](#server-to-client-control-messages)
  - [error](#error)
  - [seek](#seek-1)
  - [time](#time)
  - [token](#token)
  - [lastFmApiKey](#lastfmapikey)
  - [lastFmGetSessionSuccess](#lastfmgetsessionsuccess)
  - [lastFmGetSessionError](#lastfmgetsessionerror)
  - [user](#user)
- [Subscribed Information Change Messages](#subscribed-information-change-messages)
  - [currentTrack](#currenttrack)
  - [autoDjOn](#autodjon-1)
  - [autoDjHistorySize](#autodjhistorysize-1)
  - [autoDjFutureSize](#autodjfuturesize-1)
  - [repeat](#repeat-1)
  - [volume](#volume)
  - [queue](#queue-1)
  - [hardwarePlayback](#hardwareplayback-1)
  - [library](#library)
  - [libraryQueue](#libraryqueue)
  - [scanning](#scanning)
  - [playlists](#playlists)
  - [importProgress](#importprogress)
  - [anonStreamers](#anonstreamers)
  - [haveAdminUser](#haveadminuser)
  - [users](#users)
  - [streamEndpoint](#streamendpoint)
  - [protocolMetadata](#protocolmetadata)
  - [events](#events)
    - [chat](#chat-1)
    - [queue](#queue-2)
    - [currentTrack](#currenttrack-1)
    - [autoPause](#autopause)
    - [streamStart](#streamstart)
    - [streamStop](#streamstop)
    - [connect](#connect)
    - [part](#part)
    - [register](#register)
    - [login](#login-1)
    - [move](#move-1)
    - [pause](#pause-1)
    - [play](#play-1)
    - [stop](#stop-1)
    - [seek](#seek-2)
    - [playlistRename](#playlistrename-1)
    - [playlistDelete](#playlistdelete-1)
    - [playlistCreate](#playlistcreate-1)
    - [playlistAddItems](#playlistadditems-1)
    - [playlistRemoveItems](#playlistremoveitems-1)
    - [playlistMoveItems](#playlistmoveitems-1)
    - [clearQueue](#clearqueue)
    - [remove](#remove-1)
    - [shuffle](#shuffle)
    - [import](#import)
    - [labelCreate](#labelcreate-1)
    - [labelRename](#labelrename-1)
    - [labelColorUpdate](#labelcolorupdate-1)
    - [labelDelete](#labeldelete-1)
    - [labelAdd](#labeladd-1)
    - [labelRemove](#labelremove-1)
- [Client-to-Server HTTP Messages](#client-to-server-http-messages)
  - [GET /library/](#get-library)
  - [GET /library/[folder]/](#get-libraryfolder)
  - [GET /library/[songFilePath]](#get-librarysongfilepath)
  - [GET /download/keys](#get-downloadkeys)
  - [GET /[streamEndpoint]](#get-streamendpoint)
  - [POST /upload](#post-upload)
- [Version History](#version-history)
  - [0.0.1](#001)

## Overview

The Groove Basin Protocol allows you to do these things:

 * Authenticate or remain as a guest.
 * Obtain information about the music library, playlists, settings, chat, and
   play queue.
 * Control playback.
 * Update the music library, playlists, chat, and settings.
 * Import new music.
 * Download music.
 * Connect to the music stream.

Groove Basin itself serves as an example implementation of the Groove Basin
Protocol. It contains both a client and a server.

Additionally, there is [gbremote](https://github.com/andrewrk/gbremote), a
simple Node.js module and command line client demonstrating how to use the
Groove Basin Protocol.

### Establishing a Connection

The Groove Basin protocol operates within the HTTP protocol. Ideally, the
admin has configured the server to use HTTPS, either by using config options to
enable SSL and set a certificate, or by using
[a proxy such as nginx](https://github.com/andrewrk/groovebasin/wiki/Proxying-Groove-Basin-via-nginx).

After establishing an HTTP connection, establish a WebSocket connection. This
WebSocket connection is referred to as the *control connection* in this
document.

The control connection is JSON-formatted and newline-delimited. Each message
is formatted like this:

```js
{"name": "messageName", "args": messageArguments}
```

Depending on the message, `messageArguments` might be any of the JSON types:
`string`, `number`, `object`, `array`, `boolean`, or `null`.

`object` types are described like this:

 * Type: `{fieldOne, fieldTwo, fieldThree}`
 * `fieldOne`: description of field one
 * `fieldTwo`: description of field two
 * `fieldThree`: description of field three

`array` types are described like this:

 * Type: `[fieldName]`
 * `fieldName`: description of field

Datetimes are always communicated as a `string`, in simplified extended ISO
format ([ISO 8601](https://en.wikipedia.org/wiki/ISO_8601)), which is always 24
characters long: `YYYY-MM-DDTHH:mm:ss.sssZ`. The timezone is always zero UTC
offset, as denoted by the suffix "Z".

After establishing a WebSocket connection, the next thing you probably want to
do is [subscribe](#subscribe) to information relevant to you.

### Authentication

Groove Basin has the following permissions:

 0. `read` - Read-access to everything.
 0. `add` - Import new songs to library.
 0. `control` - Control playback and the queue.
 0. `playlist` - Create, update, and delete playlists.
 0. `admin` - Delete songs, update tags, modify global settings, modify users.

By default, guests have only these permissions:

 * `read`
 * `add`
 * `control`

In this document, each message that the client can send to the server has a
required permission associated with it. If the client does not have this
permission, the message is ignored and the server sends a message like this:

```json
{"name": "error", "args": "command \"play\" requires permission \"control\""}
```

When a client first connects to the server, the client has guest permissions
and is assigned a guest name such as "Guest-BPJLuPAf".

To authenticate as a user and possibly gain more permissions, send a
[login](#login) message.

See also [logout](#logout) message.

### Generating UUIDs

Sometimes it is the client's job to generate a random UUID. To do this,

Generate 24 bytes of random data and then base64 encode it to a 32-byte string.
Instead of `/` and `+`, clients must use `_` and `-` as these symbols are safe
to be put in an HTML id attribute without being escaped.

The server must reject messages which violate this constraint.

## Client-to-Server Control Messages

### approve

 * Permission: `admin`
 * Type: `[{id, replaceId, approved, name}]`
 * `id`: `string`. The id of the user requesting to be approved.
 * `replaceId`: `string` or `null`. If you want to delete the original user
   and merge them into another user, put that user id here. Otherwise use `null`
   to approve as a new user.
 * `approved`: `boolean`. `true` to approve, `false` to reject.
 * `name`: `string`. The user's name is replaced with this one.

Accept or reject a user's request for account validation. This is in lieu of,
for example, an account confirmation email.

### chat

 * Permission: `control`
 * Type: `{text, displayClass}`
 * `text`: `string`. The chat message to send.
 * `displayClass`: `string` or `null`. Use the string "me" to indicate that this
   chat message is in the third person, for example when the user types:
   `/me hides behind the desk`. Otherwise, use `null`.

Sends a chat message.

### deleteTracks

 * Permission: `admin`
 * Type: `[key]`
 * `key`: The ID of the song you wish to delete.

Deletes multiple tracks at once.

### deleteUsers

 * Permission: `admin`
 * Type: `[id]`
 * `id`: The ID of the user you wish to delete.

### autoDjOn

 * Permission: `control`
 * Type: `boolean`.

### autoDjHistorySize

 * Permission: `control`
 * Type: `number`.

Change the number of items in the playlist before the current song which are
not deleted by Auto DJ.

### autoDjFutureSize

 * Permission: `control`
 * Type: `number`.

Change the number of items in the playlist after the current song which are
selected randomly by Auto DJ.

### ensureAdminUser

 * Permission: none
 * Type: none

If there is no admin user, this action creates one and prints the credentials
to the server's stdio.

### hardwarePlayback

 * Permission: `admin`
 * Type: `boolean`

Turn on or off the server sending audio to speakers.

### importNames

 * Permission: `add`
 * Type: `{names: [query], autoQueue}`
 * `names`: `array`. Array of names to import.
   * `query`: Search query used to find song. Example: "Tristam - I Remember".
 * `autoQueue`: `boolean`. `true` to automatically queue the imported songs
   in the play queue; `false` otherwise.
 
When the client sends this message, it is the server's job to locate the songs
somehow based on the queries given, and then download or otherwise import them
into the music library.

Some servers may not be able to implement this message and clients are advised
to check for its existence before using it using the `protocolMetadata`
information.

Groove Basin uses the query to search YouTube and download the first HD result.

This message is likely to be updated before the protocol reaches 1.0.0.

### importUrl

 * Permission: `add`
 * Type: `{url, autoQueue}`
 * `url`: `string`. The Uniform Resource Locator to attempt to import.
 * `autoQueue`: `boolean`. `true` to automatically queue the imported songs
   in the play queue; `false` otherwise.

When the client sends this message, it is the server's job to interpret the
supplied URL as describing song(s) that can be imported and then import them
into the library.

Groove Basin does the following:

 0. If it is a YouTube URL, download the highest quality video from YouTube.
 0. Download the URL to disk and then act as though the user used the
    [POST /upload](#post-upload) HTTP message and uploaded the resulting file.

This message is likely to be updated before the protocol reaches 1.0.0.

### login

 * Permission: none
 * Type: `{username, password}`
 * `username`: `string`
 * `password`: `string`

After logging in, the server sends a [user](#user) message containing the
user and permissions that the connection is associated with.

See also [logout](#logout)

### logout

 * Permission: none
 * Type: none

See also [login](#login)

### subscribe

 * Permission: `read`
 * Type: `{name, delta, version}`
 * `name`: `string`. The name of the information you want to subscribe to. See
   [Subscribed Information Change Messages](#subscribed-information-change-messages)
   for a list of available information. You can also query the server for the
   available information names by subscribing to the `protocolMetadata` information.
 * `delta`: `boolean`. `true` if you want to receive object diffs; `false` if
   you want to receive simple data. See
   [Subscribed Information Change Messages](#subscribed-information-change-messages)
   for detailed description. Defaults to `false`.
 * `version`: `string`. Only used for delta subscription mode. Supply the
   version hash of the information that you have cached and the server will
   not send information if it has not changed. Defaults to `null`.

`subscribe` is the only way to get information from the server. Instead of
querying information, the client subscribes to it so that the client will
always have up to date information.

After subscribing, the server will send the information immediately
(except in delta mode and version hash matches) and then again whenever the
information is updated.

Information is guaranteed to be sent in the order the client subscribes. For
example, if the client subscribes to [library](#library) and then subscribes to
[queue](#queue), the library information is sent first followed by the queue
information.

See also [unsubscribe](#unsubscribe).

### updateTags

 * Permission: `admin`
 * Type: `{songId: {propName: propValue}}`
 * `songId`: `string`. The song ID to update tags for.
 * `propName`: `string`. Tag name. One of the following. See [library](#library)
   for details.
   - `name`
   - `artistName`
   - `albumArtistName`
   - `albumName`
   - `compilation`
   - `track`
   - `trackCount`
   - `disc`
   - `discCount`
   - `year`
   - `genre`
   - `composerName`
   - `performerName`

Note this message allows editing tags for multiple songs at once.

### updateUser

 * Permission: `admin`
 * Type: `{userId, perms}`
 * `userId`: `string`. ID of the user to update.
 * `perms`: `object`. Permissions to assign to the user.

### unsubscribe

 * Permission: none
 * Type: `string`.

See also [subscribe](#subscribe)

### move

 * Permission: `control`
 * Type: `{itemId: {sortKey}}`
 * `itemId`: `string`. ID of the playlist item to move.
 * `sortKey`: `string`. Describes the new position of the playlist item.

Moving play queue items by updating their sort key values. Use the
[keese](https://github.com/thejoshwolfe/node-keese) algorithm to compute the
desired sort keys.

### pause

 * Permission: `control`
 * Type: none

### play

 * Permission: `control`
 * Type: none

### queue

 * Permission: `control`
 * Type: `{itemId: {key, sortKey}}`
 * `itemId`: `string`. [Randomly generated UUID](#generating-uuids) to identify
   the new queue item.
 * `key`: `string`. ID of the song this queue item is for.
 * `sortKey`:`string`. [keese](https://github.com/thejoshwolfe/node-keese)
   value used to determine the position of the queue item.

### seek

 * Permission: `control`
 * Type: `{id, pos}`
 * `id`: `string`. ID of the play queue item to play.
 * `pos`: `number`. Position in seconds into the song to start playing.

### setStreaming

 * Permission: none
 * Type: `boolean`. `true` if streaming; `false` otherwise.

Clients should set this to `true` when the user indicates that they wish to
stream and they should set this to `false` when the user indicates that they
no longer wish to stream (in addition to closing the connection to the stream
endpoint).

### remove

 * Permission: `control`
 * Type: `[id]`
 * `id`: `string`. ID of the play queue item to remove.

### repeat

 * Permission: `control`
 * Type: `number`. Desired repeat state enum value.

Repeat states:

 * Repeat Off: 0
 * Repeat All: 1
 * Repeat One: 2

### requestApproval

 * Permission: none
 * Type: none

Ask the admin to approve of the connected user's account. In order for a user
to have permissions beyond guest permissions, the user must be approved.

### setVolume

 * Permission: `control`
 * Type: `number`. Desired floating point volume between 0.0 and 2.0. Volumes
   above 1.0 risk lowering the audio quality.

### stop

 * Permission: `control`
 * Type: none

### playlistCreate

 * Permission: `playlist`
 * Type: `{id, name}`
 * `id`: `string`. [Randomly generated UUID](#generating-uuids) to identify
   the new playlist.
 * `name`: `string`.

### playlistRename

 * Permission: `playlist`
 * Type: `{id, name}`
 * `id`: `string`. The ID of the playlist to rename.
 * `name`: `string`. New name of the playlist.

### playlistDelete

 * Permission: `playlist`
 * Type: `[id]`
 * `id`: ID of a playlist to delete.

Delete any number of playlists.

### playlistAddItems

 * Permission: `playlist`
 * Type: `{id, items: {itemId: {key, sortKey}}}`
 * `id`: `string`. ID of the playlist to add items to.
 * `items`: `object`
   - `itemId`: `string`. [Randomly generated UUID](#generating-uuids) to identify
     the new playlist item.
   - `key`: `string`. ID of the song this playlist item is associated with.
   - `sortKey`: `string`. [keese](https://github.com/thejoshwolfe/node-keese)
     value used to order the playlist item in the playlist.

### playlistRemoveItems

 * Permission: `playlist`
 * Type: `{playlistId: [itemId]}`
 * `playlistId`: `string`. ID of a playlist to remove items from.
 * `itemId`: `string`. ID of a playlist item to remove.

### playlistMoveItems

 * Permission: `playlist`
 * Type: `{playlistId: {itemId: {sortKey}}}`
 * `playlistId`: `string`. ID of a playlist to move items in.
 * `itemId`: `string`. Id of a playlist item to move.
 * `sortKey`: `string`. New [keese](https://github.com/thejoshwolfe/node-keese)
   value used to order the playlist item in the playlist.

### labelCreate

 * Permission: `playlist`
 * Type: `{id, name}`
 * `id`: `string`. [Randomly generated UUID](#generating-uuids) to identify the
   new label.
 * `name`: `string`.

### labelRename

 * Permission: `playlist`
 * Type: `{id, name}`
 * `id`: `string`. ID of the label to rename.
 * `name`: `string`. New name.

### labelColorUpdate

 * Permission: `playlist`
 * Type: `{id, color}`
 * `id`: `string`. ID of the label to rename.
 * `color`: `string`. New color.

### labelDelete

 * Permission: `playlist`
 * Type: `[id]`
 * `id`: `string`. A label ID to delete.

### labelAdd

 * Permission: `playlist`
 * Type: `{songId: [labelId]}`
 * `songId`: `string`. ID of the song to add labels to.
 * `labelId`: `string`. ID of a label to add to the song.

### labelRemove

 * Permission: `playlist`
 * Type: `{songId: [labelId]}`
 * `songId`: `string`. ID of the song to remove labels from.
 * `labelId`: `string`. ID of a label to remove from the song.

### lastFmGetSession

 * Permission: `read`
 * Type: `string`. Last.fm token.

This message is a holdover from older times before user accounts were
implemented and this functionality is likely to be changed before the
protocol reaches 1.0.0.

### lastFmScrobblersAdd

 * Permission: `read`
 * Type: `{username, sessionKey}`
 * `username`: `string`. Last.fm username.
 * `sessionKey`: `string`. Last.fm session key.

This message is a holdover from older times before user accounts were
implemented and this functionality is likely to be changed before the
protocol reaches 1.0.0.

### lastFmScrobblersRemove

 * Permission: `read`
 * Type: `{username, sessionKey}`
 * `username`: `string`. Last.fm username.
 * `sessionKey`: `string`. Last.fm session key.

This message is a holdover from older times before user accounts were
implemented and this functionality is likely to be changed before the
protocol reaches 1.0.0.

## Server-to-Client Control Messages

### error

 * Type: `string`. Error message.

When something goes wrong the server sends this message. Currently there is no
way to associate an error message as originating from a specific client message.

This is something that is under consideration for changing before the protocol
hits 1.0.0.

### seek

No arguments. Sent when the current song or current song position changes. If
the client is streaming, they should clear their buffer and ask for a fresh
stream.

### time

 * Type: `string`. Current datetime according to the server.

Sent on first connection, when the system time changes, and periodically
to combat clock drift. Pause time and playback start time are relative to this
time.

The client does not [subscribe](#subscribe) to get this information because the
value is constantly changing.

### token

 * Type: `string`. Identifies the connection session.

Sent on first connection. HTTP requests may use this token to act on behalf of
this control connection.

### lastFmApiKey

 * Type: `string`

Sent on first connection. Supplies the client with the Last.fm API key of the
server which the client could use to implement client-side authentication and
then set up server scrobbling.

This message is a holdover from older times before user accounts were
implemented and this functionality is likely to be changed before the
protocol reaches 1.0.0.

### lastFmGetSessionSuccess

 * Type: `{session: {username, sessionKey}}`
 * `session`: Always the string "session".
 * `username`: Last.fm username
 * `sessionKey`: Last.fm session key

This message is a holdover from older times before user accounts were
implemented and this functionality is likely to be changed before the
protocol reaches 1.0.0.

### lastFmGetSessionError

 * Type: `string`. Last.fm error message.

This message is a holdover from older times before user accounts were
implemented and this functionality is likely to be changed before the
protocol reaches 1.0.0.

### user

 * Type: `{id, name, perms, registered, requested, approved}`
 * `id`: `string`. User ID of the connection.
 * `name`: `string`. User name of the connection.
 * `perms`: `object`. permissions the connection currently has.
 * `registered`: `boolean`. Whether or not the user registered.
 * `requested`: `boolean`. Whether or not the user requested approval.
 * `approved`: `boolean`. Whether or not the user was approved.

Example `perms`:

```json
{"add": true, "control": true}
```

## Subscribed Information Change Messages

When you [subscribe](#subscribe) to information, you receive a message
immediately and then any time that information changes.

If you subscribed simply, then a subscribed information change
message looks like this:

```js
{"name": subscriptionName, "args": newValue}
```

In simple subscription mode, `newValue` is the new information, and you receive
all the information every time it changes.

If you subscribed with `delta: true`, then a subscribed information change
message looks like this:

```js
{
  "name": subscriptionName,
  "args": {
    "version": versionHash,
    "reset": false,
    "delta": newValueDelta
  }
}
```

Note that the newlines here are for document readability and in Groove Basin
protocol, there is always one JSON message per line.

With delta subscription, every time information changes, you are given a delta
object in [curlydiff](https://github.com/thejoshwolfe/curlydiff) format along
with a version hash. If you disconnect from the server and reconnect later,
you supply the version hash when subscribing. If your version hash matches the
server, the server sends no data (until the data changes next time), since the
client's cached data is correct.

If `reset` is `true`, then the client must first invalidate its cache by
setting it to `undefined`.

It is recommended that you use delta subscription mode for the library, since
the library metadata can be large and change often.

Currently all information is available regardless of permissions. This is
something that will likely change before the protocol reaches 1.0.0.

### currentTrack

 * Type: `{currentItemId, isPlaying, trackStartDate, pausedTime}`
 * `currentItemId`: `string` or `null`. The play queue ID currently playing.
 * `isPlaying`: `boolean`. `true` if playing; `false` if paused.
 * `trackStartDate`: `string`. datetime representing what time it was on the
   server when frame 0 of the current song was played.
 * `pausedTime`: `number`. Only relevant when `isPlaying` is `false`. How many
   seconds into the song the position is.

### autoDjOn

 * Type: `boolean`. `true` if Auto DJ is on; `false` if Auto DJ is off.

### autoDjHistorySize

 * Type: `number`. When Auto DJ is on, this is the number of songs in the play
   queue before the current song that are not automatically removed.

### autoDjFutureSize

 * Type: `number`. When Auto DJ is on, this is the number of songs in the play
   queue after the current song that are chosen randomly to be played next.

### repeat

 * Type: `number`. The current repeat state.

Repeat states:

 * Repeat Off: 0
 * Repeat All: 1
 * Repeat One: 2

### volume

 * Type: `number`. Range: 0.0 to 2.0. Values above 1.0 indicate that a limiter
   may be in use, compromising audio quality integrity in order to achieve
   loudness.

### queue

 * Type: `{id: {key, sortKey, isRandom}}`
 * `id`: `string`. The play queue item ID.
 * `key`: `string`. The ID of the song this queue item refers to.
 * `sortKey`: `string`. A [keese](https://github.com/thejoshwolfe/node-keese)
   string indicating the order of this item in the play queue.
 * `isRandom`: `boolean`. Indicates whether this queue item was queued by the
   user or randomly, by Auto DJ.

To display the play queue, sort the queue items by their `sortKey` string.

### hardwarePlayback

 * Type: `boolean`. Whether the server has hardware playback on.

### library

Type:

```
{
  key: {
    name,
    artistName,
    albumArtistName,
    albumName,
    compilation,
    track,
    trackCount,
    disc,
    discCount,
    duration,
    year,
    genre,
    file,
    composerName,
    performerName,
    labels: {labelId: 1},
  },
  ...
}
```

 * `key`: `string`. ID of the song in the music library.
 * `file`: `string`. Path of the song on disk relative to the music library
   root.
 * `duration`: `number`. How many seconds long this track is. Once the track
   has been scanned for loudness, this duration value is always exactly correct.
 * `name`: `string`. Track title.
 * `artistName`: `string`
 * `albumArtistName`: `string`
 * `albumName`: `string`
 * `compilation`: `boolean`
 * `track`: `number`. Which track number this is.
 * `trackCount`: `number`. How many total tracks there are on this album.
 * `disc`: `number`. Which disc number this is.
 * `discCount`: `number`. How many total discs there are in this compilation.
 * `year`: `number`. What year this track was released.
 * `genre`: `string`
 * `composerName`: `string`
 * `performerName`: `string`
 * `labels`: `object`. The value is always `1`.
   - `labelId`: `string`. ID of a label that applies to this song.

It is strongly recommended to use the delta subscription mode with this
information.

### libraryQueue

This message is the same as [library](#library), except instead of the set of
tracks returned being the entire library, it is exactly the set of tracks
relevant to the play queue. You would subscribe to this if you do not care
about the library but you do want to know what song is currently playing, for
example.

It is recommended, but not necessary, to use the delta subscription mode with
this information.

### scanning

 * Type: `{fingerprintDone, loudnessDone}`
 * `fingerprintDone`: `boolean`.
 * `loudnessDone`: `boolean`.

Contains the set of songs currently being scanned for loudness, duration,
fingerprint, and possibly more.

The way this works will likely be changed before protocol version 1.0.0 is
reached.

### playlists

 * Type: `{id: {name, mtime, items: {itemId: {songId, sortKey}}}}`
 * `id`: `string`. Playlist ID.
 * `name`: `string`.
 * `mtime`: `string`. Datetime of last modification time of playlist.
 * `items`: `object`. Set of playlist items.
   * `itemId`: `string`. ID of the playlist item in the playlist.
   * `key`: `string`. ID of the song in the music library.
   * `sortKey`: `string`. [keese](https://github.com/thejoshwolfe/node-keese)
     string which tells the position of the item in the playlist.

To display playlist items in the correct order, sort them by `sortKey`.

### importProgress

 * Type: `{id: {date, filenameHintWithoutPath, bytesWritten, size}}`
 * `id`: `string`. Import job ID.
 * `date`: `string`.
 * `filenameHintWithoutPath`: `string`.
 * `bytesWritten`: `number`. How many bytes have been imported so far.
 * `size`: `number`. How many bytes this file is.

### anonStreamers

 * Type: `number`. How many anonymous streamers are connected.

To count total streamers including non-anonymous streamers, subscribe to
[users](#users) and check if `streaming` is `true`.

### haveAdminUser

 * Type: `boolean`. `true` if an admin user exists, `false` otherwise.

If there is no admin user, the client may send the
[ensureAdminUser](#ensureadminuser) message to have an admin user generated
and credentials printed to stdio.

### users

 * Type: `{userId: {name, perms, requested, approved, connected, streaming}}`
 * `userId`: `string`. ID of the user.
 * `name`: `string`. Name of the user.
 * `perms`: `object`. Permissions the user has.
 * `requested`: `boolean`. Whether the user has requested approval.
 * `connected`: `boolean`. Whether the user is currently connected.
 * `streaming`: `boolean`. Whether the user is connected to the stream.

Before protocol version 1.0.0, this protocol message is likely to change.

### streamEndpoint

 * Type: `string`. Example: "stream.mp3". Connect to this to listen to the
   stream.

### protocolMetadata

 * Type: `{version, actions, information, httpActions}`
 * `version`: `string`. Which version of the Groove Basin protocol this server
   observes.
 * `actions`: `object`. Which client-to-server control messages are supported.
 * `information`: `object`. What information is available to subscribe to.
 * `httpActions`: `object`. What client-to-server HTTP messages are supported.

Official Groove Basin server uses the version number at the top of this
document for the `version` field.

`actions` looks like:

```
{"deleteTracks": true, "importNames": true, ...}
```

`information` looks like:

```
{"currentTrack": true, "libraryQueue": true, ...}
```

`httpActions` looks like:

```
{"GET /library/[songFilePath]": true, "GET /download/keys": true, ...}
```

Servers implementing the Groove Basin protocol are welcome to add more fields
to this message in order to provide information they deem necessary for clients
to properly detect and support them.

### events

 * Type: `{id: {date, type, sortKey, userId, text, trackId, pos, displayClass, playlistId}}`
 * `id`: `string`. Event ID.
 * `date`: `string`. Datetime when the event occurred.
 * `sortKey`: `string`. [keese](https://github.com/thejoshwolfe/node-keese)
   string specifying the order the events should be displayed in.
 * `type`: `string`. Depending on the event type there may be more fields.
   See below for details.
 * `userId`: `string`. Sometimes used; see below.
 * `text`: `string`. Sometimes used; see below.
 * `trackId`: `string`. Sometimes used; see below.
 * `pos`: `number`. Sometimes used; see below.
 * `displayClass`: `string`. Sometimes used; see below.
 * `playlistId`: `string`. Sometimes used; see below.
 * `labelId`: `string`. Sometimes used; see below.
 * `subCount`: `number`. Sometimes used; see below.

Event history.

Some of the event fields are likely to be renamed before protocol version 1.0.0.

#### chat

 * `text`: the chat message.
 * `userId`: ID of user that sent the chat message.
 * `displayClass`: "me" if the user chatted using `/me`, `null` otherwise.

When a user sends a chat message.

#### queue

 * `userId`: ID of user that queued the items.
 * `trackKey`: If only one track, the ID of the song that was queued.
 * `pos`: Number of queued tracks.

When a user queues tracks.

#### currentTrack

 * `userId`: ID of user that queued the items.
 * `trackKey`: If only one track, the ID of the song that was queued.
 * `text`: "now playing text" of the track. Might be useful to still display
   the event when the track is deleted from the library.

When the currently playing track changes for any reason.

#### autoPause

When the server automatically presses pause because nobody is listening.

#### streamStart

 * `userId`: ID of the user that started streaming.

This is likely to change before protocol version 1.0.0.

#### streamStop

 * `userId`: ID of the user that stopped streaming.

This is likely to change before protocol version 1.0.0.

#### connect

 * `userId`: ID of the user that connected.

This is likely to change before protocol version 1.0.0.

#### part

 * `userId`: ID of the user that disconnected.

#### register

 * `userId`: ID of the user that registered.

Registering is the same as changing user name.

This is likely to change before protocol version 1.0.0.

#### login

 * `userId`: ID of the user that logged in.

#### move

 * `userId`: ID of the user that moved play queue tracks.

#### pause

 * `userId`: ID of the user that pressed pause.

#### play

 * `userId`: ID of the user that pressed play.

#### stop

 * `userId`: ID of the user that pressed stop.

#### seek

 * `userId`: ID of the user that seeked.
 * `trackKey`: ID of the song the user seeked to.
 * `pos`: Position in the song the user seeked to.

#### playlistRename

 * `userId`: ID of the user that renamed a playlist.
 * `playlistId`: ID of the playlist the user renamed.
 * `text`: Old name of the playlist.

#### playlistDelete

 * `userId`: ID of the user that deleted a playlist.
 * `playlistId`: ID of the playlist the user deleted.
 * `text`: Old name of the playlist.

#### playlistCreate

 * `userId`: ID of the user that created a playlist.
 * `playlistId`: ID of the playlist the user created.

#### playlistAddItems

 * `userId`: ID of the user that added items to a playlist.
 * `playlistId`: ID of the playlist the user added to.
 * `trackKey`: If only one item, the ID of the song.
 * `pos`: The number of items added to the playlist.

#### playlistRemoveItems

 * `userId`: ID of the user that removed items from playlists.
 * `playlistId`: If only one playlist, ID of the playlist the user removed from.
 * `trackKey`: If only one item, the ID of the song.
 * `pos`: The number of items removed from playlists.

#### playlistMoveItems

 * `userId`: ID of the user that ordered items on playlists.
 * `playlistId`: If only one playlist, ID of the playlist the user ordered items
   on.
 * `pos`: The number of items re-ordered in playlists.

#### clearQueue

 * `userId`: ID of the user that cleared the play queue.

#### remove

 * `userId`: ID of the user that removed items from the play queue.
 * `trackKey`: If only one item removed, the ID of the song.
 * `pos`: Number of items removed.
 * `text`: If only one item removed, "now playing text" of the item. Might
   be useful for if the song is later deleted from the library.

#### shuffle

 * `userId`: ID of the user that shuffled the play queue.

This event will likely be removed before the protocol reaches 1.0.0, in favor
of the [move](#move-1) event.

#### import

 * `userId`: ID of the user that imported tracks.
 * `trackKey`: If only one imported track, the ID of the song.
 * `pos`: The number of songs imported.

#### labelCreate

 * `userId`: ID of the user that created a label.
 * `labelId`: ID of the label that was created.
 * `text`: name of the label that was created.

#### labelRename

 * `userId`: ID of the user that renamed a label.
 * `labelId`: ID of the label that was renamed.
 * `text`: old name of the label.

#### labelColorUpdate

 * `userId`: ID of the user that changed a label's color.
 * `labelId`: ID of the label whose color was changed.
 * `text`: old color of the label

#### labelDelete

 * `userId`: ID of the user that deleted a label.
 * `labelId`: ID of the label that was deleted.
 * `text`: name of the deleted label.

#### labelAdd

 * `userId`: ID of the user that added labels to songs.
 * `trackKey`: If label added to only one track, the key of the track that
    received a label.
 * `pos`: Number of tracks that received labels.
 * `labelId`: If only one label added to only one track, the ID of the label
   that was added.
 * `subCount`: If only one track received labels, the number of labels that
   track received.

#### labelRemove

 * `userId`: ID of the user that removed labels from songs.
 * `trackKey`: If label removed from only one track, the key of the track that
    had a label removed.
 * `pos`: Number of tracks from which labels were removed.
 * `labelId`: If only one label removed from only one track, the ID of the label
   that was removed.
 * `subCount`: If only one track had labels removed, the number of labels that
   track had removed.

## Client-to-Server HTTP Messages

To authenticate, first establish a control connection. In HTTP requests,
use a cookie with name `token` and value equal to the [token](#token) received
in the control connection.

### GET /library/

 * Permission: `read`

Downloads the entire music library as a .zip file.

### GET /library/[folder]/

 * Permission: `read`

Downloads `folder` as a .zip file.

### GET /library/[songFilePath]

 * Permission: `read`

Downloads `songFilePath`. The `content-disposition: attachment` HTTP header
will be set.

### GET /download/keys

 * Permission: `read`

Download any number of songs by ID as a .zip file.

The query string should look like: `?key1&key2&key3`

### GET /[streamEndpoint]

 * Permission: `read`

Connect to this to listen to the stream.

Don't forget to send the [setStreaming](#setStreaming) message appropriately.

### POST /upload

 * Permission: `add`

Add files to library using `multipart/form-data` upload.

*Before* each file in the multipart upload, include a form field `size` which
contains size of the file in bytes. This is used to provide an accurate
progress bar during the upload.

To indicate to the server that the upload should be queued automatically,
include a global form field `autoQueue` in the request.

Advanced servers may support uploading things like file archives and torrent
files. Groove Basin supports uploading .zip files.

## Version History

### 0.0.1

First draft of protocol specification.
