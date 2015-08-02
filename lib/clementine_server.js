var util = require('util');
var ClementineServer = require('clementine-remote').Server;

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

  var positionInterval;
  var startSendingPosition = function () {
    positionInterval = setInterval(function () {
      server.position = Math.round((Date.now() - player.playingStart.getTime()) / 1000);
    }, 1000);
  };
  var stopSendingPosition = function () {
    clearInterval(positionInterval);
  };

  // Bind events from player
  
  player.on('currentTrack', function () {
    if (player.isPlaying && server.state != 'Playing') {
      server.play();
      startSendingPosition();
    }
    if (!player.isPlaying && server.state != 'Paused') {
      server.pause();
      stopSendingPosition();
    }

    var trackInfo = player.currentTrack;
    if (!trackInfo) {
      return;
    }

    var track = player.libraryIndex.trackTable[trackInfo.key];

    var song = {
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
      pretty_year: (track.year) ? String(track.year) : null
    };

    server.song = song;
  });

  player.on('volumeUpdate', function () {
    server.volume = player.volume * 100;
  });

  // Bind events from clients
  
  server.on('playpause', function () {
    if (player.isPlaying) {
      player.pause();
    } else {
      player.play();
    }
  });

  this.server = server;

  return server.listen(done);
};

module.exports = ClementineApiServer;