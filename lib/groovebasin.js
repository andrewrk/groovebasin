var EventEmitter = require('events').EventEmitter;
var http = require('http');
var assert = require('assert');
var WebSocketServer = require('ws').Server;
var fs = require('fs');
var archiver = require('archiver');
var util = require('util');
var path = require('path');
var Pend = require('pend');
var express = require('express');
var osenv = require('osenv');
var spawn = require('child_process').spawn;
var requireIndex = require('requireindex');
var plugins = requireIndex(path.join(__dirname, 'plugins'));
var Player = require('./player');
var PlayerServer = require('./player_server');
var MpdProtocol = require('./mpd_protocol');
var MpdApiServer = require('./mpd_api_server');
var WebSocketApiClient = require('./web_socket_api_client');
var levelup = require('level');
var crypto = require('crypto');
var net = require('net');
var safePath = require('./safe_path');
var MultipartForm = require('multiparty').Form;
var createGzipStatic = require('connect-static');
var serveStatic = require('serve-static');
var bodyParser = require('body-parser');

module.exports = GrooveBasin;

var defaultConfig = {
  host: '0.0.0.0',
  port: 16242,
  dbPath: "groovebasin.db",
  musicDirectory: path.join(osenv.home(), "music"),
  permissions: {},
  defaultPermissions: {
    read: true,
    add: true,
    control: true,
  },
  lastFmApiKey: "7d831eff492e6de5be8abb736882c44d",
  lastFmApiSecret: "8713e8e893c5264608e584a232dd10a0",
  mpdHost: '0.0.0.0',
  mpdPort: 6600,
  acoustidAppKey: 'bgFvC4vW',
};

defaultConfig.permissions[genPassword()] = {
  admin: true,
  read: true,
  add: true,
  control: true,
};

util.inherits(GrooveBasin, EventEmitter);
function GrooveBasin() {
  EventEmitter.call(this);

  this.app = express();
}

GrooveBasin.prototype.initConfigVar = function(name, defaultValue) {
  this.configVars.push(name);
  this[name] = defaultValue;
};

GrooveBasin.prototype.loadConfig = function(cb) {
  var self = this;
  var pathToConfig = "config.js";
  fs.readFile(pathToConfig, {encoding: 'utf8'}, function(err, contents) {
    var anythingAdded = false;
    var config;
    if (err) {
      if (err.code === 'ENOENT') {
        anythingAdded = true;
        self.config = defaultConfig;
        console.warn("No config.js found; writing default.");
      } else {
        return cb(err);
      }
    } else {
      try {
        self.config = JSON.parse(contents);
      } catch (err) {
        cb(err);
      }
    }
    // this ensures that even old files get new config values when we add them
    for (var key in defaultConfig) {
      if (self.config[key] === undefined) {
        anythingAdded = true;
        self.config[key] = defaultConfig[key];
      }
    }
    if (anythingAdded) {
      fs.writeFile(pathToConfig, JSON.stringify(self.config, null, 4), cb);
    } else {
      cb();
    }
  });
};

GrooveBasin.prototype.start = function() {
  var self = this;

  var pend = new Pend();
  pend.go(function(cb) {
    self.loadConfig(cb);
  });
  pend.go(function(cb) {
    var options = {
      dir: path.join(__dirname, "../public"),
      aliases: [],
    };
    createGzipStatic(options, function(err, middleware) {
      if (err) return cb(err);
      self.app.use(middleware);
      cb();
    });
  });
  pend.go(function(cb) {
    createGzipStatic({dir: path.join(__dirname, "../src/public")}, function(err, middleware) {
      if (err) return cb(err);
      self.app.use(middleware);
      cb();
    });
  });
  pend.wait(function(err) {
    if (err) {
      console.error(err.stack);
      return;
    }

    self.httpHost = self.config.host;
    self.httpPort = self.config.port;
    self.db = levelup(self.config.dbPath);

    self.initializeDownload();
    self.initializeUpload();

    self.player = new Player(self.db, self.config.musicDirectory);
    self.player.initialize(function(err) {
      if (err) {
        console.error("unable to initialize player:", err.stack);
        return;
      }
      console.info("Player initialization complete.");

      self.app.use(self.player.streamMiddleware.bind(self.player));

      var pend = new Pend();
      for (var pluginName in plugins) {
        var PluginClass = plugins[pluginName];
        var plugin = new PluginClass(self);
        if (plugin.initialize) pend.go(plugin.initialize.bind(plugin));
      }
      pend.wait(function(err) {
        if (err) {
          console.error("Error initializing plugin:", err.stack);
          return;
        }
        self.startServer();
      });
    });
  });
};

