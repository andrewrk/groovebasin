var zlib = require('zlib');
var fs = require('fs');
var stream = require('stream');
var util = require('util');
var path = require('path');
var Pend = require('pend');
var findit = require('findit');

module.exports = createGzipStaticMiddleware;

function createGzipStaticMiddleware(dir, aliases, cb) {
  var cache = {};
  var pend = new Pend();
  var walker = findit(dir);
  walker.on('file', function(file) {
    if (ignoreFile(file)) return;
    var relName = '/' + path.relative(dir, file);
    var sink = new Sink();
    var inStream = fs.createReadStream(file);
    inStream.on('error', function(err) {
      if (err.code === 'EISDIR') {
        delete cache[relName];
        return;
      } else {
        throw err;
      }
    });
    cache[relName] = sink;
    pend.go(function(cb) {
      inStream.pipe(zlib.createGzip()).pipe(sink);
      sink.once('finish', cb);
    });
  });
  walker.on('end', function() {
    pend.wait(function(err) {
      if (err) return cb(err);
      aliases.forEach(function(alias) {
        cache[alias[0]] = cache[alias[1]];
      });
      cb(null, middleware);
    });
    function middleware(req, resp, next) {
      var sink = cache[req.url];
      if (!sink) return next();
      if (req.headers['accept-encoding'] == null) {
        console.log("sent raw");
        sink.createReadStream().pipe(zlib.createGunzip()).pipe(resp);
      } else {
        console.log("sent gzipped");
        resp.setHeader('content-encoding', 'gzip');
        sink.createReadStream().pipe(resp);
      }
    }
  });
}

util.inherits(Sink, stream.Writable);
function Sink(options) {
  stream.Writable.call(this, options);
  this.buffer = [];
}

Sink.prototype._write = function(chunk, encoding, callback) {
  this.buffer.push(chunk);
  callback();
};

Sink.prototype.createReadStream = function(options) {
  var s = new stream.Readable(options);
  s.buffer = this.buffer;
  s._read = function(size) {
    for (var i = 0; i < s.buffer.length; i += 1) {
      s.push(s.buffer[i]);
    }
    s.push(null);
  };
  return s;
};

function ignoreFile(file) {
  var basename = path.basename(file);
  return /^\./.test(basename) || /~$/.test(file);
}
