var groove = require('groove');
var EventEmitter = require('events').EventEmitter;
var util = require('util');
var mkdirp = require('mkdirp');
var fs = require('fs');
var uuid = require('uuid');
var path = require('path');
var Pend = require('pend');
var DedupedQueue = require('./deduped_queue');
var findit = require('findit');
var shuffle = require('mess');
var mv = require('mv');
var zfill = require('zfill');
var MusicLibraryIndex = require('music-library-index');
var keese = require('keese');
var safePath = require('./safe_path');
var PassThrough = require('stream').PassThrough;
var url = require('url');
var superagent = require('superagent');
var ytdl = require('ytdl');

module.exports = Player;

groove.setLogging(groove.LOG_WARNING);

var cpuCount = require('os').cpus().length;


// sorted from worst to best
var YTDL_AUDIO_ENCODINGS = [
  'mp3',
  'aac',
  'wma',
  'vorbis',
  'wav',
  'flac',
];

var PLAYER_KEY_PREFIX = "Player.";
var LIBRARY_KEY_PREFIX = "Library.";
var LIBRARY_DIR_PREFIX = "LibraryDir.";
var PLAYLIST_KEY_PREFIX = "Playlist.";

// db: store in the DB
// read: send to clients
// write: accept updates from clients
var DB_PROPS = {
  key: {
    db: true,
    read: true,
    write: false,
    type: 'string',
  },
  name: {
    db: true,
    read: true,
    write: true,
    type: 'string',
  },
  artistName: {
    db: true,
    read: true,
    write: true,
    type: 'string',
  },
  albumArtistName: {
    db: true,
    read: true,
    write: true,
    type: 'string',
  },
  albumName: {
    db: true,
    read: true,
    write: true,
    type: 'string',
  },
  compilation: {
    db: true,
    read: true,
    write: true,
    type: 'boolean',
  },
  track: {
    db: true,
    read: true,
    write: true,
    type: 'integer',
  },
  trackCount: {
    db: true,
    read: true,
    write: true,
    type: 'integer',
  },
  disc: {
    db: true,
    read: true,
    write: true,
    type: 'integer',
  },
  discCount: {
    db: true,
    read: true,
    write: true,
    type: 'integer',
  },
  duration: {
    db: true,
    read: true,
    write: false,
    type: 'float',
  },
  year: {
    db: true,
    read: true,
    write: true,
    type: 'integer',
  },
  genre: {
    db: true,
    read: true,
    write: true,
    type: 'string',
  },
  file: {
    db: true,
    read: true,
    write: false,
    type: 'string',
  },
  mtime: {
    db: true,
    read: false,
    write: false,
    type: 'integer',
  },
  replayGainAlbumGain: {
    db: true,
    read: false,
    write: false,
    type: 'float',
  },
  replayGainAlbumPeak: {
    db: true,
    read: false,
    write: false,
    type: 'float',
  },
  replayGainTrackGain: {
    db: true,
    read: false,
    write: false,
    type: 'float',
  },
  replayGainTrackPeak: {
    db: true,
    read: false,
    write: false,
    type: 'float',
  },
  composerName: {
    db: true,
    read: true,
    write: true,
    type: 'string',
  },
  performerName: {
    db: true,
    read: true,
    write: true,
    type: 'string',
  },
  lastQueueDate: {
    db: true,
    read: false,
    write: false,
    type: 'date',
  },
};

var PROP_TYPE_PARSERS = {
  'string': function(value) {
    return value ? String(value) : "";
  },
  'date': function(value) {
    if (!value) return null;
    var date = new Date(value);
    if (isNaN(date.getTime())) return null;
    return date;
  },
  'integer': parseIntOrNull,
  'float': parseFloatOrNull,
  'boolean': function(value) {
    return value == null ? null : !!value;
  },
};

// how many GrooveFiles to keep open, ready to be decoded
var OPEN_FILE_COUNT = 8;
var PREV_FILE_COUNT = Math.floor(OPEN_FILE_COUNT / 2);
var NEXT_FILE_COUNT = OPEN_FILE_COUNT - PREV_FILE_COUNT;

// when a streaming client connects we send them many buffers quickly
// in order to get the stream started, then we slow down.
var instantBufferBytes = 220 * 1024;

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
  this.setMaxListeners(0);

  this.db = db;
  this.musicDirectory = musicDirectory;
  this.dbFilesByPath = {};
  this.libraryIndex = new MusicLibraryIndex();
  this.addQueue = new DedupedQueue({processOne: this.addToLibrary.bind(this)});

  this.dirs = {};
  this.dirScanQueue = new DedupedQueue({
    processOne: this.refreshFilesIndex.bind(this),
    // only 1 dir scanning can happen at a time
    // we'll pass the dir to scan as the ID so that not more than 1 of the
    // same dir can queue up
    maxAsync: 1,
  });

  this.groovePlayer = null; // initialized by initialize method
  this.groovePlaylist = null; // initialized by initialize method

  this.playlist = {};
  this.currentTrack = null;
  this.tracksInOrder = []; // another way to look at playlist
  this.grooveItems = {}; // maps groove item id to track
  this.seekRequestPos = -1; // set to >= 0 when we want to seek
  this.invalidPaths = {}; // files that could not be opened

  this.repeat = Player.REPEAT_OFF;
  this.isPlaying = false;
  this.trackStartDate = null;
  this.pausedTime = 0;
  this.dynamicModeOn = false;
  this.dynamicModeHistorySize = 10;
  this.dynamicModeFutureSize = 10;

  this.ongoingTrackScans = {};
  this.ongoingAlbumScans = {};
  this.scanQueue = new Pend();
  this.scanQueue.max = cpuCount;

  this.headerBuffers = [];
  this.recentBuffers = [];
  this.recentBuffersByteCount = 0;
  this.newHeaderBuffers = [];
  this.openStreamers = [];
  this.lastEncodeItem = null;
  this.lastEncodePos = null;
  this.expectHeaders = true;

  this.playlistItemDeleteQueue = [];
}

