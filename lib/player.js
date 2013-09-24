var groove = require('groove');
var EventEmitter = require('events').EventEmitter;
var util = require('util');
var assert = require('assert');
var path = require('path');
var Pend = require('pend');
var chokidar = require('chokidar');

module.exports = Player;

var LIBRARY_KEY_PREFIX = "Library.";

// if we can't read a file that ends in one of these extension,
// suppress the warning.
var IGNORE_OK_EXTS = [
  ".jpg", ".url", ".nfo", ".xml", ".ini", ".m3u", ".sfv", ".txt", ".png",
];

// how many GrooveFiles to keep open, ready to be decoded
var OPEN_FILE_COUNT = 8;
var PREV_FILE_COUNT = Math.floor(OPEN_FILE_COUNT / 2);
var NEXT_FILE_COUNT = OPEN_FILE_COUNT - PREV_FILE_COUNT;


var actions = {
  'addid': {
    permission: 'add',
    fn: function(client, msg, cb) {
      this.addItems(msg.items);
      cb({});
    },
  },
  'clear': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.clearPlaylist();
      cb({});
    },
  },
  'currentsong': {
    permission: 'read',
    fn: function(client, msg, cb) {
      cb({msg: this.currentTrack && this.currentTrack.id});
    },
  },
  'deleteid': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.removePlaylistItems(msg.ids);
      cb({});
    },
  },
  'listallinfo': {
    permission: 'read',
    fn: function(client, msg, cb) {
      cb({msg: this.allDbFiles});
    },
  },
  'move': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.movePlaylistItems(msg.items);
      cb({});
    },
  },
  'password': {
    permission: null,
    fn: function(client, msg, cb) {
      this.authenticateWithPassword(client, msg.password);
      cb({});
    },
  },
  'pause': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.pause();
      cb({});
    },
  },
  'play': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.play();
      cb({});
    },
  },
  'playid': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.playId(msg.track_id);
      cb({});
    },
  },
  'playlistinfo': {
    permission: 'read',
    fn: function(client, msg, cb) {
      cb({msg: serializePlaylist(this.playlist)});
    },
  },
  'repeat': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.setRepeatOn(!!msg.repeat);
      this.setRepeatSingle(!!msg.single);
      cb({});
    },
  },
  'seek': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.seek(msg.pos);
      cb({});
    },
  },
  'shuffle': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.shufflePlaylist();
      cb({});
    },
  },
  'status': {
    permission: 'read',
    fn: function(client, msg, cb) {
      cb({msg: {
        volume: null,
        repeat: this.repeat.repeat,
        single: this.repeat.single,
        state: this.isPlaying ? 'play' : 'pause',
        track_start_date: this.trackStartDate,
        paused_time: this.pausedTime,
      }});
    },
  },
  'stop': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.stop();
      cb({});
    },
  },
};

util.inherits(Player, EventEmitter);
function Player(gb) {
  EventEmitter.call(this);
  this.gb = gb;
  this.db = gb.db;
  this.musicDirectory = gb.config.musicDirectory;
  this.allDbFiles = {};
  this.addQueue = new Pend();
  this.addQueue.max = 10;

  this.player = null; // initialized by initialize method

  this.playlist = {};
  this.currentTrack = null;
  this.tracksInOrder = []; // another way to look at playlist
  this.grooveItems = {}; // maps groove item id to track
  this.seekRequestPos = -1; // set to >= 0 when we want to seek

  this.repeat = {
    repeat: false,
    single: false
  };
  this.isPlaying = false;
  this.trackStartDate = null;
  this.pausedTime = 0;
}

