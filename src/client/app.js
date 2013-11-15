var $ = window.$;
var Handlebars = window.Handlebars;
var qq = window.qq;
var io = window.io;

var shuffle = require('mess');
var querystring = require('querystring');
var zfill = require('zfill');
var PlayerClient = require('./playerclient');
var streaming = require('./streaming');

var chatState;
var dynamicModeState = { on: false };


var selection = {
  ids: {
    playlist: {},
    artist: {},
    album: {},
    track: {},
    stored_playlist: {},
    stored_playlist_item: {}
  },
  cursor: null,
  type: null,
  isLibrary: function(){
    return this.type === 'artist' || this.type === 'album' || this.type === 'track';
  },
  isPlaylist: function(){
    return this.type === 'playlist';
  },
  isStoredPlaylist: function(){
    return this.type === 'stored_playlist' || this.type === 'stored_playlist_item';
  },
  clear: function(){
    this.ids.artist = {};
    this.ids.album = {};
    this.ids.track = {};
    this.ids.playlist = {};
    this.ids.stored_playlist = {};
    this.ids.stored_playlist_item = {};
  },
  fullClear: function(){
    this.clear();
    this.type = null;
    this.cursor = null;
  },
  selectOnly: function(sel_name, key){
    this.clear();
    this.type = sel_name;
    this.ids[sel_name][key] = true;
    this.cursor = key;
  },
  isMulti: function(){
    var result, k;
    if (this.isLibrary()) {
      result = 2;
      for (k in this.ids.artist) {
        if (!--result) return true;
      }
      for (k in this.ids.album) {
        if (!--result) return true;
      }
      for (k in this.ids.track) {
        if (!--result) return true;
      }
      return false;
    } else if (this.isPlaylist()) {
      result = 2;
      for (k in this.ids.playlist) {
        if (!--result) return true;
      }
      return false;
    } else if (this.isStoredPlaylist()) {
      result = 2;
      for (k in this.ids.stored_playlist) {
        if (!--result) return true;
      }
      for (k in this.ids.stored_playlist_item) {
        if (!--result) return true;
      }
      return false;
    } else {
      return false;
    }
  },
  getPos: function(type, key){
    var val;
    if (type == null) type = this.type;
    if (key == null) key = this.cursor;
    if (this.isLibrary()) {
      val = {
        type: 'library',
        artist: null,
        album: null,
        track: null
      };
      if (key != null) {
        switch (type) {
        case 'track':
          val.track = player.searchResults.trackTable[key];
          val.album = val.track.album;
          val.artist = val.album.artist;
          break;
        case 'album':
          val.album = player.searchResults.albumTable[key];
          val.artist = val.album.artist;
          break;
        case 'artist':
          val.artist = player.searchResults.artistTable[key];
          break;
        }
      } else {
        val.artist = player.searchResults.artistList[0];
      }
      return val;
    } else if (this.isStoredPlaylist()) {
      val = {
        type: 'stored_playlist',
        stored_playlist: null,
        stored_playlist_item: null
      };
      if (key != null) {
        switch (type) {
        case 'stored_playlist_item':
          val.stored_playlist_item = player.stored_playlist_item_table[key];
          val.stored_playlist = val.stored_playlist_item.playlist;
          break;
        case 'stored_playlist':
          val.stored_playlist = player.stored_playlist_table[key];
          break;
        }
      } else {
        val.stored_playlist = player.stored_playlists[0];
      }
      return val;
    } else {
      throw new Error("NothingSelected");
    }
  },
  posToArr: function(pos){
    var ref$;
    if (pos.type === 'library') {
      return [(ref$ = pos.artist) != null ? ref$.index : void 8, (ref$ = pos.album) != null ? ref$.index : void 8, (ref$ = pos.track) != null ? ref$.index : void 8];
    } else if (pos.type === 'stored_playlist') {
      return [(ref$ = pos.stored_playlist) != null ? ref$.index : void 8, (ref$ = pos.stored_playlist_item) != null ? ref$.index : void 8];
    } else {
      throw new Error("NothingSelected");
    }
  },
  posEqual: function(pos1, pos2){
    var arr1 = this.posToArr(pos1);
    var arr2 = this.posToArr(pos2);
    return compareArrays(arr1, arr2) === 0;
  },
  posInBounds: function(pos){
    if (pos.type === 'library') {
      return pos.artist != null;
    } else if (pos.type === 'stored_playlist') {
      return pos.stored_playlist != null;
    } else {
      throw new Error("NothingSelected");
    }
  },
  selectPos: function(pos){
    if (pos.type === 'library') {
      if (pos.track != null) {
        selection.ids.track[pos.track.key] = true;
      } else if (pos.album != null) {
        selection.ids.album[pos.album.key] = true;
      } else if (pos.artist != null) {
        selection.ids.artist[pos.artist.key] = true;
      }
    } else if (pos.type === 'stored_playlist') {
      if (pos.stored_playlist_item != null) {
        selection.ids.stored_playlist_item[pos.stored_playlist_item] = true;
      } else if (pos.stored_playlist != null) {
        selection.ids.stored_playlist[pos.stored_playlist] = true;
      }
    } else {
      throw new Error("NothingSelected");
    }
  },
  incrementPos: function(pos){
    if (pos.type === 'library') {
      if (pos.track != null) {
        pos.track = pos.track.album.trackList[pos.track.index + 1];
        if (pos.track == null) {
          pos.album = pos.artist.albumList[pos.album.index + 1];
          if (pos.album == null) {
            pos.artist = player.searchResults.artistList[pos.artist.index + 1];
          }
        }
      } else if (pos.album != null) {
        if (isAlbumExpanded(pos.album)) {
          pos.track = pos.album.trackList[0];
        } else {
          pos.artist = player.searchResults.artistList[pos.artist.index + 1];
          pos.album = null;
        }
      } else if (pos.artist != null) {
        if (isArtistExpanded(pos.artist)) {
          pos.album = pos.artist.albumList[0];
        } else {
          pos.artist = player.searchResults.artistList[pos.artist.index + 1];
        }
      }
    } else if (pos.type === 'stored_playlist') {
      if (pos.stored_playlist_item != null) {
        pos.stored_playlist_item = pos.stored_playlist_item.playlist.itemList[pos.stored_playlist_item.index + 1];
        if (pos.stored_playlist_item == null) {
          pos.stored_playlist = player.stored_playlists[pos.stored_playlist.index + 1];
        }
      } else if (pos.stored_playlist != null) {
        if (isStoredPlaylistExpanded(pos.stored_playlist)) {
          pos.stored_playlist_item = pos.stored_playlist.itemList[0];
          if (pos.stored_playlist_item == null) {
            pos.stored_playlist = player.stored_playlists[pos.stored_playlist.index + 1];
          }
        } else {
          pos.stored_playlist = player.stored_playlists[pos.stored_playlist.index + 1];
        }
      }
    } else {
      throw new Error("NothingSelected");
    }
  },
  toTrackKeys: function(random){
    var this$ = this;
    if (random == null) random = false;
    if (this.isLibrary()) {
      return libraryToTrackKeys();
    } else if (this.isPlaylist()) {
      return playlistToTrackKeys();
    } else if (this.isStoredPlaylist()) {
      return storedPlaylistToTrackKeys();
    } else {
      throw new Error("NothingSelected");
    }

    function libraryToTrackKeys() {
      var key;
      var track_set = {};
      function selRenderArtist(artist){
        var i, ref$, len$, album;
        for (i = 0, len$ = (ref$ = artist.albumList).length; i < len$; ++i) {
          album = ref$[i];
          selRenderAlbum(album);
        }
      }
      function selRenderAlbum(album){
        var i, ref$, len$, track;
        for (i = 0, len$ = (ref$ = album.trackList).length; i < len$; ++i) {
          track = ref$[i];
          selRenderTrack(track);
        }
      }
      function selRenderTrack(track){
        track_set[track.key] = this$.posToArr(getTrackSelPos(track));
      }
      function getTrackSelPos(track){
        return {
          type: 'library',
          artist: track.album.artist,
          album: track.album,
          track: track
        };
      }
      for (key in selection.ids.artist) {
        selRenderArtist(player.searchResults.artistTable[key]);
      }
      for (key in selection.ids.album) {
        selRenderAlbum(player.searchResults.albumTable[key]);
      }
      for (key in selection.ids.track) {
        selRenderTrack(player.searchResults.trackTable[key]);
      }
      return trackSetToKeys(track_set);
    }
    function playlistToTrackKeys(){
      var keys = [];
      for (var key in selection.ids.playlist) {
        keys.push(player.playlist.itemTable[key].track.key);
      }
      if (random) shuffle(keys);
      return keys;
    }
    function storedPlaylistToTrackKeys(){
      var track_set = {};
      function renderPlaylist(playlist){
        var i, ref$, len$, item;
        for (i = 0, len$ = (ref$ = playlist.itemList).length; i < len$; ++i) {
          item = ref$[i];
          renderPlaylistItem(item);
        }
      }
      function renderPlaylistItem(item){
        track_set[item.track.key] = this$.posToArr(getItemSelPos(item));
      }
      function getItemSelPos(item){
        return {
          type: 'stored_playlist',
          stored_playlist: item.playlist,
          stored_playlist_item: item
        };
      }
      for (var key in selection.ids.stored_playlist) {
        renderPlaylist(player.stored_playlist_table[key]);
      }
      for (key in selection.ids.stored_playlist_item) {
        renderPlaylistItem(player.stored_playlist_item_table[key]);
      }
      return trackSetToKeys(track_set);
    }

    function trackSetToKeys(track_set){
      var key;
      var keys = [];
      if (random) {
        for (key in track_set) {
          keys.push(key);
        }
        shuffle(keys);
        return keys;
      }
      var track_arr = [];
      for (key in track_set) {
        track_arr.push({
          key: key,
          pos: track_set[key],
        });
      }
      track_arr.sort(function(a, b) {
        return compareArrays(a.pos, b.pos);
      });
      for (var i = 0; i < track_arr.length; i += 1) {
        var track = track_arr[i];
        keys.push(track.key);
      }
      return keys;
    }
  }
};
var BASE_TITLE = document.title;
var MARGIN = 10;
var AUTO_EXPAND_LIMIT = 20;
var ICON_COLLAPSED = 'ui-icon-triangle-1-e';
var ICON_EXPANDED = 'ui-icon-triangle-1-se';
var permissions = {};
var socket = null;
var player = null;
var user_is_seeking = false;
var user_is_volume_sliding = false;
var started_drag = false;
var abortDrag = function(){};
var clickTab = null;
var myUserId = null;
var chat_name_input_visible = false;
var LoadStatus = {
  Init: 'Loading...',
  NoServer: 'Server is down.',
  GoodToGo: '[good to go]'
};
var repeatModeNames = ["Off", "One", "All"];
var load_status = LoadStatus.Init;
var settings_ui = {
  auth: {
    show_edit: false,
    password: ""
  }
};
var localState = {
  myUserIds: {},
  userName: null,
  lastfm: {
    username: null,
    session_key: null,
    scrobbling_on: false
  },
  authPassword: null
};
var $document = $(document);
var $window = $(window);
var $pl_window = $('#playlist-window');
var $left_window = $('#left-window');
var $playlist_items = $('#playlist-items');
var $dynamicMode = $('#dynamic-mode');
var $pl_btn_repeat = $('#pl-btn-repeat');
var $tabs = $('#tabs');
var $upload_tab = $tabs.find('.upload-tab');
var $chat_tab = $tabs.find('.chat-tab');
var $library = $('#library');
var $lib_filter = $('#lib-filter');
var $track_slider = $('#track-slider');
var $nowplaying = $('#nowplaying');
var $nowplaying_elapsed = $nowplaying.find('.elapsed');
var $nowplaying_left = $nowplaying.find('.left');
var $vol_slider = $('#vol-slider');
var $chatUserList = $('#chat-user-list');
var $chatList = $('#chat-list');
var $chat_user_id_span = $('#user-id');
var $settings = $('#settings');
var $upload_by_url = $('#upload-by-url');
var $main_err_msg = $('#main-err-msg');
var $main_err_msg_text = $('#main-err-msg-text');
var $stored_playlists = $('#stored-playlists');
var $upload = $('#upload');
var $track_display = $('#track-display');
var $chat_input = $('#chat-input');
var $chat_name_input = $('#chat-name-input');
var $chat_input_pane = $('#chat-input-pane');
var $lib_header = $('#library-pane .window-header');
var $pl_header = $pl_window.find('#playlist .header');

