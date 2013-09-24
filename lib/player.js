var groove = require('groove');
var EventEmitter = require('events').EventEmitter;
var util = require('util');
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

util.inherits(Player, EventEmitter);
function Player(gb) {
  EventEmitter.call(this);
  this.gb = gb;
  this.db = gb.db;
  this.musicDirectory = gb.config.musicDirectory;
  this.allDbFiles = {};
  this.addQueue = new Pend();
  this.addQueue.max = 10;
}

Player.prototype.initializeLibrary = function(cb) {
  var self = this;

  cacheAllDb(function(err) {
    if (err) {
      cb(err);
      return;
    }

    watchLibrary();
    cb();
  });

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

