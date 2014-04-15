var EventEmitter = require('events').EventEmitter;
var util = require('util');
var uuid = require('uuid');
var MusicLibraryIndex = require('music-library-index');
var keese = require('keese');
var jsondiffpatch = require('jsondiffpatch');

module.exports = PlayerClient;

var compareSortKeyAndId = makeCompareProps(['sortKey', 'id']);

PlayerClient.REPEAT_OFF = 0;
PlayerClient.REPEAT_ONE = 1;
PlayerClient.REPEAT_ALL = 2;

util.inherits(PlayerClient, EventEmitter);
function PlayerClient(socket) {
  EventEmitter.call(this);

  window.__debug_PlayerClient = this;

  var self = this;
  self.socket = socket;
  self.serverTimeOffset = 0;
  self.serverTrackStartDate = null;
  self.playlistFromServer = undefined;
  self.playlistFromServerVersion = null;
  self.libraryFromServer = undefined;
  self.libraryFromServerVersion = null;
  self.resetServerState();
  self.socket.on('disconnect', function() {
    self.resetServerState();
  });
  if (self.socket.isConnected) {
    self.handleConnectionStart();
  } else {
    self.socket.on('connect', self.handleConnectionStart.bind(self));
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

  self.socket.on('playlist', function(o) {
    if (o.reset) self.playlistFromServer = undefined;
    self.playlistFromServer = jsondiffpatch.patch(self.playlistFromServer, o.delta);
    deleteUndefineds(self.playlistFromServer);
    self.playlistFromServerVersion = o.version;
    self.updatePlaylistIndex();
    self.emit('statusupdate');
    self.emit('playlistupdate');
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
    self.updatePlaylistIndex();
    self.haveFileListCache = true;
    var lastQuery = self.lastQuery;
    self.lastQuery = null;
    self.search(lastQuery);
  });

  function deleteUndefineds(o) {
    for (var key in o) {
      if (o[key] === undefined) delete o[key];
    }
  }
}

PlayerClient.prototype.handleConnectionStart = function(){
  this.sendCommand('subscribe', { name: 'library', delta: true, });
  this.sendCommand('subscribe', {name: 'volume'});
  this.sendCommand('subscribe', {name: 'repeat'});
  this.sendCommand('subscribe', {name: 'currentTrack'});
  this.sendCommand('subscribe', {
    name: 'playlist',
    delta: true,
    version: this.playlistFromServerVersion,
  });
};

PlayerClient.prototype.updateTrackStartDate = function() {
  this.trackStartDate = (this.serverTrackStartDate != null) ?
    new Date(new Date(this.serverTrackStartDate) - this.serverTimeOffset) : null;
};

PlayerClient.prototype.updateCurrentItem = function() {
  this.currentItem = (this.currentItemId != null) ?
    this.playlist.itemTable[this.currentItemId] : null;
};

PlayerClient.prototype.updatePlaylistIndex = function() {
  this.clearPlaylist();
  if (!this.playlistFromServer) return;
  for (var id in this.playlistFromServer) {
    var item = this.playlistFromServer[id];
    var track = this.library.trackTable[item.key];
    this.playlist.itemTable[id] = {
      id: id,
      sortKey: item.sortKey,
      isRandom: item.isRandom,
      track: track,
      playlist: this.playlist,
    };
  }
  this.refreshPlaylistList();
  this.updateCurrentItem();
};

PlayerClient.prototype.search = function(query) {
  query = query.trim();

  var words = query.split(/\s+/);
  query = words.join(" ");
  if (query === this.lastQuery) return;

  this.lastQuery = query;
  this.searchResults = this.library.search(query);
  this.emit('libraryupdate');
  this.emit('playlistupdate');
  this.emit('statusupdate');
};

PlayerClient.prototype.getDefaultQueuePosition = function() {
  var previousKey = this.currentItem && this.currentItem.sortKey;
  var nextKey = null;
  var startPos = this.currentItem ? this.currentItem.index + 1 : 0;
  for (var i = startPos; i < this.playlist.itemList.length; i += 1) {
    var track = this.playlist.itemList[i];
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
    this.playlist.itemTable[id] = {
      id: id,
      key: key,
      sortKey: sortKey,
      isRandom: false,
      track: this.library.trackTable[key],
    };
    previousKey = sortKey;
  }
  this.refreshPlaylistList();
  this.sendCommand('addid', items);
  this.emit('playlistupdate');
};

PlayerClient.prototype.queueTracksNext = function(keys) {
  var prevKey = this.currentItem && this.currentItem.sortKey;
  var nextKey = null;
  var itemList = this.playlist.itemList;
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
  this.clearPlaylist();
  this.emit('playlistupdate');
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
  if (index >= this.playlist.itemList.length &&
      this.repeat === PlayerClient.REPEAT_ALL)
  {
    index = 0;
  }

  var item = this.playlist.itemList[index];
  var id = item && item.id;

  this.seek(id, 0);
};

PlayerClient.prototype.prev = function(){
  var index = this.currentItem ? this.currentItem.index - 1 : this.playlist.itemList.length - 1;

  // handle case of Repeat All
  if (index < 0 && this.repeat === PlayerClient.REPEAT_ALL) {
    index = this.playlist.itemList.length - 1;
  }

  var item = this.playlist.itemList[index];
  var id = item && item.id;

  this.seek(id, 0);
};

PlayerClient.prototype.moveIds = function(trackIds, previousKey, nextKey){
  var track, i;
  var tracks = [];
  for (i = 0; i < trackIds.length; i += 1) {
    var id = trackIds[i];
    track = this.playlist.itemTable[id];
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
  this.refreshPlaylistList();
  this.sendCommand('move', items);
  this.emit('playlistupdate');
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
  var itemList = this.playlist.itemList;
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
  this.refreshPlaylistList();

  this.sendCommand('move', movedItems);
  this.emit('playlistupdate');
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
    var item = this.playlist.itemTable[trackId];
    delete this.playlist.itemTable[item.id];
    this.refreshPlaylistList();
  }
  this.sendCommand('deleteid', ids);
  this.emit('playlistupdate');
};

PlayerClient.prototype.seek = function(id, pos) {
  pos = parseFloat(pos || 0, 10);
  var item = id ? this.playlist.itemTable[id] : this.currentItem;
  if (pos < 0) pos = 0;
  if (pos > item.duration) pos = item.duration;
  this.sendCommand('seek', {
    id: item.id,
    pos: pos,
  });
  this.currentItem = item;
  this.currentItemId = item.id;
  this.isPlaying = true;
  this.duration = item.track.duration;
  this.trackStartDate = elapsedToDate(pos);
  this.emit('statusupdate');
};

PlayerClient.prototype.setVolume = function(vol){
  if (vol > 1.0) vol = 1.0;
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

PlayerClient.prototype.clearPlaylist = function(){
  this.playlist = {
    itemList: [],
    itemTable: {},
    index: null,
    name: null
  };
};

PlayerClient.prototype.anticipatePlayId = function(trackId){
  var item = this.playlist.itemTable[trackId];
  this.currentItem = item;
  this.currentItemId = item.id;
  this.isPlaying = true;
  this.duration = item.track.duration;
  this.trackStartDate = new Date();
  this.emit('statusupdate');
};

PlayerClient.prototype.anticipateSkip = function(direction) {
  if (this.currentItem) {
    var nextItem = this.playlist.itemList[this.currentItem.index + direction];
    if (nextItem) this.anticipatePlayId(nextItem.id);
  }
};

PlayerClient.prototype.refreshPlaylistList = function(){
  this.playlist.itemList = [];
  var item;
  for (var id in this.playlist.itemTable) {
    item = this.playlist.itemTable[id];
    item.playlist = this.playlist;
    this.playlist.itemList.push(item);
  }
  this.playlist.itemList.sort(compareSortKeyAndId);
  for (var i = 0; i < this.playlist.itemList.length; i += 1) {
    item = this.playlist.itemList[i];
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
  this.clearPlaylist();
  this.repeat = 0;
  this.currentItem = null;
  this.currentItemId = null;

  this.stored_playlist_table = {};
  this.stored_playlist_item_table = {};
  this.stored_playlists = [];
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
  }
  if (a < b) {
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
