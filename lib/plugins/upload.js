var Plugin = require('../plugin');
var util = require('util');
var mkdirp = require('mkdirp');
var fs = require('fs');
var path = require('path');
var request = require('superagent');
var url = require('url');
var temp = require('temp');
var mv = require('mv');
var futils = require('../futils');
var getSuggestedPath = futils.getSuggestedPath;
var safePath = futils.safePath;
var express = require('express');
var multipart = express.multipart({
  keepExtensions: true
});

module.exports = Upload;

util.inherits(Upload, Plugin);
function Upload(bus) {
  Plugin.call(this);
  this.is_enabled = false;
  bus.on('app', bind$(this, 'setUpRoutes'));
  bus.on('mpd', bind$(this, 'setMpd'));
  bus.on('save_state', bind$(this, 'saveState'));
  bus.on('restore_state', bind$(this, 'restoreState'));
  bus.on('socket_connect', bind$(this, 'onSocketConnection'));
}
Upload.prototype.restoreState = function(state){
  var ref$;
  this.want_to_queue = (ref$ = state.want_to_queue) != null ? ref$ : [];
  this.is_enabled = true;
  if ((this.music_directory = state.mpd_conf.music_directory) == null) {
    this.is_enabled = false;
    console.warn("No music directory set. Upload disabled.");
    return;
  }
};
Upload.prototype.saveState = function(state){
  state.want_to_queue = this.want_to_queue;
  state.status.upload_enabled = this.is_enabled;
};
Upload.prototype.setMpd = function(mpd){
  this.mpd = mpd;
  this.mpd.on('libraryupdate', bind$(this, 'flushWantToQueue'));
};
Upload.prototype.onSocketConnection = function(socket){
  var this$ = this;
  socket.on('ImportTrackUrl', function(url_string){
    var parsed_url, remote_filename, temp_file, cleanUp, cleanAndLogIfErr, req, ws;
    parsed_url = url.parse(url_string);
    remote_filename = path.basename(parsed_url.pathname);
    temp_file = temp.path();
    cleanUp = function(){
      fs.unlink(temp_file);
    };
    cleanAndLogIfErr = function(err){
      if (err) {
        console.error("Unable to import by URL.", err.stack, "URL:", url_string);
      }
      cleanUp();
    };
    req = request.get(url_string);
    ws = fs.createWriteStream(temp_file);
    req.pipe(ws);
    ws.on('close', function(){
      this$.importFile(temp_file, remote_filename, cleanAndLogIfErr);
    });
    ws.on('error', cleanAndLogIfErr);
    req.on('error', cleanAndLogIfErr);
  });
};
Upload.prototype.importFile = function(temp_file, remote_filename, cb){
  var this$ = this;
  if (cb == null) cb = function(){};
  this.mpd.getFileInfo("file://" + temp_file, function(err, track){
    var suggested_path, relative_path, dest;
    if (err) {
      console.warn("Unable to read tags to get a suggested upload path: " + err.stack);
      suggested_path = safePath(remote_filename);
    } else {
      suggested_path = getSuggestedPath(track, remote_filename);
    }
    relative_path = path.join('incoming', suggested_path);
    dest = path.join(this$.music_directory, relative_path);
    mkdirp(path.dirname(dest), function(err){
      if (err) {
        console.error(err);
        return cb(err);
      }
      mv(temp_file, dest, function(err){
        this$.want_to_queue.push(relative_path);
        this$.emit('state_changed');
        console.info("Track was uploaded: " + dest);
        cb(err);
      });
    });
  });
};
Upload.prototype.setUpRoutes = function(app){
  var this$ = this;
  app.post('/upload', [this.checkEnabledMiddleware, multipart], function(request, response){
    var name, ref$, file;
    function logIfErr(err){
      if (err) {
        console.error("Unable to import by uploading. Error: " + err);
      }
    }
    for (name in (ref$ = request.files)) {
      file = ref$[name];
      this$.importFile(file.path, file.name, logIfErr);
    }
    response.statusCode = 200;
    response.setHeader('content-type', 'application/json');
    response.end(JSON.stringify({
      success: true
    }));
  });
};
Upload.prototype.flushWantToQueue = function(){
  var i, files, file;
  i = 0;
  files = [];
  while (i < this.want_to_queue.length) {
    file = this.want_to_queue[i];
    if (this.mpd.library.track_table[file] != null) {
      files.push(file);
      this.want_to_queue.splice(i, 1);
    } else {
      i++;
    }
  }
  this.mpd.queueFiles(files);
  this.mpd.queueFilesInStoredPlaylist(files, "Incoming");
  if (files.length) {
    this.emit('state_changed');
  }
};
function bind$(obj, key){
  return function(){ return obj[key].apply(obj, arguments) };
}
