module.exports = DynamicMode;

function DynamicMode(gb) {
  this.gb = gb;
  this.state = {
    on: false,
    historySize: 10,
    futureSize: 10,
  };
  this.previousIds = {};

  this.gb.on('socketConnect', onSocketConnection.bind(this));
  this.gb.player.on('playlistUpdate', this.checkDynamicMode.bind(this));
}

DynamicMode.prototype.initialize = function(cb) {
  var self = this;

  self.gb.db.get('Plugin.dynamicmode', function(err, value) {
    if (err) {
      if (err.type === 'NotFoundError') {
        // rely on defaults
        return cb();
      } else {
        return cb(err);
      }
    }
    self.setState(JSON.parse(value));
    cb();
  });
};

DynamicMode.prototype.setState = function(state) {
  var anythingChanged = false;
  for (var key in state) {
    if (state[key] !== undefined && this.state[key] !== undefined &&
        this.state[key] !== state[key])
    {
      anythingChanged = true;
      this.state[key] = state[key];
    }
  }
  return anythingChanged;
};

DynamicMode.prototype.persist = function() {
  var self = this;
  self.gb.db.put('Plugin.dynamicmode', JSON.stringify(self.state), function(err) {
    if (err) {
      console.error("Error persisting dynamicmode to db:", err.stack);
    }
  });
};

function onSocketConnection(socket) {
  var self = this;
  socket.on('DynamicMode', function(state) {
    if (self.setState(state)) {
      self.checkDynamicMode();
      broadcastUpdate(self, socket);
      self.persist();
    }
  });
  socket.emit('DynamicMode', self.state);
}

DynamicMode.prototype.checkDynamicMode = function() {
  var self = this;
  if (!self.state.on) return;
  var player = self.gb.player;
  var tracksInOrder = player.tracksInOrder;
  var currentTrack = player.currentTrack;
  var allIds = {};
  var now = new Date();
  tracksInOrder.forEach(function(track) {
    allIds[track.id] = true;
    if (self.previousIds[track.id] == null) {
      // tag any newly queued tracks
      var dbFile = player.allDbFiles[track.file];
      dbFile.lastQueueDate = now;
      player.persist(dbFile);
    }
  });

  // if no track is playing, assume the first track is about to be
  var currentIndex = currentTrack ? currentTrack.index : 0;
  var didAnything = false;

  var deleteCount = Math.max(currentIndex - self.state.historySize, 0);
  if (self.state.historySize < 0) deleteCount = 0;
  var addCount = Math.max(self.state.futureSize + 1 - (tracksInOrder.length - currentIndex), 0);

  for (var i = 0; i < deleteCount; i += 1) {
    delete player.playlist[tracksInOrder[i].id];
    didAnything = true;
  }

  var files = self.getRandomSongFiles(addCount);
  if (files.length > 0) didAnything = true;
  if (didAnything) player.appendFiles(files, true);
};

DynamicMode.prototype.getRandomSongFiles = function(count) {
  if (count === 0) return [];
  var player = this.gb.player;

  var neverQueued = [];
  var sometimesQueued = [];
  for (var key in player.allDbFiles) {
    var dbFile = player.allDbFiles[key];
    if (dbFile.lastQueueDate == null) {
      neverQueued.push(dbFile);
    } else {
      sometimesQueued.push(dbFile);
    }
  }
  // backwards by time
  sometimesQueued.sort(function(a, b) {
    return b.lastQueueDate - a.lastQueueDate;
  });
  // distribution is a triangle for ever queued, and a rectangle for never queued
  //    ___
  //   /| |
  //  / | |
  // /__|_|
  var maxWeight = sometimesQueued.length;
  var triangleArea = Math.floor(maxWeight * maxWeight / 2);
  if (maxWeight === 0) maxWeight = 1;
  var rectangleArea = maxWeight * neverQueued.length;
  var totalSize = triangleArea + rectangleArea;
  if (totalSize === 0) return [];
  // decode indexes through the distribution shape
  var files = [];
  for (var i = 0; i < count; i += 1) {
    var index = Math.random() * totalSize;
    if (index < triangleArea) {
      // triangle
      files.push(sometimesQueued[Math.floor(Math.sqrt(index))].file);
    } else {
      files.push(neverQueued[Math.floor((index - triangleArea) / maxWeight)].file);
    }
  }
  return files;
};

function broadcastUpdate(self, socket) {
  socket.broadcast.emit('DynamicMode', self.state);
  socket.emit('DynamicMode', self.state);
}
