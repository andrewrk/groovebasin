var CHATS_LIMIT = 100;
var USER_NAME_LIMIT = 20;

module.exports = Chat;

function Chat(gb) {
  this.gb = gb;
  this.users = [];
}

Chat.prototype.initialize = function(cb) {
  var self = this;

  self.gb.db.get('plugin-chat', function(err, value) {
    if (err) {
      if (err.type === 'NotFoundError') {
        self.chats = [];
        self.userNames = {};
        self.nextUserId = 0;
      } else {
        return cb(err);
      }
    } else {
      var chatState = JSON.parse(value);
      self.chats = chatState.chats;
      self.userNames = chatState.userNames;
      self.nextUserId = chatState.nextUserId;
    }
    self.gb.on('socketConnect', onSocketConnection.bind(self));
    cb();
  });
};

Chat.prototype.persist = function() {
  var self = this;
  var chatState = {
    chats: self.chats,
    userNames: self.userNames,
    nextUserId: self.nextUserId,
  };
  self.gb.db.put('plugin-chat', JSON.stringify(chatState), function(err) {
    if (err) {
      console.error("Error persisting chat to db:", err.stack);
    }
  });
};

function onSocketConnection(socket) {
  var self = this;
  var userId = "user_" + self.nextUserId;
  self.nextUserId += 1;
  self.users.push(userId);
  socket.emit('Identify', userId);
  socket.on('Chat', function(msg){
    var chatObject = {
      userId: userId,
      message: msg
    };
    console.info("chat: " + self.userNames[userId] + ": " + msg);
    self.chats.push(chatObject);
    if (self.chats.length > CHATS_LIMIT) {
      self.chats.splice(0, self.chats.length - CHATS_LIMIT);
    }
    self.persist();
    broadcastUpdate(self, socket);
  });
  socket.on('SetUserName', function(data){
    var userName = data.trim().replace(/\s+/g, " ");
    if (userName !== "") {
      userName = userName.substring(0, USER_NAME_LIMIT);
      self.userNames[userId] = userName;
    } else {
      delete self.userNames[userId];
    }
    self.persist();
    broadcastUpdate(self, socket);
  });
  socket.on('disconnect', function(){
    var res$, i$, ref$, len$, id;
    res$ = [];
    for (i$ = 0, len$ = (ref$ = self.users).length; i$ < len$; ++i$) {
      id = ref$[i$];
      if (id !== userId) {
        res$.push(id);
      }
    }
    self.users = res$;
    scrubStaleUserNames.call(self);
    self.persist();
    broadcastUpdate(self, socket);
  });
  self.persist();
  broadcastUpdate(self, socket);
}

function scrubStaleUserNames() {
  var i$, ref$, len$, userId, chatObject;
  var keepUserIds = {};
  for (i$ = 0, len$ = (ref$ = this.users).length; i$ < len$; ++i$) {
    userId = ref$[i$];
    keepUserIds[userId] = true;
  }
  for (i$ = 0, len$ = (ref$ = this.chats).length; i$ < len$; ++i$) {
    chatObject = ref$[i$];
    keepUserIds[chatObject.userId] = true;
  }
  for (userId in this.userNames) {
    if (!keepUserIds[userId]) {
      delete this.userNames[userId];
    }
  }
}

function broadcastUpdate(self, socket) {
  var msg = {
    userNames: self.userNames,
    chats: self.chats,
    users: self.users,
  };
  socket.broadcast.emit('Chat', msg);
  socket.emit('Chat', msg);
}
