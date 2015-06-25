#!/usr/bin/env node

var http = require('http');
var httpolyglot = require('httpolyglot');
var assert = require('assert');
var yawl = require('yawl');
var fs = require('fs');
var yazl = require('yazl');
var path = require('path');
var spawn = require('child_process').spawn;
var Pend = require('pend');
var express = require('express');
var Player = require('./player');
var PlayerServer = require('./player_server');
var MpdProtocol = require('./mpd_protocol');
var MpdApiServer = require('./mpd_api_server');
var WebSocketApiClient = require('./web_socket_api_client');
var leveldown = require('leveldown');
var net = require('net');
var safePath = require('./safe_path');
var MultipartForm = require('multiparty').Form;
var createGzipStatic = require('connect-static');
var serveStatic = require('serve-static');
var Cookies = require('cookies');
var log = require('./log');
var contentDisposition = require('content-disposition');
var getDefaultMusicDir = require('./user_music_dir');

var defaultConfig = {
  host: '127.0.0.1',
  port: 16242,
  dbPath: "groovebasin.db",
  musicDirectory: null, // will be filled in asynchronously later
  lastFmApiKey: "bb9b81026cd44fd086fa5533420ac9b4",
  lastFmApiSecret: "2309a40ae3e271de966bf320498a8f09",
  mpdHost: '0.0.0.0',
  mpdPort: 6600,
  acoustidAppKey: 'bgFvC4vW',
  encodeQueueDuration: 8,
  encodeBitRate: 256,
  sslKey: null,
  sslCert: null,
  sslCaDir: null,
  googleApiKey: "AIzaSyDdTDD8-gu_kp7dXtT-53xKcVbrboNAkpM",
};

main();

function main() {
  var pathToConfig = "config.json";
  var deleteAllUsers = false;
  var deleteAllEvents = false;
  var dumpUsers = false;
  var browserExe = null;
  var browserArgs = null;
  var printUrl = false;
  var exitGracefullyIfRunning = false;
  log.level = log.levels.Warn;
  for (var i = 2; i < process.argv.length; i += 1) {
    var arg = process.argv[i];
    if (/^--/.test(arg)) {
      if (arg === '--verbose') {
        log.level = log.levels.Debug;
      } else if (arg === '--delete-all-users') {
        deleteAllUsers = true;
      } else if (arg === '--delete-all-events') {
        deleteAllEvents = true;
      } else if (arg === '--dump-users') {
        dumpUsers = true;
      } else if (arg === '--print-url') {
        printUrl = true;
      } else if (arg === '--start') {
        exitGracefullyIfRunning = true;
      } else if (i + 1 < process.argv.length) {
        var argVal = process.argv[++i];
        if (arg === '--config') {
          pathToConfig = argVal;
        } else if (arg === '--spawn') {
          browserExe = argVal;
          browserArgs = process.argv.slice(++i);
          break;
        }
      } else {
        printUsageAndExit();
      }
    } else {
      printUsageAndExit();
    }
  }
  Player.setGrooveLoggingLevel();

  getDefaultMusicDir(function(err, defaultMusicDir) {
    if (err) throw err;
    defaultConfig.musicDirectory = defaultMusicDir;
    loadConfig(pathToConfig, function(err, config) {
      if (err) throw err;

      if (browserExe || printUrl) {
        spawnBrowserOrPrintUrl(config, browserExe, browserArgs);
        return;
      }

      var dbFilePath = path.resolve(path.dirname(pathToConfig), config.dbPath);
      var db = leveldown(dbFilePath);
      db.open(function(err) {
        if (err) {
          if (exitGracefullyIfRunning &&
            /^IO error: lock.*: Resource temporarily unavailable$/.test(err.message))
          {
            return;
          } else {
            throw err;
          }
        }

        if (deleteAllUsers || deleteAllEvents) {
          // this will call process.exit when done
          PlayerServer.deleteUsersAndEvents(db, deleteAllUsers);
          return;
        }
        if (dumpUsers) {
          // this will call process.exit when done
          PlayerServer.dumpUsers(db);
          return;
        }

        var app = express();
        var sslEnabled = !!(config.sslKey && config.sslCert);
        if (sslEnabled) {
          app.use(redirectHttpToHttps);
        }

        var pend = new Pend();
        pend.go(function(cb) {
          var options = {
            dir: path.join(__dirname, "../public"),
            aliases: [],
          };
          createGzipStatic(options, function(err, middleware) {
            if (err) return cb(err);
            app.use(middleware);
            cb();
          });
        });
        pend.go(function(cb) {
          createGzipStatic({dir: path.join(__dirname, "../src/public")}, function(err, middleware) {
            if (err) return cb(err);
            app.use(middleware);
            cb();
          });
        });
        pend.wait(function(err) {
          if (err) throw err;

          var player = new Player(db, config.musicDirectory,
            config.encodeQueueDuration, config.googleApiKey, config.encodeBitRate);
          player.initialize(function(err) {
            if (err) throw err;
            log.debug("Player initialization complete.");

            startServer(app, db, player, config, sslEnabled);
          });
        });
      });
    });
  });
}

