var groove = require('groove');
var EventEmitter = require('events').EventEmitter;
var util = require('util');
var fs = require('fs');
var uuid = require('uuid');
var path = require('path');
var Pend = require('pend');
var chokidar = require('chokidar');
var shuffle = require('mess');
var mv = require('mv');
var zfill = require('zfill');
var MusicLibraryIndex = require('music-library-index');
var keese = require('keese');
var safePath = require('./safe_path');
var PassThrough = require('stream').PassThrough;

module.exports = Player;

groove.setLogging(groove.LOG_WARNING);

var cpuCount = require('os').cpus().length;

var LIBRARY_KEY_PREFIX = "Library.";

// if we can't read a file that ends in one of these extension,
// suppress the warning.
var IGNORE_OK_EXTS = [
  ".jpg", ".url", ".nfo", ".xml", ".ini", ".m3u", ".sfv", ".txt", ".png",
];

var DB_FILE_PROPS = [
  'key', 'name', 'artistName', 'albumArtistName',
  'albumName', 'compilation', 'track', 'trackCount',
  'disc', 'discCount', 'duration', 'year', 'genre',
  'file', 'mtime', 'replayGainAlbumGain', 'replayGainAlbumPeak',
  'replayGainTrackGain', 'replayGainTrackPeak',
];

// how many GrooveFiles to keep open, ready to be decoded
var OPEN_FILE_COUNT = 8;
var PREV_FILE_COUNT = Math.floor(OPEN_FILE_COUNT / 2);
var NEXT_FILE_COUNT = OPEN_FILE_COUNT - PREV_FILE_COUNT;

var DB_SCALE = Math.log(10.0) * 0.05;
var REPLAYGAIN_PREAMP = 0.75;
var REPLAYGAIN_DEFAULT = 0.25;

Player.REPEAT_OFF = 0;
Player.REPEAT_ONE = 1;
Player.REPEAT_ALL = 2;

Player.trackWithoutIndex = trackWithoutIndex;

util.inherits(Player, EventEmitter);
function Player(db, musicDirectory) {
  EventEmitter.call(this);

  this.db = db;
  this.musicDirectory = musicDirectory;
  this.dbFilesByPath = {};
  this.libraryIndex = new MusicLibraryIndex();
  this.addQueue = new Pend();
  this.addQueue.max = cpuCount;

  this.groovePlayer = null; // initialized by initialize method
  this.groovePlaylist = null; // initialized by initialize method

  this.playlist = {};
  this.currentTrack = null;
  this.tracksInOrder = []; // another way to look at playlist
  this.grooveItems = {}; // maps groove item id to track
  this.seekRequestPos = -1; // set to >= 0 when we want to seek

  this.repeat = Player.REPEAT_OFF;
  this.isPlaying = false;
  this.trackStartDate = null;
  this.pausedTime = 0;

  this.ongoingTrackScans = {};
  this.ongoingAlbumScans = {};
  this.scanQueue = new Pend();
  this.scanQueue.max = cpuCount;

  this.headerBuffers = [];
  this.newHeaderBuffers = [];
  this.encodedAudioStream = new PassThrough();
  this.lastEncodeItem = null;
  this.lastEncodePos = null;
  this.expectHeaders = true;
}

