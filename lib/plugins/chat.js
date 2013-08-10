var Plugin, CHATS_LIMIT, USER_NAME_LIMIT, Chat;
Plugin = require('../plugin');
CHATS_LIMIT = 100;
USER_NAME_LIMIT = 20;
module.exports = Chat = (function(superclass){
  Chat.displayName = 'Chat';
  var prototype = extend$(Chat, superclass).prototype, constructor = Chat;
  function Chat(bus){
    var this$ = this instanceof ctor$ ? this : new ctor$;
    superclass.apply(this$, arguments);
    this$.users = [];
    bus.on('save_state', bind$(this$, 'saveState'));
    bus.on('restore_state', bind$(this$, 'restoreState'));
    bus.on('socket_connect', bind$(this$, 'onSocketConnection'));
    bus.on('mpd', bind$(this$, 'setMpd'));
    return this$;
  } function ctor$(){} ctor$.prototype = prototype;
  prototype.restoreState = function(state){
    var ref$;
    this.next_user_id = (ref$ = state.next_user_id) != null ? ref$ : 0;
    this.user_names = (ref$ = state.status.user_names) != null
      ? ref$
      : {};
    this.chats = (ref$ = state.status.chats) != null
      ? ref$
      : [];
  };
  prototype.saveState = function(state){
    state.next_user_id = this.next_user_id;
    state.status.users = this.users;
    state.status.user_names = this.user_names;
    state.status.chats = this.chats;
  };
  prototype.setMpd = function(mpd){
    this.mpd = mpd;
    this.mpd.on('chat', bind$(this, 'scrubStaleUserNames'));
  };
  prototype.onSocketConnection = function(socket){
    var user_id, this$ = this;
    user_id = "user_" + this.next_user_id;
    this.next_user_id += 1;
    this.users.push(user_id);
    socket.emit('Identify', user_id);
    socket.on('Chat', function(msg){
      var chat_object;
      chat_object = {
        user_id: user_id,
        message: msg
      };
      console.info("chat: " + this$.user_names[user_id] + ": " + msg);
      this$.chats.push(chat_object);
      if (this$.chats.length > CHATS_LIMIT) {
        this$.chats.splice(0, this$.chats.length - CHATS_LIMIT);
      }
      this$.emit('status_changed');
    });
    socket.on('SetUserName', function(data){
      var user_name;
      user_name = data.trim().replace(/\s+/g, " ");
      if (user_name !== "") {
        user_name = user_name.substr(0, USER_NAME_LIMIT);
        this$.user_names[user_id] = user_name;
      } else {
        delete this$.user_names[user_id];
      }
      this$.emit('status_changed');
    });
    socket.on('disconnect', function(){
      var res$, i$, ref$, len$, id;
      res$ = [];
      for (i$ = 0, len$ = (ref$ = this$.users).length; i$ < len$; ++i$) {
        id = ref$[i$];
        if (id !== user_id) {
          res$.push(id);
        }
      }
      this$.users = res$;
      this$.scrubStaleUserNames();
    });
    this.emit('status_changed');
  };
  prototype.scrubStaleUserNames = function(){
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
    this.emit('status_changed');
  };
  return Chat;
}(Plugin));
function extend$(sub, sup){
  function fun(){} fun.prototype = (sub.superclass = sup).prototype;
  (sub.prototype = new fun).constructor = sub;
  if (typeof sup.extended == 'function') sup.extended(sub);
  return sub;
}
function bind$(obj, key){
  return function(){ return obj[key].apply(obj, arguments) };
}