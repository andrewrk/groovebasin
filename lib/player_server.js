var uuid = require('uuid');
var jsondiffpatch = require('jsondiffpatch');
var Player = require('./player');

module.exports = PlayerServer;

PlayerServer.plugins = [];

PlayerServer.actions = {
  'addid': {
    permission: 'add',
    args: 'object',
    fn: function(self, client, items) {
      self.player.addItems(items);
    },
  },
  'clear': {
    permission: 'control',
    fn: function(self) {
      self.player.clearPlaylist();
    },
  },
  'deleteTracks': {
    permission: 'admin',
    args: 'array',
    fn: function(self, client, keys) {
      for (var i = 0; i < keys.length; i += 1) {
        var key = keys[i];
        self.player.deleteFile(key);
      }
    },
  },
  'deleteid': {
    permission: 'control',
    args: 'array',
    fn: function(self, client, ids) {
      self.player.removePlaylistItems(ids);
    },
  },
  'dynamicModeOn': {
    permission: 'control',
    args: 'boolean',
    fn: function(self, client, on) {
      self.player.setDynamicModeOn(on);
    },
  },
  'dynamicModeHistorySize': {
    permission: 'control',
    args: 'number',
    fn: function(self, client, size) {
      self.player.setDynamicModeHistorySize(size);
    },
  },
  'dynamicModeFutureSize': {
    permission: 'control',
    args: 'number',
    fn: function(self, client, size) {
      self.player.setDynamicModeFutureSize(size);
    },
  },
  'importUrl': {
    permission: 'control',
    args: 'object',
    fn: function(self, client, args) {
      var urlString = String(args.url);
      var id = args.id;
      self.player.importUrl(urlString, function(err, dbFile) {
        var key = null;
        if (err) {
          console.error("Unable to import url:", urlString, "error:", err.stack);
        } else if (!dbFile) {
          console.error("Unable to import file due to race condition.");
        } else {
          key = dbFile.key;
        }
        client.sendMessage('importUrl', {id: id, key: key});
      });
    },
  },
  'subscribe': {
    permission: 'read',
    args: 'object',
    fn: function(self, client, args) {
      var name = args.name;
      var subscription = self.subscriptions[name];
      if (!subscription) {
        console.warn("Invalid subscription item:", name);
        return;
      }
      if (args.delta) {
        client.subscriptions[name] = 'delta';
        if (args.version !== subscription.version) {
          client.sendMessage(name, {
            version: subscription.version,
            reset: true,
            delta: jsondiffpatch.diff(undefined, subscription.value),
          });
        }
      } else {
        client.subscriptions[name] = 'simple';
        client.sendMessage(name, subscription.value);
      }
    },
  },
  'updateTags': {
    permission: 'admin',
    args: 'object',
    fn: function(self, client, obj) {
      self.player.updateTags(obj);
    },
  },
  'unsubscribe': {
    permission: 'read',
    args: 'string',
    fn: function(self, client, name) {
      delete client.subscriptions[name];
    },
  },
  'move': {
    permission: 'control',
    args: 'object',
    fn: function(self, client, items) {
      self.player.movePlaylistItems(items);
    },
  },
  'password': {
    permission: null,
    args: 'string',
    fn: function(self, client, password) {
      var perms = self.authenticate(password);
      var success = perms != null;
      if (success) client.permissions = perms;
      client.sendMessage('permissions', client.permissions);
    },
  },
  'pause': {
    permission: 'control',
    fn: function(self, client) {
      self.player.pause();
    },
  },
  'play': {
    permission: 'control',
    fn: function(self, client) {
      self.player.play();
    },
  },
  'seek': {
    permission: 'control',
    args: 'object',
    fn: function(self, client, args) {
      self.player.seek(args.id, args.pos);
    },
  },
  'repeat': {
    permission: 'control',
    args: 'number',
    fn: function(self, client, mode) {
      self.player.setRepeat(mode);
    },
  },
  'setvol': {
    permission: 'control',
    args: 'number',
    fn: function(self, client, vol) {
      self.player.setVolume(vol);
    },
  },
  'shuffle': {
    permission: 'control',
    fn: function(self, client) {
      self.player.shufflePlaylist();
    },
  },
  'stop': {
    permission: 'control',
    fn: function(self, client) {
      self.player.stop();
    },
  },
};

function PlayerServer(options) {
  this.player = options.player;
  this.authenticate = options.authenticate;
  this.defaultPermissions = options.defaultPermissions;
  this.subscriptions = {};
  this.clients = [];

  this.playlistId = uuid();
  this.libraryId = uuid();
  this.initialize();
}

