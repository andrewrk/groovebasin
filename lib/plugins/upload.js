var fs = require('fs');
var path = require('path');
var superagent = require('superagent');
var Pend = require('pend');
var url = require('url');
var temp = require('temp');
var express = require('express');
var multipart = express.multipart();

module.exports = Upload;

temp.track();

function Upload(gb) {
  this.gb = gb;

  this.gb.on('socketConnect', onSocketConnection.bind(this));
  setupRoutes(this, this.gb.app);
}

function onSocketConnection(socket) {
  var self = this;
  socket.on('ImportTrackUrl', function(urlString){
    var parsedUrl = url.parse(urlString);
    var remoteFilename = path.basename(parsedUrl.pathname);
    var req = superagent.get(urlString);
    var ws = temp.createWriteStream({suffix: path.extname(urlString)});
    req.pipe(ws);
    ws.on('close', function(){
      self.importFile(ws.path, remoteFilename, cleanAndLogIfErr);
    });
    ws.on('error', cleanAndLogIfErr);
    req.on('error', cleanAndLogIfErr);

    function cleanAndLogIfErr(err, relPath) {
      if (err) {
        console.error("Unable to import by URL.", err.stack, "URL:", urlString);
      } else {
        socket.emit('ImportTrackComplete', relPath);
      }
      temp.cleanup();
    }
  });
}

Upload.prototype.importFile = function(tempFile, remoteFilename, cb) {
  this.gb.player.importFile(tempFile, remoteFilename, function(err, relPath) {
    if (err) return cb(err);
    cb(null, relPath);
  });
};

function setupRoutes(self, app) {
  app.post('/upload', multipart, function(request, response) {
    var pend = new Pend();
    var completedFiles = [];
    for (var name in request.files) {
      var file = request.files[name];
      pend.go(makeImportFn(file.path, file.originalFilename));
    }

    pend.wait(function() {
      response.json({success: true, files: completedFiles});
    });

    function makeImportFn(filePath, originalFilename) {
      return function(cb) {
        self.importFile(filePath, originalFilename, function(err, relPath) {
          if (err) {
            console.error("Unable to import by uploading. Error:", err.stack);
          } else {
            completedFiles.push(relPath);
          }
          cb();
        });
      };
    }
  });
}
