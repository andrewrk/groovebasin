var Player = require('./player');

module.exports = PlayerServer;

var actions = {
  'addid': {
    permission: 'add',
    fn: function(client, msg, cb) {
      this.player.addItems(msg.items);
      cb({});
    },
  },
  'clear': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.player.clearPlaylist();
      cb({});
    },
  },
  'currentsong': {
    permission: 'read',
    fn: function(client, msg, cb) {
      cb({msg: this.player.currentTrack && this.player.currentTrack.id});
    },
  },
  'deleteid': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.player.removePlaylistItems(msg.ids);
      cb({});
    },
  },
  'listallinfo': {
    permission: 'read',
    fn: function(client, msg, cb) {
      cb({msg: this.player.allDbFiles});
    },
  },
  'move': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.player.movePlaylistItems(msg.items);
      cb({});
    },
  },
  'password': {
    permission: null,
    fn: function(client, msg, cb) {
      this.authenticateWithPassword(client, msg.password);
      cb({});
    },
  },
  'pause': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.player.pause();
      cb({});
    },
  },
  'play': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.player.play();
      cb({});
    },
  },
  'playid': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.player.playId(msg.track_id);
      cb({});
    },
  },
  'playlistinfo': {
    permission: 'read',
    fn: function(client, msg, cb) {
      cb({msg: serializePlaylist(this.player.playlist)});
    },
  },
  'repeat': {
    permission: 'control',
    fn: function(client, msg, cb) {
      if (!msg.repeat) {
        this.player.setRepeat(Player.REPEAT_OFF);
      } else if (msg.single) {
        this.player.setRepeat(Player.REPEAT_ONE);
      } else {
        this.player.setRepeat(Player.REPEAT_ALL);
      }
      cb({});
    },
  },
  'seek': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.player.seek(msg.pos);
      cb({});
    },
  },
  'shuffle': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.player.shufflePlaylist();
      cb({});
    },
  },
  'status': {
    permission: 'read',
    fn: function(client, msg, cb) {
      cb({msg: {
        volume: null,
        repeat: this.player.repeat !== Player.REPEAT_OFF,
        single: this.player.repeat === Player.REPEAT_ONE,
        state: this.player.isPlaying ? 'play' : 'pause',
        track_start_date: this.player.trackStartDate,
        paused_time: this.player.pausedTime,
      }});
    },
  },
  'stop': {
    permission: 'control',
    fn: function(client, msg, cb) {
      this.player.stop();
      cb({});
    },
  },
};

function PlayerServer(player, authenticate) {
  this.player = player;
  this.authenticate = authenticate;
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
  self.player.on('playlistUpdate', function() {
    try {
      socket.emit('PlayerStatus', JSON.stringify(['playlist', 'player']));
    } catch (e$) {}
  });
  self.player.on('error', function(msg){
    try {
      socket.emit('MpdError', msg);
    } catch (e$) {}
  });
  socket.emit('Permissions', JSON.stringify(permissions));
  return client;
};


PlayerServer.prototype.request = function(client, request, cb){
  cb = cb || noop;
  if (typeof request !== 'object') {
    console.warn("ignoring invalid command:", request);
    cb({err: "invalid command: " + JSON.stringify(request)});
    return;
  }
  requestObject(this, client, request, cb);
};

PlayerServer.prototype.authenticateWithPassword = function(client, password) {
  var perms = this.authenticate(password);
  var success = perms != null;
  if (success) client.permissions = perms;
  client.emit('Permissions', JSON.stringify(client.permissions));
  client.emit('PasswordResult', JSON.stringify(success));
};

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

function serializePlaylist(playlist) {
  var o = {};
  for (var id in playlist) {
    var item = playlist[id];
    o[id] = {
      file: item.file,
      sort_key: item.sort_key,
      is_random: item.is_random,
      time: item.time,
    };
  }
  return o;
}

function noop() {}

