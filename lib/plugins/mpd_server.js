var net = require('net');

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
  "add": addCmd,
  "addid": addidCmd,
  "channels": channelsCmd,
  "clear": clearCmd,
  "clearerror": clearerrorCmd,
  "close": closeCmd,
  "commands": commandsCmd,
  "config": configCmd,
  "consume": consumeCmd,
  "count": countCmd,
  "crossfade": crossfadeCmd,
  "currentsong": currentsongCmd,
  "decoders": decodersCmd,
  "delete": deleteCmd,
  "deleteid": deleteidCmd,
  "disableoutput": disableoutputCmd,
  "enableoutput": enableoutputCmd,
  "find": findCmd,
  "findadd": findaddCmd,
  "idle": idleCmd,
  "kill": killCmd,
  "list": listCmd,
  "listall": listallCmd,
  "listallinfo": listallinfoCmd,
  "listplaylist": listplaylistCmd,
  "listplaylistinfo": listplaylistinfoCmd,
  "listplaylists": listplaylistsCmd,
  "load": loadCmd,
  "lsinfo": lsinfoCmd,
  "mixrampdb": mixrampdbCmd,
  "mixrampdelay": mixrampdelayCmd,
  "move": moveCmd,
  "moveid": moveidCmd,
  "next": nextCmd,
  "notcommands": notcommandsCmd,
  "outputs": outputsCmd,
  "password": passwordCmd,
  "pause": pauseCmd,
  "ping": pingCmd,
  "play": playCmd,
  "playid": playidCmd,
  "playlist": playlistCmd,
  "playlistadd": playlistaddCmd,
  "playlistclear": playlistclearCmd,
  "playlistdelete": playlistdeleteCmd,
  "playlistfind": playlistfindCmd,
  "playlistid": playlistidCmd,
  "playlistinfo": playlistinfoCmd,
  "playlistmove": playlistmoveCmd,
  "playlistsearch": playlistsearchCmd,
  "plchanges": plchangesCmd,
  "plchangesposid": plchangesposidCmd,
  "previous": previousCmd,
  "prio": prioCmd,
  "prioid": prioidCmd,
  "random": randomCmd,
  "readmessages": readmessagesCmd,
  "rename": renameCmd,
  "repeat": repeatCmd,
  "replay_gain_mode": replay_gain_modeCmd,
  "replay_gain_status": replay_gain_statusCmd,
  "rescan": rescanCmd,
  "rm": rmCmd,
  "save": saveCmd,
  "search": searchCmd,
  "searchadd": searchaddCmd,
  "searchaddpl": searchaddplCmd,
  "seek": seekCmd,
  "seekcur": seekcurCmd,
  "seekid": seekidCmd,
  "sendmessage": sendmessageCmd,
  "setvol": setvolCmd,
  "shuffle": shuffleCmd,
  "single": singleCmd,
  "stats": statsCmd,
  "status": statusCmd,
  "sticker": stickerCmd,
  "stop": stopCmd,
  "subscribe": subscribeCmd,
  "swap": swapCmd,
  "swapid": swapidCmd,
  "tagtypes": tagtypesCmd,
  "unsubscribe": unsubscribeCmd,
  "update": updateCmd,
  "urlhandlers": urlhandlersCmd,
};

function MpdServer(gb) {
  this.gb = gb;
}

var stateCount = 0;
var STATE_CMD       = stateCount++;
var STATE_CMD_SPACE = stateCount++;
var STATE_ARG       = stateCount++;
var STATE_ARG_QUOTE = stateCount++;
var STATE_ARG_ESC   = stateCount++;

var cmdListStateCount = 0;
var CMD_LIST_STATE_NONE   = cmdListStateCount++;
var CMD_LIST_STATE_LIST   = cmdListStateCount++;

