var util = require('util');
var EventEmitter = require('events').EventEmitter;

module.exports = PlayerServer;

var command_permissions = {
  addid: 'add',
  clear: 'control',
  currentsong: 'read',
  deleteid: 'control',
  listallinfo: 'read',
  listplaylist: 'read',
  listplaylists: 'read',
  lsinfo: 'read',
  move: 'control',
  next: 'control',
  password: null,
  pause: 'control',
  play: 'control',
  playid: 'control',
  playlistadd: 'add',
  playlistinfo: 'read',
  previous: 'control',
  repeat: 'control',
  seek: 'control',
  setvol: 'control',
  shuffle: 'control',
  status: 'read',
  sticker: 'admin',
  stop: 'control'
};

util.inherits(PlayerServer, EventEmitter);
function PlayerServer(library, parser, authenticate) {
  var self = this;
  self.library = library;
  self.parser = parser;
  self.authenticate = authenticate;
  self.parser.on('status', function(systems){
    var i$, len$, system;
    console.log('changed systems:', systems);
    var refresh_mpd_status = false;
    var client_systems = [];
    for (i$ = 0, len$ = systems.length; i$ < len$; ++i$) {
      system = systems[i$];
      if (system === 'stored_playlist' || system === 'sticker') {
        client_systems.push(system);
      } else if (system === 'playlist' || system === 'player' || system === 'mixer' || system === 'options') {
        refresh_mpd_status = true;
      }
    }
    if (client_systems.length) {
      self.emit('status', client_systems);
    }
    if (refresh_mpd_status) {
      refreshMpdStatus(self);
    }
  });
  self.parser.on('error', function(msg){
    console.error("mpd error:", msg);
  });
  self.playlist = {};
  self.current_id = null;
  self.repeat = {
    repeat: false,
    single: false
  };
  self.is_playing = false;
  self.mpd_is_playing = false;
  self.mpd_should_be_playing_id = null;
  self.parser.sendRequest("clear");
  self.track_start_date = null;
  self.paused_time = 0;
}
PlayerServer.prototype.createClient = function(socket, permissions) {
  var self = this;
  var client = socket;
  client.permissions = permissions;
  socket.on('request', function(request){
    request = JSON.parse(request);
    self.request(client, request.cmd, function(arg){
      var response;
      response = JSON.stringify(import$({
        callback_id: request.callback_id
      }, arg));
      socket.emit('PlayerResponse', response);
    });
  });
  this.on('status', function(arg){
    try {
      socket.emit('PlayerStatus', JSON.stringify(arg));
    } catch (e$) {}
  });
  this.on('error', function(msg){
    try {
      socket.emit('MpdError', msg);
    } catch (e$) {}
  });
  socket.emit('Permissions', JSON.stringify(permissions));
  this.on('password', function(pass){
    var ref, success;
    if (success = (ref = self.authenticate(pass)) != null) {
      client.permissions = ref;
    }
    socket.emit('Permissions', JSON.stringify(client.permissions));
    socket.emit('PasswordResult', JSON.stringify(success));
  });
  return client;
};
PlayerServer.prototype.request = function(client, request, cb){
  var self = this;
  var name, suppress_reply, reply_object, id, ref$, item, i$, len$, sort_key, commands, command;
  if (cb == null) cb = function(){};
  function check_permission(name){
    var permission = command_permissions[name];
    if (permission === null) {
      return true;
    }
    if (client.permissions[permission]) {
      return true;
    }
    var err = "command " + JSON.stringify(name) + " requires permission " + JSON.stringify(permission);
    console.warn("permissions error:", err);
    cb({
      err: err
    });
    return false;
  }
  if (typeof request === 'object') {
    name = request.name;
    if (!check_permission(name)) {
      return;
    }
    suppress_reply = false;
    reply_object = null;
    switch (name) {
      case 'listallinfo':
        suppress_reply = true;
        self.library.get_library(function(library){
          return cb({
            msg: library
          });
        });
        break;
      case 'password':
        self.emit('password', request.password);
        break;
      case 'playlistinfo':
        reply_object = self.playlist;
        break;
      case 'currentsong':
        reply_object = self.current_id;
        break;
      case 'status':
        reply_object = {
          volume: null,
          repeat: self.repeat.repeat,
          single: self.repeat.single,
          state: self.is_playing ? 'play' : 'pause',
          track_start_date: self.track_start_date,
          paused_time: self.paused_time,
        };
        break;
      case 'addid':
        for (id in (ref$ = request.items)) {
          item = ref$[id];
          self.playlist[id] = {
            file: item.file,
            sort_key: item.sort_key,
            is_random: item.is_random,
          };
        }
        playlistChanged(self);
        break;
      case 'deleteid':
        for (i$ = 0, len$ = (ref$ = request.ids).length; i$ < len$; ++i$) {
          id = ref$[i$];
          delete self.playlist[id];
        }
        playlistChanged(self);
        break;
      case 'move':
        for (id in (ref$ = request.items)) {
          sort_key = ref$[id].sort_key;
          self.playlist[id].sort_key = sort_key;
        }
        playlistChanged(self);
        break;
      case 'clear':
        self.playlist = {};
        playlistChanged(self);
        break;
      case 'shuffle':
        break;
      case 'repeat':
        self.repeat.repeat = request.repeat;
        self.repeat.single = request.single;
        break;
      case 'play':
        if (self.current_id == null) self.current_id = findNext(self.playlist, null);
        self.is_playing = true;
        playlistChanged(self);
        break;
      case 'pause':
        if (self.is_playing) {
          self.is_playing = false;
          self.paused_time = (new Date() - self.track_start_date) / 1000;
          playlistChanged(self);
        }
        break;
      case 'stop':
        self.is_playing = false;
        playlistChanged(self, {
          seekto: 0
        });
        break;
      case 'seek':
        playlistChanged(self, {
          seekto: request.pos
        });
        break;
      case 'next':
      case 'previous':
        break;
      case 'playid':
        self.current_id = request.track_id;
        self.is_playing = true;
        playlistChanged(self, {
          seekto: 0
        });
        break;
      default:
        throw new Error("invalid command " + JSON.stringify(name));
    }
    if (!suppress_reply) {
      if (reply_object != null) {
        cb({
          msg: reply_object
        });
      } else {
        cb({});
      }
    }
  } else {
    name = request.split(/\s/)[0];
    if (name === 'command_list_begin') {
      commands = request.split('\n');
      commands.shift();
      commands.pop();
      for (i$ = 0, len$ = commands.length; i$ < len$; ++i$) {
        command = commands[i$];
        name = command.split(/\s/)[0];
        if (!check_permission(name)) {
          return;
        }
      }
    } else {
      if (!check_permission(name)) {
        return;
      }
    }
    self.parser.sendRequest(request, cb);
  }
};

