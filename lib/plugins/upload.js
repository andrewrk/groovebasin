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

function Upload(gb) {
  this.is_enabled = false;

  console.error("TODO: fix upload plugin (upload disabled)");
  return;
  bus.on('app', bind$(this, 'setUpRoutes'));
  bus.on('save_state', bind$(this, 'saveState'));
  bus.on('restore_state', bind$(this, 'restoreState'));
  bus.on('socket_connect', bind$(this, 'onSocketConnection'));
}
Upload.prototype.restoreState = function(state){
  var ref$;
  this.want_to_queue = (ref$ = state.want_to_queue) != null ? ref$ : [];
  this.is_enabled = true;
  // TODO set this.music_directory based on config
  // this.music_directory = ??
  if (!this.music_directory) {
    this.is_enabled = false;
    console.warn("No music directory set. Upload disabled.");
    return;
  }
};
Upload.prototype.saveState = function(state){
  state.want_to_queue = this.want_to_queue;
  state.status.upload_enabled = this.is_enabled;
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
  // TODO implement this
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
function bind$(obj, key){
  return function(){ return obj[key].apply(obj, arguments) };
}
