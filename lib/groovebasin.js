var EventEmitter = require('events').EventEmitter;
var http = require('http');
var assert = require('assert');
var socketio = require('socket.io');
var fs = require('fs');
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
var levelup = require('level');

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

  self.loadConfig(function(err) {
    if (err) {
      console.error("Error reading config:", err.stack);
      return;
    }

    self.httpHost = self.config.host;
    self.httpPort = self.config.port;
    self.db = levelup(self.config.dbPath);

    self.app.use(express.static(path.join(__dirname, '../public')));
    self.app.use(express.static(path.join(__dirname, '../src/public')));

    self.player = new Player(self.db, self.config.musicDirectory);
    self.player.initialize(function(err) {
      if (err) {
        console.error("unable to initialize player:", err.stack);
        return;
      }
      console.info("Player initialization complete.");

      self.app.use(self.player.streamMiddleware.bind(self.player));

      self.playerServer = new PlayerServer(self.player, authenticate);

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

  function authenticate(password) {
    return self.config.permissions[password];
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
    var client = self.playerServer.createClient(socket, self.config.defaultPermissions);
    self.emit('socketConnect', client);
  }
}

function genPassword() {
  return Math.random().toString();
}
