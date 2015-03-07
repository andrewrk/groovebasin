var uuid = require('./uuid');
var curlydiff = require('curlydiff');
var Player = require('./player');
var Pend = require('pend');
var util = require('util');
var EventEmitter = require('events').EventEmitter;
var keese = require('keese');
var dbIterate = require('./db_iterate');
var log = require('./log');
var LastFmNode = require('lastfm').LastFmNode;

var USERS_KEY_PREFIX = "Users.";
var EVENTS_KEY_PREFIX = "Events.";
var GUEST_USER_ID = "(guest)"; // uses characters not in the uuid() character set
var LASTFM_DB_KEY = 'Plugin.lastfm';

var INCOMING_PLAYLIST_ID = "(incoming)"; // uses characters not in the uuid() character set

var MAX_EVENT_COUNT = 400;
var MAX_NAME_LEN = 64; // user names
var MAX_PLAYLIST_NAME_LEN = 512; // user names
var MAX_TOKEN_LEN = 64; // subscribe event names, permission names, etc
var MAX_PASSWORD_LEN = 1024;
var UUID_LEN = 36; // our uuids are 32 length but they used to be 36 long
var MAX_CHAT_LEN = 2048;
var MAX_KEY_COUNT = 100000; // how many batch operations can you do in one request
var MAX_URL_LEN = MAX_KEY_COUNT * UUID_LEN; // URL length is serious business
var MAX_SORT_KEY_LEN = 2048;

module.exports = PlayerServer;

var defaultPermissions = {
  read: true,
  add: true,
  control: true,
  playlist: false,
  admin: false,
};

var actions = {
  'approve': {
    permission: 'admin',
    args: arrayArg(objectArg({
      id: stringArg(UUID_LEN),
      replaceId: stringArg(UUID_LEN),
      approved: booleanArg(),
      name: stringArg(MAX_NAME_LEN),
    })),
    fn: function(self, client, approvals) {
      self.processApprovals(approvals);
    },
  },
  'chat': {
    permission: 'control',
    args: objectArg({
      text: stringArg(MAX_CHAT_LEN),
      displayClass: stringArg(16, null),
    }),
    fn: function(self, client, args) {
      self.addEvent({
        user: client.user,
        text: args.text,
        type: 'chat',
        displayClass: args.displayClass,
      });
    },
  },
  'deleteTracks': {
    permission: 'admin',
    args: arrayArg(stringArg(UUID_LEN), MAX_KEY_COUNT),
    fn: function(self, client, keys) {
      self.player.deleteFiles(keys);
    },
  },
  'deleteUsers': {
    permission: 'admin',
    args: arrayArg(stringArg(UUID_LEN), MAX_KEY_COUNT),
    fn: function(self, client, ids) {
      self.deleteUsers(ids);
    },
  },
  'autoDjOn': {
    permission: 'control',
    args: booleanArg(),
    fn: function(self, client, on) {
      self.player.setAutoDjOn(on);
    },
  },
  'autoDjHistorySize': {
    permission: 'control',
    args: integerArg(),
    fn: function(self, client, size) {
      self.player.setAutoDjHistorySize(size);
    },
  },
  'autoDjFutureSize': {
    permission: 'control',
    args: integerArg(),
    fn: function(self, client, size) {
      self.player.setAutoDjFutureSize(size);
    },
  },
  'ensureAdminUser': {
    permission: null,
    fn: function(self) {
      self.ensureAdminUser();
    },
  },
  'hardwarePlayback': {
    permission: 'admin',
    args: booleanArg(),
    fn: function(self, client, isOn) {
      self.player.setHardwarePlayback(isOn);
    },
  },
  'importNames': {
    permission: 'add',
    args: objectArg({
      names: arrayArg(stringArg(1024), MAX_KEY_COUNT),
      autoQueue: booleanArg(false),
    }),
    fn: function(self, client, args) {
      self.player.importNames(args.names, function(err, dbFiles) {
        if (err) {
          log.error("Unable to import song names:", args.names, err.stack);
        } else if (dbFiles.length > 0) {
          self.handleImportedTracks(client, dbFiles, args.autoQueue);
        }
      });
    },
  },
  'importUrl': {
    permission: 'add',
    args: objectArg({
      url: stringArg(MAX_URL_LEN),
      autoQueue: booleanArg(false),
    }),
    fn: function(self, client, args) {
      self.player.importUrl(args.url, function(err, dbFiles) {
        if (err) {
          log.error("Unable to import url:", args.url, err.stack);
        } else if (!dbFiles) {
          log.warn("Unable to import url, unrecognized format");
        } else if (dbFiles.length > 0) {
          self.handleImportedTracks(client, dbFiles, args.autoQueue);
        }
      });
    },
  },
  'login': {
    permission: null,
    args: objectArg({
      username: stringArg(MAX_NAME_LEN),
      password: stringArg(MAX_PASSWORD_LEN),
    }),
    fn: function(self, client, args) {
      var errMsg = self.login(client, args.username, args.password);
      if (errMsg) {
        log.warn("Refusing to login:", errMsg);
        client.transport.sendMessage('error', errMsg);
      }
      self.sendUserMessage(client);
    },
  },
  'logout': {
    permission: null,
    fn: function(self, client) {
      self.logout(client);
    },
  },
  'subscribe': {
    permission: 'read',
    args: objectArg({
      name: stringArg(MAX_TOKEN_LEN),
      delta: booleanArg(false),
      version: stringArg(UUID_LEN, null),
    }),
    fn: function(self, client, args) {
      var errText;
      var name = args.name;
      var subscription = self.subscriptions[name];
      if (!subscription) {
        errText = "Invalid subscription item: " + JSON.stringify(name);
        log.warn(errText);
        client.transport.sendMessage("error", errText);
        return;
      }
      if (!self.userHasPerm(client.user, subscription.perm)) {
        errText = "subscribing to " + JSON.stringify(name) +
          " requires permission " + JSON.stringify(subscription.perm);
        log.warn(errText);
        client.transport.sendMessage("error", errText);
        return;
      }
      if (args.delta && client.subscriptions[name] !== 'delta') {
        client.subscriptions[name] = 'delta';
        if (args.version !== subscription.version) {
          client.transport.sendMessage(name, {
            version: subscription.version,
            reset: true,
            delta: curlydiff.diff(undefined, subscription.value),
          });
        }
      } else if (client.subscriptions[name] !== 'simple') {
        client.subscriptions[name] = 'simple';
        client.transport.sendMessage(name, subscription.value);
      }
    },
  },
  'updateTags': {
    permission: 'admin',
    args: dictArg(dictArg(manualParseArg()), MAX_KEY_COUNT, UUID_LEN),
    fn: function(self, client, obj) {
      self.player.updateTags(obj);
    },
  },
  'updateUser': {
    permission: 'admin',
    args: objectArg({
      userId: stringArg(UUID_LEN),
      perms: dictArg(booleanArg(null), Object.keys(defaultPermissions).length, MAX_TOKEN_LEN),
    }),
    fn: function(self, client, args) {
      self.updateUser(client, args.userId, args.perms);
    },
  },
  'unsubscribe': {
    permission: 'read',
    args: stringArg(MAX_TOKEN_LEN),
    fn: function(self, client, name) {
      self.unsubscribe(client, name);
    },
  },
  'move': {
    permission: 'control',
    args: dictArg(objectArg({
      sortKey: stringArg(MAX_SORT_KEY_LEN),
    }), MAX_KEY_COUNT, UUID_LEN),
    fn: function(self, client, items) {
      self.moveQueueItems(client, items);
    },
  },
  'pause': {
    permission: 'control',
    fn: function(self, client) {
      self.pause(client);
    },
  },
  'play': {
    permission: 'control',
    fn: function(self, client) {
      self.play(client);
    },
  },
  'queue': {
    permission: 'control',
    args: dictArg(objectArg({
      key: stringArg(UUID_LEN),
      sortKey: stringArg(MAX_SORT_KEY_LEN),
    }), MAX_KEY_COUNT, UUID_LEN),
    fn: function(self, client, items) {
      var id, item;
      var trackCount = 0;
      var trackKey = null;
      for (id in items) {
        item = items[id];
        trackCount += 1;
        trackKey = trackKey || item.key;
      }
      self.addEvent({
        user: client.user,
        type: 'queue',
        trackKey: trackKey,
        pos: trackCount,
      });
      self.player.addItems(items);
    },
  },
  'seek': {
    permission: 'control',
    args: objectArg({
      id: stringArg(UUID_LEN),
      pos: floatArg(),
    }),
    fn: function(self, client, args) {
      self.seek(client, args.id, args.pos);
    },
  },
  'setStreaming': {
    args: booleanArg(),
    fn: function(self, client, streamOn) {
      if (client.streaming === streamOn) return;
      client.streaming = streamOn;
      if (streamOn) {
        self.emit('streamStart', client);
      } else {
        self.emit('streamStop', client);
      }
    },
  },
  'remove': {
    permission: 'control',
    args: arrayArg(stringArg(UUID_LEN), MAX_KEY_COUNT),
    fn: function(self, client, ids) {
      self.removeQueueItems(client, ids);
    },
  },
  'repeat': {
    permission: 'control',
    args: integerArg(),
    fn: function(self, client, mode) {
      self.player.setRepeat(mode);
    },
  },
  'requestApproval': {
    permission: null,
    fn: function(self, client) {
      self.requestApproval(client);
      self.sendUserMessage(client);
    },
  },
  'setvol': {
    permission: 'control',
    args: floatArg(),
    fn: function(self, client, vol) {
      self.player.setVolume(vol);
    },
  },
  'stop': {
    permission: 'control',
    fn: function(self, client) {
      self.stop(client);
    },
  },
  'playlistCreate': {
    permission: 'playlist',
    args: objectArg({
      id: stringArg(UUID_LEN),
      name: stringArg(MAX_PLAYLIST_NAME_LEN),
    }),
    fn: function(self, client, args) {
      self.playlistCreate(client, args.id, args.name);
    },
  },
  'playlistRename': {
    permission: 'playlist',
    args: objectArg({
      id: stringArg(UUID_LEN),
      name: stringArg(MAX_PLAYLIST_NAME_LEN),
    }),
    fn: function(self, client, args) {
      self.playlistRename(client, args.id, args.name);
    },
  },
  'playlistDelete': {
    permission: 'playlist',
    args: arrayArg(stringArg(UUID_LEN), MAX_KEY_COUNT),
    fn: function(self, client, ids) {
      self.playlistDelete(client, ids);
    },
  },
  'playlistAddItems': {
    permission: 'playlist',
    args: objectArg({
      id: stringArg(UUID_LEN),
      items: dictArg(objectArg({
        key: stringArg(UUID_LEN),
        sortKey: stringArg(MAX_SORT_KEY_LEN),
      }), MAX_KEY_COUNT, UUID_LEN),
    }),
    fn: function(self, client, args) {
      self.playlistAddItems(client, args.id, args.items);
    },
  },
  'playlistRemoveItems': {
    permission: 'playlist',
    args: dictArg(arrayArg(stringArg(UUID_LEN), MAX_KEY_COUNT), MAX_KEY_COUNT, UUID_LEN),
    fn: function(self, client, removals) {
      self.playlistRemoveItems(client, removals);
    },
  },
  'playlistMoveItems': {
    permission: 'playlist',
    args: dictArg(dictArg(objectArg({
      sortKey: stringArg(MAX_SORT_KEY_LEN),
    }), MAX_KEY_COUNT, UUID_LEN), MAX_KEY_COUNT, UUID_LEN),
    fn: function(self, client, updates) {
      self.playlistMoveItems(client, updates);
    },
  },
  'LastFmGetSession': {
    permission: 'read',
    args: stringArg(MAX_PASSWORD_LEN),
    fn: function(self, client, token){
      self.lastFmGetSession(client, token);
    }
  },
  'LastFmScrobblersAdd': {
    permission: 'read',
    args: objectArg({
      username: stringArg(MAX_NAME_LEN),
      sessionKey: stringArg(MAX_PASSWORD_LEN),
    }),
    fn: function(self, client, args) {
      self.lastFmAddScrobbler(args.username, args.sessionKey);
    },
  },
  'LastFmScrobblersRemove': {
    permission: 'read',
    args: objectArg({
      username: stringArg(MAX_NAME_LEN),
      sessionKey: stringArg(MAX_PASSWORD_LEN),
    }),
    fn: function(self, client, args) {
      self.lastFmRemoveScrobbler(args.username, args.sessionKey);
    },
  },
};

