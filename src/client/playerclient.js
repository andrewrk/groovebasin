var EventEmitter = require('events').EventEmitter;
var util = require('util');
var uuid = require('./uuid');
var MusicLibraryIndex = require('music-library-index');
var keese = require('keese');
var jsondiffpatch = require('jsondiffpatch');

module.exports = PlayerClient;

var compareSortKeyAndId = makeCompareProps(['sortKey', 'id']);
var compareNameAndId = makeCompareProps(['name', 'id']);

PlayerClient.REPEAT_OFF = 0;
PlayerClient.REPEAT_ONE = 1;
PlayerClient.REPEAT_ALL = 2;

PlayerClient.GUEST_USER_ID = "(guest)";

util.inherits(PlayerClient, EventEmitter);
function PlayerClient(socket) {
  EventEmitter.call(this);

  var self = this;
  self.socket = socket;
  self.serverTimeOffset = 0;
  self.serverTrackStartDate = null;

  self.queueFromServer = undefined;
  self.queueFromServerVersion = null;
  self.libraryFromServer = undefined;
  self.libraryFromServerVersion = null;
  self.scanningFromServer = undefined;
  self.scanningFromServerVersion = null;
  self.playlistsFromServer = undefined;
  self.playlistsFromServerVersion = null;
  self.eventsFromServer = undefined;
  self.eventsFromServerVersion = null;
  self.usersFromServer = undefined;
  self.usersFromServerVersion = null;

  self.resetServerState();
  self.socket.on('disconnect', function() {
    self.resetServerState();
  });
  if (self.socket.isConnected) {
    self.resubscribe();
  } else {
    self.socket.on('connect', self.resubscribe.bind(self));
  }
  self.socket.on('time', function(o) {
    self.serverTimeOffset = new Date(o) - new Date();
    self.updateTrackStartDate();
    self.emit('statusupdate');
  });
  self.socket.on('volume', function(volume) {
    self.volume = volume;
    self.emit('statusupdate');
  });
  self.socket.on('repeat', function(repeat) {
    self.repeat = repeat;
    self.emit('statusupdate');
  });
  self.socket.on('streamers', function(streamers) {
    self.streamers = streamers;
    self.emit('streamers');
  });

  self.socket.on('currentTrack', function(o) {
    self.isPlaying = o.isPlaying;
    self.serverTrackStartDate = o.trackStartDate && new Date(o.trackStartDate);
    self.pausedTime = o.pausedTime;
    self.currentItemId = o.currentItemId;
    self.updateTrackStartDate();
    self.updateCurrentItem();
    self.emit('statusupdate');
    self.emit('currentTrack');
  });

  self.socket.on('queue', function(o) {
    if (o.reset) self.queueFromServer = undefined;
    self.queueFromServer = jsondiffpatch.patch(self.queueFromServer, o.delta);
    deleteUndefineds(self.queueFromServer);
    self.queueFromServerVersion = o.version;
    self.updateQueueIndex();
    self.emit('statusupdate');
    self.emit('queueUpdate');
  });

  self.socket.on('library', function(o) {
    if (o.reset) self.libraryFromServer = undefined;
    self.libraryFromServer = jsondiffpatch.patch(self.libraryFromServer, o.delta);
    deleteUndefineds(self.libraryFromServer);
    self.libraryFromServerVersion = o.version;
    self.library.clear();
    for (var key in self.libraryFromServer) {
      var track = self.libraryFromServer[key];
      self.library.addTrack(track);
    }
    self.library.rebuild();
    self.updateQueueIndex();
    self.haveFileListCache = true;
    var lastQuery = self.lastQuery;
    self.lastQuery = null;
    self.search(lastQuery);
  });

  self.socket.on('scanning', function(o) {
    if (o.reset) self.scanningFromServer = undefined;
    self.scanningFromServer = jsondiffpatch.patch(self.scanningFromServer, o.delta);
    deleteUndefineds(self.scanningFromServer);
    self.scanningFromServerVersion = o.version;
    self.emit('scanningUpdate');
  });

  self.socket.on('playlists', function(o) {
    if (o.reset) self.playlistsFromServer = undefined;
    self.playlistsFromServer = jsondiffpatch.patch(self.playlistsFromServer, o.delta);
    deleteUndefineds(self.playlistsFromServer);
    self.playlistsFromServerVersion = o.version;
    self.updatePlaylistsIndex();
    self.emit('playlistsUpdate');
  });

  self.socket.on('events', function(o) {
    if (o.reset) self.eventsFromServer = undefined;
    self.eventsFromServer = jsondiffpatch.patch(self.eventsFromServer, o.delta);
    deleteUndefineds(self.eventsFromServer);
    self.eventsFromServerVersion = o.version;
    self.sortEventsFromServer();
    if (o.reset) self.markAllEventsSeen();
    self.emit('events');
  });

  self.socket.on('users', function(o) {
    if (o.reset) self.usersFromServer = undefined;
    self.usersFromServer = jsondiffpatch.patch(self.usersFromServer, o.delta);
    deleteUndefineds(self.usersFromServer);
    self.usersFromServerVersion = o.version;
    self.sortUsersFromServer();
    self.emit('users');
  });

  function deleteUndefineds(o) {
    for (var key in o) {
      if (o[key] === undefined) delete o[key];
    }
  }
}

