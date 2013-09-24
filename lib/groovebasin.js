var EventEmitter = require('events').EventEmitter;
var http = require('http');
var assert = require('assert');
var socketio = require('socket.io');
var fs = require('fs');
var util = require('util');
var path = require('path');
var express = require('express');
var osenv = require('osenv');
var spawn = require('child_process').spawn;
var requireIndex = require('requireindex');
var plugins = requireIndex(path.join(__dirname, 'plugins'));
var Player = require('./player');
var getDb = require('./db');

module.exports = GrooveBasin;

var CONFIG_KEY_PREFIX = "Config.";

util.inherits(GrooveBasin, EventEmitter);
function GrooveBasin() {
  EventEmitter.call(this);

  this.app = express();

  // defaults until we load the real values from the db
  this.config = {
    musicDirectory: path.join(osenv.home(), "music"),
    permissions: {},
    defaultPermissions: {
      read: true,
      add: true,
      control: true,
    },
  };
}

GrooveBasin.prototype.initConfigVar = function(name, defaultValue) {
  this.configVars.push(name);
  this[name] = defaultValue;
};

GrooveBasin.prototype.start = function(options) {
  var self = this;
  self.httpHost = options.host || "0.0.0.0";
  self.httpPort = options.port || 16242;
  self.db = getDb(options.dbPath);

  self.app.use(express.static(path.join(__dirname, '../public')));
  self.app.use(express.static(path.join(__dirname, '../src/public')));

  self.restoreState(function(err) {
    if (err) {
      console.error("unable to restore state:", err.stack);
      return;
    }
    self.player = new Player(self);
    self.player.initializeLibrary(function(err) {
      if (err) {
        console.error("unable to initialize library:", err.stack);
        return;
      }

      console.info("Library initialization complete.");
      for (var pluginName in plugins) {
        plugins[pluginName](self);
      }

      self.startServer();
    });
  });
}

GrooveBasin.prototype.restoreState = function(cb) {
  var self = this;

  // override with values from db
  var stream = self.db.createReadStream({
    start: CONFIG_KEY_PREFIX,
  });
  stream.on('data', function(data) {
    if (data.key.indexOf(CONFIG_KEY_PREFIX) !== 0) {
      stream.removeAllListeners();
      stream.destroy();
      cb();
      return;
    }
    var varName = data.key.substring(CONFIG_KEY_PREFIX.length);
    self.config[varName] = JSON.parse(data.value);
  });
  stream.on('error', function(err) {
    stream.removeAllListeners();
    stream.destroy();
    cb(err);
  });
  stream.on('close', function() {
    cb();
  });
}

GrooveBasin.prototype.startServer = function() {
  var self = this;

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
    var client = self.playerServer.createClient(socket, self.defaultPermissions);
    self.emit('socketConnect', client);
  }
}

