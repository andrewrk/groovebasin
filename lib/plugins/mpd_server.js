var net = require('net');
var path = require('path');
var findit = require('findit');
var Player = require('../player');

module.exports = MpdServer;

var ERR_CODE_NOT_LIST = 1;
var ERR_CODE_ARG = 2;
var ERR_CODE_PASSWORD = 3;
var ERR_CODE_PERMISSION = 4;
var ERR_CODE_UNKNOWN = 5;
var ERR_CODE_NO_EXIST = 50;
var ERR_CODE_PLAYLIST_MAX = 51;
var ERR_CODE_SYSTEM = 52;
var ERR_CODE_PLAYLIST_LOAD = 53;
var ERR_CODE_UPDATE_ALREADY = 54;
var ERR_CODE_PLAYER_SYNC = 55;
var ERR_CODE_EXIST = 56;

var commands = {
  "add": {
    fn: addCmd,
    permission: 'add',
    args: [
      {
        name: 'uri',
        type: 'string',
      },
    ],
  },
  "addid": {
    permission: 'add',
    args: [
      {
        name: 'uri',
        type: 'string',
      },
      {
        name: 'position',
        type: 'integer',
        optional: true,
      },
    ],
    fn: function (self, socket, args, cb) {
      var pos = args.position == null ? self.gb.player.tracksInOrder.length : args.position;
      var dbFile = self.gb.player.dbFilesByPath[args.uri];
      if (!dbFile) return cb(ERR_CODE_NO_EXIST, "Not found");
      var ids = self.gb.player.insertTracks(pos, [dbFile.key], false);
      socket.write("Id: " + self.toMpdId(ids[0]) + "\n");
      cb();
    },
  },
  "channels": {
    permission: 'read',
    fn: function channelsCmd(self, socket, args, cb) {
      cb();
    },
  },
  "clear": {
    permission: 'control',
    fn: function (self, socket, args, cb) {
      self.gb.player.clearPlaylist();
      cb();
    },
  },
  "clearerror": {
    fn: function clearerrorCmd(self, socket, args, cb) {
      cb();
    },
  },
  "close": {
    fn: function (self, socket, args, cb) {
      socket.end();
      cb();
    },
  },
  "commands": {
    permission: 'read',
    fn: function (self, socket, args, cb) {
      for (var commandName in commands) {
        socket.write("command: " + commandName + "\n");
      }
      cb();
    },
  },
  "config": {
    permission: 'disabled',
  },
  "consume": {
    permission: 'control',
    fn: consumeCmd,
  },
  "count": {
    permission: 'TODO',
    fn: countCmd,
  },
  "crossfade": {
    permission: 'control',
    fn: crossfadeCmd,
  },
  "currentsong": {
    permission: 'read',
    fn: currentsongCmd,
  },
  "decoders": {
    permission: 'read',
    fn: decodersCmd,
  },
  "delete": {
    permission: 'control',
    args: [
      {
        name: 'indexRange',
        type: 'range',
      },
    ],
    fn: function (self, socket, args, cb) {
      var start = args.indexRange.start;
      var end = args.indexRange.end;
      var ids = [];
      for (var i = start; i < end; i += 1) {
        var track = self.gb.player.tracksInOrder[i];
        if (!track) {
          cb(ERR_CODE_ARG, "Bad song index");
          return;
        }
        ids.push(track.id);
      }
      self.gb.player.removePlaylistItems(ids);
      cb();
    },
  },
  "deleteid": {
    permission: 'control',
    args: [
      {
        name: 'id',
        type: 'id',
      },
    ],
    fn: function(self, socket, args, cb) {
      self.gb.player.removePlaylistItems([args.id]);
      cb();
    },
  },
  "disableoutput": {
    permission: 'disabled',
  },
  "enableoutput": {
    permission: 'disabled',
  },
  "find": {
    permission: 'read',
    fn: findCmd,
  },
  "findadd": {
    permission: 'control',
    fn: findaddCmd,
  },
  "idle": {}, // handled in a special case
  "kill": {
    permission: 'disabled',
  },
  "list": {
    permission: 'read',
    fn: listCmd,
  },
  "listall": {
    permission: 'read',
    fn: listallCmd,
  },
  "listallinfo": {
    permission: 'read',
    fn: function (self, socket, args, cb) {
      var trackTable = self.gb.player.libraryIndex.trackTable;
      for (var key in trackTable) {
        var dbTrack = trackTable[key];
        writeTrackInfo(socket, dbTrack);
      }
      cb();
    }
  },
  "listplaylist": {
    permission: 'read',
    fn: listplaylistCmd,
  },
  "listplaylistinfo": {
    permission: 'read',
    fn: listplaylistinfoCmd,
  },
  "listplaylists": {
    permission: 'read',
    fn: listplaylistsCmd,
  },
  "load": {
    permission: 'control',
    fn: loadCmd,
  },
  "lsinfo": {
    permission: 'TODO',
    fn: lsinfoCmd,
  },
  "mixrampdb": {
    permission: 'control',
    fn: mixrampdbCmd,
  },
  "mixrampdelay": {
    permission: 'control',
    fn: mixrampdelayCmd,
  },
  "move": {
    permission: 'control',
    args: [
      {
        name: 'fromRange',
        type: 'range',
      },
      {
        name: 'pos',
        type: 'integer',
      },
    ],
    fn: function (self, socket, args, cb) {
      self.gb.player.moveRangeToPos(args.fromRange.start, args.fromRange.end, args.pos);
      cb();
    },
  },
  "moveid": {
    permission: 'control',
    args: [
      {
        name: 'id',
        type: 'id',
      },
      {
        name: 'pos',
        type: 'integer',
      },
    ],
    fn: function (self, socket, args, cb) {
      self.gb.player.moveIdsToPos([args.id], args.pos);
      cb();
    },
  },
  "next": {
    permission: 'control',
    fn: function (self, socket, args, cb) {
      self.gb.player.next();
      cb();
    }
  },
  "notcommands": {
    permission: 'read',
    fn: notcommandsCmd,
  },
  "outputs": {
    permission: 'read',
    fn: outputsCmd,
  },
  "password": {
    fn: passwordCmd,
  },
  "pause": {
    permission: 'control',
    args: [
      {
        name: 'pause',
        type: 'boolean',
        optional: true,
      },
    ],
    fn: function pauseCmd(self, socket, args, cb) {
      if (args.pause == null) {
        // toggle
        if (self.gb.player.isPlaying) {
          self.gb.player.pause();
        } else {
          self.gb.player.play();
        }
      } else {
        if (args.pause) {
          self.gb.player.pause();
        } else {
          self.gb.player.play();
        }
      }
      cb();
    },
  },
  "ping": {
    permission: 'read',
    fn: function (self, socket, args, cb) {
      cb();
    }
  },
  "play": {
    permission: 'control',
    fn: function (self, socket, args, cb) {
      var index = args.songPos == null ? 0 : args.songPos;
      self.gb.player.seekToIndex(index, 0);
      cb();
    },
    args: [
      {
        name: 'songPos',
        type: 'integer',
        optional: true,
      },
    ],
  },
  "playid": {
    permission: 'control',
    args: [
      {
        name: 'id',
        type: 'id',
        optional: true,
      },
    ],
    fn: function (self, socket, args, cb) {
      var id = args.id == null ? self.gb.player.tracksInOrder[0].id : args.id;
      var item = self.gb.player.playlist[id];
      if (!item) return cb(ERR_CODE_NO_EXIST, "No such song");
      self.gb.player.seek(id, 0);
      cb();
    },
  },
  "playlist": {
    permission: 'read',
    fn: function (self, socket, args, cb) {
      var trackTable = self.gb.player.libraryIndex.trackTable;
      self.gb.player.tracksInOrder.forEach(function(track, index) {
        var dbTrack = trackTable[track.key];
        socket.write(index + ":file: " + dbTrack.file + "\n");
      });
      cb();
    }
  },
  "playlistadd": {
    permission: 'control',
    fn: playlistaddCmd,
  },
  "playlistclear": {
    permission: 'control',
    fn: playlistclearCmd,
  },
  "playlistdelete": {
    permission: 'control',
    fn: playlistdeleteCmd,
  },
  "playlistfind": {
    permission: 'read',
    fn: playlistfindCmd,
  },
  "playlistid": {
    permission: 'TODO',
    fn: playlistidCmd,
  },
  "playlistinfo": {
    permission: 'read',
    args: [
      {
        name: 'indexRange',
        type: 'range',
        optional: true,
      },
    ],
    fn: playlistinfoCmd,
  },
  "playlistmove": {
    permission: 'control',
    fn: playlistmoveCmd,
  },
  "playlistsearch": {
    permission: 'read',
    fn: playlistsearchCmd,
  },
  "plchanges": {
    permission: 'read',
    args: [
      {
        name: "version",
        type: "integer",
      },
    ],
    fn: function(self, socket, args, cb) {
      // TODO actually do versioning?
      playlistinfoCmd(self, socket, {}, cb);
    },
  },
  "plchangesposid": {
    permission: 'read',
    fn: plchangesposidCmd,
  },
  "previous": {
    permission: 'control',
    fn: function (self, socket, args, cb) {
      self.gb.player.prev();
      cb();
    }
  },
  "prio": {
    permission: 'control',
    fn: prioCmd,
  },
  "prioid": {
    permission: 'control',
    fn: prioidCmd,
  },
  "random": {
    permission: 'control',
    fn: randomCmd,
  },
  "readmessages": {
    permission: 'TODO',
    fn: readmessagesCmd,
  },
  "rename": {
    permission: 'control',
    fn: renameCmd,
  },
  "repeat": {
    permission: 'control',
    args: [
      {
        name: 'on',
        type: 'boolean',
      },
    ],
    fn: function (self, socket, args, cb) {
      if (args.on && self.gb.player.repeat === Player.REPEAT_OFF) {
        self.gb.player.setRepeat(self.singleMode ? Player.REPEAT_ONE : Player.REPEAT_ALL);
      } else if (!args.on && self.gb.player.repeat !== Player.REPEAT_OFF) {
        self.gb.player.setRepeat(Player.REPEAT_OFF);
      }
      cb();
    },
  },
  "replay_gain_mode": {
    permission: 'control',
    fn: replay_gain_modeCmd,
  },
  "replay_gain_status": {
    permission: 'read',
    fn: replay_gain_statusCmd,
  },
  "rescan": {
    permission: 'admin',
    fn: function (self, socket, args, cb) {
      socket.write("updating_db: 1\n");
      cb();
    },
  },
  "rm": {
    permission: 'control',
    fn: rmCmd,
  },
  "save": {
    permission: 'control',
    fn: saveCmd,
  },
  "search": {
    permission: 'read',
    fn: searchCmd,
  },
  "searchadd": {
    permission: 'control',
    fn: searchaddCmd,
  },
  "searchaddpl": {
    permission: 'control',
    fn: searchaddplCmd,
  },
  "seek": {
    permission: 'control',
    args: [
      {
        name: 'index',
        type: 'integer',
      },
      {
        name: 'pos',
        type: 'float',
      },
    ],
    fn: function (self, socket, args, cb) {
      self.gb.player.seekToIndex(args.index, args.pos);
      cb();
    },
  },

  "seekcur": {
    permission: 'control',
    args: [
      {
        name: 'pos',
        type: 'float',
      },
    ],
    fn: function (self, socket, args, cb) {
      var currentTrack = self.gb.player.currentTrack;
      if (!currentTrack) return cb(ERR_CODE_PLAYER_SYNC, "Not playing");
      self.gb.player.seek(currentTrack.id, args.pos);
      cb();
    },
  },
  "seekid": {
    permission: 'control',
    args: [
      {
        name: 'id',
        type: 'id',
      },
      {
        name: 'pos',
        type: 'float',
      },
    ],
    fn: function (self, socket, args, cb) {
      self.gb.player.seek(args.id, args.pos);
      cb();
    },
  },
  "sendmessage": {
    permission: 'control',
    fn: sendmessageCmd,
  },
  "setvol": {
    permission: 'control',
    args: [
      {
        name: 'vol',
        type: 'float',
      },
    ],
    fn: function (self, socket, args, cb) {
      self.gb.player.setVolume(args.vol / 100);
      cb();
    },
  },
  "shuffle": {
    permission: 'control',
    fn: function (self, socket, args, cb) {
      self.gb.player.shufflePlaylist();
      cb();
    },
  },
  "single": {
    permission: 'control',
    fn: function singleCmd(self, socket, args, cb) {
      switch (self.gb.player.repeat) {
        case Player.REPEAT_ONE:
          self.gb.player.setRepeat(Player.REPEAT_ALL);
          self.singleMode = false;
          break;
        case Player.REPEAT_ALL:
          self.gb.player.setRepeat(Player.REPEAT_ONE);
          self.singleMode = true;
          break;
        case Player.REPEAT_OFF:
          self.singleMode = !self.singleMode;
          break;
      }
      cb();
    },
  },
  "stats": {
    permission: 'read',
    fn: statsCmd,
  },
  "status": {
    permission: 'read',
    fn: statusCmd,
  },
  "sticker": {
    permission: 'TODO',
    fn: stickerCmd,
  },
  "stop": {
    permission: 'control',
    fn: function (self, socket, args, cb) {
      self.gb.player.stop();
      cb();
    },
  },
  "subscribe": {
    permission: 'TODO',
    fn: subscribeCmd,
  },
  "swap": {
    permission: 'control',
    fn: swapCmd,
  },
  "swapid": {
    permission: 'control',
    fn: swapidCmd,
  },
  "tagtypes": {
    permission: 'TODO',
    fn: tagtypesCmd,
  },
  "unsubscribe": {
    permission: 'TODO',
    fn: unsubscribeCmd,
  },
  "update": {
    permission: 'admin',
    fn: function updateCmd(self, socket, args, cb) {
      socket.write("updating_db: 1\n");
      cb();
    },
  },
  "urlhandlers": {
    permission: 'read',
    fn: urlhandlersCmd,
  },
};

