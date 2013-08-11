var CHATS_LIMIT = 100;
var USER_NAME_LIMIT = 20;

module.exports = Chat;

function Chat(gb) {
  this.gb = gb;
  this.users = [];
  gb.on('aboutToSaveState', saveState.bind(this));
  gb.on('stateRestored', restoreState.bind(this));
  gb.on('socketConnect', onSocketConnection.bind(this));
}

function restoreState(state) {
  var ref$;
  this.next_user_id = (ref$ = state.next_user_id) != null ? ref$ : 0;
  this.user_names = (ref$ = state.status.user_names) != null ? ref$ : {};
  this.chats = (ref$ = state.status.chats) != null ? ref$ : [];
}
function saveState(state) {
  state.next_user_id = this.next_user_id;
  state.status.users = this.users;
  state.status.user_names = this.user_names;
  state.status.chats = this.chats;
}

function onSocketConnection(socket) {
  var self = this;
  var user_id = "user_" + self.next_user_id;
  self.next_user_id += 1;
  self.users.push(user_id);
  socket.emit('Identify', user_id);
  socket.on('Chat', function(msg){
    var chat_object;
    chat_object = {
      user_id: user_id,
      message: msg
    };
    console.info("chat: " + self.user_names[user_id] + ": " + msg);
    self.chats.push(chat_object);
    if (self.chats.length > CHATS_LIMIT) {
      self.chats.splice(0, self.chats.length - CHATS_LIMIT);
    }
    self.gb.saveAndSendStatus();
  });
  socket.on('SetUserName', function(data){
    var user_name;
    user_name = data.trim().replace(/\s+/g, " ");
    if (user_name !== "") {
      user_name = user_name.substr(0, USER_NAME_LIMIT);
      self.user_names[user_id] = user_name;
    } else {
      delete self.user_names[user_id];
    }
    self.gb.saveAndSendStatus();
  });
  socket.on('disconnect', function(){
    var res$, i$, ref$, len$, id;
    res$ = [];
    for (i$ = 0, len$ = (ref$ = self.users).length; i$ < len$; ++i$) {
      id = ref$[i$];
      if (id !== user_id) {
        res$.push(id);
      }
    }
    self.users = res$;
    scrubStaleUserNames.call(self);
  });
  self.gb.saveAndSendStatus();
}

function scrubStaleUserNames() {
  var keep_user_ids, i$, ref$, len$, user_id, chat_object;
  keep_user_ids = {};
  for (i$ = 0, len$ = (ref$ = this.users).length; i$ < len$; ++i$) {
    user_id = ref$[i$];
    keep_user_ids[user_id] = true;
  }
  for (i$ = 0, len$ = (ref$ = this.chats).length; i$ < len$; ++i$) {
    chat_object = ref$[i$];
    keep_user_ids[chat_object.user_id] = true;
  }
  for (user_id in this.user_names) {
    if (!keep_user_ids[user_id]) {
      delete this.user_names[user_id];
    }
  }
  this.gb.saveAndSendStatus();
}
