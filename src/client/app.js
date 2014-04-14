var $ = window.$;

var shuffle = require('mess');
var querystring = require('querystring');
var zfill = require('zfill');
var PlayerClient = require('./playerclient');
var streaming = require('./streaming');
var Socket = require('./socket');
var uuid = require('uuid');

var dynamicModeOn = false;

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
  rangeSelectAnchor: null,
  rangeSelectAnchorType: null,
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
    this.rangeSelectAnchor = null;
    this.rangeSelectAnchorType = null;
  },
  selectOnly: function(selName, key){
    this.clear();
    this.type = selName;
    this.ids[selName][key] = true;
    this.cursor = key;
    this.rangeSelectAnchor = key;
    this.rangeSelectAnchorType = selName;
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
    if (type == null) type = this.type;
    if (key == null) key = this.cursor;
    var val;
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
    } else {
      throw new Error("NothingSelected");
    }
    return val;
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
          var nextAlbum = pos.artist.albumList[pos.album.index + 1];
          if (nextAlbum) {
            pos.album = nextAlbum;
          } else {
            pos.artist = player.searchResults.artistList[pos.artist.index + 1];
            pos.album = null;
          }
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
var userIsSeeking = false;
var userIsVolumeSliding = false;
var started_drag = false;
var abortDrag = function(){};
var clickTab = null;
var myUserId = null;
var lastFmApiKey = null;
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
  authPassword: null,
  autoQueueUploads: true,
};
var $document = $(document);
var $window = $(window);
var $pl_window = $('#playlist-window');
var $left_window = $('#left-window');
var $playlistItems = $('#playlist-items');
var $dynamicMode = $('#dynamic-mode');
var $pl_btn_repeat = $('#pl-btn-repeat');
var $tabs = $('#tabs');
var $upload_tab = $tabs.find('.upload-tab');
var $library = $('#library');
var $lib_filter = $('#lib-filter');
var $trackSlider = $('#track-slider');
var $nowplaying = $('#nowplaying');
var $nowplaying_elapsed = $nowplaying.find('.elapsed');
var $nowplaying_left = $nowplaying.find('.left');
var $volSlider = $('#vol-slider');
var $settings = $('#settings');
var $uploadByUrl = $('#upload-by-url');
var $main_err_msg = $('#main-err-msg');
var $main_err_msg_text = $('#main-err-msg-text');
var $stored_playlists = $('#stored-playlists');
var $upload = $('#upload');
var $track_display = $('#track-display');
var $lib_header = $('#library-pane .window-header');
var $pl_header = $pl_window.find('#playlist .header');
var $autoQueueUploads = $('#auto-queue-uploads');
var uploadInput = document.getElementById("upload-input");
var $uploadWidget = $("#upload-widget");
var $settingsEditPassword = $('#settings-edit-password');
var $settingsShowPassword = $('#settings-show-password');
var $settingsAuthCancel = $('#settings-auth-cancel');
var $settingsAuthSave = $('#settings-auth-save');
var $settingsAuthEdit = $('#settings-auth-edit');
var $settingsAuthClear = $('#settings-auth-clear');
var passwordDisplayDom = document.getElementById('password-display');
var streamUrlDom = document.getElementById('settings-stream-url');
var $authPermRead = $('#auth-perm-read');
var $authPermAdd = $('#auth-perm-add');
var $authPermControl = $('#auth-perm-control');
var $authPermAdmin = $('#auth-perm-admin');
var $lastFmSignOut = $('#lastfm-sign-out');
var $authPassword = $('#auth-password');
var lastFmAuthUrlDom = document.getElementById('lastfm-auth-url');
var $settingsLastFmIn = $('#settings-lastfm-in');
var $settingsLastFmOut = $('#settings-lastfm-out');
var settingsLastFmUserDom = document.getElementById('settings-lastfm-user');
var $toggleScrobble = $('#toggle-scrobble');
var $shortcuts = $('#shortcuts');
var $editTagsDialog = $('#edit-tags');
var $playlistMenu = $('#menu-playlist');
var $libraryMenu = $('#menu-library');

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

function scrollLibraryToSelection(){
  var helpers = getSelectionHelpers();
  if (helpers == null) return;
  delete helpers.playlist;
  scrollThingToSelection($library, helpers);
}

function scrollPlaylistToSelection(){
  var helpers = getSelectionHelpers();
  if (helpers == null) return;
  delete helpers.track;
  delete helpers.artist;
  delete helpers.album;
  scrollThingToSelection($playlistItems, helpers);
}