Player.prototype.initialize = function(cb) {
  var self = this;

  var pend = new Pend();
  pend.go(initPlayer);
  pend.go(initLibrary);
  pend.wait(cb);

  function initPlayer(cb) {
    var groovePlaylist = groove.createPlaylist();
    var groovePlayer = groove.createPlayer();
    var grooveEncoder = groove.createEncoder();
    grooveEncoder.formatShortName = "mp3";
    grooveEncoder.codecShortName = "mp3";
    grooveEncoder.bitRate = 256 * 1000;

    var pend = new Pend();
    pend.go(function(cb) {
      groovePlayer.attach(groovePlaylist, cb);
    });
    pend.go(function(cb) {
      grooveEncoder.attach(groovePlaylist, cb);
    });
    pend.wait(doneAttaching);

    function doneAttaching(err) {
      if (err) {
        cb(err);
        return;
      }
      self.groovePlaylist = groovePlaylist;
      self.groovePlayer = groovePlayer;
      self.grooveEncoder = grooveEncoder;
      self.groovePlaylist.pause();
      self.volume = self.groovePlaylist.volume;
      self.groovePlayer.on('nowplaying', onNowPlaying);
      self.flushEncodedInterval = setInterval(flushEncoded, 10);
      cb();

      function flushEncoded() {
        // poll the encoder for more buffers until either there are no buffers
        // available or we get enough buffered
        while (1) {
          var bufferedSeconds = self.secondsIntoFuture(self.lastEncodeItem, self.lastEncodePos);
          if (bufferedSeconds > 0.5) return;
          var buf = self.grooveEncoder.getBuffer();
          if (!buf) return;
          if (buf.buffer) {
            if (buf.item) {
              if (self.expectHeaders) {
                console.log("encoder: got first non-header");
                self.headerBuffers = self.newHeaderBuffers;
                self.newHeaderBuffers = [];
                self.expectHeaders = false;
              }
              self.encodedAudioStream.write(buf.buffer);
              self.lastEncodeItem = buf.item;
              self.lastEncodePos = buf.pos;
            } else if (self.expectHeaders) {
              // this is a header
                console.log("encoder: got header");
              self.newHeaderBuffers.push(buf.buffer);
            } else {
              // it's a footer, ignore the fuck out of it
              console.info("ignoring encoded audio footer");
            }
          } else {
            // end of playlist sentinel
            console.log("encoder: end of playlist sentinel");
            self.expectHeaders = true;
          }
        }
      }

      function onNowPlaying() {
        var playHead = self.groovePlayer.position();
        var decodeHead = self.groovePlaylist.position();
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
      }
    }
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
    self.watcher.on('unlink', onFileMissing);

    self.watcher.on('error', function(err) {
      console.error("library watching error:", err.stack);
    });
  }

  function onFileMissing(fullPath) {
    var relPath = path.relative(self.musicDirectory, fullPath);
    var dbFile = self.dbFilesByPath[relPath];
    if (!dbFile) {
      console.warn("File reported deleted that was not in our db:", fullPath);
      return;
    }
    self.delDbEntry(dbFile);
  }

  function onAddOrChange(fullPath, stat) {
    // check the mtime against the mtime of the same file in the db
    var relPath = path.relative(self.musicDirectory, fullPath);
    var dbFile = self.dbFilesByPath[relPath];
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
      var dbFile = deserializeFileData(data.value);
      self.libraryIndex.addTrack(dbFile);
      self.dbFilesByPath[dbFile.file] = dbFile;
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

Player.prototype.secondsIntoFuture = function(groovePlaylistItem, pos) {
  if (!groovePlaylistItem || !pos) {
    return 0;
  }

  var item = this.grooveItems[groovePlaylistItem.id];

  if (item === this.currentTrack) {
    var curPos = this.isPlaying ?
      ((new Date() - this.trackStartDate) / 1000.0) : this.pausedTime;
    return pos - curPos;
  } else {
    return pos;
  }
};

Player.prototype.streamMiddleware = function(req, resp, next) {
  var self = this;
  if (req.path !== '/stream.ogg') return next();

  resp.setHeader('Content-Type', 'audio/vorbis');
  resp.statusCode = 200;

  var count = 0;
  self.headerBuffers.forEach(function(headerBuffer) {
    count += headerBuffer.length;
    resp.write(headerBuffer);
  });
  console.log("sent", count, "bytes of headers");
  self.encodedAudioStream.pipe(resp);
  req.on('abort', function() {
    self.encodedAudioStream.unpipe(resp);
    resp.end();
  });
};

Player.prototype.deleteFile = function(key) {
  var self = this;
  var dbFile = self.libraryIndex.trackTable[key];
  if (!dbFile) {
    console.error("Error deleting file - no entry:", key);
    return;
  }
  var fullPath = path.join(self.musicDirectory, dbFile.file);
  fs.unlink(fullPath, function(err) {
    if (err) {
      console.error("Error deleting", dbFile.file, err.stack);
    }
  });
  self.delDbEntry(dbFile);
};

Player.prototype.delDbEntry = function(dbFile) {
  var self = this;
  self.libraryIndex.removeTrack(dbFile.key);
  delete self.dbFilesByPath[dbFile.file];
  self.emit('delete', dbFile);
  self.db.del(LIBRARY_KEY_PREFIX + dbFile.key, function(err) {
    if (err) {
      console.error("Error deleting db entry", dbFile.key, err.stack);
    }
  });
};

Player.prototype.setVolume = function(value) {
  value = Math.min(1.0, value);
  value = Math.max(0.0, value);
  this.volume = value;
  this.groovePlaylist.setVolume(value);
};

Player.prototype.importFile = function(fullPath, filenameHint, cb) {
  var self = this;

  var pend = new Pend();
  var origStat;
  pend.go(function(cb) {
    fs.stat(fullPath, function(err, stat) {
      origStat = stat;
      cb(err);
    });
  });
  groove.open(fullPath, function(err, file) {
    if (err) return cb(err);
    var newDbFile = grooveFileToDbFile(file, filenameHint);
    var suggestedPath = self.getSuggestedPath(newDbFile, filenameHint);
    pend.go(testSuggestedPath);
    pend.go(function(cb) {
      file.close(cb);
    });
    pend.wait(function(err) {
      if (err) return cb(err);
      var newPath = path.join(self.musicDirectory, suggestedPath);
      mv(fullPath, newPath, {mkdirp: true}, function(err) {
        if (err) return cb(err);
        newDbFile.file = suggestedPath;
        newDbFile.mtime = origStat.mtime.getTime();
        self.persist(newDbFile, cb);
      });
    });

    function testSuggestedPath(cb) {
      fs.stat(suggestedPath, function(err, stat) {
        if (err) {
          if (err.code === 'ENOENT') {
            cb();
            return;
          } else {
            cb(err);
          }
        }
        // mangle the suggested path and try again
        suggestedPath = uniqueFilename(suggestedPath);
        pend.go(testSuggestedPath);
        cb();
      });
    }
  });
};

Player.prototype.persist = function(dbFile, cb) {
  cb = cb || logIfError;
  var prevDbFile = this.libraryIndex.trackTable[dbFile.key];
  this.libraryIndex.addTrack(dbFile);
  this.dbFilesByPath[dbFile.file] = dbFile;
  this.emit('update', prevDbFile, dbFile);
  this.db.put(LIBRARY_KEY_PREFIX + dbFile.key, serializeFileData(dbFile), cb);

  function logIfError(err) {
    if (err) {
      console.error("unable to persist db entry:", dbFile, err.stack);
    }
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
        cb(err);
        return;
      }
      var newDbFile = grooveFileToDbFile(file, relPath);
      newDbFile.file = relPath;
      newDbFile.mtime = mtime;
      var pend = new Pend();
      pend.go(function(cb) {
        file.close(cb);
      });
      pend.go(function(cb) {
        self.persist(newDbFile, function(err) {
          if (err) console.error("Error saving", relPath, "to db:", err.stack);
          cb();
        });
      });
      pend.wait(cb);
    });
  });
};

