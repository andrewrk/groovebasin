var EventEmitter = require('events').EventEmitter;
var mpd = require('mpd');
var async = require('async');
var http = require('http');
var assert = require('assert');
var socketio = require('socket.io');
var fs = require('fs');
var util = require('util');
var path = require('path');
var mkdirp = require('mkdirp');
var which = require('which');
var express = require('express');
var spawn = require('child_process').spawn;
var requireIndex = require('requireindex');
var plugins = requireIndex(path.join(__dirname, 'plugins'));
var PlayerServer = require('./playerserver');
var Killer = require('./killer');
var Library = require('./library');
var MpdConf = require('./mpdconf');

module.exports = GrooveBasin;

var STATE_VERSION = 4;
var DEFAULT_PERMISSIONS = {
  read: true,
  add: true,
  control: true
};

util.inherits(GrooveBasin, EventEmitter);
function GrooveBasin() {
  EventEmitter.call(this);

  this.runDir = "run";
  this.mpdSocketPath = path.join(this.runDir, "mpd.socket");
  this.stateFile = path.join(this.runDir, "state.json");
  this.mpdConfPath = path.join(this.runDir, "mpd.conf");
  this.mpdPidFile = path.join(this.runDir, "mpd.pid");

  this.mpdConf = new MpdConf();
  this.mpdConf.setRunDir(this.runDir);

  this.app = express();
  this.app.disable('x-powered-by');

  // initialized later
  this.state = null;
  this.library = null;
  this.socketIo = null;
  this.httpServer = null;
}

GrooveBasin.prototype.start = function(options) {
  start(this, options || {});
}

GrooveBasin.prototype.makeRunDir = function(cb) {
  mkdirp(this.runDir, cb);
}

GrooveBasin.prototype.initState = function(cb) {
  initState(this, cb);
}

GrooveBasin.prototype.restoreState = function(cb) {
  restoreState(this, cb);
}

// TODO: instead of saving json to disk use leveldb or something like that.
GrooveBasin.prototype.saveState = function(cb) {
  cb = cb || noop;
  saveState(this, cb);
}

GrooveBasin.prototype.writeMpdConf = function(cb) {
  var mc = new MpdConf(this.state.mpd_conf);
  this.state.mpd_conf = mc.state;
  fs.writeFile(this.mpdConfPath, mc.toMpdConf(), cb);
}

GrooveBasin.prototype.restartMpd = function(cb) {
  restartMpd(this, cb);
}

GrooveBasin.prototype.startServer = function(cb) {
  startServer(this, cb);
}

GrooveBasin.prototype.connectToMpd = function() {
  connectToMpd(this);
}

GrooveBasin.prototype.saveAndSendStatus = function() {
  this.saveState();
  this.socketIo.sockets.emit('Status', JSON.stringify(this.state.status));
};

GrooveBasin.prototype.rescanLibrary = function() {
  console.error("TODO: rescanning library is not yet supported.");
};

function connectToMpd(self) {
  var connectTimeout = null;
  var connectSuccess = true;

  connect();

  function tryReconnect() {
    if (connectTimeout != null) return;
    connectTimeout = setTimeout(function(){
      connectTimeout = null;
      connect();
    }, 1000);
  }

  function connect() {
    var mpdClient = mpd.connect({path: self.mpdSocketPath});
    mpdClient.on('end', function(){
      if (connectSuccess) console.warn("mpd connection closed");
      tryReconnect();
    });
    mpdClient.on('error', function(){
      if (connectSuccess) {
        connectSuccess = false;
        console.warn("mpd connection error...");
      }
      tryReconnect();
    });
    mpdClient.on('ready', function() {
      console.log((connectSuccess ? '' : '...') + "mpd connected");
      connectSuccess = true;
      self.playerServer = new PlayerServer(self.library, mpdClient, authenticate);
      self.emit('playerServerInit', self.playerServer);
    });
  }

  function authenticate(pass) {
    return self.state.permissions[pass];
  }
}