function scrollThingToSelection($scroll_area, helpers){
  var topPos = null;
  var bottomPos = null;
  for (var selName in helpers) {
    var helper = helpers[selName];
    for (var id in helper.ids) {
      var $div = helper.$getDiv(id);
      var itemTop = $div.offset().top;
      var itemBottom = itemTop + $div.height();
      if (topPos == null || itemTop < topPos) {
        topPos = itemTop;
      }
      if (bottomPos == null || itemBottom > bottomPos) {
        bottomPos = itemBottom;
      }
    }
  }
  if (topPos != null) {
    var scroll_area_top = $scroll_area.offset().top;
    var selection_top = topPos - scroll_area_top;
    var selection_bottom = bottomPos - scroll_area_top - $scroll_area.height();
    var scroll_amt = $scroll_area.scrollTop();
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
  var ref$;
  var result = {};
  for (var i = 0, len$ = (ref$ = $playlistItems.find(".pl-item").get()).length; i < len$; ++i) {
    var item = ref$[i];
    var $item = $(item);
    var middle = $item.offset().top + $item.height() / 2;
    var track = player.playlist.itemTable[$item.attr('data-id')];
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

function renderPlaylistButtons(){
  $dynamicMode
    .prop("checked", dynamicModeOn)
    .button("refresh");
  var repeatModeName = repeatModeNames[player.repeat];
  $pl_btn_repeat
    .button("option", "label", "Repeat: " + repeatModeName)
    .prop("checked", player.repeat !== PlayerClient.REPEAT_OFF)
    .button("refresh");
}

function renderPlaylist(){
  var itemList = player.playlist.itemList || [];
  var scrollTop = $playlistItems.scrollTop();

  // add the missing dom entries
  var i;
  var playlistItemsDom = $playlistItems.get(0);
  for (i = playlistItemsDom.childElementCount; i < itemList.length; i += 1) {
    $playlistItems.append(
      '<div class="pl-item">' +
        '<span class="track"></span>' +
        '<span class="title"></span>' +
        '<span class="artist"></span>' +
        '<span class="album"></span>' +
        '<span class="time"></span>' +
      '</div>');
  }
  // remove the extra dom entries
  var domItem;
  while (itemList.length < playlistItemsDom.childElementCount) {
    playlistItemsDom.removeChild(playlistItemsDom.lastChild);
  }

  // overwrite existing dom entries
  var $domItems = $playlistItems.children();
  var item, track;
  for (i = 0; i < itemList.length; i += 1) {
    var $domItem = $($domItems[i]);
    item = itemList[i];
    $domItem.attr('id', 'playlist-track-' + item.id);
    $domItem.attr('data-id', item.id);
    track = item.track;
    $domItem.find('.track').text(track.track || "");
    $domItem.find('.title').text(track.name || "");
    $domItem.find('.artist').text(track.artistName || "");
    $domItem.find('.album').text(track.albumName || "");
    $domItem.find('.time').text(formatTime(track.duration));
  }

  refreshSelection();
  labelPlaylistItems();
  $playlistItems.scrollTop(scrollTop);
}

function labelPlaylistItems() {
  var item;
  var curItem = player.currentItem;
  $playlistItems.find(".pl-item")
    .removeClass('current')
    .removeClass('old')
    .removeClass('random');
  if (curItem != null && dynamicModeOn) {
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

function getSelectionHelpers(){
  if (player == null) return null;
  if (player.playlist == null) return null;
  if (player.playlist.itemTable == null) return null;
  if (player.searchResults == null) return null;
  if (player.searchResults.artistTable == null) return null;
  return {
    playlist: {
      ids: selection.ids.playlist,
      table: player.playlist.itemTable,
      $getDiv: function(id){
        return $("#playlist-track-" + id);
      },
    },
    artist: {
      ids: selection.ids.artist,
      table: player.searchResults.artistTable,
      $getDiv: function(id){
        return $("#lib-artist-" + toHtmlId(id));
      },
    },
    album: {
      ids: selection.ids.album,
      table: player.searchResults.albumTable,
      $getDiv: function(id){
        return $("#lib-album-" + toHtmlId(id));
      },
    },
    track: {
      ids: selection.ids.track,
      table: player.searchResults.trackTable,
      $getDiv: function(id){
        return $("#lib-track-" + toHtmlId(id));
      },
    },
    stored_playlist: {
      ids: selection.ids.stored_playlist,
      table: player.stored_playlist_table,
      $getDiv: function(id){
        return $("#stored-pl-pl-" + toHtmlId(id));
      },
    },
    stored_playlist_item: {
      ids: selection.ids.stored_playlist_item,
      table: player.stored_playlist_item_table,
      $getDiv: function(id){
        return $("#stored-pl-item-" + toHtmlId(id));
      },
    },
  };
}

function refreshSelection() {
  var helpers = getSelectionHelpers();
  if (helpers == null) return;
  $playlistItems  .find(".pl-item"  ).removeClass('selected').removeClass('cursor');
  $library         .find(".clickable").removeClass('selected').removeClass('cursor');
  $stored_playlists.find(".clickable").removeClass('selected').removeClass('cursor');
  if (selection.type == null) return;
  for (var selection_type in helpers) {
    var helper = helpers[selection_type];
    var id;
    // clean out stale ids
    for (id in helper.ids) {
      if (helper.table[id] == null) {
        delete helper.ids[id];
      }
    }
    for (id in helper.ids) {
      helper.$getDiv(id).addClass('selected');
    }
    if (selection.cursor != null && selection_type === selection.type) {
      var validIds = getValidIds(selection_type);
      if (validIds[selection.cursor] == null) {
        // server just deleted our current cursor item.
        // select another of our ids randomly, if we have any.
        selection.cursor = Object.keys(helper.ids)[0];
        selection.rangeSelectAnchor = selection.cursor;
        selection.rangeSelectAnchorType = selection_type;
        if (selection.cursor == null) {
          // no selected items
          selection.fullClear();
        }
      }
      if (selection.cursor != null) {
        helper.$getDiv(selection.cursor).addClass('cursor');
      }
    }
  }
}

function getValidIds(selection_type) {
  switch (selection_type) {
    case 'playlist': return player.playlist.itemTable;
    case 'artist':   return player.library.artistTable;
    case 'album':    return player.library.albumTable;
    case 'track':    return player.library.trackTable;
  }
  throw new Error("BadSelectionType");
}

var $emptyLibraryMessage = $('#empty-library-message');
var $libraryNoItems = $('#library-no-items');
var $libraryArtists = $('#library-artists');

function artistId(s) {
  return "lib-artist-" + toHtmlId(s);
}

function artistDisplayName(name) {
  return name || '[Unknown Artist]';
}

function renderLibrary() {
  var artistList = player.searchResults.artistList || [];
  var scrollTop = $library.scrollTop();

  $emptyLibraryMessage.text(player.haveFileListCache ? "No Results" : "loading...");
  $libraryNoItems.toggle(!artistList.length);

  // add the missing dom entries
  var i;
  var artistListDom = $libraryArtists.get(0);
  for (i = artistListDom.childElementCount; i < artistList.length; i += 1) {
    $libraryArtists.append(
      '<li>' +
        '<div class="clickable expandable" data-type="artist">' +
          '<div class="ui-icon"></div>' +
          '<span></span>' +
        '</div>' +
        '<ul></ul>' +
      '</li>');
  }
  // remove the extra dom entries
  var domItem;
  while (artistList.length < artistListDom.childElementCount) {
    artistListDom.removeChild(artistListDom.lastChild);
  }

  // overwrite existing dom entries
  var artist;
  var $domItems = $libraryArtists.children();
  for (i = 0; i < artistList.length; i += 1) {
    domItem = $domItems[i];
    artist = artistList[i];
    $(domItem).data('cached', false);
    var divDom = domItem.children[0];
    divDom.setAttribute('id', artistId(artist.key));
    divDom.setAttribute('data-key', artist.key);
    var iconDom = divDom.children[0];
    $(iconDom)
      .addClass(ICON_COLLAPSED)
      .removeClass(ICON_EXPANDED);
    var spanDom = divDom.children[1];
    spanDom.textContent = artistDisplayName(artist.name);
    var ulDom = domItem.children[1];
    ulDom.style.display = 'block';
    while (ulDom.firstChild) {
      ulDom.removeChild(ulDom.firstChild);
    }
  }

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
  if (player.trackStartDate != null && player.isPlaying === true) {
    return (new Date() - player.trackStartDate) / 1000;
  } else {
    return player.pausedTime;
  }
}

function updateSliderPos() {
  if (userIsSeeking) return;

  var duration, disabled, elapsed, sliderPos;
  if (player.currentItem && player.isPlaying != null && player.currentItem.track) {
    disabled = false;
    elapsed = getCurrentTrackPosition();
    duration = player.currentItem.track.duration;
    sliderPos = elapsed / duration;
  } else {
    disabled = true;
    elapsed = duration = sliderPos = 0;
  }
  $trackSlider.slider("option", "disabled", disabled).slider("option", "value", sliderPos);
  $nowplaying_elapsed.html(formatTime(elapsed));
  $nowplaying_left.html(formatTime(duration));
}

function renderVolumeSlider() {
  if (userIsVolumeSliding) return;

  var enabled = player.volume != null;
  if (enabled) {
    $volSlider.slider('option', 'value', player.volume);
  }
  $volSlider.slider('option', 'disabled', !enabled);
}

function renderNowPlaying(){
  var track = null;
  if (player.currentItem != null) {
    track = player.currentItem.track;
  }
  var track_display;
  if (track != null) {
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
  var old_class;
  var new_class;
  if (player.isPlaying === true) {
    old_class = 'ui-icon-play';
    new_class = 'ui-icon-pause';
  } else {
    old_class = 'ui-icon-pause';
    new_class = 'ui-icon-play';
  }
  $nowplaying.find(".toggle span").removeClass(old_class).addClass(new_class);
  $trackSlider.slider("option", "disabled", player.isPlaying == null);
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
  updateSettingsAuthUi();
  updateLastFmSettingsUi();
  handleResize();
}

function renderArtist($ul, albumList) {
  albumList.forEach(function(album) {
    $ul.append(
      '<li>' +
        '<div class="clickable expandable" data-type="album">' +
          '<div class="ui-icon ui-icon-triangle-1-e"></div>' +
          '<span></span>' +
        '</div>' +
        '<ul style="display: none;"></ul>' +
      '</li>');
    var liDom = $ul.get(0).lastChild;
    var divDom = liDom.children[0];
    divDom.setAttribute('id', toAlbumId(album.key));
    divDom.setAttribute('data-key', album.key);
    var spanDom = divDom.children[1];
    spanDom.textContent = album.name || '[Unknown Album]';

    var artistUlDom = liDom.children[1];
    var $artistUlDom = $(artistUlDom);
    album.trackList.forEach(function(track) {
      $artistUlDom.append(
        '<li>' +
          '<div class="clickable" data-type="track">' +
            '<span></span>' +
          '</div>' +
        '</li>');
      var trackLiDom = artistUlDom.lastChild;
      var trackDivDom = trackLiDom.children[0];
      trackDivDom.setAttribute('id', toTrackId(track.key));
      trackDivDom.setAttribute('data-key', track.key);
      var trackSpanDom = trackDivDom.children[0];
      var caption = "";
      if (track.track) {
        caption += track.track + ". ";
      }
      if (track.compilation) {
        caption += track.artistName + " - ";
      }
      caption += track.name;
      trackSpanDom.textContent = caption;
    });
  });
}

function toggleLibraryExpansion($li){
  var $div = $li.find("> div");
  var $ul = $li.find("> ul");
  if ($div.attr('data-type') === 'artist') {
    if (!$li.data('cached')) {
      $li.data('cached', true);
      var artistKey = $div.attr('data-key');
      var albumList = player.searchResults.artistTable[artistKey].albumList;

      renderArtist($ul, albumList);

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
    socket.send('deleteTracks', keysList);
  } else if (selection.isPlaylist()) {
    if (shift) {
      keysList = [];
      for (var id in selection.ids.playlist) {
        keysList.push(player.playlist.itemTable[id].track.key);
      }
      if (!confirmDelete(keysList)) return;
      socket.send('deleteTracks', keysList);
    }
    var sortKey = player.playlist.itemTable[selection.cursor].sortKey;
    player.removeIds(Object.keys(selection.ids.playlist));
    var item = null;
    for (var i = 0; i < player.playlist.itemList.length; i++) {
      item = player.playlist.itemList[i];
      if (item.sortKey > sortKey) {
        // select the very next one
        break;
      }
      // if we deleted the last item, select the new last item.
    }
    // if there's no items, select nothing.
    if (item != null) {
      selection.selectOnly('playlist', item.id);
    }
    refreshSelection();
  }
}

function togglePlayback(){
  if (player.isPlaying === true) {
    player.pause();
  } else if (player.isPlaying === false) {
    player.play();
  }
  // else we haven't received state from server yet
}

function setDynamicMode(value) {
  dynamicModeOn = value;
  player.sendCommand('dynamicModeOn', dynamicModeOn);
}

function toggleDynamicMode(){
  setDynamicMode(!dynamicModeOn);
}

function nextRepeatState(){
  player.setRepeatMode((player.repeat + 1) % repeatModeNames.length);
}

var keyboardHandlers = (function(){
  function upDownHandler(event){
    var defaultIndex, dir, nextPos;
    if (event.which === 38) {
      // up
      defaultIndex = player.currentItem ? player.currentItem.index - 1 : player.playlist.itemList.length - 1;
      dir = -1;
    } else {
      // down
      defaultIndex = player.currentItem ? player.currentItem.index + 1 : 0;
      dir = 1;
    }
    if (defaultIndex >= player.playlist.itemList.length) {
      defaultIndex = player.playlist.itemList.length - 1;
    } else if (defaultIndex < 0) {
      defaultIndex = 0;
    }
    if (event.altKey) {
      if (selection.isPlaylist()) {
        player.shiftIds(selection.ids.playlist, dir);
      }
    } else {
      if (selection.isPlaylist()) {
        nextPos = player.playlist.itemTable[selection.cursor].index + dir;
        if (nextPos < 0 || nextPos >= player.playlist.itemList.length) {
          return;
        }
        selection.cursor = player.playlist.itemList[nextPos].id;
        if (!event.ctrlKey && !event.shiftKey) {
          // single select
          selection.clear();
          selection.ids.playlist[selection.cursor] = true;
          selection.rangeSelectAnchor = selection.cursor;
          selection.rangeSelectAnchorType = selection.type;
        } else if (!event.ctrlKey && event.shiftKey) {
          // range select
          selectPlaylistRange();
        } else {
          // ghost selection
          selection.rangeSelectAnchor = selection.cursor;
          selection.rangeSelectAnchorType = selection.type;
        }
      } else if (selection.isLibrary()) {
        nextPos = selection.getPos();
        if (dir > 0) {
          selection.incrementPos(nextPos);
        } else {
          prevLibPos(nextPos);
        }
        if (nextPos.artist == null) return;
        if (nextPos.track != null) {
          selection.type = 'track';
          selection.cursor = nextPos.track.key;
        } else if (nextPos.album != null) {
          selection.type = 'album';
          selection.cursor = nextPos.album.key;
        } else {
          selection.type = 'artist';
          selection.cursor = nextPos.artist.key;
        }
        if (!event.ctrlKey && !event.shiftKey) {
          // single select
          selection.selectOnly(selection.type, selection.cursor);
        } else if (!event.ctrlKey && event.shiftKey) {
          // range select
          selectTreeRange();
        } else {
          // ghost selection
          selection.rangeSelectAnchor = selection.cursor;
          selection.rangeSelectAnchorType = selection.type;
        }
      } else {
        if (player.playlist.itemList.length === 0) return;
        selection.selectOnly('playlist', player.playlist.itemList[defaultIndex].id);
      }
      refreshSelection();
    }
    if (selection.isPlaylist()) scrollPlaylistToSelection();
    if (selection.isLibrary()) scrollLibraryToSelection();
  }
  function leftRightHandler(event){
    var dir = event.which === 37 ? -1 : 1;
    if (selection.isLibrary()) {
      var helpers = getSelectionHelpers();
      if (helpers == null) return;
      var helper = helpers[selection.type];
      var selected_item = helper.table[selection.cursor];
      var is_expanded_funcs = {
        artist: isArtistExpanded,
        album: isAlbumExpanded,
        track: function(){
          return true;
        }
      };
      var is_expanded = is_expanded_funcs[selection.type](selected_item);
      var $li = helper.$getDiv(selection.cursor).closest("li");
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
        if (!player.currentItem) return;
        player.seek(null, getCurrentTrackPosition() + dir * player.currentItem.track.duration * 0.10);
      } else {
        player.seek(null, getCurrentTrackPosition() + dir * 10);
      }
    }
  }
  var volumeDownHandler = {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(){
        player.setVolume(player.volume - 0.10);
      }
  };
  var volumeUpHandler = {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(){
        player.setVolume(player.volume + 0.10);
      }
  };
  return {
    // Enter
    13: {
      ctrl: false,
      alt: null,
      shift: null,
      handler: function(event){
        if (selection.isPlaylist()) {
          player.seek(selection.cursor, 0);
        } else if (selection.isLibrary()) {
          queueSelection(event);
        }
        return false;
      },
    },
    // Escape
    27: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: function(){
        if (started_drag) {
          abortDrag();
          return;
        }
        if (removeContextMenu()) return;
        selection.fullClear();
        refreshSelection();
      },
    },
    // Space
    32: {
      ctrl: null,
      alt: false,
      shift: false,
      handler: function() {
        if (event.ctrlKey) {
          toggleSelectionUnderCursor();
          refreshSelection();
        } else {
          togglePlayback();
        }
      },
    },
    // Left
    37: {
      ctrl: null,
      alt: false,
      shift: null,
      handler: leftRightHandler,
    },
    // Up
    38: {
      ctrl: null,
      alt: null,
      shift: null,
      handler: upDownHandler,
    },
    // Right
    39: {
      ctrl: null,
      alt: false,
      shift: null,
      handler: leftRightHandler,
    },
    // Down
    40: {
      ctrl: null,
      alt: null,
      shift: null,
      handler: upDownHandler,
    },
    // Delete
    46: {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(event){
        handleDeletePressed(event.shiftKey);
      },
    },
    // =
    61: volumeUpHandler,
    // C
    67: {
      ctrl: false,
      alt: false,
      shift: true,
      handler: function(){
        player.clear();
      },
    },
    // d
    68: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: toggleDynamicMode,
    },
    // S
    72: {
      ctrl: false,
      alt: false,
      shift: true,
      handler: function(){
        player.shuffle();
      },
    },
    // l
    76: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: function(){
        clickTab('library');
      },
    },
    // r
    82: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: nextRepeatState
    },
    // s
    83: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: streaming.toggleStatus
    },
    // u
    85: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: function(){
        clickTab('upload');
        $uploadByUrl.focus().select();
      },
    },
    // - maybe?
    173: volumeDownHandler,
    // +
    187: volumeUpHandler,
    // , <
    188: {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(){
        player.prev();
      },
    },
    // _ maybe?
    189: volumeDownHandler,
    // . >
    190: {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(){
        player.next();
      },
    },
    // ?
    191: {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(event){
        if (event.shiftKey) {
          $shortcuts.dialog({
            modal: true,
            title: "Keyboard Shortcuts",
            minWidth: 600,
            height: $document.height() - 40,
          });
          $shortcuts.focus();
        } else {
          clickTab('library');
          $lib_filter.focus().select();
        }
      },
    },
  };
})();

