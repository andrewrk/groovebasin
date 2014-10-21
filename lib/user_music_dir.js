var osenv = require('osenv');
var execFile = require('child_process').execFile;
var log = require('./log');
var path = require('path');

module.exports = getDefaultMusicDir;

function getDefaultMusicDir(cb) {
  execFile('xdg-user-dir', ['MUSIC'], function(err, stdout, stderr) {
    var result = stdout.trim();
    var fallbackValue = path.join(osenv.home(), "music");
    if (err) {
      log.debug("unable to execute xdg-user-dir: " + err.message);
      cb(null, fallbackValue);
      return;
    }
    if (!result || stderr) {
      log.debug("xdg-user-dir had error:", stderr);
      cb(null, fallbackValue);
      return;
    }
    cb(null, result);
  });
}
