var EventEmitter = require('./event_emitter');
var inherits = require('./inherits');
var uuid = require('./uuid');
var MusicLibraryIndex = require('music-library-index');
var keese = require('keese');
var curlydiff = require('curlydiff');
var shuffle = require('mess');

module.exports = PlayerClient;

var compareSortKeyAndId = makeCompareProps(['sortKey', 'id']);
var compareNameAndId = makeCompareProps(['name', 'id']);
var compareDates = makeCompareProps(['date', 'id']);

PlayerClient.REPEAT_OFF = 0;
PlayerClient.REPEAT_ALL = 1;
PlayerClient.REPEAT_ONE = 2;

PlayerClient.GUEST_USER_ID = "(guest)";

inherits(PlayerClient, EventEmitter);
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
  self.labelsFromServer = undefined;
  self.labelsFromServerVersion = null;
  self.eventsFromServer = undefined;
  self.eventsFromServerVersion = null;
  self.usersFromServer = undefined;
  self.usersFromServerVersion = null;
  self.importProgressFromServer = undefined;
  self.importProgressFromServerVersion = null;

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
    self.sortEventsFromServer(); // because they rely on serverTimeOffset
    self.emit('statusUpdate');
  });
  self.socket.on('volume', function(volume) {
    self.volume = volume;
    self.emit('volumeUpdate');
  });
  self.socket.on('repeat', function(repeat) {
    self.repeat = repeat;
    self.emit('statusUpdate');
  });
  self.socket.on('anonStreamers', function(anonStreamers) {
    self.anonStreamers = anonStreamers;
    self.emit('anonStreamers');
  });

  self.socket.on('currentTrack', function(o) {
    self.isPlaying = o.isPlaying;
    self.serverTrackStartDate = o.trackStartDate && new Date(o.trackStartDate);
    self.pausedTime = o.pausedTime;
    self.currentItemId = o.currentItemId;
    self.updateTrackStartDate();
    self.updateCurrentItem();
    self.emit('statusUpdate');
    self.emit('currentTrack');
  });

  self.socket.on('queue', function(o) {
    if (o.reset) self.queueFromServer = undefined;
    self.queueFromServer = curlydiff.apply(self.queueFromServer, o.delta);
    self.queueFromServerVersion = o.version;
    self.updateQueueIndex();
    self.emit('statusUpdate');
    self.emit('queueUpdate');
  });

  self.socket.on('library', function(o) {
    if (o.reset) self.libraryFromServer = undefined;
    self.libraryFromServer = curlydiff.apply(self.libraryFromServer, o.delta);
    self.libraryFromServerVersion = o.version;
    self.library.clearTracks();
    for (var key in self.libraryFromServer) {
      var track = self.libraryFromServer[key];
      self.library.addTrack(track);
    }
    self.library.rebuildTracks();
    self.updateQueueIndex();
    self.haveFileListCache = true;
    var lastQuery = self.lastQuery;
    self.lastQuery = null;
    self.search(lastQuery);
  });

  self.socket.on('scanning', function(o) {
    if (o.reset) self.scanningFromServer = undefined;
    self.scanningFromServer = curlydiff.apply(self.scanningFromServer, o.delta);
    self.scanningFromServerVersion = o.version;
    self.emit('scanningUpdate');
  });

  self.socket.on('playlists', function(o) {
    if (o.reset) self.playlistsFromServer = undefined;
    self.playlistsFromServer = curlydiff.apply(self.playlistsFromServer, o.delta);
    self.playlistsFromServerVersion = o.version;
    self.updatePlaylistsIndex();
    self.emit('playlistsUpdate');
  });

  self.socket.on('labels', function(o) {
    if (o.reset) self.labelsFromServer = undefined;
    self.labelsFromServer = curlydiff.apply(self.labelsFromServer, o.delta);
    self.labelsFromServerVersion = o.version;
    self.updateLabelsIndex();
    self.emit('labelsUpdate');
  });

  self.socket.on('events', function(o) {
    if (o.reset) self.eventsFromServer = undefined;
    self.eventsFromServer = curlydiff.apply(self.eventsFromServer, o.delta);
    self.eventsFromServerVersion = o.version;
    self.sortEventsFromServer();
    if (o.reset) self.markAllEventsSeen();
    self.emit('events');
  });

  self.socket.on('users', function(o) {
    if (o.reset) self.usersFromServer = undefined;
    self.usersFromServer = curlydiff.apply(self.usersFromServer, o.delta);
    self.usersFromServerVersion = o.version;
    self.sortUsersFromServer();
    self.emit('users');
  });

  self.socket.on('importProgress', function(o) {
    if (o.reset) self.importProgressFromServer = undefined;
    self.importProgressFromServer = curlydiff.apply(self.importProgressFromServer, o.delta);
    self.importProgressFromServerVersion = o.version;
    self.sortImportProgressFromServer();
    self.emit('importProgress');
  });
}