Player.prototype.initialize = function(cb) {
  var self = this;

  var pend = new Pend();
  pend.go(initPlayer);
  pend.go(initLibrary);
  pend.wait(cb);

  function initPlayer(cb) {
    groove.createPlayer(function(err, player) {
      if (err) {
        cb(err);
        return;
      }
      self.player = player;
      self.player.pause();
      self.player.on('nowplaying', function() {
        var obj = self.player.position();
        if (obj.item) {
          var nowMs = (new Date()).getTime();
          var posMs = obj.pos * 1000;
          self.trackStartDate = new Date(nowMs - posMs);
          self.currentTrack = self.grooveItems[obj.item.id];
        } else {
          self.currentTrack = null;
        }
        playlistChanged(self);
      });
      cb();
    });
  }

  function initLibrary(cb) {
    cacheAllDb(function(err) {
      if (err) {
        cb(err);
        return;
      }

      watchLibrary();
      cb();
    });
  }

  function watchLibrary() {
    var opts = {
      ignored: isFileIgnored,
      persistent: true,
      interval: 5000,
    };
    self.watcher = chokidar.watch(self.musicDirectory, opts);

    self.watcher.on('add', onAddOrChange);
    self.watcher.on('change', onAddOrChange);
    self.watcher.on('unlink', removeFile);

    self.watcher.on('error', function(err) {
      console.error("library watching error:", err.stack);
    });
  }

  function removeFile(file) {
    self.db.del(file, function(err) {
      if (err) {
        console.error("Error deleting", file, err.stack);
        return;
      }
      self.emit('delete', file);
    });
  }

  function onAddOrChange(fullPath, stat) {
    // check the mtime against the mtime of the same file in the db
    var relPath = path.relative(self.musicDirectory, fullPath);
    var dbFile = self.allDbFiles[relPath];
    var fileMtime = stat.mtime.getTime();

    if (dbFile) {
      var dbMtime = dbFile.mtime;

      if (dbMtime >= fileMtime) {
        // the info we have in our db for this file is fresh
        return;
      }
    }
    self.queueAddToLibrary(relPath, fileMtime);
  }

  function cacheAllDb(cb) {
    var stream = self.db.createReadStream({
      start: LIBRARY_KEY_PREFIX,
    });
    stream.on('data', function(data) {
      if (data.key.indexOf(LIBRARY_KEY_PREFIX) !== 0) {
        stream.removeAllListeners();
        stream.destroy();
        cb();
        return;
      }
      var file = data.key.substring(LIBRARY_KEY_PREFIX.length);
      self.allDbFiles[file] = deserializeFileData(data.value);
    });
    stream.on('error', function(err) {
      stream.removeAllListeners();
      stream.destroy();
      cb(err);
    });
    stream.on('close', function() {
      cb();
    });
  }
};

Player.prototype.queueAddToLibrary = function(relPath, mtime) {
  var self = this;
  self.addQueue.go(function(cb) {
    var fullPath = path.join(self.musicDirectory, relPath);
    groove.open(fullPath, function(err, file) {
      if (err) {
        if (!shouldSuppressWarning(relPath)) {
          console.warn("Unable to add to library:", relPath, err.message);
        }
        cb();
        return;
      }
      var prevDbFile = self.allDbFiles[relPath];
      var newDbFile = {
        file: relPath,
        mtime: mtime,
        name: file.getMetadata("title") || trackNameFromFile(relPath),
        artist_name: (file.getMetadata("artist") || "").trim(),
        artist_disambiguation: "",
        album_artist_name: (file.getMetadata("album_artist") || "").trim(),
        album_name: (file.getMetadata("album") || "").trim(),
        track: parseInt(file.getMetadata("track") || "0", 10),
        time: file.duration(),
        year: parseInt(file.getMetadata("date") || "0", 10),
      };
      var pend = new Pend();
      pend.go(function(cb) {
        file.close(cb);
      });
      pend.go(function(cb) {
        self.db.put(LIBRARY_KEY_PREFIX + relPath, serializeFileData(newDbFile), function(err) {
          if (err) {
            console.error("Error saving", relPath, "to db:", err.stack);
            cb();
            return;
          }
          self.allDbFiles[relPath] = newDbFile;
          self.emit('update', prevDbFile, newDbFile);
          cb();
        });
      });
      pend.wait(cb);
    });
  });
};

Player.prototype.authenticate = function(password) {
  return this.gb.config.permissions[password];
};

Player.prototype.createClient = function(socket, permissions) {
  var self = this;
  var client = socket;
  client.permissions = permissions;
  socket.on('request', function(request){
    request = JSON.parse(request);
    self.request(client, request.cmd, function(arg){
      var response = {callback_id: request.callback_id};
      response.err = arg.err;
      response.msg = arg.msg;
      socket.emit('PlayerResponse', JSON.stringify(response));
    });
  });
  self.on('status', function(arg){
    try {
      socket.emit('PlayerStatus', JSON.stringify(arg));
    } catch (e$) {}
  });
  self.on('error', function(msg){
    try {
      socket.emit('MpdError', msg);
    } catch (e$) {}
  });
  socket.emit('Permissions', JSON.stringify(permissions));
  return client;
};


Player.prototype.authenticateWithPassword = function(client, password) {
  var perms = this.authenticate(password);
  var success = perms != null;
  if (success) client.permissions = perms;
  client.emit('Permissions', JSON.stringify(client.permissions));
  client.emit('PasswordResult', JSON.stringify(success));
};

// items looks like [{file, sort_key}]
Player.prototype.addItems = function(items, tagAsRandom) {
  var self = this;
  tagAsRandom = !!tagAsRandom;
  for (var id in items) {
    var item = items[id];
    var playlistItem = {
      id: id,
      file: item.file,
      sort_key: item.sort_key,
      is_random: tagAsRandom,
      time: self.allDbFiles[item.file].time,
      grooveFile: null,
      pendingGrooveFile: false,
      deleted: false,
    };
    self.playlist[id] = playlistItem;
  }
  playlistChanged(self);
}