function removeContextMenu() {
  if ($playlistMenu.is(":visible")) {
    $playlistMenu.hide();
    return true;
  }
  if ($libraryMenu.is(":visible")) {
    $libraryMenu.hide();
    return true;
  }
  return false;
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
  var $li = $("#lib-album-" + toHtmlId(album.key)).closest("li");
  return $li.find("> ul").is(":visible");
}
function isStoredPlaylistExpanded(stored_playlist){
  var $li = $("#stored-pl-pl-" + toHtmlId(stored_playlist.name)).closest("li");
  return $li.find("> ul").is(":visible");
}

function prevLibPos(libPos){
  if (libPos.track != null) {
    libPos.track = libPos.track.album.trackList[libPos.track.index - 1];
  } else if (libPos.album != null) {
    libPos.album = libPos.artist.albumList[libPos.album.index - 1];
    if (libPos.album != null && isAlbumExpanded(libPos.album)) {
      libPos.track = libPos.album.trackList[libPos.album.trackList.length - 1];
    }
  } else if (libPos.artist != null) {
    libPos.artist = player.searchResults.artistList[libPos.artist.index - 1];
    if (libPos.artist != null && isArtistExpanded(libPos.artist)) {
      libPos.album = libPos.artist.albumList[libPos.artist.albumList.length - 1];
      if (libPos.album != null && isAlbumExpanded(libPos.album)) {
        libPos.track = libPos.album.trackList[libPos.album.trackList.length - 1];
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

function toggleSelectionUnderCursor() {
  var key = selection.cursor;
  var type = selection.type;
  if (selection.ids[type][key] != null) {
    delete selection.ids[type][key];
  } else {
    selection.ids[type][key] = true;
  }
}

function selectPlaylistRange() {
  selection.clear();
  var anchor = selection.rangeSelectAnchor;
  if (anchor == null) anchor = selection.cursor;
  var min_pos = player.playlist.itemTable[anchor].index;
  var max_pos = player.playlist.itemTable[selection.cursor].index;
  if (max_pos < min_pos) {
    var tmp = min_pos;
    min_pos = max_pos;
    max_pos = tmp;
  }
  for (var i = min_pos; i <= max_pos; i++) {
    selection.ids.playlist[player.playlist.itemList[i].id] = true;
  }
}
function selectTreeRange() {
  selection.clear();
  var old_pos = selection.getPos(selection.rangeSelectAnchorType, selection.rangeSelectAnchor);
  var new_pos = selection.getPos(selection.type, selection.cursor);
  if (compareArrays(selection.posToArr(old_pos), selection.posToArr(new_pos)) > 0) {
    var tmp = old_pos;
    old_pos = new_pos;
    new_pos = tmp;
  }
  while (selection.posInBounds(old_pos)) {
    selection.selectPos(old_pos);
    if (selection.posEqual(old_pos, new_pos)) {
      break;
    }
    selection.incrementPos(old_pos);
  }
}

function sendAuth() {
  var pass = localState.authPassword;
  if (!pass) return;
  socket.send('password', pass);
}

function settingsAuthSave(){
  settings_ui.auth.show_edit = false;
  localState.authPassword = $authPassword.val();
  saveLocalState();
  updateSettingsAuthUi();
  sendAuth();
}

function settingsAuthCancel(){
  settings_ui.auth.show_edit = false;
  updateSettingsAuthUi();
}

function performDrag(event, callbacks){
  abortDrag();
  var start_drag_x = event.pageX;
  var start_drag_y = event.pageY;
  abortDrag = function(){
    $document.off('mousemove', onDragMove).off('mouseup', onDragEnd);
    if (started_drag) {
      $playlistItems.find(".pl-item").removeClass('border-top').removeClass('border-bottom');
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
    $playlistItems.find(".pl-item").removeClass('border-top').removeClass('border-bottom');
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
  $(".jquery-button").button().on('click', blur);
  $document.on('mousedown', function(){
    removeContextMenu();
    selection.fullClear();
    refreshSelection();
  });
  $document.on('keydown', function(event){
    var handler = keyboardHandlers[event.which];
    if (handler == null) return true;
    if (handler.ctrl  != null && handler.ctrl  !== event.ctrlKey)  return true;
    if (handler.alt   != null && handler.alt   !== event.altKey)   return true;
    if (handler.shift != null && handler.shift !== event.shiftKey) return true;
    handler.handler(event);
    return false;
  });
  $shortcuts.on('keydown', function(event) {
    event.stopPropagation();
    if (event.which === 27) {
      $shortcuts.dialog('close');
    }
  });
}

function blur() {
  $(this).blur();
}

var dynamicModeLabel = document.getElementById('dynamic-mode-label');
var plBtnRepeatLabel = document.getElementById('pl-btn-repeat-label');
function setUpPlaylistUi(){
  $pl_window.on('click', 'button.clear', function(event){
    player.clear();
  });
  $pl_window.on('mousedown', 'button.clear', stopPropagation);

  $pl_window.on('click', 'button.shuffle', function(){
    player.shuffle();
  });
  $pl_window.on('mousedown', 'button.shuffle', stopPropagation);

  $pl_btn_repeat.on('click', nextRepeatState);
  plBtnRepeatLabel.addEventListener('mousedown', stopPropagation, false);

  $dynamicMode.on('click', function(){
    var value = $(this).prop("checked");
    setDynamicMode(value);
    return false;
  });
  dynamicModeLabel.addEventListener('mousedown', stopPropagation, false);

  $playlistItems.on('dblclick', '.pl-item', function(event){
    var trackId = $(this).attr('data-id');
    player.seek(trackId, 0);
  });
  $playlistItems.on('contextmenu', function(event){
    return event.altKey;
  });
  $playlistItems.on('mousedown', '.pl-item', function(event){
    var trackId, skipDrag;
    if (started_drag) return true;
    $(document.activeElement).blur();
    if (event.which === 1) {
      event.preventDefault();
      removeContextMenu();
      trackId = $(this).attr('data-id');
      skipDrag = false;
      if (!selection.isPlaylist()) {
        selection.selectOnly('playlist', trackId);
      } else if (event.ctrlKey || event.shiftKey) {
        skipDrag = true;
        if (event.shiftKey && !event.ctrlKey) {
          // range select click
          selection.cursor = trackId;
          selectPlaylistRange();
        } else if (!event.shiftKey && event.ctrlKey) {
          // individual item selection toggle
          selection.cursor = trackId;
          selection.rangeSelectAnchor = trackId;
          selection.rangeSelectAnchorType = selection.type;
          toggleSelectionUnderCursor();
        }
      } else if (selection.ids.playlist[trackId] == null) {
        selection.selectOnly('playlist', trackId);
      }
      refreshSelection();
      if (!skipDrag) {
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
            })(), result.previous_key, result.next_key);
          },
          cancel: function(){
            selection.selectOnly('playlist', trackId);
            refreshSelection();
          }
        });
      }
    } else if (event.which === 3) {
      if (event.altKey) return;
      event.preventDefault();
      removeContextMenu();
      trackId = $(this).attr('data-id');
      if (!selection.isPlaylist() || selection.ids.playlist[trackId] == null) {
        selection.selectOnly('playlist', trackId);
        refreshSelection();
      }
      if (!selection.isMulti()) {
        var item = player.playlist.itemTable[trackId];
        $playlistMenu.find('.download').attr('href', 'library/' + encodeURI(item.track.file));
      } else {
        $playlistMenu.find('.download').attr('href', '#');
      }
      $playlistMenu.show().offset({
        left: event.pageX + 1,
        top: event.pageY + 1
      });
      updateAdminActions($playlistMenu);
    }
  });
  $playlistItems.on('mousedown', function(){
    return false;
  });
  $playlistMenu.menu();
  $playlistMenu.on('mousedown', function(){
    return false;
  });
  $playlistMenu.on('click', '.remove', function(){
    handleDeletePressed(false);
    removeContextMenu();
    return false;
  });
  $playlistMenu.on('click', '.download', onDownloadContextMenu);
  $playlistMenu.on('click', '.delete', onDeleteContextMenu);
  $playlistMenu.on('click', '.edit-tags', onEditTagsContextMenu);
}

