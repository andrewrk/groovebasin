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

// how many files to keep open and ready to decode on the underlying
// libgroove playlist
var OPEN_FILE_COUNT = 4;

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
      cb({msg: this.currentId});
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
      cb({msg: this.playlist});
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
  this.currentId = null;
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
          var posMs = obj.pos / 1000;
          self.trackStartDate = new Date(nowMs - posMs);
          self.currentId = obj.item.id;
        } else {
          self.trackStartDate = null;
          self.currentId = null;
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
  tagAsRandom = !!tagAsRandom;
  for (var id in items) {
    var item = items[id];
    var playlistItem = {
      file: item.file,
      sort_key: item.sort_key,
      is_random: tagAsRandom,
      time: this.allDbFiles[item.file].time,
    };
    this.playlist[id] = playlistItem;
  }
  playlistChanged(this);
}

Player.prototype.clearPlaylist = function() {
  this.playlist = {};
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
  playlistChanged(this);
}

Player.prototype.play = function() {
  if (this.currentId == null) this.currentId = findNext(this.playlist, null);
  this.isPlaying = true;
  playlistChanged(this);
}

Player.prototype.playId = function(id) {
  this.currentId = id;
  this.isPlaying = true;
  playlistChanged(this, {
    seekto: 0
  });
}

Player.prototype.setRepeatOn = function(isOn) {
  this.repeat.repeat = isOn;
};

Player.prototype.setRepeatSingle = function(single) {
  this.repeat.single = single;
};

Player.prototype.seek = function(pos) {
  playlistChanged(this, { seekto: pos });
}

Player.prototype.stop = function() {
  this.isPlaying = false;
  playlistChanged(this, {
    seekto: 0
  });
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

// TODO: reduce this code duplication
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
  var tracksInOrder = Object.keys(self.playlist).map(function(id) { return self.playlist[id]; });
  tracksInOrder.sort(function (a, b) { return operatorCompare(a.sort_key, b.sort_key); });
  var previousUniqueKey = null;
  var previousKey = null;
  for (var i = 0; i < tracksInOrder.length; i++) {
    var track = tracksInOrder[i];
    if (track.sort_key === previousKey) {
      // move the repeate back
      track.sort_key = generateSortKey(previousUniqueKey, track.sort_key);
      previousUniqueKey = track.sort_key;
    } else {
      previousUniqueKey = previousKey;
      previousKey = track.sort_key;
    }
  }
}

function playlistChanged(self) {
  self.emit('status', ['playlist', 'player']);

  disambiguateSortKeys(self);
}

function findNext(object, from_id){
  var testObject = object[from_id];
  var from_key = testObject && testObject.sort_key;
  var result = null;
  for (var id in object) {
    var item = object[id];
    if (from_key == null || item.sort_key > from_key) {
      if (result == null || item.sort_key < object[result].sort_key) {
        result = id;
      }
    }
  }
  return result;
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

