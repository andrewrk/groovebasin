var groove = require('groove');
var semver = require('semver');
var EventEmitter = require('events').EventEmitter;
var util = require('util');
var mkdirp = require('mkdirp');
var fs = require('fs');
var uuid = require('./uuid');
var path = require('path');
var Pend = require('pend');
var DedupedQueue = require('./deduped_queue');
var findit = require('findit2');
var shuffle = require('mess');
var mv = require('mv');
var MusicLibraryIndex = require('music-library-index');
var keese = require('keese');
var safePath = require('./safe_path');
var PassThrough = require('stream').PassThrough;
var url = require('url');
var dbIterate = require('./db_iterate');
var log = require('./log');
var importUrlFilters = require('./import_url_filters');
var youtubeSearch = require('./youtube_search');
var yauzl = require('yauzl');

var importFileFilters = [
  {
    name: 'zip',
    fn: importFileAsZip,
  },
  {
    name: 'song',
    fn: importFileAsSong,
  },
];

module.exports = Player;

ensureGrooveVersionIsOk();

var cpuCount = require('os').cpus().length;

var PLAYER_KEY_PREFIX = "Player.";
var LIBRARY_KEY_PREFIX = "Library.";
var LIBRARY_DIR_PREFIX = "LibraryDir.";
var QUEUE_KEY_PREFIX = "Playlist.";
var PLAYLIST_KEY_PREFIX = "StoredPlaylist.";
var LABEL_KEY_PREFIX = "Label.";
var PLAYLIST_META_KEY_PREFIX = "StoredPlaylistMeta.";