function saveLocalState(){
  localStorage.setItem('state', JSON.stringify(localState));
}

function loadLocalState() {
  var stateString = localStorage.getItem('state');
  if (!stateString) return;
  var obj;
  try {
    obj = JSON.parse(stateString);
  } catch (err) {
    return;
  }
  // this makes sure it still works when we change the format of localState
  for (var key in localState) {
    if (obj[key] !== undefined) {
      localState[key] = obj[key];
    }
  }
}

function haveUserName(){
  return !!(chatState && chatState.userNames[myUserId]);
}

function getUserName(){
  return userIdToUserName(myUserId);
}

function userIdToUserName(userId) {
  if (!chatState) return userId;
  var userName = chatState.userNames[userId];
  return userName || userId;
}

function setUserName(newName) {
  newName = newName.trim();
  localState.userName = newName;
  saveLocalState();
  socket.emit('SetUserName', newName);
}

function scrollLibraryToSelection(){
  var helpers;
  if ((helpers = getSelHelpers()) == null) {
    return;
  }
  delete helpers.playlist;
  scrollThingToSelection($library, helpers);
}
function scrollPlaylistToSelection(){
  var helpers;
  if ((helpers = getSelHelpers()) == null) {
    return;
  }
  delete helpers.track;
  delete helpers.artist;
  delete helpers.album;
  scrollThingToSelection($playlist_items, helpers);
}
function scrollThingToSelection($scroll_area, helpers){
  var top_pos, bottom_pos, sel_name, ref$, ids, table, $getDiv, id, $div, item_top, item_bottom, scroll_area_top, selection_top, selection_bottom, scroll_amt;
  top_pos = null;
  bottom_pos = null;
  for (sel_name in helpers) {
    ref$ = helpers[sel_name], ids = ref$[0], table = ref$[1], $getDiv = ref$[2];
    for (id in ids) {
      item_top = ($div = $getDiv(id)).offset().top;
      item_bottom = item_top + $div.height();
      if (top_pos == null || item_top < top_pos) {
        top_pos = item_top;
      }
      if (bottom_pos == null || item_bottom > bottom_pos) {
        bottom_pos = item_bottom;
      }
    }
  }
  if (top_pos != null) {
    scroll_area_top = $scroll_area.offset().top;
    selection_top = top_pos - scroll_area_top;
    selection_bottom = bottom_pos - scroll_area_top - $scroll_area.height();
    scroll_amt = $scroll_area.scrollTop();
    if (selection_top < 0) {
      return $scroll_area.scrollTop(scroll_amt + selection_top);
    } else if (selection_bottom > 0) {
      return $scroll_area.scrollTop(scroll_amt + selection_bottom);
    }
  }
}

function downloadKeys(keys) {
  var $form = $(document.createElement('form'));
  $form.attr('action', "/download/custom");
  $form.attr('method', "post");
  $form.attr('target', "_blank");
  for (var i = 0; i < keys.length; i += 1) {
    var key = keys[i];
    var $input = $(document.createElement('input'));
    $input.attr('type', 'hidden');
    $input.attr('name', 'key');
    $input.attr('value', key);
    $form.append($input);
  }
  $form.submit();
}
function getDragPosition(x, y){
  var result, i, ref$, len$, item, $item, middle, track;
  result = {};
  for (i = 0, len$ = (ref$ = $playlist_items.find(".pl-item").get()).length; i < len$; ++i) {
    item = ref$[i];
    $item = $(item);
    middle = $item.offset().top + $item.height() / 2;
    track = player.playlist.itemTable[$item.attr('data-id')];
    if (middle < y) {
      if (result.previous_key == null || track.sortKey > result.previous_key) {
        result.$previous = $item;
        result.previous_key = track.sortKey;
      }
    } else {
      if (result.next_key == null || track.sortKey < result.next_key) {
        result.$next = $item;
        result.next_key = track.sortKey;
      }
    }
  }
  return result;
}