Player.prototype.initialize = function(cb) {
  var self = this;

  var pend = new Pend();
  pend.go(initPlayer);
  pend.go(initLibrary);
  pend.wait(function(err) {
    if (err) return cb(err);
    self.requestUpdateDb();
    playlistChanged(self);
    lazyReplayGainScanPlaylist(self);
    cacheAllOptions(cb);
  });

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
          if (bufferedSeconds > 0.5 && self.recentBuffersByteCount >= instantBufferBytes) return;
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
              self.recentBuffers.push(buf.buffer);
              self.recentBuffersByteCount += buf.buffer.length;
              while (self.recentBuffers.length > 0 &&
                  self.recentBuffersByteCount - self.recentBuffers[0].length >= instantBufferBytes)
              {
                self.recentBuffersByteCount -= self.recentBuffers.shift().length;
              }
              for (var i = 0; i < self.openStreamers.length; i += 1) {
                self.openStreamers[i].write(buf.buffer);
              }
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


        var playHeadDbKey = playHead.item && self.grooveItems[playHead.item.id].key;
        var playHeadDbFile = playHeadDbKey && self.libraryIndex.trackTable[playHeadDbKey];
        var playHeadFile = playHeadDbFile && playHeadDbFile.file;
        console.info("onNowPlaying event. playhead:", playHeadFile);

        if (playHead.item) {
          var nowMs = (new Date()).getTime();
          var posMs = playHead.pos * 1000;
          self.trackStartDate = new Date(nowMs - posMs);
          self.currentTrack = self.grooveItems[playHead.item.id];
          playlistChanged(self);
          self.emit('currentTrack');
        } else if (!decodeHead.item) {
          // both play head and decode head are null. end of playlist.
          console.log("end of playlist");
          self.currentTrack = null;
          playlistChanged(self);
          self.emit('currentTrack');
        }
      }
    }
  }

  function initLibrary(cb) {
    var pend = new Pend();
    pend.go(cacheAllDb);
    pend.go(cacheAllDirs);
    pend.go(cacheAllPlaylist);
    pend.wait(cb);
  }

  function cacheAllPlaylist(cb) {
    var stream = self.db.createReadStream({
      start: PLAYLIST_KEY_PREFIX,
    });
    stream.on('data', function(data) {
      if (data.key.indexOf(PLAYLIST_KEY_PREFIX) !== 0) {
        stream.removeAllListeners();
        stream.destroy();
        cb();
        return;
      }
      var plEntry = JSON.parse(data.value);
      self.playlist[plEntry.id] = plEntry;
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

  function cacheAllOptions(cb) {
    var options = {
      repeat: null,
      dynamicModeOn: null,
      dynamicModeHistorySize: null,
      dynamicModeFutureSize: null,
    };
    var pend = new Pend();
    for (var name in options) {
      pend.go(makeGetFn(name));
    }
    pend.wait(function(err) {
      if (err) return cb(err);
      if (options.repeat != null) {
        self.setRepeat(options.repeat);
      }
      if (options.dynamicModeOn != null) {
        self.setDynamicModeOn(options.dynamicModeOn);
      }
      if (options.dynamicModeHistorySize != null) {
        self.setDynamicModeHistorySize(options.dynamicModeHistorySize);
      }
      if (options.dynamicModeFutureSize != null) {
        self.setDynamicModeFutureSize(options.dynamicModeFutureSize);
      }
      cb();
    });

    function makeGetFn(name) {
      return function(cb) {
        self.db.get(PLAYER_KEY_PREFIX + name, function(err, value) {
          if (!err && value != null) {
            try {
              options[name] = JSON.parse(value);
            } catch (err) {
              cb(err);
              return;
            }
          }
          cb();
        });
      };
    }
  }

  function cacheAllDirs(cb) {
    var stream = self.db.createReadStream({
      start: LIBRARY_DIR_PREFIX,
    });
    stream.on('data', function(data) {
      if (data.key.indexOf(LIBRARY_DIR_PREFIX) !== 0) {
        stream.removeAllListeners();
        stream.destroy();
        cb();
        return;
      }
      var dirEntry = JSON.parse(data.value);
      self.dirs[dirEntry.dirName] = dirEntry;
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

  function cacheAllDb(cb) {
    var scrubCmds = [];
    var stream = self.db.createReadStream({
      start: LIBRARY_KEY_PREFIX,
    });
    stream.on('data', function(data) {
      if (data.key.indexOf(LIBRARY_KEY_PREFIX) !== 0) {
        stream.removeAllListeners();
        stream.destroy();
        scrubAndCb();
        return;
      }
      var dbFile = deserializeFileData(data.value);
      // scrub duplicates
      if (self.dbFilesByPath[dbFile.file]) {
        scrubCmds.push({type: 'del', key: data.key});
      } else {
        self.libraryIndex.addTrack(dbFile);
        self.dbFilesByPath[dbFile.file] = dbFile;
      }
    });
    stream.on('error', function(err) {
      stream.removeAllListeners();
      stream.destroy();
      cb(err);
    });
    stream.on('close', function() {
      scrubAndCb();
    });
    function scrubAndCb() {
      if (scrubCmds.length === 0) return cb();
      console.info("Scrubbing " + scrubCmds.length + " duplicate db entries");
      self.db.batch(scrubCmds, function(err) {
        if (err) console.error("Unable to scrub duplicate tracks from db:", err.stack);
        cb();
      });
    }
  }
};

Player.prototype.requestUpdateDb = function(dirName, forceRescan, cb) {
  var fullPath = path.resolve(this.musicDirectory, dirName || "");
  this.dirScanQueue.add(fullPath, {
    dir: fullPath,
    forceRescan: forceRescan,
  }, cb);
};

Player.prototype.refreshFilesIndex = function(args, cb) {
  var self = this;
  var dir = args.dir;
  var forceRescan = args.forceRescan;
  var dirWithSlash = ensureSep(dir);
  var walker = findit(dirWithSlash, {followSymlinks: true});
  var thisScanId = uuid();
  walker.on('directory', function(fullDirPath, stat, stop) {
    var dirName = path.relative(self.musicDirectory, fullDirPath);
    var baseName = path.basename(dirName);
    if (isFileIgnored(baseName)) return;
    var dirEntry = self.getOrCreateDir(dirName, stat);
    if (fullDirPath === dirWithSlash) return; // ignore root search path
    var parentDirName = path.dirname(dirName);
    if (parentDirName === '.') parentDirName = '';
    var parentDirEntry = self.getOrCreateDir(parentDirName);
    parentDirEntry.dirEntries[baseName] = thisScanId;
  });
  walker.on('file', function(fullPath, stat) {
    var relPath = path.relative(self.musicDirectory, fullPath);
    var dirName = path.dirname(relPath);
    if (dirName === '.') dirName = '';
    var baseName = path.basename(relPath);
    if (isFileIgnored(baseName)) return;
    var dirEntry = self.getOrCreateDir(dirName);
    dirEntry.entries[baseName] = thisScanId;
    onAddOrChange(relPath, stat);
  });
  walker.on('error', function(err) {
    console.error("library scanning error:", err.stack);
  });
  walker.on('end', function() {
    var dirName = path.relative(self.musicDirectory, dir);
    checkDirEntry(self.dirs[dirName]);
    cb();

    function checkDirEntry(dirEntry) {
      if (!dirEntry) return;
      var id;
      var baseName;
      var i;
      var deletedFiles = [];
      var deletedDirs = [];
      for (baseName in dirEntry.entries) {
        id = dirEntry.entries[baseName];
        if (id !== thisScanId) deletedFiles.push(baseName);
      }
      for (i = 0; i < deletedFiles.length; i += 1) {
        baseName = deletedFiles[i];
        delete dirEntry.entries[baseName];
        onFileMissing(dirEntry, baseName);
      }

      for (baseName in dirEntry.dirEntries) {
        id = dirEntry.dirEntries[baseName];
        var childEntry = self.dirs[path.join(dirEntry.dirName, baseName)];
        checkDirEntry(childEntry);
        if (id !== thisScanId) deletedDirs.push(baseName);
      }
      for (i = 0; i < deletedDirs.length; i += 1) {
        baseName = deletedDirs[i];
        delete dirEntry.dirEntries[baseName];
        onDirMissing(dirEntry, baseName);
      }

      self.persistDirEntry(dirEntry);
    }

  });

  function onDirMissing(parentDirEntry, baseName) {
    var dirName = path.join(parentDirEntry.dirName, baseName);
    console.log("directory deleted:", dirName);
    var dirEntry = self.dirs[dirName];
    var watcher = dirEntry.watcher;
    if (watcher) watcher.close();
    delete self.dirs[dirName];
    delete parentDirEntry.dirEntries[baseName];
  }

  function onFileMissing(parentDirEntry, baseName) {
    var relPath = path.join(parentDirEntry.dirName, baseName);
    console.log("file deleted:", relPath);
    delete parentDirEntry.entries[baseName];
    var dbFile = self.dbFilesByPath[relPath];
    if (dbFile) self.delDbEntry(dbFile);
  }

  function onAddOrChange(relPath, stat) {
    // check the mtime against the mtime of the same file in the db
    var dbFile = self.dbFilesByPath[relPath];
    var fileMtime = stat.mtime.getTime();

    if (dbFile && !forceRescan) {
      var dbMtime = dbFile.mtime;

      if (dbMtime >= fileMtime) {
        // the info we have in our db for this file is fresh
        return;
      }
    }
    self.addQueue.add(relPath, {
      relPath: relPath,
      mtime: fileMtime,
    });
  }
};

Player.prototype.watchDirEntry = function(dirEntry) {
  var self = this;
  var changeTriggered = null;
  var fullDirPath = path.join(self.musicDirectory, dirEntry.dirName);
  var watcher;
  try {
    watcher = fs.watch(fullDirPath, onChange);
    watcher.on('error', onWatchError);
  } catch (err) {
    console.error("Unable to fs.watch:", err.stack);
    watcher = null;
  }
  dirEntry.watcher = watcher;

  function onChange(eventName) {
    if (changeTriggered) clearTimeout(changeTriggered);
    changeTriggered = setTimeout(function() {
      changeTriggered = null;
      console.log("dir updated:", dirEntry.dirName);
      self.dirScanQueue.add(fullDirPath, { dir: fullDirPath });
    }, 100);
  }

  function onWatchError(err) {
    console.error("watch error:", err.stack);
  }
};

Player.prototype.getOrCreateDir = function (dirName, stat) {
  var dirEntry = this.dirs[dirName];

  if (!dirEntry) {
    dirEntry = this.dirs[dirName] = {
      dirName: dirName,
      entries: {},
      dirEntries: {},
      watcher: null, // will be set just below
      mtime: stat && stat.mtime,
    };
  } else if (stat && dirEntry.mtime !== stat.mtime) {
    dirEntry.mtime = stat.mtime;
  }
  if (!dirEntry.watcher) this.watchDirEntry(dirEntry);
  return dirEntry;
};


Player.prototype.getCurPos = function() {
  return this.isPlaying ?
      ((new Date() - this.trackStartDate) / 1000.0) : this.pausedTime;
};

Player.prototype.secondsIntoFuture = function(groovePlaylistItem, pos) {
  if (!groovePlaylistItem || !pos) {
    return 0;
  }

  var item = this.grooveItems[groovePlaylistItem.id];

  if (item === this.currentTrack) {
    return pos - this.getCurPos();
  } else {
    return pos;
  }
};

Player.prototype.streamMiddleware = function(req, resp, next) {
  var self = this;
  if (req.path !== '/stream.mp3') return next();

  resp.setHeader('Content-Type', 'audio/mpeg');
  resp.setHeader('Cache-Control', 'no-cache, no-store, must-revalidate');
  resp.setHeader('Pragma', 'no-cache');
  resp.setHeader('Expires', '0');
  resp.statusCode = 200;

  var count = 0;
  self.headerBuffers.forEach(function(headerBuffer) {
    count += headerBuffer.length;
    resp.write(headerBuffer);
  });
  self.recentBuffers.forEach(function(recentBuffer) {
    resp.write(recentBuffer);
  });
  console.log("sent", count, "bytes of headers and", self.recentBuffersByteCount,
      "bytes of unthrottled data");
  self.openStreamers.push(resp);
  req.on('abort', function() {
    for (var i = 0; i < self.openStreamers.length; i += 1) {
      if (self.openStreamers[i] === resp) {
        self.openStreamers.splice(i, 1);
        break;
      }
    }
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
  // delete items from the queue that are being deleted from the library
  var deleteQueueItems = [];
  for (var queueId in this.playlist) {
    var queueItem = this.playlist[queueId];
    if (queueItem.key === dbFile.key) {
      deleteQueueItems.push(queueId);
    }
  }
  this.removePlaylistItems(deleteQueueItems);

  this.libraryIndex.removeTrack(dbFile.key);
  delete this.dbFilesByPath[dbFile.file];
  var baseName = path.basename(dbFile.file);
  var parentDirName = path.dirname(dbFile.file);
  if (parentDirName === '.') parentDirName = '';
  var parentDirEntry = this.dirs[parentDirName];
  if (parentDirEntry) delete parentDirEntry[baseName];
  this.emit('deleteDbTrack', dbFile);
  this.db.del(LIBRARY_KEY_PREFIX + dbFile.key, function(err) {
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
  this.emit("volumeUpdate");
};

Player.prototype.importUrl = function(urlString, cb) {
  var self = this;
  cb = cb || logIfError;

  var tmpDir = path.join(self.musicDirectory, '.tmp');

  mkdirp(tmpDir, function(err) {
    if (err) return cb(err);

    var parsedUrl = url.parse(urlString);

    // detect youtube downloads
    if ((parsedUrl.hostname === 'youtube.com' || parsedUrl.hostname === 'www.youtube.com') &&
      parsedUrl.pathname === '/watch')
    {
      var bestFormat = null;
      ytdl.getInfo(urlString, gotYouTubeInfo);
    } else {
      var remoteFilename = path.basename(parsedUrl.pathname);
      var decodedFilename;
      try {
        decodedFilename = decodeURI(remoteFilename);
      } catch (err) {
        decodedFilename = remoteFilename;
      }
      var req = superagent.get(urlString);
      handleDownload(req, decodedFilename);
    }

    function gotYouTubeInfo(err, info) {
      if (err) return cb(err);
      for (var i = 0; i < info.formats.length; i += 1) {
        var format = info.formats[i];
        if (bestFormat == null || format.audioBitrate > bestFormat.audioBitrate ||
           (format.audioBitrate === bestFormat.audioBitrate &&
            YTDL_AUDIO_ENCODINGS.indexOf(format.audioEncoding) >
            YTDL_AUDIO_ENCODINGS.indexOf(bestFormat.audioEncoding)))
        {
          bestFormat = format;
        }
      }
      if (YTDL_AUDIO_ENCODINGS.indexOf(bestFormat.audioEncoding) === -1) {
        console.warn("YouTube Import: unrecognized audio format:", bestFormat.audioEncoding);
      }
      var req = ytdl(urlString, {filter: filter});
      handleDownload(req, info.title + '.' + bestFormat.container);

      function filter(format) {
        return format.audioBitrate === bestFormat.audioBitrate &&
          format.audioEncoding === bestFormat.audioEncoding;
      }
    }

    function handleDownload(req, remoteFilename) {
      var ext = path.extname(remoteFilename);
      var destPath = path.join(tmpDir, uuid() + ext);
      var ws = fs.createWriteStream(destPath);

      var calledCallback = false;
      req.pipe(ws);
      ws.on('close', function(){
        if (calledCallback) return;
        self.importFile(ws.path, remoteFilename, function(err, dbFile) {
          if (err) {
            cleanAndCb(err);
          } else {
            calledCallback = true;
            cb(null, dbFile);
          }
        });
      });
      ws.on('error', cleanAndCb);
      req.on('error', cleanAndCb);

      function cleanAndCb(err) {
        fs.unlink(destPath, function(err) {
          if (err) {
            console.warn("Unable to clean up temp file:", err.stack);
          }
        });
        if (calledCallback) return;
        calledCallback = true;
        cb(err);
      }
    }
  });

  function logIfError(err) {
    if (err) {
      console.error("Unable to import by URL.", err.stack, "URL:", urlString);
    }
  }
};

// moves the file at srcFullPath to the music library
Player.prototype.importFile = function(srcFullPath, filenameHint, cb) {
  var self = this;
  cb = cb || logIfError;

  groove.open(srcFullPath, function(err, file) {
    if (err) return cb(err);
    var newDbFile = grooveFileToDbFile(file, filenameHint);
    var suggestedPath = self.getSuggestedPath(newDbFile, filenameHint);
    var pend = new Pend();
    pend.go(function(cb) {
      file.close(cb);
    });
    pend.go(function(cb) {
      tryMv(suggestedPath, cb);
    });
    pend.wait(function(err) {
      if (err) return cb(err);
      cb(null, newDbFile);
    });

    function tryMv(destRelPath, cb) {
      var destFullPath = path.join(self.musicDirectory, destRelPath);
      mv(srcFullPath, destFullPath, {mkdirp: true, clobber: false}, function(err) {
        if (err) {
          if (err.code === 'EEXIST') {
            tryMv(uniqueFilename(destRelPath), cb);
          } else {
            cb(err);
          }
          return;
        }
        // in case it doesn't get picked up by a watcher
        self.requestUpdateDb(path.dirname(destRelPath), false, function(err) {
          if (err) return cb(err);
          self.addQueue.waitForId(destRelPath, function(err) {
            if (err) return cb(err);
            newDbFile = self.dbFilesByPath[destRelPath];
            cb();
          });
        });
      });
    }
  });

  function logIfError(err) {
    if (err) {
      console.error("unable to import file:", err.stack);
    }
  }
};

Player.prototype.persistDirEntry = function(dirEntry, cb) {
  cb = cb || logIfError;
  this.db.put(LIBRARY_DIR_PREFIX + dirEntry.dirName, serializeDirEntry(dirEntry), cb);

  function logIfError(err) {
    if (err) {
      console.error("unable to persist db entry:", dirEntry, err.stack);
    }
  }
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

Player.prototype.persistPlaylistItem = function(item, cb) {
  this.db.put(PLAYLIST_KEY_PREFIX + item.id, serializePlaylistItem(item), cb || logIfError);

  function logIfError(err) {
    if (err) {
      console.error("unable to persist playlist item:", item, err.stack);
    }
  }
};

Player.prototype.persistOption = function(name, value, cb) {
  this.db.put(PLAYER_KEY_PREFIX + name, JSON.stringify(value), cb || logIfError);
  function logIfError(err) {
    if (err) {
      console.error("unable to persist player option:", err.stack);
    }
  }
};

Player.prototype.addToLibrary = function(args, cb) {
  var self = this;
  var relPath = args.relPath;
  var mtime = args.mtime;
  var fullPath = path.join(self.musicDirectory, relPath);
  groove.open(fullPath, function(err, file) {
    if (err) {
      self.invalidPaths[relPath] = err.message;
      cb();
      return;
    }
    var dbFile = self.dbFilesByPath[relPath];
    var eventType = dbFile ? 'updateDbTrack' : 'addDbTrack';
    var newDbFile = grooveFileToDbFile(file, relPath, dbFile);
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
    self.emit(eventType, newDbFile);
    pend.wait(cb);
  });
};

Player.prototype.updateTags = function(obj) {
  for (var key in obj) {
    var track = this.libraryIndex.trackTable[key];
    if (!track) continue;
    var props = obj[key];
    if (!props || typeof props !== 'object') continue;
    for (var propName in DB_PROPS) {
      var prop = DB_PROPS[propName];
      if (! prop.write) continue;
      if (! (propName in props)) continue;
      var parser = PROP_TYPE_PARSERS[prop.type];
      track[propName] = parser(props[propName]);
    }
    this.persist(track);
    this.emit('updateDbTrack', track);
  }
};

Player.prototype.insertTracks = function(index, keys, tagAsRandom) {
  if (keys.length === 0) return;
  if (index < 0) index = 0;
  if (index > this.tracksInOrder.length) index = this.tracksInOrder.length;

  var trackBeforeIndex = this.tracksInOrder[index - 1];
  var trackAtIndex = this.tracksInOrder[index];

  var prevSortKey = trackBeforeIndex ? trackBeforeIndex.sortKey : null;
  var nextSortKey = trackAtIndex ? trackAtIndex.sortKey : null;

  var items = {};
  var ids = [];
  keys.forEach(function(key) {
    var id = uuid();
    var thisSortKey = keese(prevSortKey, nextSortKey);
    prevSortKey = thisSortKey;
    items[id] = {
      key: key,
      sortKey: thisSortKey,
    };
    ids.push(id);
  });
  this.addItems(items, tagAsRandom);
  return ids;
};

Player.prototype.appendTracks = function(keys, tagAsRandom) {
  return this.insertTracks(this.tracksInOrder.length, keys, tagAsRandom);
};

// items looks like {id: {key, sortKey}}
Player.prototype.addItems = function(items, tagAsRandom) {
  var self = this;
  tagAsRandom = !!tagAsRandom;
  for (var id in items) {
    var item = items[id];
    var dbFile = self.libraryIndex.trackTable[item.key];
    if (!dbFile) continue;
    dbFile.lastQueueDate = new Date();
    self.persist(dbFile);
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
    self.persistPlaylistItem(playlistItem);
  }
  playlistChanged(self);
  lazyReplayGainScanPlaylist(self);
};

Player.prototype.clearPlaylist = function() {
  this.removePlaylistItems(Object.keys(this.playlist));
};

Player.prototype.shufflePlaylist = function() {
  shuffle(this.tracksInOrder);
  // fix sortKey and index properties
  var nextSortKey = keese(null, null);
  for (var i = 0; i < this.tracksInOrder.length; i += 1) {
    var track = this.tracksInOrder[i];
    track.index = i;
    track.sortKey = nextSortKey;
    this.persistPlaylistItem(track);
    nextSortKey = keese(nextSortKey, null);
  }
  playlistChanged(this);
};

Player.prototype.removePlaylistItems = function(ids) {
  if (ids.length === 0) return;
  var delCmds = [];
  var currentTrackChanged = false;
  for (var i = 0; i < ids.length; i += 1) {
    var id = ids[i];
    var item = this.playlist[id];
    if (!item) continue;

    delCmds.push({type: 'del', key: PLAYLIST_KEY_PREFIX + id});

    if (item.grooveFile) this.playlistItemDeleteQueue.push(item);
    if (item === this.currentTrack) {
      this.currentTrack = null;
      currentTrackChanged = true;
    }

    delete this.playlist[id];
  }
  if (delCmds.length > 0) this.db.batch(delCmds, logIfError);

  playlistChanged(this);
  if (currentTrackChanged) this.emit('currentTrack');

  function logIfError(err) {
    if (err) {
      console.error("Error deleting playlist entries from db:", err.stack);
    }
  }
};

// items looks like {id: {sortKey}}
Player.prototype.movePlaylistItems = function(items) {
  for (var id in items) {
    var track = this.playlist[id];
    if (!track) continue; // race conditions, etc.
    track.sortKey = items[id].sortKey;
    this.persistPlaylistItem(track);
  }
  playlistChanged(this);
};

Player.prototype.moveRangeToPos = function(startPos, endPos, toPos) {
  var ids = [];
  for (var i = startPos; i < endPos; i += 1) {
    var track = this.tracksInOrder[i];
    if (!track) continue;

    ids.push(track.id);
  }
  this.moveIdsToPos(ids, toPos);
};

Player.prototype.moveIdsToPos = function(ids, toPos) {
  var trackBeforeIndex = this.tracksInOrder[toPos - 1];
  var trackAtIndex = this.tracksInOrder[toPos];

  var prevSortKey = trackBeforeIndex ? trackBeforeIndex.sortKey : null;
  var nextSortKey = trackAtIndex ? trackAtIndex.sortKey : null;

  for (var i = 0; i < ids.length; i += 1) {
    var id = ids[i];
    var track = this.playlist[id];
    if (!track) continue;

    var thisSortKey = keese(prevSortKey, nextSortKey);
    prevSortKey = thisSortKey;
    track.sortKey = thisSortKey;
    this.persistPlaylistItem(track);
  }
  playlistChanged(this);
};

Player.prototype.pause = function() {
  if (!this.isPlaying) return;
  this.isPlaying = false;
  this.pausedTime = (new Date() - this.trackStartDate) / 1000;
  this.groovePlaylist.pause();
  playlistChanged(this);
  this.emit('currentTrack');
};

Player.prototype.play = function() {
  if (!this.currentTrack) {
    this.currentTrack = this.tracksInOrder[0];
  } else if (!this.isPlaying) {
    this.trackStartDate = new Date(new Date() - this.pausedTime * 1000);
  }
  this.groovePlaylist.play();
  this.isPlaying = true;
  playlistChanged(this);
  this.emit('currentTrack');
};

// This function should be avoided in favor of seek. Note that it is called by
// some MPD protocol commands, because the MPD protocol is stupid.
Player.prototype.seekToIndex = function(index, pos) {
  this.currentTrack = this.tracksInOrder[index];
  this.isPlaying = true;
  this.groovePlaylist.play();
  this.seekRequestPos = pos;
  playlistChanged(this);
  this.emit('currentTrack');
};

Player.prototype.seek = function(id, pos) {
  this.currentTrack = this.playlist[id];
  this.isPlaying = true;
  this.groovePlaylist.play();
  this.seekRequestPos = pos;
  playlistChanged(this);
  this.emit('currentTrack');
};

Player.prototype.next = function() {
  this.skipBy(1);
};

Player.prototype.prev = function() {
  this.skipBy(-1);
};

Player.prototype.skipBy = function(amt) {
  var defaultIndex = amt > 0 ? -1 : this.tracksInOrder.length;
  var currentIndex = this.currentTrack ? this.currentTrack.index : defaultIndex;
  var newIndex = currentIndex + amt;
  this.seekToIndex(newIndex, 0);
};

Player.prototype.setRepeat = function(value) {
  value = Math.floor(value);
  if (value !== Player.REPEAT_ONE &&
      value !== Player.REPEAT_ALL &&
      value !== Player.REPEAT_OFF)
  {
    return;
  }
  if (value === this.repeat) return;
  this.repeat = value;
  this.persistOption('repeat', this.repeat);
  playlistChanged(this);
  this.emit('repeatUpdate');
};

Player.prototype.setDynamicModeOn = function(value) {
  value = !!value;
  if (value === this.dynamicModeOn) return;
  this.dynamicModeOn = value;
  this.persistOption('dynamicModeOn', this.dynamicModeOn);
  this.emit('dynamicModeOn');
  this.checkDynamicMode();
};

Player.prototype.setDynamicModeHistorySize = function(value) {
  value = Math.floor(value);
  if (value === this.dynamicModeHistorySize) return;
  this.dynamicModeHistorySize = value;
  this.persistOption('dynamicModeHistorySize', this.dynamicModeHistorySize);
  this.emit('dynamicModeHistorySize');
  this.checkDynamicMode();
};

Player.prototype.setDynamicModeFutureSize = function(value) {
  value = Math.floor(value);
  if (value === this.dynamicModeFutureSize) return;
  this.dynamicModeFutureSize = value;
  this.persistOption('dynamicModeFutureSize', this.dynamicModeFutureSize);
  this.emit('dynamicModeFutureSize');
  this.checkDynamicMode();
};

Player.prototype.stop = function() {
  this.isPlaying = false;
  this.groovePlaylist.pause();
  this.seekRequestPos = 0;
  this.pausedTime = 0;
  playlistChanged(this);
};

Player.prototype.clearEncodedBuffer = function() {
  while (this.recentBuffers.length > 0) {
    this.recentBuffers.shift();
  }
  this.recentBuffersByteCount = 0;
};

Player.prototype.getSuggestedPath = function(track, filenameHint) {
  var p = "";
  if (track.albumArtistName) {
    p = path.join(p, safePath(track.albumArtistName));
  } else if (track.compilation) {
    p = path.join(p, safePath(this.libraryIndex.variousArtistsName));
  } else if (track.artistName) {
    p = path.join(p, safePath(track.artistName));
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
    playlist: null,
    detector: null,
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
      self.libraryIndex.rebuild();
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

      var scanPlaylist = groove.createPlaylist();
      var scanDetector = groove.createLoudnessDetector();

      scanDetector.on('info', function() {
        var info;
        while (info = scanDetector.getInfo()) {
          console.log("loudness", info.loudness);
          var gain = groove.loudnessToReplayGain(info.loudness);
          if (info.item) {
            var fileInfo = scanContext.files[info.item.file.id];
            console.info("replaygain scan file complete:", fileInfo.track.name, "gain", gain, "duration", info.duration);
            fileInfo.progress = 1.0;
            fileInfo.gain = gain;
            fileInfo.peak = info.peak;
            fileInfo.track.replayGainTrackGain = gain;
            fileInfo.track.replayGainTrackPeak = info.peak;
            fileInfo.track.duration = info.duration;
            checkUpdateGroovePlaylist(self);
          } else {
            if (scanContext.aborted) return cleanupAndCb();

            console.info("Replaygain scan complete:", JSON.stringify(scanKey), "gain", gain);
            delete scanTable[scanKey];
            for (var fileId in scanContext.files) {
              var scanFileContext = scanContext.files[fileId];
              var dbFile = scanFileContext.track;
              dbFile.replayGainAlbumGain = gain;
              dbFile.replayGainAlbumPeak = info.peak;
              self.persist(dbFile);
              self.emit('scanComplete', dbFile);
            }
            checkUpdateGroovePlaylist(self);
            cleanupAndCb();
            return;
          }
        }
      });

      scanDetector.attach(scanPlaylist, function(err) {
        if (err) {
          console.error("Error attaching loudness detector:", err.stack);
          return cleanupAndCb();
        }

        scanContext.playlist = scanPlaylist;
        scanContext.detector = scanDetector;


        fileList.forEach(function(file) {
          scanPlaylist.insert(file);
        });
      });
    });
    function cleanupAndCb() {
      fileList.forEach(function(file) {
        pend.go(function(cb) {
          file.close(cb);
        });
      });
      if (scanContext.detector) {
        pend.go(function(cb) {
          scanContext.detector.detach(cb);
        });
      }
      pend.wait(cb);
    }
  }
};

Player.prototype.checkDynamicMode = function() {
  var self = this;
  if (!self.dynamicModeOn) return;

  // if no track is playing, assume the first track is about to be
  var currentIndex = self.currentTrack ? self.currentTrack.index : 0;

  var deleteCount = Math.max(currentIndex - self.dynamicModeHistorySize, 0);
  if (self.dynamicModeHistorySize < 0) deleteCount = 0;
  var addCount = Math.max(self.dynamicModeFutureSize + 1 - (self.tracksInOrder.length - currentIndex), 0);

  var idsToDelete = [];
  for (var i = 0; i < deleteCount; i += 1) {
    idsToDelete.push(self.tracksInOrder[i].id);
  }
  var keys = getRandomSongKeys(addCount);
  self.removePlaylistItems(idsToDelete);
  self.appendTracks(keys, true);

  function getRandomSongKeys(count) {
    if (count === 0) return [];
    var neverQueued = [];
    var sometimesQueued = [];
    for (var key in self.libraryIndex.trackTable) {
      var dbFile = self.libraryIndex.trackTable[key];
      if (dbFile.lastQueueDate == null) {
        neverQueued.push(dbFile);
      } else {
        sometimesQueued.push(dbFile);
      }
    }
    // backwards by time
    sometimesQueued.sort(function(a, b) {
      return b.lastQueueDate - a.lastQueueDate;
    });
    // distribution is a triangle for ever queued, and a rectangle for never queued
    //    ___
    //   /| |
    //  / | |
    // /__|_|
    var maxWeight = sometimesQueued.length;
    var triangleArea = Math.floor(maxWeight * maxWeight / 2);
    if (maxWeight === 0) maxWeight = 1;
    var rectangleArea = maxWeight * neverQueued.length;
    var totalSize = triangleArea + rectangleArea;
    if (totalSize === 0) return [];
    // decode indexes through the distribution shape
    var keys = [];
    for (var i = 0; i < count; i += 1) {
      var index = Math.random() * totalSize;
      if (index < triangleArea) {
        // triangle
        keys.push(sometimesQueued[Math.floor(Math.sqrt(index))].key);
      } else {
        keys.push(neverQueued[Math.floor((index - triangleArea) / maxWeight)].key);
      }
    }
    return keys;
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
    if (!dbFile) return;
    var albumKey = self.libraryIndex.getAlbumKey(dbFile);
    var needScan = dbFile.replayGainAlbumGain == null ||
        dbFile.replayGainTrackGain == null ||
        (dbFile.albumName && albumGain[albumKey] && albumGain[albumKey] !== dbFile.replayGainAlbumGain);
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
  performGrooveFileDeletes(self);

  self.checkDynamicMode();

  self.emit('playlistUpdate');
}

function performGrooveFileDeletes(self) {
  while (self.playlistItemDeleteQueue.length) {
    var item = self.playlistItemDeleteQueue.shift();
    // we set this so that any callbacks that return which were trying to
    // set the grooveItem can check if the item got deleted
    item.deleted = true;
    closeFile(item.grooveFile);
  }
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
      self.clearEncodedBuffer();
      self.groovePlaylist.seek(currentGrooveItem, seekPos);
      self.seekRequestPos = -1;
      var nowMs = (new Date()).getTime();
      var posMs = seekPos * 1000;
      self.trackStartDate = new Date(nowMs - posMs);
      self.emit('seek');
      self.emit('currentTrack');
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

function isFileIgnored(basename) {
  return (/^\./).test(basename) || (/~$/).test(basename);
}

function deserializeFileData(dataStr) {
  var dbFile = JSON.parse(dataStr);
  for (var propName in DB_PROPS) {
    var propInfo = DB_PROPS[propName];
    if (!propInfo) continue;
    var parser = PROP_TYPE_PARSERS[propInfo.type];
    dbFile[propName] = parser(dbFile[propName]);
  }
  return dbFile;
}

function serializePlaylistItem(item) {
  return JSON.stringify({
    id: item.id,
    key: item.key,
    sortKey: item.sortKey,
    isRandom: item.isRandom,
  });
}

function trackWithoutIndex(category, dbFile) {
  var out = {};
  for (var propName in DB_PROPS) {
    var prop = DB_PROPS[propName];
    if (!prop[category]) continue;
    // save space by leaving out null and undefined values
    var value = dbFile[propName];
    if (value == null) continue;
    out[propName] = value;
  }
  return out;
}

function serializeFileData(dbFile) {
  return JSON.stringify(trackWithoutIndex('db', dbFile));
}

function serializeDirEntry(dirEntry) {
  return JSON.stringify({
    dirName: dirEntry.dirName,
    entries: dirEntry.entries,
    dirEntries: dirEntry.dirEntries,
    mtime: dirEntry.mtime,
  });
}

function trackNameFromFile(filename) {
  var basename = path.basename(filename);
  var ext = path.extname(basename);
  return basename.substring(0, basename.length - ext.length);
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
      value: parseIntOrNull(parts[0]),
      total: parseIntOrNull(parts[1]),
    };
  }
  return {
    value: parseIntOrNull(parts[0]),
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

function grooveFileToDbFile(file, filenameHint, object) {
  object = object || {key: uuid()};
  var parsedTrack = parseTrackString(file.getMetadata("track"));
  var parsedDisc = parseTrackString(file.getMetadata("disc") || file.getMetadata("TPA"));
  object.name = (file.getMetadata("title") || trackNameFromFile(filenameHint) || "").trim();
  object.artistName = (file.getMetadata("artist") || "").trim();
  object.composerName = (file.getMetadata("composer") ||
                         file.getMetadata("TCM") || "").trim();
  object.performerName = (file.getMetadata("performer") || "").trim();
  object.albumArtistName = (file.getMetadata("album_artist") || "").trim();
  object.albumName = (file.getMetadata("album") || "").trim();
  object.compilation = !!(parseInt(file.getMetadata("TCP"),  10) ||
                          parseInt(file.getMetadata("TCMP"), 10));
  object.track = parsedTrack.value;
  object.trackCount = parsedTrack.total;
  object.disc = parsedDisc.value;
  object.discCount = parsedDisc.total;
  object.duration = file.duration();
  object.year = parseIntOrNull(file.getMetadata("date"));
  object.genre = file.getMetadata("genre");
  object.replayGainTrackGain = parseFloatOrNull(file.getMetadata("REPLAYGAIN_TRACK_GAIN"));
  object.replayGainTrackPeak = parseFloatOrNull(file.getMetadata("REPLAYGAIN_TRACK_PEAK"));
  object.replayGainAlbumGain = parseFloatOrNull(file.getMetadata("REPLAYGAIN_ALBUM_GAIN"));
  object.replayGainAlbumPeak = parseFloatOrNull(file.getMetadata("REPLAYGAIN_ALBUM_PEAK"));
  return object;
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

function ensureSep(dir) {
  return (dir[dir.length - 1] === path.sep) ? dir : (dir + path.sep);
}