function stopPropagation(event) {
  event.stopPropagation();
}

function onDownloadContextMenu() {
  removeContextMenu();

  if (selection.isMulti()) {
    downloadKeys(selection.toTrackKeys());
    return false;
  }

  return true;
}
function onDeleteContextMenu() {
  if (!permissions.admin) return false;
  removeContextMenu();
  handleDeletePressed(true);
  return false;
}
var editTagsTrackKeys = null;
var editTagsTrackIndex = null;
function onEditTagsContextMenu() {
  if (!permissions.admin) return false;
  removeContextMenu();
  editTagsTrackKeys = selection.toTrackKeys();
  editTagsTrackIndex = 0;
  showEditTags();
  return false;
}
var EDITABLE_PROPS = {
  name: {
    type: 'string',
    write: true,
  },
  artistName: {
    type: 'string',
    write: true,
  },
  albumArtistName: {
    type: 'string',
    write: true,
  },
  albumName: {
    type: 'string',
    write: true,
  },
  compilation: {
    type: 'boolean',
    write: true,
  },
  track: {
    type: 'integer',
    write: true,
  },
  trackCount: {
    type: 'integer',
    write: true,
  },
  disc: {
    type: 'integer',
    write: true,
  },
  discCount: {
    type: 'integer',
    write: true,
  },
  year: {
    type: 'integer',
    write: true,
  },
  genre: {
    type: 'string',
    write: true,
  },
  composerName: {
    type: 'string',
    write: true,
  },
  performerName: {
    type: 'string',
    write: true,
  },
  file: {
    type: 'string',
    write: false,
  },
};
var EDIT_TAG_TYPES = {
  'string': {
    get: function(domItem) {
      return domItem.value;
    },
    set: function(domItem, value) {
      domItem.value = value || "";
    },
  },
  'integer': {
    get: function(domItem) {
      var n = parseInt(domItem.value, 10);
      if (isNaN(n)) return null;
      return n;
    },
    set: function(domItem, value) {
      domItem.value = value == null ? "" : value;
    },
  },
  'boolean': {
    get: function(domItem) {
      return domItem.checked;
    },
    set: function(domItem, value) {
      domItem.checked = !!value;
    },
  },
};
var perDom = document.getElementById('edit-tags-per');
var perLabelDom = document.getElementById('edit-tags-per-label');
var prevDom = document.getElementById('edit-tags-prev');
var nextDom = document.getElementById('edit-tags-next');
var editTagsFocusDom = document.getElementById('edit-tag-name');
function updateEditTagsUi() {
  var multiple = editTagsTrackKeys.length > 1;
  prevDom.disabled = !perDom.checked || editTagsTrackIndex === 0;
  nextDom.disabled = !perDom.checked || (editTagsTrackIndex === editTagsTrackKeys.length - 1);
  prevDom.style.visibility = multiple ? 'visible' : 'hidden';
  nextDom.style.visibility = multiple ? 'visible' : 'hidden';
  perLabelDom.style.visibility = multiple ? 'visible' : 'hidden';
  var multiCheckBoxVisible = multiple && !perDom.checked;
  var trackKeysToUse = perDom.checked ? [editTagsTrackKeys[editTagsTrackIndex]] : editTagsTrackKeys;

  for (var propName in EDITABLE_PROPS) {
    var propInfo = EDITABLE_PROPS[propName];
    var type = propInfo.type;
    var setter = EDIT_TAG_TYPES[type].set;
    var domItem = document.getElementById('edit-tag-' + propName);
    domItem.disabled = !propInfo.write;
    var multiCheckBoxDom = document.getElementById('edit-tag-multi-' + propName);
    multiCheckBoxDom.style.visibility = (multiCheckBoxVisible && propInfo.write) ? 'visible' : 'hidden';
    var commonValue = null;
    var consistent = true;
    for (var i = 0; i < trackKeysToUse.length; i += 1) {
      var key = trackKeysToUse[i];
      var track = player.library.trackTable[key];
      var value = track[propName];
      if (commonValue == null) {
        commonValue = value;
      } else if (commonValue !== value) {
        consistent = false;
        break;
      }
    }
    multiCheckBoxDom.checked = consistent;
    setter(domItem, consistent ? commonValue : null);
  }
}
function showEditTags() {
  $editTagsDialog.dialog({
    modal: true,
    title: "Edit Tags",
    minWidth: 800,
    height: $document.height() - 40,
  });
  perDom.checked = false;
  updateEditTagsUi();
  editTagsFocusDom.focus();
}