function renderSettings() {
  var apiKey = ""; // TODO fix this when lastfm plugin is fixed
  var context = {
    lastfm: {
      auth_url: "http://www.last.fm/api/auth/?api_key=" +
        encodeURIComponent(apiKey) + "&cb=" +
        encodeURIComponent(location.protocol + "//" + location.host + "/"),
      username: localState.lastfm.username,
      session_key: localState.lastfm.session_key,
      scrobbling_on: localState.lastfm.scrobbling_on
    },
    auth: {
      password: localState.authPassword,
      show_edit: localState.authPassword == null || settings_ui.auth.show_edit,
      permissions: permissions
    },
    misc: {
      stream_url: streaming.getUrl()
    }
  };
  $settings.html(Handlebars.templates.settings(context));
  $settings.find('.signout').button();
  $settings.find('#toggle-scrobble').button();
  $settings.find('.auth-cancel').button();
  $settings.find('.auth-save').button();
  $settings.find('.auth-edit').button();
  $settings.find('.auth-clear').button();
  $settings.find('#auth-password').val(settings_ui.auth.password);
}

function scrollChatWindowToBottom(){
  $chatList.scrollTop(1000000);
}

function renderChat() {
  var i;
  var chatStatusText = "";
  var users = chatState && chatState.users;
  if (users) {
    var userObjects = [];
    for (i = 0; i < users.length; ++i) {
      var userId = users[i];
      var userName = userIdToUserName(userId);
      if (userName === '[server]') {
        continue;
      }
      var class_ = userId === myUserId ? "chat-user-self" : "chat-user";
      userObjects.push({
        userName: userName,
        "class": class_
      });
    }
    if (userObjects.length > 1) {
      chatStatusText = " (" + userObjects.length + ")";
    }
    $chatUserList.html(Handlebars.templates.chat_user_list({
      users: userObjects
    }));
    for (i = 0; i < chatState.chats.length; ++i) {
      var chatObject = chatState.chats[i];
      chatObject["class"] = localState.myUserIds[chatObject.userId] != null ? "chat-user-self" : "chat-user";
      chatObject.userName = userIdToUserName(chatObject.userId);
    }
    $chatList.html(Handlebars.templates.chat_list({
      chats: chatState.chats
    }));
    scrollChatWindowToBottom();
    $chat_user_id_span.text(chat_name_input_visible ? "" : getUserName() + ": ");
  }
  $chat_tab.find("span").text("Chat" + chatStatusText);
}

function renderPlaylistButtons(){
  $dynamicMode
    .prop("checked", dynamicModeState.on)
    .button("refresh");
  var repeatModeName = repeatModeNames[player.repeat];
  $pl_btn_repeat
    .button("option", "label", "Repeat: " + repeatModeName)
    .prop("checked", player.repeat !== PlayerClient.REPEAT_OFF)
    .button("refresh");
}
function renderPlaylist(){
  var context = {
    playlist: player.playlist.itemList,
  };
  var scrollTop = $playlist_items.scrollTop();
  $playlist_items.html(Handlebars.templates.playlist(context));
  refreshSelection();
  labelPlaylistItems();
  $playlist_items.scrollTop(scrollTop);
}
function renderStoredPlaylists(){
  var context = {
    stored_playlists: player.stored_playlists
  };
  var scrollTop = $stored_playlists.scrollTop();
  $stored_playlists.html(Handlebars.templates.stored_playlists(context));
  $stored_playlists.scrollTop(scrollTop);
  refreshSelection();
}

function labelPlaylistItems() {
  var item;
  var curItem = player.currentItem;
  $playlist_items.find(".pl-item").removeClass('current').removeClass('old');
  if (curItem != null && dynamicModeState.on) {
    for (var index = 0; index < curItem.index; ++index) {
      item = player.playlist.itemList[index];
      var itemId = item && item.id;
      if (itemId != null) {
        $("#playlist-track-" + itemId).addClass('old');
      }
    }
  }
  for (var i = 0; i < player.playlist.itemList.length; i += 1) {
    item = player.playlist.itemList[i];
    if (item.isRandom) {
      $("#playlist-track-" + item.id).addClass('random');
    }
  }
  if (curItem != null) {
    $("#playlist-track-" + curItem.id).addClass('current');
  }
}

function getSelHelpers(){
  var ref$;
  if ((player != null ? (ref$ = player.playlist) != null ? ref$.itemTable : void 8 : void 8) == null) {
    return null;
  }
  if ((player != null ? (ref$ = player.searchResults) != null ? ref$.artistTable : void 8 : void 8) == null) {
    return null;
  }
  return {
    playlist: [
      selection.ids.playlist, player.playlist.itemTable, function(id){
        return $("#playlist-track-" + id);
      }
    ],
    artist: [
      selection.ids.artist, player.searchResults.artistTable, function(id){
        return $("#lib-artist-" + toHtmlId(id));
      }
    ],
    album: [
      selection.ids.album, player.searchResults.albumTable, function(id){
        return $("#lib-album-" + toHtmlId(id));
      }
    ],
    track: [
      selection.ids.track, player.searchResults.trackTable, function(id){
        return $("#lib-track-" + toHtmlId(id));
      }
    ],
    stored_playlist: [
      selection.ids.stored_playlist, player.stored_playlist_table, function(id){
        return $("#stored-pl-pl-" + toHtmlId(id));
      }
    ],
    stored_playlist_item: [
      selection.ids.stored_playlist_item, player.stored_playlist_item_table, function(id){
        return $("#stored-pl-item-" + toHtmlId(id));
      }
    ]
  };
}
function refreshSelection(){
  var helpers, sel_name, ref$, ids, table, $getDiv, i, id, len$;
  if ((helpers = getSelHelpers()) == null) {
    return;
  }
  $playlist_items.find(".pl-item").removeClass('selected').removeClass('cursor');
  $library.find(".clickable").removeClass('selected').removeClass('cursor');
  $stored_playlists.find(".clickable").removeClass('selected').removeClass('cursor');
  if (selection.type == null) {
    return;
  }
  for (sel_name in helpers) {
    ref$ = helpers[sel_name], ids = ref$[0], table = ref$[1], $getDiv = ref$[2];
    for (i = 0, len$ = (ref$ = (fn$())).length; i < len$; ++i) {
      id = ref$[i];
      delete ids[id];
    }
    for (id in ids) {
      $getDiv(id).addClass('selected');
    }
    if (selection.cursor != null && sel_name === selection.type) {
      $getDiv(selection.cursor).addClass('cursor');
    }
  }
  function fn$(){
    var results$ = [];
    for (var id in ids) {
      if (table[id] == null) {
        results$.push(id);
      }
    }
    return results$;
  }
}
function renderLibrary() {
  var context = {
    artistList: player.searchResults.artistList,
    emptyLibraryMessage: player.haveFileListCache ? "No Results" : "loading..."
  };
  var scrollTop = $library.scrollTop();
  $library.html(Handlebars.templates.library(context));
  var $artists = $library.children("ul").children("li");
  var nodeCount = $artists.length;
  expandStuff($artists);
  $library.scrollTop(scrollTop);
  refreshSelection();

  function expandStuff($li_set) {
    for (var i = 0; i < $li_set.length; i += 1) {
      var li = $li_set[i];
      var $li = $(li);
      if (nodeCount >= AUTO_EXPAND_LIMIT) return;
      var $ul = $li.children("ul");
      var $sub_li_set = $ul.children("li");
      var proposedNodeCount = nodeCount + $sub_li_set.length;
      if (proposedNodeCount <= AUTO_EXPAND_LIMIT) {
        toggleLibraryExpansion($li);
        $ul = $li.children("ul");
        $sub_li_set = $ul.children("li");
        nodeCount = proposedNodeCount;
        expandStuff($sub_li_set);
      }
    }
  }
}
function getCurrentTrackPosition(){
  if (player.trackStartDate != null && player.state === "play") {
    return (new Date() - player.trackStartDate) / 1000;
  } else {
    return player.pausedTime;
  }
}