function startServer(self, cb) {
  assert.ok(self.httpServer == null);
  assert.ok(self.socketIo == null);

  self.httpServer = http.createServer(self.app);
  self.socketIo = socketio.listen(self.httpServer);
  self.socketIo.set('log level', 2);
  self.socketIo.sockets.on('connection', onSocketIoConnection);
  self.httpServer.listen(self.httpPort, self.httpHost, function() {
    self.emit('listening');
    console.info("Listening at http://" + self.httpHost + ":" + self.httpPort + "/");
  });
  self.httpServer.on('close', function() {
    console.info("server closed");
  });
  function onSocketIoConnection(socket){
    if (self.playerServer == null) {
      console.error("TODO: make PlayerServer not depend on other bullshit so that this works when we have to reconnect. (refresh the browser)");
      return;
    }
    var client = self.playerServer.createClient(socket, self.state.default_permissions);
    self.emit('socketConnect', client);
  }
}

function startMpd(self, cb){
  console.info("starting mpd", self.state.mpd_exe_path);
  var args = ['--no-daemon', self.mpdConfPath];
  var opts = {
    stdio: 'inherit',
    detached: true,
  };
  var child = spawn(self.state.mpd_exe_path, args, opts);
  cb();
}

function restartMpd(self, cb) {
  mkdirp(self.mpdConf.playlistDirectory(), function(err) {
    if (err) return cb(err);
    fs.readFile(self.mpdPidFile, {encoding: 'utf8'}, function(err, pidStr) {
      if (err && err.code === 'ENOENT') {
        startMpd(self, cb);
        return;
      } else if (err) {
        cb(err);
        return;
      }
      var pid = parseInt(pidStr, 10);
      console.info("killing mpd", pid);
      var killer = new Killer(pid);
      killer.on('error', function(err) {
        cb(err);
      });
      killer.on('end', function() {
        startMpd(self, cb);
      });
      killer.kill();
    });
  });
}

function saveState(self, cb) {
  self.emit('aboutToSaveState', self.state);
  process.nextTick(function() {
    var data = JSON.stringify(self.state, null, 4);
    fs.writeFile(self.stateFile, data, function(err) {
      if (err) {
        console.error("Error saving state to disk:", err.stack);
      }
      cb();
    });
  });
}

function restoreState(self, cb) {
  fs.readFile(self.stateFile, 'utf8', function(err, data) {
    if (err && err.code === 'ENOENT') {
      console.warn("No state file. Creating a new one.");
    } else if (err) {
      return cb(err);
    } else {
      var loadedState;
      try {
        loadedState = JSON.parse(data);
      } catch (err) {
        cb(new Error("state file contains invalid JSON: " + err.message));
        return;
      }
      if (loadedState.state_version !== STATE_VERSION) {
        return cb(new Error("State version is " + loadedState.state_version +
            " but should be " + STATE_VERSION));
      }
      self.state = loadedState;
    }
    self.emit('stateRestored', self.state);
    cb();
  });
}

function initState(self, cb) {
  which('mpd', function(err, mpdExe){
    if (err) {
      // it's okay. this was just a good default.
      console.warn("Unable to find mpd binary in path:", err.stack);
    }
    self.state = {
      state_version: STATE_VERSION,
      mpd_exe_path: mpdExe,
      status: {},
      mpd_conf: self.mpdConf.state,
      permissions: {},
      default_permissions: DEFAULT_PERMISSIONS
    };
    self.emit("stateInitialized");
    cb();
  });
}

function start(self, options) {
  self.httpHost = options.host || "0.0.0.0";
  self.httpPort = options.port || 16242;

  self.on('stateRestored', function() {
    self.library = new Library(self.mpdConf.state.music_directory);
    self.library.startScan();
  });

  self.app.use(express.static(path.join(__dirname, '../public')));
  self.app.use(express.static(path.join(__dirname, '../src/public')));

  for (var pluginName in plugins) {
    plugins[pluginName](self, options);
  }

  async.series([
    self.initState.bind(self),
    self.makeRunDir.bind(self),
    self.restoreState.bind(self),
    self.writeMpdConf.bind(self),
    self.restartMpd.bind(self),
  ], function(err) {
    assert.ifError(err);
    self.connectToMpd();
    self.startServer();
  });
}

function noop() {}
