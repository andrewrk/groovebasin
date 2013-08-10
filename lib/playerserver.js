var EventEmitter, command_permissions, PlayerServer;
EventEmitter = require('events').EventEmitter;
command_permissions = {
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
function qEscape(str){
  return str.toString().replace(/"/g, '\\"');
}
function findNext(object, from_id){
  var ref$, from_key, result, id, item;
  from_key = (ref$ = object[from_id]) != null ? ref$.sort_key : void 8;
  result = null;
  for (id in object) {
    item = object[id];
    if (from_key == null || item.sort_key > from_key) {
      if (result == null || item.sort_key < object[result].sort_key) {
        result = id;
      }
    }
  }
  return result;
}
module.exports = PlayerServer = (function(superclass){
  PlayerServer.displayName = 'PlayerServer';
  var prototype = extend$(PlayerServer, superclass).prototype, constructor = PlayerServer;
  function PlayerServer(library, parser, authenticate){
    var this$ = this instanceof ctor$ ? this : new ctor$;
    this$.library = library;
    this$.parser = parser;
    this$.authenticate = authenticate;
    this$.parser.on('status', function(systems){
      var refresh_mpd_status, client_systems, i$, len$, system;
      console.log('changed systems:', systems);
      refresh_mpd_status = false;
      client_systems = [];
      for (i$ = 0, len$ = systems.length; i$ < len$; ++i$) {
        system = systems[i$];
        if (system === 'stored_playlist' || system === 'sticker') {
          client_systems.push(system);
        } else if (system === 'playlist' || system === 'player' || system === 'mixer' || system === 'options') {
          refresh_mpd_status = true;
        }
      }
      if (client_systems.length) {
        this$.emit('status', client_systems);
      }
      if (refresh_mpd_status) {
        this$.refreshMpdStatus();
      }
    });
    this$.parser.on('error', function(msg){
      console.error("mpd error:", msg);
    });
    this$.playlist = {};
    this$.current_id = null;
    this$.repeat = {
      repeat: false,
      single: false
    };
    this$.is_playing = false;
    this$.mpd_is_playing = false;
    this$.mpd_should_be_playing_id = null;
    this$.parser.sendRequest("clear");
    this$.track_start_date = null;
    this$.paused_time = 0;
    return this$;
  } function ctor$(){} ctor$.prototype = prototype;
  prototype.refreshMpdStatus = function(){
    var this$ = this;
    this.parser.sendRequest(this.parser.current_song_and_status_command, function(o){
      var mpd_was_playing;
      mpd_was_playing = this$.mpd_is_playing;
      this$.mpd_is_playing = o.state === 'play';
      if (this$.mpd_should_be_playing_id != null && mpd_was_playing && !this$.mpd_is_playing) {
        this$.current_id = findNext(this$.playlist, this$.current_id);
        return this$.playlistChanged();
      }
    });
  };
  prototype.createClient = function(socket, permissions){
    var client, this$ = this;
    client = socket;
    client.permissions = permissions;
    socket.on('request', function(request){
      request = JSON.parse(request);
      this$.request(client, request.cmd, function(arg){
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
      if (success = (ref = this$.authenticate(pass)) != null) {
        client.permissions = ref;
      }
      socket.emit('Permissions', JSON.stringify(client.permissions));
      socket.emit('PasswordResult', JSON.stringify(success));
    });
    return client;
  };
  prototype.request = function(client, request, cb){
    var name, suppress_reply, reply_object, id, ref$, item, i$, len$, sort_key, commands, command, this$ = this;
    cb == null && (cb = function(){});
    function check_permission(name){
      var permission, err;
      permission = command_permissions[name];
      if (permission === null) {
        return true;
      }
      if (client.permissions[permission]) {
        return true;
      }
      err = "command " + JSON.stringify(name) + " requires permission " + JSON.stringify(permission);
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
        this.library.get_library(function(library){
          return cb({
            msg: library
          });
        });
        break;
      case 'password':
        this.emit('password', request.password);
        break;
      case 'playlistinfo':
        reply_object = this.playlist;
        break;
      case 'currentsong':
        reply_object = this.current_id;
        break;
      case 'status':
        reply_object = {
          volume: null,
          repeat: this.repeat.repeat,
          single: this.repeat.single,
          state: this.is_playing ? 'play' : 'pause',
          track_start_date: this.track_start_date,
          paused_time: this.paused_time
        };
        break;
      case 'addid':
        for (id in ref$ = request.items) {
          item = ref$[id];
          this.playlist[id] = {
            file: item.file,
            sort_key: item.sort_key,
            is_random: item.is_random
          };
        }
        this.playlistChanged();
        break;
      case 'deleteid':
        for (i$ = 0, len$ = (ref$ = request.ids).length; i$ < len$; ++i$) {
          id = ref$[i$];
          delete this.playlist[id];
        }
        this.playlistChanged();
        break;
      case 'move':
        for (id in ref$ = request.items) {
          sort_key = ref$[id].sort_key;
          this.playlist[id].sort_key = sort_key;
        }
        this.playlistChanged();
        break;
      case 'clear':
        this.playlist = {};
        this.playlistChanged();
        break;
      case 'shuffle':
        break;
      case 'repeat':
        this.repeat.repeat = request.repeat;
        this.repeat.single = request.single;
        break;
      case 'play':
        this.current_id == null && (this.current_id = findNext(this.playlist, null));
        this.is_playing = true;
        this.playlistChanged();
        break;
      case 'pause':
        if (this.is_playing) {
          this.is_playing = false;
          this.paused_time = (new Date - this.track_start_date) / 1000;
          this.playlistChanged();
        }
        break;
      case 'stop':
        this.is_playing = false;
        this.playlistChanged({
          seekto: 0
        });
        break;
      case 'seek':
        this.playlistChanged({
          seekto: request.pos
        });
        break;
      case 'next':
      case 'previous':
        break;
      case 'playid':
        this.current_id = request.track_id;
        this.is_playing = true;
        this.playlistChanged({
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
      this.parser.sendRequest(request, cb);
    }
  };
  prototype.playlistChanged = function(o){
    var commands, seek_command;
    o == null && (o = {});
    if (this.playlist[this.current_id] == null) {
      this.current_id = null;
      this.is_playing = false;
      this.track_start_date = null;
      this.paused_time = 0;
      o.seekto = null;
    }
    commands = [];
    if (this.is_playing) {
      if (this.current_id !== this.mpd_should_be_playing_id) {
        commands.push("clear");
        commands.push("addid \"" + qEscape(this.playlist[this.current_id].file) + "\"");
        commands.push("play");
        this.mpd_should_be_playing_id = this.current_id;
        if (o.seekto === 0) {
          o.seekto = null;
        } else if (this.paused_time) {
          o.seekto = this.paused_time;
        }
        this.track_start_date = new Date;
      }
      if (o.seekto != null) {
        seek_command = "seek 0 " + Math.round(o.seekto);
        if (commands[commands.length - 1] === "play") {
          commands.splice(commands.length - 1, 0, seek_command);
        } else {
          commands.push(seek_command);
        }
        this.track_start_date = new Date - o.seekto * 1000;
      }
      this.paused_time = null;
    } else {
      if (this.mpd_should_be_playing_id != null) {
        commands.push("clear");
        this.mpd_should_be_playing_id = null;
      }
      if (o.seekto != null) {
        this.paused_time = o.seekto;
      }
      this.track_start_date = null;
      this.paused_time == null && (this.paused_time = 0);
    }
    if (commands.length) {
      if (commands.length > 1) {
        commands.unshift("command_list_begin");
        commands.push("command_list_end");
      }
      this.parser.sendRequest(commands.join("\n"));
    }
    this.emit('status', ['playlist', 'player']);
  };
  return PlayerServer;
}(EventEmitter));
function extend$(sub, sup){
  function fun(){} fun.prototype = (sub.superclass = sup).prototype;
  (sub.prototype = new fun).constructor = sub;
  if (typeof sup.extended == 'function') sup.extended(sub);
  return sub;
}
function import$(obj, src){
  var own = {}.hasOwnProperty;
  for (var key in src) if (own.call(src, key)) obj[key] = src[key];
  return obj;
}