util.inherits(PlayerServer, EventEmitter);
function PlayerServer(options) {
  EventEmitter.call(this);

  this.player = options.player;
  this.db = options.db;

  this.subscriptions = {};
  this.users = {};
  this.addGuestUser();
  this.usernameIndex = null; // username -> user
  this.computeUsersIndex();

  this.clients = {};

  this.events = {};
  this.eventsInOrder = [];

  this.lastFmApiKey = options.config.lastFmApiKey;
  this.lastFmApiSecret = options.config.lastFmApiSecret;
  this.lastFm = new LastFmNode({
    api_key: this.lastFmApiKey,
    secret: this.lastFmApiSecret,
  });
  this.previousNowPlaying = null;
  this.scrobblers = {};
  this.scrobbles = [];

  this.playlistId = uuid();
  this.libraryId = uuid();
  this.initialize();
}

PlayerServer.prototype.ensureGuestUser = function() {
  this.guestUser = this.users[GUEST_USER_ID];
  if (!this.guestUser) {
    this.addGuestUser();
  }
};

PlayerServer.prototype.addGuestUser = function() {
  // default guest user. overridden by db if present
  this.guestUser = {
    id: GUEST_USER_ID,
    name: 'Guest',
    password: "",
    registered: true,
    requested: true,
    approved: true,
    perms: extend({}, defaultPermissions),
  };
  this.users[this.guestUser.id] = this.guestUser;
};

PlayerServer.prototype.haveAdminUser = function() {
  for (var id in this.users) {
    var user = this.users[id];
    if (user.perms.admin) {
      return true;
    }
  }
  return false;
};

