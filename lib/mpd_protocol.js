var Duplex = require('stream').Duplex;
var EventEmitter = require('events').EventEmitter;
var util = require('util');
var Player = require('./player');
var path = require('path');
var log = require('./log');
var uuid = require('./uuid');
var keese = require('keese');

module.exports = MpdProtocol;

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

var compareSortKeyAndId = makeCompareProps(['sortKey', 'id']);

var tagTypes = {
  file: {
    caseCorrect: "File",
    grooveTag: "file",
  },
  artist: {
    caseCorrect: "Artist",
    grooveTag: "artistName",
  },
  artistsort: {
    caseCorrect: "ArtistSort",
    grooveTag: "artistName",
    sort: true,
  },
  album: {
    caseCorrect: "Album",
    grooveTag: "albumName",
  },
  albumartist: {
    caseCorrect: "AlbumArtist",
    grooveTag: "albumArtistName",
  },
  albumartistsort: {
    caseCorrect: "AlbumArtistSort",
    grooveTag: "albumArtistName",
    sort: true,
  },
  title: {
    caseCorrect: "Title",
    grooveTag: "name",
  },
  track: {
    caseCorrect: "Track",
    grooveTag: "track",
  },
  name: {
    caseCorrect: "Name",
    grooveTag: "name",
  },
  genre: {
    caseCorrect: "Genre",
    grooveTag: "genre",
  },
  date: {
    caseCorrect: "Date",
    grooveTag: "year",
  },
  composer: {
    caseCorrect: "Composer",
    grooveTag: "composerName",
  },
  performer: {
    caseCorrect: "Performer",
    grooveTag: "performerName",
  },
  disc: {
    caseCorrect: "Disc",
    grooveTag: "disc",
  },
};

