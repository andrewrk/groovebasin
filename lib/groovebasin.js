var EventEmitter = require('events').EventEmitter;
var http = require('http');
var assert = require('assert');
var socketio = require('socket.io');
var fs = require('fs');
var util = require('util');
var path = require('path');
var mkdirp = require('mkdirp');
var express = require('express');
var osenv = require('osenv');
var spawn = require('child_process').spawn;
var requireIndex = require('requireindex');
var plugins = requireIndex(path.join(__dirname, 'plugins'));
var PlayerServer = require('./playerserver');
var Library = require('./library');

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
  this.stateFile = path.join(this.runDir, "state.json");
  mkdirp.sync(this.runDir);

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

GrooveBasin.prototype.restoreState = function(cb) {
  restoreState(this, cb);
}

// TODO: instead of saving json to disk use leveldb or something like that.
GrooveBasin.prototype.saveState = function(cb) {
  cb = cb || noop;
  saveState(this, cb);
}

GrooveBasin.prototype.startServer = function(cb) {
  startServer(this, cb);
}

GrooveBasin.prototype.saveAndSendStatus = function() {
  this.saveState();
  this.socketIo.sockets.emit('Status', JSON.stringify(this.state.status));
};

GrooveBasin.prototype.rescanLibrary = function() {
  console.error("TODO: rescanning library is not yet supported.");
};

function startServer(self) {
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

function start(self, options) {
  self.httpHost = options.host || "0.0.0.0";
  self.httpPort = options.port || 16242;

  self.once('stateRestored', function() {
    self.library = new Library(self.state.musicDirectory);
    self.library.startScan();
    self.library.on('library', function() {
      // TODO: this is weird. PlayerServer and Library should be the same object IMO
      self.playerServer = new PlayerServer(self.library, authenticate);
      startServer(self);
    });
  });

  self.app.use(express.static(path.join(__dirname, '../public')));
  self.app.use(express.static(path.join(__dirname, '../src/public')));

  self.state = {
    musicDirectory: path.join(osenv.home(), "music"),
    state_version: STATE_VERSION,
    status: {},
    permissions: {},
    default_permissions: DEFAULT_PERMISSIONS
  };

  for (var pluginName in plugins) {
    plugins[pluginName](self, options);
  }

  restoreState(self, function(err) {
    if (err) {
      console.error("unable to restore state:", err.stack);
      return;
    }
  });

  function authenticate(pass) {
    return self.state.permissions[pass];
  }
}

function noop() {}