Player.prototype.appendTracks = function(keys, tagAsRandom) {
  var lastTrack = this.tracksInOrder[this.tracksInOrder.length - 1];
  var items = {};
  var lastSortKey = lastTrack ? lastTrack.sortKey : keese();
  keys.forEach(function(key) {
    var id = uuid();
    var nextSortKey = keese(lastSortKey, null);
    lastSortKey = nextSortKey;
    items[id] = {
      key: key,
      sortKey: nextSortKey,
    };
  });
  this.addItems(items, tagAsRandom);
};

// items looks like {id: {key, sortKey}}
Player.prototype.addItems = function(items, tagAsRandom) {
  var self = this;
  tagAsRandom = !!tagAsRandom;
  for (var id in items) {
    var item = items[id];
    var playlistItem = {
      id: id,
      key: item.key,
      sortKey: item.sortKey,
      isRandom: tagAsRandom,
      grooveFile: null,
      pendingGrooveFile: false,
      deleted: false,
    };
    self.playlist[id] = playlistItem;
  }
  playlistChanged(self);
  lazyReplayGainScanPlaylist(self);
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
  // fix sortKey and index properties
  var nextSortKey = keese(null, null);
  this.tracksInOrder.forEach(function(track, index) {
    track.index = index;
    track.sortKey = nextSortKey;
    nextSortKey = keese(nextSortKey, null);
  });
  playlistChanged(this);
}