PlayerClient.prototype.resubscribe = function(){
  this.sendCommand('subscribe', {
    name: 'library',
    delta: true,
    version: this.libraryFromServerVersion,
  });
  this.sendCommand('subscribe', {name: 'volume'});
  this.sendCommand('subscribe', {name: 'repeat'});
  this.sendCommand('subscribe', {name: 'currentTrack'});
  this.sendCommand('subscribe', {
    name: 'queue',
    delta: true,
    version: this.queueFromServerVersion,
  });
  this.sendCommand('subscribe', {
    name: 'scanning',
    delta: true,
    version: this.scanningFromServerVersion,
  });
  this.sendCommand('subscribe', {
    name: 'playlists',
    delta: true,
    version: this.playlistsFromServerVersion,
  });
  this.sendCommand('subscribe', {name: 'streamers'});
  this.sendCommand('subscribe', {
    name: 'users',
    delta: true,
    version: this.usersFromServerVersion,
  });
  this.sendCommand('subscribe', {
    name: 'events',
    delta: true,
    version: this.eventsFromServerVersion,
  });
};

PlayerClient.prototype.sortEventsFromServer = function() {
  this.eventsList = [];
  this.unseenChatCount = 0;
  for (var id in this.eventsFromServer) {
    var serverEvent = this.eventsFromServer[id];
    var seen = !!this.seenEvents[id];
    var ev = {
      id: id,
      date: new Date(serverEvent.date),
      type: serverEvent.type,
      sortKey: serverEvent.sortKey,
      text: serverEvent.text,
      pos: serverEvent.pos ? serverEvent.pos : 0,
      seen: seen,
    };
    if (!seen && serverEvent.type === 'chat') {
      this.unseenChatCount += 1;
    }
    if (serverEvent.trackId) {
      ev.track = this.library.trackTable[serverEvent.trackId];
    }
    if (serverEvent.userId) {
      ev.user = this.usersTable[serverEvent.userId];
    }
    this.eventsList.push(ev);
  }
  this.eventsList.sort(compareSortKeyAndId);
};

PlayerClient.prototype.markAllEventsSeen = function() {
  this.seenEvents = {};
  for (var i = 0; i < this.eventsList.length; i += 1) {
    var ev = this.eventsList[i];
    this.seenEvents[ev.id] = true;
    ev.seen = true;
  }
  this.unseenChatCount = 0;
};

PlayerClient.prototype.sortUsersFromServer = function() {
  this.usersList = [];
  this.usersTable = {};
  for (var id in this.usersFromServer) {
    var serverUser = this.usersFromServer[id];
    var user = {
      id: id,
      name: serverUser.name,
      perms: serverUser.perms,
      requested: !!serverUser.requested,
      approved: !!serverUser.approved,
      streaming: !!serverUser.streaming,
      connected: !!serverUser.connected,
    };
    this.usersTable[id] = user;
    this.usersList.push(user);
  }
  this.usersList.sort(compareUserNames);
};

