var util = require('util');
var mpd = require('mpd');
var assert = require('assert');
var EventEmitter = require('events').EventEmitter;

module.exports = PlayerServer;

var actions = {};

action('addid', {
  permission: 'add',
  fn: function(client, msg, cb) {
    this.addItems(msg.items);
    cb({});
  },
});

action('clear', {
  permission: 'control',
  fn: function(client, msg, cb) {
    this.clearPlaylist();
    cb({});
  },
});

action('currentsong', {
  permission: 'read',
  fn: function(client, msg, cb) {
    cb({msg: this.current_id});
  },
});

action('deleteid', {
  permission: 'control',
  fn: function(client, msg, cb) {
    this.removePlaylistItems(msg.ids);
    cb({});
  },
});

action('listallinfo', {
  permission: 'read',
  fn: function(client, msg, cb) {
    this.library.get_library(function(library) {
      cb({msg: library});
    });
  },
});

action('move', {
  permission: 'control',
  fn: function(client, msg, cb) {
    this.movePlaylistItems(msg.items);
    cb({});
  },
});

action('password', {
  permission: null,
  fn: function(client, msg, cb) {
    this.authenticateWithPassword(client, msg.password);
    cb({});
  },
});

action('pause', {
  permission: 'control',
  fn: function(client, msg, cb) {
    this.pause();
    cb({});
  },
});

action('play', {
  permission: 'control',
  fn: function(client, msg, cb) {
    this.play();
    cb({});
  },
});

action('playid', {
  permission: 'control',
  fn: function(client, msg, cb) {
    this.playId(msg.track_id);
    cb({});
  },
});

action('playlistinfo', {
  permission: 'read',
  fn: function(client, msg, cb) {
    cb({msg: this.playlist});
  },
});

action('repeat', {
  permission: 'control',
  fn: function(client, msg, cb) {
    this.setRepeatOn(!!msg.repeat);
    this.setRepeatSingle(!!msg.single);
    cb({});
  },
});

action('seek', {
  permission: 'control',
  fn: function(client, msg, cb) {
    this.seek(msg.pos);
    cb({});
  },
});

action('shuffle', {
  permission: 'control',
  fn: function(client, msg, cb) {
    this.shufflePlaylist();
    cb({});
  },
});

action('status', {
  permission: 'read',
  fn: function(client, msg, cb) {
    cb({msg: {
      volume: null,
      repeat: this.repeat.repeat,
      single: this.repeat.single,
      state: this.is_playing ? 'play' : 'pause',
      track_start_date: this.track_start_date,
      paused_time: this.paused_time,
    }});
  },
});

action('stop', {
  permission: 'control',
  fn: function(client, msg, cb) {
    this.stop();
    cb({});
  },
});

util.inherits(PlayerServer, EventEmitter);
function PlayerServer(library, mpdClient, authenticate) {
  var self = this;
  self.library = library;
  self.mpdClient = mpdClient;
  self.authenticate = authenticate;
  self.mpdClient.on('system', function(system){
    console.log('changed system:', system);
  });
  self.mpdClient.on('system-stored_playlist', clientSystemChanged);
  self.mpdClient.on('system-sticker', clientSystemChanged);
  self.mpdClient.on('system-playlist', doRefreshMpdStatus);
  self.mpdClient.on('system-player', doRefreshMpdStatus);
  self.mpdClient.on('system-mixer', doRefreshMpdStatus);
  self.mpdClient.on('system-options', doRefreshMpdStatus);
  self.playlist = {};
  self.current_id = null;
  self.repeat = {
    repeat: false,
    single: false
  };
  self.is_playing = false;
  self.mpd_is_playing = false;
  self.mpd_should_be_playing_id = null;
  self.mpdClient.sendCommand("clear");
  self.track_start_date = null;
  self.paused_time = 0;
  function clientSystemChanged(system) {
    self.emit('status', [system]);
  }
  function doRefreshMpdStatus(system) {
    refreshMpdStatus(self);
  }
}

