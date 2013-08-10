var EventEmitter, trackNameFromFile, parseMsgToTrackObjects, MpdParser;
EventEmitter = require('events').EventEmitter;
trackNameFromFile = require('./futils').trackNameFromFile;
function noop(arg$){
  var err;
  err = arg$.err;
  if (err) {
    throw err;
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
function parseMpdObject(msg){
  var o, i$, ref$, line, len$, ref1$, key, val;
  o = {};
  for (i$ = 0, len$ = (ref$ = (fn$())).length; i$ < len$; ++i$) {
    ref1$ = ref$[i$], key = ref1$[0], val = ref1$[1];
    o[key] = val;
  }
  return o;
  function fn$(){
    var i$, ref$, len$, results$ = [];
    for (i$ = 0, len$ = (ref$ = msg.split("\n")).length; i$ < len$; ++i$) {
      line = ref$[i$];
      results$.push(splitOnce(line, ": "));
    }
    return results$;
  }
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
function parseMpdTracks(msg, flush){
  return parseWithSepField(msg, 'file', {
    'directory': true
  }, flush);
}
parseMsgToTrackObjects = function(msg){
  var tracks;
  tracks = [];
  parseMpdTracks(msg, function(mpd_track){
    var ref$, artist_name, track;
    artist_name = ((ref$ = mpd_track.Artist) != null ? ref$ : "").trim();
    track = {
      file: mpd_track.file,
      name: mpd_track.Title || trackNameFromFile(mpd_track.file),
      artist_name: artist_name,
      artist_disambiguation: "",
      album_artist_name: mpd_track.AlbumArtist || artist_name,
      album_name: ((ref$ = mpd_track.Album) != null ? ref$ : "").trim(),
      track: parseMaybeUndefNumber(mpd_track.Track),
      time: parseInt(mpd_track.Time, 10),
      year: parseMaybeUndefNumber(mpd_track.Date)
    };
    tracks.push(track);
  });
  return tracks;
};
module.exports = MpdParser = (function(superclass){
  MpdParser.displayName = 'MpdParser';
  var prototype = extend$(MpdParser, superclass).prototype, constructor = MpdParser;
  function MpdParser(mpd_socket){
    var this$ = this instanceof ctor$ ? this : new ctor$;
    this$.mpd_socket = mpd_socket;
    superclass.call(this$);
    this$.mpd_socket.on('data', function(data){
      this$.receive(data);
    });
    this$.buffer = "";
    this$.msg_handler_queue = [];
    this$.idling = false;
    return this$;
  } function ctor$(){} ctor$.prototype = prototype;
  prototype.current_song_and_status_command = "command_list_begin\ncurrentsong\nstatus\ncommand_list_end";
  prototype.sendRequest = function(command, cb){
    var this$ = this;
    cb == null && (cb = noop);
    if (command.indexOf("sticker") == -1) {
      console.log("sending to mpd:", JSON.stringify(command));
    }
    if (this.idling) {
      this.send("noidle\n");
    }
    this.sendWithCallback(command, function(response){
      cb(this$.parseResponse(command, response));
    });
    this.sendWithCallback("idle", bind$(this, 'handleIdleResultsLoop'));
    this.idling = true;
  };
  prototype.parseResponse = function(complete_command, response){
    var err, msg, command_name, items, item;
    err = response.err, msg = response.msg;
    if (err != null) {
      return response;
    }
    if (complete_command === this.current_song_and_status_command) {
      return parseMpdObject(msg);
    }
    command_name = complete_command.match(/^\S*/)[0];
    return {
      msg: (function(){
        switch (command_name) {
        case 'listallinfo':
          return parseMsgToTrackObjects(msg);
        case 'lsinfo':
          return parseMsgToTrackObjects(msg)[0];
        case 'status':
          return parseMpdObject(msg);
        case 'playlistinfo':
          items = [];
          parseMpdTracks(msg, function(track){
            return items.push({
              id: parseInt(track.Id, 10),
              file: track.file
            });
          });
          return items;
        case 'currentsong':
          item = null;
          parseMpdTracks(msg, function(track){
            return item = {
              id: parseInt(track.Id, 10),
              pos: parseInt(track.Pos, 10),
              file: track.file
            };
          });
          return item;
        default:
          return msg;
        }
      }())
    };
  };
  prototype.send = function(data){
    this.mpd_socket.write(data);
  };
  prototype.handleMessage = function(arg){
    this.msg_handler_queue.shift()(arg);
  };
  prototype.receive = function(data){
    var m, msg, line, code, str, err;
    this.buffer += data;
    for (;;) {
      m = this.buffer.match(/^(OK|ACK|list_OK)(.*)$/m);
      if (m == null) {
        return;
      }
      msg = this.buffer.substring(0, m.index);
      line = m[0], code = m[1], str = m[2];
      if (code === "ACK") {
        this.emit('error', str);
        err = new Error(str);
        this.handleMessage({
          err: err
        });
      } else if (line.indexOf("OK MPD") === 0) {} else {
        this.handleMessage({
          msg: msg
        });
      }
      this.buffer = this.buffer.substring(msg.length + line.length + 1);
    }
  };
  prototype.handleIdleResults = function(msg){
    var systems, i$, ref$, len$, system;
    systems = [];
    for (i$ = 0, len$ = (ref$ = msg.trim().split("\n")).length; i$ < len$; ++i$) {
      system = ref$[i$];
      if (system.length > 0) {
        systems.push(system.substring(9));
      }
    }
    if (systems.length) {
      this.emit('status', systems);
    }
  };
  prototype.sendWithCallback = function(cmd, cb){
    cb == null && (cb = noop);
    this.msg_handler_queue.push(cb);
    this.send(cmd + "\n");
  };
  prototype.handleIdleResultsLoop = function(arg){
    var err, msg;
    err = arg.err, msg = arg.msg;
    if (err) {
      throw err;
    }
    this.handleIdleResults(msg);
    if (this.msg_handler_queue.length === 0) {
      this.sendWithCallback("idle", bind$(this, 'handleIdleResultsLoop'));
    }
  };
  return MpdParser;
}(EventEmitter));
function extend$(sub, sup){
  function fun(){} fun.prototype = (sub.superclass = sup).prototype;
  (sub.prototype = new fun).constructor = sub;
  if (typeof sup.extended == 'function') sup.extended(sub);
  return sub;
}
function bind$(obj, key){
  return function(){ return obj[key].apply(obj, arguments) };
}