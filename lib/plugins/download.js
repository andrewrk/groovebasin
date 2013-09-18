var fs = require('fs');
var zipstream = require('zipstream');
var path = require('path');
var safePath = require('../futils').safePath;
var express = require('express');
var walk = require('walkdir');

module.exports = Download;

function Download(gb) {
  var self = this;
  self.is_enabled = false;
  self.is_ready = false;

  console.error("TODO: fix download plugin (download plugin disabled)");
  return;
  bus.on('save_state', function(state){
    return state.status.download_enabled = self.is_enabled;
  });
  bus.on('restore_state', function(state){
    self.is_enabled = true;
    // TODO set music_directory based on config
    // self.music_directory = ??
    if (!self.music_directory) {
      self.is_enabled = false;
      console.warn("No music directory set. Download plugin disabled.");
    }
  });
  bus.on('app', function(app){
    app.use('/library', self.whenEnabled(express.static(self.music_directory)));
    app.get('/library/', self.checkEnabledMiddleware, function(req, resp){
      return self.downloadPath(self.music_directory, "library.zip", req, resp);
    });
    app.get(/^\/library\/(.*)\/$/, self.checkEnabledMiddleware, function(req, resp){
      var req_dir, zip_name, dl_path;
      req_dir = req.params[0];
      zip_name = safePath(req_dir.replace(/\//g, " - ")) + ".zip";
      dl_path = path.join(self.music_directory, req_dir);
      return self.downloadPath(dl_path, zip_name, req, resp);
    });
    app.post('/download/custom', [self.checkEnabledMiddleware, express.urlencoded()], function(req, resp){
      var res$, i$, ref$, len$, f, files, zip_name;
      res$ = [];
      for (i$ = 0, len$ = (ref$ = req.body.file).length; i$ < len$; ++i$) {
        f = ref$[i$];
        res$.push(path.join(self.music_directory, f));
      }
      files = res$;
      zip_name = "music.zip";
      return self.sendZipOfFiles(zip_name, files, req, resp);
    });
    app.get('/download/album/:album', self.checkEnabledMiddleware, function(req, resp){
      // TODO implement
    });
    return app.get('/download/artist/:artist', self.checkEnabledMiddleware,
        function(req, resp)
    {
      // TODO: implement
    });
  });
}

Download.prototype.downloadPath = function(dl_path, zip_name, req, resp){
  var self = this;
  var walker = walk(dl_path);
  var files = [];
  walker.on('file', function(file){
    files.push(file);
  });
  walker.on('error', function(err){
    walker.removeAllListeners('end');
    console.error("Error when downloading zip of", dl_path, err.stack);
    resp.statusCode = 404;
    resp.end();
  });
  walker.on('end', function(){
    self.sendZipOfFiles(zip_name, files, req, resp);
  });
};

Download.prototype.sendZipOfFiles = function(zip_name, files, req, resp){
  var self = this;
  var cleanup = [];
  req.on('close', function(){
    var i$, ref$, len$, fn;
    for (i$ = 0, len$ = (ref$ = cleanup).length; i$ < len$; ++i$) {
      fn = ref$[i$];
      try {
        fn();
      } catch (e$) {}
    }
    resp.end();
  });
  resp.setHeader("Content-Type", "application/zip");
  resp.setHeader("Content-Disposition", "attachment; filename=" + zip_name);
  var zip = zipstream.createZip({});
  cleanup.push(function(){
    zip.destroy();
  });
  zip.pipe(resp);
  function nextFile(){
    var file_path, options, read_stream;
    file_path = files.shift();
    if (file_path != null) {
      options = {
        name: path.relative(self.music_directory, file_path),
        store: true
      };
      read_stream = fs.createReadStream(file_path);
      cleanup.push(function(){
        read_stream.destroy();
      });
      zip.addFile(read_stream, options, nextFile);
    } else {
      zip.finalize(function(){
        resp.end();
      });
    }
  }
  nextFile();
};