Player.prototype.removePlaylistItems = function(ids) {
  ids.forEach(function(id) {
    delete this.playlist[id];
  }.bind(this));
  playlistChanged(this);
}

// items looks like {id: {sortKey}}
Player.prototype.movePlaylistItems = function(items) {
  for (var id in items) {
    this.playlist[id].sortKey = items[id].sortKey;
  }
  playlistChanged(this);
}

Player.prototype.pause = function() {
  if (!this.isPlaying) return;
  this.isPlaying = false;
  this.pausedTime = (new Date() - this.trackStartDate) / 1000;
  this.groovePlaylist.pause();
  playlistChanged(this);
}

Player.prototype.play = function() {
  if (!this.currentTrack) {
    this.currentTrack = this.tracksInOrder[0];
  } else if (!this.isPlaying) {
    this.trackStartDate = new Date(new Date() - this.pausedTime * 1000);
  }
  this.groovePlaylist.play();
  this.isPlaying = true;
  playlistChanged(this);
}

Player.prototype.playId = function(id) {
  this.currentTrack = this.playlist[id];
  this.isPlaying = true;
  this.groovePlaylist.play();
  this.seekRequestPos = 0;
  playlistChanged(this);
}

Player.prototype.setRepeat = function(value) {
  value = Math.floor(value);
  if (value !== Player.REPEAT_ONE &&
      value !== Player.REPEAT_ALL &&
      value !== Player.REPEAT_OFF)
  {
    return;
  }
  this.repeat = value;
  playlistChanged(this);
};

Player.prototype.seek = function(pos) {
  this.seekRequestPos = pos;
  playlistChanged(this);
}

Player.prototype.stop = function() {
  this.isPlaying = false;
  this.groovePlaylist.pause();
  this.seekRequestPos = 0;
  playlistChanged(this);
}

Player.prototype.getSuggestedPath = function(track, filenameHint) {
  var p = "";
  if (track.albumArtistName) {
    p = path.join(p, safePath(track.albumArtistName));
  } else if (track.compilation) {
    p = path.join(p, safePath(this.libraryIndex.variousArtistsName));
  }
  if (track.albumName) {
    p = path.join(p, safePath(track.albumName));
  }
  var t = "";
  if (track.track != null) {
    t += safePath(zfill(track.track, 2)) + " ";
  }
  t += safePath(track.name + path.extname(filenameHint));
  return path.join(p, t);
};