MpdServer.prototype.initialize = function(cb) {
  var self = this;
  var mpdPort = self.gb.config.mpdPort;
  var server = net.createServer(onSocketConnection);
  server.listen(mpdPort, function() {
    console.info("MPD Protocol listening on port", mpdPort);
    cb();
  });

  function onSocketConnection(socket) {
    var buffer = "";
    var cmdListState = CMD_LIST_STATE_NONE;
    var cmdList = [];
    var okMode = false;

    socket.setEncoding('utf8');
    socket.write("OK MPD 0.17.0\n");
    socket.on('data', bufferStr);
    
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
      console.info("handleLine", JSON.stringify(line));
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
      handleCommand(cmd, args);
    }

    function handleCommand(cmdName, args) {
      console.info("handleCommand", JSON.stringify(cmdName), JSON.stringify(args));
      switch (cmdListState) {
        case CMD_LIST_STATE_NONE:
          if (cmdName === 'command_list_begin' && args.length === 0) {
            cmdListState = CMD_LIST_STATE_LIST;
            okMode = false;
            return;
          } else if (cmdName === 'command_list_ok_begin' && args.length === 0) {
            cmdListState = CMD_LIST_STATE_LIST;
            okMode = true;
            return;
          } else {
            if (!runOneCommand(cmdName, args, 0)) {
              socket.write("OK\n");
            }
          }
          break;
        case CMD_LIST_STATE_LIST:
          if (cmdName === 'command_list_end' && args.length === 0) {
            var errorOccurred = false;
            for (var i = 0; i < cmdList.length; i += 1) {
              var commandPayload = cmdList[i];
              var thisCmdName = commandPayload[0];
              var thisCmdArgs = commandPayload[1];
              if (runOneCommand(thisCmdName, thisCmdArgs, i)) {
                errorOccurred = true;
                break;
              } else if (okMode) {
                socket.write("list_OK\n");
              }
            }
            if (!errorOccurred) {
              socket.write("OK\n");
            }
            cmdList = [];
            cmdListState = CMD_LIST_STATE_NONE;
            return;
          } else {
            cmdList.push([cmdName, args]);
          }
          break;
        default:
          throw new Error("unrecognized state");
      }
    }

    function runOneCommand(cmdName, args, index) {
      var returnValue = execOneCommand(cmdName, args);
      if (returnValue) {
        var code = returnValue[0];
        var msg = returnValue[1];
        if (code === ERR_CODE_UNKNOWN) cmdName = "";
        socket.write("ACK [" + code + "@" + index + "] {" + cmdName + "} " + msg + "\n");
        return true;
      }
    }

    function execOneCommand(cmdName, args) {
      if (!cmdName.length) return [ERR_CODE_UNKNOWN, "No command given"];
      var cmd = commands[cmdName];
      if (!cmd) return [ERR_CODE_UNKNOWN, "unknown command \"" + cmdName + "\""];
      return cmd(self, socket, args);
    }
  }
}

function isSpace(c) {
  return c === '\t' || c === ' ';
}

function parseBool(str) {
  return !!parseInt(str, 10);
}

function addCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function addidCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function channelsCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function clearCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function clearerrorCmd(self, socket, args) {
  // nothing to do
}

function closeCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function commandsCmd(self, socket, args) {
  for (var commandName in commands) {
    socket.write("command: " + commandName + "\n");
  }
}

function configCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function consumeCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function countCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function crossfadeCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function currentsongCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function decodersCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function deleteCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function deleteidCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function disableoutputCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function enableoutputCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function findCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function findaddCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function idleCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function killCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function listCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function listallCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function listallinfoCmd(self, socket, args) {
  var trackTable = self.gb.player.libraryIndex.trackTable;
  for (var key in trackTable) {
    var dbTrack = trackTable[key];
    socket.write("file: " + dbTrack.file + "\n");
    if (dbTrack.mtime != null) {
      socket.write("Last-Modified: " + JSON.stringify(dbTrack.mtime) + "\n");
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
}

function listplaylistCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function listplaylistinfoCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function listplaylistsCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function loadCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function lsinfoCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function mixrampdbCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function mixrampdelayCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function moveCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function moveidCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function nextCmd(self, socket, args) {
  self.gb.player.next();
}

function notcommandsCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function outputsCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function passwordCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function pauseCmd(self, socket, args) {
  if (args.length > 1) {
    return [ERR_CODE_ARG, "too many arguments for \"pause\""];
  } else if (args.length === 1) {
    var pause = parseBool(args[0]);
    if (pause) {
      self.gb.player.pause();
    } else {
      self.gb.player.play();
    }
  } else {
    // toggle
    if (self.gb.player.isPlaying) {
      self.gb.player.pause();
    } else {
      self.gb.player.play();
    }
  }
}

function pingCmd(self, socket, args) {
  // nothing to do
}

function playCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function playidCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function playlistCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function playlistaddCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function playlistclearCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function playlistdeleteCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function playlistfindCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function playlistidCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function playlistinfoCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function playlistmoveCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function playlistsearchCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function plchangesCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function plchangesposidCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function previousCmd(self, socket, args) {
  self.gb.player.prev();
}

function prioCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function prioidCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function randomCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function readmessagesCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function renameCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function repeatCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function replay_gain_modeCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function replay_gain_statusCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function rescanCmd(self, socket, args) {
  socket.write("updating_db: 1\n");
}

function rmCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function saveCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function searchCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function searchaddCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function searchaddplCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function seekCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function seekcurCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function seekidCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function sendmessageCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function setvolCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function shuffleCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function singleCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function statsCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function statusCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function stickerCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function stopCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function subscribeCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function swapCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function swapidCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function tagtypesCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function unsubscribeCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function updateCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}

function urlhandlersCmd(self, socket, args) {
  return [ERR_CODE_UNKNOWN, "unimplemented"];
}