function updateSliderPos() {
  if (user_is_seeking) return;

  var duration, disabled, elapsed, sliderPos;
  if (player.currentItem != null && player.state && player.state !== "stop") {
    disabled = false;
    elapsed = getCurrentTrackPosition();
    duration = player.currentItem.track.duration;
    sliderPos = elapsed / duration;
  } else {
    disabled = true;
    elapsed = duration = sliderPos = 0;
  }
  $track_slider.slider("option", "disabled", disabled).slider("option", "value", sliderPos);
  $nowplaying_elapsed.html(formatTime(elapsed));
  $nowplaying_left.html(formatTime(duration));
}

function renderVolumeSlider() {
  if (user_is_volume_sliding) return;

  var enabled = player.volume != null;
  if (enabled) {
    $vol_slider.slider('option', 'value', player.volume);
  }
  $vol_slider.slider('option', 'disabled', !enabled);
}

function renderNowPlaying(){
  var ref$, track, track_display, state, toggle_icon, old_class, new_class;
  if ((track = (ref$ = player.currentItem) != null ? ref$.track : void 8) != null) {
    track_display = track.name + " - " + track.artistName;
    if (track.albumName.length) {
      track_display += " - " + track.albumName;
    }
    document.title = track_display + " - " + BASE_TITLE;
    if (track.name.indexOf("Groove Basin") === 0) {
      $("html").addClass('groovebasin');
    } else {
      $("html").removeClass('groovebasin');
    }
    if (track.name.indexOf("Never Gonna Give You Up") === 0 && track.artistName.indexOf("Rick Astley") === 0) {
      $("html").addClass('nggyu');
    } else {
      $("html").removeClass('nggyu');
    }
  } else {
    track_display = "&nbsp;";
    document.title = BASE_TITLE;
  }
  $track_display.html(track_display);
  state = (ref$ = player.state) != null ? ref$ : "stop";
  toggle_icon = {
    play: ['ui-icon-play', 'ui-icon-pause'],
    stop: ['ui-icon-pause', 'ui-icon-play'],
    pause: ['ui-icon-pause', 'ui-icon-play']
  };
  ref$ = toggle_icon[state], old_class = ref$[0], new_class = ref$[1];
  $nowplaying.find(".toggle span").removeClass(old_class).addClass(new_class);
  $track_slider.slider("option", "disabled", state === "stop");
  updateSliderPos();
  renderVolumeSlider();
}
function render(){
  var hide_main_err = load_status === LoadStatus.GoodToGo;
  $pl_window.toggle(hide_main_err);
  $left_window.toggle(hide_main_err);
  $nowplaying.toggle(hide_main_err);
  $main_err_msg.toggle(!hide_main_err);
  if (!hide_main_err) {
    document.title = BASE_TITLE;
    $main_err_msg_text.text(load_status);
    return;
  }
  renderPlaylist();
  renderPlaylistButtons();
  renderLibrary();
  renderNowPlaying();
  renderChat();
  renderSettings();
  handleResize();
}
function genericToggleExpansion($li, options){
  var $div = $li.find("> div");
  var $ul = $li.find("> ul");
  if ($div.attr('data-type') === options.top_level_type) {
    if (!$li.data('cached')) {
      $li.data('cached', true);
      $ul.html(options.template(options.context($div.attr('data-key'))));
      $ul.toggle();
      refreshSelection();
    }
  }
  $ul.toggle();
  var old_class = ICON_EXPANDED;
  var new_class = ICON_COLLAPSED;
  if ($ul.is(":visible")) {
    var tmp = old_class;
    old_class = new_class;
    new_class = tmp;
  }
  $div.find("div").removeClass(old_class).addClass(new_class);
}
function togglePlaylistExpansion($li){
  genericToggleExpansion($li, {
    top_level_type: 'stored_playlist',
    template: Handlebars.templates.playlist_items,
    context: function(key){
      return {
        itemList: player.stored_playlist_table[key].itemList
      };
    }
  });
}
function toggleLibraryExpansion($li){
  return genericToggleExpansion($li, {
    top_level_type: 'artist',
    template: Handlebars.templates.albums,
    context: function(key){
      return {
        albumList: player.searchResults.artistTable[key].albumList
      };
    }
  });
}

function confirmDelete(keysList) {
  var fileList = keysList.map(function(key) {
    return player.library.trackTable[key].file;
  });
  var listText = fileList.slice(0, 7).join("\n  ");
  if (fileList.length > 7) {
    listText += "\n  ...";
  }
  var songText = fileList.length === 1 ? "song" : "songs";
  return confirm("You are about to delete " + fileList.length + " " + songText + " permanently:\n\n  " + listText);
}

function handleDeletePressed(shift) {
  var keysList;
  if (selection.isLibrary()) {
    keysList = selection.toTrackKeys();
    if (!confirmDelete(keysList)) {
      return;
    }
    socket.emit('DeleteFromLibrary', keysList);
  } else if (selection.isPlaylist()) {
    if (shift) {
      keysList = [];
      for (var id in selection.ids.playlist) {
        keysList.push(player.playlist.itemTable[id].track.key);
      }
      if (!confirmDelete(keysList)) return;
      socket.emit('DeleteFromLibrary', keysList);
    }
    var index = player.playlist.itemTable[selection.cursor].index;
    player.removeIds((function(){
      var results$ = [];
      for (var id in selection.ids.playlist) {
        results$.push(id);
      }
      return results$;
    }()));
    if (index >= player.playlist.itemList.length) {
      index = player.playlist.itemList.length - 1;
    }
    if (index > -1) {
      selection.selectOnly('playlist', player.playlist.itemList[index].id);
    }
    refreshSelection();
  }
}
function togglePlayback(){
  if (player.state === 'play') {
    player.pause();
  } else {
    player.play();
  }
}

function setDynamicMode(value) {
  dynamicModeState.on = value;
  socket.emit('DynamicMode', dynamicModeState);
}

function toggleDynamicMode(){
  setDynamicMode(!dynamicModeState.on);
}

function nextRepeatState(){
  player.setRepeatMode((player.repeat + 1) % repeatModeNames.length);
}

