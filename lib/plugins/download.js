var fs = require('fs');
var archiver = require('archiver');
var path = require('path');
var findit = require('findit');
var safePath = require('../safe_path');
var express = require('express');

module.exports = Download;

function Download(gb) {
  this.gb = gb;
  setup(this);
}

function setup(self) {
  var app = self.gb.app;
  var musicDir = self.gb.config.musicDirectory;
  app.use('/library', express.static(musicDir));
  app.get('/library/', function(req, resp) {
    self.downloadPath(musicDir, "library.zip", req, resp);
  });
  app.get(/^\/library\/(.*)\/$/, function(req, resp){
    var reqDir = req.params[0];
    var zipName = safePath(reqDir.replace(/\//g, " - ")) + ".zip";
    var dlPath = path.join(musicDir, reqDir);
    self.downloadPath(dlPath, zipName, req, resp);
  });
  app.post('/download/custom', express.urlencoded(), function(req, resp) {
    var reqKeys = req.body.key;
    if (!Array.isArray(reqKeys)) {
      reqKeys = [reqKeys];
    }
    var files = reqKeys.map(function(key) {
      var dbFile = self.gb.player.libraryIndex.trackTable[key];
      return dbFile && path.join(musicDir, dbFile.file);
    });
    var reqZipName = req.body.zipName || "music";
    var zipName = safePath(reqZipName.toString()) + ".zip";
    self.sendZipOfFiles(zipName, files, req, resp);
  });
}

Download.prototype.downloadPath = function(dlPath, zipName, req, resp){
  var self = this;
  var walker = findit(dlPath);
  var files = [];
  walker.on('file', function(file){
    files.push(file);
  });
  walker.on('error', function(err){
    walker.removeAllListeners();
    console.error("Error when downloading zip of", dlPath, err.stack);
    resp.statusCode = 500;
    resp.end();
  });
  walker.on('end', function(){
    self.sendZipOfFiles(zipName, files, req, resp);
  });
};

Download.prototype.sendZipOfFiles = function(zipName, files, req, resp) {
  var self = this;
  var cleanup = [];
  req.on('close', cleanupEverything);

  resp.setHeader("Content-Type", "application/zip");
  resp.setHeader("Content-Disposition", "attachment; filename=" + zipName);

  var archive = archiver('zip');
  archive.on('error', function(err) {
    console.log("Error while sending zip of files:", err.stack);
    cleanupEverything();
  });

  cleanup.push(function(){
    archive.destroy();
  });
  archive.pipe(resp);

  files.forEach(function(file) {
    var options = {
      name: path.relative(self.gb.config.musicDirectory, file),
    };
    var readStream = fs.createReadStream(file);
    cleanup.push(function() {
      readStream.destroy();
    });
    archive.append(readStream, options);
  });
  archive.finalize(function(err) {
    if (err) {
      console.error("Error finalizing zip:", err.stack);
      cleanupEverything();
    }
  });

  function cleanupEverything() {
    cleanup.forEach(function(fn) {
      try {
        fn();
      } catch(err) {}
    });
    resp.end();
  }
};
