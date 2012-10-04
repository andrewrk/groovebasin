Plugin = require('../plugin').Plugin
exports.Plugin = class Chat extends Plugin
  constructor: ->
    super
    # the online users list is always blank at startup
    @users = []

  restoreState: (state) =>
    @next_user_id = state.next_user_id ? 0
    @user_names = state.status.user_names ? {}
    @chats = state.status.chats ? []

  saveState: (state) =>
    state.next_user_id = @next_user_id
    state.status.users = @users
    state.status.user_names = @user_names
    state.status.chats = @chats

  setMpd: (@mpd) =>
    @mpd.on 'chat', @scrubStaleUserNames

  onSocketConnection: (socket) =>
    user_id = "user_" + @next_user_id
    @next_user_id += 1
    @users.push user_id
    socket.emit 'Identify', user_id
    socket.on 'Chat', (data) =>
      chat_object =
        user_id: user_id
        message: data.toString()
      @chats.push(chat_object)
      chats_limit = 100
      @chats.splice(0, @chats.length - chats_limit) if @chats.length > chats_limit
      @onStatusChanged()
    socket.on 'SetUserName', (data) =>
      user_name = data.toString().trim().split(/\s+/).join(" ")
      if user_name != ""
        user_name_limit = 20
        user_name = user_name.substr(0, user_name_limit)
        @user_names[user_id] = user_name
      else
        delete @user_names[user_id]
      @onStatusChanged()
    socket.on 'disconnect', =>
      @users = (id for id in @users when id != user_id)
      @scrubStaleUserNames()
    @onStatusChanged()

  scrubStaleUserNames: =>
    keep_user_ids = {}
    for user_id in @users
      keep_user_ids[user_id] = true
    for chat_object in @chats
      keep_user_ids[chat_object.user_id] = true
    for user_id of @user_names
      delete @user_names[user_id] unless keep_user_ids[user_id]
    @onStatusChanged()