GrooveBasin.prototype.initializeDownload = function() {
  var self = this;
  var musicDir = self.config.musicDirectory;
  self.app.use('/library', serveStatic(musicDir));
  self.app.get('/library/', function(req, resp) {
    downloadPath("", "library.zip", req, resp);
  });
  self.app.get(/^\/library\/(.*)\/$/, function(req, resp){
    var reqDir = req.params[0];
    var zipName = safePath(reqDir.replace(/\//g, " - ")) + ".zip";
    downloadPath(reqDir, zipName, req, resp);
  });
  self.app.post('/download/custom', bodyParser(), function(req, resp) {
    var reqKeys = req.body.key;
    if (!Array.isArray(reqKeys)) {
      reqKeys = [reqKeys];
    }
    var files = [];
    for (var i = 0; i < reqKeys.length; i += 1) {
      var key = reqKeys[i];
      var dbFile = self.player.libraryIndex.trackTable[key];
      if (dbFile) files.push(path.join(musicDir, dbFile.file));
    }
    var reqZipName = (req.body.zipName || "music").toString();
    var zipName = safePath(reqZipName) + ".zip";
    sendZipOfFiles(zipName, files, req, resp);
  });

  function downloadPath(dirName, zipName, req, resp) {
    var files = [];
    var dirEntry = self.player.dirs[dirName];
    if (!dirEntry) {
      resp.statusCode = 404;
      resp.end("Not found");
      return;
    }
    sendZipOfFiles(zipName, files, req, resp);

    function addOneDir(dirEntry) {
      var baseName, relPath;
      for (baseName in dirEntry.entries) {
        relPath = path.join(dirEntry.dirName, baseName);
        var dbTrack = self.player.dbFilesByPath[relPath];
        if (dbTrack) files.push(dbTrack.file);
      }
      for (baseName in dirEntry.dirEntries) {
        relPath = path.join(dirEntry.dirName, baseName);
        var childEntry = self.player.dirs[relPath];
        if (childEntry) addOneDir(childEntry);
      }
    }
  }

  function sendZipOfFiles(zipName, files, req, resp) {
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
        name: path.relative(self.config.musicDirectory, file),
      };
      var readStream = fs.createReadStream(file);
      readStream.on('error', function(err) {
        console.error("zip read stream error:", err.stack);
      });
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
  }
};

GrooveBasin.prototype.initializeUpload = function() {
  var self = this;
  self.app.post('/upload', function(request, response, next) {
    var form = new MultipartForm();
    form.parse(request, function(err, fields, files) {
      if (err) return next(err);

      var keys = [];
      var pend = new Pend();
      for (var key in files) {
        var arr = files[key];
        for (var i = 0; i < arr.length; i += 1) {
          var file = arr[i];
          pend.go(makeImportFn(file));
        }
      }
      pend.wait(function() {
        response.json(keys);
      });

      function makeImportFn(file) {
        return function(cb) {
          self.player.importFile(file.path, file.originalFilename, function(err, dbFile) {
            if (err) {
              console.error("Unable to import file:", file.path, "error:", err.stack);
            } else if (!dbFile) {
              console.error("Unable to locate new file due to race condition");
            } else {
              keys.push(dbFile.key);
            }
            cb();
          });
        };
      }
    });
  });
};

GrooveBasin.prototype.startServer = function() {
  var self = this;

  assert.ok(self.httpServer == null);

  self.playerServer = new PlayerServer({
    player: self.player,
    authenticate: authenticate,
    defaultPermissions: self.config.defaultPermissions,
  });
  self.mpdApiServer = new MpdApiServer(self.player);

  self.httpServer = http.createServer(self.app);
  self.wss = new WebSocketServer({
    server: self.httpServer,
    clientTracking: false,
  });
  self.wss.on('connection', function(ws) {
    self.playerServer.handleNewClient(new WebSocketApiClient(ws));
  });
  self.httpServer.listen(self.httpPort, self.httpHost, function() {
    self.emit('listening');
    console.info("Listening at http://" + self.httpHost + ":" + self.httpPort + "/");
  });
  self.httpServer.on('close', function() {
    console.info("server closed");
  });
  var mpdPort = self.config.mpdPort;
  var mpdHost = self.config.mpdHost;
  if (mpdPort == null || mpdHost == null) {
    console.info("MPD Protocol disabled");
  } else {
    self.protocolServer = net.createServer(function(socket) {
      socket.setEncoding('utf8');
      var protocol = new MpdProtocol({
        player: self.player,
        playerServer: self.playerServer,
        apiServer: self.mpdApiServer,
        authenticate: authenticate,
        permissions: self.config.defaultPermissions,
      });
      protocol.on('error', handleError);
      socket.on('error', handleError);
      socket.pipe(protocol).pipe(socket);
      socket.on('close', cleanup);
      self.mpdApiServer.handleNewClient(protocol);

      function handleError(err) {
        console.error("socket error:", err.stack);
        socket.destroy();
        cleanup();
      }
      function cleanup() {
        self.mpdApiServer.handleClientEnd(protocol);
      }
    });
    self.protocolServer.listen(mpdPort, mpdHost, function() {
      console.info("MPD/GrooveBasin Protocol listening at " +
        mpdHost + ":" + mpdPort);
    });
  }

  function authenticate(password) {
    return self.config.permissions[password];
  }
};

function genPassword() {
  return crypto.pseudoRandomBytes(9).toString('base64');
}