Player.prototype.performScan = function(dbFile) {
  var self = this;

  var scanTable, scanKey, scanType;
  if (dbFile.albumName) {
    scanType = 'album';
    scanKey = self.libraryIndex.getAlbumKey(dbFile);
    scanTable = self.ongoingAlbumScans;
  } else {
    scanType = 'track';
    scanKey = dbFile.key;
    scanTable = self.ongoingTrackScans;
  }

  if (scanTable[scanKey]) {
    console.warn("Not interrupting ongoing scan.");
    return;
  }
  var scanContext = {
    scan: null,
    files: {},
    aborted: false,
    started: false,
    timeout: null,
  };
  scanTable[scanKey] = scanContext;
  self.scanQueue.go(doIt);

  function doIt(cb) {
    var fileList = [];
    var pend = new Pend();
    pend.max = cpuCount;
    if (scanContext.aborted) return cleanupAndCb();
    var trackList;
    if (scanType === 'album') {
      var albumKey = scanKey;
      self.libraryIndex.rebuildAlbumTable();
      var album = self.libraryIndex.albumTable[albumKey];
      if (!album) {
        console.warn("wanted to scan album with key", JSON.stringify(albumKey), "but no longer exists.");
        cleanupAndCb();
        return;
      }
      console.info("Replaygain album scan starting:", JSON.stringify(albumKey));
      trackList = album.trackList;
    } else if (scanType === 'track') {
      var trackKey = scanKey;
      var dbFile = self.libraryIndex.trackTable[trackKey];
      console.info("Track scan starting:", JSON.stringify(trackKey));
      trackList = [dbFile];
    } else {
      throw new Error("unexpected scan type");
    }
    trackList.forEach(function(track) {
      pend.go(function(cb) {
        var fullPath = path.join(self.musicDirectory, track.file);
        groove.open(fullPath, function(err, file) {
          if (err) {
            console.error("Error opening", fullPath, "in order to scan:", err.stack);
          } else {
            scanContext.files[file.id] = {
              track: track,
              progress: 0.0,
              gain: null,
              peak: null,
            };
            fileList.push(file);
          }
          cb();
        });
      });
    });
    pend.wait(function() {
      if (scanContext.aborted) return cleanupAndCb();

      var scan = groove.createReplayGainScan(fileList, 10);
      scanContext.scan = scan;
      scan.on('progress', function(file, progress) {
        scanContext.files[file.id].progress = progress;
      });
      scan.on('file', function(file, gain, peak) {
        var fileInfo = scanContext.files[file.id];
        console.info("replaygain scan file complete:", fileInfo.track.name, "gain", gain);
        fileInfo.progress = 1.0;
        fileInfo.gain = gain;
        fileInfo.peak = peak;
        fileInfo.track.replayGainTrackGain = gain;
        fileInfo.track.replayGainTrackPeak = peak;
        checkUpdateGroovePlaylist(self);
      });
      scan.on('error', function(err) {
        console.error("Error scanning", JSON.stringify(scanKey), err.stack);
        scan.removeAllListeners();
        cleanupAndCb();
      });
      scan.on('end', function(gain, peak) {
        if (scanContext.aborted) return cleanupAndCb();

        console.info("Replaygain scan complete:", JSON.stringify(scanKey), "gain", gain);
        delete scanTable[scanKey];
        for (var fileId in scanContext.files) {
          var scanFileContext = scanContext.files[fileId];
          var dbFile = scanFileContext.track;
          dbFile.replayGainAlbumGain = gain;
          dbFile.replayGainAlbumPeak = peak;
          self.persist(dbFile);
          checkUpdateGroovePlaylist(self);
        }
        cleanupAndCb();
      });
    });
    function cleanupAndCb() {
      fileList.forEach(function(file) {
        pend.go(function(cb) {
          file.close(cb);
        });
      });
      pend.wait(cb);
    }
  }
};

function operatorCompare(a, b) {
  return a < b ? -1 : a > b ? 1 : 0;
}