var argParsers = {
  'integer': parseInteger,
  'float': parseFloat,
  'range': parseRange,
  'boolean': parseBoolean,
  'string': parseString,
  'id': parseId,
};

var stateCount = 0;
var STATE_CMD       = stateCount++;
var STATE_CMD_SPACE = stateCount++;
var STATE_ARG       = stateCount++;
var STATE_ARG_QUOTE = stateCount++;
var STATE_ARG_ESC   = stateCount++;

var cmdListStateCount = 0;
var CMD_LIST_STATE_NONE   = cmdListStateCount++;
var CMD_LIST_STATE_LIST   = cmdListStateCount++;

function MpdServer(gb) {
  this.gb = gb;
  this.gbIdToMpdId = {};
  this.mpdIdToGbId = {};
  this.nextMpdId = 0;
}

MpdServer.prototype.initialize = function(cb) {
  var self = this;
  var mpdPort = self.gb.config.mpdPort;
  var mpdHost = self.gb.config.mpdHost;
  if (mpdPort == null || mpdHost == null) {
    console.info("MPD Protocol disabled");
    cb();
    return;
  }
  self.bootTime = new Date();
  self.singleMode = false;
  var server = net.createServer(onSocketConnection);
  server.listen(mpdPort, mpdHost, function() {
    console.info("MPD Protocol listening at " + mpdHost + ":" + mpdPort);
    cb();
  });

  function onSocketConnection(socket) {
    var buffer = "";
    var cmdListState = CMD_LIST_STATE_NONE;
    var cmdList = [];
    var okMode = false;
    var isIdle = false;
    var commandQueue = [];
    var ongoingCommand = false;
    var permissions = self.gb.config.defaultPermissions;
    var updatedSubsystems = {
      database: false,
      update: false,
      stored_playlist: false,
      playlist: false,
      player: false,
      mixer: false,
      output: false,
      options: false,
      sticker: false,
      subscription: false,
      message: false,
    };

    socket.setEncoding('utf8');
    socket.write("OK MPD 0.17.0\n");
    socket.on('data', bufferStr);
    socket.on('error', onError);
    self.gb.player.on('volumeUpdate', onVolumeUpdate);
    self.gb.player.on('repeatUpdate', updateOptionsSubsystem);
    self.gb.player.on('dynamicModeUpdate', updateOptionsSubsystem);
    self.gb.player.on('playlistUpdate', onPlaylistUpdate);

    function onVolumeUpdate() {
      subsystemUpdate('mixer');
    }

    function onPlaylistUpdate() {
      // TODO make these updates more fine grained
      subsystemUpdate('playlist');
      subsystemUpdate('player');
    }

    function updateOptionsSubsystem() {
      subsystemUpdate('options');
    }

    function subsystemUpdate(subsystem) {
      updatedSubsystems[subsystem] = true;
      if (isIdle) handleIdle();
    }

    function onError(err) {
      console.warn("socket error:", err.message);
    }
    
    function bufferStr(str) {
      var lines = str.split(/\r?\n/);
      buffer += lines[0];
      if (lines.length === 1) return;
      handleLine(buffer);
      var lastIndex = lines.length - 1;
      for (var i = 1; i < lastIndex; i += 1) {
        handleLine(lines[i]);
      }
      buffer = lines[lastIndex];
    }

    function handleLine(line) {
      var state = STATE_CMD;
      var cmd = "";
      var args = [];
      var curArg = "";
      for (var i = 0; i < line.length; i += 1) {
        var c = line[i];
        switch (state) {
          case STATE_CMD:
            if (isSpace(c)) {
              state = STATE_CMD_SPACE;
            } else {
              cmd += c;
            }
            break;
          case STATE_CMD_SPACE:
            if (c === '"') {
              curArg = "";
              state = STATE_ARG_QUOTE;
            } else if (!isSpace(c)) {
              curArg = c;
              state = STATE_ARG;
            }
            break;
          case STATE_ARG:
            if (isSpace(c)) {
              args.push(curArg);
              curArg = "";
              state = STATE_CMD_SPACE;
            } else {
              curArg += c;
            }
            break;
          case STATE_ARG_QUOTE:
            if (c === '"') {
              args.push(curArg);
              curArg = "";
              state = STATE_CMD_SPACE;
            } else if (c === "\\") {
              state = STATE_ARG_ESC;
            } else {
              curArg += c;
            }
            break;
          case STATE_ARG_ESC:
            curArg += c;
            state = STATE_ARG_QUOTE;
            break;
          default:
            throw new Error("unrecognized state");
        }
      }
      if (state === STATE_ARG) {
        args.push(curArg);
      }
      commandQueue.push([cmd, args]);
      flushQueue();
    }

    function flushQueue() {
      if (ongoingCommand) return;
      var queueItem = commandQueue.shift();
      if (!queueItem) return;
      var cmd = queueItem[0];
      var args = queueItem[1];
      ongoingCommand = true;
      handleCommand(cmd, args, function() {
        ongoingCommand = false;
        flushQueue();
      });
    }

    function handleCommand(cmdName, args, cb) {
      var cmdIndex = 0;

      switch (cmdListState) {
        case CMD_LIST_STATE_NONE:
          if (cmdName === 'command_list_begin' && args.length === 0) {
            cmdListState = CMD_LIST_STATE_LIST;
            cmdList = [];
            okMode = false;
            cb();
            return;
          } else if (cmdName === 'command_list_ok_begin' && args.length === 0) {
            cmdListState = CMD_LIST_STATE_LIST;
            cmdList = [];
            okMode = true;
            cb();
            return;
          } else {
            runOneCommand(cmdName, args, 0, function(ok) {
              if (ok) socket.write("OK\n");
              cb();
            });
            return;
          }
          break;
        case CMD_LIST_STATE_LIST:
          if (cmdName === 'command_list_end' && args.length === 0) {
            cmdListState = CMD_LIST_STATE_NONE;

            runAndCheckOneCommand();
            return;

          } else {
            cmdList.push([cmdName, args]);
            cb();
            return;
          }
          break;
        default:
          throw new Error("unrecognized state");
      }

      function runAndCheckOneCommand() {
        var commandPayload = cmdList.shift();
        if (!commandPayload) {
          socket.write("OK\n");
          cb();
          return;
        }
        var thisCmdName = commandPayload[0];
        var thisCmdArgs = commandPayload[1];
        runOneCommand(thisCmdName, thisCmdArgs, cmdIndex++, function(ok) {
          if (!ok) {
            cb();
            return;
          } else if (okMode) {
            socket.write("list_OK\n");
          }
        });
      }
    }

    function runOneCommand(cmdName, args, index, cb) {
      if (cmdName === 'noidle') {
        handleNoIdle(args);
        cb(false);
        return;
      }
      if (isIdle) {
        socket.end();
        cb(false);
        return;
      }
      if (cmdName === 'idle') {
        handleIdle(args);
        cb(false);
        return;
      }
      execOneCommand(cmdName, args, cmdDone);

      function cmdDone(code, msg) {
        if (code) {
          console.warn("cmd err:", cmdName, JSON.stringify(args), msg);
          if (code === ERR_CODE_UNKNOWN) cmdName = "";
          socket.write("ACK [" + code + "@" + index + "] {" + cmdName + "} " + msg + "\n");
          cb(false);
          return;
        }
        cb(true);
      }
    }

    function execOneCommand(cmdName, args, cb) {
      if (!cmdName.length) return cb(ERR_CODE_UNKNOWN, "No command given");
      var cmd = commands[cmdName];
      if (!cmd) return cb(ERR_CODE_UNKNOWN, "unknown command \"" + cmdName + "\"");

      var perm = cmd.permission;
      if (perm != null && !permissions[perm]) {
        cb(ERR_CODE_PERMISSION, "you don't have permission for \"" + cmdName + "\"");
        return;
      }

      var min = 0;
      var max = 0;
      var i;
      var cmdArgs = cmd.args || [];
      for (i = 0; i < cmdArgs.length; i += 1) {
        if (!cmdArgs[i].optional) min += 1;
        max += 1;
      }
      if (args.length < min) {
        cb(ERR_CODE_ARG, "too few arguments for \"" + cmdName + "\"");
        return;
      }
      if (args.length > max) {
        cb(ERR_CODE_ARG, "too many arguments for \"" + cmdName + "\"");
        return;
      }
      var namedArgs = {};
      for (i = 0; i < args.length; i += 1) {
        var arg = args[i];
        var argInfo = cmdArgs[i];

        var parseArg = argParsers[argInfo.type];
        if (!parseArg) throw new Error("unrecognized arg type: " + argInfo.type);
        var ret = parseArg.call(self, arg);
        if (ret.msg) {
          cb(ERR_CODE_ARG, ret.msg);
          return;
        }
        namedArgs[argInfo.name] = ret.value;
      }
      console.info("ok mpd command", cmdName, JSON.stringify(namedArgs));
      cmd.fn(self, socket, namedArgs, cb);
    }

    function handleIdle(args) {
      var anyUpdated = false;
      for (var subsystem in updatedSubsystems) {
        var isUpdated = updatedSubsystems[subsystem];
        if (isUpdated) {
          socket.write("changed: " + subsystem + "\n");
          anyUpdated = true;
          updatedSubsystems[subsystem] = false;
        }
      }
      if (anyUpdated) {
        socket.write("OK\n");
        isIdle = false;
        return;
      }
      isIdle = true;
    }

    function handleNoIdle(args) {
      if (!isIdle) return;
      isIdle = false;
    }
  }
}