var commands = {
  "add": {
    fn: addCmd,
    permission: 'control',
    args: [
      {
        name: 'uri',
        type: 'string',
      },
    ],
  },
  "addid": {
    permission: 'control',
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
    fn: function (self, args, cb) {
      var pos = args.position == null ? self.player.tracksInOrder.length : args.position;
      var dbFile = self.player.dbFilesByPath[args.uri];
      if (!dbFile) return cb(ERR_CODE_NO_EXIST, "Not found");
      var ids = self.playerServer.insertTracks(self.client, pos, [dbFile.key], false);
      self.push("Id: " + self.apiServer.toMpdId(ids[0]) + "\n");
      cb();
    },
  },
  "channels": {
    permission: 'read',
    fn: function (self, args, cb) {
      cb();
    },
  },
  "clear": {
    permission: 'control',
    fn: function (self, args, cb) {
      self.playerServer.clearQueue(self.client);
      cb();
    },
  },
  "clearerror": {
    permission: 'control',
    fn: function (self, args, cb) {
      cb();
    },
  },
  "close": {
    fn: function (self, args, cb) {
      self.close();
    },
  },
  "commands": {
    fn: function (self, args, cb) {
      for (var commandName in commands) {
        self.push("command: " + commandName + "\n");
      }
      cb();
    },
  },
  "consume": {
    permission: 'control',
    args: [
      {
        name: 'state',
        type: 'boolean',
      },
    ],
    fn: function (self, args, cb) {
      self.player.setAutoDjOn(args.state);
      cb();
    },
  },
  "count": {
    permission: 'read',
    args: [
      {
        name: 'tag',
        type: 'string',
      },
      {
        name: 'needle',
        type: 'string',
      },
    ],
    fn: function (self, args, cb) {
      var tagType = tagTypes[args.tag.toLowerCase()];
      if (!tagType) return cb(ERR_CODE_ARG, "incorrect arguments");
      var filters = [{
        field: tagType.grooveTag,
        value: args.needle,
      }];
      var songs = 0;
      forEachMatchingTrack(self, filters, true, function(track) {
        songs += 1;
      });
      self.push("songs: " + songs + "\n");
      self.push("playtime: 0\n");
      cb();
    },
  },
  "currentsong": {
    permission: 'read',
    fn: function (self, args, cb) {
      var currentTrack = self.player.currentTrack;
      if (!currentTrack) return cb();

      var start = currentTrack.index;
      var end = start + 1;
      writePlaylistInfo(self, start, end);
      cb();
    },
  },
  "delete": {
    permission: 'control',
    args: [
      {
        name: 'indexRange',
        type: 'range',
      },
    ],
    fn: function (self, args, cb) {
      var start = args.indexRange.start;
      var end = args.indexRange.end;
      var ids = [];
      for (var i = start; i < end; i += 1) {
        var track = self.player.tracksInOrder[i];
        if (!track) {
          cb(ERR_CODE_ARG, "Bad song index");
          return;
        }
        ids.push(track.id);
      }
      self.playerServer.removeQueueItems(self.client, ids);
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
    fn: function(self, args, cb) {
      self.playerServer.removeQueueItems(self.client, [args.id]);
      cb();
    },
  },
  "find": {
    permission: 'read',
    manualArgParsing: true,
    fn: function (self, args, cb) {
      findOrSearch(self, args, true, cb);
    },
  },
  "findadd": {
    permission: 'control',
    manualArgParsing: true,
    fn: function (self, args, cb) {
      findOrSearchAdd(self, args, true, cb);
    },
  },
  "idle": {}, // handled in a special case
  "list": {
    permission: 'read',
    manualArgParsing: true,
    fn: function (self, args, cb) {
      if (args.length < 1) {
        cb(ERR_CODE_ARG, "too few arguments for \"list\"");
        return;
      }
      var tagTypeId = args[0].toLowerCase();
      if (args.length === 2 && tagTypeId !== 'album') {
        cb(ERR_CODE_ARG, "should be \"Album\" for 3 arguments");
        return;
      }
      if (args.length !== 2 && args.length % 2 !== 1) {
        cb(ERR_CODE_ARG, "not able to parse args");
        return;
      }
      var targetTagType = tagTypes[tagTypeId];
      if (!targetTagType) return cb(ERR_CODE_ARG, "\"" + args[0] + "\" is not known");
      var caseCorrect = targetTagType.caseCorrect;

      var filters = [];
      if (args.length === 2) {
        filters.push({
          field: 'artistName',
          value: args[1],
        });
      } else {
        for (var i = 1; i < args.length; i += 2) {
          var tagType = tagTypes[args[i].toLowerCase()];
          if (!tagType) return cb(ERR_CODE_ARG, "\"" + args[i] + "\" is not known");
          filters.push({
            field: tagType.grooveTag,
            value: args[i+1],
          });
        }
      }

      var set = {};
      forEachMatchingTrack(self, filters, true, function(track) {
        var field = track[targetTagType.grooveTag];
        if (!set[field]) {
          set[field] = true;
          self.push(caseCorrect + ": " + field + "\n");
        }
      });

      cb();
    },
  },
  "listall": {
    permission: 'read',
    args: [
      {
        name: 'uri',
        type: 'string',
        optional: true,
      },
    ],
    fn: function (self, args, cb) {
      forEachListAll(self, args, writeFileOnly, cb);

      function writeFileOnly(track) {
        self.push("file: " + track.file + "\n");
      }
    },
  },
  "listallinfo": {
    permission: 'read',
    args: [
      {
        name: 'uri',
        type: 'string',
        optional: true,
      },
    ],
    fn: function (self, args, cb) {
      forEachListAll(self, args, doWriteTrackInfo, cb);

      function doWriteTrackInfo(track) {
        writeTrackInfo(self, track);
      }
    }
  },
  "listplaylist": {
    permission: 'read',
    args: [
      {
        name: 'name',
        type: 'string',
      },
    ],
    fn: function (self, args, cb) {
      var playlist = getPlaylistByName(self.player, args.name);
      if (!playlist) return cb(ERR_CODE_NO_EXIST, "No such playlist");
      var plItems = playlistItemsInOrder(playlist);
      plItems.forEach(function(plItem) {
        var track = self.player.libraryIndex.trackTable[plItem.key];
        self.push("file: " + track.file + "\n");
      });
      cb();
    },
  },
  "listplaylistinfo": {
    permission: 'read',
    args: [
      {
        name: 'name',
        type: 'string',
      },
    ],
    fn: function (self, args, cb) {
      var playlist = getPlaylistByName(self.player, args.name);
      if (!playlist) return cb(ERR_CODE_NO_EXIST, "No such playlist");
      var plItems = playlistItemsInOrder(playlist);
      plItems.forEach(function(plItem) {
        var track = self.player.libraryIndex.trackTable[plItem.key];
        writeTrackInfo(self, track);
      });
      cb();
    },
  },
  "listplaylists": {
    permission: 'read',
    fn: function (self, args, cb) {
      for (var playlistId in self.player.playlists) {
        var playlist = self.player.playlists[playlistId];
        self.push("playlist: " + playlist.name + "\n");
        self.push("Last-Modified: " + new Date(playlist.mtime).toISOString() + "\n");
      }
      cb();
    }
  },
  "load": {
    permission: 'control',
    args: [
      {
        name: 'name',
        type: 'string',
      },
      {
        name: 'indexRange',
        type: 'range',
        optional: true,
      },
    ],
    fn: function (self, args, cb) {
      var playlist = getPlaylistByName(self.player, args.name);
      if (!playlist) return cb(ERR_CODE_NO_EXIST, "No such playlist");

      var plItems = playlistItemsInOrder(playlist);

      var start = 0;
      var end = plItems.length;

      if (args.indexRange != null) {
        start = args.indexRange.start;
        end = args.indexRange.end;
      }

      var trackKeys = plItems.slice(start, end).map(getKey);
      self.playerServer.queueTrackKeys(self.client, trackKeys);
      cb();
    },
  },
  "lsinfo": {
    permission: 'read',
    args: [
      {
        name: 'uri',
        type: 'string',
        optional: true,
      },
    ],
    fn: function (self, args, cb) {
      var dirName = args.uri || "";
      if (dirName === '/') dirName = "";
      var dirEntry = self.player.dirs[dirName];
      if (!dirEntry) return cb(ERR_CODE_NO_EXIST, "Not found");
      var baseName, relPath;
      var dbFilesByPath = self.player.dbFilesByPath;
      for (baseName in dirEntry.entries) {
        relPath = path.join(dirName, baseName);
        var dbTrack = dbFilesByPath[relPath];
        if (dbTrack) writeTrackInfo(self, dbTrack);
      }
      for (baseName in dirEntry.dirEntries) {
        relPath = path.join(dirName, baseName);
        var childEntry = self.player.dirs[relPath];
        self.push("directory: " + relPath + "\n");
        self.push("Last-Modified: " + new Date(childEntry.mtime).toISOString() + "\n");
      }
      cb();
    },
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
    fn: function (self, args, cb) {
      self.playerServer.moveRangeToPos(self.client,
          args.fromRange.start, args.fromRange.end, args.pos);
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
    fn: function (self, args, cb) {
      self.playerServer.moveIdsToPos(self.client, [args.id], args.pos);
      cb();
    },
  },
  "next": {
    permission: 'control',
    fn: function (self, args, cb) {
      self.playerServer.next(self.client);
      cb();
    }
  },
  "notcommands": {
    fn: function (self, args, cb) {
      for (var commandName in commands) {
        var cmd = commands[commandName];
        if (cmd.permission != null && !self.havePermission(cmd.permission)) {
          self.push("command: " + commandName + "\n");
        }
      }
      cb();
    },
  },
  "outputs": {
    permission: 'read',
    fn: function (self, args, cb) {
      self.push("outputid: 0\n");
      self.push("outputname: default detected output\n");
      self.push("outputenabled: 1\n");
      self.push("outputid: 1\n");
      self.push("outputname: GrooveBasin HTTP Stream\n");
      self.push("outputenabled: 1\n");
      cb();
    },
  },
  "password": {
    args: [
      {
        name: 'password',
        type: 'string',
      },
    ],
    fn: function (self, args, cb) {
      var errMsg = self.playerServer.loginOneLineAuth(self.client, args.password);
      if (errMsg) {
        log.warn("MPD login failure:", errMsg);
        cb(ERR_CODE_PASSWORD, "incorrect password");
        return;
      }
      cb();
    },
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
    fn: function (self, args, cb) {
      if (args.pause == null) {
        // toggle
        if (self.player.isPlaying) {
          self.playerServer.pause(self.client);
        } else {
          self.playerServer.play(self.client);
        }
      } else {
        if (args.pause) {
          self.playerServer.pause(self.client);
        } else {
          self.playerServer.play(self.client);
        }
      }
      cb();
    },
  },
  "ping": {
    fn: function (self, args, cb) {
      cb();
    }
  },
  "play": {
    permission: 'control',
    fn: function (self, args, cb) {
      var currentTrack = self.player.currentTrack;
      if ((args.songPos == null || args.songPos === -1) && currentTrack) {
        self.playerServer.play(self.client);
        cb();
        return;
      }
      var currentIndex = currentTrack ? currentTrack.index : 0;
      var index = (args.songPos == null || args.songPos === -1) ? currentIndex : args.songPos;
      var track = self.playerServer.seekToIndex(self.client, index, 0);
      if (track) self.playerServer.play(self.client);
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
    fn: function (self, args, cb) {
      var id = args.id == null ? self.player.tracksInOrder[0].id : args.id;
      var item = self.player.playlist[id];
      if (!item) return cb(ERR_CODE_NO_EXIST, "No such song");
      var track = self.playerServer.seek(self.client, id, 0);
      if (track) self.playerServer.play(self.client);
      cb();
    },
  },
  "playlist": {
    permission: 'read',
    fn: function (self, args, cb) {
      var trackTable = self.player.libraryIndex.trackTable;
      self.player.tracksInOrder.forEach(function(track, index) {
        var dbTrack = trackTable[track.key];
        self.push(index + ":file: " + dbTrack.file + "\n");
      });
      cb();
    }
  },
  "playlistadd": {
    permission: 'playlist',
    args: [
      {
        name: 'name',
        type: 'string',
      },
      {
        name: 'uri',
        type: 'string',
      },
    ],
    fn: function (self, args, cb) {
      var playlist = getPlaylistByName(self.player, args.name);
      if (!playlist) {
        playlist = self.playerServer.playlistCreate(self.client, uuid(), args.name);
      }

      var dbFilesByPath = self.player.dbFilesByPath;
      var dbFile = dbFilesByPath[args.uri];
      if (!dbFile) return cb(ERR_CODE_NO_EXIST, "Not found");

      var plItems = playlistItemsInOrder(playlist);
      var lastPlItem = plItems[plItems.length - 1];
      var lastSortKey = lastPlItem && lastPlItem.sortKey;
      var newSortKey = keese(lastSortKey, null);

      var items = {};
      items[uuid()] = {
        key: dbFile.key,
        sortKey: newSortKey,
      };

      self.playerServer.playlistAddItems(self.client, playlist.id, items);

      cb();
    }
  },
  "playlistclear": {
    permission: 'playlist',
    args: [
      {
        name: 'name',
        type: 'string',
      },
    ],
    fn: function (self, args, cb) {
      var playlist = getPlaylistByName(self.player, args.name);
      if (!playlist) {
        playlist = self.playerServer.playlistCreate(self.client, uuid(), args.name);
      }

      var removals = {};
      removals[playlist.id] = Object.keys(playlist.items);
      self.playerServer.playlistRemoveItems(self.client, removals);
      cb();
    }
  },
  "playlistdelete": {
    permission: 'playlist',
    args: [
      {
        name: 'name',
        type: 'string',
      },
      {
        name: 'pos',
        type: 'integer',
      },
    ],
    fn: function (self, args, cb) {
      var playlist = getPlaylistByName(self.player, args.name);
      if (!playlist) return cb(ERR_CODE_NO_EXIST, "No such playlist");

      var plItems = playlistItemsInOrder(playlist);
      var plItem = plItems[args.pos];
      if (!plItem) return cb(ERR_CODE_ARG, "Bad song index");

      var removals = {};
      removals[playlist.id] = [plItem.id];
      self.playerServer.playlistRemoveItems(self.client, removals);
      cb();
    }
  },
  "playlistid": {
    permission: 'read',
    args: [
      {
        name: 'id',
        type: 'id',
        optional: true,
      },
    ],
    fn: function (self, args, cb) {
      var start = 0;
      var end = self.player.tracksInOrder.length;
      if (args.id != null) {
        start = self.player.playlist[args.id].index;
        end = start + 1;
      }
      writePlaylistInfo(self, start, end);
      cb();
    },
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
    fn: function (self, args, cb) {
      var start = 0;
      var end = self.player.tracksInOrder.length;

      if (args.indexRange != null) {
        start = args.indexRange.start;
        end = args.indexRange.end;
      }

      writePlaylistInfo(self, start, end);
      cb();
    },
  },
  "playlistmove": {
    permission: 'playlist',
    args: [
      {
        name: 'name',
        type: 'string',
      },
      {
        name: 'fromPos',
        type: 'integer',
      },
      {
        name: 'toPos',
        type: 'integer',
      },
    ],
    fn: function (self, args, cb) {
      var playlist = getPlaylistByName(self.player, args.name);
      if (!playlist) return cb(ERR_CODE_NO_EXIST, "No such playlist");

      var plItems = playlistItemsInOrder(playlist);
      var fromPlItem = plItems[args.fromPos];
      if (!fromPlItem) return cb(ERR_CODE_ARG, "Bad song index");

      var prevPlItem = plItems[args.toPos - 1];
      var nextPlItem = plItems[args.toPos];
      var newSortKey = keese(prevPlItem && prevPlItem.sortKey,
                             nextPlItem && nextPlItem.sortKey);
      var items = {};
      items[fromPlItem.id] = {sortKey: newSortKey};

      var updates = {};
      updates[playlist.id] = items;

      self.playerServer.playlistMoveItems(self.client, updates);
      cb();
    }
  },
  "plchanges": {
    permission: 'read',
    args: [
      {
        name: "version",
        type: "integer",
      },
    ],
    fn: function(self, args, cb) {
      writePlaylistInfo(self, 0, self.player.tracksInOrder.length);
      cb();
    },
  },
  "plchangesposid": {
    permission: 'read',
    args: [
      {
        name: "version",
        type: "integer",
      },
    ],
    fn: function (self, args, cb) {
      var tracksInOrder = self.player.tracksInOrder;
      for (var i = 0; i < tracksInOrder.length; i += 1) {
        var item = tracksInOrder[i];
        self.push("cpos: " + i + "\n");
        self.push("Id: " + self.apiServer.toMpdId(item.id) + "\n");
      }
      cb();
    },
  },
  "previous": {
    permission: 'control',
    fn: function (self, args, cb) {
      self.playerServer.prev(self.client);
      cb();
    }
  },
  "rename": {
    permission: 'playlist',
    args: [
      {
        name: 'oldName',
        type: 'string',
      },
      {
        name: 'newName',
        type: 'string',
      },
    ],
    fn: function (self, args, cb) {
      var playlist = getPlaylistByName(self.player, args.oldName);
      if (!playlist) return cb(ERR_CODE_NO_EXIST, "No such playlist");
      self.playerServer.playlistRename(self.client, playlist.id, args.newName);
      cb();
    },
  },
  "repeat": {
    permission: 'control',
    args: [
      {
        name: 'on',
        type: 'boolean',
      },
    ],
    fn: function (self, args, cb) {
      if (args.on && self.player.repeat === Player.REPEAT_OFF) {
        self.player.setRepeat(self.apiServer.singleMode ? Player.REPEAT_ONE : Player.REPEAT_ALL);
      } else if (!args.on && self.player.repeat !== Player.REPEAT_OFF) {
        self.player.setRepeat(Player.REPEAT_OFF);
      }
      cb();
    },
  },
  "rm": {
    permission: 'playlist',
    args: [
      {
        name: 'name',
        type: 'string',
      },
    ],
    fn: function (self, args, cb) {
      var playlist = getPlaylistByName(self.player, args.name);
      if (!playlist) return cb(ERR_CODE_NO_EXIST, "No such playlist");
      self.playerServer.playlistDelete(self.client, [playlist.id]);
      cb();
    },
  },
  "replay_gain_status": {
    permission: 'read',
    fn: function (self, args, cb) {
      self.push("replay_gain_mode: auto\n");
      cb();
    },
  },
  "rescan": {
    permission: 'admin',
    args: [
      {
        name: 'uri',
        type: 'string',
        optional: true,
      },
    ],
    fn: function (self, args, cb) {
      handleUpdate(self, args, true, cb);
    },
  },
  "save": {
    permission: 'playlist',
    args: [
      {
        name: 'name',
        type: 'string',
      },
    ],
    fn: function (self, args, cb) {
      var playlist = getPlaylistByName(self.player, args.name);
      if (playlist) return cb(ERR_CODE_EXIST, "Playlist already exists");

      var playlistId = uuid();
      self.playerServer.playlistCreate(self.client, playlistId, args.name);
      var items = {};
      for (var queueItemKey in self.player.playlist) {
        var queueItem = self.player.playlist[queueItemKey];
        items[queueItem.id] = {
          key: queueItem.key,
          sortKey: queueItem.sortKey,
        };
      }
      self.playerServer.playlistAddItems(self.client, playlistId, items);
      cb();
    },
  },
  "search": {
    permission: 'read',
    manualArgParsing: true,
    fn: function (self, args, cb) {
      findOrSearch(self, args, false, cb);
    },
  },
  "searchadd": {
    permission: 'control',
    manualArgParsing: true,
    fn: function (self, args, cb) {
      findOrSearchAdd(self, args, false, cb);
    },
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
    fn: function (self, args, cb) {
      var track = self.playerServer.seekToIndex(self.client, args.index, args.pos);
      if (track) self.playerServer.play(self.client);
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
    fn: function (self, args, cb) {
      var currentTrack = self.player.currentTrack;
      if (!currentTrack) return cb(ERR_CODE_PLAYER_SYNC, "Not playing");
      var track = self.playerServer.seek(self.client, currentTrack.id, args.pos);
      if (track) self.playerServer.play(self.client);
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
    fn: function (self, args, cb) {
      var track = self.playerServer.seek(self.client, args.id, args.pos);
      if (track) self.playerServer.play(self.client);
      cb();
    },
  },
  "setvol": {
    permission: 'control',
    args: [
      {
        name: 'vol',
        type: 'float',
      },
    ],
    fn: function (self, args, cb) {
      self.player.setVolume(args.vol / 100);
      cb();
    },
  },
  "shuffle": {
    permission: 'control',
    fn: function (self, args, cb) {
      self.playerServer.shufflePlaylist(self.client);
      cb();
    },
  },
  "single": {
    permission: 'control',
    args: [
      {
        name: 'single',
        type: 'boolean',
      },
    ],
    fn: function (self, args, cb) {
      self.apiServer.setSingleMode(args.single);
      if (self.apiServer.singleMode && self.player.repeat === Player.REPEAT_ALL) {
        self.player.setRepeat(Player.REPEAT_ONE);
      } else if (!self.apiServer.singleMode && self.player.repeat === Player.REPEAT_ONE) {
        self.player.setRepeat(Player.REPEAT_ALL);
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
  "stop": {
    permission: 'control',
    fn: function (self, args, cb) {
      self.playerServer.stop(self.client);
      cb();
    },
  },
  "swap": {
    permission: 'control',
    args: [
      {
        name: 'pos1',
        type: 'integer',
      },
      {
        name: 'pos2',
        type: 'integer',
      },
    ],
    fn: function (self, args, cb) {
      swapItems(self,
          self.player.tracksInOrder[args.pos1],
          self.player.tracksInOrder[args.pos2], cb);
    },
  },
  "swapid": {
    permission: 'control',
    args: [
      {
        name: 'id1',
        type: 'id',
      },
      {
        name: 'id2',
        type: 'id',
      },
    ],
    fn: function (self, args, cb) {
      swapItems(self, self.player.playlist[args.id1], self.player.playlist[args.id2], cb);
    },
  },
  "tagtypes": {
    permission: 'read',
    fn: function (self, args, cb) {
      for (var tagTypeId in tagTypes) {
        var tagType = tagTypes[tagTypeId];
        self.push("tagtype: " + tagType.caseCorrect + "\n");
      }
      cb();
    },
  },
  "update": {
    permission: 'control',
    args: [
      {
        name: 'uri',
        type: 'string',
        optional: true,
      },
    ],
    fn: function (self, args, cb) {
      handleUpdate(self, args, false, cb);
    },
  },
  "urlhandlers": {
    permission: 'read',
    fn: function (self, args, cb) {
      cb(); // no URL handlers
    },
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

var bootTime = new Date();

util.inherits(MpdProtocol, Duplex);
function MpdProtocol(options) {
  var streamOptions = extend(extend({}, options.streamOptions || {}), {decodeStrings: false});
  Duplex.call(this, streamOptions);
  this.player = options.player;
  this.apiServer = options.apiServer;
  this.playerServer = options.playerServer;

  this.closed = false;
  this.buffer = "";
  this.bufferIndex = 0;
  this.cmdListState = CMD_LIST_STATE_NONE;
  this.cmdList = [];
  this.okMode = false;
  this.isIdle = false;
  this.commandQueue = [];
  this.ongoingCommand = false;
  this.updatedSubsystems = {
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
  this._read = mpdRead;
  this._write = mpdWrite;

  this.mpdTransport = new EventEmitter();
  this.mpdTransport.sendMessage = noop;
  this.client = this.playerServer.createClient(this.mpdTransport, "MPD-Guest");
  this.id = uuid();
  this.apiServer.handleNewClient(this);

  this.push("OK MPD 0.19.0\n");
}

MpdProtocol.prototype.close = function() {
  if (this.closed) return;
  this.closed = true;
  this.push(null);
  this.apiServer.handleClientEnd(this);
  this.mpdTransport.emit('close');
};

MpdProtocol.prototype.havePermission = function(permName) {
  return this.playerServer.userHasPerm(this.client.user, permName);
};

function mpdRead(size) {}

function mpdWrite(chunk, encoding, callback) {
  var self = this;

  this.buffer += chunk;
  while (this.buffer.length) {
    var newlinePos = this.buffer.indexOf("\n", this.bufferIndex);
    if (newlinePos === -1) {
      this.bufferIndex = this.buffer.length;
      callback();
      return;
    }
    var lineLength = newlinePos - 1;
    if (this.buffer[lineLength] !== "\r") lineLength += 1;

    var line = this.buffer.substring(0, lineLength);
    this.buffer = this.buffer.substring(newlinePos + 1);
    this.bufferIndex = 0;
    handleLine(line);
  }
  callback();

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
    self.commandQueue.push([cmd, args]);
    flushQueue();
  }

  function flushQueue() {
    if (self.ongoingCommand) return;
    var queueItem = self.commandQueue.shift();
    if (!queueItem) return;
    var cmd = queueItem[0];
    var args = queueItem[1];
    self.ongoingCommand = true;
    handleCommand(cmd, args, function() {
      self.ongoingCommand = false;
      flushQueue();
    });
  }

  function handleCommand(cmdName, args, cb) {
    var cmdIndex = 0;

    switch (self.cmdListState) {
      case CMD_LIST_STATE_NONE:
        if (cmdName === 'command_list_begin' && args.length === 0) {
          self.cmdListState = CMD_LIST_STATE_LIST;
          self.cmdList = [];
          self.okMode = false;
          cb();
          return;
        } else if (cmdName === 'command_list_ok_begin' && args.length === 0) {
          self.cmdListState = CMD_LIST_STATE_LIST;
          self.cmdList = [];
          self.okMode = true;
          cb();
          return;
        } else {
          runOneCommand(cmdName, args, 0, function(ok) {
            if (ok) self.push("OK\n");
            cb();
          });
          return;
        }
        break;
      case CMD_LIST_STATE_LIST:
        if (cmdName === 'command_list_end' && args.length === 0) {
          self.cmdListState = CMD_LIST_STATE_NONE;

          runAndCheckOneCommand();
          return;
        } else {
          self.cmdList.push([cmdName, args]);
          cb();
          return;
        }
        break;
      default:
        throw new Error("unrecognized state");
    }

    function runAndCheckOneCommand() {
      var commandPayload = self.cmdList.shift();
      if (!commandPayload) {
        self.push("OK\n");
        cb();
        return;
      }
      var thisCmdName = commandPayload[0];
      var thisCmdArgs = commandPayload[1];
      runOneCommand(thisCmdName, thisCmdArgs, cmdIndex++, function(ok) {
        if (!ok) {
          cb();
          return;
        }
        if (self.okMode) self.push("list_OK\n");
        runAndCheckOneCommand();
      });
    }

    function runOneCommand(cmdName, args, index, cb) {
      if (cmdName === 'noidle') {
        var ok = self.isIdle;
        self.isIdle = false;
        cb(ok);
        return;
      }
      if (self.isIdle) {
        self.close();
        cb(false);
        return;
      }
      if (cmdName === 'idle') {
        if (!self.havePermission('read')) {
          cmdDone(ERR_CODE_PERMISSION, "you don't have permission for \"" + cmdName + "\"");
        } else {
          self.handleIdle(args);
        }
        cb(false);
        return;
      }
      execOneCommand(cmdName, args, cmdDone);

      function cmdDone(code, msg) {
        if (code) {
          log.warn("cmd err:", cmdName, JSON.stringify(args), msg);
          if (code === ERR_CODE_UNKNOWN) cmdName = "";
          self.push("ACK [" + code + "@" + index + "] {" + cmdName + "} " + msg + "\n");
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
      if (perm != null && !self.havePermission(perm)) {
        cb(ERR_CODE_PERMISSION, "you don't have permission for \"" + cmdName + "\"");
        return;
      }

      var argsParam;
      if (cmd.manualArgParsing) {
        argsParam = args;
      } else {
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
          var ret = parseArg.call(self, arg, argInfo);
          if (ret.msg) {
            cb(ERR_CODE_ARG, ret.msg);
            return;
          }
          namedArgs[argInfo.name] = ret.value;
        }
        argsParam = namedArgs;
      }
      log.debug("ok mpd command", cmdName, JSON.stringify(argsParam));
      cmd.fn(self, argsParam, cb);
    }
  }
}

MpdProtocol.prototype.handleIdle = function(args) {
  var anyUpdated = false;
  for (var subsystem in this.updatedSubsystems) {
    var isUpdated = this.updatedSubsystems[subsystem];
    if (isUpdated) {
      this.push("changed: " + subsystem + "\n");
      anyUpdated = true;
      this.updatedSubsystems[subsystem] = false;
    }
  }
  if (anyUpdated) {
    this.push("OK\n");
    this.isIdle = false;
    return;
  }
  this.isIdle = true;
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

function parseRange(str, argInfo) {
  var msg = null;
  var start = null;
  var end = null;
  var parts = str.split(":");
  if (parts.length === 2) {
    start = parseInt(parts[0], 10);
    end = parseInt(parts[1], 10);
  } else if (parts.length === 1) {
    start = parseInt(parts[0], 10);
    if (start === -1 && argInfo.optional) {
      return {
        value: null,
        msg: null,
      };
    }
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
  var grooveBasinId = this.apiServer.fromMpdId(results.value);
  var msg = grooveBasinId ? null : "No such song";
  return {
    value: grooveBasinId,
    msg: null,
  };
}

function writeTrackInfo(self, dbTrack) {
  self.push("file: " + dbTrack.file + "\n");
  if (dbTrack.mtime != null) {
    self.push("Last-Modified: " + new Date(dbTrack.mtime).toISOString() + "\n");
  }
  if (dbTrack.duration != null) {
    self.push("Time: " + Math.round(dbTrack.duration) + "\n");
  }
  if (dbTrack.artistName) {
    self.push("Artist: " + dbTrack.artistName + "\n");
  }
  if (dbTrack.albumName) {
    self.push("Album: " + dbTrack.albumName + "\n");
  }
  if (dbTrack.albumArtistName) {
    self.push("AlbumArtist: " + dbTrack.albumArtistName + "\n");
  }
  if (dbTrack.genre) {
    self.push("Genre: " + dbTrack.genre + "\n");
  }
  if (dbTrack.name) {
    self.push("Title: " + dbTrack.name + "\n");
  }
  if (dbTrack.track != null) {
    if (dbTrack.trackCount != null) {
      self.push("Track: " + dbTrack.track + "/" + dbTrack.trackCount + "\n");
    } else {
      self.push("Track: " + dbTrack.track + "\n");
    }
  }
  if (dbTrack.composerName) {
    self.push("Composer: " + dbTrack.composerName + "\n");
  }
  if (dbTrack.disc != null) {
    if (dbTrack.discCount != null) {
      self.push("Disc: " + dbTrack.disc + "/" + dbTrack.discCount + "\n");
    } else {
      self.push("Disc: " + dbTrack.disc + "\n");
    }
  }
  if (dbTrack.year != null) {
    self.push("Date: " + dbTrack.year + "\n");
  }
}

function writePlaylistInfo(self, start, end) {
  var trackTable = self.player.libraryIndex.trackTable;
  for (var i = start; i < end; i += 1) {
    var item = self.player.tracksInOrder[i];
    var track = trackTable[item.key];
    writeTrackInfo(self, track);
    self.push("Pos: " + i + "\n");
    self.push("Id: " + self.apiServer.toMpdId(item.id) + "\n");
  }
}

function forEachMatchingTrack(self, filters, caseSensitive, fn) {
  // TODO: support 'in' as tag type
  var trackTable = self.player.libraryIndex.trackTable;
  if (!caseSensitive) {
    filters.forEach(function(filter) {
      filter.value = filter.value.toLowerCase();
    });
  }
  for (var key in trackTable) {
    var track = trackTable[key];
    if (filters.length === 0) {
      fn(track);
      continue;
    }
    var matches = false;
    for (var filterIndex = 0; filterIndex < filters.length; filterIndex += 1) {
      var filter = filters[filterIndex];
      var filterField = String(track[filter.field]);
      if (!filterField) continue;
      if (!caseSensitive && filterField) filterField = filterField.toLowerCase();

      /* assumes:
       *   caseSensitive  implies "find"
       *   !caseSensitive implies "search"
       */
      if (caseSensitive) {
        if (filterField === filter.value) {
          matches = true;
          break;
        }
      } else if (filterField.indexOf(filter.value) > -1) {
          matches = true;
          break;
      }
    }
    if (matches) fn(track);
  }
}

function forEachListAll(self, args, onTrack, cb) {
  var dirName = args.uri || "";
  var dirEntry = self.player.dirs[dirName];
  if (!dirEntry) return cb(ERR_CODE_NO_EXIST, "Not found");
  printOneDir(dirEntry);
  cb();

  function printOneDir(dirEntry) {
    var baseName, relPath;
    if (dirEntry.dirName) { // exclude root
      self.push("directory: " + dirEntry.dirName + "\n");
      self.push("Last-Modified: " + new Date(dirEntry.mtime).toISOString() + "\n");
    }
    var dbFilesByPath = self.player.dbFilesByPath;
    for (baseName in dirEntry.entries) {
      relPath = path.join(dirEntry.dirName, baseName);
      var dbTrack = dbFilesByPath[relPath];
      if (dbTrack) onTrack(dbTrack);
    }
    for (baseName in dirEntry.dirEntries) {
      relPath = path.join(dirEntry.dirName, baseName);
      var childEntry = self.player.dirs[relPath];
      if (childEntry) {
        printOneDir(childEntry);
      }
    }
  }
}

function parseFindArgs(self, args, caseSensitive, onTrack, cb, onFinish) {
  if (args.length < 2) {
    cb(ERR_CODE_ARG, "too few arguments for \"find\"");
    return;
  }
  if (args.length % 2 !== 0) {
    cb(ERR_CODE_ARG, "incorrect arguments");
    return;
  }
  var filters = [];
  var tagType;
  for (var i = 0; i < args.length; i += 2) {
    var tagsToSearch = [];
    if (args[i].toLowerCase() === "any") {
      // Special case the any key. Just search everything.
      for (tagType in tagTypes) {
        tagsToSearch.push(tagTypes[tagType]);
      }
    } else {
      tagType = tagTypes[args[i].toLowerCase()];
      if (!tagType) return cb(ERR_CODE_ARG, "\"" + args[i] + "\" is not known");
      tagsToSearch.push(tagType);
    }
    for (var j = 0; j < tagsToSearch.length; j++) {
      filters.push({
        field: tagsToSearch[j].grooveTag,
        value: args[i+1],
      });
    }
    forEachMatchingTrack(self, filters, caseSensitive, onTrack);
  }
  onFinish();
}

function handleUpdate(self, args, forceRescan, cb) {
  var dirEntry = self.player.dirs[args.uri || ""];
  if (!dirEntry) {
    cb(ERR_CODE_ARG, "Malformed path");
    return;
  }
  if (forceRescan) {
    self.player.deleteDbMtimes();
  }
  self.player.requestUpdateDb(dirEntry.dirName);
  self.push("updating_db: 1\n");
  cb();
}

function findOrSearch(self, args, caseSensitive, cb) {
  parseFindArgs(self, args, caseSensitive, onTrack, cb, cb);
  function onTrack(track) {
    writeTrackInfo(self, track);
  }
}

function findOrSearchAdd(self, args, caseSensitive, cb) {
  var keys = [];
  parseFindArgs(self, args, caseSensitive, onTrack, cb, onFinish);

  function onTrack(track) {
    keys.push(track.key);
  }

  function onFinish() {
    self.playerServer.queueTrackKeys(self.client, keys);
    cb();
  }
}

function swapItems(self, item1, item2, cb) {
  if (!item1 || !item2) return cb(ERR_CODE_ARG, "No such song");
  var o = {};
  o[item1.id] = {sortKey: item2.sortKey};
  o[item2.id] = {sortKey: item1.sortKey};
  self.playerServer.moveQueueItems(self.client, o);
  cb();
}

function addCmd(self, args, cb) {
  var dbFilesByPath = self.player.dbFilesByPath;
  var dbFile = dbFilesByPath[args.uri];

  if (dbFile) {
    self.playerServer.queueTrackKeys(self.client, [dbFile.key]);
    cb();
    return;
  }

  var keys = [];
  var dirEntry = self.player.dirs[args.uri];
  if (!dirEntry) return cb(ERR_CODE_NO_EXIST, "Not found");
  addDir(dirEntry);
  if (keys.length === 0) {
    cb(ERR_CODE_NO_EXIST, "Not found");
    return;
  }
  self.playerServer.queueTrackKeys(self.client, keys);
  cb();

  function addDir(dirEntry) {
    var baseName;
    for (baseName in dirEntry.entries) {
      var relPath = path.join(dirEntry.dirName, baseName);
      var dbFile = dbFilesByPath[relPath];
      if (dbFile) keys.push(dbFile.key);
    }
    for (baseName in dirEntry.dirEntries) {
      var childEntry = self.player.dirs[path.join(dirEntry.dirName, baseName)];
      addDir(childEntry);
    }
  }
}

function statsCmd(self, args, cb) {
  var uptime = Math.floor((new Date() - bootTime) / 1000);

  var libraryIndex = self.player.libraryIndex;
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
  self.push("artists: " + artists + "\n");
  self.push("albums: " + albums + "\n");
  self.push("songs: " + songs + "\n");
  self.push("uptime: " + uptime + "\n");
  self.push("playtime: 0\n"); // TODO keep track of this?
  self.push("db_playtime: " + dbPlaytime + "\n");
  self.push("db_update: " + dbUpdate + "\n");

  cb();
}

function statusCmd(self, args, cb) {
  var volume = Math.round(self.player.volume * 100);

  var repeat, single;
  switch (self.player.repeat) {
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
      single = +self.apiServer.singleMode;
      break;
  }
  var consume = +self.player.autoDjOn;
  var playlistLength = self.player.tracksInOrder.length;
  var currentTrack = self.player.currentTrack;
  var state;
  if (self.player.isPlaying) {
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
  var trackTable = self.player.libraryIndex.trackTable;
  if (currentTrack) {
    song = currentTrack.index;
    songId = self.apiServer.toMpdId(currentTrack.id);
    var nextTrack = self.player.tracksInOrder[currentTrack.index + 1];
    if (nextTrack) {
      nextSong = nextTrack.index;
      nextSongId = self.apiServer.toMpdId(nextTrack.id);
    }

    var dbTrack = trackTable[currentTrack.key];
    elapsed = self.player.getCurPos();
    time = Math.round(elapsed) + ":" + Math.round(dbTrack.duration);
  }

  self.push("volume: " + volume + "\n");
  self.push("repeat: " + repeat + "\n");
  self.push("random: 0\n");
  self.push("single: " + single + "\n");
  self.push("consume: " + consume + "\n");
  self.push("playlist: 0\n"); // TODO what to do with this?
  self.push("playlistlength: " + playlistLength + "\n");
  self.push("xfade: 0\n");
  self.push("mixrampdb: 0.000000\n");
  self.push("mixrampdelay: nan\n");
  self.push("state: " + state + "\n");
  if (song != null) {
    self.push("song: " + song + "\n");
    self.push("songid: " + songId + "\n");
    if (nextSong != null) {
      self.push("nextsong: " + nextSong + "\n");
      self.push("nextsongid: " + nextSongId + "\n");
    }
    self.push("time: " + time + "\n");
    self.push("elapsed: " + elapsed + "\n");
    self.push("bitrate: 192\n"); // TODO make this not hardcoded?
    self.push("audio: 44100:24:2\n"); // TODO make this not hardcoded?
  }

  cb();
}

function extend(o, src) {
  for (var key in src) o[key] = src[key];
  return o;
}

function noop() {}

function getPlaylistByName(player, name) {
  for (var id in player.playlists) {
    var playlist = player.playlists[id];
    if (playlist.name === name) {
      return playlist;
    }
  }
  return null;
}

function playlistItemsInOrder(playlist) {
  var items = [];
  for (var id in playlist.items) {
    var item = playlist.items[id];
    items.push(item);
  }
  items.sort(compareSortKeyAndId);
  return items;
}

function operatorCompare(a, b){
  if (a === b) {
    return 0;
  } else if (a < b) {
    return -1;
  } else {
    return 1;
  }
}

function makeCompareProps(props){
  return function(a, b) {
    for (var i = 0; i < props.length; i += 1) {
      var prop = props[i];
      var result = operatorCompare(a[prop], b[prop]);
      if (result) return result;
    }
    return 0;
  };
}

function getKey(item) {
  return item.key;
}
