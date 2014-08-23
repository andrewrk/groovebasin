var EventEmitter = require('events').EventEmitter;
var http = require('http');
var https = require('https');
var assert = require('assert');
var WebSocketServer = require('ws').Server;
var fs = require('fs');
var ZipStream = require('zip-stream');
var util = require('util');
var path = require('path');
var Pend = require('pend');
var express = require('express');
var osenv = require('osenv');
var spawn = require('child_process').spawn;
var plugins = [
  require('./plugins/ytdl'),
  require('./plugins/lastfm'),
];
var Player = require('./player');
var PlayerServer = require('./player_server');
var MpdProtocol = require('./mpd_protocol');
var MpdApiServer = require('./mpd_api_server');
var WebSocketApiClient = require('./web_socket_api_client');
var levelup = require('level-packager')(require('leveldown'));
var net = require('net');
var safePath = require('./safe_path');
var MultipartForm = require('multiparty').Form;
var createGzipStatic = require('connect-static');
var serveStatic = require('serve-static');
var bodyParser = require('body-parser');
var Cookies = require('cookies');

module.exports = GrooveBasin;

var defaultConfig = {
  host: '0.0.0.0',
  port: 16242,
  dbPath: "groovebasin.db",
  musicDirectory: path.join(osenv.home(), "music"),
  lastFmApiKey: "bb9b81026cd44fd086fa5533420ac9b4",
  lastFmApiSecret: "2309a40ae3e271de966bf320498a8f09",
  mpdHost: '0.0.0.0',
  mpdPort: 6600,
  acoustidAppKey: 'bgFvC4vW',
  encodeQueueDuration: 8,
  sslKey: 'certs/self-signed-key.pem',
  sslCert: 'certs/self-signed-cert.pem',
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
    if (process.argv.indexOf('--delete-all-users') > 0) {
      // this will call process.exit when done
      PlayerServer.deleteAllUsers(self.db);
      return;
    }


    self.player = new Player(self.db, self.config.musicDirectory, self.config.encodeQueueDuration);
    self.player.initialize(function(err) {
      if (err) {
        console.error("unable to initialize player:", err.stack);
        return;
      }
      console.info("Player initialization complete.");

      self.app.use(self.player.streamMiddleware.bind(self.player));

      var pend = new Pend();
      plugins.forEach(function(PluginClass) {
        var plugin = new PluginClass(self);
        if (plugin.initialize) pend.go(plugin.initialize.bind(plugin));
      });
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
  var serve = serveStatic(musicDir);
  self.app.use('/library', serveStaticMiddleware);
  self.app.get('/library/', self.hasPermRead, function(req, resp) {
    downloadPath("", "library.zip", req, resp);
  });
  self.app.get(/^\/library\/(.*)\/$/, self.hasPermRead, function(req, resp){
    var reqDir = req.params[0];
    var zipName = safePath(reqDir.replace(/\//g, " - ")) + ".zip";
    downloadPath(reqDir, zipName, req, resp);
  });
  var urlencodedMw = bodyParser.urlencoded({extended: false});
  self.app.post('/download/custom', self.hasPermRead, urlencodedMw, function(req, resp) {
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

    var archive = new ZipStream({store: true});
    archive.on('error', function(err) {
      console.error("Error while sending zip of files:", err.stack);
      cleanupEverything();
    });

    cleanup.push(function(){
      archive.destroy();
    });
    archive.pipe(resp);

    var fileIndex = 0;

    addOne();

    function addOne() {
      var file = files[fileIndex];
      if (!file) {
        finalize();
        return;
      }
      var options = {
        name: path.relative(self.config.musicDirectory, file),
      };
      var readStream = fs.createReadStream(file);
      readStream.on('error', function(err) {
        console.error("zip read stream error:", err.stack);
        cleanupEverything();
      });
      cleanup.push(function() {
        readStream.destroy();
      });
      archive.entry(readStream, options, function(err) {
        if (err) {
          console.error("Error adding zip entry:", err.stack);
          cleanupEverything();
          return;
        }
        fileIndex += 1;
        addOne();
      });
    }

    function finalize() {
      archive.finalize(function(err) {
        if (err) {
          console.error("Error finalizing zip:", err.stack);
          cleanupEverything();
        }
      });
    }

    function cleanupEverything() {
      cleanup.forEach(function(fn) {
        try {
          fn();
        } catch(err) {}
      });
      resp.end();
    }
  }

  function serveStaticMiddleware(req, res, next) {
    self.hasPermRead(req, res, function() {
      res.setHeader('Content-Disposition', 'attachment');
      serve(req, res, function(err) {
        res.removeHeader('Content-Disposition');
        next(err);
      });
    });
  }
};

GrooveBasin.prototype.initializeUpload = function() {
  var self = this;
  self.app.post('/upload', self.hasPermAdd, function(request, response, next) {
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

GrooveBasin.prototype.createHasPermMiddleware = function(permName) {
  var self = this;
  return function(req, resp, next) {
    var cookies = new Cookies(req, resp);
    var token = cookies.get('token');
    var client = self.playerServer.clients[token];
    var hasPermission = self.playerServer.userHasPerm(client && client.user, permName);
    if (hasPermission) {
      req.client = client;
      next();
    } else {
      resp.statusCode = 403;
      resp.end("not authorized");
    }
  };
};

GrooveBasin.prototype.startServer = function() {
  var self = this;

  assert.ok(self.httpServer == null);

  self.playerServer = new PlayerServer({
    player: self.player,
    db: self.db,
  });

  self.hasPermRead = self.createHasPermMiddleware('read');
  self.hasPermAdd = self.createHasPermMiddleware('add');
  self.mpdApiServer = new MpdApiServer(self.player);

  self.initializeDownload();
  self.initializeUpload();

  self.playerServer.init(function(err) {
    if (err) throw err;
    createHttpServer(attachWebSocketServer);
  });

  function createHttpServer(cb) {
    if (!self.config.sslKey || !self.config.sslCert) {
      console.warn("WARNING: SSL disabled, using HTTP.");
      self.httpServer = http.createServer(self.app);
      self.httpProtocol = 'http';
      cb();
    } else {
      if (self.config.sslKey === defaultConfig.sslKey ||
        self.config.sslCert === defaultConfig.sslCert)
      {
        console.warn("WARNING: Using public self-signed certificate. " +
            "For better security, provide your own self-signed certificate, " +
            "or better yet, one signed by a certificate authority.");
      }
      self.httpProtocol = 'https';
      readKeyAndCert(cb);
    }
    function readKeyAndCert(cb) {
      var pend = new Pend();
      var options =  {};
      pend.go(function(cb) {
        fs.readFile(self.config.sslKey, function(err, data) {
          if (err) {
            console.error("Unable to read SSL key file: " + err.message);
            process.exit(1);
            return;
          }
          options.key = data;
          cb();
        });
      });
      pend.go(function(cb) {
        fs.readFile(self.config.sslCert, function(err, data) {
          if (err) {
            console.error("Unable to read SSL cert file: " + err.message);
            process.exit(1);
            return;
          }
          options.cert = data;
          cb();
        });
      });
      pend.wait(function() {
        self.httpServer = https.createServer(options, self.app);
        cb();
      });
    }
  }

  function attachWebSocketServer() {
    self.wss = new WebSocketServer({
      server: self.httpServer,
      clientTracking: false,
    });
    self.wss.on('connection', function(ws) {
      self.playerServer.handleNewClient(new WebSocketApiClient(ws));
    });
    self.httpServer.listen(self.httpPort, self.httpHost, function() {
      self.emit('listening');
      console.info("Listening at " + self.httpProtocol + "://" + self.httpHost + ":" + self.httpPort + "/");
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
      self.protocolServer.on('error', function(err) {
        if (err.code === 'EADDRINUSE') {
          console.error("Failed to bind MPD protocol to port " + mpdPort +
            ": Address in use.");
        } else {
          throw err;
        }
      });
      self.protocolServer.listen(mpdPort, mpdHost, function() {
        console.info("MPD/GrooveBasin Protocol listening at " +
          mpdHost + ":" + mpdPort);
      });
    }
  }
};
