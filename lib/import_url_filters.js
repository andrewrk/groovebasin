var ytdl = require('ytdl-core');
var url = require('url');
var log = require('./log');
var path = require('path');
var download = require('./download').download;
var parseContentDisposition = require('content-disposition').parse;

// sorted from worst to best
var YTDL_AUDIO_ENCODINGS = [
  'mp3',
  'aac',
  'wma',
  'vorbis',
  'wav',
  'flac',
];

module.exports = [
  {
    name: "YouTube Download",
    fn: ytdlImportUrl,
  },
  {
    name: "Raw Download",
    fn: downloadRawImportUrl,
  },
];

function ytdlImportUrl(urlString, cb) {
  var parsedUrl = url.parse(urlString);

  var isYouTube = (parsedUrl.pathname === '/watch' &&
    (parsedUrl.hostname === 'youtube.com' ||
     parsedUrl.hostname === 'www.youtube.com' ||
     parsedUrl.hostname === 'm.youtube.com')) ||
    parsedUrl.hostname === 'youtu.be' ||
    parsedUrl.hostname === 'www.youtu.be';

  if (!isYouTube) {
    cb();
    return;
  }

  var bestFormat = null;
  try {
    ytdl.getInfo(urlString, {downloadURL: true}, gotYouTubeInfo);
  } catch (e) {
    // Error: Video id (blah) does not match expected format (/^[a-zA-Z0-9-_]{11}$/)
    cb();
    return;
  }

  function gotYouTubeInfo(err, info) {
    if (err) return cb(err);
    if (info.requires_purchase) {
      cb(new Error("this YouTube video requires purchase"));
      return;
    }
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
    if (!bestFormat) {
      cb(new Error("YouTube response contained no audio formats"));
      return;
    }
    if (YTDL_AUDIO_ENCODINGS.indexOf(bestFormat.audioEncoding) === -1) {
      log.warn("YouTube Import: unrecognized audio format:", bestFormat.audioEncoding);
    }
    var req = ytdl.downloadFromInfo(info, {format: bestFormat});
    var filenameHintWithoutPath = info.title + '.' + bestFormat.container;
    var callbackCalled = false;
    req.on('error', onError);
    req.on('response', function(res) {
      req.removeListener('error', onError);
      if (callbackCalled) return;
      callbackCalled = true;
      var sizeInBytes = res.headers['content-length'];
      cb(null, req, filenameHintWithoutPath, sizeInBytes);
    });

    function onError(err) {
      if (callbackCalled) return;
      callbackCalled = true;
      cb(err);
    }
  }
}

function downloadRawImportUrl(urlString, cb) {
  var parsedUrl = url.parse(urlString);
  var remoteFilename = path.basename(parsedUrl.pathname);
  var decodedFilename;
  try {
    decodedFilename = decodeURI(remoteFilename);
  } catch (err) {
    decodedFilename = remoteFilename;
  }
  download(urlString, function(err, resp) {
    if (err) return cb(err);
    var contentDisposition = resp.headers['content-disposition'];
    if (contentDisposition) {
      var filename;
      try {
        filename = parseContentDisposition(contentDisposition).parameters.filename;
      } catch (err) {
        // do nothing
      }
      if (filename) {
        decodedFilename = filename;
      }
    }
    var contentLength = parseInt(resp.headers['content-length'], 10);
    cb(null, resp, decodedFilename, contentLength);
  });
}