PlayerClient.prototype.updateTrackStartDate = function() {
  this.trackStartDate = (this.serverTrackStartDate != null) ?
    new Date(new Date(this.serverTrackStartDate) - this.serverTimeOffset) : null;
};

PlayerClient.prototype.updateCurrentItem = function() {
  this.currentItem = (this.currentItemId != null) ?
    this.queue.itemTable[this.currentItemId] : null;
};

PlayerClient.prototype.clearStoredPlaylists = function() {
  this.stored_playlist_table = {};
  this.stored_playlist_item_table = {};
  this.stored_playlists = [];
};

PlayerClient.prototype.sortAndIndexPlaylists = function() {
  this.stored_playlists.sort(compareNameAndId);
  this.stored_playlists.forEach(function(playlist, index) {
    playlist.index = index;
  });
};

PlayerClient.prototype.updatePlaylistsIndex = function() {
  this.clearStoredPlaylists();
  if (!this.playlistsFromServer) return;
  for (var id in this.playlistsFromServer) {
    var playlistFromServer = this.playlistsFromServer[id];
    var playlist = {
      itemList: [],
      itemTable: {},
      id: playlistFromServer.id,
      name: playlistFromServer.name,
      index: 0, // we'll set this correctly later
    };
    for (var itemId in playlistFromServer.items) {
      var itemFromServer = playlistFromServer.items[itemId];
      var track = this.library.trackTable[itemFromServer.key];
      var item = {
        id: itemId,
        sortKey: itemFromServer.sortKey,
        isRandom: false,
        track: track,
        playlist: playlist,
      };
      playlist.itemTable[itemId] = item;
      this.stored_playlist_item_table[itemId] = item;
    }
    this.refreshPlaylistList(playlist);
    this.stored_playlists.push(playlist);
    this.stored_playlist_table[playlist.id] = playlist;
  }
  this.sortAndIndexPlaylists();
};

PlayerClient.prototype.updateQueueIndex = function() {
  this.clearQueue();
  if (!this.queueFromServer) return;
  for (var id in this.queueFromServer) {
    var item = this.queueFromServer[id];
    var track = this.library.trackTable[item.key];
    this.queue.itemTable[id] = {
      id: id,
      sortKey: item.sortKey,
      isRandom: item.isRandom,
      track: track,
      playlist: this.queue,
    };
  }
  this.refreshPlaylistList(this.queue);
  this.updateCurrentItem();
};

PlayerClient.prototype.isScanning = function(track) {
  var scanInfo = this.scanningFromServer && this.scanningFromServer[track.key];
  return scanInfo && (!scanInfo.fingerprintDone || !scanInfo.loudnessDone);
};

PlayerClient.prototype.search = function(query) {
  query = query.trim();

  var words = query.split(/\s+/);
  query = words.join(" ");
  if (query === this.lastQuery) return;

  this.lastQuery = query;
  this.searchResults = this.library.search(query);
  this.emit('libraryupdate');
  this.emit('queueUpdate');
  this.emit('statusupdate');
};

PlayerClient.prototype.getDefaultQueuePosition = function() {
  var previousKey = this.currentItem && this.currentItem.sortKey;
  var nextKey = null;
  var startPos = this.currentItem ? this.currentItem.index + 1 : 0;
  for (var i = startPos; i < this.queue.itemList.length; i += 1) {
    var track = this.queue.itemList[i];
    var sortKey = track.sortKey;
    if (track.isRandom) {
      nextKey = sortKey;
      break;
    }
    previousKey = sortKey;
  }
  return {
    previousKey: previousKey,
    nextKey: nextKey
  };
};