function setUpEditTagsUi() {
  $editTagsDialog.find("input").on("keydown", function(event) {
    event.stopPropagation();
    if (event.which === 27) {
      $editTagsDialog.dialog('close');
    } else if (event.which === 13) {
      saveAndClose();
    }
  });
  for (var propName in EDITABLE_PROPS) {
    var domItem = document.getElementById('edit-tag-' + propName);
    var multiCheckBoxDom = document.getElementById('edit-tag-multi-' + propName);
    var listener = createChangeListener(multiCheckBoxDom);
    domItem.addEventListener('change', listener, false);
    domItem.addEventListener('keypress', listener, false);
    domItem.addEventListener('focus', onFocus, false);
  }

  function onFocus(event) {
    editTagsFocusDom = event.target;
  }

  function createChangeListener(multiCheckBoxDom) {
    return function() {
      multiCheckBoxDom.checked = true;
    };
  }
  $("#edit-tags-ok").on('click', saveAndClose);
  $("#edit-tags-cancel").on('click', closeDialog);
  perDom.addEventListener('click', updateEditTagsUi, false);
  nextDom.addEventListener('click', saveAndNext, false);
  prevDom.addEventListener('click', saveAndPrev, false);

  function saveAndMoveOn(dir) {
    save();
    editTagsTrackIndex += dir;
    updateEditTagsUi();
    editTagsFocusDom.focus();
    editTagsFocusDom.select();
  }

  function saveAndNext() {
    saveAndMoveOn(1);
  }

  function saveAndPrev() {
    saveAndMoveOn(-1);
  }

  function save() {
    var trackKeysToUse = perDom.checked ? [editTagsTrackKeys[editTagsTrackIndex]] : editTagsTrackKeys;
    var cmd = {};
    for (var i = 0; i < trackKeysToUse.length; i += 1) {
      var key = trackKeysToUse[i];
      var track = player.library.trackTable[key];
      var props = cmd[track.key] = {};
      for (var propName in EDITABLE_PROPS) {
        var propInfo = EDITABLE_PROPS[propName];
        var type = propInfo.type;
        var getter = EDIT_TAG_TYPES[type].get;
        var domItem = document.getElementById('edit-tag-' + propName);
        var multiCheckBoxDom = document.getElementById('edit-tag-multi-' + propName);
        if (multiCheckBoxDom.checked && propInfo.write) {
          props[propName] = getter(domItem);
        }
      }
    }
    player.sendCommand('updateTags', cmd);
  }

  function saveAndClose() {
    save();
    closeDialog();
  }

  function closeDialog() {
    $editTagsDialog.dialog('close');
  }
}

