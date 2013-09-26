var $ = window.$;
var Handlebars = window.Handlebars;
var qq = window.qq;
var io = window.io;

var shuffle = require('mess');
var querystring = require('querystring');
var zfill = require('zfill');
var PlayerClient = require('./playerclient');
var streaming = require('./streaming');



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
          val.track = mpd.search_results.track_table[key];
          val.album = val.track.album;
          val.artist = val.album.artist;
          break;
        case 'album':
          val.album = mpd.search_results.album_table[key];
          val.artist = val.album.artist;
          break;
        case 'artist':
          val.artist = mpd.search_results.artist_table[key];
          break;
        }
      } else {
        val.artist = mpd.search_results.artists[0];
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
          val.stored_playlist_item = mpd.stored_playlist_item_table[key];
          val.stored_playlist = val.stored_playlist_item.playlist;
          break;
        case 'stored_playlist':
          val.stored_playlist = mpd.stored_playlist_table[key];
          break;
        }
      } else {
        val.stored_playlist = mpd.stored_playlists[0];
      }
      return val;
    } else {
      throw new Error("NothingSelected");
    }
  },
  posToArr: function(pos){
    var ref$;
    if (pos.type === 'library') {
      return [(ref$ = pos.artist) != null ? ref$.pos : void 8, (ref$ = pos.album) != null ? ref$.pos : void 8, (ref$ = pos.track) != null ? ref$.pos : void 8];
    } else if (pos.type === 'stored_playlist') {
      return [(ref$ = pos.stored_playlist) != null ? ref$.pos : void 8, (ref$ = pos.stored_playlist_item) != null ? ref$.pos : void 8];
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
        selection.ids.track[pos.track.file] = true;
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
        pos.track = pos.track.album.tracks[pos.track.pos + 1];
        if (pos.track == null) {
          pos.album = pos.artist.albums[pos.album.pos + 1];
          if (pos.album == null) {
            pos.artist = mpd.search_results.artists[pos.artist.pos + 1];
          }
        }
      } else if (pos.album != null) {
        if (isAlbumExpanded(pos.album)) {
          pos.track = pos.album.tracks[0];
        } else {
          pos.artist = mpd.search_results.artists[pos.artist.pos + 1];
          pos.album = null;
        }
      } else if (pos.artist != null) {
        if (isArtistExpanded(pos.artist)) {
          pos.album = pos.artist.albums[0];
        } else {
          pos.artist = mpd.search_results.artists[pos.artist.pos + 1];
        }
      }
    } else if (pos.type === 'stored_playlist') {
      if (pos.stored_playlist_item != null) {
        pos.stored_playlist_item = pos.stored_playlist_item.playlist.item_list[pos.stored_playlist_item.pos + 1];
        if (pos.stored_playlist_item == null) {
          pos.stored_playlist = mpd.stored_playlists[pos.stored_playlist.pos + 1];
        }
      } else if (pos.stored_playlist != null) {
        if (isStoredPlaylistExpanded(pos.stored_playlist)) {
          pos.stored_playlist_item = pos.stored_playlist.item_list[0];
          if (pos.stored_playlist_item == null) {
            pos.stored_playlist = mpd.stored_playlists[pos.stored_playlist.pos + 1];
          }
        } else {
          pos.stored_playlist = mpd.stored_playlists[pos.stored_playlist.pos + 1];
        }
      }
    } else {
      throw new Error("NothingSelected");
    }
  },
  decrementPos: function(pos){},
  toFiles: function(random){
    var this$ = this;
    if (random == null) random = false;
    if (this.isLibrary()) {
      return libraryToFiles();
    } else if (this.isPlaylist()) {
      return playlistToFiles();
    } else if (this.isStoredPlaylist()) {
      return storedPlaylistToFiles();
    } else {
      throw new Error("NothingSelected");
    }
    function libraryToFiles(){
      var track_set, key, file;
      track_set = {};
      function selRenderArtist(artist){
        var i$, ref$, len$, album;
        for (i$ = 0, len$ = (ref$ = artist.albums).length; i$ < len$; ++i$) {
          album = ref$[i$];
          selRenderAlbum(album);
        }
      }
      function selRenderAlbum(album){
        var i$, ref$, len$, track;
        for (i$ = 0, len$ = (ref$ = album.tracks).length; i$ < len$; ++i$) {
          track = ref$[i$];
          selRenderTrack(track);
        }
      }
      function selRenderTrack(track){
        track_set[track.file] = this$.posToArr(getTrackSelPos(track));
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
        selRenderArtist(mpd.search_results.artist_table[key]);
      }
      for (key in selection.ids.album) {
        selRenderAlbum(mpd.search_results.album_table[key]);
      }
      for (file in selection.ids.track) {
        selRenderTrack(mpd.search_results.track_table[file]);
      }
      return trackSetToFiles(track_set);
    }
    function playlistToFiles(){
      var res$, key, files;
      res$ = [];
      for (key in selection.ids.playlist) {
        res$.push(mpd.playlist.item_table[key].track.file);
      }
      files = res$;
      if (random) shuffle(files);
      return files;
    }
    function storedPlaylistToFiles(){
      var track_set, key;
      track_set = {};
      function renderPlaylist(playlist){
        var i$, ref$, len$, item;
        for (i$ = 0, len$ = (ref$ = playlist.item_list).length; i$ < len$; ++i$) {
          item = ref$[i$];
          renderPlaylistItem(item);
        }
      }
      function renderPlaylistItem(item){
        track_set[item.track.file] = this$.posToArr(getItemSelPos(item));
      }
      function getItemSelPos(item){
        return {
          type: 'stored_playlist',
          stored_playlist: item.playlist,
          stored_playlist_item: item
        };
      }
      for (key in selection.ids.stored_playlist) {
        renderPlaylist(mpd.stored_playlist_table[key]);
      }
      for (key in selection.ids.stored_playlist_item) {
        renderPlaylistItem(mpd.stored_playlist_item_table[key]);
      }
      return trackSetToFiles(track_set);
    }
    function trackSetToFiles(track_set){
      var res$, file, files, pos, track_arr, i$, len$, track, results$ = [];
      if (random) {
        res$ = [];
        for (file in track_set) {
          res$.push(file);
        }
        files = res$;
        shuffle(files);
        return files;
      } else {
        res$ = [];
        for (file in track_set) {
          pos = track_set[file];
          res$.push({
            file: file,
            pos: pos
          });
        }
        track_arr = res$;
        track_arr.sort(function(a, b){
          return compareArrays(a.pos, b.pos);
        });
        for (i$ = 0, len$ = track_arr.length; i$ < len$; ++i$) {
          track = track_arr[i$];
          results$.push(track.file);
        }
        return results$;
      }
    }
    return trackSetToFiles;
  }
};
var BASE_TITLE = document.title;
var MARGIN = 10;
var AUTO_EXPAND_LIMIT = 20;
var ICON_COLLAPSED = 'ui-icon-triangle-1-e';
var ICON_EXPANDED = 'ui-icon-triangle-1-se';
var server_status = null;
var permissions = {};
var socket = null;
var mpd = null;
var user_is_seeking = false;
var user_is_volume_sliding = false;
var started_drag = false;
var abortDrag = function(){};
var clickTab = null;
var my_user_id = null;
var chat_name_input_visible = false;
var LoadStatus = {
  Init: 'Loading...',
  NoServer: 'Server is down.',
  GoodToGo: '[good to go]'
};
var repeatModeNames = ["Off", "All", "One"];
var load_status = LoadStatus.Init;
var settings_ui = {
  auth: {
    show_edit: false,
    password: ""
  }
};
var local_state = {
  my_user_ids: {},
  user_name: null,
  lastfm: {
    username: null,
    session_key: null,
    scrobbling_on: false
  },
  auth_password: null
};
var $document = $(document);
var $window = $(window);
var $pl_window = $('#playlist-window');
var $left_window = $('#left-window');
var $playlist_items = $('#playlist-items');
var $dynamic_mode = $('#dynamic-mode');
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
var $chat_user_list = $('#chat-user-list');
var $chat_list = $('#chat-list');
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
  localStorage.state = JSON.stringify(local_state);
}
function loadLocalState(){
  var state_string;
  if ((state_string = localStorage.state) != null) {
    local_state = JSON.parse(state_string);
  }
}
function haveUserName(){
  return (server_status != null ? server_status.user_names[my_user_id] : void 8) != null;
}
function getUserName(){
  return userIdToUserName(my_user_id);
}
function userIdToUserName(user_id){
  if (server_status == null) return user_id;
  var user_name = server_status.user_names[user_id];
  return user_name != null ? user_name : user_id;
}
function setUserName(new_name){
  new_name = $.trim(new_name);
  local_state.user_name = new_name;
  saveLocalState();
  socket.emit('SetUserName', new_name);
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
function downloadFiles(files){
  var $form = $(document.createElement('form'));
  $form.attr('action', "/download/custom");
  $form.attr('method', "post");
  $form.attr('target', "_blank");
  for (var i = 0; i < files.length; i += 1) {
    var file = files[i];
    var $input = $(document.createElement('input'));
    $input.attr('type', 'hidden');
    $input.attr('name', 'file');
    $input.attr('value', file);
    $form.append($input);
  }
  $form.submit();
}
function getDragPosition(x, y){
  var result, i$, ref$, len$, item, $item, middle, track;
  result = {};
  for (i$ = 0, len$ = (ref$ = $playlist_items.find(".pl-item").get()).length; i$ < len$; ++i$) {
    item = ref$[i$];
    $item = $(item);
    middle = $item.offset().top + $item.height() / 2;
    track = mpd.playlist.item_table[$item.attr('data-id')];
    if (middle < y) {
      if (result.previous_key == null || track.sort_key > result.previous_key) {
        result.$previous = $item;
        result.previous_key = track.sort_key;
      }
    } else {
      if (result.next_key == null || track.sort_key < result.next_key) {
        result.$next = $item;
        result.next_key = track.sort_key;
      }
    }
  }
  return result;
}
function renderSettings(){
  var api_key, context;
  if ((api_key = server_status != null ? server_status.lastfm_api_key : void 8) == null) {
    return;
  }
  context = {
    lastfm: {
      auth_url: "http://www.last.fm/api/auth/?api_key=" +
        encodeURIComponent(api_key) + "&cb=" +
        encodeURIComponent(location.protocol + "//" + location.host + "/"),
      username: local_state.lastfm.username,
      session_key: local_state.lastfm.session_key,
      scrobbling_on: local_state.lastfm.scrobbling_on
    },
    auth: {
      password: local_state.auth_password,
      show_edit: local_state.auth_password == null || settings_ui.auth.show_edit,
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
  $chat_list.scrollTop(1000000);
}
function renderChat(){
  var chat_status_text, users, user_objects, i$, len$, user_id, user_name, class_, ref$, chat_object;
  chat_status_text = "";
  if ((users = server_status != null ? server_status.users : void 8) != null) {
    user_objects = [];
    for (i$ = 0, len$ = users.length; i$ < len$; ++i$) {
      user_id = users[i$];
      user_name = userIdToUserName(user_id);
      if (user_name === '[server]') {
        continue;
      }
      class_ = user_id === my_user_id ? "chat-user-self" : "chat-user";
      user_objects.push({
        user_name: user_name,
        "class": class_
      });
    }
    if (user_objects.length > 1) {
      chat_status_text = " (" + user_objects.length + ")";
    }
    $chat_user_list.html(Handlebars.templates.chat_user_list({
      users: user_objects
    }));
    for (i$ = 0, len$ = (ref$ = server_status.chats).length; i$ < len$; ++i$) {
      chat_object = ref$[i$];
      chat_object["class"] = local_state.my_user_ids[chat_object.user_id] != null ? "chat-user-self" : "chat-user";
      chat_object.user_name = userIdToUserName(chat_object.user_id);
    }
    $chat_list.html(Handlebars.templates.chat_list({
      chats: server_status.chats
    }));
    scrollChatWindowToBottom();
    $chat_user_id_span.text(chat_name_input_visible ? "" : getUserName() + ": ");
  }
  $chat_tab.find("span").text("Chat" + chat_status_text);
}
function renderPlaylistButtons(){
  $dynamic_mode
    .prop("checked", server_status != null && server_status.dynamic_mode ? true : false)
    .button("option", "disabled", !(server_status != null && server_status.dynamic_mode_enabled))
    .button("refresh");
  var repeatModeName = repeatModeNames[mpd.status.repeat];
  $pl_btn_repeat
    .button("option", "label", "Repeat: " + repeatModeName)
    .prop("checked", mpd.status.repeat !== PlayerClient.REPEAT_OFF)
    .button("refresh");
  $upload_tab.removeClass("ui-state-disabled");
  if (!(server_status != null && server_status.upload_enabled)) {
    $upload_tab.addClass("ui-state-disabled");
  }
}
function renderPlaylist(){
  var context, scroll_top;
  context = {
    playlist: mpd.playlist.item_list,
    server_status: server_status
  };
  scroll_top = $playlist_items.scrollTop();
  $playlist_items.html(Handlebars.templates.playlist(context));
  refreshSelection();
  labelPlaylistItems();
  $playlist_items.scrollTop(scroll_top);
}
function renderStoredPlaylists(){
  var context, scroll_top;
  context = {
    stored_playlists: mpd.stored_playlists
  };
  scroll_top = $stored_playlists.scrollTop();
  $stored_playlists.html(Handlebars.templates.stored_playlists(context));
  $stored_playlists.scrollTop(scroll_top);
  refreshSelection();
}
function labelPlaylistItems(){
  var cur_item, pos, to$, ref$, id, i$, len$, item;
  cur_item = mpd.status.current_item;
  $playlist_items.find(".pl-item").removeClass('current').removeClass('old');
  if (cur_item != null && (server_status != null && server_status.dynamic_mode)) {
    for (pos = 0, to$ = cur_item.pos; pos < to$; ++pos) {
      if ((id = (ref$ = mpd.playlist.item_list[pos]) != null ? ref$.id : void 8) != null) {
        $("#playlist-track-" + id).addClass('old');
      }
    }
  }
  for (i$ = 0, len$ = (ref$ = mpd.playlist.item_list).length; i$ < len$; ++i$) {
    item = ref$[i$];
    if (item.is_random) {
      $("#playlist-track-" + item.id).addClass('random');
    }
  }
  if (cur_item != null) {
    $("#playlist-track-" + cur_item.id).addClass('current');
  }
}
function getSelHelpers(){
  var ref$;
  if ((mpd != null ? (ref$ = mpd.playlist) != null ? ref$.item_table : void 8 : void 8) == null) {
    return null;
  }
  if ((mpd != null ? (ref$ = mpd.search_results) != null ? ref$.artist_table : void 8 : void 8) == null) {
    return null;
  }
  return {
    playlist: [
      selection.ids.playlist, mpd.playlist.item_table, function(id){
        return $("#playlist-track-" + id);
      }
    ],
    artist: [
      selection.ids.artist, mpd.search_results.artist_table, function(id){
        return $("#lib-artist-" + toHtmlId(id));
      }
    ],
    album: [
      selection.ids.album, mpd.search_results.album_table, function(id){
        return $("#lib-album-" + toHtmlId(id));
      }
    ],
    track: [
      selection.ids.track, mpd.search_results.track_table, function(id){
        return $("#lib-track-" + toHtmlId(id));
      }
    ],
    stored_playlist: [
      selection.ids.stored_playlist, mpd.stored_playlist_table, function(id){
        return $("#stored-pl-pl-" + toHtmlId(id));
      }
    ],
    stored_playlist_item: [
      selection.ids.stored_playlist_item, mpd.stored_playlist_item_table, function(id){
        return $("#stored-pl-item-" + toHtmlId(id));
      }
    ]
  };
}
function refreshSelection(){
  var helpers, sel_name, ref$, ids, table, $getDiv, i$, id, len$;
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
    for (i$ = 0, len$ = (ref$ = (fn$())).length; i$ < len$; ++i$) {
      id = ref$[i$];
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
function renderLibrary(){
  var context = {
    artists: mpd.search_results.artists,
    empty_library_message: mpd.have_file_list_cache ? "No Results" : "loading..."
  };
  var scroll_top = $library.scrollTop();
  $library.html(Handlebars.templates.library(context));
  var $artists = $library.children("ul").children("li");
  var node_count = $artists.length;
  function expandStuff($li_set){
    var i$, len$, li, $li, $ul, $sub_li_set, proposed_node_count;
    for (i$ = 0, len$ = $li_set.length; i$ < len$; ++i$) {
      li = $li_set[i$];
      $li = $(li);
      if (node_count >= AUTO_EXPAND_LIMIT) {
        return;
      }
      $ul = $li.children("ul");
      $sub_li_set = $ul.children("li");
      proposed_node_count = node_count + $sub_li_set.length;
      if (proposed_node_count <= AUTO_EXPAND_LIMIT) {
        toggleLibraryExpansion($li);
        $ul = $li.children("ul");
        $sub_li_set = $ul.children("li");
        node_count = proposed_node_count;
        expandStuff($sub_li_set);
      }
    }
  }
  expandStuff($artists);
  $library.scrollTop(scroll_top);
  refreshSelection();
}
function getCurrentTrackPosition(){
  if (mpd.status.track_start_date != null && mpd.status.state === "play") {
    return (new Date() - mpd.status.track_start_date) / 1000;
  } else {
    return mpd.status.paused_time;
  }
}
function updateSliderPos(){
  var ref$, disabled, elapsed, time, slider_pos;
  if (user_is_seeking) return;
  if (mpd.status.current_item != null && ((ref$ = mpd.status.state) != null ? ref$ : "stop") !== "stop") {
    disabled = false;
    elapsed = getCurrentTrackPosition();
    time = mpd.status.current_item.track.time;
    slider_pos = elapsed / time;
  } else {
    disabled = true;
    elapsed = time = slider_pos = 0;
  }
  $track_slider.slider("option", "disabled", disabled).slider("option", "value", slider_pos);
  $nowplaying_elapsed.html(formatTime(elapsed));
  $nowplaying_left.html(formatTime(time));
}
function renderNowPlaying(){
  var ref$, track, track_display, state, toggle_icon, old_class, new_class, vol, enabled;
  if ((track = (ref$ = mpd.status.current_item) != null ? ref$.track : void 8) != null) {
    track_display = track.name + " - " + track.artist_name;
    if (track.album_name.length) {
      track_display += " - " + track.album_name;
    }
    document.title = track_display + " - " + BASE_TITLE;
    if (track.name.indexOf("Groove Basin") === 0) {
      $("html").addClass('groovebasin');
    } else {
      $("html").removeClass('groovebasin');
    }
    if (track.name.indexOf("Never Gonna Give You Up") === 0 && track.artist_name.indexOf("Rick Astley") === 0) {
      $("html").addClass('nggyu');
    } else {
      $("html").removeClass('nggyu');
    }
  } else {
    track_display = "&nbsp;";
    document.title = BASE_TITLE;
  }
  $track_display.html(track_display);
  state = (ref$ = mpd.status.state) != null ? ref$ : "stop";
  toggle_icon = {
    play: ['ui-icon-play', 'ui-icon-pause'],
    stop: ['ui-icon-pause', 'ui-icon-play'],
    pause: ['ui-icon-pause', 'ui-icon-play']
  };
  ref$ = toggle_icon[state], old_class = ref$[0], new_class = ref$[1];
  $nowplaying.find(".toggle span").removeClass(old_class).addClass(new_class);
  $track_slider.slider("option", "disabled", state === "stop");
  updateSliderPos();
  if (!user_is_volume_sliding) {
    enabled = (vol = mpd.status.volume) != null;
    if (enabled) {
      $vol_slider.slider('option', 'value', vol);
    }
    $vol_slider.slider('option', 'disabled', !enabled);
  }
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
        item_list: mpd.stored_playlist_table[key].item_list
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
        albums: mpd.search_results.artist_table[key].albums
      };
    }
  });
}
function confirmDelete(files_list){
  var list_text = files_list.slice(0, 7).join("\n  ");
  if (files_list.length > 7) {
    list_text += "\n  ...";
  }
  var song_text = files_list.length === 1 ? "song" : "songs";
  return confirm("You are about to delete " + files_list.length + " " + song_text + " permanently:\n\n  " + list_text);
}
function handleDeletePressed(shift){
  var files_list;
  if (selection.isLibrary()) {
    files_list = selection.toFiles();
    if (!confirmDelete(files_list)) {
      return;
    }
    socket.emit('DeleteFromLibrary', JSON.stringify(files_list));
  } else if (selection.isPlaylist()) {
    if (shift) {
      files_list = [];
      for (var id in selection.ids.playlist) {
        files_list.push(mpd.playlist.item_table[id].track.file);
      }
      if (!confirmDelete(files_list)) return;
      socket.emit('DeleteFromLibrary', JSON.stringify(files_list));
    }
    var pos = mpd.playlist.item_table[selection.cursor].pos;
    mpd.removeIds((function(){
      var results$ = [];
      for (var id in selection.ids.playlist) {
        results$.push(id);
      }
      return results$;
    }()));
    if (pos >= mpd.playlist.item_list.length) {
      pos = mpd.playlist.item_list.length - 1;
    }
    if (pos > -1) {
      selection.selectOnly('playlist', mpd.playlist.item_list[pos].id);
    }
    refreshSelection();
  }
}
function togglePlayback(){
  if (mpd.status.state === 'play') {
    mpd.pause();
  } else {
    mpd.play();
  }
}
function setDynamicMode(value){
  var args = {
    dynamic_mode: value
  };
  socket.emit('DynamicMode', JSON.stringify(args));
}
function toggleDynamicMode(){
  setDynamicMode(!server_status.dynamic_mode);
}

function nextRepeatState(){
  mpd.setRepeatMode((mpd.status.repeat + 1) % repeatModeNames.length);
}

var keyboard_handlers = (function(){
  var handlers;
  function upDownHandler(event){
    var default_index, dir, next_pos;
    if (event.which === 38) {
      default_index = mpd.playlist.item_list.length - 1;
      dir = -1;
    } else {
      default_index = 0;
      dir = 1;
    }
    if (event.ctrlKey) {
      if (selection.isPlaylist()) {
        mpd.shiftIds(selection.ids.playlist, dir);
      }
    } else {
      if (selection.isPlaylist()) {
        next_pos = mpd.playlist.item_table[selection.cursor].pos + dir;
        if (next_pos < 0 || next_pos >= mpd.playlist.item_list.length) {
          return;
        }
        selection.cursor = mpd.playlist.item_list[next_pos].id;
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
          selection.cursor = next_pos.track.file;
        } else if (next_pos.album != null) {
          selection.type = 'album';
          selection.cursor = next_pos.album.key;
        } else {
          selection.type = 'artist';
          selection.cursor = next_pos.artist.key;
        }
        selection.ids[selection.type][selection.cursor] = true;
      } else {
        selection.selectOnly('playlist', mpd.playlist.item_list[default_index].id);
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
          mpd.next();
        } else {
          mpd.prev();
        }
      } else if (event.shiftKey) {
        mpd.seek(getCurrentTrackPosition() + dir * mpd.status.time * 0.10);
      } else {
        mpd.seek(getCurrentTrackPosition() + dir * 10);
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
          mpd.playId(selection.cursor);
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
        mpd.clear();
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
        mpd.shuffle();
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
        mpd.setVolume(mpd.status.volume + 0.10);
      }
    },
    188: {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(){
        mpd.prev();
      }
    },
    189: {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(){
        mpd.setVolume(mpd.status.volume - 0.10);
      }
    },
    190: {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(){
        mpd.next();
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
    lib_pos.track = lib_pos.track.album.tracks[lib_pos.track.pos - 1];
  } else if (lib_pos.album != null) {
    lib_pos.album = lib_pos.artist.albums[lib_pos.album.pos - 1];
    if (lib_pos.album != null && isAlbumExpanded(lib_pos.album)) {
      lib_pos.track = lib_pos.album.tracks[lib_pos.album.tracks.length - 1];
    }
  } else if (lib_pos.artist != null) {
    lib_pos.artist = mpd.search_results.artists[lib_pos.artist.pos - 1];
    if (lib_pos.artist != null && isArtistExpanded(lib_pos.artist)) {
      lib_pos.album = lib_pos.artist.albums[lib_pos.artist.albums.length - 1];
      if (lib_pos.album != null && isAlbumExpanded(lib_pos.album)) {
        lib_pos.track = lib_pos.album.tracks[lib_pos.album.tracks.length - 1];
      }
    }
  }
}
function queueSelection(event){
  var files;
  files = selection.toFiles(event.altKey);
  if (event.shiftKey) {
    mpd.queueFilesNext(files);
  } else {
    mpd.queueFiles(files);
  }
  return false;
}
function sendAuth(){
  var pass;
  pass = local_state.auth_password;
  if (pass == null) {
    return;
  }
  mpd.authenticate(pass, function(err){
    if (err) {
      local_state.auth_password = null;
      saveLocalState();
    }
    renderSettings();
  });
}
function settingsAuthSave(){
  var $text_box;
  settings_ui.auth.show_edit = false;
  $text_box = $('#auth-password');
  local_state.auth_password = $text_box.val();
  saveLocalState();
  renderSettings();
  sendAuth();
}
function settingsAuthCancel(){
  settings_ui.auth.show_edit = false;
  renderSettings();
}
function performDrag(event, callbacks){
  var start_drag_x, start_drag_y;
  abortDrag();
  start_drag_x = event.pageX;
  start_drag_y = event.pageY;
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
    mpd.clear();
  });
  $pl_window.on('click', 'button.shuffle', function(){
    mpd.shuffle();
  });
  $pl_btn_repeat.on('click', function(){
    nextRepeatState();
  });
  $dynamic_mode.on('click', function(){
    var value;
    value = $(this).prop("checked");
    setDynamicMode(value);
    return false;
  });
  $playlist_items.on('dblclick', '.pl-item', function(event){
    var track_id;
    track_id = $(this).attr('data-id');
    mpd.playId(track_id);
  });
  $playlist_items.on('contextmenu', function(event){
    return event.altKey;
  });
  $playlist_items.on('mousedown', '.pl-item', function(event){
    var track_id, skip_drag, old_pos, new_pos, i, context, $menu;
    if (started_drag) {
      return true;
    }
    $(document.activeElement).blur();
    if (event.which === 1) {
      event.preventDefault();
      removeContextMenu();
      track_id = $(this).attr('data-id');
      skip_drag = false;
      if (!selection.isPlaylist()) {
        selection.selectOnly('playlist', track_id);
      } else if (event.ctrlKey || event.shiftKey) {
        skip_drag = true;
        if (event.shiftKey && !event.ctrlKey) {
          selection.clear();
        }
        if (event.shiftKey) {
          old_pos = selection.cursor != null ? mpd.playlist.item_table[selection.cursor].pos : 0;
          new_pos = mpd.playlist.item_table[track_id].pos;
          for (i = old_pos; i <= new_pos; ++i) {
            selection.ids.playlist[mpd.playlist.item_list[i].id] = true;
          }
        } else if (event.ctrlKey) {
          if (selection.ids.playlist[track_id] != null) {
            delete selection.ids.playlist[track_id];
          } else {
            selection.ids.playlist[track_id] = true;
          }
          selection.cursor = track_id;
        }
      } else if (selection.ids.playlist[track_id] == null) {
        selection.selectOnly('playlist', track_id);
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
            mpd.moveIds((function(){
              var results$ = [];
              for (var id in selection.ids.playlist) {
                results$.push(id);
              }
              return results$;
            }()), result.previous_key, result.next_key);
          },
          cancel: function(){
            selection.selectOnly('playlist', track_id);
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
      track_id = $(this).attr('data-id');
      if (!selection.isPlaylist() || selection.ids.playlist[track_id] == null) {
        selection.selectOnly('playlist', track_id);
        refreshSelection();
      }
      context = {
        status: server_status,
        permissions: permissions
      };
      if (selection.isMulti()) {
        context.download_multi = true;
      } else {
        context.item = mpd.playlist.item_table[track_id];
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
        downloadFiles(selection.toFiles());
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
    var message, new_user_name, NICK;
    event.stopPropagation();
    if (event.which === 27) {
      $(event.target).blur();
      return false;
    } else if (event.which === 13) {
      message = $.trim($(event.target).val());
      setTimeout(function(){
        $(event.target).val("");
      }, 0);
      if (message === "") {
        return false;
      }
      if (!haveUserName()) {
        new_user_name = message;
      }
      NICK = "/nick ";
      if (message.substr(0, NICK.length) === NICK) {
        new_user_name = message.substr(NICK.length);
      }
      if (new_user_name != null) {
        setUserName(new_user_name);
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
      mpd.prev();
    },
    next: function(){
      mpd.next();
    },
    stop: function(){
      mpd.stop();
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
      mpd.seek(ui.value * mpd.status.time);
    },
    slide: function(event, ui){
      updateSliderUi(ui.value);
      $nowplaying_elapsed.html(formatTime(ui.value * mpd.status.time));
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
    mpd.setVolume(ui.value);
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
  var tabs, i$, len$, tab;
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
    var i$, ref$, len$, tab;
    $tabs.find('li').removeClass('ui-state-active');
    for (i$ = 0, len$ = (ref$ = tabs).length; i$ < len$; ++i$) {
      tab = ref$[i$];
      $pane(tab).hide();
    }
  }
  clickTab = function(name){
    if (name === 'upload' && !(server_status != null && server_status.upload_enabled)) {
      return;
    }
    unselectTabs();
    $tab(name).addClass('ui-state-active');
    $pane(name).show();
    handleResize();
  };
  for (i$ = 0, len$ = tabs.length; i$ < len$; ++i$) {
    tab = tabs[i$];
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
    local_state.lastfm.username = null;
    local_state.lastfm.session_key = null;
    local_state.lastfm.scrobbling_on = false;
    saveLocalState();
    renderSettings();
    return false;
  });
  $settings.on('click', '#toggle-scrobble', function(event){
    var value, msg, params;
    value = $(this).prop("checked");
    if (value) {
      msg = 'LastfmScrobblersAdd';
      local_state.lastfm.scrobbling_on = true;
    } else {
      msg = 'LastfmScrobblersRemove';
      local_state.lastfm.scrobbling_on = false;
    }
    saveLocalState();
    params = {
      username: local_state.lastfm.username,
      session_key: local_state.lastfm.session_key
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
    $text_box.focus().val((ref$ = local_state.auth_password) != null ? ref$ : "").select();
  });
  $settings.on('click', '.auth-clear', function(event){
    local_state.auth_password = null;
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
    var files, i$, ref$, len$, artist, j$, ref1$, len1$, album, k$, ref2$, len2$, track;
    event.stopPropagation();
    switch (event.which) {
    case 27:
      if ($(event.target).val().length === 0) {
        $(event.target).blur();
      } else {
        setTimeout(function(){
          $(event.target).val("");
          mpd.search("");
        }, 0);
      }
      return false;
    case 13:
      files = [];
      for (i$ = 0, len$ = (ref$ = mpd.search_results.artists).length; i$ < len$; ++i$) {
        artist = ref$[i$];
        for (j$ = 0, len1$ = (ref1$ = artist.albums).length; j$ < len1$; ++j$) {
          album = ref1$[j$];
          for (k$ = 0, len2$ = (ref2$ = album.tracks).length; k$ < len2$; ++k$) {
            track = ref2$[k$];
            files.push(track.file);
          }
        }
      }
      if (event.altKey) shuffle(files);
      if (files.length > 2000) {
        if (!confirm("You are about to queue " + files.length + " songs.")) {
          return false;
        }
      }
      if (event.shiftKey) {
        mpd.queueFilesNext(files);
      } else {
        mpd.queueFiles(files);
      }
      return false;
    case 40:
      selection.selectOnly('artist', mpd.search_results.artists[0].key);
      refreshSelection();
      $lib_filter.blur();
      return false;
    case 38:
      selection.selectOnly('artist', mpd.search_results.artists[mpd.search_results.artists.length - 1].key);
      refreshSelection();
      $lib_filter.blur();
      return false;
    }
  });
  $lib_filter.on('keyup', function(event){
    mpd.search($(event.target).val());
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
            var delta, files;
            delta = {
              top: 0,
              bottom: 1
            };
            files = selection.toFiles(event.altKey);
            mpd.queueFiles(files, result.previous_key, result.next_key);
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
        status: server_status,
        permissions: permissions
      };
      if (selection.isMulti()) {
        context.download_multi = true;
      } else {
        if (type === 'track') {
          context.track = mpd.search_results.track_table[key];
        } else if (type === 'stored_playlist_item') {
          context.track = mpd.stored_playlist_item_table[key].track;
        } else {
          context.download_type = type;
          context.escaped_key = encodeURIComponent(key);
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
        mpd.queueFiles(selection.toFiles());
        removeContextMenu();
        return false;
      });
      $menu.on('click', '.queue-next', function(){
        mpd.queueFilesNext(selection.toFiles());
        removeContextMenu();
        return false;
      });
      $menu.on('click', '.queue-random', function(){
        mpd.queueFiles(selection.toFiles(true));
        removeContextMenu();
        return false;
      });
      $menu.on('click', '.queue-next-random', function(){
        mpd.queueFilesNext(selection.toFiles(true));
        removeContextMenu();
        return false;
      });
      $menu.on('click', '.download', function(){
        removeContextMenu();
        return true;
      });
      $menu.on('click', '.download-multi', function(){
        removeContextMenu();
        downloadFiles(selection.toFiles());
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
  $chat_list.height(MARGIN);
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
  $chat_list.height(tab_contents_height - $chat_user_list.height() - $chat_input_pane.height());
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
      local_state.lastfm.username = params.session.name;
      local_state.lastfm.session_key = params.session.key;
      local_state.lastfm.scrobbling_on = false;
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
    var user_name;
    my_user_id = data.toString();
    local_state.my_user_ids[my_user_id] = 1;
    saveLocalState();
    if ((user_name = local_state.user_name) != null) {
      setUserName(user_name);
    }
  });
  socket.on('Permissions', function(data){
    permissions = JSON.parse(data.toString());
    renderSettings();
  });
  socket.on('Status', function(data){
    server_status = JSON.parse(data.toString());
    renderPlaylistButtons();
    renderChat();
    labelPlaylistItems();
    renderSettings();
    window._debug_server_status = server_status;
  });
  mpd = new PlayerClient(socket);
  mpd.on('libraryupdate', renderLibrary);
  mpd.on('playlistupdate', renderPlaylist);
  mpd.on('storedplaylistupdate', renderStoredPlaylists);
  mpd.on('statusupdate', function(){
    renderNowPlaying();
    renderPlaylistButtons();
    labelPlaylistItems();
  });
  mpd.on('chat', renderChat);
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
  streaming.init(mpd, socket);
  render();
  $window.resize(handleResize);
  window._debug_mpd = mpd;
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
