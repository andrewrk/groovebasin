var EventEmitter = require('events').EventEmitter;
var util = require('util');
var uuid = require('uuid');
var MusicLibraryIndex = require('music-library-index');
var keese = require('keese');

module.exports = PlayerClient;

/*
 * If you look at the code in this file and think to yourself "What the fuck?"
 * This should clear it up:
 * 
 *   * This code was written in JavaScript, then converted to Coffee-Script,
 *     then converted to satyr/coco, and then back to JavaScript (by using
 *     the output of the coco compiler).
 *   * This code used to use the MPD protocol, but that is no longer true.
 *
 * */

var compareSortKeyAndId = makeCompareProps(['sortKey', 'id']);

PlayerClient.REPEAT_OFF = 0;
PlayerClient.REPEAT_ALL = 1;
PlayerClient.REPEAT_ONE = 2;

util.inherits(PlayerClient, EventEmitter);
function PlayerClient(socket) {
  EventEmitter.call(this);

  var self = this;
  self.socket = socket;
  self.resetServerState();
  self.updateFuncs = {
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
  var self = this;
  this.updateLibrary(function(){
    self.updateServerTime();
    self.updateStatus();
    self.updatePlaylist();
  });
};

PlayerClient.prototype.updateLibrary = function(callback){
  var self = this;
  callback = callback || noop;
  this.sendCommandName('listallinfo', function(err, trackTable){
    if (err) return callback(err);
    self.library.clear();
    for (var key in trackTable) {
      var track = trackTable[key];
      self.library.addTrack(track);
    }
    self.library.rebuild();
    self.haveFileListCache = true;
    var lastQuery = self.lastQuery;
    self.lastQuery = null;
    self.search(lastQuery);
    callback();
  });
};

PlayerClient.prototype.updatePlaylist = function(callback){
  var self = this;
  callback = callback || noop;
  this.sendCommandName('playlistinfo', function(err, tracks){
    if (err) return callback(err);
    self.clearPlaylist();
    for (var id in tracks) {
      var item = tracks[id];
      var track = self.library.trackTable[item.key];
      self.playlist.itemTable[id] = {
        id: id,
        sortKey: item.sortKey,
        isRandom: item.isRandom,
        track: track,
        playlist: self.playlist,
      };
    }
    self.refreshPlaylistList();
    if (self.currentItem != null) {
      self.currentItem = self.playlist.itemTable[self.currentItem.id];
    }
    if (self.currentItem != null) {
      self.emit('playlistupdate');
      callback();
    } else {
      self.updateStatus(function(err){
        if (err) {
          return callback(err);
        }
        self.emit('playlistupdate');
        callback();
      });
    }
  });
};

PlayerClient.prototype.updateServerTime = function(callback) {
  var self = this;
  callback = callback || noop;
  this.sendCommandName('whattimeisit', function(err, o){
    if (err) return callback(err);
    self.serverTimeOffset = new Date(o) - new Date();
    callback();
  });
};

PlayerClient.prototype.updateStatus = function(callback){
  var self = this;
  callback = callback || noop;
  this.sendCommandName('status', function(err, o){
    if (err) return callback(err);
    self.volume = o.volume;
    self.repeat = o.repeat;
    self.isPlaying = o.isPlaying;
    self.trackStartDate = o.trackStartDate != null ? new Date(new Date(o.trackStartDate) - self.serverTimeOffset) : null;
    self.pausedTime = o.pausedTime;
  });
  this.sendCommandName('currentsong', function(err, id){
    if (err) return callback(err);
    if (id != null) {
      self.currentItem = self.playlist.itemTable[id];
      if (self.currentItem != null) {
        self.duration = self.currentItem.track.duration;
        self.emit('statusupdate');
        callback();
      } else {
        self.currentItem = null;
        self.updatePlaylist(function(err){
          if (err) {
            return callback(err);
          }
          self.emit('statusupdate');
          callback();
        });
      }
    } else {
      self.currentItem = null;
      callback();
      self.emit('statusupdate');
    }
  });
};

PlayerClient.prototype.search = function(query) {
  query = query.trim();

  var words = query.split(/\s+/);
  query = words.join(" ");
  if (query === this.lastQuery) return;

  this.lastQuery = query;
  this.searchResults = this.library.search(query);
  this.emit('libraryupdate');
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
  this.sendCommand({
    name: 'addid',
    items: items,
  });
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
  this.sendCommandName('clear');
  this.clearPlaylist();
  this.emit('playlistupdate');
};

PlayerClient.prototype.shuffle = function(){
  this.sendCommandName('shuffle');
};

PlayerClient.prototype.play = function(){
  this.sendCommandName('play');
  if (this.isPlaying === false) {
    this.trackStartDate = elapsedToDate(this.pausedTime);
    this.isPlaying = true;
    this.emit('statusupdate');
  }
};

PlayerClient.prototype.stop = function(){
  this.sendCommandName('stop');
  if (this.isPlaying === true) {
    this.pausedTime = 0;
    this.isPlaying = false;
    this.emit('statusupdate');
  }
};

PlayerClient.prototype.pause = function(){
  this.sendCommandName('pause');
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

  this.playId(id);
};

PlayerClient.prototype.prev = function(){
  var index = this.currentItem ? this.currentItem.index - 1 : this.playlist.itemList.length - 1;

  // handle case of Repeat All
  if (index < 0 && this.repeat === PlayerClient.REPEAT_ALL) {
    index = this.playlist.itemList.length - 1;
  }

  var item = this.playlist.itemList[index];
  var id = item && item.id;

  this.playId(id);
};

PlayerClient.prototype.playId = function(trackId){
  this.sendCommand({
    name: 'playid',
    trackId: trackId
  });
  this.anticipatePlayId(trackId);
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
  this.sendCommand({
    name: 'move',
    items: items,
  });
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

  this.sendCommand({
    name: 'move',
    items: movedItems,
  });
  this.emit('playlistupdate');
};

PlayerClient.prototype.removeIds = function(trackIds){
  if (trackIds.length === 0) return;
  var ids = [];
  for (var i = 0; i < trackIds.length; i += 1) {
    var trackId = trackIds[i];
    var currentId = this.currentItem && this.currentItem.id;
    if (currentId === trackId) {
      this.currentItem = null;
    }
    ids.push(trackId);
    var item = this.playlist.itemTable[trackId];
    delete this.playlist.itemTable[item.id];
    this.refreshPlaylistList();
  }
  this.sendCommand({
    name: 'deleteid',
    ids: ids
  });
  this.emit('playlistupdate');
};

PlayerClient.prototype.seek = function(pos) {
  pos = parseFloat(pos, 10);
  if (pos < 0) pos = 0;
  if (pos > this.duration) pos = this.duration;
  this.sendCommand({
    name: 'seek',
    pos: pos,
  });
  this.trackStartDate = elapsedToDate(pos);
  this.emit('statusupdate');
};

PlayerClient.prototype.setVolume = function(vol){
  if (vol > 1.0) vol = 1.0;
  if (vol < 0.0) vol = 0.0;
  this.volume = vol;
  this.sendCommand({
    name: "setvol",
    vol: this.volume,
  });
  this.emit('statusupdate');
};

PlayerClient.prototype.setRepeatMode = function(mode) {
  this.repeat = mode;
  this.sendCommand({
    name: 'repeat',
    mode: mode,
  });
  this.emit('statusupdate');
};

PlayerClient.prototype.authenticate = function(password, callback){
  callback = callback || noop;
  this.sendCommand({
    name: 'password',
    password: password
  }, function(err){
    callback(err);
  });
};

PlayerClient.prototype.sendCommandName = function(name, cb){
  cb = cb || noop;
  this.sendCommand({
    name: name
  }, cb);
};

PlayerClient.prototype.sendCommand = function(cmd, cb){
  cb = cb || noop;
  var callbackId = this.nextResponseHandlerId++;
  this.responseHandlers[callbackId] = cb;
  this.socket.emit('request', JSON.stringify({
    cmd: cmd,
    callbackId: callbackId
  }));
};

PlayerClient.prototype.handleResponse = function(arg){
  var err = arg.err;
  var msg = arg.msg;
  var callbackId = arg.callbackId;
  var handler = this.responseHandlers[callbackId];
  delete this.responseHandlers[callbackId];
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
    itemList: [],
    itemTable: {},
    index: null,
    name: null
  };
};

PlayerClient.prototype.anticipatePlayId = function(trackId){
  var item = this.playlist.itemTable[trackId];
  this.currentItem = item;
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

PlayerClient.prototype.resetServerState = function(){
  this.responseHandlers = {};
  this.nextResponseHandlerId = 0;
  this.haveFileListCache = false;
  this.library = new MusicLibraryIndex({
    searchFields: MusicLibraryIndex.defaultSearchFields.concat('file'),
  });
  this.searchResults = this.library;
  this.lastQuery = "";
  this.clearPlaylist();
  this.repeat = 0;
  this.currentItem = null;

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
