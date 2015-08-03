var util = require('util');
var ClementineServer = require('clementine-remote').Server;
var Player = require('./player');
var log = require('./log');

function ClementineApiServer(player) {
  var self = this;

  this.player = player;
  this.server = null;

  /*player.on('repeatUpdate', updateOptionsSubsystem);
  player.on('autoDjOn', updateOptionsSubsystem);
  player.on('queueUpdate', onQueueUpdate);
  player.on('deleteDbTrack', updateDatabaseSubsystem);
  player.on('updateDb', updateDatabaseSubsystem);
  player.on('playlistCreate', updateStoredPlaylistSubsystem);
  player.on('playlistUpdate', updateStoredPlaylistSubsystem);
  player.on('playlistDelete', updateStoredPlaylistSubsystem);*/
}

ClementineApiServer.prototype.listen = function (port, host, done) {
  var player = this.player;

  var server = new ClementineServer({
    host: host,
    port: port
  });

  // Current position
  // TODO: something more reliable
  var position = 0, positionInterval;
  var startSendingPosition = function () {
    if (positionInterval) {
      return;
    }

    positionInterval = setInterval(function () {
      server.position = position;
      position++;
    }, 1000);
  };
  var stopSendingPosition = function () {
    clearInterval(positionInterval);
    positionInterval = null;
  };
  var resetPosition = function () {
    stopSendingPosition();
    position = 0;
  };

  var getSong = function (track) {
    return {
      id: 0,
      index: 0,
      title: track.name,
      is_local: true,
      genre: track.genre,
      artist: track.artistName,
      album: track.albumName,
      albumartist: track.albumArtistName,
      length: Math.round(track.duration),
      track: track.track,
      disc: track.disc,
      pretty_year: (track.year) ? String(track.year) : null,
      filename: track.file,
      playcount: track.playCount
    };
  };

  var populateLibrary = function () {
    // TODO: something more optimized
    server.library.reset();

    for (var key in player.libraryIndex.trackTable) {
      var track = player.libraryIndex.trackTable[key];

      server.library.addSong(getSong(track), function (err) {
        if (err) log.error('Could not add song to library:', err);
      });
    }
  };

  var populateQueue = function () {
    var songs = [];
    for (var id in player.playlist) {
      var trackInfo = player.playlist[id];
      var track = player.libraryIndex.trackTable[trackInfo.key];
      songs.push(getSong(track));
    }

    server.playlist.setSongs(songs);
  };

  // Bind events from player
  
  player.on('currentTrack', function () {
    if (player.isPlaying && server.state != 'Playing') {
      server.play();
    }
    if (!player.isPlaying && server.state != 'Paused') {
      server.pause();
    }

    var trackInfo = player.currentTrack;
    if (!trackInfo) {
      return;
    }

    var track = player.libraryIndex.trackTable[trackInfo.key];
    server.song = getSong(track);
  });

  player.on('volumeUpdate', function () {
    server.volume = player.volume * 100;
  });

  player.on('queueUpdate', function () {
    if (player.isPlaying) {
      startSendingPosition();
    }
    if (!player.isPlaying) {
      stopSendingPosition();
    }
  });

  player.on('queueUpdate', populateQueue);

  // Bind events from clients
  
  server.on('play', function () {
    player.play();
  });

  server.on('pause', function () {
    player.pause();
  });

  server.on('playpause', function () {
    if (player.isPlaying) {
      player.pause();
    } else {
      player.play();
    }
  });

  server.on('stop', function () {
    player.stop();
  });

  server.on('next', function () {
    player.next();
  });

  server.on('previous', function () {
    player.prev();
  });

  server.on('insert_urls', function (data) {
    // TODO: support position, playlist_id, play_now, enqueue

    var keys = data.urls.map(function (url) {
      var trackInfo = player.dbFilesByPath[url];
      return trackInfo.key;
    });

    player.appendTracks(keys);
  });

  server.on('repeat', function (repeat) {
    var mode = null;
    switch (repeat) {
      case 'Repeat_Off':
        mode = Player.REPEAT_OFF;
      case 'Repeat_Track':
        mode = Player.REPEAT_ONE;
      case 'Repeat_Playlist':
        mode = Player.REPEAT_ALL;
      case 'Repeat_Album':
      case 'Repeat_Off':
      default:
        // TODO: unsupported values
        return;
    }
    player.setRepeat(mode);
  });

  // TODO: "shuffle" event

  server.on('shuffle_playlist', function () {
    player.shufflePlaylist();
  });

  server.on('change_song', function (data) {
    // TODO: support data.playlist_id
    player.seekToIndex(data.song_index, 0);
  });

  server.on('set_track_position', function (pos) {
    player.seek(player.currentTrack.id, pos);
  });

  this.server = server;

  // Populate server library & queue
  server.library.on('ready', function () {
    populateLibrary();
  });

  populateQueue();

  return server.listen(function () {
    // Start mDNS service
    server.mdns();

    if (done) {
      done();
    }
  });
};

module.exports = ClementineApiServer;