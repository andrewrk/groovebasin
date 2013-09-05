var path = require('path');
var util = require('util');
var Pend = require('pend');
var walk = require('walkdir');
var fs = require('fs');
var EventEmitter = require('events').EventEmitter;
var trackNameFromFile = require('./futils').trackNameFromFile;

// using my fork while we wait for upstream to publish some critical bug fixes
var MusicMetadataParser = require('musicmetadata-superjoe30');

module.exports = Library;

// include files of this extension even if they don't seem to be audio
var extraExtensions = ['.aac', '.mp3', '.ogg', '.flac', '.wma'];
var defaultMetaData = {
  title: '',
  artist: [],
  albumartist: [],
  album: '',
  year: "",
  track: { no: 0, of: 0 },
};

util.inherits(Library, EventEmitter);
function Library(musicLibPath) {
  EventEmitter.call(this);
  this.musicLibPath = musicLibPath;
}

Library.prototype.get_library = function(cb){
  if (this.scan_complete) {
    cb(this.library);
  } else {
    this.once('library', cb);
  }
};

Library.prototype.startScan = function(){
  startScan(this);
}

function startScan(self) {
  if (self.library != null) return;

  self.library = {};
  console.log('starting library scan');
  var start_time = new Date();
  var pend = new Pend();
  pend.max = 20;
  var musicPath = maybeAddTrailingSlash(self.musicLibPath);
  var walker = walk.walk(musicPath);
  walker.on('file', function(filename, stat) {
    if (ignoreFile(filename)) return;
    pend.go(function(cb) {
      var stream = fs.createReadStream(filename);
      var parser = new MusicMetadataParser(stream);
      var localFile = path.relative(self.musicLibPath, filename);
      var metadata = null;
      parser.on('mime', function(mime) {
        // this event is fired when we know that it is an audio file
        // we will add it to the library whether or not it has metadata.
        metadata = defaultMetaData;
      });
      parser.on('metadata', function(md) {
        metadata = md;
      });
      parser.on('done', function() {
        if (!metadata) {
          var ext = path.extname(localFile).toLowerCase();
          if (extraExtensions.indexOf(ext) >= 0) metadata = defaultMetaData;
        }
        if (metadata) {
          self.library[localFile] = {
            file: localFile,
            name: metadata.title.trim() || trackNameFromFile(filename),
            artist_name: (metadata.artist[0] || "").trim(),
            artist_disambiguation: "",
            album_artist_name: (metadata.albumartist[0] || "").trim(),
            album_name: metadata.album.trim(),
            track: metadata.track.no,
            time: null,
            year: parseInt(metadata.year, 10),
          };
        }
        stream.destroy();
        cb();
      });
    });
  });
  walker.on('end', function(){
    pend.wait(function(err) {
      if (err) {
        console.error("Error scanning:", err.stack);
        return;
      }
      var duration = Math.round(new Date() - start_time);
      console.log("library scan complete. " + duration + "ms.");
      self.scan_complete = true;
      self.emit('library', self.library);
    });
  });
}

function parseMaybeUndefNumber(n) {
  n = parseInt(n, 10);
  if (isNaN(n)) n = null;
  return n;
}

function maybeAddTrailingSlash(p) {
  if (p[p.length - 1] === path.sep) return p;
  return p + path.sep;
}

function ignoreFile(filename) {
  return (/^\./).test(path.basename(filename));
}

