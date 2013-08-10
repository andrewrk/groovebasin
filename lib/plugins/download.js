var Plugin, fs, zipstream, path, safePath, express, findit, Download;
Plugin = require('../plugin');
fs = require('fs');
zipstream = require('zipstream');
path = require('path');
safePath = require('../futils').safePath;
express = require('express');
findit = require('findit');
module.exports = Download = (function(superclass){
  Download.displayName = 'Download';
  var prototype = extend$(Download, superclass).prototype, constructor = Download;
  function Download(bus){
    var this$ = this instanceof ctor$ ? this : new ctor$;
    superclass.apply(this$, arguments);
    this$.is_enabled = false;
    this$.is_ready = false;
    bus.on('save_state', function(state){
      return state.status.download_enabled = this$.is_enabled;
    });
    bus.on('restore_state', function(state){
      this$.is_enabled = true;
      if ((this$.music_directory = state.mpd_conf.music_directory) == null) {
        this$.is_enabled = false;
        console.warn("No music directory set. Download plugin disabled.");
      }
    });
    bus.on('app', function(app){
      app.use('/library', this$.whenEnabled(express['static'](this$.music_directory)));
      app.get('/library/', this$.checkEnabledMiddleware, function(req, resp){
        return this$.downloadPath(this$.music_directory, "library.zip", req, resp);
      });
      app.get(/^\/library\/(.*)\/$/, this$.checkEnabledMiddleware, function(req, resp){
        var req_dir, zip_name, dl_path;
        req_dir = req.params[0];
        zip_name = safePath(req_dir.replace(/\//g, " - ")) + ".zip";
        dl_path = path.join(this$.music_directory, req_dir);
        return this$.downloadPath(dl_path, zip_name, req, resp);
      });
      app.post('/download/custom', [this$.checkEnabledMiddleware, express.urlencoded()], function(req, resp){
        var res$, i$, ref$, len$, f, files, zip_name;
        res$ = [];
        for (i$ = 0, len$ = (ref$ = req.body.file).length; i$ < len$; ++i$) {
          f = ref$[i$];
          res$.push(path.join(this$.music_directory, f));
        }
        files = res$;
        zip_name = "music.zip";
        return this$.sendZipOfFiles(zip_name, files, req, resp);
      });
      app.get('/download/album/:album', this$.checkEnabledMiddleware, function(req, resp){
        var album, res$, i$, ref$, len$, track, files, zip_name;
        album = this$.mpd.library.album_table[req.params.album];
        if (album == null) {
          resp.statusCode = 404;
          resp.end();
          return;
        }
        res$ = [];
        for (i$ = 0, len$ = (ref$ = album.tracks).length; i$ < len$; ++i$) {
          track = ref$[i$];
          res$.push(path.join(this$.music_directory, track.file));
        }
        files = res$;
        zip_name = safePath(album.name) + ".zip";
        return this$.sendZipOfFiles(zip_name, files, req, resp);
      });
      return app.get('/download/artist/:artist', this$.checkEnabledMiddleware, function(req, resp){
        var artist, zip_name, files, i$, ref$, len$, album, j$, ref1$, len1$, track;
        artist = this$.mpd.library.artist_table[req.params.artist];
        if (artist == null) {
          resp.statusCode = 404;
          resp.end();
          return;
        }
        zip_name = safePath(artist.name) + ".zip";
        files = [];
        for (i$ = 0, len$ = (ref$ = artist.albums).length; i$ < len$; ++i$) {
          album = ref$[i$];
          for (j$ = 0, len1$ = (ref1$ = album.tracks).length; j$ < len1$; ++j$) {
            track = ref1$[j$];
            files.push(path.join(this$.music_directory, track.file));
          }
        }
        return this$.sendZipOfFiles(zip_name, files, req, resp);
      });
    });
    bus.on('mpd', function(mpd){
      this$.mpd = mpd;
    });
    return this$;
  } function ctor$(){} ctor$.prototype = prototype;
  prototype.downloadPath = function(dl_path, zip_name, req, resp){
    var finder, files, this$ = this;
    finder = findit.find(dl_path);
    files = [];
    finder.on('file', function(file){
      files.push(file);
    });
    finder.on('error', function(err){
      finder.removeAllListeners('end');
      console.error("Error when downloading zip of", relative_path, err.stack);
      resp.statusCode = 404;
      resp.end();
    });
    finder.on('end', function(){
      this$.sendZipOfFiles(zip_name, files, req, resp);
    });
  };
  prototype.sendZipOfFiles = function(zip_name, files, req, resp){
    var cleanup, zip, this$ = this;
    cleanup = [];
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
    zip = zipstream.createZip({});
    cleanup.push(function(){
      zip.destroy();
    });
    zip.pipe(resp);
    function nextFile(){
      var file_path, options, read_stream;
      file_path = files.shift();
      if (file_path != null) {
        options = {
          name: path.relative(this$.music_directory, file_path),
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
  return Download;
}(Plugin));
function extend$(sub, sup){
  function fun(){} fun.prototype = (sub.superclass = sup).prototype;
  (sub.prototype = new fun).constructor = sub;
  if (typeof sup.extended == 'function') sup.extended(sub);
  return sub;
}