Player.prototype.clearPlaylist = function() {
  for (var id in this.playlist) {
    var track = this.playlist[id];
    if (track.grooveFile) closeFile(track.grooveFile);
    // we set this so that any callbacks that return which were trying to
    // set the groveItem can check if the item got deleted
    track.deleted = true;
  }
  this.playlist = {};
  this.currentTrack = null;
  playlistChanged(this);
}

Player.prototype.shufflePlaylist = function() {
  console.error("TODO: implement shuffle");
}

Player.prototype.removePlaylistItems = function(ids) {
  ids.forEach(function(id) {
    delete this.playlist[id];
  }.bind(this));
  playlistChanged(this);
}

// items looks like {id: {sort_key}}
Player.prototype.movePlaylistItems = function(items) {
  for (var id in items) {
    this.playlist[id].sort_key = items[id].sort_key;
  }
  playlistChanged(this);
}

Player.prototype.pause = function() {
  if (!this.isPlaying) return;
  this.isPlaying = false;
  this.pausedTime = (new Date() - this.trackStartDate) / 1000;
  this.player.pause();
  playlistChanged(this);
}

Player.prototype.play = function() {
  if (!this.currentTrack) {
    this.currentTrack = this.tracksInOrder[0];
  }
  this.player.play();
  this.isPlaying = true;
  playlistChanged(this);
}

Player.prototype.playId = function(id) {
  this.currentTrack = this.playlist[id];
  this.isPlaying = true;
  this.seekRequestPos = 0;
  playlistChanged(this);
}

Player.prototype.setRepeatOn = function(isOn) {
  // TODO implement
  this.repeat.repeat = isOn;
};

Player.prototype.setRepeatSingle = function(single) {
  // TODO implement
  this.repeat.single = single;
};

Player.prototype.seek = function(pos) {
  this.seekRequestPos = pos;
  playlistChanged(this);
}

Player.prototype.stop = function() {
  this.isPlaying = false;
  this.player.pause();
  this.seekRequestPos = 0;
  playlistChanged(this);
}

function requestObject(self, client, request, cb) {
  var name = request.name;
  var action = actions[name];
  if (! action) {
    console.warn("Invalid command:", name);
    cb({err: "invalid command: " + JSON.stringify(name)});
    return;
  }
  var perm = action.permission;
  if (perm != null && !client.permissions[perm]) {
    var errText = "command " + JSON.stringify(name) +
      " requires permission " + JSON.stringify(perm);
    console.warn("permissions error:", errText);
    cb({err: errText});
    return;
  }
  console.info("ok command", name);
  action.fn.call(self, client, request, cb);
}

Player.prototype.request = function(client, request, cb){
  cb = cb || noop;
  if (typeof request !== 'object') {
    console.warn("ignoring invalid command:", request);
    cb({err: "invalid command: " + JSON.stringify(request)});
    return;
  }
  requestObject(this, client, request, cb);
};

function operatorCompare(a, b) {
  return a < b ? -1 : a > b ? 1 : 0;
}

// TODO: use keese
function generateSortKey(previous_key, next_key){
  if (previous_key != null) {
    if (next_key != null) {
      return (previous_key + next_key) / 2;
    } else {
      return 0 | previous_key + 1;
    }
  } else {
    if (next_key != null) {
      return (0 + next_key) / 2;
    } else {
      return 1;
    }
  }
}

function disambiguateSortKeys(self) {
  var previousUniqueKey = null;
  var previousKey = null;
  self.tracksInOrder.forEach(function(track, i) {
    if (track.sort_key === previousKey) {
      // move the repeat back
      track.sort_key = generateSortKey(previousUniqueKey, track.sort_key);
      previousUniqueKey = track.sort_key;
    } else {
      previousUniqueKey = previousKey;
      previousKey = track.sort_key;
    }
  });
}

function cacheTracksArray(self) {
  self.tracksInOrder = Object.keys(self.playlist).map(function(id) { return self.playlist[id]; });
  self.tracksInOrder.sort(asc);
  self.tracksInOrder.forEach(function(track, index) {
    track.index = index;
  });

  function asc(a, b) {
    return operatorCompare(a.sort_key, b.sort_key);
  }
}

