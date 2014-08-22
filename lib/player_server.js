var uuid = require('./uuid');
var jsondiffpatch = require('jsondiffpatch');
var Player = require('./player');
var Pend = require('pend');
var util = require('util');
var EventEmitter = require('events').EventEmitter;

var USERS_KEY_PREFIX = "Users.";
var EVENTS_KEY_PREFIX = "Events.";
var GUEST_USER_ID = "(guest)"; // uses characters not in the uuid() character set

var MAX_EVENT_COUNT = 100;

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
  'approve': {
    permission: 'admin',
    args: 'array',
    fn: function(self, client, approvals) {
      self.processApprovals(approvals);
    },
  },
  'clear': {
    permission: 'control',
    fn: function(self) {
      self.player.clearQueue();
    },
  },
  'chat': {
    permission: 'control',
    args: 'string',
    fn: function(self, client, text) {
      self.addEvent(client.user, 'chat', text);
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
      self.player.removeQueueItems(ids);
    },
  },
  'deleteUsers': {
    permission: 'admin',
    args: 'array',
    fn: function(self, client, ids) {
      self.deleteUsers(ids);
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
  'ensureAdminUser': {
    permission: null,
    fn: function(self) {
      self.ensureAdminUser();
    },
  },
  'hardwarePlayback': {
    permission: 'admin',
    args: 'boolean',
    fn: function(self, client, isOn) {
      self.player.setHardwarePlayback(isOn);
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
  'login': {
    permission: null,
    args: 'object',
    fn: function(self, client, args) {
      self.login(client, args.username, args.password);
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
    args: 'object',
    fn: function(self, client, args) {
      var errText;
      var name = args.name;
      var subscription = self.subscriptions[name];
      if (!subscription) {
        errText = "Invalid subscription item: " + JSON.stringify(name);
        console.warn(errText);
        client.sendMessage("error", errText);
        return;
      }
      if (!self.clientHasPerm(client, subscription.perm)) {
        // TODO instead of failing when they subscribe, let them subscribe,
        // and check permissions when the message is about to be sent.
        // this will better handle switching between an account with more
        // permissions and an account with less.
        errText = "subscribing to " + JSON.stringify(name) +
          " requires permission " + JSON.stringify(subscription.perm);
        console.warn(errText);
        client.sendMessage("error", errText);
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
  'updateUser': {
    permission: 'admin',
    args: 'object',
    fn: function(self, client, args) {
      self.updateUser(client, args.userId, args.perms);
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
      self.player.moveQueueItems(items);
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
  'requestApproval': {
    permission: null,
    fn: function(self, client) {
      self.requestApproval(client);
      self.sendUserMessage(client);
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
  'playlistCreate': {
    permission: 'control',
    args: 'object',
    fn: function(self, client, args) {
      self.player.playlistCreate(args.id, args.name);
    },
  },
  'playlistRename': {
    permission: 'control',
    args: 'object',
    fn: function(self, client, args) {
      self.player.playlistRename(args.id, args.name);
    },
  },
  'playlistDelete': {
    permission: 'control',
    args: 'array',
    fn: function(self, client, ids) {
      self.player.playlistDelete(ids);
    },
  },
  'playlistAddItems': {
    permission: 'control',
    args: 'object',
    fn: function(self, client, args) {
      self.player.playlistAddItems(args.id, args.items);
    },
  },
  'playlistRemoveItems': {
    permission: 'control',
    args: 'object',
    fn: function(self, client, args) {
      self.player.playlistRemoveItems(args.id, args.items);
    },
  },
  'playlistMoveItems': {
    permission: 'control',
    args: 'object',
    fn: function(self, client, args) {
      self.player.playlistMoveItems(args.id, args.items);
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
    perms: {
      read: true,
      add: true,
      control: true,
      admin: false,
    },
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
      admin: true,
    },
  };
  this.users[adminUser.id] = adminUser;
  this.saveUser(adminUser);

  console.info("No admin account found. Created one:");
  console.info("Username: " + adminUser.name);
  console.info("Password: " + adminUser.password);

  this.emit("haveAdminUser");
  this.emit("approvedUsers");
};

PlayerServer.prototype.initialize = function() {
  var self = this;
  self.player.on('currentTrack', addSubscription('currentTrack', getCurrentTrack));
  self.player.on('dynamicModeOn', addSubscription('dynamicModeOn', getDynamicModeOn));
  self.player.on('dynamicModeHistorySize', addSubscription('dynamicModeHistorySize', getDynamicModeHistorySize));
  self.player.on('dynamicModeFutureSize', addSubscription('dynamicModeFutureSize', getDynamicModeFutureSize));
  self.player.on('repeatUpdate', addSubscription('repeat', getRepeat));
  self.player.on('volumeUpdate', addSubscription('volume', getVolume));
  self.player.on('queueUpdate', addSubscription('queue', serializeQueue));
  self.player.on('hardwarePlayback', addSubscription('hardwarePlayback', getHardwarePlayback));

  var onLibraryUpdate = addSubscription('library', serializeLibrary);
  self.player.on('addDbTrack', onLibraryUpdate);
  self.player.on('updateDbTrack', onLibraryUpdate);
  self.player.on('deleteDbTrack', onLibraryUpdate);
  self.player.on('scanComplete', onLibraryUpdate);


  self.player.on('scanProgress', addSubscription('scanning', serializeScanState));

  var onPlaylistUpdate = addSubscription('playlists', serializePlaylists);
  self.player.on('playlistCreate', onPlaylistUpdate);
  self.player.on('playlistUpdate', onPlaylistUpdate);
  self.player.on('playlistDelete', onPlaylistUpdate);

  self.player.on('seek', function() {
    self.forEachClient(function(client) {
      client.sendMessage('seek');
    });
  });

  var onStreamersUpdate = addSubscription('streamers', serializeStreamers);
  self.player.on('streamerConnect', onStreamersUpdate);
  self.player.on('streamerDisconnect', onStreamersUpdate);
  self.on('connectedUsers', onStreamersUpdate);

  setInterval(function() {
    self.forEachClient(function(client) {
      client.sendMessage('time', new Date());
    });
  }, 30000);

  self.on('haveAdminUser', addSubscription('haveAdminUser', getHaveAdminUser));
  self.on('events', addSubscription('events', getEvents));
  self.on('connectedUsers', addSubscription('connectedUsers', getConnectedUsers));
  self.on('approvedUsers', addPermSubscription('approvedUsers', 'admin', getApprovedUsers));
  self.on('requests', addPermSubscription('requests', 'admin', getRequests));

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
      var delta = jsondiffpatch.diff(subscription.value, newValue);
      if (!delta) return; // no delta, nothing to send!
      subscription.value = newValue;
      subscription.version = uuid();
      self.forEachClient(function(client) {
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

  function getDynamicModeOn() {
    return self.player.dynamicModeOn;
  }

  function getDynamicModeFutureSize() {
    return self.player.dynamicModeFutureSize;
  }

  function getDynamicModeHistorySize() {
    return self.player.dynamicModeHistorySize;
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
    return self.player.playlists;
  }

  function serializeStreamers() {
    var userIds = [];
    var anonCount = 0;
    self.player.openStreamers.forEach(function(openStreamer) {
      var client = self.clients[openStreamer.token];
      if (client) {
        userIds.push(client.user.id);
      } else {
        anonCount += 1;
      }
    });
    return {
      userIds: userIds,
      anonCount: anonCount,
    };
  }

  function getConnectedUsers() {
    var users = {};
    for (var id in self.clients) {
      var client = self.clients[id];
      users[client.user.id] = {
        name: client.user.name,
      };
    }
    return users;
  }

  function getApprovedUsers() {
    var users = {};
    for (var id in self.users) {
      var user = self.users[id];
      if (user.approved) {
        users[id] = {
          name: user.name,
          perms: extend({}, user.perms),
        };
      }
    }
    return users;
  }

  function getRequests() {
    var users = {};
    for (var id in self.users) {
      var user = self.users[id];
      if (!user.approved && user.requested) {
        users[id] = {
          name: user.name,
        };
      }
    }
    return users;
  }

  function getEvents() {
    var events = {};
    for (var id in self.events) {
      var ev = self.events[id];
      events[ev.id] = ev;
    }
    return events;
  }

  function getHaveAdminUser() {
    return self.haveAdminUser();
  }
};

PlayerServer.prototype.init = function(cb) {
  var self = this;

  var pend = new Pend();
  pend.go(loadAllUsers);
  pend.go(loadAllEvents);
  pend.wait(cb);

  function loadAllUsers(cb) {
    var stream = self.db.createReadStream({
      start: USERS_KEY_PREFIX,
    });
    stream.on('data', function(data) {
      if (data.key.indexOf(USERS_KEY_PREFIX) !== 0) {
        stream.removeAllListeners();
        stream.destroy();
        done();
        return;
      }
      var user = deserializeUser(data.value);
      self.users[user.id] = user;
    });
    stream.on('error', function(err) {
      stream.removeAllListeners();
      stream.destroy();
      cb(err);
    });
    stream.on('close', done);
    function done() {
      self.ensureGuestUser();
      self.computeUsersIndex();
      self.emit('approvedUsers');
      self.emit('requests');
      self.emit('haveAdminUser');
      cb();
    }
  }

  function loadAllEvents(cb) {
    var stream = self.db.createReadStream({
      start: EVENTS_KEY_PREFIX,
    });
    stream.on('data', function(data) {
      if (data.key.indexOf(EVENTS_KEY_PREFIX) !== 0) {
        stream.removeAllListeners();
        stream.destroy();
        done();
        return;
      }
      var ev = deserializeEvent(data.value);
      self.events[ev.id] = ev;
    });
    stream.on('error', function(err) {
      stream.removeAllListeners();
      stream.destroy();
      cb(err);
    });
    stream.on('close', done);
    function done() {
      self.cacheEventsArray();
      self.emit('events');
      cb();
    }
  }
};

PlayerServer.prototype.forEachClient = function(fn) {
  for (var id in this.clients) {
    var client = this.clients[id];
    fn(client);
  }
};

PlayerServer.prototype.createGuestUser = function() {
  var user = true;
  var name;
  while (user) {
    name = this.guestUser.name + "-" + uuid.len(6);
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

PlayerServer.prototype.logout = function(client) {
  client.user = this.createGuestUser();
  this.sendUserMessage(client);
};

PlayerServer.prototype.handleNewClient = function(client) {
  var self = this;
  client.subscriptions = {};

  // this is a secret; if a user finds out the client.id they can execute
  // commands on behalf of that user.
  client.id = uuid();

  client.user = self.createGuestUser();
  self.clients[client.id] = client;
  client.on('message', onMessage);
  self.sendUserMessage(client);
  client.sendMessage('time', new Date());
  client.sendMessage('token', client.id);
  client.on('close', onClose);
  PlayerServer.plugins.forEach(function(plugin) {
    plugin.handleNewClient(client);
  });

  function onClose() {
    delete self.clients[client.id];
  }

  function onMessage(name, args) {
    var action = PlayerServer.actions[name];
    if (!action) {
      console.warn("Invalid command:", name);
      client.sendMessage("error", "invalid command: " + JSON.stringify(name));
      return;
    }
    var perm = action.permission;
    if (perm != null && !self.clientHasPerm(client, perm)) {
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

PlayerServer.prototype.clientHasPerm = function(client, perm) {
  return !perm || client.user.perms[perm];
};

PlayerServer.prototype.requestApproval = function(client) {
  client.user.requested = true;
  client.user.registered = true;
  this.saveUser(client.user);
  this.emit('requests');
  this.emit('connectedUsers');
};

PlayerServer.prototype.login = function(client, username, password) {
  var errText;
  if (!password) {
    errText = "empty password";
    console.warn("Refusing to login:", errText);
    client.sendMessage('error', errText);
    return;
  }
  var user = this.usernameIndex[username];
  if (!user) {
    client.user.name = username;
    client.user.password = password;
    client.user.requested = false;
    client.user.registered = true;

    this.computeUsersIndex();
    this.saveUser(client.user);

    this.emit("requests");
    this.emit("connectedUsers");

    this.addEvent(client.user, 'register');
    return;
  }

  if (user === client.user) {
    user.name = username;
    user.password = password;
    this.computeUsersIndex();
    this.saveUser(user);
    this.emit("connectedUsers");
    return;
  }

  if (!user.password || user.password !== password) {
    errText = "invalid login";
    console.warn(errText);
    client.sendMessage('error', errText);
    return;
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

  this.emit('connectedUsers');

  this.addEvent(client.user, 'login');

  function logIfError(err) {
    if (err) {
      console.error("Unable to modify users:", err.stack);
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
  client.sendMessage('user', {
    id: client.user.id,
    name: client.user.name,
    perms: client.user.perms,
    registered: client.user.registered,
    requested: client.user.requested,
    approved: client.user.approved,
  });
};

PlayerServer.prototype.saveUser = function(user) {
  this.db.put(userKey(user), serializeUser(user), function(err) {
    if (err) {
      console.error("Unable to save user:", err.stack);
    }
  });
};

PlayerServer.prototype.processApprovals = function(approvals) {
  var cmds = [];
  var eventsModified = false;

  var connectedUsersModified = false;
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
    if (connectedUserIds[user.id]) connectedUsersModified = true;
    if (!approval.approved) {
      user.requested = false;
      cmds.push({type: 'put', key: userKey(user), value: serializeUser(user)});
    } else if (replaceUser && user !== replaceUser) {
      replaceUser.name = approval.name;

      eventsModified = true;
      this.mergeUsers(cmds, user, replaceUser);
      if (connectedUserIds[replaceUser.id]) connectedUsersModified = true;
    } else {
      user.name = approval.name;
      user.approved = true;
      cmds.push({type: 'put', key: userKey(user), value: serializeUser(user)});
    }
  }

  if (cmds.length > 0) {
    this.computeUsersIndex();
    this.db.batch(cmds, logIfError);
    this.emit('approvedUsers');
    this.emit('requests');
    if (eventsModified) {
      this.emit('events');
    }
    if (connectedUsersModified) {
      this.emit('connectedUsers');
    }
  }

  function logIfError(err) {
    if (err) {
      console.error("Unable to modify users:", err.stack);
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

PlayerServer.prototype.addEvent = function(user, type, text) {
  var lastEvent = this.eventsInOrder[this.eventsInOrder.length - 1];
  var sortKey = lastEvent ? (lastEvent.sortKey + 1) : 0;
  var ev = {
    id: uuid(),
    userId: user.id,
    type: type,
    sortKey: sortKey,
    text: text,
  };
  this.events[ev.id] = ev;
  this.eventsInOrder.push(ev);
  var extraEvents = this.eventsInOrder.length - MAX_EVENT_COUNT;
  var cmds = [];
  if (extraEvents > 0) {
    for (var i = 0; i < extraEvents; i += 1) {
      deleteEventCmd(cmds, this.eventsInOrder[i]);
      delete this.events[this.eventsInOrder[i]];
    }
    this.eventsInOrder.splice(0, extraEvents);
  }
  cmds.push({type: 'put', key: eventKey(ev), value: serializeEvent(ev)});
  this.db.batch(cmds, logIfError);
  this.emit('events');

  function logIfError(err) {
    if (err) {
      console.error("Unable to modify events:", err.stack);
    }
  }
};

PlayerServer.prototype.updateUser = function(client, userId, perms) {
  var user = this.users[userId];
  if (!user) {
    var errText = "invalid user id";
    console.warn("unable to update user: " + errText);
    client.sendMessage('error', errText);
    return;
  }

  extend(user.perms, perms);
  this.saveUser(user);

  this.emit('approvedUsers');
  this.emit('haveAdminUser');

  for (var id in this.clients) {
    client = this.clients[id];
    if (client.user === user) {
      this.emit('connectedUsers');
      this.sendUserMessage(client);
      return;
    }
  }
};

PlayerServer.prototype.deleteUsers = function(ids) {
  var cmds = [];

  var connectedUsersChange = false;
  var approvedUsersChange = false;
  var haveAdminUserChange = false;
  var eventsChange = false;
  var requestsChange = false;
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
    approvedUsersChange = approvedUsersChange || user.approved;
    requestsChange = requestsChange || (user.requested && !user.approved);
    haveAdminUserChange = haveAdminUserChange || user.perms.admin;
    delete this.users[userId];
    for (var clientId in this.clients) {
      var client = this.clients[clientId];
      if (client.user === user) {
        connectedUsersChange = true;
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
  if (requestsChange) {
    this.emit('requestsChange');
  }
  if (connectedUsersChange) {
    this.emit('connectedUsers');
  }
  if (approvedUsersChange) {
    this.emit('approvedUsers');
  }
  if (haveAdminUserChange) {
    this.emit('haveAdminUser');
  }

  function logIfError(err) {
    if (err) {
      console.error("Unable to delete users:", err.stack);
    }
  }
};

PlayerServer.deleteAllUsers = function(db) {
  var cmds = [];
  var usersDeleted = 0;
  var eventsDeleted = 0;

  var pend = new Pend();
  pend.go(function(cb) {
    var stream = db.createReadStream({
      start: USERS_KEY_PREFIX,
    });
    stream.on('data', function(data) {
      if (data.key.indexOf(USERS_KEY_PREFIX) !== 0) {
        stream.removeAllListeners();
        stream.destroy();
        cb();
        return;
      }
      cmds.push({type: 'del', key: data.key});
      usersDeleted += 1;
    });
    stream.on('error', function(err) {
      stream.removeAllListeners();
      stream.destroy();
      cb(err);
    });
    stream.on('close', cb);
  });
  pend.go(function(cb) {
    var stream = db.createReadStream({
      start: EVENTS_KEY_PREFIX,
    });
    stream.on('data', function(data) {
      if (data.key.indexOf(EVENTS_KEY_PREFIX) !== 0) {
        stream.removeAllListeners();
        stream.destroy();
        cb();
        return;
      }
      cmds.push({type: 'del', key: data.key});
      eventsDeleted += 1;
    });
    stream.on('error', function(err) {
      stream.removeAllListeners();
      stream.destroy();
      cb(err);
    });
    stream.on('close', cb);
  });
  pend.wait(function(err) {
    if (err) throw err;
    db.batch(cmds, function(err) {
      if (err) throw err;
      console.log("Users deleted: " + usersDeleted);
      console.log("Events deleted: " + eventsDeleted);
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