PlayerServer.prototype.createClient = function(socket, permissions) {
  var self = this;
  var client = socket;
  client.permissions = permissions;
  socket.on('request', function(request){
    request = JSON.parse(request);
    self.request(client, request.cmd, function(arg){
      var response = {callback_id: request.callback_id};
      response.err = arg.err;
      response.msg = arg.msg;
      socket.emit('PlayerResponse', JSON.stringify(response));
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
  return client;
};

PlayerServer.prototype.authenticateWithPassword = function(client, password) {
  var perms = this.authenticate(password);
  var success = perms != null;
  if (success) client.permissions = perms;
  client.emit('Permissions', JSON.stringify(client.permissions));
  client.emit('PasswordResult', JSON.stringify(success));
};

// items looks like [{file, sort_key}]
PlayerServer.prototype.addItems = function(items, tagAsRandom) {
  tagAsRandom = !!tagAsRandom;
  var wantInfoTracksByFile = {};
  var commands = [];
  for (var id in items) {
    var item = items[id];
    var playlistItem = {
      file: item.file,
      sort_key: item.sort_key,
      is_random: tagAsRandom,
    };
    this.playlist[id] = playlistItem;
    wantInfoTracksByFile[item.file] = playlistItem;
    commands.push(mpd.cmd('listallinfo', [item.file]));
  }
  this.mpdClient.sendCommands(commands, function(err, msg) {
    if (err) console.error("Error getting time info for tracks:", err.stack);
    var objects = parseMpdObjects(msg);
    objects.forEach(function(o) {
      wantInfoTracksByFile[o.file].time = parseInt(o.Time, 10);
      this.emit('status', ['playlist', 'player']);
    }.bind(this));
  }.bind(this));
  playlistChanged(this);
}

PlayerServer.prototype.clearPlaylist = function() {
  this.playlist = {};
  playlistChanged(this);
}

PlayerServer.prototype.shufflePlaylist = function() {
  console.error("TODO: implement shuffle");
}

PlayerServer.prototype.removePlaylistItems = function(ids) {
  ids.forEach(function(id) {
    delete this.playlist[id];
  }.bind(this));
  playlistChanged(this);
}

// items looks like {id: {sort_key}}
PlayerServer.prototype.movePlaylistItems = function(items) {
  for (var id in items) {
    this.playlist[id].sort_key = items[id].sort_key;
  }
  playlistChanged(this);
}

PlayerServer.prototype.pause = function() {
  if (!this.is_playing) return;
  this.is_playing = false;
  this.paused_time = (new Date() - this.track_start_date) / 1000;
  playlistChanged(this);
}

PlayerServer.prototype.play = function() {
  if (this.current_id == null) this.current_id = findNext(this.playlist, null);
  this.is_playing = true;
  playlistChanged(this);
}

PlayerServer.prototype.playId = function(id) {
  this.current_id = id;
  this.is_playing = true;
  playlistChanged(this, {
    seekto: 0
  });
}

PlayerServer.prototype.setRepeatOn = function(isOn) {
  this.repeat.repeat = isOn;
};

PlayerServer.prototype.setRepeatSingle = function(single) {
  this.repeat.single = single;
};

PlayerServer.prototype.seek = function(pos) {
  playlistChanged(this, { seekto: pos });
}

PlayerServer.prototype.stop = function() {
  this.is_playing = false;
  playlistChanged(this, {
    seekto: 0
  });
}

function requestObject(self, client, request, cb) {
  var name = request.name;
  var action = actions[name];
  if (! action) {
    console.warn("Invalid command:", name);
    cb({err: "invalid command: " + JSON.stringify(name)});
    return;
  }
  var perm = action.permission;
  if (perm != null && !client.permissions[perm]) {
    var errText = "command " + JSON.stringify(name) +
      " requires permission " + JSON.stringify(perm);
    console.warn("permissions error:", errText);
    cb({err: errText});
    return;
  }
  console.info("ok command", name);
  action.fn.call(self, client, request, cb);
}

PlayerServer.prototype.request = function(client, request, cb){
  cb = cb || noop;
  if (typeof request !== 'object') {
    console.warn("ignoring invalid command:", request);
    cb({err: "invalid command: " + JSON.stringify(request)});
    return;
  }
  requestObject(this, client, request, cb);
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
      var file = self.playlist[self.current_id].file;
      commands.push("clear");
      commands.push(mpd.cmd("addid", [file]));
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
      var seek_command = mpd.cmd("seek", [0, Math.round(o.seekto)]);
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

  if (commands.length) self.mpdClient.sendCommands(commands)

  self.emit('status', ['playlist', 'player']);

  disambiguateSortKeys(self);
}

function findNext(object, from_id){
  var testObject = object[from_id];
  var from_key = testObject && testObject.sort_key;
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

var refreshMpdCommands = ['currentsong', 'status'];
function refreshMpdStatus(self) {
  self.mpdClient.sendCommands(refreshMpdCommands, function(err, msg) {
    if (err) {
      console.error("mpd status error:", err.stack);
      return;
    }
    var o = parseMpdObject(msg);
    var mpd_was_playing = self.mpd_is_playing;
    self.mpd_is_playing = o.state === 'play';
    if (self.mpd_should_be_playing_id != null && mpd_was_playing && !self.mpd_is_playing) {
      self.current_id = findNext(self.playlist, self.current_id);
      return playlistChanged(self);
    }
  });
}

function parseMpdObjects(msg) {
  var list = [];
  var o = null;
  msg.split("\n").forEach(function(line) {
    var index = line.indexOf(": ");
    var key = line.substr(0, index);
    var value = line.substr(index + 2);
    if (key === 'file') {
      if (o) list.push(o);
      o = {};
    }
    o[key] = value;
  });
  if (o) list.push(o);
  return list;
}

function parseMpdObject(msg) {
  var o = {};
  msg.split("\n").forEach(function(line) {
    var index = line.indexOf(": ");
    var key = line.substr(0, index);
    var value = line.substr(index + 2);
    o[key] = value;
  });
  return o;
}

function noop() {}

function action(name, options) {
  actions[name] = options;
}
