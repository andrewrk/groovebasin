var zfill = require('zfill');
var path = require('path');

exports.safePath = safePath;
exports.trackNameFromFile = trackNameFromFile;
exports.getSuggestedPath = getSuggestedPath;

function safePath(string){
  return string.replace(/[<>:"\/\\|?*%]/g, "_");
}

function trackNameFromFile(filename){
  var fileTitle = filename.substr(filename.lastIndexOf('/') + 1);
  var dot = fileTitle.lastIndexOf('.');
  var len = (dot >= 0) ? dot : fileTitle.length;
  return fileTitle.substring(0, len);
}

function getSuggestedPath(track, default_name){
  var p, t;
  p = "";
  if (track.album_artist_name) {
    p = path.join(p, safePath(track.album_artist_name));
  }
  if (track.album_name) {
    p = path.join(p, safePath(track.album_name));
  }
  t = "";
  if (track.track != null) {
    t += safePath(zfill(track.track)) + " ";
  }
  if (track.name === trackNameFromFile(track.file)) {
    t += safePath(default_name);
  } else {
    t += track.name + path.extname(track.file);
  }
  return path.join(p, t);
}
