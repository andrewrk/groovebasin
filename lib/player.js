PREFIXES_TO_STRIP = [/^\s*the\s+/, /^\s*a\s+/, /^\s*an\s+/];
function stripPrefixes(str){
  var i$, ref$, len$, regex;
  for (i$ = 0, len$ = (ref$ = PREFIXES_TO_STRIP).length; i$ < len$; ++i$) {
    regex = ref$[i$];
    str = str.replace(regex, '');
    break;
  }
  return str;
}
MPD_SENTINEL = /^(OK|ACK|list_OK)(.*)$/m;
trim = (__trim = String.prototype.trim) != null
  ? function(text){
    return __trim.call(text);
  }
  : (__trimLeft = /^\s+/, __trimRight = /\s+$/, function(text){
    return text.replace(__trimLeft, "").replace(__trimRight, "");
  });
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
  return stripPrefixes(removeDiacritics(title));
}
function titleCompare(a, b){
  var _a, _b;
  _a = sortableTitle(a);
  _b = sortableTitle(b);
  if (_a < _b) {
    return -1;
  } else if (_a > _b) {
    return 1;
  } else {
    // At this point we compare the original strings. Our cache update code
    // depends on this behavior.
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
  if (err) {
    throw err;
  }
}
function qEscape(str){
  // replace all " with \"
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
  var current_obj, i$, ref$, len$, line, ref1$, key, value;
  if (msg === "") {
    return [];
  }
  current_obj = null;
  function flushCurrentObj(){
    if (current_obj != null) {
      flush(current_obj);
    }
    current_obj = {};
  }
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
}
function getOrCreate(key, table, initObjFunc){
  var result;
  result = table[key];
  if (result == null) {
    result = initObjFunc();
    // insert into table
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
    return removeDiacritics(track.album_name);
  } else {
    return removeDiacritics(track.album_artist_name);
  }
}
function artistKey(artist_name){
  return removeDiacritics(artist_name);
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
function firstKey(object){
  var k;
  for (k in object) {
    return k;
  }
  return null;
}
next_id = 0;
function nextId(){
  return "id-" + next_id++;
}
if ((EventEmitter = typeof require == 'function' ? (ref$ = require('events')) != null ? ref$.EventEmitter : void 8 : void 8) == null) {
  EventEmitter = (function(){
    EventEmitter.displayName = 'EventEmitter';
    var prototype = EventEmitter.prototype, constructor = EventEmitter;
    constructor.count = 0;
    function EventEmitter(){
      var this$ = this instanceof ctor$ ? this : new ctor$;
      this$.event_handlers = {};
      this$.next_id = 0;
      this$.prop = "__EventEmitter_" + constructor.count++ + "_id";
      return this$;
    } function ctor$(){} ctor$.prototype = prototype;
    prototype.on = function(event_name, handler){
      var ref$;
      handler[this.prop] = this.next_id;
      ((ref$ = this.event_handlers)[event_name] || (ref$[event_name] = {}))[this.next_id] = handler;
      this.next_id += 1;
    };
    prototype.removeListener = function(event_name, handler){
      var ref$;
      delete ((ref$ = this.event_handlers)[event_name] || (ref$[event_name] = {}))[handler[this.prop]];
    };
    prototype.emit = function(event_name){
      var args, id, ref$, ref1$, h;
      args = slice$.call(arguments, 1);
      for (id in ref$ = (ref1$ = this.event_handlers)[event_name] || (ref1$[event_name] = {})) {
        h = ref$[id];
        h.apply(null, args);
      }
    };
    return EventEmitter;
  }());
}
Player = (function(superclass){
  Player.displayName = 'Player';
  var prototype = extend$(Player, superclass).prototype, constructor = Player;
  constructor.trackNameFromFile = function(filename){
    var filetitle, dot, len;
    filetitle = filename.substr(filename.lastIndexOf('/') + 1);
    len = (dot = filetitle.lastIndexOf('.')) >= 0
      ? dot
      : filetitle.length;
    return filetitle.substr(0, len);
  };
  constructor.addSearchTags = function(tracks){
    var i$, len$, track;
    for (i$ = 0, len$ = tracks.length; i$ < len$; ++i$) {
      track = tracks[i$];
      track.search_tags = removeDiacritics([track.artist_name, track.album_artist_name, track.album_name, track.name, track.file].join("\n"));
    }
  };
  // this key is unique because it has capital letters
  constructor.VARIOUS_ARTISTS_KEY = "VariousArtists";
  function Player(){
    var this$ = this instanceof ctor$ ? this : new ctor$;
    superclass.call(this$);
    // options the user can toggle
    this$.various_artists_name = "Various Artists";
    this$.resetServerState();
    // maps mpd subsystems to our function to call which will update ourself
    this$.updateFuncs = {
      database: function(){ // the song database has been modified after update.
        this$.have_file_list_cache = false;
        return this$.updateLibrary();
      },
      update: noop, // a database update has started or finished. If the database was modified during the update, the database event is also emitted.
      stored_playlist: bind$(this$, 'updateStoredPlaylists'), // a stored playlist has been modified, renamed, created or deleted
      playlist: bind$(this$, 'updatePlaylist'), // the current playlist has been modified
      player: bind$(this$, 'updateStatus'), // the player has been started, stopped or seeked
      mixer: bind$(this$, 'updateStatus'), // the volume has been changed
      output: noop, // an audio output has been enabled or disabled
      options: bind$(this$, 'updateStatus'), // options like repeat, random, crossfade, replay gain
      sticker: function(){ // the sticker database has been modified.
        return this$.emit('stickerupdate');
      },
      subscription: noop, // a client has subscribed or unsubscribed to a channel
      message: noop // a message was received on a channel this client is subscribed to; this event is only emitted when the queue is empty
    };
    return this$;
  } function ctor$(){} ctor$.prototype = prototype;
  prototype.handleConnectionStart = function(){
    this.updateLibrary();
    this.updateStatus();
    this.updatePlaylist();
    this.updateStoredPlaylists();
  };
  prototype.sendCommands = function(command_list, callback){
    callback == null && (callback = noop);
    if (command_list.length === 0) {
      return;
    }
    this.sendCommand("command_list_begin\n" + command_list.join("\n") + "\ncommand_list_end", callback);
  };
  prototype.updateLibrary = function(callback){
    var this$ = this;
    callback == null && (callback = noop);
    this.sendCommand('listallinfo', function(err, tracks){
      var last_query;
      if (err) {
        return callback(err);
      }
      constructor.addSearchTags(tracks);
      this$.buildArtistAlbumTree(tracks, this$.library);
      this$.have_file_list_cache = true;
      // in case the user has a search open, we'll apply their search again.
      last_query = this$.last_query;
      // reset last query so that search is forced to run again
      this$.last_query = "";
      this$.search(last_query);
      callback();
    });
  };
  prototype.updatePlaylist = function(callback){
    var this$ = this;
    callback == null && (callback = noop);
    this.sendCommand("playlistinfo", function(err, tracks){
      var i$, len$, ref$, id, file, obj;
      if (err) {
        return callback(err);
      }
      this$.clearPlaylist();
      for (i$ = 0, len$ = tracks.length; i$ < len$; ++i$) {
        ref$ = tracks[i$], id = ref$.id, file = ref$.file;
        obj = {
          id: id,
          track: this$.library.track_table[file],
          pos: this$.playlist.item_list.length,
          playlist: this$.playlist
        };
        this$.playlist.item_list.push(obj);
        this$.playlist.item_table[id] = obj;
      }
      // make sure current track data is correct
      if (this$.status.current_item != null) {
        this$.status.current_item = this$.playlist.item_table[this$.status.current_item.id];
      }
      if (this$.status.current_item != null) {
        // looks good, notify listeners
        this$.emit('playlistupdate');
        callback();
      } else {
        // we need a status update before raising a playlist update event
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
  prototype.updateStoredPlaylists = function(callback){
    var this$ = this;
    callback == null && (callback = noop);
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
            return cb(err);
          }
          count -= 1;
          finishUp();
        });
      });
      finishUp();
      function finishUp(){
        var res$, k, ref$, v, i, len$, playlist;
        if (count === 0) {
          this$.stored_playlist_table = stored_playlist_table;
          this$.stored_playlist_item_table = stored_playlist_item_table;
          res$ = [];
          for (k in ref$ = stored_playlist_table) {
            v = ref$[k];
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
  prototype.updateStatus = function(callback){
    var this$ = this;
    callback == null && (callback = noop);
    this.sendCommand("status", function(err, o){
      var ref$;
      if (err) {
        return callback(err);
      }
      ref$ = this$.status;
      ref$.volume = fromMpdVol(o.volume);
      ref$.repeat = !!parseInt(o.repeat, 2);
      ref$.random = !!parseInt(o.random, 2);
      ref$.single = !!parseInt(o.single, 2);
      ref$.consume = !!parseInt(o.consume, 2);
      ref$.state = o.state;
      ref$.time = null;
      ref$.bitrate = null;
      ref$.track_start_date = null;
      if (o.bitrate != null) {
        this$.status.bitrate = parseInt(o.bitrate, 10);
      }
      if (o.time != null && o.elapsed != null) {
        this$.status.time = parseInt(o.time.split(":")[1], 10);
        // we still add elapsed for when its paused
        this$.status.elapsed = parseFloat(o.elapsed, 10);
        // add a field for the start date of the current track
        this$.status.track_start_date = elapsedToDate(this$.status.elapsed);
      }
    });
    this.sendCommand("currentsong", function(err, track){
      var id, pos, file;
      if (err) {
        return callback(err);
      }
      if (track != null) {
        id = track.id, pos = track.pos, file = track.file;
        this$.status.current_item = this$.playlist.item_table[id];
        if (this$.status.current_item != null && this$.status.current_item.pos === pos) {
          this$.status.current_item.track = this$.library.track_table[file];
          // looks good, notify listeners
          this$.emit('statusupdate');
          callback();
        } else {
          // missing or inconsistent playlist data, need to get playlist update
          this$.status.current_item = {
            id: id,
            pos: pos,
            track: this$.library.track_table[file]
          };
          this$.updatePlaylist(function(err){
            if (err) {
              return callback(err);
            }
            this$.emit('statusupdate');
            callback();
          });
        }
      } else {
        // no current song
        this$.status.current_item = null;
        callback();
        this$.emit('statusupdate');
      }
    });
  };
  // puts the search results in search_results
  prototype.search = function(query){
    var words, result, k, ref$, track, is_match;
    query = trim(query);
    if (query.length === 0) {
      this.search_results = this.library;
      this.emit('libraryupdate');
      this.last_query = query;
      return;
    }
    words = removeDiacritics(query).split(/\s+/);
    query = words.join(" ");
    if (query === this.last_query) {
      return;
    }
    this.last_query = query;
    result = [];
    for (k in ref$ = this.library.track_table) {
      track = ref$[k];
      is_match = fn$();
      if (is_match) {
        result.push(track);
      }
    }
    // zip results into album
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
  prototype.queueFiles = function(files, pos, callback){
    var cmds, i$, file, res$, len$, items, ref$, this$ = this;
    pos == null && (pos = this.playlist.item_list.length);
    callback == null && (callback = noop);
    if (!files.length) {
      return callback(null, []);
    }
    cmds = [];
    for (i$ = files.length - 1; i$ >= 0; --i$) {
      file = files[i$];
      cmds.push("addid \"" + qEscape(file) + "\" " + pos);
    }
    res$ = [];
    for (i$ = 0, len$ = files.length; i$ < len$; ++i$) {
      file = files[i$];
      res$.push({
        id: null,
        pos: null,
        track: this.library.track_table[file]
      });
    }
    items = res$;
    (ref$ = this.playlist.item_list).splice.apply(ref$, [pos, 0].concat(slice$.call(items)));
    this.fixPlaylistPosCache();
    this.sendCommands(cmds, function(err, msg){
      var i, ref$, len$, line, index, item_id;
      if (err) {
        return callback(err);
      }
      for (i = 0, len$ = (ref$ = msg.split("\n")).length; i < len$; ++i) {
        line = ref$[i];
        if (!line) {
          continue;
        }
        index = files.length - 1 - i;
        item_id = parseInt(line.substring(4), 10);
        items[index].id = item_id;
      }
      callback(null, items);
    });
    this.emit('playlistupdate');
  };
  prototype.queueFile = function(file, pos, callback){
    this.queueFiles([file], pos, callback);
  };
  prototype.queueFilesNext = function(files){
    var ref$, new_pos;
    new_pos = ((ref$ = (ref$ = this.status.current_item) != null ? ref$.pos : void 8) != null
      ? ref$
      : -1) + 1;
    this.queueFiles(files, new_pos);
  };
  prototype.queueFileNext = function(file){
    this.queueFilesNext([file]);
  };
  prototype.clear = function(){
    this.sendCommand("clear");
    this.clearPlaylist();
    this.emit('playlistupdate');
  };
  prototype.shuffle = function(){
    this.sendCommand("shuffle");
  };
  prototype.stop = function(){
    this.sendCommand("stop");
    this.status.state = "stop";
    this.emit('statusupdate');
  };
  prototype.play = function(){
    this.sendCommand("play");
    if (this.status.state === "pause") {
      this.status.track_start_date = elapsedToDate(this.status.elapsed);
      this.status.state = "play";
      this.emit('statusupdate');
    }
  };
  prototype.pause = function(){
    this.sendCommand("pause 1");
    if (this.status.state === "play") {
      this.status.elapsed = dateToElapsed(this.status.track_start_date);
      this.status.state = "pause";
      this.emit('statusupdate');
    }
  };
  prototype.next = function(){
    // if mpd is stopped, it will ignore our request. so we must play, then skip, then stop.
    if (this.status.state === "stop") {
      this.sendCommand("play");
      this.sendCommand("next");
      this.sendCommand("stop");
    } else {
      this.sendCommand("next");
    }
    this.anticipateSkip(1);
  };
  prototype.prev = function(){
    // if mpd is stopped, it will ignore our request. so we must play, then skip, then stop.
    if (this.status.state === "stop") {
      this.sendCommand("play");
      this.sendCommand("previous");
      this.sendCommand("stop");
    } else {
      this.sendCommand("previous");
    }
    this.anticipateSkip(-1);
  };
  prototype.playId = function(track_id){
    track_id = parseInt(track_id, 10);
    this.sendCommand("playid " + qEscape(track_id));
    this.anticipatePlayId(track_id);
  };
  prototype.moveIds = function(track_ids, pos){
    var res$, i$, len$, id, item, items, cmds, real_pos;
    pos = parseInt(pos, 10);
    // get the playlist items for the ids
    res$ = [];
    for (i$ = 0, len$ = track_ids.length; i$ < len$; ++i$) {
      id = track_ids[i$];
      if ((item = this.playlist.item_table[id]) != null) {
        res$.push(item);
      }
    }
    items = res$;
    // sort the list by the reverse order in the playlist
    items.sort(function(a, b){
      return b.pos - a.pos;
    });
    cmds = [];
    while (items.length > 0) {
      if (pos <= items[0].pos) {
        real_pos = pos;
        item = items.shift();
      } else {
        real_pos = pos - 1;
        item = items.pop();
      }
      cmds.push("moveid " + item.id + " " + real_pos);
      this.playlist.item_list.splice(item.pos, 1);
      this.playlist.item_list.splice(real_pos, 0, item);
      this.fixPlaylistPosCache();
    }
    this.sendCommands(cmds);
    this.emit('playlistupdate');
  };
  // shifts the list of ids by offset, winamp style
  prototype.shiftIds = function(track_ids, offset){
    var res$, i$, len$, id, item, items, ref$, new_pos;
    offset = parseInt(offset, 10);
    if (offset === 0 || track_ids.length === 0) {
      return;
    }
    res$ = [];
    for (i$ = 0, len$ = track_ids.length; i$ < len$; ++i$) {
      id = track_ids[i$];
      if ((item = this.playlist.item_table[id]) != null) {
        res$.push(item);
      }
    }
    items = res$;
    items.sort(function(a, b){
      return sign(offset) * (b.pos - a.pos);
    });
    // abort if any are out of bounds
    for (i$ = 0, len$ = (ref$ = [items[0], items[items.length - 1]]).length; i$ < len$; ++i$) {
      item = ref$[i$];
      new_pos = item.pos + offset;
      if (new_pos < 0 || new_pos >= this.playlist.item_list.length) {
        return;
      }
    }
    this.sendCommands((function(){
      var i$, ref$, len$, results$ = [];
      for (i$ = 0, len$ = (ref$ = items).length; i$ < len$; ++i$) {
        item = ref$[i$];
        results$.push("moveid " + item.id + " " + (item.pos + offset));
      }
      return results$;
    }()));
    // anticipate the result
    for (i$ = 0, len$ = items.length; i$ < len$; ++i$) {
      item = items[i$];
      this.playlist.item_list.splice(item.pos, 1);
      this.playlist.item_list.splice(item.pos + offset, 0, item);
      this.fixPlaylistPosCache();
    }
    this.emit('playlistupdate');
  };
  prototype.removeIds = function(track_ids, callback){
    var cmds, i$, len$, track_id, ref$, item, this$ = this;
    callback == null && (callback = noop);
    if (track_ids.length === 0) {
      return callback();
    }
    cmds = [];
    for (i$ = 0, len$ = track_ids.length; i$ < len$; ++i$) {
      track_id = track_ids[i$];
      track_id = parseInt(track_id, 10);
      if (((ref$ = this.status.current_item) != null ? ref$.id : void 8) === track_id) {
        this.anticipateSkip(1);
        if (this.status.state !== "play") {
          this.status.state = "stop";
        }
      }
      cmds.push("deleteid " + qEscape(track_id));
      item = this.playlist.item_table[track_id];
      delete this.playlist.item_table[item.id];
      this.playlist.item_list.splice(item.pos, 1);
      this.fixPlaylistPosCache();
    }
    this.sendCommands(cmds, function(err){
      return callback(err);
    });
    this.emit('playlistupdate');
  };
  prototype.removeId = function(track_id, callback){
    callback == null && (callback = noop);
    this.removeIds([track_id], callback);
  };
  // in seconds
  prototype.seek = function(pos){
    pos = parseFloat(pos, 10);
    if (pos < 0) {
      pos = 0;
    }
    if (pos > this.status.time) {
      pos = this.status.time;
    }
    this.sendCommand("seekid " + this.status.current_item.id + " " + Math.round(pos));
    this.status.track_start_date = elapsedToDate(pos);
    this.emit('statusupdate');
  };
  // between 0 and 1
  prototype.setVolume = function(vol){
    vol = toMpdVol(vol);
    this.sendCommand("setvol " + vol);
    this.status.volume = fromMpdVol(vol);
    this.emit('statusupdate');
  };
  prototype.changeStatus = function(status){
    var cmds;
    cmds = [];
    if (status.consume != null) {
      this.status.consume = status.consume;
      cmds.push("consume " + Number(status.consume));
    }
    if (status.random != null) {
      this.status.random = status.random;
      cmds.push("random " + Number(status.random));
    }
    if (status.repeat != null) {
      this.status.repeat = status.repeat;
      cmds.push("repeat " + Number(status.repeat));
    }
    if (status.single != null) {
      this.status.single = status.single;
      cmds.push("single " + Number(status.single));
    }
    this.sendCommands(cmds);
    this.emit('statusupdate');
  };
  prototype.getFileInfo = function(file, callback){
    var this$ = this;
    callback == null && (callback = noop);
    this.sendCommand("lsinfo \"" + qEscape(file) + "\"", function(err, track){
      if (err) {
        return callback(err);
      }
      callback(null, track);
    });
  };
  prototype.authenticate = function(password, callback){
    var this$ = this;
    callback == null && (callback = noop);
    this.sendCommand("password \"" + qEscape(password) + "\"", function(err){
      callback(err);
    });
  };
  prototype.scanFiles = function(files){
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
  prototype.findStickers = function(dir, name, cb){
    var this$ = this;
    cb == null && (cb = noop);
    this.sendCommand("sticker find song \"" + qEscape(dir) + "\" \"" + qEscape(name) + "\"", function(err, msg){
      var current_file, stickers, i$, ref$, len$, line, ref1$, name, value;
      if (err) {
        return cb(err);
      }
      current_file = null;
      stickers = {};
      for (i$ = 0, len$ = (ref$ = msg.split("\n")).length; i$ < len$; ++i$) {
        line = ref$[i$];
        ref1$ = splitOnce(line, ": "), name = ref1$[0], value = ref1$[1];
        if (name === "file") {
          current_file = value;
        } else if (name === "sticker") {
          if (current_file == null) {
            return cb("protocol");
          }
          value = splitOnce(value, "=")[1];
          stickers[current_file] = value;
        }
      }
      cb(null, stickers);
    });
  };
  prototype.setStickers = function(files, name, value, cb){
    var res$, i$, len$, file, cmds, this$ = this;
    cb == null && (cb = noop);
    res$ = [];
    for (i$ = 0, len$ = files.length; i$ < len$; ++i$) {
      file = files[i$];
      res$.push("sticker set song \"" + qEscape(file) + "\" \"" + qEscape(name) + "\" \"" + qEscape(value) + "\"");
    }
    cmds = res$;
    this.sendCommands(cmds, function(err){
      return cb(err);
    });
  };
  prototype.setSticker = function(file, name, value, cb){
    cb == null && (cb = noop);
    this.setStickers([file], name, value, cb);
  };
  prototype.queueFilesInStoredPlaylist = function(files, stored_playlist_name, pos, callback){
    var esc_name, stored_playlist, cmds, pl_length, i$, file, len$;
    callback == null && (callback = noop);
    if (!files.length) {
      return callback(null, []);
    }
    esc_name = qEscape(stored_playlist_name);
    stored_playlist = this.stored_playlist_table[stored_playlist_name];
    cmds = [];
    if (stored_playlist != null) {
      pl_length = stored_playlist.item_list.length;
      pos || (pos = pl_length);
      for (i$ = files.length - 1; i$ >= 0; --i$) {
        file = files[i$];
        cmds.push("playlistadd \"" + esc_name + "\" \"" + qEscape(file) + "\"");
        cmds.push("playlistmove \"" + esc_name + "\" " + pl_length + " " + pos);
        pl_length += 1;
      }
    } else {
      // this playlist doesn't exist yet.
      for (i$ = 0, len$ = files.length; i$ < len$; ++i$) {
        file = files[i$];
        cmds.push("playlistadd \"" + esc_name + "\" \"" + qEscape(file) + "\"");
      }
    }
    this.sendCommands(cmds, function(err){
      if (err) {
        return callback(err);
      }
      callback();
    });
  };
  prototype.queueFileInStoredPlaylist = function(file, stored_playlist_name, pos, callback){
    this.queueFilesInStoredPlaylist([file], stored_playlist_name, pos, callback);
  };
  prototype.createStoredPlaylist = function(name, callback){
    var any_file, esc_name, cmds;
    callback == null && (callback = noop);
    any_file = firstKey(this.library.track_table);
    esc_name = qEscape(name);
    cmds = ["playlistadd \"" + esc_name + "\" \"" + qEscape(any_file) + "\"", "playlistclear \"" + esc_name + "\""];
    this.sendCommands(cmds, function(err){
      if (err) {
        return callback(err);
      }
      callback();
    });
  };
  prototype.sendCommand = function(cmd, cb){
    cb == null && (cb = noop);
    this.msg_handler_queue.push(cb);
    this.emit('request', cmd);
  };
  prototype.handleResponse = function(arg){
    var err, msg, handler;
    err = arg.err, msg = arg.msg;
    handler = this.msg_handler_queue.shift();
    handler(err, msg);
  };
  prototype.handleStatus = function(systems){
    var i$, len$, system, ref$, updateFunc;
    for (i$ = 0, len$ = systems.length; i$ < len$; ++i$) {
      system = systems[i$];
      updateFunc = (ref$ = this.updateFuncs[system]) != null ? ref$ : noop;
      updateFunc();
    }
  };
  prototype.clearPlaylist = function(){
    this.playlist = {
      item_list: [],
      item_table: {},
      pos: null,
      name: null
    };
  };
  prototype.anticipatePlayId = function(track_id){
    var item;
    item = this.playlist.item_table[track_id];
    this.status.current_item = item;
    this.status.state = "play";
    this.status.time = item.track.time;
    this.status.track_start_date = new Date();
    this.emit('statusupdate');
  };
  prototype.anticipateSkip = function(direction){
    var next_item;
    next_item = this.playlist.item_list[this.status.current_item.pos + direction];
    if (next_item != null) {
      this.anticipatePlayId(next_item.id);
    }
  };
  prototype.buildArtistAlbumTree = function(tracks, library){
    var i$, len$, track, album_key, album, artist_table, k, ref$, album_artists, i, ref1$, album_artist_name, artist_key, artist, various_artist;
    // determine set of albums
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
    // find compilation albums and create artist objects
    artist_table = {};
    for (k in ref$ = library.album_table) {
      // count up all the artists and album artists mentioned in this album
      album = ref$[k];
      album_artists = {};
      album.tracks.sort(trackComparator);
      for (i = 0, len$ = (ref1$ = album.tracks).length; i < len$; ++i) {
        // cache the track indexes
        track = ref1$[i];
        track.pos = i;
        album_artist_name = track.album_artist_name;
        album_artists[artistKey(album_artist_name)] = true;
        album_artists[artistKey(track.artist_name)] = true;
      }
      if (moreThanOneKey(album_artists)) {
        // multiple artists. we're sure it's a compilation album.
        album_artist_name = this.various_artists_name;
        artist_key = constructor.VARIOUS_ARTISTS_KEY;
        // make sure to disambiguate the artist names
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
    // collect list of artists and sort albums
    library.artists = [];
    various_artist = null;
    for (k in artist_table) {
      artist = artist_table[k];
      artist.albums.sort(albumComparator);
      // cache the album indexes
      for (i = 0, len$ = (ref$ = artist.albums).length; i < len$; ++i) {
        album = ref$[i];
        album.pos = i;
      }
      if (artist.key === constructor.VARIOUS_ARTISTS_KEY) {
        various_artist = artist;
      } else {
        library.artists.push(artist);
      }
    }
    // sort artists
    library.artists.sort(artistComparator);
    // various artists goes first
    if (various_artist != null) {
      library.artists.splice(0, 0, various_artist);
    }
    // cache the artist indexes
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
  prototype.fixPlaylistPosCache = function(){
    var i, ref$, len$, item;
    for (i = 0, len$ = (ref$ = this.playlist.item_list).length; i < len$; ++i) {
      item = ref$[i];
      item.pos = i;
    }
  };
  // clear state so we can start over with new mpd connection
  prototype.resetServerState = function(){
    this.buffer = "";
    this.msg_handler_queue = [];
    this.have_file_list_cache = false;
    // cache of library data from mpd. See comment at top of this file
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
  return Player;
}(EventEmitter));
if (typeof module != 'undefined' && module !== null) {
  module.exports = Player;
}
if (typeof window != 'undefined' && window !== null) {
  window.Player = Player;
}
function extend$(sub, sup){
  function fun(){} fun.prototype = (sub.superclass = sup).prototype;
  (sub.prototype = new fun).constructor = sub;
  if (typeof sup.extended == 'function') sup.extended(sub);
  return sub;
}
function bind$(obj, key){
  return function(){ return obj[key].apply(obj, arguments) };
}
