var EventEmitter = require('events').EventEmitter;
var http = require('http');
var assert = require('assert');
var socketio = require('socket.io');
var fs = require('fs');
var Pend = require('pend');
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

util.inherits(GrooveBasin, EventEmitter);
function GrooveBasin(options) {
  EventEmitter.call(this);

  options = options || {};

  this.app = express();
}

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

      for (var pluginName in plugins) {
        plugins[pluginName](self);
      }

      self.startServer();
    });
  });
}

GrooveBasin.prototype.restoreState = function(cb) {
  var self = this;

  // things to read from the db and their defaults
  var defaults = {
    musicDirectory: path.join(osenv.home(), "music"),
    permissions: {},
    defaultPermissions: {
      read: true,
      add: true,
      control: true,
    },
  };

  var pend = new Pend();
  for (var key in defaults) {
    pend.go(getStateFn(key));
  }

  pend.wait(function(err) {
    if (err) return cb(err);
    self.emit('stateRestored');
    cb();
  });

  function getStateFn(key) {
    return function(cb) {
      self.db.get(key, function(err, value) {
        if (err) {
          if (err.notFound) {
            self[key] = defaults[key];
            return cb();
          }
          return cb(err);
        }
        self[key] = value;
        return cb();
      });
    };
  }
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