var keyboard_handlers = (function(){
  var handlers;
  function upDownHandler(event){
    var default_index, dir, next_pos;
    if (event.which === 38) {
      default_index = player.playlist.itemList.length - 1;
      dir = -1;
    } else {
      default_index = 0;
      dir = 1;
    }
    if (event.ctrlKey) {
      if (selection.isPlaylist()) {
        player.shiftIds(selection.ids.playlist, dir);
      }
    } else {
      if (selection.isPlaylist()) {
        next_pos = player.playlist.itemTable[selection.cursor].index + dir;
        if (next_pos < 0 || next_pos >= player.playlist.itemList.length) {
          return;
        }
        selection.cursor = player.playlist.itemList[next_pos].id;
        if (!event.shiftKey) {
          selection.clear();
        }
        selection.ids.playlist[selection.cursor] = true;
      } else if (selection.isLibrary()) {
        next_pos = selection.getPos();
        if (dir > 0) {
          selection.incrementPos(next_pos);
        } else {
          prevLibPos(next_pos);
        }
        if (next_pos.artist == null) {
          return;
        }
        if (!event.shiftKey) {
          selection.clear();
        }
        if (next_pos.track != null) {
          selection.type = 'track';
          selection.cursor = next_pos.track.key;
        } else if (next_pos.album != null) {
          selection.type = 'album';
          selection.cursor = next_pos.album.key;
        } else {
          selection.type = 'artist';
          selection.cursor = next_pos.artist.key;
        }
        selection.ids[selection.type][selection.cursor] = true;
      } else {
        selection.selectOnly('playlist', player.playlist.itemList[default_index].id);
      }
      refreshSelection();
    }
    if (selection.isPlaylist()) {
      scrollPlaylistToSelection();
    }
    if (selection.isLibrary()) {
      scrollLibraryToSelection();
    }
  }
  function leftRightHandler(event){
    var dir, helpers, ref$, ids, table, $getDiv, selected_item, is_expanded_funcs, is_expanded, $li, cursor_pos;
    dir = event.which === 37 ? -1 : 1;
    if (selection.isLibrary()) {
      if (!(helpers = getSelHelpers())) {
        return;
      }
      ref$ = helpers[selection.type], ids = ref$[0], table = ref$[1], $getDiv = ref$[2];
      selected_item = table[selection.cursor];
      is_expanded_funcs = {
        artist: isArtistExpanded,
        album: isAlbumExpanded,
        track: function(){
          return true;
        }
      };
      is_expanded = is_expanded_funcs[selection.type](selected_item);
      $li = $getDiv(selection.cursor).closest("li");
      cursor_pos = selection.getPos();
      if (dir > 0) {
        if (!is_expanded) {
          toggleLibraryExpansion($li);
        }
      } else {
        if (is_expanded) {
          toggleLibraryExpansion($li);
        }
      }
    } else {
      if (event.ctrlKey) {
        if (dir > 0) {
          player.next();
        } else {
          player.prev();
        }
      } else if (event.shiftKey) {
        player.seek(getCurrentTrackPosition() + dir * player.duration * 0.10);
      } else {
        player.seek(getCurrentTrackPosition() + dir * 10);
      }
    }
  }
  return handlers = {
    13: {
      ctrl: false,
      alt: null,
      shift: null,
      handler: function(event){
        if (selection.isPlaylist()) {
          player.playId(selection.cursor);
        } else if (selection.isLibrary()) {
          queueSelection(event);
        }
        return false;
      }
    },
    27: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: function(){
        if (started_drag) {
          abortDrag();
          return;
        }
        if ($('#menu').get().length > 0) {
          removeContextMenu();
          return;
        }
        selection.fullClear();
        refreshSelection();
      }
    },
    32: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: togglePlayback
    },
    37: {
      ctrl: null,
      alt: false,
      shift: null,
      handler: leftRightHandler
    },
    38: {
      ctrl: null,
      alt: false,
      shift: null,
      handler: upDownHandler
    },
    39: {
      ctrl: null,
      alt: false,
      shift: null,
      handler: leftRightHandler
    },
    40: {
      ctrl: null,
      alt: false,
      shift: null,
      handler: upDownHandler
    },
    46: {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(event){
        handleDeletePressed(event.shiftKey);
      }
    },
    67: {
      ctrl: false,
      alt: false,
      shift: true,
      handler: function(){
        player.clear();
      }
    },
    68: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: toggleDynamicMode
    },
    72: {
      ctrl: false,
      alt: false,
      shift: true,
      handler: function(){
        player.shuffle();
      }
    },
    76: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: function(){
        clickTab('library');
      }
    },
    82: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: nextRepeatState
    },
    83: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: streaming.toggleStatus
    },
    84: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: function(){
        clickTab('chat');
        $chat_input.focus().select();
      }
    },
    85: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: function(){
        clickTab('upload');
        $upload_by_url.focus().select();
      }
    },
    187: {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(){
        player.setVolume(player.volume + 0.10);
      }
    },
    188: {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(){
        player.prev();
      }
    },
    189: {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(){
        player.setVolume(player.volume - 0.10);
      }
    },
    190: {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(){
        player.next();
      }
    },
    191: {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(event){
        if (event.shiftKey) {
          $(Handlebars.templates.shortcuts()).appendTo(document.body);
          $('#shortcuts').dialog({
            modal: true,
            title: "Keyboard Shortcuts",
            minWidth: 600,
            height: $document.height() - 40,
            close: function(){
              $('#shortcuts').remove();
            }
          });
        } else {
          clickTab('library');
          $lib_filter.focus().select();
        }
      }
    }
  };
})();
function removeContextMenu(){
  $('#menu').remove();
}
function isArtistExpanded(artist){
  var $li;
  $li = $("#lib-artist-" + toHtmlId(artist.key)).closest("li");
  if (!$li.data('cached')) {
    return false;
  }
  return $li.find("> ul").is(":visible");
}
function isAlbumExpanded(album){
  var $li;
  $li = $("#lib-album-" + toHtmlId(album.key)).closest("li");
  return $li.find("> ul").is(":visible");
}
function isStoredPlaylistExpanded(stored_playlist){
  var $li;
  $li = $("#stored-pl-pl-" + toHtmlId(stored_playlist.name)).closest("li");
  return $li.find("> ul").is(":visible");
}
function prevLibPos(lib_pos){
  if (lib_pos.track != null) {
    lib_pos.track = lib_pos.track.album.trackList[lib_pos.track.index - 1];
  } else if (lib_pos.album != null) {
    lib_pos.album = lib_pos.artist.albumList[lib_pos.album.index - 1];
    if (lib_pos.album != null && isAlbumExpanded(lib_pos.album)) {
      lib_pos.track = lib_pos.album.trackList[lib_pos.album.trackList.length - 1];
    }
  } else if (lib_pos.artist != null) {
    lib_pos.artist = player.searchResults.artistList[lib_pos.artist.index - 1];
    if (lib_pos.artist != null && isArtistExpanded(lib_pos.artist)) {
      lib_pos.album = lib_pos.artist.albumList[lib_pos.artist.albumList.length - 1];
      if (lib_pos.album != null && isAlbumExpanded(lib_pos.album)) {
        lib_pos.track = lib_pos.album.trackList[lib_pos.album.trackList.length - 1];
      }
    }
  }
}
function queueSelection(event){
  var keys = selection.toTrackKeys(event.altKey);
  if (event.shiftKey) {
    player.queueTracksNext(keys);
  } else {
    player.queueTracks(keys);
  }
  return false;
}

function sendAuth() {
  var pass = localState.authPassword;
  if (!pass) return;
  player.authenticate(pass, function(err){
    if (err) {
      localState.authPassword = null;
      saveLocalState();
    }
    renderSettings();
  });
}

