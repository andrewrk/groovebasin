var removeDiacritics = require('diacritics').remove;
var EventEmitter = require('events').EventEmitter;
var util = require('util');

module.exports = PlayerClient;

var ref$, slice$ = [].slice;
var PREFIXES_TO_STRIP = [/^\s*the\s+/, /^\s*a\s+/, /^\s*an\s+/];
var next_id = 0;
var VARIOUS_ARTISTS_KEY = "VariousArtists";
var VARIOUS_ARTISTS_NAME = "Various Artists";
var compareSortKeyAndId = makeCompareProps(['sort_key', 'id']);

function stripPrefixes(str){
  var i$, ref$, len$, regex;
  for (i$ = 0, len$ = (ref$ = PREFIXES_TO_STRIP).length; i$ < len$; ++i$) {
    regex = ref$[i$];
    str = str.replace(regex, '');
    break;
  }
  return str;
}
var MPD_SENTINEL = /^(OK|ACK|list_OK)(.*)$/m;
function elapsedToDate(elapsed){
  return new Date(new Date() - elapsed * 1000);
}
function dateToElapsed(date){
  return (new Date() - date) / 1000;
}
function fromMpdVol(vol){
  vol = parseInt(vol, 10);
  if (vol < 0 || vol > 100) {
    return null;
  } else {
    return vol / 100;
  }
}
function toMpdVol(vol){
  var ref$;
  return (ref$ = 0 > (ref$ = Math.round(parseFloat(vol, 10) * 100)) ? 0 : ref$) < 100 ? ref$ : 100;
}
function sortableTitle(title){
  return stripPrefixes(formatSearchable(title));
}
function titleCompare(a, b){
  var _a = sortableTitle(a);
  var _b = sortableTitle(b);
  if (_a < _b) {
    return -1;
  } else if (_a > _b) {
    return 1;
  } else {
    if (a < b) {
      return -1;
    } else if (a > b) {
      return 1;
    } else {
      return 0;
    }
  }
}
function noop(err){
  if (err) throw err;
}
function qEscape(str){
  return str.toString().replace(/"/g, '\\"');
}
function sign(n){
  if (n > 0) {
    return 1;
  } else if (n < 0) {
    return -1;
  } else {
    return 0;
  }
}
function parseMaybeUndefNumber(n){
  n = parseInt(n, 10);
  if (isNaN(n)) {
    n = null;
  }
  return n;
}
function splitOnce(line, separator){
  var index;
  index = line.indexOf(separator);
  return [line.substr(0, index), line.substr(index + separator.length)];
}
function parseWithSepField(msg, sep_field, skip_fields, flush){
  var i$, ref$, len$, line, ref1$, key, value;
  if (msg === "") {
    return [];
  }
  var current_obj = null;
  for (i$ = 0, len$ = (ref$ = msg.split("\n")).length; i$ < len$; ++i$) {
    line = ref$[i$];
    ref1$ = splitOnce(line, ': '), key = ref1$[0], value = ref1$[1];
    if (key in skip_fields) {
      continue;
    }
    if (key === sep_field) {
      flushCurrentObj();
    }
    current_obj[key] = value;
  }
  return flushCurrentObj();

  function flushCurrentObj(){
    if (current_obj != null) {
      flush(current_obj);
    }
    current_obj = {};
  }
}
function getOrCreate(key, table, initObjFunc){
  var result;
  result = table[key];
  if (result == null) {
    result = initObjFunc();
    table[key] = result;
  }
  return result;
}
function trackComparator(a, b){
  if (a.track < b.track) {
    return -1;
  } else if (a.track > b.track) {
    return 1;
  } else {
    return titleCompare(a.name, b.name);
  }
}
function albumComparator(a, b){
  if (a.year < b.year) {
    return -1;
  } else if (a.year > b.year) {
    return 1;
  } else {
    return titleCompare(a.name, b.name);
  }
}
function artistComparator(a, b){
  return titleCompare(a.name, b.name);
}
function playlistComparator(a, b){
  return titleCompare(a.name, b.name);
}
function albumKey(track){
  if (track.album_name) {
    return formatSearchable(track.album_name);
  } else {
    return formatSearchable(track.album_artist_name);
  }
}
function artistKey(artist_name){
  return formatSearchable(artist_name);
}
function moreThanOneKey(object){
  var count, k;
  count = -2;
  for (k in object) {
    if (!++count) {
      return true;
    }
  }
  return false;
}
function nextId(){
  return "id-" + next_id++;
}
function addSearchTags(tracks){
  var i$, len$, track;
  for (i$ = 0, len$ = tracks.length; i$ < len$; ++i$) {
    track = tracks[i$];
    track.search_tags = formatSearchable([
        track.artist_name,
        track.album_artist_name,
        track.album_name,
        track.name,
        track.file,
    ].join("\n"));
  }
}
function formatSearchable(str) {
  return removeDiacritics(str).toLowerCase();
}
function operatorCompare(a, b){
  if (a === b) {
    return 0;
  }
  if (a < b) {
    return -1;
  } else {
    return 1;
  }
}
function makeCompareProps(props){
  return function(a, b){
    var i$, ref$, len$, prop, result;
    for (i$ = 0, len$ = (ref$ = props).length; i$ < len$; ++i$) {
      prop = ref$[i$];
      result = operatorCompare(a[prop], b[prop]);
      if (result) {
        return result;
      }
    }
    return 0;
  };
}

util.inherits(PlayerClient, EventEmitter);
function PlayerClient(socket) {
  EventEmitter.call(this);

  var self = this;
  self.socket = socket;
  self.resetServerState();
  self.updateFuncs = {
    stored_playlist: self.updateStoredPlaylists.bind(self),
    playlist: self.updatePlaylist.bind(self),
    player: self.updateStatus.bind(self),
    mixer: self.updateStatus.bind(self),
  };
  self.socket.on('PlayerResponse', function(data) {
    self.handleResponse(JSON.parse(data));
  });
  self.socket.on('PlayerStatus', function(data) {
    self.handleStatus(JSON.parse(data));
  });
  self.socket.on('disconnect', function() {
    self.resetServerState();
  });
  if (self.socket.socket.connected) {
    self.handleConnectionStart();
  } else {
    self.socket.on('connect', self.handleConnectionStart.bind(self));
  }
}

PlayerClient.prototype.handleConnectionStart = function(){
  var this$ = this;
  this.updateLibrary(function(){
    this$.updateStatus();
    this$.updatePlaylist();
    this$.updateStoredPlaylists();
  });
};
PlayerClient.prototype.updateLibrary = function(callback){
  var this$ = this;
  callback = callback || noop;
  this.sendCommandName('listallinfo', function(err, track_table){
    var res$, _, track, tracks, last_query;
    if (err) {
      return callback(err);
    }
    res$ = [];
    for (_ in track_table) {
      track = track_table[_];
      res$.push(track);
    }
    tracks = res$;
    addSearchTags(tracks);
    this$.buildArtistAlbumTree(tracks, this$.library);
    this$.have_file_list_cache = true;
    last_query = this$.last_query;
    this$.last_query = "";
    this$.search(last_query);
    callback();
  });
};
PlayerClient.prototype.updatePlaylist = function(callback){
  var this$ = this;
  callback = callback || noop;
  this.sendCommandName('playlistinfo', function(err, tracks){
    if (err) return callback(err);
    this$.clearPlaylist();
    for (var id in tracks) {
      var item = tracks[id];
      var track = this$.library.track_table[item.file];
      track.time = item.time;
      this$.playlist.item_table[id] = {
        id: id,
        sort_key: item.sort_key,
        is_random: item.is_random,
        track: track,
        playlist: this$.playlist,
      };
    }
    this$.refreshPlaylistList();
    if (this$.status.current_item != null) {
      this$.status.current_item = this$.playlist.item_table[this$.status.current_item.id];
    }
    if (this$.status.current_item != null) {
      this$.emit('playlistupdate');
      callback();
    } else {
      this$.updateStatus(function(err){
        if (err) {
          return callback(err);
        }
        this$.emit('playlistupdate');
        callback();
      });
    }
  });
};
PlayerClient.prototype.updateStoredPlaylists = function(callback){
  var this$ = this;
  callback = callback || noop;
  this.sendCommand("listplaylists", function(err, msg){
    var count, stored_playlist_table, stored_playlist_item_table;
    if (err) {
      return callback(err);
    }
    count = 0;
    stored_playlist_table = {};
    stored_playlist_item_table = {};
    parseWithSepField(msg, 'playlist', {}, function(obj){
      var name;
      name = obj.playlist;
      count += 1;
      updateStoredPlaylist(name, function(err){
        if (count == null) {
          return;
        }
        if (err) {
          count = null;
          return callback(err);
        }
        count -= 1;
        finishUp();
      });
    });
    finishUp();
    function finishUp(){
      var res$, k, v, i, len$, playlist;
      if (count === 0) {
        this$.stored_playlist_table = stored_playlist_table;
        this$.stored_playlist_item_table = stored_playlist_item_table;
        res$ = [];
        for (k in stored_playlist_table) {
          v = stored_playlist_table[k];
          res$.push(v);
        }
        this$.stored_playlists = res$;
        this$.stored_playlists.sort(playlistComparator);
        for (i = 0, len$ = (ref$ = this$.stored_playlists).length; i < len$; ++i) {
          playlist = ref$[i];
          playlist.pos = i;
        }
        callback();
        return this$.emit('storedplaylistupdate');
      }
    }
    function updateStoredPlaylist(name, callback){
      this$.sendCommand("listplaylist \"" + qEscape(name) + "\"", function(err, msg){
        var item_list, item_table, playlist;
        if (err) {
          return callback(err);
        }
        item_list = [];
        item_table = {};
        playlist = {
          name: name,
          item_list: item_list,
          item_table: item_table
        };
        parseWithSepField(msg, 'file', {}, function(item){
          item = {
            track: this$.library.track_table[item.file],
            pos: item_list.length,
            id: nextId(),
            playlist: playlist
          };
          item_list.push(item);
          item_table[item.id] = item;
          stored_playlist_item_table[item.id] = item;
        });
        stored_playlist_table[name] = playlist;
        callback();
      });
    }
  });
};
PlayerClient.prototype.updateStatus = function(callback){
  var this$ = this;
  callback = callback || noop;
  this.sendCommandName('status', function(err, o){
    var ref$;
    if (err) {
      return callback(err);
    }
    ref$ = this$.status;
    ref$.volume = o.volume;
    ref$.repeat = o.repeat;
    ref$.single = o.single;
    ref$.state = o.state;
    this$.status.track_start_date = o.track_start_date != null ? new Date(o.track_start_date) : null;
    this$.status.paused_time = o.paused_time;
  });
  this.sendCommandName('currentsong', function(err, id){
    if (err) return callback(err);
    if (id != null) {
      this$.status.current_item = this$.playlist.item_table[id];
      if (this$.status.current_item != null) {
        this$.status.time = this$.status.current_item.track.time;
        this$.emit('statusupdate');
        callback();
      } else {
        this$.status.current_item = null;
        this$.updatePlaylist(function(err){
          if (err) {
            return callback(err);
          }
          this$.emit('statusupdate');
          callback();
        });
      }
    } else {
      this$.status.current_item = null;
      callback();
      this$.emit('statusupdate');
    }
  });
};
PlayerClient.prototype.search = function(query){
  query = query.trim();
  if (query.length === 0) {
    this.search_results = this.library;
    this.emit('libraryupdate');
    this.last_query = query;
    return;
  }
  var words = formatSearchable(query).split(/\s+/);
  query = words.join(" ");
  if (query === this.last_query) {
    return;
  }
  this.last_query = query;
  var result = [];
  for (var k in this.library.track_table) {
    var track = this.library.track_table[k];
    var is_match = fn$();
    if (is_match) {
      result.push(track);
    }
  }
  this.buildArtistAlbumTree(result, this.search_results = {});
  this.emit('libraryupdate');
  function fn$(){
    var i$, ref$, len$, word;
    for (i$ = 0, len$ = (ref$ = words).length; i$ < len$; ++i$) {
      word = ref$[i$];
      if (track.search_tags.indexOf(word) === -1) {
        return false;
      }
    }
    return true;
  }
};
PlayerClient.prototype.getDefaultQueuePosition = function(){
  var ref$, previous_key, next_key, start_pos, i, to$, track, sort_key;
  previous_key = (ref$ = this.status.current_item) != null ? ref$.sort_key : void 8;
  next_key = null;
  start_pos = ((ref$ = (ref$ = this.status.current_item) != null ? ref$.pos : void 8) != null ? ref$ : -1) + 1;
  for (i = start_pos, to$ = this.playlist.item_list.length; i < to$; ++i) {
    track = this.playlist.item_list[i];
    sort_key = track.sort_key;
    if (track.is_random) {
      next_key = sort_key;
      break;
    }
    previous_key = sort_key;
  }
  return {
    previous_key: previous_key,
    next_key: next_key
  };
};
PlayerClient.prototype.generateSortKey = function(previous_key, next_key){
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
};
PlayerClient.prototype.generateId = function(){
  var result, _, shortHex, __;
  result = "";
  for (_ = 0; _ < 8; ++_) {
    shortHex = (0 | Math.random() * 0x10000).toString(16);
    for (__ = shortHex.length; __ < 4; ++__) {
      result += "0";
    }
    result += shortHex;
  }
  return result;
};
PlayerClient.prototype.queueFiles = function(files, previous_key, next_key, is_random){
  var ref$, items, i$, len$, file, sort_key, id;
  if (!files.length) {
    return;
  }
  is_random = Boolean(is_random);
  if (previous_key == null && next_key == null) {
    ref$ = this.getDefaultQueuePosition(), previous_key = ref$.previous_key, next_key = ref$.next_key;
  }
  items = {};
  for (i$ = 0, len$ = files.length; i$ < len$; ++i$) {
    file = files[i$];
    sort_key = this.generateSortKey(previous_key, next_key);
    id = this.generateId();
    items[id] = {
      file: file,
      sort_key: sort_key,
      is_random: is_random
    };
    this.playlist.item_table[id] = {
      id: id,
      sort_key: sort_key,
      is_random: is_random,
      track: this.library.track_table[file]
    };
    previous_key = sort_key;
  }
  this.refreshPlaylistList();
  this.sendCommand({
    name: 'addid',
    items: items
  });
  this.emit('playlistupdate');
};
PlayerClient.prototype.queueFilesNext = function(files){
  var ref$, previous_key, next_key, i$, len$, track;
  previous_key = (ref$ = this.status.current_item) != null ? ref$.sort_key : void 8;
  next_key = null;
  for (i$ = 0, len$ = (ref$ = this.playlist.item_list).length; i$ < len$; ++i$) {
    track = ref$[i$];
    if (previous_key == null || track.sort_key > previous_key) {
      if (next_key == null || track.sort_key < next_key) {
        next_key = track.sort_key;
      }
    }
  }
  this.queueFiles(files, previous_key, next_key);
};
PlayerClient.prototype.clear = function(){
  this.sendCommandName('clear');
  this.clearPlaylist();
  this.emit('playlistupdate');
};
PlayerClient.prototype.shuffle = function(){
  this.sendCommandName('shuffle');
};
PlayerClient.prototype.stop = function(){
  this.sendCommandName('stop');
  this.status.state = "stop";
  this.emit('statusupdate');
};
PlayerClient.prototype.play = function(){
  this.sendCommandName('play');
  if (this.status.state === "pause") {
    this.status.track_start_date = elapsedToDate(this.status.paused_time);
    this.status.state = "play";
    this.emit('statusupdate');
  }
};
PlayerClient.prototype.pause = function(){
  this.sendCommandName('pause');
  if (this.status.state === "play") {
    this.status.paused_time = dateToElapsed(this.status.track_start_date);
    this.status.state = "pause";
    this.emit('statusupdate');
  }
};
PlayerClient.prototype.next = function(){
  var ref$, current_pos, pos, id;
  current_pos = (ref$ = this.status.current_item) != null ? ref$.pos : void 8;
  if (current_pos != null) {
    pos = current_pos + 1;
  } else {
    pos = 0;
  }
  id = (ref$ = this.playlist.item_list[pos]) != null ? ref$.id : void 8;
  this.playId(id);
};
PlayerClient.prototype.prev = function(){
  var ref$, current_pos, pos, id;
  current_pos = (ref$ = this.status.current_item) != null ? ref$.pos : void 8;
  if (current_pos != null) {
    pos = current_pos - 1;
  } else {
    pos = this.playlist.item_list.length - 1;
  }
  id = (ref$ = this.playlist.item_list[pos]) != null ? ref$.id : void 8;
  this.playId(id);
};
PlayerClient.prototype.playId = function(track_id){
  this.sendCommand({
    name: 'playid',
    track_id: track_id
  });
  this.anticipatePlayId(track_id);
};
PlayerClient.prototype.moveIds = function(track_ids, previous_key, next_key){
  var res$, i$, len$, id, track, tracks, items, sort_key;
  res$ = [];
  for (i$ = 0, len$ = track_ids.length; i$ < len$; ++i$) {
    id = track_ids[i$];
    if ((track = this.playlist.item_table[id]) != null) {
      res$.push(track);
    }
  }
  tracks = res$;
  tracks.sort(compareSortKeyAndId);
  items = {};
  for (i$ = 0, len$ = tracks.length; i$ < len$; ++i$) {
    track = tracks[i$];
    sort_key = this.generateSortKey(previous_key, next_key);
    items[id] = {
      sort_key: sort_key
    };
    track.sort_key = sort_key;
    previous_key = sort_key;
  }
  this.refreshPlaylistList();
  this.sendCommand({
    name: 'move',
    items: items
  });
  this.emit('playlistupdate');
};
PlayerClient.prototype.shiftIds = function(track_id_set, offset){
  var items, previous_key, next_key, i$, ref$, len$, track, sort_key;
  items = {};
  previous_key = null;
  next_key = null;
  if (offset < 0) {
    for (i$ = 0, len$ = (ref$ = this.playlist.item_list).length; i$ < len$; ++i$) {
      track = ref$[i$];
      if (track.id in track_id_set) {
        if (next_key == null) {
          continue;
        }
        sort_key = this.generateSortKey(previous_key, next_key);
        items[track.id] = {
          sort_key: sort_key
        };
        track.sort_key = sort_key;
      }
      previous_key = next_key;
      next_key = track.sort_key;
    }
  } else {
    for (i$ = (ref$ = this.playlist.item_list).length - 1; i$ >= 0; --i$) {
      track = ref$[i$];
      if (track.id in track_id_set) {
        if (previous_key == null) {
          continue;
        }
        sort_key = this.generateSortKey(previous_key, next_key);
        items[track.id] = {
          sort_key: sort_key
        };
        track.sort_key = sort_key;
      }
      next_key = previous_key;
      previous_key = track.sort_key;
    }
  }
  this.refreshPlaylistList();
  this.sendCommand({
    name: 'move',
    items: items
  });
  this.emit('playlistupdate');
};
PlayerClient.prototype.removeIds = function(track_ids){
  var ids, i$, len$, track_id, ref$, item;
  if (track_ids.length === 0) {
    return;
  }
  ids = [];
  for (i$ = 0, len$ = track_ids.length; i$ < len$; ++i$) {
    track_id = track_ids[i$];
    if (((ref$ = this.status.current_item) != null ? ref$.id : void 8) === track_id) {
      this.status.current_item = null;
    }
    ids.push(track_id);
    item = this.playlist.item_table[track_id];
    delete this.playlist.item_table[item.id];
    this.refreshPlaylistList();
  }
  this.sendCommand({
    name: 'deleteid',
    ids: ids
  });
  this.emit('playlistupdate');
};
PlayerClient.prototype.seek = function(pos){
  pos = parseFloat(pos, 10);
  if (pos < 0) pos = 0;
  if (pos > this.status.time) pos = this.status.time;
  this.sendCommand({
    name: 'seek',
    pos: pos
  });
  this.status.track_start_date = elapsedToDate(pos);
  this.emit('statusupdate');
};
PlayerClient.prototype.setVolume = function(vol){
  vol = toMpdVol(vol);
  this.sendCommand("setvol " + vol);
  this.status.volume = fromMpdVol(vol);
  this.emit('statusupdate');
};
PlayerClient.prototype.changeStatus = function(arg$){
  var repeat, single;
  repeat = arg$.repeat, single = arg$.single;
  this.status.repeat = repeat;
  this.status.single = single;
  this.sendCommand({
    name: 'repeat',
    repeat: repeat,
    single: single
  });
  this.emit('statusupdate');
};
PlayerClient.prototype.getFileInfo = function(file, callback){
  var this$ = this;
  callback = callback || noop;
  this.sendCommand("lsinfo \"" + qEscape(file) + "\"", function(err, track){
    if (err) {
      return callback(err);
    }
    callback(null, track);
  });
};
PlayerClient.prototype.authenticate = function(password, callback){
  var this$ = this;
  callback = callback || noop;
  this.sendCommand({
    name: 'password',
    password: password
  }, function(err){
    callback(err);
  });
};
PlayerClient.prototype.scanFiles = function(files){
  var file;
  this.sendCommands((function(){
    var i$, ref$, len$, results$ = [];
    for (i$ = 0, len$ = (ref$ = files).length; i$ < len$; ++i$) {
      file = ref$[i$];
      results$.push("update \"" + qEscape(file) + "\"");
    }
    return results$;
  }()));
};
PlayerClient.prototype.queueFilesInStoredPlaylist = function(files, stored_playlist_name){
  var file;
  this.sendCommands((function(){
    var i$, ref$, len$, results$ = [];
    for (i$ = 0, len$ = (ref$ = files).length; i$ < len$; ++i$) {
      file = ref$[i$];
      results$.push("playlistadd \"" + qEscape(stored_playlist_name) + "\" \"" + qEscape(file) + "\"");
    }
    return results$;
  }()));
};
PlayerClient.prototype.sendCommands = function(command_list, callback){
  callback = callback || noop;
  if (command_list.length === 0) return;
  this.sendCommand("command_list_begin\n" + command_list.join("\n") + "\ncommand_list_end", callback);
};
PlayerClient.prototype.sendCommandName = function(name, cb){
  cb = cb || noop;
  this.sendCommand({
    name: name
  }, cb);
};
PlayerClient.prototype.sendCommand = function(cmd, cb){
  cb = cb || noop;
  var callback_id = this.next_response_handler_id++;
  this.response_handlers[callback_id] = cb;
  this.socket.emit('request', JSON.stringify({
    cmd: cmd,
    callback_id: callback_id
  }));
};
PlayerClient.prototype.handleResponse = function(arg){
  var err, msg, callback_id, handler;
  err = arg.err, msg = arg.msg, callback_id = arg.callback_id;
  handler = this.response_handlers[callback_id];
  delete this.response_handlers[callback_id];
  handler(err, msg);
};
PlayerClient.prototype.handleStatus = function(systems){
  for (var i = 0; i < systems.length; i += 1) {
    var system = systems[i];
    var updateFunc = this.updateFuncs[system];
    if (updateFunc) updateFunc();
  }
};
PlayerClient.prototype.clearPlaylist = function(){
  this.playlist = {
    item_list: [],
    item_table: {},
    pos: null,
    name: null
  };
};
PlayerClient.prototype.anticipatePlayId = function(track_id){
  var item = this.playlist.item_table[track_id];
  this.status.current_item = item;
  this.status.state = "play";
  this.status.time = item.track.time;
  this.status.track_start_date = new Date();
  this.emit('statusupdate');
};
PlayerClient.prototype.anticipateSkip = function(direction){
  var ref$, next_item;
  next_item = this.playlist.item_list[((ref$ = this.status.current_item) != null ? ref$.pos : void 8) + direction];
  if (next_item != null) {
    this.anticipatePlayId(next_item.id);
  }
};
PlayerClient.prototype.buildArtistAlbumTree = function(tracks, library){
  var i$, len$, track, album_key, album, artist_table, k, album_artists, i, ref1$, album_artist_name, artist_key, artist, various_artist;
  library.track_table = {};
  library.album_table = {};
  for (i$ = 0, len$ = tracks.length; i$ < len$; ++i$) {
    track = tracks[i$];
    library.track_table[track.file] = track;
    album_key = albumKey(track);
    album = getOrCreate(album_key, library.album_table, fn$);
    track.album = album;
    album.tracks.push(track);
    if (album.year == null) {
      album.year = track.year;
    }
  }
  artist_table = {};
  for (k in library.album_table) {
    album = library.album_table[k];
    album_artists = {};
    album.tracks.sort(trackComparator);
    for (i = 0, len$ = (ref1$ = album.tracks).length; i < len$; ++i) {
      track = ref1$[i];
      track.pos = i;
      album_artist_name = track.album_artist_name;
      album_artists[artistKey(album_artist_name)] = true;
      album_artists[artistKey(track.artist_name)] = true;
    }
    if (moreThanOneKey(album_artists)) {
      album_artist_name = VARIOUS_ARTISTS_NAME;
      artist_key = VARIOUS_ARTISTS_KEY;
      for (i$ = 0, len$ = (ref1$ = album.tracks).length; i$ < len$; ++i$) {
        track = ref1$[i$];
        track.artist_disambiguation = track.artist_name;
      }
    } else {
      artist_key = artistKey(album_artist_name);
    }
    artist = getOrCreate(artist_key, artist_table, fn1$);
    album.artist = artist;
    artist.albums.push(album);
  }
  library.artists = [];
  various_artist = null;
  for (k in artist_table) {
    artist = artist_table[k];
    artist.albums.sort(albumComparator);
    for (i = 0, len$ = (ref$ = artist.albums).length; i < len$; ++i) {
      album = ref$[i];
      album.pos = i;
    }
    if (artist.key === VARIOUS_ARTISTS_KEY) {
      various_artist = artist;
    } else {
      library.artists.push(artist);
    }
  }
  library.artists.sort(artistComparator);
  if (various_artist != null) {
    library.artists.splice(0, 0, various_artist);
  }
  for (i = 0, len$ = (ref$ = library.artists).length; i < len$; ++i) {
    artist = ref$[i];
    artist.pos = i;
  }
  library.artist_table = artist_table;
  function fn$(){
    return {
      name: track.album_name,
      year: track.year,
      tracks: [],
      key: album_key
    };
  }
  function fn1$(){
    return {
      name: album_artist_name,
      albums: [],
      key: artist_key
    };
  }
};
PlayerClient.prototype.refreshPlaylistList = function(){
  var id, item, i, len$;
  this.playlist.item_list = [];
  for (id in this.playlist.item_table) {
    item = this.playlist.item_table[id];
    item.playlist = this.playlist;
    this.playlist.item_list.push(item);
  }
  this.playlist.item_list.sort(compareSortKeyAndId);
  for (i = 0, len$ = (ref$ = this.playlist.item_list).length; i < len$; ++i) {
    item = ref$[i];
    item.pos = i;
  }
};
PlayerClient.prototype.resetServerState = function(){
  this.buffer = "";
  this.response_handlers = {};
  this.next_response_handler_id = 0;
  this.have_file_list_cache = false;
  this.library = {
    artists: [],
    track_table: {}
  };
  this.search_results = this.library;
  this.last_query = "";
  this.clearPlaylist();
  this.status = {
    current_item: null
  };
  this.stored_playlist_table = {};
  this.stored_playlist_item_table = {};
  this.stored_playlists = [];
};