PlayerClient.prototype.queueTracks = function(keys, previousKey, nextKey) {
  if (!keys.length) return;

  if (previousKey == null && nextKey == null) {
    var defaultPos = this.getDefaultQueuePosition();
    previousKey = defaultPos.previousKey;
    nextKey = defaultPos.nextKey;
  }

  var items = {};
  for (var i = 0; i < keys.length; i += 1) {
    var key = keys[i];
    var sortKey = keese(previousKey, nextKey);
    var id = uuid();
    items[id] = {
      key: key,
      sortKey: sortKey,
    };
    this.queue.itemTable[id] = {
      id: id,
      key: key,
      sortKey: sortKey,
      isRandom: false,
      track: this.library.trackTable[key],
    };
    previousKey = sortKey;
  }
  this.refreshPlaylistList(this.queue);
  this.sendCommand('queue', items);
  this.emit('queueUpdate');
};

PlayerClient.prototype.queueTracksNext = function(keys) {
  var prevKey = this.currentItem && this.currentItem.sortKey;
  var nextKey = null;
  var itemList = this.queue.itemList;
  for (var i = 0; i < itemList.length; ++i) {
    var track = itemList[i];
    if (prevKey == null || track.sortKey > prevKey) {
      if (nextKey == null || track.sortKey < nextKey) {
        nextKey = track.sortKey;
      }
    }
  }
  this.queueTracks(keys, prevKey, nextKey);
};

PlayerClient.prototype.clear = function(){
  this.sendCommand('clear');
  this.clearQueue();
  this.emit('queueUpdate');
};

PlayerClient.prototype.shuffle = function(){
  this.sendCommand('shuffle');
};

PlayerClient.prototype.play = function(){
  this.sendCommand('play');
  if (this.isPlaying === false) {
    this.trackStartDate = elapsedToDate(this.pausedTime);
    this.isPlaying = true;
    this.emit('statusupdate');
  }
};

PlayerClient.prototype.stop = function(){
  this.sendCommand('stop');
  if (this.isPlaying === true) {
    this.pausedTime = 0;
    this.isPlaying = false;
    this.emit('statusupdate');
  }
};

PlayerClient.prototype.pause = function(){
  this.sendCommand('pause');
  if (this.isPlaying === true) {
    this.pausedTime = dateToElapsed(this.trackStartDate);
    this.isPlaying = false;
    this.emit('statusupdate');
  }
};

PlayerClient.prototype.next = function(){
  var index = this.currentItem ? this.currentItem.index + 1 : 0;

  // handle the case of Repeat All
  if (index >= this.queue.itemList.length &&
      this.repeat === PlayerClient.REPEAT_ALL)
  {
    index = 0;
  }

  var item = this.queue.itemList[index];
  var id = item && item.id;

  this.seek(id, 0);
};

PlayerClient.prototype.prev = function(){
  var index = this.currentItem ? this.currentItem.index - 1 : this.queue.itemList.length - 1;

  // handle case of Repeat All
  if (index < 0 && this.repeat === PlayerClient.REPEAT_ALL) {
    index = this.queue.itemList.length - 1;
  }

  var item = this.queue.itemList[index];
  var id = item && item.id;

  this.seek(id, 0);
};

PlayerClient.prototype.moveIds = function(trackIds, previousKey, nextKey){
  var track, i;
  var tracks = [];
  for (i = 0; i < trackIds.length; i += 1) {
    var id = trackIds[i];
    track = this.queue.itemTable[id];
    if (track) tracks.push(track);
  }
  tracks.sort(compareSortKeyAndId);
  var items = {};
  for (i = 0; i < tracks.length; i += 1) {
    track = tracks[i];
    var sortKey = keese(previousKey, nextKey);
    items[track.id] = {
      sortKey: sortKey,
    };
    track.sortKey = sortKey;
    previousKey = sortKey;
  }
  this.refreshPlaylistList(this.queue);
  this.sendCommand('move', items);
  this.emit('queueUpdate');
};