function settingsAuthSave(){
  settings_ui.auth.show_edit = false;
  var $text_box = $('#auth-password');
  localState.authPassword = $text_box.val();
  saveLocalState();
  renderSettings();
  sendAuth();
}
function settingsAuthCancel(){
  settings_ui.auth.show_edit = false;
  renderSettings();
}
function performDrag(event, callbacks){
  abortDrag();
  var start_drag_x = event.pageX;
  var start_drag_y = event.pageY;
  abortDrag = function(){
    $document.off('mousemove', onDragMove).off('mouseup', onDragEnd);
    if (started_drag) {
      $playlist_items.find(".pl-item").removeClass('border-top').removeClass('border-bottom');
      started_drag = false;
    }
    abortDrag = function(){};
  };
  function onDragMove(event){
    var dist, result;
    if (!started_drag) {
      dist = Math.pow(event.pageX - start_drag_x, 2) + Math.pow(event.pageY - start_drag_y, 2);
      if (dist > 64) {
        started_drag = true;
      }
      if (!started_drag) {
        return;
      }
    }
    result = getDragPosition(event.pageX, event.pageY);
    $playlist_items.find(".pl-item").removeClass('border-top').removeClass('border-bottom');
    if (result.$next != null) {
      result.$next.addClass("border-top");
    } else if (result.$previous != null) {
      result.$previous.addClass("border-bottom");
    }
  }
  function onDragEnd(event){
    if (event.which !== 1) {
      return false;
    }
    if (started_drag) {
      callbacks.complete(getDragPosition(event.pageX, event.pageY), event);
    } else {
      callbacks.cancel();
    }
    abortDrag();
  }
  $document.on('mousemove', onDragMove).on('mouseup', onDragEnd);
  onDragMove(event);
}
function setUpGenericUi(){
  $document.on('mouseover', '.hoverable', function(event){
    $(this).addClass("ui-state-hover");
  });
  $document.on('mouseout', '.hoverable', function(event){
    $(this).removeClass("ui-state-hover");
  });
  $(".jquery-button").button();
  $document.on('mousedown', function(){
    removeContextMenu();
    selection.type = null;
    refreshSelection();
  });
  $document.on('keydown', function(event){
    var handler;
    if ((handler = keyboard_handlers[event.which]) != null && (handler.ctrl == null || handler.ctrl === event.ctrlKey) && (handler.alt == null || handler.alt === event.altKey) && (handler.shift == null || handler.shift === event.shiftKey)) {
      handler.handler(event);
      return false;
    }
    return true;
  });
}
function setUpPlaylistUi(){
  $pl_window.on('click', 'button.clear', function(){
    player.clear();
  });
  $pl_window.on('click', 'button.shuffle', function(){
    player.shuffle();
  });
  $pl_btn_repeat.on('click', function(){
    nextRepeatState();
  });
  $dynamicMode.on('click', function(){
    var value = $(this).prop("checked");
    setDynamicMode(value);
    return false;
  });
  $playlist_items.on('dblclick', '.pl-item', function(event){
    var trackId = $(this).attr('data-id');
    player.playId(trackId);
  });
  $playlist_items.on('contextmenu', function(event){
    return event.altKey;
  });
  $playlist_items.on('mousedown', '.pl-item', function(event){
    var trackId, skip_drag, old_pos, new_pos, i, context, $menu;
    if (started_drag) {
      return true;
    }
    $(document.activeElement).blur();
    if (event.which === 1) {
      event.preventDefault();
      removeContextMenu();
      trackId = $(this).attr('data-id');
      skip_drag = false;
      if (!selection.isPlaylist()) {
        selection.selectOnly('playlist', trackId);
      } else if (event.ctrlKey || event.shiftKey) {
        skip_drag = true;
        if (event.shiftKey && !event.ctrlKey) {
          selection.clear();
        }
        if (event.shiftKey) {
          old_pos = selection.cursor != null ? player.playlist.itemTable[selection.cursor].index : 0;
          new_pos = player.playlist.itemTable[trackId].index;
          for (i = old_pos; i <= new_pos; ++i) {
            selection.ids.playlist[player.playlist.itemList[i].id] = true;
          }
        } else if (event.ctrlKey) {
          if (selection.ids.playlist[trackId] != null) {
            delete selection.ids.playlist[trackId];
          } else {
            selection.ids.playlist[trackId] = true;
          }
          selection.cursor = trackId;
        }
      } else if (selection.ids.playlist[trackId] == null) {
        selection.selectOnly('playlist', trackId);
      }
      refreshSelection();
      if (!skip_drag) {
        return performDrag(event, {
          complete: function(result, event){
            var delta, id;
            delta = {
              top: 0,
              bottom: 1
            };
            player.moveIds((function(){
              var results$ = [];
              for (var id in selection.ids.playlist) {
                results$.push(id);
              }
              return results$;
            }()), result.previous_key, result.next_key);
          },
          cancel: function(){
            selection.selectOnly('playlist', trackId);
            refreshSelection();
          }
        });
      }
    } else if (event.which === 3) {
      if (event.altKey) {
        return;
      }
      event.preventDefault();
      removeContextMenu();
      trackId = $(this).attr('data-id');
      if (!selection.isPlaylist() || selection.ids.playlist[trackId] == null) {
        selection.selectOnly('playlist', trackId);
        refreshSelection();
      }
      context = {
        downloadEnabled: true,
        permissions: permissions
      };
      if (selection.isMulti()) {
        context.download_multi = true;
      } else {
        context.item = player.playlist.itemTable[trackId];
      }
      $(Handlebars.templates.playlist_menu(context)).appendTo(document.body);
      $menu = $('#menu');
      $menu.offset({
        left: event.pageX + 1,
        top: event.pageY + 1
      });
      $menu.on('mousedown', function(){
        return false;
      });
      $menu.on('click', '.remove', function(){
        handleDeletePressed(false);
        removeContextMenu();
        return false;
      });
      $menu.on('click', '.download', function(){
        removeContextMenu();
        return true;
      });
      $menu.on('click', '.download-multi', function(){
        removeContextMenu();
        downloadKeys(selection.toTrackKeys());
        return false;
      });
      return $menu.on('click', '.delete', function(){
        handleDeletePressed(true);
        removeContextMenu();
        return false;
      });
    }
  });
  $playlist_items.on('mousedown', function(){
    return false;
  });
}
function setUpChatUi(){
  $chat_user_id_span.on('click', function(event){
    $chat_input.attr("disabled", "disabled");
    chat_name_input_visible = true;
    $chat_name_input.show().val("").focus().select();
    renderChat();
  });
  $chat_name_input.on('keydown', function(event){
    var done;
    event.stopPropagation();
    if (event.which === 27) {
      done = true;
    } else if (event.which === 13) {
      done = true;
      setUserName($(event.target).val());
    }
    if (done) {
      chat_name_input_visible = false;
      $chat_name_input.hide();
      $chat_input.removeAttr("disabled").focus().select();
      renderChat();
      return false;
    }
  });
  $chat_input.on('keydown', function(event){
    var message, newUserName, NICK;
    event.stopPropagation();
    if (event.which === 27) {
      $(event.target).blur();
      return false;
    } else if (event.which === 13) {
      message = $(event.target).val().trim();
      setTimeout(function(){
        $(event.target).val("");
      }, 0);
      if (message === "") {
        return false;
      }
      if (!haveUserName()) {
        newUserName = message;
      }
      NICK = "/nick ";
      if (message.substr(0, NICK.length) === NICK) {
        newUserName = message.substring(0, NICK.length);
      }
      if (newUserName != null) {
        setUserName(newUserName);
        return false;
      }
      socket.emit('Chat', message);
      return false;
    }
  });
}
function updateSliderUi(value){
  var percent;
  percent = value * 100;
  $track_slider.css('background-size', percent + "% 100%");
}
function setUpNowPlayingUi(){
  var actions, cls, action;
  actions = {
    toggle: togglePlayback,
    prev: function(){
      player.prev();
    },
    next: function(){
      player.next();
    },
    stop: function(){
      player.stop();
    }
  };
  for (cls in actions) {
    action = actions[cls];
    (fn$.call(this, cls, action));
  }
  $track_slider.slider({
    step: 0.0001,
    min: 0,
    max: 1,
    change: function(event, ui){
      updateSliderUi(ui.value);
      if (event.originalEvent == null) {
        return;
      }
      player.seek(ui.value * player.duration);
    },
    slide: function(event, ui){
      updateSliderUi(ui.value);
      $nowplaying_elapsed.html(formatTime(ui.value * player.duration));
    },
    start: function(event, ui){
      user_is_seeking = true;
    },
    stop: function(event, ui){
      user_is_seeking = false;
    }
  });
  function setVol(event, ui){
    if (event.originalEvent == null) {
      return;
    }
    player.setVolume(ui.value);
  }
  $vol_slider.slider({
    step: 0.01,
    min: 0,
    max: 1,
    change: setVol,
    start: function(event, ui){
      user_is_volume_sliding = true;
    },
    stop: function(event, ui){
      user_is_volume_sliding = false;
    }
  });
  setInterval(updateSliderPos, 100);
  function fn$(cls, action){
    $nowplaying.on('mousedown', "li." + cls, function(event){
      action();
      return false;
    });
  }
}
function setUpTabsUi(){
  var tabs, i, len$, tab;
  $tabs.on('mouseover', 'li', function(event){
    $(this).addClass('ui-state-hover');
  });
  $tabs.on('mouseout', 'li', function(event){
    $(this).removeClass('ui-state-hover');
  });
  tabs = ['library', 'stored-playlists', 'upload', 'chat', 'settings'];
  function tabSelector(tab_name){
    return "li." + tab_name + "-tab";
  }
  function $pane(tab_name){
    return $("#" + tab_name + "-pane");
  }
  function $tab(tab_name){
    return $tabs.find(tabSelector(tab_name));
  }
  function unselectTabs(){
    var i, ref$, len$, tab;
    $tabs.find('li').removeClass('ui-state-active');
    for (i = 0, len$ = (ref$ = tabs).length; i < len$; ++i) {
      tab = ref$[i];
      $pane(tab).hide();
    }
  }
  clickTab = function(name){
    unselectTabs();
    $tab(name).addClass('ui-state-active');
    $pane(name).show();
    handleResize();
  };
  for (i = 0, len$ = tabs.length; i < len$; ++i) {
    tab = tabs[i];
    (fn$.call(this, tab));
  }
  function fn$(tab){
    $tabs.on('click', tabSelector(tab), function(event){
      clickTab(tab);
    });
  }
}
function setUpUploadUi(){
  var uploader;
  uploader = new qq.FileUploader({
    element: document.getElementById("upload-widget"),
    action: '/upload',
    encoding: 'multipart'
  });
  $upload_by_url.on('keydown', function(event){
    var url;
    event.stopPropagation();
    if (event.which === 27) {
      $upload_by_url.val("").blur();
    } else if (event.which === 13) {
      url = $upload_by_url.val();
      $upload_by_url.val("").blur();
      socket.emit('ImportTrackUrl', url);
    }
  });
}
function setUpSettingsUi(){
  $settings.on('click', '.signout', function(event){
    localState.lastfm.username = null;
    localState.lastfm.session_key = null;
    localState.lastfm.scrobbling_on = false;
    saveLocalState();
    renderSettings();
    return false;
  });
  $settings.on('click', '#toggle-scrobble', function(event){
    var value, msg, params;
    value = $(this).prop("checked");
    if (value) {
      msg = 'LastfmScrobblersAdd';
      localState.lastfm.scrobbling_on = true;
    } else {
      msg = 'LastfmScrobblersRemove';
      localState.lastfm.scrobbling_on = false;
    }
    saveLocalState();
    params = {
      username: localState.lastfm.username,
      session_key: localState.lastfm.session_key
    };
    socket.emit(msg, JSON.stringify(params));
    renderSettings();
    return false;
  });
  $settings.on('click', '.auth-edit', function(event){
    var $text_box, ref$;
    settings_ui.auth.show_edit = true;
    renderSettings();
    $text_box = $('#auth-password');
    $text_box.focus().val((ref$ = localState.authPassword) != null ? ref$ : "").select();
  });
  $settings.on('click', '.auth-clear', function(event){
    localState.authPassword = null;
    saveLocalState();
    settings_ui.auth.password = "";
    renderSettings();
  });
  $settings.on('click', '.auth-save', function(event){
    settingsAuthSave();
  });
  $settings.on('click', '.auth-cancel', function(event){
    settingsAuthCancel();
  });
  $settings.on('keydown', '#auth-password', function(event){
    var $text_box;
    $text_box = $(this);
    event.stopPropagation();
    settings_ui.auth.password = $text_box.val();
    if (event.which === 27) {
      settingsAuthCancel();
    } else if (event.which === 13) {
      settingsAuthSave();
    }
  });
  $settings.on('keyup', '#auth-password', function(event){
    settings_ui.auth.password = $(this).val();
  });
}
function setUpLibraryUi(){
  $lib_filter.on('keydown', function(event){
    var keys, i, ref$, len$, artist, j$, ref1$, len1$, album, k$, ref2$, len2$, track;
    event.stopPropagation();
    switch (event.which) {
    case 27:
      if ($(event.target).val().length === 0) {
        $(event.target).blur();
      } else {
        setTimeout(function(){
          $(event.target).val("");
          player.search("");
        }, 0);
      }
      return false;
    case 13:
      keys = [];
      for (i = 0, len$ = (ref$ = player.searchResults.artistList).length; i < len$; ++i) {
        artist = ref$[i];
        for (j$ = 0, len1$ = (ref1$ = artist.albumList).length; j$ < len1$; ++j$) {
          album = ref1$[j$];
          for (k$ = 0, len2$ = (ref2$ = album.trackList).length; k$ < len2$; ++k$) {
            track = ref2$[k$];
            keys.push(track.key);
          }
        }
      }
      if (event.altKey) shuffle(keys);
      if (keys.length > 2000) {
        if (!confirm("You are about to queue " + keys.length + " songs.")) {
          return false;
        }
      }
      if (event.shiftKey) {
        player.queueTracksNext(keys);
      } else {
        player.queueTracks(keys);
      }
      return false;
    case 40:
      selection.selectOnly('artist', player.searchResults.artistList[0].key);
      refreshSelection();
      $lib_filter.blur();
      return false;
    case 38:
      selection.selectOnly('artist', player.searchResults.artistList[player.searchResults.artistList.length - 1].key);
      refreshSelection();
      $lib_filter.blur();
      return false;
    }
  });
  $lib_filter.on('keyup', function(event){
    player.search($(event.target).val());
  });
  genericTreeUi($library, {
    toggleExpansion: toggleLibraryExpansion,
    isSelectionOwner: function(){
      return selection.isLibrary();
    }
  });
}
function setUpStoredPlaylistsUi(){
  genericTreeUi($stored_playlists, {
    toggleExpansion: togglePlaylistExpansion,
    isSelectionOwner: function(){
      return selection.isStoredPlaylist();
    }
  });
}
function genericTreeUi($elem, options){
  $elem.on('mousedown', 'div.expandable > div.ui-icon', function(event){
    options.toggleExpansion($(this).closest('li'));
    return false;
  });
  $elem.on('dblclick', 'div.expandable > div.ui-icon', function(){
    return false;
  });
  $elem.on('dblclick', 'div.clickable', queueSelection);
  $elem.on('contextmenu', function(event){
    return event.altKey;
  });
  $elem.on('mousedown', '.clickable', function(event){
    var $this, type, key;
    $(document.activeElement).blur();
    $this = $(this);
    type = $this.attr('data-type');
    key = $this.attr('data-key');
    if (event.which === 1) {
      leftMouseDown(event);
    } else if (event.which === 3) {
      if (event.altKey) {
        return;
      }
      rightMouseDown(event);
    }
    function leftMouseDown(event){
      var skip_drag, old_pos, new_pos, new_arr, old_arr, ref$;
      event.preventDefault();
      removeContextMenu();
      skip_drag = false;
      if (!options.isSelectionOwner()) {
        selection.selectOnly(type, key);
      } else if (event.ctrlKey || event.shiftKey) {
        skip_drag = true;
        if (event.shiftKey && !event.ctrlKey) {
          selection.clear();
        }
        if (event.shiftKey) {
          old_pos = selection.getPos(selection.type, selection.cursor);
          new_pos = selection.getPos(type, key);
          new_arr = selection.posToArr(new_pos);
          old_arr = selection.posToArr(old_pos);
          if (compareArrays(old_arr, new_arr) > 0) {
            ref$ = [new_pos, old_pos], old_pos = ref$[0], new_pos = ref$[1];
          }
          while (selection.posInBounds(old_pos)) {
            selection.selectPos(old_pos);
            if (selection.posEqual(old_pos, new_pos)) {
              break;
            }
            selection.incrementPos(old_pos);
          }
        } else if (event.ctrlKey) {
          if (selection.ids[type][key] != null) {
            delete selection.ids[type][key];
          } else {
            selection.ids[type][key] = true;
          }
          selection.cursor = key;
          selection.type = type;
        }
      } else if (selection.ids[type][key] == null) {
        selection.selectOnly(type, key);
      }
      refreshSelection();
      if (!skip_drag) {
        performDrag(event, {
          complete: function(result, event){
            var delta = {
              top: 0,
              bottom: 1
            };
            var keys = selection.toTrackKeys(event.altKey);
            player.queueTracks(keys, result.previous_key, result.next_key);
          },
          cancel: function(){
            selection.selectOnly(type, key);
            refreshSelection();
          }
        });
      }
    }
    function rightMouseDown(event){
      var context, $menu;
      event.preventDefault();
      removeContextMenu();
      if (!options.isSelectionOwner() || selection.ids[type][key] == null) {
        selection.selectOnly(type, key);
        refreshSelection();
      }
      context = {
        downloadEnabled: true,
        permissions: permissions
      };
      if (selection.isMulti()) {
        context.download_multi = true;
      } else {
        if (type === 'track') {
          context.track = player.searchResults.trackTable[key];
        } else if (type === 'stored_playlist_item') {
          context.track = player.stored_playlist_item_table[key].track;
        } else {
          context.download_multi = true;
        }
      }
      $(Handlebars.templates.library_menu(context)).appendTo(document.body);
      $menu = $('#menu');
      $menu.offset({
        left: event.pageX + 1,
        top: event.pageY + 1
      });
      $menu.on('mousedown', function(){
        return false;
      });
      $menu.on('click', '.queue', function(){
        player.queueTracks(selection.toTrackKeys());
        removeContextMenu();
        return false;
      });
      $menu.on('click', '.queue-next', function(){
        player.queueTracksNext(selection.toTrackKeys());
        removeContextMenu();
        return false;
      });
      $menu.on('click', '.queue-random', function(){
        player.queueTracks(selection.toTrackKeys(true));
        removeContextMenu();
        return false;
      });
      $menu.on('click', '.queue-next-random', function(){
        player.queueTracksNext(selection.toTrackKeys(true));
        removeContextMenu();
        return false;
      });
      $menu.on('click', '.download', function(){
        removeContextMenu();
        return true;
      });
      $menu.on('click', '.download-multi', function(){
        removeContextMenu();
        downloadKeys(selection.toTrackKeys());
        return false;
      });
      $menu.on('click', '.delete', function(){
        handleDeletePressed(true);
        removeContextMenu();
        return false;
      });
    }
  });
  $elem.on('mousedown', function(){
    return false;
  });
}
function setUpUi(){
  setUpGenericUi();
  setUpPlaylistUi();
  setUpLibraryUi();
  setUpStoredPlaylistsUi();
  setUpChatUi();
  setUpNowPlayingUi();
  setUpTabsUi();
  setUpUploadUi();
  setUpSettingsUi();
}
function initHandlebars(){
  Handlebars.registerHelper('time', formatTime);
  Handlebars.registerHelper('artistid', function(s){
    return "lib-artist-" + toHtmlId(s);
  });
  Handlebars.registerHelper('albumid', function(s){
    return "lib-album-" + toHtmlId(s);
  });
  Handlebars.registerHelper('trackid', function(s){
    return "lib-track-" + toHtmlId(s);
  });
  Handlebars.registerHelper('storedplaylistid', function(s){
    return "stored-pl-pl-" + toHtmlId(s);
  });
  Handlebars.registerHelper('storedplaylistitemid', function(s){
    return "stored-pl-item-" + toHtmlId(s);
  });
}
function handleResize(){
  var second_layer_top, tab_contents_height;
  $nowplaying.width(MARGIN);
  $pl_window.height(MARGIN);
  $left_window.height(MARGIN);
  $library.height(MARGIN);
  $upload.height(MARGIN);
  $stored_playlists.height(MARGIN);
  $chatList.height(MARGIN);
  $playlist_items.height(MARGIN);
  $nowplaying.width($document.width() - MARGIN * 2);
  second_layer_top = $nowplaying.offset().top + $nowplaying.height() + MARGIN;
  $left_window.offset({
    left: MARGIN,
    top: second_layer_top
  });
  $pl_window.offset({
    left: $left_window.offset().left + $left_window.width() + MARGIN,
    top: second_layer_top
  });
  $pl_window.width($window.width() - $pl_window.offset().left - MARGIN);
  $left_window.height($window.height() - $left_window.offset().top);
  $pl_window.height($left_window.height() - MARGIN);
  tab_contents_height = $left_window.height() - $tabs.height() - MARGIN;
  $library.height(tab_contents_height - $lib_header.height());
  $upload.height(tab_contents_height);
  $stored_playlists.height(tab_contents_height);
  $chatList.height(tab_contents_height - $chatUserList.height() - $chat_input_pane.height());
  $playlist_items.height($pl_window.height() - $pl_header.position().top - $pl_header.height());
}
function refreshPage(){
  location.href = location.protocol + "//" + location.host + "/";
}
window.WEB_SOCKET_SWF_LOCATION = "/vendor/socket.io/WebSocketMain.swf";
$document.ready(function(){
  var ref$, token;
  loadLocalState();
  socket = io.connect();
  var queryObj = querystring.parse(location.search.substring(1));
  if (queryObj.token) {
    socket.emit('LastfmGetSession', queryObj.token);
    socket.on('LastfmGetSessionSuccess', function(data){
      var params;
      params = JSON.parse(data);
      localState.lastfm.username = params.session.name;
      localState.lastfm.session_key = params.session.key;
      localState.lastfm.scrobbling_on = false;
      saveLocalState();
      refreshPage();
    });
    socket.on('LastfmGetSessionError', function(data){
      var params;
      params = JSON.parse(data);
      alert("Error authenticating: " + params.message);
      refreshPage();
    });
    return;
  }
  socket.on('Identify', function(data){
    myUserId = data.toString();
    localState.myUserIds[myUserId] = 1;
    saveLocalState();
    var userName = localState.userName;
    if (userName) setUserName(userName);
  });
  socket.on('Permissions', function(data){
    permissions = JSON.parse(data.toString());
    renderSettings();
  });
  socket.on('Chat', function(data) {
    chatState = data;
    renderChat();
  });
  socket.on('volumeUpdate', function(vol) {
    player.volume = vol;
    renderVolumeSlider();
  });
  socket.on('DynamicMode', function(data) {
    dynamicModeState = data;
    renderPlaylistButtons();
    renderPlaylist();
  });
  player = new PlayerClient(socket);
  player.on('libraryupdate', renderLibrary);
  player.on('playlistupdate', renderPlaylist);
  player.on('storedplaylistupdate', renderStoredPlaylists);
  player.on('statusupdate', function(){
    renderNowPlaying();
    renderPlaylistButtons();
    labelPlaylistItems();
  });
  socket.on('connect', function(){
    sendAuth();
    load_status = LoadStatus.GoodToGo;
    render();
  });
  socket.on('disconnect', function(){
    load_status = LoadStatus.NoServer;
    render();
  });
  setUpUi();
  initHandlebars();
  streaming.init(player, socket);
  render();
  $window.resize(handleResize);
  window._debug_player = player;
});

function compareArrays(arr1, arr2) {
  for (var i1 = 0; i1 < arr1.length; i1 += 1) {
    var val1 = arr1[i1];
    var val2 = arr2[i1];
    var diff = (val1 != null ? val1 : -1) - (val2 != null ? val2 : -1);
    if (diff !== 0) return diff;
  }
  return 0;
}

function formatTime(seconds) {
  seconds = Math.floor(seconds);
  var minutes = Math.floor(seconds / 60);
  seconds -= minutes * 60;
  var hours = Math.floor(minutes / 60);
  minutes -= hours * 60;
  if (hours !== 0) {
    return hours + ":" + zfill(minutes, 2) + ":" + zfill(seconds, 2);
  } else {
    return minutes + ":" + zfill(seconds, 2);
  }
}

var badCharRe = new RegExp('[^a-zA-Z0-9-]', 'gm');
function toHtmlId(string) {
  return string.replace(badCharRe, function(c) {
    return "_" + c.charCodeAt(0) + "_";
  });
}
