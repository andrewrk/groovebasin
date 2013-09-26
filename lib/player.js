var groove = require('groove');
var EventEmitter = require('events').EventEmitter;
var util = require('util');
var path = require('path');
var Pend = require('pend');
var chokidar = require('chokidar');
var shuffle = require('mess');

module.exports = Player;

//groove.setLogging(groove.LOG_WARNING);

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

Player.REPEAT_OFF = 0;
Player.REPEAT_ONE = 1;
Player.REPEAT_ALL = 2;

util.inherits(Player, EventEmitter);
function Player(db, musicDirectory) {
  EventEmitter.call(this);
  this.db = db;
  this.musicDirectory = musicDirectory;
  this.allDbFiles = {};
  this.addQueue = new Pend();
  this.addQueue.max = 10;

  this.player = null; // initialized by initialize method

  this.playlist = {};
  this.currentTrack = null;
  this.tracksInOrder = []; // another way to look at playlist
  this.grooveItems = {}; // maps groove item id to track
  this.seekRequestPos = -1; // set to >= 0 when we want to seek

  this.repeat = Player.REPEAT_OFF;
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
      self.volume = self.player.getVolume();
      self.player.on('nowplaying', function() {
        var playHead = self.player.position();
        var decodeHead = self.player.decodePosition();
        if (playHead.item) {
          var nowMs = (new Date()).getTime();
          var posMs = playHead.pos * 1000;
          self.trackStartDate = new Date(nowMs - posMs);
          self.currentTrack = self.grooveItems[playHead.item.id];
          playlistChanged(self);
        } else if (!decodeHead.item) {
          // both play head and decode head are null. end of playlist.
          console.log("end of playlist");
          self.currentTrack = null;
          playlistChanged(self);
        }
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

Player.prototype.setVolume = function(value) {
  value = Math.min(1.0, value);
  value = Math.max(0.0, value);
  this.volume = value;
  this.player.setVolume(value);
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
  shuffle(this.tracksInOrder);
  // fix sort_key and index properties
  this.tracksInOrder.forEach(function(track, index) {
    track.index = index;
    track.sort_key = index;
  });
  playlistChanged(this);
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
  } else if (!this.isPlaying) {
    this.trackStartDate = new Date(new Date() - this.pausedTime * 1000);
  }
  this.player.play();
  this.isPlaying = true;
  playlistChanged(this);
}

Player.prototype.playId = function(id) {
  this.currentTrack = this.playlist[id];
  this.isPlaying = true;
  this.player.play();
  this.seekRequestPos = 0;
  playlistChanged(this);
}

Player.prototype.setRepeat = function(value) {
  this.repeat = value;
  playlistChanged(this);
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

// generate self.tracksInOrder from self.playlist
function cacheTracksArray(self) {
  self.tracksInOrder = Object.keys(self.playlist).map(trackById);
  self.tracksInOrder.sort(asc);
  self.tracksInOrder.forEach(function(track, index) {
    track.index = index;
  });

  function asc(a, b) {
    return operatorCompare(a.sort_key, b.sort_key);
  }
  function trackById(id) {
    return self.playlist[id];
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

  self.emit('playlistUpdate');

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

  var groovePlaylist = self.player.playlist();
  var playHead = self.player.position();
  var playHeadItemId = playHead.item && playHead.item.id;
  var groovePlIndex = 0;
  var grooveItem;

  while (groovePlIndex < groovePlaylist.length) {
    grooveItem = groovePlaylist[groovePlIndex];
    if (grooveItem.id === playHeadItemId) break;
    // this groove playlist item is before the current playhead. delete it!
    self.player.remove(grooveItem);
    delete self.grooveItems[grooveItem.id];
    groovePlIndex += 1;
  }

  var plItemIndex = self.currentTrack.index;
  var plTrack;
  var currentGrooveItem = null; // might be different than playHead.item
  var groovePlItemCount = 0;
  while (groovePlIndex < groovePlaylist.length) {
    grooveItem = groovePlaylist[groovePlIndex];
    var grooveTrack = self.grooveItems[grooveItem.id];
    // now we have deleted all items before the current track. we are now
    // comparing the libgroove playlist and the groovebasin playlist
    // side by side.
    plTrack = self.tracksInOrder[plItemIndex];
    if (grooveTrack === plTrack) {
      // if they're the same, we advance
      currentGrooveItem = currentGrooveItem || grooveItem;
      groovePlIndex += 1;
      incrementPlIndex();
      continue;
    }

    // this groove track is wrong. delete it.
    self.player.remove(grooveItem);
    delete self.grooveItems[grooveItem.id];
    groovePlIndex += 1;
  }

  while (groovePlItemCount < NEXT_FILE_COUNT) {
    plTrack = self.tracksInOrder[plItemIndex];
    if (!plTrack || !plTrack.grooveFile) {
      // we can't do anything
      break;
    }
    grooveItem = self.player.insert(plTrack.grooveFile);
    self.grooveItems[grooveItem.id] = plTrack;
    currentGrooveItem = currentGrooveItem || grooveItem;
    incrementPlIndex();
  }

  if (currentGrooveItem) {
    if (currentGrooveItem.id !== playHeadItemId && self.seekRequestPos < 0) {
      self.seekRequestPos = 0;
    }
    if (self.seekRequestPos >= 0) {
      var seekPos = self.seekRequestPos;
      self.player.seek(currentGrooveItem, seekPos);
      self.seekRequestPos = -1;
      var nowMs = (new Date()).getTime();
      var posMs = seekPos * 1000;
      self.trackStartDate = new Date(nowMs - posMs);
    }
  }

  function incrementPlIndex() {
    groovePlItemCount += 1;
    if (self.repeat !== Player.REPEAT_ONE) {
      plItemIndex += 1;
      if (self.repeat === Player.REPEAT_ALL && plItemIndex >= self.tracksInOrder.length) {
        plItemIndex = 0;
      }
    }
  }
}

function removePreloadFromTrack(self, track) {
  if (!track.grooveFile) return;
  var file = track.grooveFile;
  track.grooveFile = null;
  closeFile(file);
}

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