PlayerClient.prototype.shiftIds = function(trackIdSet, offset) {
  // an example of shifting 5 items (a,c,f,g,i) "down":
  // offset: +1, reverse: false, this -> way
  // selection: *     *        *  *     *
  //    before: a, b, c, d, e, f, g, h, i
  //             \     \        \  \    |
  //              \     \        \  \   |
  //     after: b, a, d, c, e, h, f, g, i
  // selection:    *     *        *  *  *
  // (note that "i" does not move because it has no futher to go.)
  //
  // an alternate way to think about it: some items "leapfrog" backwards over the selected items.
  // this ends up being much simpler to compute, and even more compact to communicate.
  // selection: *     *        *  *     *
  //    before: a, b, c, d, e, f, g, h, i
  //              /     /        ___/
  //             /     /        /
  //     after: b, a, d, c, e, h, f, g, i
  // selection:    *     *        *  *  *
  // (note that the moved items are not the selected items)
  var itemList = this.queue.itemList;
  var movedItems = {};
  var reverse = offset === -1;
  function getKeeseBetween(itemA, itemB) {
    if (reverse) {
      var tmp = itemA;
      itemA = itemB;
      itemB = tmp;
    }
    var keyA = itemA == null ? null : itemA.sortKey;
    var keyB = itemB == null ? null : itemB.sortKey;
    return keese(keyA, keyB);
  }
  if (reverse) {
    // to make this easier, just reverse the item list in place so we can write one iteration routine.
    // note that we are editing our data model live! so don't forget to refresh it later.
    itemList.reverse();
  }
  for (var i = itemList.length - 1; i >= 1; i--) {
    var track = itemList[i];
    if (!(track.id in trackIdSet) && (itemList[i - 1].id in trackIdSet)) {
      // this one needs to move backwards (e.g. found "h" is not selected, and "g" is selected)
      i--; // e.g. g
      i--; // e.g. f
      while (true) {
        if (i < 0) {
          // fell off the end (or beginning) of the list
          track.sortKey = getKeeseBetween(null, itemList[0]);
          break;
        }
        if (!(itemList[i].id in trackIdSet)) {
          // this is where it goes (e.g. found "d" is not selected)
          track.sortKey = getKeeseBetween(itemList[i], itemList[i + 1]);
          break;
        }
        i--;
      }
      movedItems[track.id] = {sortKey: track.sortKey};
      i++;
    }
  }
  // we may have reversed the table and adjusted all the sort keys, so we need to refresh this.
  this.refreshPlaylistList(this.queue);

  this.sendCommand('move', movedItems);
  this.emit('queueUpdate');
};

PlayerClient.prototype.removeIds = function(trackIds){
  if (trackIds.length === 0) return;
  var ids = [];
  for (var i = 0; i < trackIds.length; i += 1) {
    var trackId = trackIds[i];
    var currentId = this.currentItem && this.currentItem.id;
    if (currentId === trackId) {
      this.currentItemId = null;
      this.currentItem = null;
    }
    ids.push(trackId);
    var item = this.queue.itemTable[trackId];
    delete this.queue.itemTable[item.id];
    this.refreshPlaylistList(this.queue);
  }
  this.sendCommand('remove', ids);
  this.emit('queueUpdate');
};

PlayerClient.prototype.deleteTracks = function(keysList) {
  this.sendCommand('deleteTracks', keysList);
  [this.library, this.searchResults].forEach(function(lib) {
    keysList.forEach(function(key) {
      lib.removeTrack(key);
    });
    lib.rebuild();
  });
  this.emit('libraryupdate');
};

PlayerClient.prototype.deletePlaylists = function(idsList) {
  this.sendCommand('playlistDelete', idsList);
  for (var i = 0; i < idsList.length; i += 1) {
    var id = idsList[i];
    var playlist = this.stored_playlist_table[id];
    for (var j = 0; j < playlist.itemList; j += 1) {
      var item = playlist.itemList[j];
      delete this.stored_playlist_item_table[item.id];
    }
    delete this.stored_playlist_table[id];
    this.stored_playlists.splice(playlist.index, 1);
    for (j = playlist.index; j < this.stored_playlists.length; j += 1) {
      this.stored_playlists[j].index -= 1;
    }
  }
  this.emit('playlistsUpdate');
};

