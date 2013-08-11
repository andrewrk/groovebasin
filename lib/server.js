var fs = require('fs');
var http = require('http');
var net = require('net');
var socketio = require('socket.io');
var socketio_client = require('socket.io-client');
var express = require('express');
var path = require('path');
var assert = require('assert');
var mkdirp = require('mkdirp');
var PlayerClient = require('./playerclient');
var MpdParser = require('./mpdparser');
var PlayerServer = require('./playerserver');
var async = require('async');
var which = require('which');
var Library = require('./library');
var MpdConf = require('./mpdconf');
var Killer = require('./killer');
var spawn = require('child_process').spawn;
var EventEmitter = require('events').EventEmitter;
if (!process.env.NODE_ENV) process.env.NODE_ENV = "dev";
var HOST = process.env.HOST || "0.0.0.0";
var PORT = parseInt(process.env.PORT, 10) || 16242;
var RUN_DIR = "run";
var MPD_SOCKET_PATH = path.join(RUN_DIR, "mpd.socket");
var STATE_FILE = path.join(RUN_DIR, "state.json");
var MPD_CONF_PATH = path.join(RUN_DIR, "mpd.conf");
var MPD_PID_FILE = path.join(RUN_DIR, "mpd.pid");
var mpd_conf = new MpdConf();
mpd_conf.setRunDir(RUN_DIR);
var player_server = null;
var my_player = null;
var state = null;
var app = null;
var io = null;
var plugins = {
  objects: {},
  bus: new EventEmitter(),
  initialize: function(cb){
    var PLUGIN_PATH, this$ = this;
    PLUGIN_PATH = path.join(__dirname, "plugins");
    fs.readdir(PLUGIN_PATH, function(err, files){
      var i$, len$, file, name, Plugin, plugin;
      if (err) {
        return cb(err);
      }
      for (i$ = 0, len$ = files.length; i$ < len$; ++i$) {
        file = files[i$];
        if (!/\.js$/.test(file)) {
          continue;
        }
        name = path.basename(file, ".js");
        Plugin = require("./plugins/" + name);
        plugin = this$.objects[name] = new Plugin(this$.bus);
        plugin.on('state_changed', saveState);
        plugin.on('status_changed', saveAndSendStatus);
      }
      cb();
    });
  },
  featuresList: function(){
    var name, ref$, plugin, results$ = [];
    for (name in (ref$ = this.objects)) {
      plugin = ref$[name];
      results$.push([name, plugin.is_enabled]);
    }
    return results$;
  }
};
var library = new Library(plugins.bus);
function makeRunDir(cb){
  mkdirp(RUN_DIR, cb);
}
var STATE_VERSION = 4;
var DEFAULT_PERMISSIONS = {
  read: true,
  add: true,
  control: true
};
function initState(cb){
  which('mpd', function(err, mpd_exe){
    if (err) {
      console.warn("Unable to find mpd binary in path: " + err.stack);
    }
    state = {
      state_version: STATE_VERSION,
      mpd_exe_path: mpd_exe,
      status: {},
      mpd_conf: mpd_conf.state,
      permissions: {},
      default_permissions: DEFAULT_PERMISSIONS
    };
    cb();
  });
}
function startSocketIo(){
  var app_server;
  app = express();
  app.disable('x-powered-by');
  app_server = http.createServer(app);
  if (io != null) {
    try {
      io.server.close();
    } catch (e$) {}
  }
  io = socketio.listen(app_server);
  io.set('log level', 2);
  io.sockets.on('connection', onSocketIoConnection);
  app_server.listen(PORT, HOST, function(){
    if (typeof process.send === 'function') {
      process.send('online');
    }
    console.info("Listening at http://" + HOST + ":" + PORT);
    connectMasterPlayer();
  });
  app_server.on('close', function(){
    console.info("server closed");
  });
  process.on('message', function(message){
    if (message === 'shutdown') {
      process.exit(0);
    }
  });
}
function startPlugins(){
  var i$, ref$, len$, ref1$, name, enabled;
  console.log('starting plugins');
  app.use(express.static(path.join(__dirname, '../public')));
  app.use(express.static(path.join(__dirname, '../src/public')));
  plugins.bus.emit('app', app);
  plugins.bus.emit('mpd', my_player);
  plugins.bus.emit('save_state', state);
  for (i$ = 0, len$ = (ref$ = plugins.featuresList()).length; i$ < len$; ++i$) {
    ref1$ = ref$[i$], name = ref1$[0], enabled = ref1$[1];
    if (enabled) {
      console.info(name + " is enabled.");
    } else {
      console.warn(name + " is disabled.");
    }
  }
}
function oncePerEventLoopFunc(fn){
  var queued, cbs;
  queued = false;
  cbs = [];
  return function(cb){
    if (cb != null) {
      cbs.push(cb);
    }
    if (queued) {
      return;
    }
    queued = true;
    process.nextTick(function(){
      queued = false;
      fn(function(){
        var i$, ref$, len$, cb;
        for (i$ = 0, len$ = (ref$ = cbs).length; i$ < len$; ++i$) {
          cb = ref$[i$];
          cb.apply(this, arguments);
        }
      });
    });
  };
}
var saveState = oncePerEventLoopFunc(function(cb){
  plugins.bus.emit('save_state', state);
  fs.writeFile(STATE_FILE, JSON.stringify(state, null, 4), "utf8", function(err){
    if (err) {
      console.error("Error saving state to disk: " + err.stack);
    }
    cb(err);
  });
});
function restoreState(cb){
  fs.readFile(STATE_FILE, 'utf8', function(err, data){
    var loaded_state, e;
    if ((err != null ? err.code : void 8) === 'ENOENT') {
      console.warn("No state file. Creating a new one.");
    } else if (err) {
      return cb(err);
    } else {
      try {
        loaded_state = JSON.parse(data);
      } catch (e$) {
        e = e$;
        return cb(new Error("state file contains invalid JSON: " + e));
      }
      if (loaded_state.state_version !== STATE_VERSION) {
        return cb(new Error("State version is " + loaded_state.state_version + " but should be " + STATE_VERSION));
      }
      state = loaded_state;
    }
    plugins.bus.emit('restore_state', state);
    plugins.bus.emit('save_state', state);
    cb();
  });
}
function saveAndSendStatus(){
  saveState();
  io.sockets.emit('Status', JSON.stringify(state.status));
}
function writeMpdConf(cb){
  mpd_conf = new MpdConf(state.mpd_conf);
  state.mpd_conf = mpd_conf.state;
  fs.writeFile(MPD_CONF_PATH, mpd_conf.toMpdConf(), cb);
}
function onSocketIoConnection(socket){
  var client;
  client = player_server.createClient(socket, state.default_permissions);
  plugins.bus.emit('socket_connect', client);
}
function restartMpd(cb){
  mkdirp(mpd_conf.playlistDirectory(), function(err){
    if (err) {
      return cb(err);
    }
    fs.readFile(MPD_PID_FILE, 'utf8', function(err, pid_str){
      var pid, killer;
      if (err) {
        if ((err != null ? err.code : void 8) === 'ENOENT') {
          startMpd(cb);
        } else {
          cb(err);
        }
      } else {
        pid = parseInt(pid_str, 10);
        console.info("killing mpd", pid);
        killer = new Killer(pid);
        killer.on('error', function(err){
          cb(err);
        });
        killer.on('end', function(){
          startMpd(cb);
        });
        killer.kill();
      }
    });
  });
}
function startMpd(cb){
  var child;
  console.info("starting mpd", state.mpd_exe_path);
  child = spawn(state.mpd_exe_path, ['--no-daemon', MPD_CONF_PATH], {
    stdio: 'inherit',
    detached: true
  });
  cb();
}
function makeConnectFunction(name, arg$){
  var createSocket, onSuccess, connect_timeout, connect_success;
  createSocket = arg$.createSocket, onSuccess = arg$.onSuccess;
  connect_timeout = null;
  function tryReconnect(){
    if (connect_timeout != null) {
      return;
    }
    connect_timeout = setTimeout(function(){
      connect_timeout = null;
      connect();
    }, 1000);
  }
  connect_success = true;
  function connect(){
    var socket;
    socket = createSocket();
    socket.on('close', function(){
      if (connect_success) {
        console.warn(name + " connection closed");
      }
      tryReconnect();
    });
    socket.on('error', function(){
      if (connect_success) {
        connect_success = false;
        console.warn(name + " connection error...");
      }
      tryReconnect();
    });
    socket.on('connect', function(){
      console.log((connect_success ? '' : '...') + "" + name + " connected");
      connect_success = true;
      onSuccess(socket);
    });
  }
  return connect;
}
var connectToMpd = makeConnectFunction('mpd', {
  createSocket: function(){
    var socket;
    socket = net.connect({
      path: MPD_SOCKET_PATH
    });
    socket.setEncoding('utf8');
    return socket;
  },
  onSuccess: function(socket){
    var mpd_parser, authenticate;
    mpd_parser = new MpdParser(socket);
    authenticate = function(pass){
      return state.permissions[pass];
    };
    player_server = new PlayerServer(library, mpd_parser, authenticate);
    startSocketIo();
  }
});
var connectMasterPlayer = makeConnectFunction('master player', {
  createSocket: function(){
    return socketio_client.connect("http://localhost:" + PORT);
  },
  onSuccess: function(socket){
    my_player = new PlayerClient(socket);
    my_player.on('MpdError', function(msg){
      console.error(msg);
    });
    socket.emit('SetUserName', '[server]');
    my_player.authenticate(state.admin_password);
    startPlugins();
  }
});
async.series([
  initState,
  makeRunDir,
  plugins.initialize.bind(plugins),
  restoreState,
  writeMpdConf,
  restartMpd,
], function(err) {
  assert.ifError(err);
  connectToMpd();
});