function updateSliderUi(value){
  var percent = value * 100;
  $trackSlider.css('background-size', percent + "% 100%");
}

function setUpNowPlayingUi(){
  var actions = {
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
  for (var cls in actions) {
    var action = actions[cls];
    setUpMouseDownListener(cls, action);
  }
  $trackSlider.slider({
    step: 0.0001,
    min: 0,
    max: 1,
    change: function(event, ui){
      updateSliderUi(ui.value);
      if (event.originalEvent == null) {
        return;
      }
      if (!player.currentItem) return;
      player.seek(null, ui.value * player.currentItem.track.duration);
    },
    slide: function(event, ui){
      updateSliderUi(ui.value);
      if (!player.currentItem) return;
      $nowplaying_elapsed.html(formatTime(ui.value * player.currentItem.track.duration));
    },
    start: function(event, ui){
      userIsSeeking = true;
    },
    stop: function(event, ui){
      userIsSeeking = false;
    }
  });
  function setVol(event, ui){
    if (event.originalEvent == null) return;
    player.setVolume(ui.value);
  }
  $volSlider.slider({
    step: 0.01,
    min: 0,
    max: 1,
    change: setVol,
    start: function(event, ui){
      userIsVolumeSliding = true;
    },
    stop: function(event, ui){
      userIsVolumeSliding = false;
    }
  });
  setInterval(updateSliderPos, 100);
  function setUpMouseDownListener(cls, action){
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
  tabs = ['library', 'upload', 'settings'];
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

function uploadFiles(files) {
  if (files.length === 0) return;

  var formData = new FormData();

  for (var i = 0; i < files.length; i += 1) {
    var file = files[i];
    formData.append("file", file);
  }

  var $progressBar = $('<div></div>');
  $progressBar.progressbar();
  var $cancelBtn = $('<button>Cancel</button>');
  $cancelBtn.on('click', onCancel);

  $uploadWidget.append($progressBar);
  $uploadWidget.append($cancelBtn);

  var req = new XMLHttpRequest();
  req.upload.addEventListener('progress', onProgress, false);
  req.addEventListener('load', onLoad, false);
  req.open('POST', '/upload');
  req.send(formData);
  uploadInput.value = null;

  function onProgress(e) {
    if (!e.lengthComputable) return;
    var progress = e.loaded / e.total;
    $progressBar.progressbar("option", "value", progress * 100);
  }

  function onLoad(e) {
    if (localState.autoQueueUploads) {
      var keys = JSON.parse(this.response);
      // sort them the same way the library is sorted
      player.queueTracks(player.sortKeys(keys));
    }
    cleanup();
  }

  function onCancel() {
    req.abort();
    cleanup();
  }

  function cleanup() {
    $progressBar.remove();
    $cancelBtn.remove();
  }
}

function setAutoUploadBtnState() {
  $autoQueueUploads
    .button('option', 'label', localState.autoQueueUploads ? 'On' : 'Off')
    .prop('checked', localState.autoQueueUploads)
    .button('refresh');
}

function setUpUploadUi(){
  $autoQueueUploads.button({ label: "..." });
  setAutoUploadBtnState();
  $autoQueueUploads.on('click', function(event) {
    var value = $(this).prop('checked');
    localState.autoQueueUploads = value;
    saveLocalState();
    setAutoUploadBtnState();
  });
  uploadInput.addEventListener('change', onChange, false);

  function onChange(e) {
    uploadFiles(this.files);
  }

  $uploadByUrl.on('keydown', function(event){
    event.stopPropagation();
    if (event.which === 27) {
      $uploadByUrl.val("").blur();
    } else if (event.which === 13) {
      importUrl();
    }
  });

  function importUrl() {
    var url = $uploadByUrl.val();
    var id = uuid();
    $uploadByUrl.val("").blur();
    socket.on('importUrl', onImportUrl);
    socket.send('importUrl', {
      url: url,
      id: id,
    });

    function onImportUrl(args) {
      if (args.id !== id) return;
      socket.removeListener('importUrl', onImportUrl);
      if (!args.key) return;
      if (localState.autoQueueUploads) {
        player.queueTracks([args.key]);
      }
    }
  }
}

function updateLastFmApiKey(key) {
  lastFmApiKey = key;
  updateLastFmSettingsUi();
}

function updateLastFmSettingsUi() {
  if (localState.lastfm.username) {
    $settingsLastFmIn.show();
    $settingsLastFmOut.hide();
  } else {
    $settingsLastFmIn.hide();
    $settingsLastFmOut.show();
  }
  settingsLastFmUserDom.setAttribute('href', "http://last.fm/user/" +
      encodeURIComponent(localState.lastfm.username));
  settingsLastFmUserDom.textContent = localState.lastfm.username;
  var authUrl = "http://www.last.fm/api/auth/?api_key=" +
        encodeURIComponent(lastFmApiKey) + "&cb=" +
        encodeURIComponent(location.protocol + "//" + location.host + "/");
  lastFmAuthUrlDom.setAttribute('href', authUrl);
  $toggleScrobble
    .button('option', 'label', localState.lastfm.scrobbling_on ? 'On' : 'Off')
    .prop('checked', localState.lastfm.scrobbling_on)
    .button('refresh');
}

function updateSettingsAuthUi() {
  var showEdit = !!(localState.authPassword == null || settings_ui.auth.show_edit);
  $settingsEditPassword.toggle(showEdit);
  $settingsShowPassword.toggle(!showEdit);
  $settingsAuthCancel.toggle(!!localState.authPassword);
  $authPassword.val(localState.authPassword);
  passwordDisplayDom.textContent = localState.authPassword;
  $authPermRead.toggle(!!permissions.read);
  $authPermAdd.toggle(!!permissions.add);
  $authPermControl.toggle(!!permissions.control);
  $authPermAdmin.toggle(!!permissions.admin);
  streamUrlDom.setAttribute('href', streaming.getUrl());
}

function setUpSettingsUi(){
  $toggleScrobble.button();
  $lastFmSignOut.button();
  $settingsAuthCancel.button();
  $settingsAuthSave.button();
  $settingsAuthEdit.button();
  $settingsAuthClear.button();

  $lastFmSignOut.on('click', function(event) {
    localState.lastfm.username = null;
    localState.lastfm.session_key = null;
    localState.lastfm.scrobbling_on = false;
    saveLocalState();
    updateLastFmSettingsUi();
    return false;
  });
  $toggleScrobble.on('click', function(event) {
    var msg;
    var value = $(this).prop("checked");
    if (value) {
      msg = 'LastFmScrobblersAdd';
      localState.lastfm.scrobbling_on = true;
    } else {
      msg = 'LastFmScrobblersRemove';
      localState.lastfm.scrobbling_on = false;
    }
    saveLocalState();
    var params = {
      username: localState.lastfm.username,
      session_key: localState.lastfm.session_key
    };
    socket.send(msg, params);
    updateLastFmSettingsUi();
  });
  $settingsAuthEdit.on('click', function(event) {
    settings_ui.auth.show_edit = true;
    updateSettingsAuthUi();
    $authPassword
      .focus()
      .val(localState.authPassword || "")
      .select();
  });
  $settingsAuthClear.on('click', function(event) {
    localState.authPassword = null;
    saveLocalState();
    settings_ui.auth.password = "";
    updateSettingsAuthUi();
  });
  $settingsAuthSave.on('click', function(event){
    settingsAuthSave();
  });
  $settingsAuthCancel.on('click', function(event) {
    settingsAuthCancel();
  });
  $authPassword.on('keydown', function(event) {
    event.stopPropagation();
    settings_ui.auth.password = $authPassword.val();
    if (event.which === 27) {
      settingsAuthCancel();
    } else if (event.which === 13) {
      settingsAuthSave();
    }
  });
  $authPassword.on('keyup', function(event) {
    settings_ui.auth.password = $authPassword.val();
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
  $libraryMenu.menu();
  $libraryMenu.on('mousedown', function(){
    return false;
  });
  $libraryMenu.on('click', '.queue', function(){
    player.queueTracks(selection.toTrackKeys());
    removeContextMenu();
    return false;
  });
  $libraryMenu.on('click', '.queue-next', function(){
    player.queueTracksNext(selection.toTrackKeys());
    removeContextMenu();
    return false;
  });
  $libraryMenu.on('click', '.queue-random', function(){
    player.queueTracks(selection.toTrackKeys(true));
    removeContextMenu();
    return false;
  });
  $libraryMenu.on('click', '.queue-next-random', function(){
    player.queueTracksNext(selection.toTrackKeys(true));
    removeContextMenu();
    return false;
  });
  $libraryMenu.on('click', '.download', onDownloadContextMenu);
  $libraryMenu.on('click', '.delete', onDeleteContextMenu);
  $libraryMenu.on('click', '.edit-tags', onEditTagsContextMenu);
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
    $(document.activeElement).blur();
    var $this = $(this);
    var type = $this.attr('data-type');
    var key = $this.attr('data-key');
    if (event.which === 1) {
      leftMouseDown(event);
    } else if (event.which === 3) {
      if (event.altKey) {
        return;
      }
      rightMouseDown(event);
    }
    function leftMouseDown(event){
      event.preventDefault();
      removeContextMenu();
      var skipDrag = false;
      if (!options.isSelectionOwner()) {
        selection.selectOnly(type, key);
      } else if (event.ctrlKey || event.shiftKey) {
        skipDrag = true;
        selection.cursor = key;
        selection.type = type;
        if (!event.shiftKey && !event.ctrlKey) {
          selection.selectOnly(type, key);
        } else if (event.shiftKey) {
          selectTreeRange();
        } else if (event.ctrlKey) {
          toggleSelectionUnderCursor();
        }
      } else if (selection.ids[type][key] == null) {
        selection.selectOnly(type, key);
      }
      refreshSelection();
      if (!skipDrag) {
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
      event.preventDefault();
      removeContextMenu();
      if (!options.isSelectionOwner() || selection.ids[type][key] == null) {
        selection.selectOnly(type, key);
        refreshSelection();
      }
      var track = null;
      if (!selection.isMulti()) {
        if (type === 'track') {
          track = player.searchResults.trackTable[key];
        } else if (type === 'stored_playlist_item') {
          track = player.stored_playlist_item_table[key].track;
        }
      }
      if (track) {
        $libraryMenu.find('.download').attr('href', 'library/' + encodeURI(track.file));
      } else {
        $libraryMenu.find('.download').attr('href', '#');
      }
      $libraryMenu.show().offset({
        left: event.pageX + 1,
        top: event.pageY + 1
      });
      updateAdminActions($libraryMenu);
    }
  });
  $elem.on('mousedown', function(){
    return false;
  });
}
function updateAdminActions($menu) {
  if (!permissions.admin) {
    $menu.find('.delete,.edit-tags')
      .addClass('ui-state-disabled')
      .attr('title', "Insufficient privileges. See Settings.");
  } else {
    $menu.find('.delete,.edit-tags')
      .removeClass('ui-state-disabled')
      .attr('title', '');
  }
}
function setUpUi(){
  setUpGenericUi();
  setUpPlaylistUi();
  setUpLibraryUi();
  setUpNowPlayingUi();
  setUpTabsUi();
  setUpUploadUi();
  setUpSettingsUi();
  setUpEditTagsUi();
}

function toAlbumId(s) {
  return "lib-album-" + toHtmlId(s);
}

function toTrackId(s) {
  return "lib-track-" + toHtmlId(s);
}

function handleResize(){
  $nowplaying.width(MARGIN);
  $pl_window.height(MARGIN);
  $left_window.height(MARGIN);
  $library.height(MARGIN);
  $upload.height(MARGIN);
  $playlistItems.height(MARGIN);
  $nowplaying.width($document.width() - MARGIN * 2);
  var second_layer_top = $nowplaying.offset().top + $nowplaying.height() + MARGIN;
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
  var tab_contents_height = $left_window.height() - $tabs.height() - MARGIN;
  $library.height(tab_contents_height - $lib_header.height());
  $upload.height(tab_contents_height);
  $playlistItems.height($pl_window.height() - $pl_header.position().top - $pl_header.height());
}
function refreshPage(){
  location.href = location.protocol + "//" + location.host + "/";
}

$document.ready(function(){
  loadLocalState();
  socket = new Socket();
  var queryObj = querystring.parse(location.search.substring(1));
  if (queryObj.token) {
    socket.on('connect', function() {
      socket.send('LastFmGetSession', queryObj.token);
    });
    socket.on('LastFmGetSessionSuccess', function(params){
      localState.lastfm.username = params.session.name;
      localState.lastfm.session_key = params.session.key;
      localState.lastfm.scrobbling_on = false;
      saveLocalState();
      refreshPage();
    });
    socket.on('LastFmGetSessionError', function(message){
      alert("Error authenticating: " + message);
      refreshPage();
    });
    return;
  }
  socket.on('LastFmApiKey', updateLastFmApiKey);
  socket.on('permissions', function(data){
    permissions = data;
    updateSettingsAuthUi();
  });
  socket.on('volumeUpdate', function(vol) {
    player.volume = vol;
    renderVolumeSlider();
  });
  socket.on('dynamicModeOn', function(data) {
    dynamicModeOn = data;
    renderPlaylistButtons();
    renderPlaylist();
  });
  player = new PlayerClient(socket);
  player.on('libraryupdate', renderLibrary);
  player.on('playlistupdate', renderPlaylist);
  player.on('statusupdate', function(){
    renderNowPlaying();
    renderPlaylistButtons();
    labelPlaylistItems();
  });
  socket.on('connect', function(){
    socket.send('subscribe', {name: 'dynamicModeOn'});
    sendAuth();
    load_status = LoadStatus.GoodToGo;
    render();
  });
  socket.on('disconnect', function(){
    load_status = LoadStatus.NoServer;
    render();
  });
  setUpUi();
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
  if (seconds == null) return "";
  var sign = "";
  if (seconds < 0) {
    sign = "-";
    seconds = -seconds;
  }
  seconds = Math.floor(seconds);
  var minutes = Math.floor(seconds / 60);
  seconds -= minutes * 60;
  var hours = Math.floor(minutes / 60);
  minutes -= hours * 60;
  if (hours !== 0) {
    return sign + hours + ":" + zfill(minutes, 2) + ":" + zfill(seconds, 2);
  } else {
    return sign + minutes + ":" + zfill(seconds, 2);
  }
}

var badCharRe = new RegExp('[^a-zA-Z0-9-]', 'gm');
function toHtmlId(string) {
  return string.replace(badCharRe, function(c) {
    return "_" + c.charCodeAt(0) + "_";
  });
}