function spawnBrowserOrPrintUrl(config, browserExe, browserArgs) {
  var sslEnabled = !!(config.sslKey && config.sslCert);
  var httpProtocol = sslEnabled ? 'https' : 'http';
  var theUrl = httpProtocol + "://" + config.host + ":" + config.port + "/";
  browserArgs = browserArgs.map(function(arg) {
    return arg.replace(/(%%?)/g, function(x) {
      return (x.length === 2) ? "%" : theUrl;
    });
  });
  if (browserExe) {
    console.error("spawning", browserExe, browserArgs);
    spawn(browserExe, browserArgs, {detached: true});
  } else {
    console.log(theUrl);
  }
}

function loadConfig(pathToConfig, cb) {
  fs.readFile(pathToConfig, {encoding: 'utf8'}, function(err, contents) {
    var brandNewConfig = false;
    var config;
    if (err) {
      if (err.code === 'ENOENT') {
        brandNewConfig = true;
        config = defaultConfig;
      } else {
        console.error("Unable to read " + pathToConfig + ": " + err.message);
        process.exit(1);
      }
    } else {
      try {
        config = JSON.parse(contents);
      } catch (err) {
        console.error("Unable to parse " + pathToConfig + ": " + err.message);
        process.exit(1);
      }
    }
    // this ensures that even old files get new config values when we add them
    var extraFields = [];
    var missingFields = [];
    var key;
    for (key in defaultConfig) {
      if (config[key] === undefined) {
        missingFields.push(key);
        config[key] = defaultConfig[key];
      }
    }
    for (key in config) {
      if (defaultConfig[key] === undefined) {
        extraFields.push(key);
      }
    }
    if (missingFields.length > 0 || brandNewConfig) {
      fs.writeFile(pathToConfig, JSON.stringify(config, null, 4), printMsgAndExit);
    } else if (extraFields.length > 0) {
      printExtraFieldsMsgAndExit();
    } else {
      cb(null, config);
    }

    function printExtraFieldsMsgAndExit() {
      console.error(pathToConfig + " contains the following unrecognized config options:\n" +
          extraFields.join(", ") + "\n" +
          "Remove them from the config file and then start Groove Basin again.");
      process.exit(1);
    }

    function printMsgAndExit(err) {
      if (err) throw err;
      if (brandNewConfig) {
        console.error("No " + pathToConfig + " found; writing default.");
      } else {
        console.error("Added missing fields to " + pathToConfig + ":\n" +
            missingFields.join(", "));
      }
      console.error(
       "Take a peek and make sure the values are to your liking,\n" +
       "then start Groove Basin again.");
      process.exit(1);
    }
  });
}

