var ytdl = require('ytdl');
var url = require('url');

module.exports = YtDlPlugin;

// sorted from worst to best
var YTDL_AUDIO_ENCODINGS = [
  'mp3',
  'aac',
  'wma',
  'vorbis',
  'wav',
  'flac',
];

function YtDlPlugin(gb) {
  gb.player.importUrlFilters.push(this);
}

YtDlPlugin.prototype.importUrl = function(urlString, cb) {
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
  ytdl.getInfo(urlString, gotYouTubeInfo);

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
    var filename = info.title + '.' + bestFormat.container;
    cb(null, req, filename);

    function filter(format) {
      return format.audioBitrate === bestFormat.audioBitrate &&
        format.audioEncoding === bestFormat.audioEncoding;
    }
  }
};