PlayerClient.prototype.seek = function(id, pos) {
  pos = parseFloat(pos || 0);
  var item = id ? this.queue.itemTable[id] : this.currentItem;
  if (item == null) return;
  if (pos < 0) pos = 0;
  if (pos > item.track.duration) pos = item.track.duration;
  this.sendCommand('seek', {
    id: item.id,
    pos: pos,
  });
  this.currentItem = item;
  this.currentItemId = item.id;
  this.duration = item.track.duration;
  if (this.isPlaying) {
    this.trackStartDate = elapsedToDate(pos);
  } else {
    this.pausedTime = pos;
  }
  this.emit('statusupdate');
};

PlayerClient.prototype.setVolume = function(vol){
  if (vol > 2.0) vol = 2.0;
  if (vol < 0.0) vol = 0.0;
  this.volume = vol;
  this.sendCommand('setvol', this.volume);
  this.emit('statusupdate');
};

PlayerClient.prototype.setRepeatMode = function(mode) {
  this.repeat = mode;
  this.sendCommand('repeat', mode);
  this.emit('statusupdate');
};

PlayerClient.prototype.sendCommand = function(name, args) {
  this.socket.send(name, args);
};

PlayerClient.prototype.clearQueue = function(){
  this.queue = {
    itemList: [],
    itemTable: {},
    index: null,
    name: null
  };
};

PlayerClient.prototype.refreshPlaylistList = function(playlist) {
  playlist.itemList = [];
  var item;
  for (var id in playlist.itemTable) {
    item = playlist.itemTable[id];
    item.playlist = playlist;
    playlist.itemList.push(item);
  }
  playlist.itemList.sort(compareSortKeyAndId);
  for (var i = 0; i < playlist.itemList.length; i += 1) {
    item = playlist.itemList[i];
    item.index = i;
  }
};

// sort keys according to how they appear in the library
PlayerClient.prototype.sortKeys = function(keys) {
  var realLib = this.library;
  var lib = new MusicLibraryIndex();
  keys.forEach(function(key) {
    var track = realLib.trackTable[key];
    if (track) lib.addTrack(track);
  });
  lib.rebuild();
  var results = [];
  lib.artistList.forEach(function(artist) {
    artist.albumList.forEach(function(album) {
      album.trackList.forEach(function(track) {
        results.push(track.key);
      });
    });
  });
  return results;
};

PlayerClient.prototype.resetServerState = function(){
  this.haveFileListCache = false;
  this.library = new MusicLibraryIndex({
    searchFields: MusicLibraryIndex.defaultSearchFields.concat('file'),
  });
  this.searchResults = this.library;
  this.lastQuery = "";
  this.clearQueue();
  this.repeat = 0;
  this.currentItem = null;
  this.currentItemId = null;
  this.streamers = 0;
  this.usersList = [];
  this.usersTable = {};
  this.eventsList = [];
  this.seenEvents = {};
  this.unseenChatCount = 0;

  this.clearStoredPlaylists();
};

PlayerClient.prototype.createPlaylist = function(name) {
  var id = uuid();
  this.sendCommand('playlistCreate', {
    id: id,
    name: name,
  });
  // anticipate server response
  var playlist = {
    itemList: [],
    itemTable: {},
    id: id,
    name: name,
    index: 0,
  };
  this.stored_playlist_table[id] = playlist;
  this.stored_playlists.push(playlist);
  this.sortAndIndexPlaylists();
  this.emit('playlistsUpdate');

  return playlist;
};

function elapsedToDate(elapsed){
  return new Date(new Date() - elapsed * 1000);
}

function dateToElapsed(date){
  return (new Date() - date) / 1000;
}

function noop(err){
  if (err) throw err;
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

function compareUserNames(a, b) {
  var lowerA = a.name.toLowerCase();
  var lowerB = b.name.toLowerCase();
  if (a.id === PlayerClient.GUEST_USER_ID) {
    return -1;
  } else if (b.id === PlayerClient.GUEST_USER_ID) {
    return 1;
  } else if (lowerA < lowerB) {
    return -1;
  } else if (lowerA > lowerB) {
    return 1;
  } else {
    return 0;
  }
}