function initializeDownload(app, player, config, hasPermRead) {
  var musicDir = config.musicDirectory;
  var serve = serveStatic(musicDir);
  app.use('/library', serveStaticMiddleware);
  app.get('/library/', hasPermRead, function(req, resp) {
    downloadPath("", "library.zip", req, resp);
  });
  app.get(/^\/library\/(.*)\/$/, hasPermRead, function(req, resp){
    var reqDir = req.params[0];
    var zipName = safePath(reqDir.replace(/\//g, " - ")) + ".zip";
    downloadPath(reqDir, zipName, req, resp);
  });
  app.get('/download/keys', hasPermRead, function(req, resp) {
    var reqKeys = Object.keys(req.query);
    var files = [];
    var commonArtistName, commonAlbumName;
    reqKeys.forEach(function(key) {
      var dbFile = player.libraryIndex.trackTable[key];
      if (!dbFile) return;
      files.push(path.join(musicDir, dbFile.file));
      if (commonAlbumName === undefined) commonAlbumName = dbFile.albumName || "";
      else if (commonAlbumName !== dbFile.albumName) commonAlbumName = null;
      if (commonArtistName === undefined) commonArtistName = dbFile.artistName || "";
      else if (commonArtistName !== dbFile.artistName) commonArtistName = null;
    });
    var reqZipName = commonAlbumName || commonArtistName || "songs";
    var zipName = safePath(reqZipName) + ".zip";
    sendZipOfFiles(zipName, files, req, resp);
  });

  function downloadPath(dirName, zipName, req, resp) {
    var files = [];
    var dirEntry = player.dirs[dirName];
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
        var dbTrack = player.dbFilesByPath[relPath];
        if (dbTrack) files.push(dbTrack.file);
      }
      for (baseName in dirEntry.dirEntries) {
        relPath = path.join(dirEntry.dirName, baseName);
        var childEntry = player.dirs[relPath];
        if (childEntry) addOneDir(childEntry);
      }
    }
  }

  function sendZipOfFiles(zipName, files, req, resp) {
    req.on('close', cleanupEverything);

    resp.setHeader("Content-Type", "application/zip");
    resp.setHeader("Content-Disposition", contentDisposition(zipName, {type: "attachment"}));

    var zipfile = new yazl.ZipFile();
    zipfile.on('error', function(err) {
      log.error("Error while sending zip of files:", err.stack);
      cleanupEverything();
    });

    files.forEach(function(file) {
      zipfile.addFile(file, path.relative(config.musicDirectory, file), {compress: false});
    });
    zipfile.end(function(finalSize) {
      resp.setHeader("Content-Length", finalSize.toString());
      zipfile.outputStream.pipe(resp);
    });

    function cleanupEverything() {
      resp.end();
    }
  }

  function serveStaticMiddleware(req, res, next) {
    hasPermRead(req, res, function() {
      res.setHeader('Content-Disposition', 'attachment');
      serve(req, res, function(err) {
        res.removeHeader('Content-Disposition');
        next(err);
      });
    });
  }
}

function initializeUpload(app, player, playerServer, hasPermAdd) {
  app.post('/upload', hasPermAdd, function(request, response, next) {
    var form = new MultipartForm({
      maxFields: 100000, // let them eat cake
      autoFields: true,
    });
    var allDbFiles = [];
    var pend = new Pend();
    var autoQueue = false;
    var size;

    form.on('error', next);
    form.on('part', function(part) {
      pend.go(function(cb) {
        log.debug("import part", part.filename);
        player.importStream(part, part.filename, size, function(err, dbFiles) {
          if (err) {
            log.error("Unable to import stream:", err.stack);
          } else if (!dbFiles) {
            log.warn("Unable to import stream, unrecognized format");
          } else {
            allDbFiles = allDbFiles.concat(dbFiles);
          }
          log.debug("done importing part", part.filename);
          cb();
        });
      });
    });
    form.on('field', function(name, value) {
      if (name === 'autoQueue') {
        autoQueue = true;
      } else if (name === 'size') {
        size = parseInt(value, 10);
      }
    });
    form.on('close', function() {
      pend.wait(function() {
        if (allDbFiles.length > 0) {
          playerServer.handleImportedTracks(request.client, allDbFiles, autoQueue);
        }
        response.json({});
      });
    });
    form.parse(request);
  });
}

function initializeStream(app, player, hasPermRead) {
  app.get('/stream.mp3', hasPermRead, function(req, resp, next) {
    resp.setHeader('Content-Type', 'audio/mpeg');
    resp.setHeader('Cache-Control', 'no-cache, no-store, must-revalidate');
    resp.setHeader('Pragma', 'no-cache');
    resp.setHeader('Expires', '0');
    resp.statusCode = 200;

    player.startStreaming(resp);

    req.on('close', function() {
      player.stopStreaming(resp);
      resp.end();
    });
  });
}

function createHasPermMiddleware(playerServer, permName) {
  return function(req, resp, next) {
    var cookies = new Cookies(req, resp);
    var token = cookies.get('token');
    var client = playerServer.clients[token];
    var hasPermission = playerServer.userHasPerm(client && client.user, permName);
    if (hasPermission) {
      req.client = client;
      resp.client = client;
      next();
    } else {
      resp.statusCode = 403;
      resp.end("not authorized");
    }
  };
}

function redirectHttpToHttps(req, resp, next) {
  if (req.socket.encrypted) return next();
  resp.writeHead(301, {
    Location: "https://" + req.headers.host + req.url,
    Connection: "close",
  });
  resp.end();
}