function operatorCompare(a, b) {
  return a < b ? -1 : a > b ? 1 : 0;
}

// TODO: reduce this code duplication
function generateSortKey(previous_key, next_key){
  if (previous_key != null) {
    if (next_key != null) {
      return (previous_key + next_key) / 2;
    } else {
      return 0 | previous_key + 1;
    }
  } else {
    if (next_key != null) {
      return (0 + next_key) / 2;
    } else {
      return 1;
    }
  }
}

function disambiguateSortKeys(self) {
  var tracksInOrder = Object.keys(self.playlist).map(function(id) { return self.playlist[id]; });
  tracksInOrder.sort(function (a, b) { return operatorCompare(a.sort_key, b.sort_key); });
  var previousUniqueKey = null;
  var previousKey = null;
  for (var i = 0; i < tracksInOrder.length; i++) {
    var track = tracksInOrder[i];
    if (track.sort_key === previousKey) {
      // move the repeate back
      track.sort_key = generateSortKey(previousUniqueKey, track.sort_key);
      previousUniqueKey = track.sort_key;
    } else {
      previousUniqueKey = previousKey;
      previousKey = track.sort_key;
    }
  }
}

function playlistChanged(self, o) {
  if (o == null) o = {};

  if (self.playlist[self.current_id] == null) {
    self.current_id = null;
    self.is_playing = false;
    self.track_start_date = null;
    self.paused_time = 0;
    o.seekto = null;
  }
  var commands = [];
  if (self.is_playing) {
    if (self.current_id !== self.mpd_should_be_playing_id) {
      commands.push("clear");
      commands.push("addid \"" + qEscape(self.playlist[self.current_id].file) + "\"");
      commands.push("play");
      self.mpd_should_be_playing_id = self.current_id;
      if (o.seekto === 0) {
        o.seekto = null;
      } else if (self.paused_time) {
        o.seekto = self.paused_time;
      }
      self.track_start_date = new Date();
    }
    if (o.seekto != null) {
      var seek_command = "seek 0 " + Math.round(o.seekto);
      if (commands[commands.length - 1] === "play") {
        commands.splice(commands.length - 1, 0, seek_command);
      } else {
        commands.push(seek_command);
      }
      self.track_start_date = new Date() - o.seekto * 1000;
    }
    self.paused_time = null;
  } else {
    if (self.mpd_should_be_playing_id != null) {
      commands.push("clear");
      self.mpd_should_be_playing_id = null;
    }
    if (o.seekto != null) {
      self.paused_time = o.seekto;
    }
    self.track_start_date = null;
    if (self.paused_time == null) self.paused_time = 0;
  }

  if (commands.length) {
    if (commands.length > 1) {
      commands.unshift("command_list_begin");
      commands.push("command_list_end");
    }
    self.parser.sendRequest(commands.join("\n"));
  }

  disambiguateSortKeys(self);

  self.emit('status', ['playlist', 'player']);
}

function import$(obj, src){
  var own = {}.hasOwnProperty;
  for (var key in src) if (own.call(src, key)) obj[key] = src[key];
  return obj;
}

function qEscape(str){
  return str.toString().replace(/"/g, '\\"');
}
function findNext(object, from_id){
  var ref$;
  var from_key = (ref$ = object[from_id]) != null ? ref$.sort_key : void 8;
  var result = null;
  for (var id in object) {
    var item = object[id];
    if (from_key == null || item.sort_key > from_key) {
      if (result == null || item.sort_key < object[result].sort_key) {
        result = id;
      }
    }
  }
  return result;
}
function refreshMpdStatus(self) {
  self.parser.sendRequest(self.parser.current_song_and_status_command, function(o){
    var mpd_was_playing = self.mpd_is_playing;
    self.mpd_is_playing = o.state === 'play';
    if (self.mpd_should_be_playing_id != null && mpd_was_playing && !self.mpd_is_playing) {
      self.current_id = findNext(self.playlist, self.current_id);
      return playlistChanged(self);
    }
  });
}