PlayerServer.prototype.ensureAdminUser = function() {
  if (this.haveAdminUser()) {
    return;
  }

  var user = true;
  var name;
  while (user) {
    name = "Admin-" + uuid.len(6);
    user = this.usernameIndex[name];
  }

  var adminUser = {
    id: uuid(),
    name: name,
    password: uuid(),
    registered: true,
    requested: true,
    approved: true,
    perms: {
      read: true,
      add: true,
      control: true,
      playlist: true,
      admin: true,
    },
  };
  this.users[adminUser.id] = adminUser;
  this.saveUser(adminUser);

  log.info("No admin account found. Created one:");
  log.info("Username: " + adminUser.name);
  log.info("Password: " + adminUser.password);

  this.emit("haveAdminUser");
  this.emit("users");
};

PlayerServer.prototype.initialize = function() {
  var self = this;

  self.player.on('currentTrack', addSubscription('currentTrack', getCurrentTrack));
  self.player.on('autoDjOn', addSubscription('autoDjOn', getAutoDjOn));
  self.player.on('autoDjHistorySize', addSubscription('autoDjHistorySize', getAutoDjHistorySize));
  self.player.on('autoDjFutureSize', addSubscription('autoDjFutureSize', getAutoDjFutureSize));
  self.player.on('repeatUpdate', addSubscription('repeat', getRepeat));
  self.player.on('volumeUpdate', addSubscription('volume', getVolume));
  self.player.on('queueUpdate', addSubscription('queue', serializeQueue));
  self.player.on('hardwarePlayback', addSubscription('hardwarePlayback', getHardwarePlayback));

  var onLibraryUpdate = addSubscription('library', serializeLibrary);
  self.player.on('updateDb', onLibraryUpdate);
  self.player.on('deleteDbTrack', onLibraryUpdate);
  self.player.on('scanComplete', onLibraryUpdate);

  var onQueueUpdate = addSubscription('libraryQueue', getQueueLibraryInfo);
  self.player.on('queueUpdate', onQueueUpdate);
  self.player.on('updateDb', onQueueUpdate);
  self.player.on('deleteDbTrack', onQueueUpdate);
  self.player.on('scanComplete', onQueueUpdate);

  self.player.on('scanProgress', addSubscription('scanning', serializeScanState));

  var onPlaylistUpdate = addSubscription('playlists', serializePlaylists);
  self.player.on('playlistCreate', onPlaylistUpdate);
  self.player.on('playlistUpdate', onPlaylistUpdate);
  self.player.on('playlistDelete', onPlaylistUpdate);

  self.player.on('seek', function() {
    self.forEachClient(function(client) {
      client.transport.sendMessage('seek');
    });
  });

  var onImportProgress = addSubscription('importProgress', serializeImportProgress);
  self.player.on('importStart', onImportProgress);
  self.player.on('importEnd', onImportProgress);
  self.player.on('importAbort', onImportProgress);
  self.player.on('importProgress', onImportProgress);

  // this is only anonymous streamers
  var onStreamersUpdate = addSubscription('streamers', serializeStreamers);
  self.player.on('streamerConnect', onStreamersUpdate);
  self.player.on('streamerDisconnect', onStreamersUpdate);

  setInterval(function() {
    self.forEachClient(function(client) {
      client.transport.sendMessage('time', new Date());
    });
  }, 30000);

  self.on('haveAdminUser', addSubscription('haveAdminUser', getHaveAdminUser));
  self.on('events', addSubscription('events', getEvents));

  var onUsersUpdate = addSubscription('users', getUsers);
  self.on('users', onUsersUpdate);
  self.on('streamStart', onUsersUpdate);
  self.on('streamStop', onUsersUpdate);

  // last.fm
  self.player.on('queueUpdate', self.updateNowPlaying.bind(self));
  self.player.on('play', self.scrobblePlay.bind(self));

  // events
  self.player.on('currentTrack', addCurrentTrackEvent);
  self.on('streamStart', addStreamerConnectEvent);
  self.on('streamStop', addStreamerDisconnectEvent);
  self.player.on('streamerConnect', maybeAddAnonStreamerConnectEvent);
  self.player.on('streamerDisconnect', maybeAddAnonStreamerDisconnectEvent);

  self.player.on('streamerDisconnect', self.checkLastStreamerDisconnected.bind(self));
  self.on('streamStop', self.checkLastStreamerDisconnected.bind(self));

  self.player.on('autoPause', addAutoPauseEvent);


  var prevCurrentTrackKey = null;
  function addCurrentTrackEvent() {
    var currentTrack = self.player.currentTrack;
    var currentTrackKey = currentTrack ? currentTrack.key : null;
    if (currentTrackKey === prevCurrentTrackKey) return;
    prevCurrentTrackKey = currentTrackKey;
    // nextTick to make the currentTrack event after the "so and so chose a
    // different song"
    var dbFile = currentTrackKey && self.player.libraryIndex.trackTable[currentTrackKey];
    var nowPlayingText = getNowPlayingText(dbFile);
    process.nextTick(function() {
      self.addEvent({
        type: 'currentTrack',
        trackKey: currentTrackKey,
        text: nowPlayingText,
      });
    });
  }

  function addAutoPauseEvent() {
    self.addEvent({
      type: 'autoPause',
    });
  }

  function addStreamerConnectEvent(client) {
    self.addEvent({
      user: client.user,
      type: 'streamStart',
    });
  }

  function addStreamerDisconnectEvent(client) {
    self.addEvent({
      user: client.user,
      type: 'streamStop',
    });
  }

  function maybeAddAnonStreamerConnectEvent(client) {
    if (!client) {
      self.addEvent({
        type: 'streamStart',
      });
    }
  }

  function maybeAddAnonStreamerDisconnectEvent(client) {
    if (!client) {
      self.addEvent({
        type: 'streamStop',
      });
    }
  }

  function addSubscription(name, serializeFn) {
    return addPermSubscription(name, null, serializeFn);
  }

  function addPermSubscription(name, perm, serializeFn) {
    var subscription = self.subscriptions[name] = {
      version: uuid(),
      value: serializeFn(),
      perm: perm,
    };
    return function() {
      var newValue = serializeFn();
      var delta = curlydiff.diff(subscription.value, newValue);
      if (delta === undefined) return; // no delta, nothing to send!
      subscription.value = newValue;
      subscription.version = uuid();
      self.forEachClient(function(client) {
        var clientSubscription = client.subscriptions[name];
        if (clientSubscription === 'simple') {
          client.transport.sendMessage(name, newValue);
        } else if (clientSubscription === 'delta') {
          client.transport.sendMessage(name, {
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

  function getHardwarePlayback(client) {
    return self.player.desiredPlayerHardwareState;
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

  function getAutoDjOn() {
    return self.player.autoDjOn;
  }

  function getAutoDjFutureSize() {
    return self.player.autoDjFutureSize;
  }

  function getAutoDjHistorySize() {
    return self.player.autoDjHistorySize;
  }

  function serializeQueue() {
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

  function getQueueLibraryInfo() {
    var table = {};
    for (var itemId in self.player.playlist) {
      var entry = self.player.playlist[itemId];
      var track = self.player.libraryIndex.trackTable[entry.key];
      table[track.key] = Player.trackWithoutIndex('read', track);
    }
    return table;
  }

  function serializeLibrary() {
    var table = {};
    for (var key in self.player.libraryIndex.trackTable) {
      var track = self.player.libraryIndex.trackTable[key];
      table[key] = Player.trackWithoutIndex('read', track);
    }
    return table;
  }

  function serializeScanState() {
    var ongoingScans = self.player.ongoingScans;
    var o = {};
    for (var key in ongoingScans) {
      var item = ongoingScans[key];
      o[key] = {
        fingerprintDone: item.fingerprintDone,
        loudnessDone: item.loudnessDone,
      };
    }
    return o;
  }

  function serializePlaylists() {
    var o = {};
    for (var key in self.player.playlists) {
      var pl = self.player.playlists[key];
      var outItems = {};
      for (var itemKey in pl.items) {
        var plItem = pl.items[itemKey];
        outItems[itemKey] = {
          key: plItem.key,
          sortKey: plItem.sortKey,
        };
      }
      o[key] = {
        name: pl.name,
        mtime: pl.mtime,
        items: outItems,
      };
    }
    return o;
  }

  function serializeStreamers() {
    var anonCount = 0;
    self.player.openStreamers.forEach(function(openStreamer) {
      if (!openStreamer.client) {
        anonCount += 1;
      }
    });
    return anonCount;
  }

  function getUsers() {
    var users = {};
    var outUser;
    for (var id in self.users) {
      var user = self.users[id];
      outUser = {
        name: user.name,
        perms: extend({}, user.perms),
      };
      if (user.requested) outUser.requested = true;
      if (user.approved) outUser.approved = true;
      users[id] = outUser;
    }
    for (var clientId in self.clients) {
      var client = self.clients[clientId];
      outUser = users[client.user.id];
      outUser.connected = true;
      if (client.streaming) outUser.streaming = true;
    }
    return users;
  }

  function getEvents() {
    var events = {};
    for (var id in self.events) {
      var ev = self.events[id];
      var outEvent = {
        date: ev.date,
        type: ev.type,
        sortKey: ev.sortKey,
        userId: ev.userId,
        text: ev.text,
        trackId: ev.trackId,
        pos: ev.pos,
        displayClass: ev.displayClass,
        playlistId: ev.playlistId,
      };
      events[ev.id] = outEvent;
    }
    return events;
  }

  function getHaveAdminUser() {
    return self.haveAdminUser();
  }

  function serializeImportProgress() {
    var out = {};
    for (var id in self.player.importProgress) {
      var ev = self.player.importProgress[id];
      var outEvent = {
        date: ev.date,
        filenameHintWithoutPath: ev.filenameHintWithoutPath,
        bytesWritten: ev.bytesWritten,
        size: ev.size,
      };
      out[ev.id] = outEvent;
    }
    return out;
  }
};

PlayerServer.prototype.checkLastStreamerDisconnected = function() {
  var streamerCount = 0;
  this.forEachClient(function(client) {
    streamerCount += client.streaming;
  });
  if (this.player.openStreamers.length === 0 && streamerCount === 0) {
    this.player.lastStreamerDisconnected();
  }
};

PlayerServer.prototype.init = function(cb) {
  var self = this;

  var pend = new Pend();
  pend.go(loadAllUsers);
  pend.go(loadAllEvents);
  pend.go(loadLastFmState);
  pend.wait(cb);

  function loadAllUsers(cb) {
    dbIterate(self.db, USERS_KEY_PREFIX, processOne, function(err) {
      if (err) return cb(err);
      self.ensureGuestUser();
      self.computeUsersIndex();
      self.emit('users');
      self.emit('haveAdminUser');
      cb();
    });
    function processOne(key, value) {
      var user = deserializeUser(value);
      self.users[user.id] = user;
    }
  }

  function loadAllEvents(cb) {
    dbIterate(self.db, EVENTS_KEY_PREFIX, processOne, function(err) {
      if (err) return cb(err);
      self.cacheEventsArray();
      self.emit('events');
      cb();
    });
    function processOne(key, value) {
      var ev = deserializeEvent(value);
      self.events[ev.id] = ev;
    }
  }

  function loadLastFmState(cb) {
    self.db.get(LASTFM_DB_KEY, function(err, value) {
      if (err) {
        var notFoundError = /^NotFound/.test(err.message);
        if (!notFoundError) return cb(err);
      } else {
        var state;
        try {
          state = JSON.parse(value);
        } catch (err) {
          cb(new Error("unable to parse lastfm state: " + err.message));
          return;
        }
        self.scrobblers = state.scrobblers;
        self.scrobbles = state.scrobbles;
      }
      // in case scrobbling fails and then the user presses stop, this will still
      // flush the queue.
      setInterval(self.flushScrobbleQueue.bind(self), 120000);
      cb();
    });
  }
};

PlayerServer.prototype.forEachClient = function(fn) {
  for (var id in this.clients) {
    var client = this.clients[id];
    fn(client);
  }
};

PlayerServer.prototype.createGuestUser = function(prefix) {
  prefix = prefix || this.guestUser.name;
  var user = true;
  var name;
  while (user) {
    name = prefix + "-" + uuid.len(6);
    user = this.usernameIndex[name];
  }
  user = {
    id: uuid(),
    name: name,
    password: "",
    registered: false,
    requested: false,
    approved: false,
    perms: extend({}, this.guestUser.perms),
  };
  this.users[user.id] = user;
  this.computeUsersIndex();
  this.saveUser(user);
  return user;
};

PlayerServer.prototype.unsubscribe = function(client, name) {
  delete client.subscriptions[name];
};

PlayerServer.prototype.logout = function(client) {
  client.user = this.createGuestUser();
  // unsubscribe from subscriptions that the client no longer has permissions for
  for (var name in client.subscriptions) {
    var subscription = this.subscriptions[name];
    if (!this.userHasPerm(client.user, subscription.perm)) {
      this.unsubscribe(client, name);
    }
  }
  this.sendUserMessage(client);
};

// transport should support sendMessage and emit 'message' and 'close'
PlayerServer.prototype.createClient = function(transport, prefix) {
  var self = this;
  var client = {
    transport: transport,
    subscriptions: {},

    // this is a secret; if a user finds out the client.id they can execute
    // commands on behalf of that user.
    id: uuid(),

    user: self.createGuestUser(prefix),
    streaming: false,
  };

  self.clients[client.id] = client;
  transport.on('message', onMessage);
  transport.on('close', onClose);
  self.sendUserMessage(client);
  transport.sendMessage('time', new Date());
  transport.sendMessage('token', client.id);
  transport.sendMessage('LastFmApiKey', self.lastFmApiKey);
  self.emit('users');
  self.addEvent({
    user: client.user,
    type: 'connect',
  });

  return client;

  function onClose() {
    self.addEvent({
      user: client.user,
      type: 'part',
    });
    var wasStreaming = client.streaming;
    delete self.clients[client.id];
    self.emit('users');
    if (wasStreaming) {
      self.checkLastStreamerDisconnected();
    }
  }

  function onMessage(name, args) {
    var action = actions[name];
    if (!action) {
      log.warn("Invalid command:", name);
      transport.sendMessage("error", "invalid command: " + JSON.stringify(name));
      return;
    }

    var perm = action.permission;
    var errMsg;
    if (perm != null && !self.userHasPerm(client.user, perm)) {
      errMsg = "command " + JSON.stringify(name) +
        " requires permission " + JSON.stringify(perm);
      log.warn("permissions error:", errMsg);
      transport.sendMessage("error", errMsg);
      return;
    }

    var result = validateArgs(args, action.args);
    if (result.error) {
      log.warn("invalid command:", result.error, name, args);
      transport.sendMessage("error", result.error);
      return;
    }

    log.debug("ok command", name, result.args);
    action.fn(self, client, result.args);
  }

};

PlayerServer.prototype.userHasPerm = function(user, perm) {
  if (!perm) {
    return true;
  }
  user = user ? this.users[user.id] : null;
  var perms = this.getUserPerms(user);
  return perms[perm];
};

PlayerServer.prototype.getUserPerms = function(user) {
  return (!user || !user.approved) ? this.guestUser.perms : user.perms;
};

PlayerServer.prototype.requestApproval = function(client) {
  client.user.requested = true;
  client.user.registered = true;
  this.saveUser(client.user);
  this.emit('users');
};

PlayerServer.prototype.loginOneLineAuth = function(client, passwordString) {
  // note that this requires '/' to be an invalid username character.
  // it is not, however, an invalid password character.
  var match = passwordString.match(/^([^\/]+)\/(.+)$/);
  if (!match) return "invalid password format";
  return this.login(client, match[1], match[2]);
};

PlayerServer.prototype.login = function(client, username, password) {
  if (!password) return "empty password";
  if (!validateUsername(username)) return "invalid username";
  var user = this.usernameIndex[username];
  if (!user) {
    client.user.name = username;
    client.user.password = password;
    client.user.registered = true;

    this.computeUsersIndex();
    this.saveUser(client.user);

    this.emit('users');

    this.addEvent({
      user: client.user,
      type: 'register',
    });
    return null;
  }

  if (user === client.user) {
    user.name = username;
    user.password = password;
    this.computeUsersIndex();
    this.saveUser(user);
    this.emit('users');
    return null;
  }

  if (!user.password || user.password !== password) {
    return "invalid login";
  }

  var oldUser = client.user;
  client.user = user;

  if (!oldUser.registered) {
    var cmds = [];
    this.mergeUsers(cmds, oldUser, user);
    if (cmds.length > 0) {
      this.db.batch(cmds, logIfError);
    }
  }

  this.emit('users');

  this.addEvent({
    user: client.user,
    type: 'login',
  });

  return null;

  function logIfError(err) {
    if (err) {
      log.error("Unable to modify users:", err.stack);
    }
  }
};

PlayerServer.prototype.mergeUsers = function(cmds, dupeUser, canonicalUser) {
  for (var eventId in this.events) {
    var ev = this.events[eventId];
    if (ev.userId === dupeUser.id) {
      ev.userId = canonicalUser.id;
      cmds.push({type: 'put', key: eventKey(ev), value: serializeEvent(ev)});
    }
  }
  this.forEachClient(function(client) {
    if (client.user === dupeUser) {
      client.user = canonicalUser;
    }
  });
  cmds.push({type: 'del', key: userKey(dupeUser)});
  cmds.push({type: 'put', key: userKey(canonicalUser), value: serializeUser(canonicalUser)});
  delete this.users[dupeUser.id];
};

PlayerServer.prototype.computeUsersIndex = function() {
  this.usernameIndex = {};
  for (var id in this.users) {
    var user = this.users[id];
    this.usernameIndex[user.name] = user;
  }
};

PlayerServer.prototype.sendUserMessage = function(client) {
  client.transport.sendMessage('user', {
    id: client.user.id,
    name: client.user.name,
    perms: this.getUserPerms(client.user),
    registered: client.user.registered,
    requested: client.user.requested,
    approved: client.user.approved,
  });
};

PlayerServer.prototype.saveUser = function(user) {
  this.db.put(userKey(user), serializeUser(user), function(err) {
    if (err) {
      log.error("Unable to save user:", err.stack);
    }
  });
};

PlayerServer.prototype.processApprovals = function(approvals) {
  var cmds = [];
  var eventsModified = false;

  var connectedUserIds = {};
  for (var id in this.clients) {
    var client = this.clients[id];
    connectedUserIds[client.user.id] = true;
  }

  for (var i = 0; i < approvals.length; i += 1) {
    var approval = approvals[i];
    var user = this.users[approval.id];
    var replaceUser = this.users[approval.replaceId];
    if (!user) continue;
    if (!approval.approved) {
      user.requested = false;
      cmds.push({type: 'put', key: userKey(user), value: serializeUser(user)});
    } else if (replaceUser && user !== replaceUser) {
      replaceUser.name = approval.name;

      eventsModified = true;
      this.mergeUsers(cmds, user, replaceUser);
    } else {
      user.name = approval.name;
      user.approved = true;
      cmds.push({type: 'put', key: userKey(user), value: serializeUser(user)});
    }
  }

  if (cmds.length > 0) {
    this.computeUsersIndex();
    this.db.batch(cmds, logIfError);
    if (eventsModified) {
      this.emit('events');
    }
    this.emit('users');
  }

  function logIfError(err) {
    if (err) {
      log.error("Unable to modify users:", err.stack);
    }
  }
};

PlayerServer.prototype.cacheEventsArray = function() {
  var self = this;
  self.eventsInOrder = Object.keys(self.events).map(eventById);
  self.eventsInOrder.sort(asc);
  self.eventsInOrder.forEach(function(ev, index) {
    ev.index = index;
  });

  function asc(a, b) {
    return operatorCompare(a.sortKey, b.sortKey);
  }
  function eventById(id) {
    return self.events[id];
  }
};

PlayerServer.prototype.addEvent = function(options) {
  var user = options.user;
  var type = options.type;
  var text = options.text;
  var trackKey = options.trackKey;
  var pos = options.pos;
  var dedupe = options.dedupe;
  var displayClass = options.displayClass;
  var playlistId = options.playlistId;

  var lastEvent = this.eventsInOrder[this.eventsInOrder.length - 1];
  if (dedupe && lastEvent.type === type && lastEvent.userId === user.id) {
    return;
  }
  var ev = {
    id: uuid(),
    date: new Date(),
    userId: user && user.id,
    type: type,
    sortKey: keese(lastEvent && lastEvent.sortKey, null),
    text: text,
    trackId: trackKey,
    pos: pos,
    displayClass: displayClass,
    playlistId: playlistId,
  };
  this.events[ev.id] = ev;
  this.eventsInOrder.push(ev);
  var extraEvents = this.eventsInOrder.length - MAX_EVENT_COUNT;
  var cmds = [];
  var usersChanged = 0;
  var haveAdminUserChange = false;
  if (extraEvents > 0) {
    var scrubUserIds = {};
    var i;
    for (i = 0; i < extraEvents; i += 1) {
      var thisEvent = this.eventsInOrder[i];
      if (thisEvent.user && !thisEvent.user.approved) {
        scrubUserIds[thisEvent.user.id] = true;
      }
      deleteEventCmd(cmds, thisEvent);
      delete this.events[thisEvent.id];
    }
    this.eventsInOrder.splice(0, extraEvents);
    // scrub users associated with these deleted events if they are not
    // referenced anywhere else
    for (i = 0; i < this.eventsInOrder.length; i += 1) {
      delete scrubUserIds[this.eventsInOrder[i].userId];
    }
    for (var clientId in this.clients) {
      delete scrubUserIds[this.clients[clientId].user.id];
    }
    for (var userId in scrubUserIds) {
      usersChanged += 1;
      var deletedUser = this.users[userId];
      delete this.users[userId];
      cmds.push({type: 'del', key: userKey(deletedUser)});
      haveAdminUserChange = haveAdminUserChange || deletedUser.perms.admin;
    }
  }
  cmds.push({type: 'put', key: eventKey(ev), value: serializeEvent(ev)});
  this.db.batch(cmds, logIfError);
  this.emit('events');
  if (usersChanged > 0) {
    this.emit('users');
  }
  if (haveAdminUserChange) {
    this.emit('haveAdminUser');
  }

  function logIfError(err) {
    if (err) {
      log.error("Unable to modify events:", err.stack);
    }
  }
};

PlayerServer.prototype.updateUser = function(client, userId, perms) {
  var user = this.users[userId];
  if (!user) {
    var errText = "invalid user id";
    log.warn("unable to update user: " + errText);
    client.transport.sendMessage('error', errText);
    return;
  }

  var guestUserChanged = (user === this.guestUser);

  extend(user.perms, perms);
  this.saveUser(user);


  for (var id in this.clients) {
    client = this.clients[id];
    if (client.user === user || (guestUserChanged && !client.user.approved)) {
      this.sendUserMessage(client);
    }
  }
  this.emit('haveAdminUser');
  this.emit('users');
};

PlayerServer.prototype.deleteUsers = function(ids) {
  var cmds = [];

  var haveAdminUserChange = false;
  var eventsChange = false;
  for (var i = 0; i < ids.length; i += 1) {
    var userId = ids[i];
    var user = this.users[userId];
    if (!user || user === this.guestUser) continue;

    var deleteEvents = [];
    var ev;
    for (var eventId in this.events) {
      ev = this.events[eventId];
      if (ev.userId === userId) {
        deleteEvents.push(ev);
      }
    }
    eventsChange = eventsChange || (deleteEvents.length > 0);
    for (var j = 0; j < deleteEvents.length; j += 1) {
      ev = deleteEvents[j];
      cmds.push({type: 'del', key: eventKey(ev)});
      delete this.events[ev.id];
    }

    cmds.push({type: 'del', key: userKey(user)});
    haveAdminUserChange = haveAdminUserChange || user.perms.admin;
    delete this.users[userId];
    for (var clientId in this.clients) {
      var client = this.clients[clientId];
      if (client.user === user) {
        this.logout(client);
        break;
      }
    }
  }

  if (cmds.length > 0) {
    this.computeUsersIndex();
    this.db.batch(cmds, logIfError);
  }

  if (eventsChange) {
    this.emit('events');
  }
  this.emit('users');
  if (haveAdminUserChange) {
    this.emit('haveAdminUser');
  }

  function logIfError(err) {
    if (err) {
      log.error("Unable to delete users:", err.stack);
    }
  }
};

PlayerServer.prototype.lastFmGetSession = function(client, token) {
  var self = this;
  self.lastFm.request("auth.getSession", {
    token: token,
    handlers: {
      success: function(data){
        delete self.scrobblers[data.session.name];
        client.transport.sendMessage('LastFmGetSessionSuccess', data);
      },
      error: function(error){
        log.error("error from last.fm auth.getSession:", error.message);
        client.transport.sendMessage('LastFmGetSessionError', error.message);
      }
    }
  });
};

PlayerServer.prototype.lastFmAddScrobbler = function(username, sessionKey) {
  var existingUser = this.scrobblers[username];
  if (existingUser) {
    log.warn("Trying to overwrite a scrobbler:", username);
    return;
  }
  this.scrobblers[username] = sessionKey;
  this.lastFmPersist();
};

PlayerServer.prototype.lastFmRemoveScrobbler = function(username, sessionKey) {
  var realSessionKey = this.scrobblers[username];
  if (realSessionKey !== sessionKey) {
    log.warn("Invalid session key from user trying to remove scrobbler:", username);
    return;
  }
  delete this.scrobblers[username];
  this.lastFmPersist();
};

PlayerServer.prototype.lastFmPersist = function() {
  var state = {
    scrobblers: this.scrobblers,
    scrobbles: this.scrobbles,
  };
  this.db.put(LASTFM_DB_KEY, JSON.stringify(state), function(err) {
    if (err) {
      log.error("Unable to persist lastfm state to db:", err.stack);
    }
  });
};

PlayerServer.prototype.flushScrobbleQueue = function() {
  var self = this;
  var params;
  var maxSimultaneous = 10;
  var count = 0;
  while ((params = self.scrobbles.shift()) != null && count++ < maxSimultaneous) {
    log.debug("scrobbling " + params.track + " for session " + params.sk);
    params.handlers = {
      error: onError,
    };
    self.lastFm.request('track.scrobble', params);
  }
  self.lastFmPersist();

  function onError(error){
    log.error("error from last.fm track.scrobble:", error.stack);
    if (!error.code || error.code === 11 || error.code === 16) {
      // try again
      self.scrobbles.push(params);
      self.lastFmPersist();
    }
  }
};

PlayerServer.prototype.updateNowPlaying = function() {
  var self = this;

  if (!self.player.isPlaying) return;

  var track = self.player.currentTrack;
  if (!track) return;

  if (self.previousNowPlaying === track) return;
  self.previousNowPlaying = track;

  var dbFile = self.player.libraryIndex.trackTable[track.key];
  if (!dbFile.artistName) {
    log.debug("Not updating last.fm now playing for " + dbFile.name + ": missing artist");
    return;
  }

  for (var username in self.scrobblers) {
    var sessionKey = self.scrobblers[username];
    var props = {
      sk: sessionKey,
      track: dbFile.name,
      artist: dbFile.artistName,
      album: dbFile.albumName,
      albumArtist: dbFile.albumArtistName,
      trackNumber: dbFile.track,
      duration: Math.round(dbFile.duration),
      handlers: {
        error: onError
      }
    };
    log.debug("updateNowPlaying", props);
    self.lastFm.request("track.updateNowPlaying", props);
  }

  function onError(error){
    log.error("unable to update last.fm now playing:", error.message);
  }
};

PlayerServer.prototype.scrobblePlay = function(item, dbFile, playingStart) {
  if (!dbFile.artistName) {
    log.debug("Not scrobbling " + dbFile.name + " - missing artist.");
  }

  for (var username in this.scrobblers) {
    var sessionKey = this.scrobblers[username];
    this.scrobbles.push({
      sk: sessionKey,
      chosenByUser: +!item.isRandom,
      timestamp: Math.round(playingStart.getTime() / 1000),
      album: dbFile.albumName,
      track: dbFile.name,
      artist: dbFile.artistName,
      albumArtist: dbFile.albumArtistName,
      duration: Math.round(dbFile.duration),
      trackNumber: dbFile.track,
    });
  }
  this.lastFmPersist();
  this.flushScrobbleQueue();
};

PlayerServer.prototype.moveQueueItems = function(client, items) {
  this.player.moveQueueItems(items);
  this.addEvent({
    user: client.user,
    type: 'move',
    dedupe: true,
  });
};

PlayerServer.prototype.moveRangeToPos = function(client, start, end, pos) {
  this.addEvent({
    user: client.user,
    type: 'move',
    dedupe: true,
  });
  this.player.moveRangeToPos(start, end, pos);
};

PlayerServer.prototype.moveIdsToPos = function(client, ids, pos) {
  this.addEvent({
    user: client.user,
    type: 'move',
    dedupe: true,
  });
  this.player.moveIdsToPos(ids, pos);
};

PlayerServer.prototype.pause = function(client) {
  if (!this.player.isPlaying) return;
  this.addEvent({
    user: client.user,
    type: 'pause',
  });
  this.player.pause();
};

PlayerServer.prototype.play = function(client) {
  if (this.player.isPlaying) return;
  this.addEvent({
    user: client.user,
    type: 'play',
  });
  this.player.play();
};

PlayerServer.prototype.stop = function(client) {
  this.addEvent({
    user: client.user,
    type: 'stop',
  });
  this.player.stop();
};

PlayerServer.prototype.seekToIndex = function(client, index, pos) {
  var track = this.player.seekToIndex(index, pos);
  if (!track) return null;
  this.addEvent({
    user: client.user,
    type: 'seek',
    trackKey: track.key,
    pos: pos,
  });
  return track;
};

PlayerServer.prototype.next = function(client) {
  return this.skipBy(client, 1);
};

PlayerServer.prototype.prev = function(client) {
  return this.skipBy(client, -1);
};

PlayerServer.prototype.skipBy = function(client, amt) {
  var track = this.player.skipBy(amt);
  if (!track) return;
  this.addEvent({
    user: client.user,
    type: 'seek',
    trackKey: track.key,
    pos: 0,
  });
  return track;
};

PlayerServer.prototype.seek = function(client, id, pos) {
  var track = this.player.seek(id, pos);
  if (!track) return null;
  this.addEvent({
    user: client.user,
    type: 'seek',
    trackKey: track.key,
    pos: pos,
  });
  return track;
};

PlayerServer.prototype.playlistRename = function(client, playlistId, newName) {
  var oldName = this.player.playlists[playlistId].name;
  this.player.playlistRename(playlistId, newName);
  this.addEvent({
    user: client.user,
    type: 'playlistRename',
    playlistId: playlistId,
    text: oldName,
  });
};

PlayerServer.prototype.playlistDelete = function(client, playlistIds) {
  for (var i = 0; i < playlistIds.length; i += 1) {
    var playlistId = playlistIds[i];
    var oldName = this.player.playlists[playlistId].name;
    this.addEvent({
      user: client.user,
      type: 'playlistDelete',
      playlistId: playlistId,
      text: oldName,
    });
  }
  this.player.playlistDelete(playlistIds);
};

PlayerServer.prototype.playlistCreate = function(client, playlistId, name) {
  this.addEvent({
    user: client.user,
    type: 'playlistCreate',
    playlistId: playlistId,
  });
  return this.player.playlistCreate(playlistId, name);
};

PlayerServer.prototype.playlistAddItems = function(client, playlistId, items) {
  var itemCount = keyCount(items);
  if (itemCount === 0) return;
  this.addEvent({
    user: client.user,
    type: 'playlistAddItems',
    playlistId: playlistId,
    trackKey: firstInObject(items).key,
    pos: itemCount,
  });
  this.player.playlistAddItems(playlistId, items);
};

PlayerServer.prototype.queueTrackKeys = function(client, trackKeys) {
  if (trackKeys.length === 0) return;
  this.addEvent({
    user: client.user,
    type: 'queue',
    trackKey: trackKeys[0],
    pos: trackKeys.length,
  });
  this.player.queueTrackKeys(trackKeys);
};

PlayerServer.prototype.insertTracks = function(client, index, keys, tagAsRandom) {
  this.addEvent({
    user: client.user,
    type: 'queue',
    trackKey: keys[0],
    pos: keys.length,
  });
  this.player.insertTracks(index, keys, tagAsRandom);
};

PlayerServer.prototype.playlistRemoveItems = function(client, removals) {
  var totalRemovals = 0;
  var totalPlaylists = 0;
  var playlistForEvent;
  var itemKey;
  for (var playlistId in removals) {
    var itemKeys = removals[playlistId];
    if (itemKeys.length === 0) continue;

    var playlist = this.player.playlists[playlistId];
    if (!playlist) continue;

    playlistForEvent = playlist;
    totalPlaylists += 1;
    totalRemovals += itemKeys.length;
    itemKey = itemKeys[0];
  }
  this.addEvent({
    user: client.user,
    type: 'playlistRemoveItems',
    playlistId: (totalPlaylists === 1) ? playlistForEvent.id : null,
    pos: totalRemovals,
    trackKey: playlistForEvent.items[itemKey].key,
  });
  this.player.playlistRemoveItems(removals);
};

PlayerServer.prototype.playlistMoveItems = function(client, updates) {
  var totalMoves = 0;
  var totalPlaylists = 0;
  var playlistForEvent;
  for (var playlistId in updates) {
    var items = updates[playlistId];
    var playlist = this.player.playlists[playlistId];
    if (!playlist) continue;

    playlistForEvent = playlist;
    totalPlaylists += 1;
    totalMoves += keyCount(items);
  }
  this.addEvent({
    user: client.user,
    type: 'playlistMoveItems',
    playlistId: (totalPlaylists === 1) ? playlistForEvent.id : null,
    pos: totalMoves,
  });
  this.player.playlistMoveItems(updates);
};

PlayerServer.prototype.clearQueue = function(client) {
  this.addEvent({
    user: client.user,
    type: 'clearQueue',
  });
  this.player.clearQueue();
};

PlayerServer.prototype.removeQueueItems = function(client, ids) {
  var item = (ids.length === 1) && this.player.playlist[ids[0]];
  var key = item && item.key;
  this.addEvent({
    user: client.user,
    type: 'remove',
    trackKey: key,
    pos: ids.length,
    text: item && getNowPlayingText(item),
  });
  this.player.removeQueueItems(ids);
};

PlayerServer.prototype.shufflePlaylist = function(client) {
  this.addEvent({
    user: client.user,
    type: 'shuffle',
  });
  this.player.shufflePlaylist();
};

PlayerServer.prototype.addDbFilesToIncomingPlaylist = function(dbFiles) {
  var playlist = this.player.playlists[INCOMING_PLAYLIST_ID];
  if (!playlist) {
    playlist = this.player.playlistCreate(INCOMING_PLAYLIST_ID, "Incoming");
  }
  var lastSortKey = null;
  for (var id in playlist.items) {
    var item = playlist.items[id];
    if (!lastSortKey || item.sortKey > lastSortKey) {
      lastSortKey = item.sortKey;
    }
  }
  this.player.sortAndQueueTracksInPlaylist(playlist, dbFiles, lastSortKey, null);
};

PlayerServer.prototype.handleImportedTracks = function(client, dbFiles, autoQueue) {
  var user = client && client.user;
  this.addEvent({
    user: user,
    type: 'import',
    trackKey: dbFiles[0].key,
    pos: dbFiles.length,
  });
  if (autoQueue) {
    this.player.sortAndQueueTracks(dbFiles);
    this.addEvent({
      user: user,
      type: 'queue',
      trackKey: dbFiles[0].key,
      pos: dbFiles.length,
    });
  }
  this.addDbFilesToIncomingPlaylist(dbFiles);
};

PlayerServer.deleteUsersAndEvents = function(db, deleteUsers) {
  var cmds = [];
  var usersDeleted = 0;
  var eventsDeleted = 0;

  var pend = new Pend();
  pend.go(function(cb) {
    if (!deleteUsers) return cb();
    dbIterate(db, USERS_KEY_PREFIX, processOne, cb);

    function processOne(key, value) {
      cmds.push({type: 'del', key: key});
      usersDeleted += 1;
    }
  });
  pend.go(function(cb) {
    dbIterate(db, EVENTS_KEY_PREFIX, processOne, cb);
    function processOne(key, value) {
      cmds.push({type: 'del', key: key});
      eventsDeleted += 1;
    }
  });
  pend.wait(function(err) {
    if (err) throw err;
    db.batch(cmds, function(err) {
      if (err) throw err;
      log.info("Users deleted: " + usersDeleted);
      log.info("Events deleted: " + eventsDeleted);
      process.exit(0);
    });
  });
};

function deleteEventCmd(cmds, ev) {
  cmds.push({type: 'del', key: eventKey(ev)});
}

function serializeUser(user) {
  return JSON.stringify(user);
}

function deserializeUser(payload) {
  return JSON.parse(payload);
}

function serializeEvent(ev) {
  return JSON.stringify(ev);
}

function deserializeEvent(payload) {
  return JSON.parse(payload);
}

function extend(o, src) {
  for (var key in src) o[key] = src[key];
  return o;
}

function userKey(user) {
  return USERS_KEY_PREFIX + user.id;
}

function eventKey(ev) {
  return EVENTS_KEY_PREFIX + ev.id;
}

function operatorCompare(a, b) {
  return a < b ? -1 : a > b ? 1 : 0;
}

function validateUsername(username) {
  return username.length < MAX_NAME_LEN && /^[^\s\/]+$/.test(username);
}

function stringArg(maxLength, defaultValue) {
  return {
    validate: validateString,
    maxLength: maxLength || 1024,
    defaultValue: defaultValue,
  };
}

function integerArg() {
  return {
    validate: validateInteger,
  };
}

function floatArg() {
  return {
    validate: validateFloat,
  };
}

function booleanArg(defaultValue) {
  return {
    validate: validateBoolean,
    defaultValue: defaultValue,
  };
}

function arrayArg(definition, maxLength) {
  return {
    validate: validateArray,
    definition: definition,
    maxLength: maxLength || 1024,
  };
}

function objectArg(fields) {
  return {
    validate: validateObject,
    fields: fields,
  };
}

function dictArg(definition, maxLength, maxKeyLength) {
  return {
    validate: validateDict,
    definition: definition,
    maxLength: maxLength || 1024,
    maxKeyLength: maxKeyLength || 1024,
  };
}

function manualParseArg() {
  return {
    validate: validateManualParse,
  };
}

function validateArgs(args, definition) {
  if (definition == null) {
    return args == null ? {args: args} : {error: "expected no arguments"};
  }
  args = (args === undefined ? definition.defaultValue : args);
  return (args === null && definition.defaultValue === null) ?
    {args: args} : definition.validate(args, definition);
}

function validateString(args, definition) {
  var typeOfArg = typeof args;
  if (typeOfArg !== 'string' &&
      !(definition.defaultValue === null && typeOfArg === null))
  {
    return {error: "expected string, got " + typeOfArg};
  }
  if (args.length > definition.maxLength) {
    return {error: "string too long. max length is " + definition.maxLength};
  }
  return {args: args};
}

function validateInteger(args, definition) {
  var typeOfArg = typeof args;
  if (typeOfArg !== 'number') {
    return {error: "expected integer, got " + typeOfArg};
  }
  if (args % 1 !== 0) {
    return {error: "expected integer, got float"};
  }
  return {args: args};
}

function validateFloat(args, definition) {
  var typeOfArg = typeof args;
  if (typeOfArg !== 'number') {
    return {error: "expected float, got " + typeOfArg};
  }
  if (isNaN(args)) {
    return {error: "expected float, got NaN"};
  }
  if (!isFinite(args)) {
    return {error: "expected float, got +/- Infinity"};
  }
  return {args: args};
}

function validateBoolean(args, definition) {
  var typeOfArg = typeof args;
  if (typeOfArg !== 'boolean') {
    return {error: "expected boolean, got " + typeOfArg};
  }
  return {args: args};
}

function validateManualParse(args, definition) {
  return {args: args};
}

function validateDict(args, definition) {
  var typeOfArg = (Array.isArray(args) ? 'array' : (typeof args));
  if (typeOfArg !== 'object') {
    return {error: "expected object, got " + typeOfArg};
  }
  if (args === null) {
    return {error: "expected object, got null"};
  }
  var subDef = definition.definition;
  var count = 0;
  for (var key in args) {
    count += 1;
    if (count > definition.maxLength) {
      return {error: "too many keys. max is " + definition.maxLength};
    }
    if (key.length > definition.maxKeyLength) {
      return {error: "key too long. max length is " + definition.maxKeyLength};
    }
    var val = args[key];
    var result = validateArgs(val, subDef);
    if (result.error) return result;
    args[key] = result.args;
  }
  return {args: args};
}

function validateObject(args, definition) {
  var typeOfArg = (Array.isArray(args) ? 'array' : (typeof args));
  if (typeOfArg !== 'object') {
    return {error: "expected object, got " + typeOfArg};
  }
  if (args === null) {
    return {error: "expected object, got null"};
  }
  for (var argKey in args) {
    if (!definition.fields[argKey]) {
      return {error: "unexpected field: " + argKey};
    }
  }
  for (var key in definition.fields) {
    var subDef = definition.fields[key];
    var val = args[key];
    var result = validateArgs(val, subDef);
    if (result.error) return result;
    args[key] = result.args;
  }
  return {args: args};
}

function validateArray(args, definition) {
  var typeOfArg = (Array.isArray(args) ? 'array' : (typeof args));
  if (typeOfArg !== 'array') {
    return {error: "expected array, got " + typeOfArg};
  }
  if (args.length > definition.maxLength) {
    return {error: "array too long. max length is " + definition.maxLength};
  }
  var subDef = definition.definition;
  for (var i = 0; i < args.length; i += 1) {
    var val = args[i];
    var result = validateArgs(val, subDef);
    if (result.error) return result;
    args[i] = result.args;
  }
  return {args: args};
}

function keyCount(o) {
  var count = 0;
  for (var key in o) {
    count += 1;
  }
  return count;
}

function firstInObject(o) {
  for (var key in o) {
    return o[key];
  }
}

function getNowPlayingText(track) {
  if (!track) return null;
  var str = track.name + " - " + track.artistName;
  if (track.albumName) {
    str += " - " + track.albumName;
  }
  return str;
}