MpdServer.prototype.toMpdId = function(grooveBasinId) {
  var mpdId = this.gbIdToMpdId[grooveBasinId];
  if (!mpdId) {
    mpdId = this.nextMpdId++;
    this.gbIdToMpdId[grooveBasinId] = mpdId;
    this.mpdIdToGbId[mpdId] = grooveBasinId;
  }
  return mpdId;
};

MpdServer.prototype.fromMpdId = function(mpdId) {
  return this.mpdIdToGbId[mpdId];
};

function isSpace(c) {
  return c === '\t' || c === ' ';
}

function parseBoolean(str) {
  return {
    value: !!parseInt(str, 10),
    msg: null,
  };
}

function parseFloat(str) {
  var x = parseInt(str, 10);
  return {
    value: x,
    msg: isNaN(x) ? ("Number expected: " + str) : null,
  };
}

function parseInteger(str) {
  var x = parseInt(str, 10);
  return {
    value: x,
    msg: isNaN(x) ? ("Integer expected: " + str) : null,
  };
}

function parseRange(str) {
  var msg = null;
  var start = null;
  var end = null;
  var parts = str.split(":");
  if (parts.length === 2) {
    start = parseInt(parts[0], 10);
    end = parseInt(parts[1], 10);
  } else if (parts.length === 1) {
    start = parseInt(parts[0], 10);
    end = start + 1;
  }
  if (start == null || end == null || isNaN(start) || isNaN(end)) {
    msg = "Integer or range expected: " + str;
  } else if (start < 0 || end < 0) {
    msg = "Number is negative: " + str;
  } else if (end < start) {
    msg = "Bad song index";
  }
  return {
    value: {
      start: start,
      end: end,
    },
    msg: msg,
  };
}