PlayerServer.prototype.initialize = function() {
  var self = this;
  self.player.on('currentTrack', addSubscription('currentTrack', getCurrentTrack));
  self.player.on('dynamicModeOn', addSubscription('dynamicModeOn', getDynamicModeOn));
  self.player.on('dynamicModeHistorySize', addSubscription('dynamicModeHistorySize', getDynamicModeHistorySize));
  self.player.on('dynamicModeFutureSize', addSubscription('dynamicModeFutureSize', getDynamicModeFutureSize));
  self.player.on('repeatUpdate', addSubscription('repeat', getRepeat));
  self.player.on('volumeUpdate', addSubscription('volume', getVolume));
  self.player.on('playlistUpdate', addSubscription('playlist', serializePlaylist));

  var onLibraryUpdate = addSubscription('library', serializeLibrary);
  self.player.on('addDbTrack', onLibraryUpdate);
  self.player.on('updateDbTrack', onLibraryUpdate);
  self.player.on('deleteDbTrack', onLibraryUpdate);
  self.player.on('scanComplete', onLibraryUpdate);

  self.player.on('seek', function() {
    self.clients.forEach(function(client) {
      client.sendMessage('seek');
    });
  });

  setInterval(function() {
    self.clients.forEach(function(client) {
      client.sendMessage('time', new Date());
    });
  }, 30000);

  function addSubscription(name, serializeFn) {
    var subscription = self.subscriptions[name] = {
      version: uuid(),
      value: serializeFn(),
    };
    return function() {
      var newValue = serializeFn();
      var delta = jsondiffpatch.diff(subscription.value, newValue);
      if (!delta) return; // no delta, nothing to send!
      subscription.value = newValue;
      subscription.version = uuid();
      self.clients.forEach(function(client) {
        var clientSubscription = client.subscriptions[name];
        if (clientSubscription === 'simple') {
          client.sendMessage(name, newValue);
        } else if (clientSubscription === 'delta') {
          client.sendMessage(name, {
            version: subscription.version,
            delta: delta,
          });
        }
      });
    };
  }

  function getVolume(client) {
    return self.player.volume;
  }

  function getTime(client) {
    return new Date();
  }

  function getRepeat(client) {
    return self.player.repeat;
  }

  function getCurrentTrack() {
    return {
      currentItemId: self.player.currentTrack && self.player.currentTrack.id,
      isPlaying: self.player.isPlaying,
      trackStartDate: self.player.trackStartDate,
      pausedTime: self.player.pausedTime,
    };
  }

  function getDynamicModeOn() {
    return self.player.dynamicModeOn;
  }

  function getDynamicModeFutureSize() {
    return self.player.dynamicModeFutureSize;
  }

  function getDynamicModeHistorySize() {
    return self.player.dynamicModeHistorySize;
  }

  function serializePlaylist() {
    var playlist = self.player.playlist;
    var o = {};
    for (var id in playlist) {
      var item = playlist[id];
      o[id] = {
        key: item.key,
        sortKey: item.sortKey,
        isRandom: item.isRandom,
      };
    }
    return o;
  }

  function serializeLibrary() {
    var table = {};
    for (var key in self.player.libraryIndex.trackTable) {
      var track = self.player.libraryIndex.trackTable[key];
      table[key] = Player.trackWithoutIndex('read', track);
    }
    return table;
  }
};

PlayerServer.prototype.handleNewClient = function(client) {
  var self = this;
  client.subscriptions = {};
  client.permissions = self.defaultPermissions;
  client.on('message', onMessage);
  client.sendMessage('permissions', client.permissions);
  client.sendMessage('time', new Date());
  client.on('close', onClose);
  self.clients.push(client);
  PlayerServer.plugins.forEach(function(plugin) {
    plugin.handleNewClient(client);
  });

  function onClose() {
    var index = self.clients.indexOf(client);
    if (index >= 0) self.clients.splice(index, 1);
  }

  function onMessage(name, args) {
    var action = PlayerServer.actions[name];
    if (!action) {
      console.warn("Invalid command:", name);
      client.sendMessage("error", "invalid command: " + JSON.stringify(name));
      return;
    }
    var perm = action.permission;
    if (perm != null && !client.permissions[perm]) {
      var errText = "command " + JSON.stringify(name) +
        " requires permission " + JSON.stringify(perm);
      console.warn("permissions error:", errText);
      client.sendMessage("error", errText);
      return;
    }
    var argsType = Array.isArray(args) ? 'array' : typeof args;
    if (action.args && argsType !== action.args) {
      console.warn("expected arg type", action.args, args);
      client.sendMessage("error", "expected " + action.args + ": " + JSON.stringify(args));
      return;
    }
    console.info("ok command", name, args);
    action.fn(self, client, args);
  }
};