PlayerClient.prototype.resubscribe = function(){
  this.sendCommand('subscribe', {
    name: 'labels',
    delta: true,
    version: this.labelsFromServerVersion,
  });
  this.sendCommand('subscribe', {
    name: 'library',
    delta: true,
    version: this.libraryFromServerVersion,
  });
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
  this.sendCommand('subscribe', {name: 'volume'});
  this.sendCommand('subscribe', {name: 'repeat'});
  this.sendCommand('subscribe', {name: 'currentTrack'});
  this.sendCommand('subscribe', {
    name: 'playlists',
    delta: true,
    version: this.playlistsFromServerVersion,
  });
  this.sendCommand('subscribe', {name: 'anonStreamers'});
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
  this.sendCommand('subscribe', {
    name: 'importProgress',
    delta: true,
    version: this.importProgressFromServerVersion,
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
      date: new Date(new Date(serverEvent.date) - this.serverTimeOffset),
      type: serverEvent.type,
      sortKey: serverEvent.sortKey,
      text: serverEvent.text,
      pos: serverEvent.pos ? serverEvent.pos : 0,
      seen: seen,
      displayClass: serverEvent.displayClass,
      subCount: serverEvent.subCount ? serverEvent.subCount : 0,
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
    if (serverEvent.playlistId) {
      ev.playlist = this.playlistTable[serverEvent.playlistId];
    }
    if (serverEvent.labelId) {
      ev.label = this.library.labelTable[serverEvent.labelId];
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

PlayerClient.prototype.sortImportProgressFromServer = function() {
  this.importProgressList = [];
  this.importProgressTable = {};
  for (var id in this.importProgressFromServer) {
    var ev = this.importProgressFromServer[id];
    var importEvent = {
      id: id,
      date: new Date(ev.date),
      filenameHintWithoutPath: ev.filenameHintWithoutPath,
      bytesWritten: ev.bytesWritten,
      size: ev.size,
    };
    this.importProgressTable[id] = importEvent;
    this.importProgressList.push(importEvent);
  }
  this.importProgressList.sort(compareDates);
};

PlayerClient.prototype.updateTrackStartDate = function() {
  this.trackStartDate = (this.serverTrackStartDate != null) ?
    new Date(new Date(this.serverTrackStartDate) - this.serverTimeOffset) : null;
};

PlayerClient.prototype.updateCurrentItem = function() {
  this.currentItem = (this.currentItemId != null) ?
    this.queue.itemTable[this.currentItemId] : null;
};

PlayerClient.prototype.clearPlaylists = function() {
  this.playlistTable = {};
  this.playlistItemTable = {};
  this.playlistList = [];
};

PlayerClient.prototype.sortAndIndexPlaylists = function() {
  this.playlistList.sort(compareNameAndId);
  this.playlistList.forEach(function(playlist, index) {
    playlist.index = index;
  });
};

PlayerClient.prototype.updatePlaylistsIndex = function() {
  this.clearPlaylists();
  if (!this.playlistsFromServer) return;
  for (var id in this.playlistsFromServer) {
    var playlistFromServer = this.playlistsFromServer[id];
    var playlist = {
      itemList: [],
      itemTable: {},
      id: id,
      name: playlistFromServer.name,
      mtime: playlistFromServer.mtime,
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
      this.playlistItemTable[itemId] = item;
    }
    this.refreshPlaylistList(playlist);
    this.playlistList.push(playlist);
    this.playlistTable[playlist.id] = playlist;
  }
  this.sortAndIndexPlaylists();
};

PlayerClient.prototype.updateLabelsIndex = function() {
  this.library.clearLabels();
  if (!this.labelsFromServer) return;
  for (var id in this.labelsFromServer) {
    var labelFromServer = this.labelsFromServer[id];
    var label = {
      id: id,
      name: labelFromServer.name,
      color: labelFromServer.color,
      index: 0, // this gets set during rebuildLabels()
    };
    this.library.addLabel(label);
  }
  this.library.rebuildLabels();
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
  return !!scanInfo;
};

PlayerClient.prototype.search = function(query) {
  query = query.trim();

  if (query === this.lastQuery) return;

  this.lastQuery = query;
  this.searchResults = this.library.search(query);
  this.emit('libraryUpdate');
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

PlayerClient.prototype.queueOnQueue = function(keys, previousKey, nextKey) {
  if (keys.length === 0) return;

  if (previousKey == null && nextKey == null) {
    var defaultPos = this.getDefaultQueuePosition();
    previousKey = defaultPos.previousKey;
    nextKey = defaultPos.nextKey;
  }

  var items = this.queueTracks(this.queue, keys, previousKey, nextKey);
  this.sendCommand('queue', items);
  this.emit('queueUpdate');
};

PlayerClient.prototype.queueOnPlaylist = function(playlistId, keys, previousKey, nextKey) {
  if (keys.length === 0) return;

  var playlist = this.playlistTable[playlistId];
  if (previousKey == null && nextKey == null && playlist.itemList.length > 0) {
    previousKey = playlist.itemList[playlist.itemList.length - 1].sortKey;
  }
  var items = this.queueTracks(playlist, keys, previousKey, nextKey);

  this.sendCommand('playlistAddItems', {
    id: playlistId,
    items: items,
  });

  this.emit('playlistsUpdate');
};

PlayerClient.prototype.renamePlaylist = function(playlist, newName) {
  playlist.name = newName;

  this.sendCommand('playlistRename', {
    id: playlist.id,
    name: playlist.name,
  });

  this.emit('playlistUpdate');
};

PlayerClient.prototype.queueTracks = function(playlist, keys, previousKey, nextKey) {
  var items = {}; // we'll send this to the server
  var sortKeys = keese(previousKey, nextKey, keys.length);
  for (var i = 0; i < keys.length; i += 1) {
    var key = keys[i];
    var sortKey = sortKeys[i];
    var id = uuid();
    items[id] = {
      key: key,
      sortKey: sortKey,
    };
    playlist[playlist.id] = {
      id: id,
      key: key,
      sortKey: sortKey,
      isRandom: false,
      track: this.library.trackTable[key],
    };
  }

  this.refreshPlaylistList(playlist);

  return items;
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
  this.queueOnQueue(keys, prevKey, nextKey);
};

PlayerClient.prototype.clear = function(){
  this.sendCommand('clear');
  this.clearQueue();
  this.emit('queueUpdate');
};

PlayerClient.prototype.play = function(){
  this.sendCommand('play');
  if (this.isPlaying === false) {
    this.trackStartDate = elapsedToDate(this.pausedTime);
    this.isPlaying = true;
    this.emit('statusUpdate');
  }
};

PlayerClient.prototype.stop = function(){
  this.sendCommand('stop');
  if (this.isPlaying === true) {
    this.pausedTime = 0;
    this.isPlaying = false;
    this.emit('statusUpdate');
  }
};

PlayerClient.prototype.pause = function(){
  this.sendCommand('pause');
  if (this.isPlaying === true) {
    this.pausedTime = dateToElapsed(this.trackStartDate);
    this.isPlaying = false;
    this.emit('statusUpdate');
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
  var sortKeys = keese(previousKey, nextKey, tracks.length);
  for (i = 0; i < tracks.length; i += 1) {
    track = tracks[i];
    var sortKey = sortKeys[i];
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

PlayerClient.prototype.shuffleQueueItems = function(ids) {
  var items = shuffleIds(ids, this.queue.itemTable);
  this.refreshPlaylistList(this.queue);
  this.sendCommand('move', items);
  this.emit('queueUpdate');
};

PlayerClient.prototype.shufflePlaylists = function(playlistIdSet) {
  var updates = {};
  for (var playlistId in playlistIdSet) {
    var playlist = this.playlistTable[playlistId];
    var items = shuffleIds(Object.keys(playlist.itemTable), playlist.itemTable);
    updates[playlistId] = items;
    this.refreshPlaylistList(playlist);
  }

  this.sendCommand('playlistMoveItems', updates);
  this.emit('playlistsUpdate');
};

PlayerClient.prototype.shufflePlaylistItems = function(idSet) {
  var idLists = {};
  var idList;
  for (var id in idSet) {
    var item = this.playlistItemTable[id];
    idList = idLists[item.playlist.id] || (idLists[item.playlist.id] = []);
    idList.push(id);
  }
  var updates = {};
  for (var playlistId in idLists) {
    idList = idLists[playlistId];
    var playlist = this.playlistTable[playlistId];
    updates[playlistId] = shuffleIds(idList, playlist.itemTable);
    this.refreshPlaylistList(playlist);
  }
  this.sendCommand('playlistMoveItems', updates);
  this.emit('playlistsUpdate');
};

PlayerClient.prototype.playlistShiftIds = function(trackIdSet, offset) {
  var perPlaylistSet = {};
  var set;
  for (var trackId in trackIdSet) {
    var item = this.playlistItemTable[trackId];
    set = perPlaylistSet[item.playlist.id] || (perPlaylistSet[item.playlist.id] = {});
    set[trackId] = true;
  }

  var updates = {};
  for (var playlistId in perPlaylistSet) {
    set = perPlaylistSet[playlistId];
    var playlist = this.playlistTable[playlistId];
    updates[playlistId] = shiftIdsInPlaylist(this, playlist, set, offset);
  }

  this.sendCommand('playlistMoveItems', updates);
  this.emit('playlistsUpdate');
};

PlayerClient.prototype.shiftIds = function(trackIdSet, offset) {
  var movedItems = shiftIdsInPlaylist(this, this.queue, trackIdSet, offset);

  this.sendCommand('move', movedItems);
  this.emit('queueUpdate');
};

PlayerClient.prototype.removeIds = function(trackIds){
  if (trackIds.length === 0) return;

  var currentId = this.currentItem && this.currentItem.id;
  var currentIndex = this.currentItem && this.currentItem.index;
  var offset = 0;
  for (var i = 0; i < trackIds.length; i += 1) {
    var trackId = trackIds[i];
    if (trackId === currentId) {
      this.trackStartDate = new Date();
      this.pausedTime = 0;
    }
    var item = this.queue.itemTable[trackId];
    if (item.index < currentIndex) {
      offset -= 1;
    }
    delete this.queue.itemTable[trackId];
  }
  currentIndex += offset;
  this.refreshPlaylistList(this.queue);
  this.currentItem = (currentIndex == null) ? null : this.queue.itemList[currentIndex];
  this.currentItemId = this.currentItem && this.currentItem.id;

  this.sendCommand('remove', trackIds);
  this.emit('queueUpdate');
};

PlayerClient.prototype.removeItemsFromPlaylists = function(idSet) {
  var removals = {};
  var playlist;
  for (var playlistItemId in idSet) {
    var playlistItem = this.playlistItemTable[playlistItemId];
    playlist = playlistItem.playlist;
    var removal = removals[playlist.id];
    if (!removal) {
      removal = removals[playlist.id] = [];
    }
    removal.push(playlistItemId);

    delete playlist.itemTable[playlistItemId];
  }
  for (var playlistId in removals) {
    playlist = this.playlistTable[playlistId];
    this.refreshPlaylistList(playlist);
  }
  this.sendCommand('playlistRemoveItems', removals);
  this.emit('playlistsUpdate');
};

PlayerClient.prototype.deleteTracks = function(keysList) {
  this.sendCommand('deleteTracks', keysList);
  removeTracksInLib(this.library, keysList);
  removeTracksInLib(this.searchResults, keysList);

  var queueDirty = false;
  var dirtyPlaylists = {};
  for (var keysListIndex = 0; keysListIndex < keysList.length; keysListIndex += 1) {
    var key = keysList[keysListIndex];

    // delete items from the queue that are being deleted from the library
    var i;
    for (i = 0; i < this.queue.itemList.length; i += 1) {
      var queueItem = this.queue.itemList[i];
      if (queueItem.track.key === key) {
        delete this.queue.itemTable[queueItem.id];
        queueDirty = true;
      }
    }

    // delete items from playlists that are being deleted from the library
    for (var playlistIndex = 0; playlistIndex < this.playlistList.length; playlistIndex += 1) {
      var playlist = this.playlistList[playlistIndex];
      for (i = 0; i < playlist.itemList.length; i += 1) {
        var plItem = playlist.itemList[i];
        if (plItem.track.key === key) {
          delete playlist.itemTable[plItem.id];
          dirtyPlaylists[playlist.id] = playlist;
        }
      }
    }
  }
  if (queueDirty) {
    this.refreshPlaylistList(this.queue);
    this.emit('queueUpdate');
  }
  var anyDirtyPlaylists = false;
  for (var dirtyPlId in dirtyPlaylists) {
    var dirtyPlaylist = dirtyPlaylists[dirtyPlId];
    this.refreshPlaylistList(dirtyPlaylist);
    anyDirtyPlaylists = true;
  }
  if (anyDirtyPlaylists) {
    this.emit('playlistsUpdate');
  }

  this.emit('libraryUpdate');
};

PlayerClient.prototype.deletePlaylists = function(idSet) {
  var idList = Object.keys(idSet);
  if (idList.length === 0) return;
  this.sendCommand('playlistDelete', idList);
  for (var id in idSet) {
    var playlist = this.playlistTable[id];
    for (var j = 0; j < playlist.itemList; j += 1) {
      var item = playlist.itemList[j];
      delete this.playlistItemTable[item.id];
    }
    delete this.playlistTable[id];
    this.playlistList.splice(playlist.index, 1);
    for (j = playlist.index; j < this.playlistList.length; j += 1) {
      this.playlistList[j].index -= 1;
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
  this.emit('statusUpdate');
};

PlayerClient.prototype.setVolume = function(vol){
  if (vol > 2.0) vol = 2.0;
  if (vol < 0.0) vol = 0.0;
  this.volume = vol;
  this.sendCommand('setVolume', this.volume);
  this.emit('statusUpdate');
};

PlayerClient.prototype.setRepeatMode = function(mode) {
  this.repeat = mode;
  this.sendCommand('repeat', mode);
  this.emit('statusUpdate');
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
  this.anonStreamers = 0;
  this.usersList = [];
  this.usersTable = {};
  this.eventsList = [];
  this.seenEvents = {};
  this.unseenChatCount = 0;
  this.importProgressList = [];
  this.importProgressTable = {};

  this.clearPlaylists();
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
  this.playlistTable[id] = playlist;
  this.playlistList.push(playlist);
  this.sortAndIndexPlaylists();
  this.emit('playlistsUpdate');

  return playlist;
};

PlayerClient.prototype.removeLabel = function(labelId, keys) {
  if (keys.length === 0) return;

  var label = this.library.labelTable[labelId];

  var removals = {};
  for (var i = 0; i < keys.length; i += 1) {
    var key = keys[i];
    removals[key] = [labelId];
  }

  this.sendCommand('labelRemove', removals);

  // TODO anticipate server response
};

PlayerClient.prototype.updateLabelColor = function(labelId, color) {
  this.sendCommand('labelColorUpdate', {
    id: labelId,
    color: color,
  });
  // TODO anticipate server response
};

PlayerClient.prototype.renameLabel = function(labelId, name) {
  this.sendCommand('labelRename', {
    id: labelId,
    name: name,
  });
  // TODO anticipate server response
};

PlayerClient.prototype.addLabel = function(labelId, keys) {
  if (keys.length === 0) return;

  var label = this.library.labelTable[labelId];

  var additions = {};
  for (var i = 0; i < keys.length; i += 1) {
    var key = keys[i];
    additions[key] = [labelId];
  }

  this.sendCommand('labelAdd', additions);

  // TODO anticipate server response
};

PlayerClient.prototype.createLabel = function(name) {
  var id = uuid();
  this.sendCommand('labelCreate', {
    id: id,
    name: name,
  });
  // anticipate server response
  var label = {
    id: id,
    name: name,
    index: 0,
  };
  this.library.addLabel(label);
  this.library.rebuildLabels();
  this.emit('labelsUpdate');

  return label;
};

PlayerClient.prototype.deleteLabels = function(labelIds) {
  if (labelIds.length === 0) return;
  this.sendCommand('labelDelete', labelIds);
  // TODO anticipate server response
};

function shiftIdsInPlaylist(self, playlist, trackIdSet, offset) {
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
  var itemList = playlist.itemList;
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
  self.refreshPlaylistList(playlist);
  return movedItems;
}

function shuffleIds(ids, table) {
  var sortKeys = [];
  var i, id, sortKey;
  for (i = 0; i < ids.length; i += 1) {
    id = ids[i];
    sortKey = table[id].sortKey;
    sortKeys.push(sortKey);
  }
  shuffle(sortKeys);
  var items = {};
  for (i = 0; i < ids.length; i += 1) {
    id = ids[i];
    sortKey = sortKeys[i];
    items[id] = {sortKey: sortKey};
    table[id].sortKey = sortKey;
  }
  return items;
}

function removeTracksInLib(lib, keysList) {
  keysList.forEach(function(key) {
    lib.removeTrack(key);
  });
  lib.rebuildTracks();
}

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