function parseString(str) {
  return {
    value: str,
    msg: null,
  };
}

function parseId(str) {
  var results = parseInteger.call(this, str);
  if (results.msg) return results;
  var grooveBasinId = this.fromMpdId(results.value);
  var msg = grooveBasinId ? null : "No such song";
  return {
    value: grooveBasinId,
    msg: null,
  };
}

function writeTrackInfo(socket, dbTrack) {
  socket.write("file: " + dbTrack.file + "\n");
  if (dbTrack.mtime != null) {
    socket.write("Last-Modified: " + new Date(dbTrack.mtime).toISOString() + "\n");
  }
  if (dbTrack.duration != null) {
    socket.write("Time: " + Math.round(dbTrack.duration) + "\n");
  }
  if (dbTrack.artistName != null) {
    socket.write("Artist: " + dbTrack.artistName + "\n");
  }
  if (dbTrack.albumName != null) {
    socket.write("Album: " + dbTrack.albumName + "\n");
  }
  if (dbTrack.albumArtistName != null) {
    socket.write("AlbumArtist: " + dbTrack.albumArtistName + "\n");
  }
  if (dbTrack.genre != null) {
    socket.write("Genre: " + dbTrack.genre + "\n");
  }
  if (dbTrack.name != null) {
    socket.write("Title: " + dbTrack.name + "\n");
  }
  if (dbTrack.track != null) {
    if (dbTrack.trackCount != null) {
      socket.write("Track: " + dbTrack.track + "/" + dbTrack.trackCount + "\n");
    } else {
      socket.write("Track: " + dbTrack.track + "\n");
    }
  }
  if (dbTrack.composerName != null) {
    socket.write("Composer: " + dbTrack.composerName + "\n");
  }
  if (dbTrack.disc != null) {
    if (dbTrack.discCount != null) {
      socket.write("Disc: " + dbTrack.disc + "/" + dbTrack.discCount + "\n");
    } else {
      socket.write("Disc: " + dbTrack.disc + "\n");
    }
  }
  if (dbTrack.year != null) {
    socket.write("Date: " + dbTrack.year + "\n");
  }
}