function playlistChanged(self) {
  cacheTracksArray(self);
  disambiguateSortKeys(self);

  if (self.currentTrack) {
    self.tracksInOrder.forEach(function(track, index) {
      var withinPrev = (self.currentTrack.index - index) <= PREV_FILE_COUNT;
      var withinNext = (index - self.currentTrack.index) <= NEXT_FILE_COUNT;
      var shouldHaveGrooveFile = withinPrev || withinNext;
      var hasGrooveFile = track.grooveFile != null || track.pendingGrooveFile;
      if (hasGrooveFile && !shouldHaveGrooveFile) {
        removePreloadFromTrack(self, track);
      } else if (!hasGrooveFile && shouldHaveGrooveFile) {
        preloadFile(self, track);
      }
    });
  } else {
    self.isPlaying = false;
    self.trackStartDate = null;
    self.pausedTime = 0;
  }
  checkUpdateGroovePlaylist(self);
  self.emit('status', ['playlist', 'player']);

}

function preloadFile(self, track) {
  var fullPath = path.join(self.musicDirectory, track.file);
  track.pendingGrooveFile = true;
  groove.open(fullPath, function(err, file) {
    track.pendingGrooveFile = false;
    if (err) {
      console.error("Error opening", track.file, err.stack);
      return;
    }
    if (track.deleted) {
      closeFile(file);
      return;
    }
    track.grooveFile = file;
    checkUpdateGroovePlaylist(self);
  });
}

function checkUpdateGroovePlaylist(self) {
  if (!self.currentTrack) {
    self.player.clear();
    self.grooveItems = {};
    return;
  }

  // delete all items before currentTrack
  var groovePlaylist = self.player.playlist();
  var groovePlIndex = 0;
  var plItemIndex = self.currentTrack.index;
  var grooveItem, plTrack;
  var currentGrooveItem = null;
  while (groovePlIndex < groovePlaylist.length) {
    grooveItem = groovePlaylist[groovePlIndex];
    var grooveTrack = self.grooveItems[grooveItem.id];
    if (grooveTrack.index < self.plItemIndex) {
      self.player.remove(grooveItem);
      delete self.grooveItems[grooveItem.id];
      groovePlIndex += 1;
      continue;
    }
    // now we have deleted all items before the current track. we are now
    // looking at the current pl item and the current groove item.
    plTrack = self.tracksInOrder[plItemIndex];
    if (grooveTrack === plTrack) {
      // if they're the same, we advance
      currentGrooveItem = currentGrooveItem || grooveItem;
      groovePlIndex += 1;
      plItemIndex += 1;
      continue;
    }

    // this groove track is wrong. delete it.
    self.player.remove(grooveItem);
    delete self.grooveItems[grooveItem.id];
    groovePlIndex += 1;
  }

  while (plItemIndex < self.currentTrack.index + NEXT_FILE_COUNT) {
    plTrack = self.tracksInOrder[plItemIndex];
    if (!plTrack || !plTrack.grooveFile) {
      // we can't do anything
      break;
    }
    grooveItem = self.player.insert(plTrack.grooveFile);
    self.grooveItems[grooveItem.id] = plTrack;
    currentGrooveItem = currentGrooveItem || grooveItem;
    plItemIndex += 1;
  }

  if (self.seekRequestPos >= 0 && currentGrooveItem) {
    var seekPos = self.seekRequestPos;
    self.player.seek(currentGrooveItem, seekPos);
    self.seekRequestPos = -1;
    var nowMs = (new Date()).getTime();
    var posMs = seekPos * 1000;
    self.trackStartDate = new Date(nowMs - posMs);
  }
}

function removePreloadFromTrack(self, track) {
  if (!track.grooveFile) return;
  var file = track.grooveFile;
  track.grooveFile = null;
  closeFile(file);
}

function noop() {}

function isFileIgnored(fullPath) {
  var basename = path.basename(fullPath);
  return (/^\./).test(basename) || (/~$/).test(basename);
}

function deserializeFileData(dataStr) {
  var obj = JSON.parse(dataStr);
  return obj;
}

function serializeFileData(dbFile) {
  return JSON.stringify(dbFile);
}

function trackNameFromFile(filename) {
  var basename = path.basename(filename);
  var ext = path.extname(basename);
  return basename.substring(0, basename.length - ext.length);
}

function shouldSuppressWarning(filename) {
  var ext = path.extname(filename).toLowerCase();
  return IGNORE_OK_EXTS.indexOf(ext) !== -1;
}

function getFile(item) {
  return item.file;
}

function closeFile(file) {
  file.close(function(err) {
    if (err) {
      console.error("Error closing", file, err.stack);
    }
  });
}

function serializePlaylist(playlist) {
  var o = {};
  for (var id in playlist) {
    var item = playlist[id];
    o[id] = {
      file: item.file,
      sort_key: item.sort_key,
      is_random: item.is_random,
      time: item.time,
    };
  }
  return o;
}