function disambiguateSortKeys(self) {
  var previousUniqueKey = null;
  var previousKey = null;
  self.tracksInOrder.forEach(function(track, i) {
    if (track.sortKey === previousKey) {
      // move the repeat back
      track.sortKey = keese(previousUniqueKey, track.sortKey);
      previousUniqueKey = track.sortKey;
    } else {
      previousUniqueKey = previousKey;
      previousKey = track.sortKey;
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
    return operatorCompare(a.sortKey, b.sortKey);
  }
  function trackById(id) {
    return self.playlist[id];
  }
}

function lazyReplayGainScanPlaylist(self) {
  var albumGain = {};
  self.tracksInOrder.forEach(function(track) {
    var dbFile = self.libraryIndex.trackTable[track.key];
    var albumKey = self.libraryIndex.getAlbumKey(dbFile);
    var needScan = dbFile.replayGainAlbumGain == null ||
        dbFile.replayGainTrackGain == null ||
        (albumGain[albumKey] && albumGain[albumKey] !== dbFile.replayGainAlbumGain);
    if (needScan) {
      self.performScan(dbFile);
    } else {
      albumGain[albumKey] = dbFile.replayGainAlbumGain;
    }
  });
}

function playlistChanged(self) {
  cacheTracksArray(self);
  disambiguateSortKeys(self);

  self.lastEncodeItem = null;
  self.lastEncodePos = null;

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
  var relPath = self.libraryIndex.trackTable[track.key].file;
  var fullPath = path.join(self.musicDirectory, relPath);
  track.pendingGrooveFile = true;
  groove.open(fullPath, function(err, file) {
    track.pendingGrooveFile = false;
    if (err) {
      console.error("Error opening", relPath, err.stack);
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
    self.groovePlaylist.clear();
    self.grooveItems = {};
    return;
  }

  var groovePlaylist = self.groovePlaylist.items();
  var playHead = self.groovePlayer.position();
  var playHeadItemId = playHead.item && playHead.item.id;
  var groovePlIndex = 0;
  var grooveItem;

  while (groovePlIndex < groovePlaylist.length) {
    grooveItem = groovePlaylist[groovePlIndex];
    if (grooveItem.id === playHeadItemId) break;
    // this groove playlist item is before the current playhead. delete it!
    self.groovePlaylist.remove(grooveItem);
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
      // but we might have to correct the gain
      self.groovePlaylist.setItemGain(grooveItem, calcGain(plTrack));
      currentGrooveItem = currentGrooveItem || grooveItem;
      groovePlIndex += 1;
      incrementPlIndex();
      continue;
    }

    // this groove track is wrong. delete it.
    self.groovePlaylist.remove(grooveItem);
    delete self.grooveItems[grooveItem.id];
    groovePlIndex += 1;
  }

  while (groovePlItemCount < NEXT_FILE_COUNT) {
    plTrack = self.tracksInOrder[plItemIndex];
    if (!plTrack || !plTrack.grooveFile) {
      // we can't do anything
      break;
    }
    // compute the gain adjustment
    grooveItem = self.groovePlaylist.insert(plTrack.grooveFile, calcGain(plTrack));
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
      self.groovePlaylist.seek(currentGrooveItem, seekPos);
      self.seekRequestPos = -1;
      var nowMs = (new Date()).getTime();
      var posMs = seekPos * 1000;
      self.trackStartDate = new Date(nowMs - posMs);
    }
  }

  function calcGain(plTrack) {
    // if the previous item is the previous item from the album, or the
    // next item is the next item from the album, use album replaygain.
    // else, use track replaygain.
    var dbFile = self.libraryIndex.trackTable[plTrack.key];
    var albumMode = albumInfoMatch(-1) || albumInfoMatch(1);

    var gain = REPLAYGAIN_PREAMP;
    if (dbFile.replayGainAlbumGain != null && albumMode) {
      gain *= dBToFloat(dbFile.replayGainAlbumGain);
    } else if (dbFile.replayGainTrackGain != null) {
      gain *= dBToFloat(dbFile.replayGainTrackGain);
    } else {
      gain *= REPLAYGAIN_DEFAULT;
    }
    return gain;

    function albumInfoMatch(dir) {
      var otherPlTrack = self.tracksInOrder[plTrack.index + dir];
      if (!otherPlTrack) return false;

      var otherDbFile = self.libraryIndex.trackTable[otherPlTrack.key];
      if (!otherDbFile) return false;

      var albumMatch = self.libraryIndex.getAlbumKey(dbFile) === self.libraryIndex.getAlbumKey(otherDbFile);
      if (!albumMatch) return false;

      var trackMatch = (dbFile.track == null && otherDbFile.track == null) || dbFile.track + dir === otherDbFile.track;
      if (!trackMatch) return false;

      return true;
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

function trackWithoutIndex(dbFile) {
  var out = {};
  DB_FILE_PROPS.forEach(function(propName) {
    out[propName] = dbFile[propName];
  });
  return out;
}

function serializeFileData(dbFile) {
  return JSON.stringify(trackWithoutIndex(dbFile));
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

function closeFile(file) {
  file.close(function(err) {
    if (err) {
      console.error("Error closing", file, err.stack);
    }
  });
}

function parseTrackString(trackStr) {
  if (!trackStr) return {};
  var parts = trackStr.split('/');
  if (parts.length > 1) {
    return {
      track: parseIntOrNull(parts[0]),
      trackCount: parseIntOrNull(parts[1]),
    };
  }
  return {
    track: parseIntOrNull(parts[0]),
  };
}

function parseIntOrNull(n) {
  n = parseInt(n, 10);
  if (isNaN(n)) return null;
  return n;
}

function parseFloatOrNull(n) {
  n = parseFloat(n);
  if (isNaN(n)) return null;
  return n;
}

function grooveFileToDbFile(file, filenameHint) {
  var parsedTrack = parseTrackString(file.getMetadata("track"));
  var parsedDisc = parseTrackString(file.getMetadata("disc"));
  return {
    key: uuid(),
    name: file.getMetadata("title") || trackNameFromFile(filenameHint),
    artistName: (file.getMetadata("artist") || "").trim(),
    albumArtistName: (file.getMetadata("album_artist") || "").trim(),
    albumName: (file.getMetadata("album") || "").trim(),
    compilation: !!parseInt(file.getMetadata("TCP"), 10),
    track: parsedTrack.track,
    trackCount: parsedTrack.trackCount,
    disc: parsedDisc.disc,
    discCount: parsedDisc.discCount,
    duration: file.duration(),
    year: parseInt(file.getMetadata("date") || "0", 10),
    genre: file.getMetadata("genre"),
    replayGainTrackGain: parseFloatOrNull(file.getMetadata("REPLAYGAIN_TRACK_GAIN")),
    replayGainTrackPeak: parseFloatOrNull(file.getMetadata("REPLAYGAIN_TRACK_PEAK")),
    replayGainAlbumGain: parseFloatOrNull(file.getMetadata("REPLAYGAIN_ALBUM_GAIN")),
    replayGainAlbumPeak: parseFloatOrNull(file.getMetadata("REPLAYGAIN_ALBUM_PEAK")),
  };
}

function uniqueFilename(filename) {
  // break into parts
  var dirname = path.dirname(filename);
  var basename = path.basename(filename);
  var extname = path.extname(filename);

  var withoutExt = basename.substring(0, basename.length - extname.length);

  var match = withoutExt.match(/_(\d+)$/);
  var withoutMatch;
  var number;
  if (match) {
    number = parseInt(match[1], 10);
    if (!number) number = 0;
    withoutMatch = withoutExt.substring(0, match.index);
  } else {
    number = 0;
    withoutMatch = withoutExt;
  }

  number += 1;

  // put it back together
  var newBasename = withoutMatch + "_" + number + extname;
  return path.join(dirname, newBasename);
}

function dBToFloat(dB) {
  return Math.exp(dB * DB_SCALE);
}