function startServer(app, db, player, config, sslEnabled) {
  var playerServer = new PlayerServer({
    config: config,
    player: player,
    db: db,
  });

  var hasPermRead = createHasPermMiddleware(playerServer, 'read');
  var hasPermAdd = createHasPermMiddleware(playerServer, 'add');
  var mpdApiServer = new MpdApiServer(player);

  initializeDownload(app, player, config, hasPermRead);
  initializeUpload(app, player, playerServer, hasPermAdd);
  initializeStream(app, player, hasPermRead);

  var httpServer;
  var httpProtocol;

  playerServer.init(function(err) {
    if (err) throw err;
    createHttpServer(attachWebSocketServer);
  });

  function createHttpServer(cb) {
    if (!sslEnabled) {
      if (config.host !== 'localhost' && config.host !== '127.0.0.1') {
        log.warn(
          "WARNING: SSL disabled, and configured to listen on the network. Passwords and\n" +
          "other information might be eavesdropped by third parties.");
      }
      httpServer = http.createServer(app);
      httpProtocol = 'http';
      cb();
    } else {
      httpProtocol = 'https';
      readKeyAndCert(cb);
    }
    function readKeyAndCert(cb) {
      var pend = new Pend();
      var options =  {};
      pend.go(function(cb) {
        fs.readFile(config.sslKey, function(err, data) {
          if (err) {
            log.fatal("Unable to read SSL key file: " + err.message);
            process.exit(1);
            return;
          }
          options.key = data;
          cb();
        });
      });
      pend.go(function(cb) {
        fs.readFile(config.sslCert, function(err, data) {
          if (err) {
            log.fatal("Unable to read SSL cert file: " + err.message);
            process.exit(1);
            return;
          }
          options.cert = data;
          cb();
        });
      });
      pend.go(function(cb) {
        if (!config.sslCaDir) return cb();
        options.ca = [];
        fs.readdir(config.sslCaDir, function(err, fileList) {
          if (err) {
            log.fatal("Unable to read SSL CA dir: " + err.message);
            process.exit(1);
            return;
          }
          fileList.forEach(function(file) {
            pend.go(function(cb) {
              var caPath = path.join(config.sslCaDir, file);
              fs.readFile(caPath, function(err, data) {
                if (err) {
                  log.fatal("Unable to read SSL CA file: " + err.message);
                  process.exit(1);
                  return;
                }
                options.ca.push(data);
                cb();
              });
            });
          });
          cb();
        });
      });
      pend.wait(function() {
        httpServer = httpolyglot.createServer(options, app);
        cb();
      });
    }
  }

  function attachWebSocketServer() {
    var wss = yawl.createServer({
      server: httpServer,
      allowTextMessages: true,
      maxFrameSize: 16 * 1024 * 1024, // 16 MB
      origin: null,
    });
    wss.on('error', function(err) {
      log.error("web socket server error:", err.stack);
    });
    wss.on('connection', function(ws) {
      playerServer.createClient(new WebSocketApiClient(ws), "Guest");
    });
    httpServer.listen(config.port, config.host, function() {
      log.info("Web interface listening at " + httpProtocol + "://" + config.host + ":" + config.port + "/");
    });
    httpServer.on('close', function() {
      log.debug("server closed");
    });
    var mpdPort = config.mpdPort;
    var mpdHost = config.mpdHost;
    if (mpdPort == null || mpdHost == null) {
      log.info("MPD Protocol disabled");
    } else {
      var protocolServer = net.createServer(function(socket) {
        socket.setEncoding('utf8');
        var protocol = new MpdProtocol({
          player: player,
          playerServer: playerServer,
          apiServer: mpdApiServer,
        });
        protocol.on('error', handleError);
        socket.on('error', handleError);
        socket.pipe(protocol).pipe(socket);
        socket.on('close', cleanup);

        function handleError(err) {
          log.error("socket error:", err.stack);
          socket.destroy();
          cleanup();
        }
        function cleanup() {
          protocol.close();
        }
      });
      protocolServer.on('error', function(err) {
        if (err.code === 'EADDRINUSE') {
          log.error("Failed to bind MPD protocol to port " + mpdPort +
            ": Address in use.");
        } else {
          throw err;
        }
      });
      protocolServer.listen(mpdPort, mpdHost, function() {
        log.info("MPD/GrooveBasin Protocol listening at " +
          mpdHost + ":" + mpdPort);
      });
    }
  }
}

function getKey(o) {
  return o.key;
}

function printUsageAndExit() {
  process.stderr.write(
    "Usage: groovebasin [options]\n" +
    "\n" +
    "Available Options:\n" +
    "--config config.json       path to config json file\n" +
    "--verbose                  turn on verbose logging\n" +
    "--delete-all-events        delete all events in db then exit\n" +
    "--delete-all-users         delete all users and events in db then exit\n" +
    "--dump-users               print all users info (recover passwords)\n" +
    "--print-url                write the groovebasin URL to stdout then exit\n" +
    "--spawn [cmd] [args]       spawn cmd with args, replacing % with groovebasin url\n" +
    "--start                    start groovebasin, exit gracefully if already running\n" +
    "");
  process.exit(1);
}