function addCmd(self, socket, args, cb) {
  var musicDir = self.gb.config.musicDirectory;

  var walker = findit(path.join(musicDir, args.uri));
  var files = [];
  walker.on('file', function(file) {
    files.push(file);
  });
  walker.on('error', function(err) {
    walker.removeAllListeners();
    console.error("unable to walk file system:", err.stack);
    cb(ERR_CODE_UNKNOWN, "Unknown error");
  });
  walker.on('end', function() {
    var keys = [];
    for (var i = 0; i < files.length; i += 1) {
      var file = files[i];
      var relPath = path.relative(musicDir, file);
      var dbFile = self.gb.player.dbFilesByPath[relPath];
      if (dbFile) keys.push(dbFile.key);
    }
    if (keys.length === 0) {
      cb(ERR_CODE_NO_EXIST, "Not found");
      return;
    }
    self.gb.player.appendTracks(keys, false);
    cb();
  });
}

function consumeCmd(self, socket, args, cb) {
  // TODO make this turn on/off dynamic mode
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function countCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function crossfadeCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function currentsongCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function decodersCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function deleteidCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function findCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function findaddCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function listCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function listallCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function listplaylistCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function listplaylistinfoCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function listplaylistsCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function loadCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function lsinfoCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function mixrampdbCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function mixrampdelayCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function notcommandsCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function outputsCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function passwordCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function playlistaddCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function playlistclearCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function playlistdeleteCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function playlistfindCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function playlistidCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function playlistinfoCmd(self, socket, args, cb) {
  var start = 0;
  var end = self.gb.player.tracksInOrder.length;

  if (args.indexRange != null) {
    start = args.indexRange.start;
    end = args.indexRange.end;
  }

  var trackTable = self.gb.player.libraryIndex.trackTable;
  for (var i = start; i < end; i += 1) {
    var item = self.gb.player.tracksInOrder[i];
    var track = trackTable[item.key];
    writeTrackInfo(socket, track);
    socket.write("Pos: " + i + "\n");
    socket.write("Id: " + self.toMpdId(item.id) + "\n");
  }
  cb();
}

function playlistmoveCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function playlistsearchCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function plchangesCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function plchangesposidCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function prioCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function prioidCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function randomCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function readmessagesCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function renameCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function replay_gain_modeCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function replay_gain_statusCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function rmCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function saveCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function searchCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function searchaddCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function searchaddplCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function sendmessageCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function statsCmd(self, socket, args, cb) {
  var uptime = Math.floor((new Date() - self.bootTime) / 1000);

  var libraryIndex = self.gb.player.libraryIndex;
  var artists = libraryIndex.artistList.length;
  var albums = libraryIndex.albumList.length;
  var songs = 0;
  var trackTable = libraryIndex.trackTable;
  var dbPlaytime = 0;
  for (var key in trackTable) {
    var dbTrack = trackTable[key];
    songs += 1;
    dbPlaytime += dbTrack.duration;
  }
  dbPlaytime = Math.floor(dbPlaytime);
  var dbUpdate = Math.floor(new Date().getTime() / 1000);
  socket.write("artists: " + artists + "\n");
  socket.write("albums: " + albums + "\n");
  socket.write("songs: " + songs + "\n");
  socket.write("uptime: " + uptime + "\n");
  socket.write("playtime: 0\n"); // TODO keep track of this?
  socket.write("db_playtime: " + dbPlaytime + "\n");
  socket.write("db_update: " + dbUpdate + "\n");

  cb();
}

function statusCmd(self, socket, args, cb) {
  var volume = Math.round(self.gb.player.volume * 100);

  var repeat, single;
  switch (self.gb.player.repeat) {
    case Player.REPEAT_ONE:
      repeat = 1;
      single = 1;
      break;
    case Player.REPEAT_ALL:
      repeat = 1;
      single = 0;
      break;
    case Player.REPEAT_OFF:
      repeat = 0;
      single = +self.singleMode;
      break;
  }
  var playlistLength = self.gb.player.tracksInOrder.length;
  var currentTrack = self.gb.player.currentTrack;
  var state;
  if (self.gb.player.isPlaying) {
    state = 'play';
  } else if (currentTrack) {
    state = 'pause';
  } else {
    state = 'stop';
  }

  var song = null;
  var songId = null;
  var nextSong = null;
  var nextSongId = null;
  var elapsed = null;
  var time = null;
  var trackTable = self.gb.player.libraryIndex.trackTable;
  if (currentTrack) {
    song = currentTrack.index;
    songId = self.toMpdId(currentTrack.id);
    var nextTrack = self.gb.player.tracksInOrder[currentTrack.index + 1];
    if (nextTrack) {
      nextSong = nextTrack.index;
      nextSongId = self.toMpdId(nextTrack.id);
    }

    var dbTrack = trackTable[currentTrack.key];
    elapsed = self.gb.player.getCurPos();
    time = Math.round(elapsed) + ":" + Math.round(dbTrack.duration);
  }

  socket.write("volume: " + volume + "\n");
  socket.write("repeat: " + repeat + "\n");
  socket.write("random: 0\n");
  socket.write("single: " + single + "\n");
  socket.write("consume: 0\n");
  socket.write("playlist: 0\n"); // TODO what to do with this?
  socket.write("playlistlength: " + playlistLength + "\n");
  socket.write("xfade: 0\n");
  socket.write("mixrampdb: 0.000000\n");
  socket.write("mixrampdelay: nan\n");
  socket.write("state: " + state + "\n");
  if (song != null) {
    socket.write("song: " + song + "\n");
    socket.write("songid: " + songId + "\n");
    if (nextSong != null) {
      socket.write("nextsong: " + nextSong + "\n");
      socket.write("nextsongid: " + nextSongId + "\n");
    }
    socket.write("time: " + time + "\n");
    socket.write("elapsed: " + elapsed + "\n");
    socket.write("bitrate: 192\n"); // TODO make this not hardcoded?
    socket.write("audio: 44100:24:2\n"); // TODO make this not hardcoded?
  }

  cb();
}

function stickerCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function subscribeCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function swapCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function swapidCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function tagtypesCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function unsubscribeCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}

function urlhandlersCmd(self, socket, args, cb) {
  cb(ERR_CODE_UNKNOWN, "unimplemented");
}