// db: store in the DB
var DB_PROPS = {
  key: {
    db: true,
    clientVisible: true,
    clientCanModify: false,
    type: 'string',
  },
  name: {
    db: true,
    clientVisible: true,
    clientCanModify: true,
    type: 'string',
  },
  artistName: {
    db: true,
    clientVisible: true,
    clientCanModify: true,
    type: 'string',
  },
  albumArtistName: {
    db: true,
    clientVisible: true,
    clientCanModify: true,
    type: 'string',
  },
  albumName: {
    db: true,
    clientVisible: true,
    clientCanModify: true,
    type: 'string',
  },
  compilation: {
    db: true,
    clientVisible: true,
    clientCanModify: true,
    type: 'boolean',
  },
  track: {
    db: true,
    clientVisible: true,
    clientCanModify: true,
    type: 'integer',
  },
  trackCount: {
    db: true,
    clientVisible: true,
    clientCanModify: true,
    type: 'integer',
  },
  disc: {
    db: true,
    clientVisible: true,
    clientCanModify: true,
    type: 'integer',
  },
  discCount: {
    db: true,
    clientVisible: true,
    clientCanModify: true,
    type: 'integer',
  },
  duration: {
    db: true,
    clientVisible: true,
    clientCanModify: false,
    type: 'float',
  },
  year: {
    db: true,
    clientVisible: true,
    clientCanModify: true,
    type: 'integer',
  },
  genre: {
    db: true,
    clientVisible: true,
    clientCanModify: true,
    type: 'string',
  },
  file: {
    db: true,
    clientVisible: true,
    clientCanModify: false,
    type: 'string',
  },
  mtime: {
    db: true,
    clientVisible: false,
    clientCanModify: false,
    type: 'integer',
  },
  replayGainAlbumGain: {
    db: true,
    clientVisible: false,
    clientCanModify: false,
    type: 'float',
  },
  replayGainAlbumPeak: {
    db: true,
    clientVisible: false,
    clientCanModify: false,
    type: 'float',
  },
  replayGainTrackGain: {
    db: true,
    clientVisible: false,
    clientCanModify: false,
    type: 'float',
  },
  replayGainTrackPeak: {
    db: true,
    clientVisible: false,
    clientCanModify: false,
    type: 'float',
  },
  composerName: {
    db: true,
    clientVisible: true,
    clientCanModify: true,
    type: 'string',
  },
  performerName: {
    db: true,
    clientVisible: true,
    clientCanModify: true,
    type: 'string',
  },
  lastQueueDate: {
    db: true,
    clientVisible: false,
    clientCanModify: false,
    type: 'date',
  },
  fingerprint: {
    db: true,
    clientVisible: false,
    clientCanModify: false,
    type: 'array_of_integer',
  },
  playCount: {
    db: true,
    clientVisible: false,
    clientCanModify: false,
    type: 'integer',
  },
  labels: {
    db: true,
    clientVisible: true,
    clientCanModify: false,
    type: 'set',
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
  'array_of_integer': function(value) {
    if (!Array.isArray(value)) return null;
    value = value.map(parseIntOrNull);
    for (var i = 0; i < value.length; i++) {
      if (value[i] == null) return null;
    }
    return value;
  },
  'set': function(value) {
    var result = {};
    for (var key in value) {
      result[key] = 1;
    }
    return result;
  },
};

var labelColors = [
  "#e11d21", "#eb6420", "#fbca04", "#009800",
  "#006b75", "#207de5", "#0052cc", "#5319e7",
  "#f7c6c7", "#fad8c7", "#fef2c0", "#bfe5bf",
  "#bfdadc", "#bfd4f2", "#c7def8", "#d4c5f9",
];

// how many GrooveFiles to keep open, ready to be decoded
var OPEN_FILE_COUNT = 8;
var PREV_FILE_COUNT = Math.floor(OPEN_FILE_COUNT / 2);
var NEXT_FILE_COUNT = OPEN_FILE_COUNT - PREV_FILE_COUNT;

var DB_SCALE = Math.log(10.0) * 0.05;
var REPLAYGAIN_PREAMP = 0.75;
var REPLAYGAIN_DEFAULT = 0.25;

Player.REPEAT_OFF = 0;
Player.REPEAT_ALL = 1;
Player.REPEAT_ONE = 2;

Player.trackWithoutIndex = trackWithoutIndex;
Player.setGrooveLoggingLevel = setGrooveLoggingLevel;

util.inherits(Player, EventEmitter);
function Player(db, config) {
  EventEmitter.call(this);
  this.setMaxListeners(0);

  this.db = db;
  this.musicDirectory = config.musicDirectory;
  this.dbFilesByPath = {};
  this.dbFilesByLabel = {};
  this.libraryIndex = new MusicLibraryIndex({
    searchFields: MusicLibraryIndex.defaultSearchFields.concat('file'),
  });
  this.addQueue = new DedupedQueue({
    processOne: this.addToLibrary.bind(this),
    // limit to 1 async operation because we're blocking on the hard drive,
    // it's faster to read one file at a time.
    maxAsync: 1,
  });

  this.dirs = {};
  this.dirScanQueue = new DedupedQueue({
    processOne: this.refreshFilesIndex.bind(this),
    // only 1 dir scanning can happen at a time
    // we'll pass the dir to scan as the ID so that not more than 1 of the
    // same dir can queue up
    maxAsync: 1,
  });
  this.dirScanQueue.on('error', function(err) {
    log.error("library scanning error:", err.stack);
  });
  this.disableFsRefCount = 0;

  this.playlist = {};
  this.playlists = {};
  this.currentTrack = null;
  this.tracksInOrder = []; // another way to look at playlist
  this.grooveItems = {}; // maps groove item id to track
  this.seekRequestPos = -1; // set to >= 0 when we want to seek
  this.invalidPaths = {}; // files that could not be opened
  this.playlistItemDeleteQueue = [];
  this.dontBelieveTheEndOfPlaylistSentinelItsATrap = false;
  this.queueClearEncodedBuffers = false;

  this.repeat = Player.REPEAT_OFF;
  this.desiredPlayerHardwareState = null; // true: normal hardware playback. false: dummy
  this.pendingPlayerAttachDetach = null;
  this.isPlaying = false;
  this.trackStartDate = null;
  this.pausedTime = 0;
  this.autoDjOn = false;
  this.autoDjHistorySize = 10;
  this.autoDjFutureSize = 10;

  this.ongoingScans = {};
  this.scanQueue = new DedupedQueue({
    processOne: this.performScan.bind(this),
    maxAsync: cpuCount,
  });

  this.headerBuffers = [];
  this.recentBuffers = [];
  this.newHeaderBuffers = [];
  this.openStreamers = [];
  this.expectHeaders = true;
  // when a streaming client connects we send them many buffers quickly
  // in order to get the stream started, then we slow down.
  this.encodeQueueDuration = config.encodeQueueDuration;

  this.groovePlaylist = groove.createPlaylist();
  this.groovePlayer = null;
  this.grooveEncoder = groove.createEncoder();
  this.grooveEncoder.encodedBufferSize = 128 * 1024;

  this.detachEncoderTimeout = null;
  this.pendingEncoderAttachDetach = false;
  this.desiredEncoderAttachState = false;
  this.flushEncodedInterval = null;
  this.groovePlaylist.pause();
  this.volume = this.groovePlaylist.gain;
  this.grooveEncoder.formatShortName = "mp3";
  this.grooveEncoder.codecShortName = "mp3";
  this.grooveEncoder.bitRate = config.encodeBitRate * 1000;

  this.importProgress = {};
  this.lastImportProgressEvent = new Date();

  // tracking playCount
  this.previousIsPlaying = false;
  this.playingStart = new Date();
  this.playingTime = 0;
  this.lastPlayingItem = null;

  this.googleApiKey = config.googleApiKey;

  this.ignoreExtensions = config.ignoreExtensions.map(makeLower);
}

Player.prototype.initialize = function(cb) {
  var self = this;
  var startupTrackInfo = null;

  initLibrary(function(err) {
    if (err) return cb(err);
    cacheTracksArray(self);
    self.requestUpdateDb();
    cacheAllOptions(function(err) {
      if (err) return cb(err);
      setInterval(doPersistCurrentTrack, 10000);
      if (startupTrackInfo) {
        self.seek(startupTrackInfo.id, startupTrackInfo.pos);
      } else {
        playlistChanged(self);
      }
      lazyReplayGainScanPlaylist(self);
      cb();
    });
  });

  function initLibrary(cb) {
    var pend = new Pend();
    pend.go(cacheAllDb);
    pend.go(cacheAllDirs);
    pend.go(cacheAllQueue);
    pend.go(cacheAllPlaylists);
    pend.go(cacheAllLabels);
    pend.wait(cb);
  }

  function cacheAllPlaylists(cb) {
    cacheAllPlaylistMeta(function(err) {
      if (err) return cb(err);
      cacheAllPlaylistItems(cb);
    });

    function cacheAllPlaylistMeta(cb) {
      dbIterate(self.db, PLAYLIST_META_KEY_PREFIX, processOne, cb);
      function processOne(key, value) {
        var playlist = deserializePlaylist(value);
        self.playlists[playlist.id] = playlist;
      }
    }

    function cacheAllPlaylistItems(cb) {
      dbIterate(self.db, PLAYLIST_KEY_PREFIX, processOne, cb);
      function processOne(key, value) {
        var playlistIdEnd = key.indexOf('.', PLAYLIST_KEY_PREFIX.length);
        var playlistId = key.substring(PLAYLIST_KEY_PREFIX.length, playlistIdEnd);
        var playlistItem = JSON.parse(value);
        self.playlists[playlistId].items[playlistItem.id] = playlistItem;
      }
    }
  }

  function cacheAllLabels(cb) {
    dbIterate(self.db, LABEL_KEY_PREFIX, processOne, cb);
    function processOne(key, value) {
      var labelId = key.substring(LABEL_KEY_PREFIX.length);
      var labelEntry = JSON.parse(value);
      self.libraryIndex.addLabel(labelEntry);
    }
  }

  function cacheAllQueue(cb) {
    dbIterate(self.db, QUEUE_KEY_PREFIX, processOne, cb);
    function processOne(key, value) {
      var plEntry = JSON.parse(value);
      self.playlist[plEntry.id] = plEntry;
    }
  }

  function cacheAllOptions(cb) {
    var options = {
      repeat: null,
      autoDjOn: null,
      autoDjHistorySize: null,
      autoDjFutureSize: null,
      hardwarePlayback: null,
      volume: null,
      currentTrackInfo: null,
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
      if (options.autoDjOn != null) {
        self.setAutoDjOn(options.autoDjOn);
      }
      if (options.autoDjHistorySize != null) {
        self.setAutoDjHistorySize(options.autoDjHistorySize);
      }
      if (options.autoDjFutureSize != null) {
        self.setAutoDjFutureSize(options.autoDjFutureSize);
      }
      if (options.volume != null) {
        self.setVolume(options.volume);
      }
      startupTrackInfo = options.currentTrackInfo;
      var hardwarePlaybackValue = options.hardwarePlayback == null ? true : options.hardwarePlayback;
      // start the hardware player first
      // fall back to dummy
      self.setHardwarePlayback(hardwarePlaybackValue, function(err) {
        if (err) {
          log.error("Unable to attach hardware player, falling back to dummy.", err.stack);
          self.setHardwarePlayback(false);
        }
        cb();
      });
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
    dbIterate(self.db, LIBRARY_DIR_PREFIX, processOne, cb);
    function processOne(key, value) {
      var dirEntry = JSON.parse(value);
      self.dirs[dirEntry.dirName] = dirEntry;
    }
  }

  function cacheAllDb(cb) {
    var scrubCmds = [];
    dbIterate(self.db, LIBRARY_KEY_PREFIX, processOne, scrubAndCb);
    function processOne(key, value) {
      var dbFile = deserializeFileData(value);
      // scrub duplicates
      if (self.dbFilesByPath[dbFile.file]) {
        scrubCmds.push({type: 'del', key: key});
      } else {
        self.libraryIndex.addTrack(dbFile);
        self.dbFilesByPath[dbFile.file] = dbFile;
        for (var labelId in dbFile.labels) {
          var files = self.dbFilesByLabel[labelId];
          if (files == null) files = self.dbFilesByLabel[labelId] = {};
          files[dbFile.key] = dbFile;
        }
      }
    }
    function scrubAndCb() {
      if (scrubCmds.length === 0) return cb();
      log.warn("Scrubbing " + scrubCmds.length + " duplicate db entries");
      self.db.batch(scrubCmds, function(err) {
        if (err) log.error("Unable to scrub duplicate tracks from db:", err.stack);
        cb();
      });
    }
  }

  function doPersistCurrentTrack() {
    if (self.isPlaying) {
      self.persistCurrentTrack();
    }
  }
};

function startEncoderAttach(self, cb) {
  if (self.desiredEncoderAttachState) return;
  self.desiredEncoderAttachState = true;

  if (self.pendingEncoderAttachDetach) return;
  self.pendingEncoderAttachDetach = true;
  self.grooveEncoder.attach(self.groovePlaylist, function(err) {
    self.pendingEncoderAttachDetach = false;

    if (err) {
      self.desiredEncoderAttachState = false;
      cb(err);
    } else if (!self.desiredEncoderAttachState) {
      startEncoderDetach(self, cb);
    }
  });
}

function startEncoderDetach(self, cb) {
  if (!self.desiredEncoderAttachState) return;
  self.desiredEncoderAttachState = false;

  if (self.pendingEncoderAttachDetach) return;
  self.pendingEncoderAttachDetach = true;
  self.grooveEncoder.detach(function(err) {
    self.pendingEncoderAttachDetach = false;

    if (err) {
      self.desiredEncoderAttachState = true;
      cb(err);
    } else if (self.desiredEncoderAttachState) {
      startEncoderAttach(self, cb);
    }
  });
}

Player.prototype.getBufferedSeconds = function() {
  if (this.recentBuffers.length < 2) return 0;
  var firstPts = this.recentBuffers[0].pts;
  var lastPts = this.recentBuffers[this.recentBuffers.length - 1].pts;
  var frameCount = lastPts - firstPts;
  var sampleRate = this.grooveEncoder.actualAudioFormat.sampleRate;
  return frameCount / sampleRate;
};

Player.prototype.attachEncoder = function(cb) {
  var self = this;

  cb = cb || logIfError;

  if (self.flushEncodedInterval) return cb();

  log.debug("first streamer connected - attaching encoder");
  self.flushEncodedInterval = setInterval(flushEncoded, 100);

  startEncoderAttach(self, cb);

  function flushEncoded() {
    if (!self.desiredEncoderAttachState || self.pendingEncoderAttachDetach) return;

    var playHead = self.groovePlayer.position();
    if (!playHead.item) return;

    var plItems = self.groovePlaylist.items();

    // get rid of old items
    var buf;
    while (buf = self.recentBuffers[0]) {
      /*
      log.debug("     buf.item " + buf.item.file.filename + "\n" +
                "playHead.item " + playHead.item.file.filename + "\n" +
                " playHead.pos " + playHead.pos + "\n" +
                "      buf.pos " + buf.pos);
      */
      if (isBufOld(buf)) {
        self.recentBuffers.shift();
      } else {
        break;
      }
    }

    // poll the encoder for more buffers until either there are no buffers
    // available or we get enough buffered
    while (self.getBufferedSeconds() < self.encodeQueueDuration) {
      buf = self.grooveEncoder.getBuffer();
      if (!buf) break;
      if (buf.buffer) {
        if (buf.item) {
          if (self.expectHeaders) {
            log.debug("encoder: got first non-header");
            self.headerBuffers = self.newHeaderBuffers;
            self.newHeaderBuffers = [];
            self.expectHeaders = false;
          }
          self.recentBuffers.push(buf);
          for (var i = 0; i < self.openStreamers.length; i += 1) {
            self.openStreamers[i].write(buf.buffer);
          }
        } else if (self.expectHeaders) {
          // this is a header
          log.debug("encoder: got header");
          self.newHeaderBuffers.push(buf.buffer);
        } else {
          // it's a footer, ignore the fuck out of it
          log.debug("ignoring encoded audio footer");
        }
      } else {
        // end of playlist sentinel
        log.debug("encoder: end of playlist sentinel");
        if (self.queueClearEncodedBuffers) {
          self.queueClearEncodedBuffers = false;
          self.clearEncodedBuffer();
          self.emit('seek');
        }
        self.expectHeaders = true;
      }
    }

    function isBufOld(buf) {
      // typical case
      if (buf.item.id === playHead.item.id) {
        return playHead.pos > buf.pos;
      }
      // edge case
      var playHeadIndex = -1;
      var bufItemIndex = -1;
      for (var i = 0; i < plItems.length; i += 1) {
        var plItem = plItems[i];
        if (plItem.id === playHead.item.id) {
          playHeadIndex = i;
        } else if (plItem.id === buf.item.id) {
          bufItemIndex = i;
        }
      }
      return playHeadIndex > bufItemIndex;
    }
  }

  function logIfError(err) {
    if (err) {
      log.error("Unable to attach encoder:", err.stack);
    }
  }
};

Player.prototype.detachEncoder = function(cb) {
  cb = cb || logIfError;

  this.clearEncodedBuffer();
  this.queueClearEncodedBuffers = false;
  clearInterval(this.flushEncodedInterval);
  this.flushEncodedInterval = null;
  startEncoderDetach(this, cb);
  this.grooveEncoder.removeAllListeners();

  function logIfError(err) {
    if (err) {
      log.error("Unable to detach encoder:", err.stack);
    }
  }
};

Player.prototype.deleteDbMtimes = function(cb) {
  cb = cb || logIfDbError;
  var updateCmds = [];
  for (var key in this.libraryIndex.trackTable) {
    var dbFile = this.libraryIndex.trackTable[key];
    delete dbFile.mtime;
    persistDbFile(dbFile, updateCmds);
  }
  this.db.batch(updateCmds, cb);
};

Player.prototype.requestUpdateDb = function(dirName, cb) {
  var fullPath = path.resolve(this.musicDirectory, dirName || "");
  this.dirScanQueue.add(fullPath, {
    dir: fullPath,
  }, cb);
};

Player.prototype.refreshFilesIndex = function(args, cb) {
  var self = this;
  var dir = args.dir;
  var dirWithSlash = ensureSep(dir);
  var walker = findit(dirWithSlash, {followSymlinks: true});
  var thisScanId = uuid();
  var delCmds = [];
  walker.on('directory', function(fullDirPath, stat, stop, linkPath) {
    var usePath = linkPath || fullDirPath;
    var dirName = path.relative(self.musicDirectory, usePath);
    var baseName = path.basename(dirName);
    if (isFileIgnored(baseName)) {
      stop();
      return;
    }
    var dirEntry = self.getOrCreateDir(dirName, stat);
    if (usePath === dirWithSlash) return; // ignore root search path
    var parentDirName = path.dirname(dirName);
    if (parentDirName === '.') parentDirName = '';
    var parentDirEntry = self.getOrCreateDir(parentDirName);
    parentDirEntry.dirEntries[baseName] = thisScanId;
  });
  walker.on('file', function(fullPath, stat, linkPath) {
    var usePath = linkPath || fullPath;
    var relPath = path.relative(self.musicDirectory, usePath);
    var dirName = path.dirname(relPath);
    if (dirName === '.') dirName = '';
    var baseName = path.basename(relPath);
    if (isFileIgnored(baseName)) return;
    var extName = path.extname(relPath);
    if (isExtensionIgnored(self, extName)) return;
    var dirEntry = self.getOrCreateDir(dirName);
    dirEntry.entries[baseName] = thisScanId;
    var fileMtime = stat.mtime.getTime();
    onAddOrChange(self, relPath, fileMtime);
  });
  walker.on('error', function(err) {
    walker.stop();
    cleanupAndCb(err);
  });
  walker.on('end', function() {
    var dirName = path.relative(self.musicDirectory, dir);
    checkDirEntry(self.dirs[dirName]);
    cleanupAndCb();

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

  function cleanupAndCb(err) {
    if (delCmds.length > 0) {
      self.db.batch(delCmds, logIfDbError);
      self.emit('deleteDbTrack');
    }
    cb(err);
  }

  function onDirMissing(parentDirEntry, baseName) {
    var dirName = path.join(parentDirEntry.dirName, baseName);
    log.debug("directory deleted:", dirName);
    var dirEntry = self.dirs[dirName];
    var watcher = dirEntry.watcher;
    if (watcher) watcher.close();
    delete self.dirs[dirName];
    delete parentDirEntry.dirEntries[baseName];
  }

  function onFileMissing(parentDirEntry, baseName) {
    var relPath = path.join(parentDirEntry.dirName, baseName);
    log.debug("file deleted:", relPath);
    delete parentDirEntry.entries[baseName];
    var dbFile = self.dbFilesByPath[relPath];
    if (dbFile) {
      // batch up some db delete commands to run after walking the file system
      delDbEntryCmds(self, dbFile, delCmds);
    }
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
    log.warn("Unable to fs.watch:", err.stack);
    watcher = null;
  }
  dirEntry.watcher = watcher;

  function onChange(eventName) {
    if (changeTriggered) clearTimeout(changeTriggered);
    changeTriggered = setTimeout(function() {
      changeTriggered = null;
      log.debug("dir updated:", dirEntry.dirName);
      self.dirScanQueue.add(fullDirPath, { dir: fullDirPath });
    }, 100);
  }

  function onWatchError(err) {
    log.error("watch error:", err.stack);
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

function startPlayerSwitchDevice(self, wantHardware, cb) {
  self.desiredPlayerHardwareState = wantHardware;
  if (self.pendingPlayerAttachDetach) return;

  self.pendingPlayerAttachDetach = true;
  if (self.groovePlayer) {
    self.groovePlayer.removeAllListeners();
    self.groovePlayer.detach(onDetachComplete);
  } else {
    onDetachComplete();
  }

  function onDetachComplete(err) {
    if (err) return cb(err);
    self.groovePlayer = groove.createPlayer();
    self.groovePlayer.deviceIndex = wantHardware ? null : groove.DUMMY_DEVICE;
    self.groovePlayer.attach(self.groovePlaylist, function(err) {
      self.pendingPlayerAttachDetach = false;
      if (err) return cb(err);
      if (self.desiredPlayerHardwareState !== wantHardware) {
        startPlayerSwitchDevice(self, self.desiredPlayerHardwareState, cb);
      } else {
        cb();
      }
    });
  }
}

Player.prototype.setHardwarePlayback = function(value, cb) {
  var self = this;

  cb = cb || logIfError;
  value = !!value;

  if (value === self.desiredPlayerHardwareState) return cb();

  startPlayerSwitchDevice(self, value, function(err) {
    if (err) return cb(err);

    self.clearEncodedBuffer();
    self.emit('seek');
    self.groovePlayer.on('nowplaying', onNowPlaying);
    self.persistOption('hardwarePlayback', self.desiredPlayerHardwareState);
    self.emit('hardwarePlayback', self.desiredPlayerHardwareState);
    cb();
  });

  function onNowPlaying() {
    var playHead = self.groovePlayer.position();
    var decodeHead = self.groovePlaylist.position();
    if (playHead.item) {
      var nowMs = (new Date()).getTime();
      var posMs = playHead.pos * 1000;
      self.trackStartDate = new Date(nowMs - posMs);
      self.currentTrack = self.grooveItems[playHead.item.id];
      playlistChanged(self);
      self.currentTrackChanged();
    } else if (!decodeHead.item) {
      if (!self.dontBelieveTheEndOfPlaylistSentinelItsATrap) {
        // both play head and decode head are null. end of playlist.
        log.debug("end of playlist");
        self.currentTrack = null;
        playlistChanged(self);
        self.currentTrackChanged();
      }
    }
  }

  function logIfError(err) {
    if (err) {
      log.error("Unable to set hardware playback mode:", err.stack);
    }
  }
};

Player.prototype.startStreaming = function(resp) {
  this.headerBuffers.forEach(function(headerBuffer) {
    resp.write(headerBuffer);
  });
  this.recentBuffers.forEach(function(recentBuffer) {
    resp.write(recentBuffer.buffer);
  });
  this.cancelDetachEncoderTimeout();
  this.attachEncoder();
  this.openStreamers.push(resp);
  this.emit('streamerConnect', resp.client);
};

Player.prototype.stopStreaming = function(resp) {
  for (var i = 0; i < this.openStreamers.length; i += 1) {
    if (this.openStreamers[i] === resp) {
      this.openStreamers.splice(i, 1);
      this.emit('streamerDisconnect', resp.client);
      break;
    }
  }
};

Player.prototype.lastStreamerDisconnected = function() {
  log.debug("last streamer disconnected");
  this.startDetachEncoderTimeout();
  if (!this.desiredPlayerHardwareState && this.isPlaying) {
    this.emit("autoPause");
    this.pause();
  }
};

Player.prototype.cancelDetachEncoderTimeout = function() {
  if (this.detachEncoderTimeout) {
    clearTimeout(this.detachEncoderTimeout);
    this.detachEncoderTimeout = null;
  }
};

Player.prototype.startDetachEncoderTimeout = function() {
  var self = this;
  self.cancelDetachEncoderTimeout();
  // we use encodeQueueDuration for the encoder timeout so that we are
  // guaranteed to have audio available for the encoder in the case of
  // detaching and reattaching the encoder.
  self.detachEncoderTimeout = setTimeout(timeout, self.encodeQueueDuration * 1000);

  function timeout() {
    if (self.openStreamers.length === 0 && self.isPlaying) {
      log.debug("detaching encoder");
      self.detachEncoder();
    }
  }
};

Player.prototype.deleteFiles = function(keys) {
  var self = this;

  var delCmds = [];
  for (var i = 0; i < keys.length; i += 1) {
    var key = keys[i];

    var dbFile = self.libraryIndex.trackTable[key];
    if (!dbFile) continue;

    var fullPath = path.join(self.musicDirectory, dbFile.file);
    delDbEntryCmds(self, dbFile, delCmds);
    fs.unlink(fullPath, logIfError);
  }

  if (delCmds.length > 0) {
    self.emit('deleteDbTrack');
    self.db.batch(delCmds, logIfError);
  }

  function logIfError(err) {
    if (err) {
      log.error("Error deleting files:", err.stack);
    }
  }
};

function delDbEntryCmds(self, dbFile, dbCmds) {
  // delete items from the queue that are being deleted from the library
  var deleteQueueItems = [];
  for (var queueId in self.playlist) {
    var queueItem = self.playlist[queueId];
    if (queueItem.key === dbFile.key) {
      deleteQueueItems.push(queueId);
    }
  }
  self.removeQueueItems(deleteQueueItems);

  // delete items from playlists that are being deleted from the library
  var playlistRemovals = {};
  for (var playlistId in self.playlists) {
    var playlist = self.playlists[playlistId];
    var removals = [];
    playlistRemovals[playlistId] = removals;
    for (var playlistItemId in playlist.items) {
      var playlistItem = playlist.items[playlistItemId];
      if (playlistItem.key === dbFile.key) {
        removals.push(playlistItemId);
      }
    }
  }
  self.playlistRemoveItems(playlistRemovals);

  self.libraryIndex.removeTrack(dbFile.key);
  delete self.dbFilesByPath[dbFile.file];
  var baseName = path.basename(dbFile.file);
  var parentDirName = path.dirname(dbFile.file);
  if (parentDirName === '.') parentDirName = '';
  var parentDirEntry = self.dirs[parentDirName];
  if (parentDirEntry) delete parentDirEntry[baseName];

  dbCmds.push({type: 'del', key: LIBRARY_KEY_PREFIX + dbFile.key});
}

Player.prototype.setVolume = function(value) {
  value = Math.min(2.0, value);
  value = Math.max(0.0, value);
  this.volume = value;
  this.groovePlaylist.setGain(value);
  this.persistOption('volume', this.volume);
  this.emit("volumeUpdate");
};

Player.prototype.importUrl = function(urlString, cb) {
  var self = this;
  cb = cb || logIfError;

  var filterIndex = 0;
  tryImportFilter();

  function tryImportFilter() {
    var importFilter = importUrlFilters[filterIndex];
    if (!importFilter) return cb();
    importFilter.fn(urlString, callNextFilter);
    function callNextFilter(err, dlStream, filenameHintWithoutPath, size) {
      if (err || !dlStream) {
        if (err) {
          log.error(importFilter.name + " import filter error, skipping:", err.stack);
        }
        filterIndex += 1;
        tryImportFilter();
        return;
      }
      self.importStream(dlStream, filenameHintWithoutPath, size, cb);
    }
  }

  function logIfError(err) {
    if (err) {
      log.error("Unable to import by URL.", err.stack, "URL:", urlString);
    }
  }
};

Player.prototype.importNames = function(names, cb) {
  var self = this;
  var pend = new Pend();
  var allDbFiles = [];
  names.forEach(function(name) {
    pend.go(function(cb) {
      youtubeSearch(name, self.googleApiKey, function(err, videoUrl) {
        if (err) {
          log.error("YouTube search error, skipping " + name + ": " + err.stack);
          cb();
          return;
        }
        self.importUrl(videoUrl, function(err, dbFiles) {
          if (err) {
            log.error("Unable to import from YouTube: " + err.stack);
          } else if (!dbFiles) {
            log.error("Unrecognized YouTube URL: " + videoUrl);
          } else if (dbFiles.length > 0) {
            allDbFiles = allDbFiles.concat(dbFiles);
          }
          cb();
        });
      });
    });
  });
  pend.wait(function() {
    cb(null, allDbFiles);
  });
};

Player.prototype.importStream = function(readStream, filenameHintWithoutPath, size, cb) {
  var self = this;
  var ext = path.extname(filenameHintWithoutPath);
  var tmpDir = path.join(self.musicDirectory, '.tmp');
  var id = uuid();
  var destPath = path.join(tmpDir, id + ext);
  var calledCallback = false;
  var writeStream = null;
  var progressTimer = null;
  var importEvent = {
    id: id,
    filenameHintWithoutPath: filenameHintWithoutPath,
    bytesWritten: 0,
    size: size,
    date: new Date(),
  };

  readStream.on('error', cleanAndCb);
  self.importProgress[importEvent.id] = importEvent;
  self.emit('importStart', importEvent);

  mkdirp(tmpDir, function(err) {
    if (calledCallback) return;
    if (err) return cleanAndCb(err);

    writeStream = fs.createWriteStream(destPath);
    readStream.pipe(writeStream);
    progressTimer = setInterval(checkProgress, 100);
    writeStream.on('close', onClose);
    writeStream.on('error', cleanAndCb);

    function checkProgress() {
      importEvent.bytesWritten = writeStream.bytesWritten;
      self.maybeEmitImportProgress();
    }
    function onClose(){
      if (calledCallback) return;
      checkProgress();
      self.importFile(destPath, filenameHintWithoutPath, function(err, dbFiles) {
        if (calledCallback) return;
        if (err) {
          cleanAndCb(err);
        } else {
          calledCallback = true;
          cleanTimer();
          delete self.importProgress[importEvent.id];
          self.emit('importEnd', importEvent);
          cb(null, dbFiles);
        }
      });
    }
  });

  function cleanTimer() {
    if (progressTimer) {
      clearInterval(progressTimer);
      progressTimer = null;
    }
  }

  function cleanAndCb(err) {
    if (writeStream) {
      fs.unlink(destPath, onUnlinkDone);
      writeStream = null;
    }
    cleanTimer();
    if (calledCallback) return;
    calledCallback = true;
    delete self.importProgress[importEvent.id];
    self.emit('importAbort', importEvent);
    cb(err);

    function onUnlinkDone(err) {
      if (err) {
        log.warn("Unable to clean up temp file:", err.stack);
      }
    }
  }
};

Player.prototype.importFile = function(srcFullPath, filenameHintWithoutPath, cb) {
  var self = this;
  cb = cb || logIfError;
  var filterIndex = 0;

  log.debug("importFile open file:", srcFullPath);

  disableFsListenRef(self, tryImportFilter);

  function tryImportFilter() {
    var importFilter = importFileFilters[filterIndex];
    if (!importFilter) return cleanAndCb();
    importFilter.fn(self, srcFullPath, filenameHintWithoutPath, callNextFilter);
    function callNextFilter(err, dbFiles) {
      if (err || !dbFiles) {
        if (err) {
          log.debug(importFilter.name + " import filter error, skipping:", err.message);
        }
        filterIndex += 1;
        tryImportFilter();
        return;
      }
      cleanAndCb(null, dbFiles);
    }
  }

  function cleanAndCb(err, dbFiles) {
    if (!dbFiles) {
      fs.unlink(srcFullPath, logIfUnlinkError);
    }
    disableFsListenUnref(self);
    cb(err, dbFiles);
  }

  function logIfUnlinkError(err) {
    if (err) {
      log.error("unable to unlink file:", err.stack);
    }
  }

  function logIfError(err) {
    if (err) {
      log.error("unable to import file:", err.stack);
    }
  }
};

Player.prototype.maybeEmitImportProgress = function() {
  var now = new Date();
  var passedTime = now - this.lastImportProgressEvent;
  if (passedTime > 500) {
    this.lastImportProgressEvent = now;
    this.emit("importProgress");
  }
};

Player.prototype.persistDirEntry = function(dirEntry, cb) {
  cb = cb || logIfError;
  this.db.put(LIBRARY_DIR_PREFIX + dirEntry.dirName, serializeDirEntry(dirEntry), cb);

  function logIfError(err) {
    if (err) {
      log.error("unable to persist db entry:", dirEntry, err.stack);
    }
  }
};

Player.prototype.persistDbFile = function(dbFile, updateCmds) {
  this.libraryIndex.addTrack(dbFile);
  this.dbFilesByPath[dbFile.file] = dbFile;
  persistDbFile(dbFile, updateCmds);
};

Player.prototype.persistOneDbFile = function(dbFile, cb) {
  cb = cb || logIfDbError;
  var updateCmds = [];
  this.persistDbFile(dbFile, updateCmds);
  this.db.batch(updateCmds, cb);
};

Player.prototype.persistOption = function(name, value, cb) {
  this.db.put(PLAYER_KEY_PREFIX + name, JSON.stringify(value), cb || logIfError);
  function logIfError(err) {
    if (err) {
      log.error("unable to persist player option:", err.stack);
    }
  }
};

Player.prototype.addToLibrary = function(args, cb) {
  var self = this;
  var relPath = args.relPath;
  var mtime = args.mtime;
  var fullPath = path.join(self.musicDirectory, relPath);
  log.debug("addToLibrary open file:", fullPath);
  groove.open(fullPath, function(err, file) {
    if (err) {
      self.invalidPaths[relPath] = err.message;
      cb();
      return;
    }
    var dbFile = self.dbFilesByPath[relPath];
    var filenameHintWithoutPath = path.basename(relPath);
    var newDbFile = grooveFileToDbFile(file, filenameHintWithoutPath, dbFile);
    newDbFile.file = relPath;
    newDbFile.mtime = mtime;
    var pend = new Pend();
    pend.go(function(cb) {
      log.debug("addToLibrary close file:", file.filename);
      file.close(cb);
    });
    pend.go(function(cb) {
      self.persistOneDbFile(newDbFile, function(err) {
        if (err) log.error("Error saving", relPath, "to db:", err.stack);
        cb();
      });
    });
    self.emit('updateDb');
    pend.wait(cb);
  });
};

Player.prototype.updateTags = function(obj) {
  var updateCmds = [];
  for (var key in obj) {
    var dbFile = this.libraryIndex.trackTable[key];
    if (!dbFile) continue;
    var props = obj[key];
    for (var propName in DB_PROPS) {
      var prop = DB_PROPS[propName];
      if (! prop.clientCanModify) continue;
      if (! (propName in props)) continue;
      var parser = PROP_TYPE_PARSERS[prop.type];
      dbFile[propName] = parser(props[propName]);
    }
    this.persistDbFile(dbFile, updateCmds);
  }
  if (updateCmds.length > 0) {
    this.db.batch(updateCmds, logIfDbError);
    this.emit('updateDb');
  }
};

Player.prototype.insertTracks = function(index, keys, tagAsRandom) {
  if (keys.length === 0) return [];
  if (index < 0) index = 0;
  if (index > this.tracksInOrder.length) index = this.tracksInOrder.length;

  var trackBeforeIndex = this.tracksInOrder[index - 1];
  var trackAtIndex = this.tracksInOrder[index];

  var prevSortKey = trackBeforeIndex ? trackBeforeIndex.sortKey : null;
  var nextSortKey = trackAtIndex ? trackAtIndex.sortKey : null;

  var items = {};
  var ids = [];
  var sortKeys = keese(prevSortKey, nextSortKey, keys.length);
  keys.forEach(function(key, i) {
    var id = uuid();
    var sortKey = sortKeys[i];
    items[id] = {
      key: key,
      sortKey: sortKey,
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
  var updateCmds = [];
  for (var id in items) {
    var item = items[id];
    var dbFile = self.libraryIndex.trackTable[item.key];
    if (!dbFile) continue;
    dbFile.lastQueueDate = new Date();
    self.persistDbFile(dbFile, updateCmds);
    var queueItem = {
      id: id,
      key: item.key,
      sortKey: item.sortKey,
      isRandom: tagAsRandom,
      grooveFile: null,
      pendingGrooveFile: false,
      deleted: false,
    };
    self.playlist[id] = queueItem;
    persistQueueItem(queueItem, updateCmds);
  }
  if (updateCmds.length > 0) {
    self.db.batch(updateCmds, logIfDbError);
    playlistChanged(self);
    lazyReplayGainScanPlaylist(self);
  }
};

Player.prototype.playlistCreate = function(id, name) {
  if (this.playlists[id]) {
    log.warn("tried to create playlist with same id as existing");
    return;
  }
  var playlist = {
    id: id,
    name: name,
    mtime: new Date().getTime(),
    items: {},
  };
  this.playlists[playlist.id] = playlist;
  this.persistPlaylist(playlist);
  this.emit('playlistCreate', playlist);
  return playlist;
};

Player.prototype.playlistRename = function(playlistId, newName) {
  var playlist = this.playlists[playlistId];
  if (!playlist) return;

  playlist.name = newName;
  playlist.mtime = new Date().getTime();
  this.persistPlaylist(playlist);
  this.emit('playlistUpdate', playlist);
};

Player.prototype.playlistDelete = function(playlistIds) {
  var delCmds = [];
  for (var i = 0; i < playlistIds.length; i += 1) {
    var playlistId = playlistIds[i];
    var playlist = this.playlists[playlistId];
    if (!playlist) continue;

    for (var id in playlist.items) {
      var item = playlist.items[id];
      if (!item) continue;

      delCmds.push({type: 'del', key: playlistItemKey(playlist, item)});
      delete playlist.items[id];
    }
    delCmds.push({type: 'del', key: playlistKey(playlist)});
    delete this.playlists[playlistId];
  }

  if (delCmds.length > 0) {
    this.db.batch(delCmds, logIfDbError);
    this.emit('playlistDelete');
  }
};

Player.prototype.playlistAddItems = function(playlistId, items) {
  var playlist = this.playlists[playlistId];
  if (!playlist) return;

  var updateCmds = [];
  for (var id in items) {
    var item = items[id];
    var dbFile = this.libraryIndex.trackTable[item.key];
    if (!dbFile) continue;

    var playlistItem = {
      id: id,
      key: item.key,
      sortKey: item.sortKey,
    };
    playlist.items[id] = playlistItem;

    updateCmds.push({
      type: 'put',
      key: playlistItemKey(playlist, playlistItem),
      value: serializePlaylistItem(playlistItem),
    });
  }

  if (updateCmds.length > 0) {
    playlist.mtime = new Date().getTime();
    updateCmds.push({
      type: 'put',
      key: playlistKey(playlist),
      value: serializePlaylist(playlist),
    });
    this.db.batch(updateCmds, logIfDbError);
    this.emit('playlistUpdate', playlist);
  }
};

Player.prototype.playlistRemoveItems = function(removals) {
  var updateCmds = [];
  for (var playlistId in removals) {
    var playlist = this.playlists[playlistId];
    if (!playlist) continue;

    var ids = removals[playlistId];
    var dirty = false;
    for (var i = 0; i < ids.length; i += 1) {
      var id = ids[i];
      var item = playlist.items[id];
      if (!item) continue;

      dirty = true;
      updateCmds.push({type: 'del', key: playlistItemKey(playlist, item)});
      delete playlist.items[id];
    }

    if (dirty) {
      playlist.mtime = new Date().getTime();
      updateCmds.push({
        type: 'put',
        key: playlistKey(playlist),
        value: serializePlaylist(playlist),
      });
    }
  }
  if (updateCmds.length > 0) {
    this.db.batch(updateCmds, logIfDbError);
    this.emit('playlistUpdate');
  }
};

// items looks like {playlistId: {id: {sortKey}}}
Player.prototype.playlistMoveItems = function(updates) {
  var updateCmds = [];

  for (var playlistId in updates) {
    var playlist = this.playlists[playlistId];
    if (!playlist) continue;

    var playlistDirty = false;
    var update = updates[playlistId];
    for (var id in update) {
      var playlistItem = playlist.items[id];
      if (!playlistItem) continue;

      var updateItem = update[id];
      playlistItem.sortKey = updateItem.sortKey;
      playlistDirty = true;
      updateCmds.push({
        type: 'put',
        key: playlistItemKey(playlist, playlistItem),
        value: serializePlaylistItem(playlistItem),
      });
    }

    if (playlistDirty) {
      playlist.mtime = new Date().getTime();
      updateCmds.push({
        type: 'put',
        key: playlistKey(playlist),
        value: serializePlaylist(playlist),
      });
    }
  }
  if (updateCmds.length > 0) {
    this.db.batch(updateCmds, logIfDbError);
    this.emit('playlistUpdate');
  }
};

Player.prototype.persistPlaylist = function(playlist, cb) {
  cb = cb || logIfDbError;
  var key = playlistKey(playlist);
  var payload = serializePlaylist(playlist);
  this.db.put(key, payload, cb);
};

Player.prototype.labelCreate = function(id, name) {
  if (id in this.libraryIndex.labelTable) {
    log.warn("tried to create label that already exists");
    return;
  }
  var color = labelColors[Math.floor(Math.random() * labelColors.length)];
  var labelEntry = {id: id, name: name, color: color};
  this.libraryIndex.addLabel(labelEntry);
  var key = LABEL_KEY_PREFIX + id;
  this.db.put(key, JSON.stringify(labelEntry), logIfDbError);
  this.emit('labelCreate');
};

Player.prototype.labelRename = function(id, name) {
  var labelEntry = this.libraryIndex.labelTable[id];
  if (!labelEntry) return;
  labelEntry.name = name;
  this.libraryIndex.addLabel(labelEntry);
  var key = LABEL_KEY_PREFIX + id;
  this.db.put(key, JSON.stringify(labelEntry), logIfDbError);
  this.emit('labelRename');
};

Player.prototype.labelColorUpdate = function(id, color) {
  var labelEntry = this.libraryIndex.labelTable[id];
  if (!labelEntry) return;
  labelEntry.color = color;
  this.libraryIndex.addLabel(labelEntry);
  var key = LABEL_KEY_PREFIX + id;
  this.db.put(key, JSON.stringify(labelEntry), logIfDbError);
  this.emit('labelColorUpdate');
};

Player.prototype.labelDelete = function(ids) {
  var updateCmds = [];
  var libraryChanged = false;
  for (var i = 0; i < ids.length; i++) {
    var labelId = ids[i];
    if (!(labelId in this.libraryIndex.labelTable)) continue;
    this.libraryIndex.removeLabel(labelId);
    var key = LABEL_KEY_PREFIX + labelId;
    updateCmds.push({type: 'del', key: key});

    // clean out references from the library
    var files = this.dbFilesByLabel[labelId];
    for (var fileId in files) {
      var dbFile = files[fileId];
      delete dbFile.labels[labelId];
      persistDbFile(dbFile, updateCmds);
      libraryChanged = true;
    }
    delete this.dbFilesByLabel[labelId];
  }
  if (updateCmds.length === 0) return;

  this.db.batch(updateCmds, logIfDbError);
  if (libraryChanged) {
    this.emit('updateDb');
  }
  this.emit('labelDelete');
};

Player.prototype.labelAdd = function(additions) {
  this.changeLabels(additions, true);
};
Player.prototype.labelRemove = function(removals) {
  this.changeLabels(removals, false);
};
Player.prototype.changeLabels = function(changes, isAdd) {
  var self = this;
  var updateCmds = [];
  for (var id in changes) {
    var labelIds = changes[id];
    var dbFile = this.libraryIndex.trackTable[id];
    if (!dbFile) continue;
    if (labelIds.length === 0) continue;
    var changedTrack = false;
    for (var i = 0; i < labelIds.length; i++) {
      var labelId = labelIds[i];
      var filesByThisLabel = self.dbFilesByLabel[labelId];
      if (isAdd) {
        if (labelId in dbFile.labels) continue; // already got it
        dbFile.labels[labelId] = 1;
        if (filesByThisLabel == null) filesByThisLabel = self.dbFilesByLabel[labelId] = {};
        filesByThisLabel[dbFile.key] = dbFile;
      } else {
        if (!(labelId in dbFile.labels)) continue; // already gone
        delete dbFile.labels[labelId];
        delete filesByThisLabel[dbFile.key];
      }
      changedTrack = true;
    }
    if (changedTrack) {
      this.persistDbFile(dbFile, updateCmds);
    }
  }
  if (updateCmds.length === 0) return;
  this.db.batch(updateCmds, logIfDbError);
  this.emit('updateDb');
};

Player.prototype.clearQueue = function() {
  this.removeQueueItems(Object.keys(this.playlist));
};

Player.prototype.removeAllRandomQueueItems = function() {
  var idsToRemove = [];
  for (var i = 0; i < this.tracksInOrder.length; i += 1) {
    var track = this.tracksInOrder[i];
    if (track.isRandom && track !== this.currentTrack) {
      idsToRemove.push(track.id);
    }
  }
  return this.removeQueueItems(idsToRemove);
};

Player.prototype.shufflePlaylist = function() {
  if (this.tracksInOrder.length === 0) return;
  if (this.autoDjOn) return this.removeAllRandomQueueItems();

  var sortKeys = this.tracksInOrder.map(function(track) {
    return track.sortKey;
  });
  shuffle(sortKeys);

  // fix sortKey and index properties
  var updateCmds = [];
  for (var i = 0; i < this.tracksInOrder.length; i += 1) {
    var track = this.tracksInOrder[i];
    track.index = i;
    track.sortKey = sortKeys[i];
    persistQueueItem(track, updateCmds);
  }
  this.db.batch(updateCmds, logIfDbError);
  playlistChanged(this);
};

Player.prototype.removeQueueItems = function(ids) {
  if (ids.length === 0) return;
  var delCmds = [];
  var currentTrackChanged = false;
  for (var i = 0; i < ids.length; i += 1) {
    var id = ids[i];
    var item = this.playlist[id];
    if (!item) continue;

    delCmds.push({type: 'del', key: QUEUE_KEY_PREFIX + id});

    if (item.grooveFile) this.playlistItemDeleteQueue.push(item);
    if (item === this.currentTrack) {
      var nextPos = this.currentTrack.index + 1;
      for (;;) {
        var nextTrack = this.tracksInOrder[nextPos];
        var nextTrackId = nextTrack && nextTrack.id;
        this.currentTrack = nextTrackId && this.playlist[nextTrack.id];
        if (!this.currentTrack && nextPos < this.tracksInOrder.length) {
          nextPos += 1;
          continue;
        }
        break;
      }
      if (this.currentTrack) {
        this.seekRequestPos = 0;
      }
      currentTrackChanged = true;
    }

    delete this.playlist[id];
  }
  if (delCmds.length > 0) this.db.batch(delCmds, logIfDbError);

  playlistChanged(this);
  if (currentTrackChanged) {
    this.currentTrackChanged();
  }
};

// items looks like {id: {sortKey}}
Player.prototype.moveQueueItems = function(items) {
  var updateCmds = [];
  for (var id in items) {
    var track = this.playlist[id];
    if (!track) continue; // race conditions, etc.
    track.sortKey = items[id].sortKey;
    persistQueueItem(track, updateCmds);
  }
  if (updateCmds.length > 0) {
    this.db.batch(updateCmds, logIfDbError);
    playlistChanged(this);
  }
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

  var sortKeys = keese(prevSortKey, nextSortKey, ids.length);
  var updateCmds = [];
  for (var i = 0; i < ids.length; i += 1) {
    var id = ids[i];
    var queueItem = this.playlist[id];
    if (!queueItem) continue;

    queueItem.sortKey = sortKeys[i];
    persistQueueItem(queueItem, updateCmds);
  }
  if (updateCmds.length > 0) {
    this.db.batch(updateCmds, logIfDbError);
    playlistChanged(this);
  }
};

Player.prototype.pause = function() {
  if (!this.isPlaying) return;
  this.isPlaying = false;
  this.pausedTime = (new Date() - this.trackStartDate) / 1000;
  this.groovePlaylist.pause();
  this.cancelDetachEncoderTimeout();
  playlistChanged(this);
  this.currentTrackChanged();
};

Player.prototype.play = function() {
  if (!this.currentTrack) {
    this.currentTrack = this.tracksInOrder[0];
  } else if (!this.isPlaying) {
    this.trackStartDate = new Date(new Date() - this.pausedTime * 1000);
  }
  this.groovePlaylist.play();
  this.startDetachEncoderTimeout();
  this.isPlaying = true;
  playlistChanged(this);
  this.currentTrackChanged();
};

// This function should be avoided in favor of seek. Note that it is called by
// some MPD protocol commands, because the MPD protocol is stupid.
Player.prototype.seekToIndex = function(index, pos) {
  var track = this.tracksInOrder[index];
  if (!track) return null;
  this.currentTrack = track;
  this.seekRequestPos = pos;
  playlistChanged(this);
  this.currentTrackChanged();
  return track;
};

Player.prototype.seek = function(id, pos) {
  var track = this.playlist[id];
  if (!track) return null;
  this.currentTrack = this.playlist[id];
  this.seekRequestPos = pos;
  playlistChanged(this);
  this.currentTrackChanged();
  return track;
};

Player.prototype.next = function() {
  return this.skipBy(1);
};

Player.prototype.prev = function() {
  return this.skipBy(-1);
};

Player.prototype.skipBy = function(amt) {
  var defaultIndex = amt > 0 ? -1 : this.tracksInOrder.length;
  var currentIndex = this.currentTrack ? this.currentTrack.index : defaultIndex;
  var newIndex = currentIndex + amt;
  return this.seekToIndex(newIndex, 0);
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

Player.prototype.setAutoDjOn = function(value) {
  value = !!value;
  if (value === this.autoDjOn) return;
  this.autoDjOn = value;
  this.persistOption('autoDjOn', this.autoDjOn);
  this.emit('autoDjOn');
  this.checkAutoDj();
};

Player.prototype.setAutoDjHistorySize = function(value) {
  value = Math.floor(value);
  if (value === this.autoDjHistorySize) return;
  this.autoDjHistorySize = value;
  this.persistOption('autoDjHistorySize', this.autoDjHistorySize);
  this.emit('autoDjHistorySize');
  this.checkAutoDj();
};

Player.prototype.setAutoDjFutureSize = function(value) {
  value = Math.floor(value);
  if (value === this.autoDjFutureSize) return;
  this.autoDjFutureSize = value;
  this.persistOption('autoDjFutureSize', this.autoDjFutureSize);
  this.emit('autoDjFutureSize');
  this.checkAutoDj();
};

Player.prototype.stop = function() {
  this.isPlaying = false;
  this.cancelDetachEncoderTimeout();
  this.groovePlaylist.pause();
  this.seekRequestPos = 0;
  this.pausedTime = 0;
  playlistChanged(this);
};

Player.prototype.clearEncodedBuffer = function() {
  while (this.recentBuffers.length > 0) {
    this.recentBuffers.shift();
  }
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

Player.prototype.queueScan = function(dbFile) {
  var self = this;

  var scanKey, scanType;
  if (dbFile.albumName) {
    scanType = 'album';
    scanKey = self.libraryIndex.getAlbumKey(dbFile);
  } else {
    scanType = 'track';
    scanKey = dbFile.key;
  }

  if (self.scanQueue.idInQueue(scanKey)) {
    return;
  }
  self.scanQueue.add(scanKey, {
    type: scanType,
    key: scanKey,
  });
};

Player.prototype.performScan = function(args, cb) {
  var self = this;
  var scanType = args.type;
  var scanKey = args.key;

  // build list of files we want to open
  var dbFilesToOpen;
  if (scanType === 'album') {
    var albumKey = scanKey;
    self.libraryIndex.rebuildTracks();
    var album = self.libraryIndex.albumTable[albumKey];
    if (!album) {
      log.warn("wanted to scan album with key", JSON.stringify(albumKey), "but no longer exists.");
      cb();
      return;
    }
    log.debug("Scanning album for loudness:", JSON.stringify(albumKey));
    dbFilesToOpen = album.trackList;
  } else if (scanType === 'track') {
    var trackKey = scanKey;
    var dbFile = self.libraryIndex.trackTable[trackKey];
    if (!dbFile) {
      log.warn("wanted to scan track with key", JSON.stringify(trackKey), "but no longer exists.");
      cb();
      return;
    }
    log.debug("Scanning track for loudness:", JSON.stringify(trackKey));
    dbFilesToOpen = [dbFile];
  } else {
    throw new Error("unexpected scan type: " + scanType);
  }

  // open all the files in the list
  var pend = new Pend();
  // we're already doing multiple parallel scans. within each scan let's
  // read one thing at a time to avoid slamming the system.
  pend.max = 1;

  var grooveFileList = [];
  var files = {};
  dbFilesToOpen.forEach(function(dbFile) {
    pend.go(function(cb) {
      var fullPath = path.join(self.musicDirectory, dbFile.file);
      log.debug("performScan open file:", fullPath);
      groove.open(fullPath, function(err, file) {
        if (err) {
          log.error("Error opening", fullPath, "in order to scan:", err.stack);
        } else {
          var fileInfo;
          files[file.id] = fileInfo = {
            dbFile: dbFile,
            loudnessDone: false,
            fingerprintDone: false,
          };
          self.ongoingScans[dbFile.key] = fileInfo;
          grooveFileList.push(file);
        }
        cb();
      });
    });
  });

  var scanPlaylist;
  var endOfPlaylistPend = new Pend();

  var scanDetector;
  var scanDetectorAttached = false;
  var endOfDetectorCb;

  var scanFingerprinter;
  var scanFingerprinterAttached = false;
  var endOfFingerprinterCb;

  pend.wait(function() {
    // emit this because we updated ongoingScans
    self.emit('scanProgress');

    scanPlaylist = groove.createPlaylist();
    scanPlaylist.setFillMode(groove.ANY_SINK_FULL);
    scanDetector = groove.createLoudnessDetector();
    scanFingerprinter = groove.createFingerprinter();

    scanDetector.on('info', onLoudnessInfo);
    scanFingerprinter.on('info', onFingerprinterInfo);

    var pend = new Pend();
    pend.go(attachLoudnessDetector);
    pend.go(attachFingerprinter);
    pend.wait(onEverythingAttached);
  });

  function onEverythingAttached(err) {
    if (err) {
      log.error("Error attaching:", err.stack);
      cleanupAndCb();
      return;
    }

    grooveFileList.forEach(function(file) {
      scanPlaylist.insert(file);
    });

    endOfPlaylistPend.wait(function() {
      for (var fileId in files) {
        var fileInfo = files[fileId];
        var dbFile = fileInfo.dbFile;
        self.persistOneDbFile(dbFile);
        self.emit('scanComplete', dbFile);
      }
      cleanupAndCb();
    });
  }

  function attachLoudnessDetector(cb) {
    scanDetector.attach(scanPlaylist, function(err) {
      if (err) return cb(err);
      scanDetectorAttached = true;
      endOfPlaylistPend.go(function(cb) {
        endOfDetectorCb = cb;
      });
      cb();
    });
  }

  function attachFingerprinter(cb) {
    scanFingerprinter.attach(scanPlaylist, function(err) {
      if (err) return cb(err);
      scanFingerprinterAttached = true;
      endOfPlaylistPend.go(function(cb) {
        endOfFingerprinterCb = cb;
      });
      cb();
    });
  }

  function onLoudnessInfo() {
    var info;
    while (info = scanDetector.getInfo()) {
      var gain = groove.loudnessToReplayGain(info.loudness);
      var dbFile;
      var fileInfo;
      if (info.item) {
        fileInfo = files[info.item.file.id];
        fileInfo.loudnessDone = true;
        dbFile = fileInfo.dbFile;
        log.info("loudness scan file complete:", dbFile.name,
            "gain", gain, "duration", info.duration);
        dbFile.replayGainTrackGain = gain;
        dbFile.replayGainTrackPeak = info.peak;
        dbFile.duration = info.duration;
        checkUpdateGroovePlaylist(self);
        self.emit('scanProgress');
      } else {
        log.debug("loudness scan complete:", JSON.stringify(scanKey), "gain", gain);
        for (var fileId in files) {
          fileInfo = files[fileId];
          dbFile = fileInfo.dbFile;
          dbFile.replayGainAlbumGain = gain;
          dbFile.replayGainAlbumPeak = info.peak;
        }
        checkUpdateGroovePlaylist(self);
        if (endOfDetectorCb) {
          endOfDetectorCb();
          endOfDetectorCb = null;
        }
        return;
      }
    }
  }

  function onFingerprinterInfo() {
    var info;
    while (info = scanFingerprinter.getInfo()) {
      if (info.item) {
        var fileInfo = files[info.item.file.id];
        fileInfo.fingerprintDone = true;
        var dbFile = fileInfo.dbFile;
        log.info("fingerprint scan file complete:", dbFile.name);
        dbFile.fingerprint = info.fingerprint;
        self.emit('scanProgress');
      } else {
        log.debug("fingerprint scan complete:", JSON.stringify(scanKey));
        if (endOfFingerprinterCb) {
          endOfFingerprinterCb();
          endOfFingerprinterCb = null;
        }
        return;
      }
    }
  }

  function cleanupAndCb() {
    grooveFileList.forEach(function(file) {
      pend.go(function(cb) {
        var fileInfo = files[file.id];
        var dbFile = fileInfo.dbFile;
        delete self.ongoingScans[dbFile.key];
        log.debug("performScan close file:", file.filename);
        file.close(cb);
      });
    });
    if (scanDetectorAttached) pend.go(detachLoudnessScanner);
    if (scanFingerprinterAttached) pend.go(detachFingerprinter);
    pend.wait(function(err) {
      // emit this because we changed ongoingScans above
      self.emit('scanProgress');
      cb(err);
    });
  }

  function detachLoudnessScanner(cb) {
    scanDetector.detach(cb);
  }

  function detachFingerprinter(cb) {
    scanFingerprinter.detach(cb);
  }
};

Player.prototype.checkAutoDj = function() {
  var self = this;
  if (!self.autoDjOn) return;

  // if no track is playing, assume the first track is about to be
  var currentIndex = self.currentTrack ? self.currentTrack.index : 0;

  var deleteCount = Math.max(currentIndex - self.autoDjHistorySize, 0);
  if (self.autoDjHistorySize < 0) deleteCount = 0;
  var addCount = Math.max(self.autoDjFutureSize + 1 - (self.tracksInOrder.length - currentIndex), 0);

  var idsToDelete = [];
  for (var i = 0; i < deleteCount; i += 1) {
    idsToDelete.push(self.tracksInOrder[i].id);
  }
  var keys = getRandomSongKeys(addCount);
  self.removeQueueItems(idsToDelete);
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
    // area of a triangle made of squares is n(n+1)/2
    var triangleArea = Math.floor(maxWeight * (maxWeight + 1) / 2);
    if (maxWeight === 0) maxWeight = 1;
    var rectangleArea = maxWeight * neverQueued.length;
    var totalSize = triangleArea + rectangleArea;
    if (totalSize === 0) return [];
    // decode indexes through the distribution shape
    var keys = [];
    for (var i = 0; i < count; i += 1) {
      var index = Math.random() * totalSize;
      if (index < triangleArea) {
        // index falls in the triangle
        // inverse of y = x(x+1)/2  is  x = (sqrt(8y+1)-1)/2
        keys.push(sometimesQueued[Math.floor((Math.sqrt(8 * index + 1) - 1) / 2)].key);
      } else {
        // index falls in the rectangle
        keys.push(neverQueued[Math.floor((index - triangleArea) / maxWeight)].key);
      }
    }
    return keys;
  }
};

Player.prototype.currentTrackChanged = function() {
  this.persistCurrentTrack();
  this.emit('currentTrack');
};

Player.prototype.persistCurrentTrack = function(cb) {
  // save the current track and time to db
  var currentTrackInfo = {
    id: this.currentTrack && this.currentTrack.id,
    pos: this.getCurPos(),
  };
  this.persistOption('currentTrackInfo', currentTrackInfo, cb);
};

Player.prototype.sortAndQueueTracks = function(tracks) {
  // given an array of tracks, sort them according to the library sorting
  // and then queue them in the best place
  if (!tracks.length) return;
  var sortedTracks = sortTracks(tracks);
  this.queueTracks(sortedTracks);
};

Player.prototype.sortAndQueueTracksInPlaylist = function(playlist, tracks, previousKey, nextKey) {
  if (!tracks.length) return;
  var sortedTracks = sortTracks(tracks);

  var items = {};
  var sortKeys = keese(previousKey, nextKey, tracks.length);
  for (var i = 0; i < tracks.length; i += 1) {
    var track = tracks[i];
    var sortKey = sortKeys[i];
    var id = uuid();
    items[id] = {
      key: track.key,
      sortKey: sortKey,
    };
  }
  this.playlistAddItems(playlist.id, items);
};

Player.prototype.queueTrackKeys = function(trackKeys, previousKey, nextKey) {
  if (!trackKeys.length) return;
  if (previousKey == null && nextKey == null) {
    var defaultPos = this.getDefaultQueuePosition();
    previousKey = defaultPos.previousKey;
    nextKey = defaultPos.nextKey;
  }

  var items = {};
  var sortKeys = keese(previousKey, nextKey, trackKeys.length);
  for (var i = 0; i < trackKeys.length; i += 1) {
    var trackKey = trackKeys[i];
    var sortKey = sortKeys[i];
    var id = uuid();
    items[id] = {
      key: trackKey,
      sortKey: sortKey,
    };
  }
  this.addItems(items, false);
};

Player.prototype.queueTracks = function(tracks, previousKey, nextKey) {
  // given an array of tracks, and a previous sort key and a next sort key,
  // call addItems correctly
  var trackKeys = tracks.map(function(track) {
    return track.key;
  }).filter(function(key) {
    return !!key;
  });
  return this.queueTrackKeys(trackKeys, previousKey, nextKey);
};

Player.prototype.getDefaultQueuePosition = function() {
  var previousKey = this.currentTrack && this.currentTrack.sortKey;
  var nextKey = null;
  var startPos = this.currentTrack ? this.currentTrack.index + 1 : 0;
  for (var i = startPos; i < this.tracksInOrder.length; i += 1) {
    var track = this.tracksInOrder[i];
    var sortKey = track.sortKey;
    if (track.isRandom) {
      nextKey = sortKey;
      break;
    }
    previousKey = sortKey;
  }
  return {
    previousKey: previousKey,
    nextKey: nextKey
  };
};

function persistDbFile(dbFile, updateCmds) {
  updateCmds.push({
    type: 'put',
    key: LIBRARY_KEY_PREFIX + dbFile.key,
    value: serializeFileData(dbFile),
  });
}

function persistQueueItem(item, updateCmds) {
  updateCmds.push({
    type: 'put',
    key: QUEUE_KEY_PREFIX + item.id,
    value: serializeQueueItem(item),
  });
}

function onAddOrChange(self, relPath, fileMtime, cb) {
  cb = cb || logIfError;

  // check the mtime against the mtime of the same file in the db
  var dbFile = self.dbFilesByPath[relPath];

  if (dbFile) {
    var dbMtime = dbFile.mtime;

    if (dbMtime >= fileMtime) {
      // the info we have in our db for this file is fresh
      cb(null, dbFile);
      return;
    }
  }
  self.addQueue.add(relPath, {
    relPath: relPath,
    mtime: fileMtime,
  });
  self.addQueue.waitForId(relPath, function(err) {
    var dbFile = self.dbFilesByPath[relPath];
    cb(err, dbFile);
  });

  function logIfError(err) {
    if (err) {
      log.error("Unable to add to queue:", err.stack);
    }
  }
}

function checkPlayCount(self) {
  if (self.isPlaying && !self.previousIsPlaying) {
    self.playingStart = new Date(new Date() - self.playingTime);
    self.previousIsPlaying = true;
  }
  self.playingTime = new Date() - self.playingStart;

  if (self.currentTrack === self.lastPlayingItem) return;

  if (self.lastPlayingItem) {
    var dbFile = self.libraryIndex.trackTable[self.lastPlayingItem.key];
    if (dbFile) {
      var minAmt = 15 * 1000;
      var maxAmt = 4 * 60 * 1000;
      var halfAmt = dbFile.duration / 2 * 1000;

      if (self.playingTime >= minAmt && (self.playingTime >= maxAmt || self.playingTime >= halfAmt)) {
        dbFile.playCount += 1;
        self.persistOneDbFile(dbFile);
        self.emit('play', self.lastPlayingItem, dbFile, self.playingStart);
        self.emit('updateDb');
      }
    }
  }
  self.lastPlayingItem = self.currentTrack;
  self.previousIsPlaying = self.isPlaying;
  self.playingStart = new Date();
  self.playingTime = 0;
}

function disableFsListenRef(self, fn) {
  self.disableFsRefCount += 1;
  if (self.disableFsRefCount === 1) {
    log.debug("pause dirScanQueue");
    self.dirScanQueue.setPause(true);
    self.dirScanQueue.waitForProcessing(fn);
  } else {
    fn();
  }
}

function disableFsListenUnref(self) {
  self.disableFsRefCount -= 1;
  if (self.disableFsRefCount === 0) {
    log.debug("unpause dirScanQueue");
    self.dirScanQueue.setPause(false);
  } else if (self.disableFsRefCount < 0) {
    throw new Error("disableFsListenUnref called too many times");
  }
}

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
  // clear the queue since we're going to completely rebuild it anyway
  // this allows the following priority code to work.
  self.scanQueue.clear();

  // prioritize the currently playing track, followed by the next tracks,
  // followed by the previous tracks
  var albumGain = {};
  var start1 = self.currentTrack ? self.currentTrack.index : 0;
  var i;
  for (i = start1; i < self.tracksInOrder.length; i += 1) {
    checkScan(self.tracksInOrder[i]);
  }
  for (i = 0; i < start1; i += 1) {
    checkScan(self.tracksInOrder[i]);
  }

  function checkScan(track) {
    var dbFile = self.libraryIndex.trackTable[track.key];
    if (!dbFile) return;
    var albumKey = self.libraryIndex.getAlbumKey(dbFile);
    var needScan =
        dbFile.fingerprint == null ||
        dbFile.replayGainAlbumGain == null ||
        dbFile.replayGainTrackGain == null ||
        (dbFile.albumName && albumGain[albumKey] && albumGain[albumKey] !== dbFile.replayGainAlbumGain);
    if (needScan) {
      self.queueScan(dbFile);
    } else {
      albumGain[albumKey] = dbFile.replayGainAlbumGain;
    }
  }
}

function playlistChanged(self) {
  cacheTracksArray(self);
  disambiguateSortKeys(self);

  if (self.currentTrack) {
    self.tracksInOrder.forEach(function(track, index) {
      var prevDiff = self.currentTrack.index - index;
      var nextDiff = index - self.currentTrack.index;
      var withinPrev = prevDiff <= PREV_FILE_COUNT && prevDiff >= 0;
      var withinNext = nextDiff <= NEXT_FILE_COUNT && nextDiff >= 0;
      var shouldHaveGrooveFile = withinPrev || withinNext;
      var hasGrooveFile = track.grooveFile != null || track.pendingGrooveFile;
      if (hasGrooveFile && !shouldHaveGrooveFile) {
        self.playlistItemDeleteQueue.push(track);
      } else if (!hasGrooveFile && shouldHaveGrooveFile) {
        preloadFile(self, track);
      }
    });
  } else {
    self.isPlaying = false;
    self.cancelDetachEncoderTimeout();
    self.trackStartDate = null;
    self.pausedTime = 0;
  }
  checkUpdateGroovePlaylist(self);
  performGrooveFileDeletes(self);

  self.checkAutoDj();

  checkPlayCount(self);
  self.emit('queueUpdate');
}

function performGrooveFileDeletes(self) {
  while (self.playlistItemDeleteQueue.length) {
    var item = self.playlistItemDeleteQueue.shift();

    // we set this so that any callbacks that return which were trying to
    // set the grooveItem can check if the item got deleted
    item.deleted = true;

    if (!item.grooveFile) continue;

    log.debug("performGrooveFileDeletes close file:", item.grooveFile.filename);
    var grooveFile = item.grooveFile;
    item.grooveFile = null;
    closeFile(grooveFile);
  }
}

function preloadFile(self, track) {
  var relPath = self.libraryIndex.trackTable[track.key].file;
  var fullPath = path.join(self.musicDirectory, relPath);
  track.pendingGrooveFile = true;

  log.debug("preloadFile open file:", fullPath);

  // set this so that we know we want the file preloaded
  track.deleted = false;

  groove.open(fullPath, function(err, file) {
    track.pendingGrooveFile = false;
    if (err) {
      log.error("Error opening", relPath, err.stack);
      return;
    }
    if (track.deleted) {
      log.debug("preloadFile close file (already deleted):", file.filename);
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

  if (playHeadItemId) {
    while (groovePlIndex < groovePlaylist.length) {
      grooveItem = groovePlaylist[groovePlIndex];
      if (grooveItem.id === playHeadItemId) break;
      // this groove playlist item is before the current playhead. delete it!
      self.groovePlaylist.remove(grooveItem);
      delete self.grooveItems[grooveItem.id];
      groovePlIndex += 1;
    }
  }

  var plItemIndex = self.currentTrack.index;
  var plTrack;
  var currentGrooveItem = null; // might be different than playHead.item
  var groovePlItemCount = 0;
  var gainAndPeak;
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
      gainAndPeak = calcGainAndPeak(plTrack);
      self.groovePlaylist.setItemGain(grooveItem, gainAndPeak.gain);
      self.groovePlaylist.setItemPeak(grooveItem, gainAndPeak.peak);
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

  // we still need to add more libgroove playlist items, but this one has
  // not yet finished loading from disk. We must take note of this so that
  // if we receive the end of playlist sentinel, we start playback again
  // once this track has finished loading.
  self.dontBelieveTheEndOfPlaylistSentinelItsATrap = true;
  while (groovePlItemCount < NEXT_FILE_COUNT) {
    plTrack = self.tracksInOrder[plItemIndex];
    if (!plTrack) {
      // we hit the end of the groove basin playlist. we're done adding tracks
      // to the libgroove playlist.
      self.dontBelieveTheEndOfPlaylistSentinelItsATrap = false;
      break;
    }
    if (!plTrack.grooveFile) {
      break;
    }
    // compute the gain adjustment
    gainAndPeak = calcGainAndPeak(plTrack);
    grooveItem = self.groovePlaylist.insert(plTrack.grooveFile, gainAndPeak.gain, gainAndPeak.peak);
    self.grooveItems[grooveItem.id] = plTrack;
    currentGrooveItem = currentGrooveItem || grooveItem;
    incrementPlIndex();
  }

  if (currentGrooveItem && self.seekRequestPos >= 0) {
    var seekPos = self.seekRequestPos;
    // we want to clear encoded buffers after the seek completes, e.g. after
    // we get the end of playlist sentinel
    self.clearEncodedBuffer();
    self.queueClearEncodedBuffers = true;
    self.groovePlaylist.seek(currentGrooveItem, seekPos);
    self.seekRequestPos = -1;
    if (self.isPlaying) {
      var nowMs = (new Date()).getTime();
      var posMs = seekPos * 1000;
      self.trackStartDate = new Date(nowMs - posMs);
    } else {
      self.pausedTime = seekPos;
    }
    self.currentTrackChanged();
  }

  function calcGainAndPeak(plTrack) {
    // if the previous item is the previous item from the album, or the
    // next item is the next item from the album, use album replaygain.
    // else, use track replaygain.
    var dbFile = self.libraryIndex.trackTable[plTrack.key];
    var albumMode = albumInfoMatch(-1) || albumInfoMatch(1);

    var gain = REPLAYGAIN_PREAMP;
    var peak;
    if (dbFile.replayGainAlbumGain != null && albumMode) {
      gain *= dBToFloat(dbFile.replayGainAlbumGain);
      peak = dbFile.replayGainAlbumPeak || 1.0;
    } else if (dbFile.replayGainTrackGain != null) {
      gain *= dBToFloat(dbFile.replayGainTrackGain);
      peak = dbFile.replayGainTrackPeak || 1.0;
    } else {
      gain *= REPLAYGAIN_DEFAULT;
      peak = 1.0;
    }
    return {gain: gain, peak: peak};

    function albumInfoMatch(dir) {
      var otherPlTrack = self.tracksInOrder[plTrack.index + dir];
      if (!otherPlTrack) return false;

      var otherDbFile = self.libraryIndex.trackTable[otherPlTrack.key];
      if (!otherDbFile) return false;

      var albumMatch = self.libraryIndex.getAlbumKey(dbFile) === self.libraryIndex.getAlbumKey(otherDbFile);
      if (!albumMatch) return false;

      // if there are no track numbers then it's hardly an album, is it?
      if (dbFile.track == null || otherDbFile.track == null) {
        return false;
      }

      var trackMatch = dbFile.track + dir === otherDbFile.track;
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

function isFileIgnored(basename) {
  return (/^\./).test(basename) || (/~$/).test(basename);
}

function isExtensionIgnored(self, extName) {
  var extNameLower = extName.toLowerCase();
  for (var i = 0; i < self.ignoreExtensions.length; i += 1) {
    if (self.ignoreExtensions[i] === extNameLower) {
      return true;
    }
  }
  return false;
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

function serializeQueueItem(item) {
  return JSON.stringify({
    id: item.id,
    key: item.key,
    sortKey: item.sortKey,
    isRandom: item.isRandom,
  });
}

function serializePlaylistItem(item) {
  return JSON.stringify({
    id: item.id,
    key: item.key,
    sortKey: item.sortKey,
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
    if (prop.type === 'set') {
      out[propName] = copySet(value);
    } else {
      out[propName] = value;
    }
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

function filenameWithoutExt(filename) {
  var ext = path.extname(filename);
  return filename.substring(0, filename.length - ext.length);
}

function closeFile(file) {
  file.close(function(err) {
    if (err) {
      log.error("Error closing", file, err.stack);
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

function grooveFileToDbFile(file, filenameHintWithoutPath, object) {
  object = object || {key: uuid()};
  var parsedTrack = parseTrackString(file.getMetadata("track"));
  var parsedDisc = parseTrackString(
      file.getMetadata("disc") ||
      file.getMetadata("TPA") ||
      file.getMetadata("TPOS"));
  var newValues = {
    name: (file.getMetadata("title") || filenameWithoutExt(filenameHintWithoutPath) || "").trim(),
    artistName: (file.getMetadata("artist") || "").trim(),
    composerName: (file.getMetadata("composer") ||
                   file.getMetadata("TCM") || "").trim(),
    performerName: (file.getMetadata("performer") || "").trim(),
    albumArtistName: (file.getMetadata("album_artist") || "").trim(),
    albumName: (file.getMetadata("album") || "").trim(),
    compilation: !!(parseInt(file.getMetadata("TCP"),  10) ||
                    parseInt(file.getMetadata("TCMP"), 10) ||
                    parseInt(file.getMetadata("COMPILATION"), 10) ||
                    parseInt(file.getMetadata("Compilation"), 10) ||
                    parseInt(file.getMetadata("cpil"), 10) ||
                    parseInt(file.getMetadata("WM/IsCompilation"), 10)),
    track: parsedTrack.value,
    trackCount: parsedTrack.total,
    disc: parsedDisc.value,
    discCount: parsedDisc.total,
    duration: file.duration(),
    year: parseIntOrNull(file.getMetadata("date")),
    genre: file.getMetadata("genre"),
    replayGainTrackGain: parseFloatOrNull(file.getMetadata("REPLAYGAIN_TRACK_GAIN")),
    replayGainTrackPeak: parseFloatOrNull(file.getMetadata("REPLAYGAIN_TRACK_PEAK")),
    replayGainAlbumGain: parseFloatOrNull(file.getMetadata("REPLAYGAIN_ALBUM_GAIN")),
    replayGainAlbumPeak: parseFloatOrNull(file.getMetadata("REPLAYGAIN_ALBUM_PEAK")),
    labels: {},
  };
  for (var key in newValues) {
    if (object[key] == null) object[key] = newValues[key];
  }
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

function ensureGrooveVersionIsOk() {
  var ver = groove.getVersion();
  var verStr = ver.major + '.' + ver.minor + '.' + ver.patch;
  var reqVer = '>=4.1.1';

  if (semver.satisfies(verStr, reqVer)) return;

  log.fatal("Found libgroove", verStr, "need", reqVer);
  process.exit(1);
}

function playlistItemKey(playlist, item) {
  return PLAYLIST_KEY_PREFIX + playlist.id + '.' + item.id;
}

function playlistKey(playlist) {
  return PLAYLIST_META_KEY_PREFIX + playlist.id;
}

function serializePlaylist(playlist) {
  return JSON.stringify({
    id: playlist.id,
    name: playlist.name,
    mtime: playlist.mtime,
  });
}

function deserializePlaylist(str) {
  var playlist = JSON.parse(str);
  playlist.items = {};
  return playlist;
}

function zfill(number, size) {
  number = String(number);
  while (number.length < size) number = "0" + number;
  return number;
}

function setGrooveLoggingLevel() {
  switch (log.level) {
    case log.levels.Fatal:
    case log.levels.Error:
    case log.levels.Info:
    case log.levels.Warn:
      groove.setLogging(groove.LOG_QUIET);
      break;
    case log.levels.Debug:
      groove.setLogging(groove.LOG_INFO);
      break;
  }
}

function importFileAsSong(self, srcFullPath, filenameHintWithoutPath, cb) {
  groove.open(srcFullPath, function(err, file) {
    if (err) return cb(err);
    var newDbFile = grooveFileToDbFile(file, filenameHintWithoutPath);
    var suggestedPath = self.getSuggestedPath(newDbFile, filenameHintWithoutPath);
    var pend = new Pend();
    pend.go(function(cb) {
      log.debug("importFileAsSong close file:", file.filename);
      file.close(cb);
    });
    pend.go(function(cb) {
      tryMv(suggestedPath, cb);
    });
    pend.wait(function(err) {
      if (err) return cb(err);
      cb(null, [newDbFile]);
    });

    function tryMv(destRelPath, cb) {
      var destFullPath = path.join(self.musicDirectory, destRelPath);
      // before importFileAsSong is called, file system watching is disabled.
      // So we can safely move files into the library without triggering an
      // update db.
      mv(srcFullPath, destFullPath, {mkdirp: true, clobber: false}, function(err) {
        if (err) {
          if (err.code === 'EEXIST') {
            tryMv(uniqueFilename(destRelPath), cb);
          } else {
            cb(err);
          }
          return;
        }
        onAddOrChange(self, destRelPath, (new Date()).getTime(), function(err, dbFile) {
          if (err) return cb(err);
          newDbFile = dbFile;
          cb();
        });
      });
    }
  });
}

function importFileAsZip(self, srcFullPath, filenameHintWithoutPath, cb) {
  yauzl.open(srcFullPath, function(err, zipfile) {
    if (err) return cb(err);
    var allDbFiles = [];
    var pend = new Pend();
    zipfile.on('error', handleError);
    zipfile.on('entry', onEntry);
    zipfile.on('end', onEnd);

    function onEntry(entry) {
      if (/\/$/.test(entry.fileName)) {
        // ignore directories
        return;
      }
      pend.go(function(cb) {
        zipfile.openReadStream(entry, function(err, readStream) {
          if (err) {
            log.warn("Error reading zip file:", err.stack);
            cb();
            return;
          }
          var entryBaseName = path.basename(entry.fileName);
          self.importStream(readStream, entryBaseName, entry.uncompressedSize, function(err, dbFiles) {
            if (err) {
              log.warn("unable to import entry from zip file:", err.stack);
            } else if (dbFiles) {
              allDbFiles = allDbFiles.concat(dbFiles);
            }
            cb();
          });
        });
      });
    }

    function onEnd() {
      pend.wait(function() {
        unlinkZipFile();
        cb(null, allDbFiles);
      });
    }

    function handleError(err) {
      unlinkZipFile();
      cb(err);
    }

    function unlinkZipFile() {
      fs.unlink(srcFullPath, function(err) {
        if (err) {
          log.error("Unable to remove zip file after importing:", err.stack);
        }
      });
    }
  });
}

// sort keys according to how they appear in the library
function sortTracks(tracks) {
  var lib = new MusicLibraryIndex();
  tracks.forEach(function(track) {
    lib.addTrack(track);
  });
  lib.rebuildTracks();
  var results = [];
  lib.artistList.forEach(function(artist) {
    artist.albumList.forEach(function(album) {
      album.trackList.forEach(function(track) {
        results.push(track);
      });
    });
  });
  return results;
}

function logIfDbError(err) {
  if (err) {
    log.error("Unable to update DB:", err.stack);
  }
}

function makeLower(str) {
  return str.toLowerCase();
}

function copySet(set) {
  var out = {};
  for (var key in set) {
    out[key] = 1;
  }
  return out;
}
