var LastFmNode = require('lastfm').LastFmNode;
var PlayerServer = require('../player_server');

module.exports = LastFm;

var DB_KEY = 'Plugin.lastfm';

function LastFm(gb) {
  this.gb = gb;

  this.previousNowPlaying = null;
  this.lastPlayingItem = null;
  this.playingStart = new Date();
  this.playingTime = 0;
  this.previousIsPlaying = false;
  this.scrobblers = {};
  this.scrobbles = [];

  this.lastFm = new LastFmNode({
    api_key: this.gb.config.lastFmApiKey,
    secret: this.gb.config.lastFmApiSecret,
  });

  this.gb.player.on('playlistUpdate', checkScrobble.bind(this));
  this.gb.player.on('playlistUpdate', updateNowPlaying.bind(this));

  this.initActions();
}

LastFm.prototype.initialize = function(cb) {
  var self = this;

  self.gb.db.get(DB_KEY, function(err, value) {
    if (err) {
      if (err.type !== 'NotFoundError') return cb(err);
    } else {
      var state = JSON.parse(value);
      self.scrobblers = state.scrobblers;
      self.scrobbles = state.scrobbles;
    }
    // in case scrobbling fails and then the user presses stop, this will still
    // flush the queue.
    setInterval(self.flushScrobbleQueue.bind(self), 120000);
    cb();
  });
};

LastFm.prototype.persist = function() {
  var self = this;
  var state = {
    scrobblers: self.scrobblers,
    scrobbles: self.scrobbles,
  };
  self.gb.db.put(DB_KEY, JSON.stringify(state), function(err) {
    if (err) {
      console.error("Unable to persist lastfm state to db:", err.stack);
    }
  });
}

LastFm.prototype.initActions = function() {
  var self = this;

  PlayerServer.plugins.push({
    handleNewClient: function(client) {
      client.sendMessage('LastFmApiKey', self.gb.config.lastFmApiKey);
    },
  });

  PlayerServer.actions.LastFmGetSession = {
    permission: 'read',
    args: 'string',
    fn: function(playerServer, client, token){
      self.lastFm.request("auth.getSession", {
        token: token,
        handlers: {
          success: function(data){
            delete self.scrobblers[data.session.name];
            client.sendMessage('LastFmGetSessionSuccess', data);
          },
          error: function(error){
            console.error("error from last.fm auth.getSession:", error.message);
            client.sendMessage('LastFmGetSessionError', error.message);
          }
        }
      });
    }
  };

  PlayerServer.actions.LastFmScrobblersAdd = {
    permission: 'read',
    args: 'object',
    fn: function(playerServer, client, params) {
      var existingUser = self.scrobblers[params.username];
      if (existingUser) {
        console.warn("Trying to overwrite a scrobbler:", params.username);
        return;
      }
      self.scrobblers[params.username] = params.session_key;
      self.persist();
    },
  };

  PlayerServer.actions.LastFmScrobblersRemove = {
    permission: 'read',
    args: 'object',
    fn: function(playerServer, client, params) {
      var sessionKey = self.scrobblers[params.username];
      if (sessionKey !== params.session_key) {
        console.warn("Invalid session key from user trying to remove scrobbler:", params.username);
        return;
      }
      delete self.scrobblers[params.username];
      self.persist();
    },
  };
}

LastFm.prototype.flushScrobbleQueue = function() {
  var self = this;
  var params;
  var maxSimultaneous = 10;
  var count = 0;
  while ((params = self.scrobbles.shift()) != null && count++ < maxSimultaneous) {
    console.info("scrobbling " + params.track + " for session " + params.sk);
    params.handlers = {
      error: onError,
    };
    self.lastFm.request('track.scrobble', params);
  }
  self.persist();

  function onError(error){
    console.error("error from last.fm track.scrobble:", error.stack);
    if (!error.code || error.code === 11 || error.code === 16) {
      // try again
      self.scrobbles.push(params);
      self.persist();
    }
  }
}

LastFm.prototype.queueScrobble = function(params){
  console.info("queueScrobble", params);
  this.scrobbles.push(params);
  this.persist();
};

function checkScrobble() {
  var self = this;

  if (self.gb.player.isPlaying && !self.previousIsPlaying) {
    self.playingStart = new Date(new Date() - self.playingTime);
    self.previousIsPlaying = true;
  }
  self.playingTime = new Date() - self.playingStart;

  var thisItem = self.gb.player.currentTrack;
  if (thisItem === self.lastPlayingItem) return;

  if (self.lastPlayingItem) {

    var dbFile = self.gb.player.libraryIndex.trackTable[self.lastPlayingItem.key];

    var minAmt = 15 * 1000;
    var maxAmt = 4 * 60 * 1000;
    var halfAmt = dbFile.duration / 2 * 1000;

    if (self.playingTime >= minAmt && (self.playingTime >= maxAmt || self.playingTime >= halfAmt)) {
      if (dbFile.artistName) {
        for (var username in self.scrobblers) {
          var sessionKey = self.scrobblers[username];
          self.queueScrobble({
            sk: sessionKey,
            chosenByUser: +!self.lastPlayingItem.isRandom,
            timestamp: Math.round(self.playingStart.getTime() / 1000),
            album: dbFile.albumName,
            track: dbFile.name,
            artist: dbFile.artistName,
            albumArtist: dbFile.albumArtistName,
            duration: Math.round(dbFile.duration),
            trackNumber: dbFile.track,
          });
        }
        self.flushScrobbleQueue();
      } else {
        console.warn("Not scrobbling " + dbFile.name + " - missing artist.");
      }
    } else {
      console.info("not scrobbling", dbFile.name, " - only listened for", self.playingTime);
    }
  }
  self.lastPlayingItem = thisItem;
  self.previousIsPlaying = self.gb.player.isPlaying;
  self.playingStart = new Date();
  self.playingTime = 0;
}

function updateNowPlaying() {
  var self = this;

  if (!self.gb.player.isPlaying) return;

  var track = self.gb.player.currentTrack;
  if (!track) return;

  if (self.previousNowPlaying === track) return;
  self.previousNowPlaying = track;

  var dbFile = self.gb.player.libraryIndex.trackTable[track.key];
  if (!dbFile.artistName) {
    console.warn("Not updating last.fm now playing for " + dbFile.name + ": missing artist");
    return;
  }

  for (var username in self.scrobblers) {
    var sessionKey = self.scrobblers[username];
    var props = {
      sk: sessionKey,
      track: dbFile.name,
      artist: dbFile.artistName,
      album: dbFile.albumName,
      albumArtist: dbFile.albumArtistName,
      trackNumber: dbFile.track,
      duration: Math.round(dbFile.duration),
      handlers: {
        error: onError
      }
    }
    console.info("updateNowPlaying", props);
    self.lastFm.request("track.updateNowPlaying", props);
  }

  function onError(error){
    console.error("unable to update last.fm now playing:", error.message);
  }
}
