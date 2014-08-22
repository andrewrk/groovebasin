var $ = window.$;

var shuffle = require('mess');
var querystring = require('querystring');
var PlayerClient = require('./playerclient');
var streaming = require('./streaming');
var Socket = require('./socket');
var uuid = require('./uuid');

var dynamicModeOn = false;
var hardwarePlaybackOn = false;
var haveAdminUser = true;
var approvedUsers = null;
var sortedApprovedUsers = null;
var approvalRequests = null;
var sortedApprovalRequests = null;

var downloadMenuZipName = null;

var selection = {
  ids: {
    queue: {},
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
  isQueue: function(){
    return this.type === 'queue';
  },
  isStoredPlaylist: function(){
    return this.type === 'stored_playlist' || this.type === 'stored_playlist_item';
  },
  clear: function(){
    this.ids.artist = {};
    this.ids.album = {};
    this.ids.track = {};
    this.ids.queue = {};
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
    } else if (this.isQueue()) {
      result = 2;
      for (k in this.ids.queue) {
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
    } else if (this.isQueue()) {
      return queueToTrackKeys();
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
    function queueToTrackKeys(){
      var keys = [];
      for (var key in selection.ids.queue) {
        keys.push(player.queue.itemTable[key].track.key);
      }
      if (random) shuffle(keys);
      return keys;
    }
    function storedPlaylistToTrackKeys(){
      var track_set = {};
      function renderQueue(playlist){
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
        renderQueue(player.stored_playlist_table[key]);
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
var AUTO_EXPAND_LIMIT = 30;
var ICON_COLLAPSED = 'ui-icon-triangle-1-e';
var ICON_EXPANDED = 'ui-icon-triangle-1-se';
var GUEST_USER_ID = "(guest)";
var myUser = {
  perms: {},
};
var socket = null;
var player = null;
var userIsSeeking = false;
var userIsVolumeSliding = false;
var started_drag = false;
var abortDrag = function(){};
var lastFmApiKey = null;
var LoadStatus = {
  Init: 'Loading...',
  NoServer: 'Server is down.',
  GoodToGo: '[good to go]'
};
var repeatModeNames = ["Off", "One", "All"];
var load_status = LoadStatus.Init;

var localState = {
  lastfm: {
    username: null,
    session_key: null,
    scrobbling_on: false
  },
  authUsername: null,
  authPassword: null,
  autoQueueUploads: true,
};
var $document = $(document);
var $window = $(window);
var $queueWindow = $('#queue-window');
var $leftWindow = $('#left-window');
var $queueItems = $('#queue-items');
var $dynamicMode = $('#dynamic-mode');
var $queueBtnRepeat = $('#queue-btn-repeat');
var $tabs = $('#tabs');
var $library = $('#library');
var $libFilter = $('#lib-filter');
var $trackSlider = $('#track-slider');
var $nowplaying = $('#nowplaying');
var $nowplaying_elapsed = $nowplaying.find('.elapsed');
var $nowplaying_left = $nowplaying.find('.left');
var $volSlider = $('#vol-slider');
var $settings = $('#settings');
var $uploadByUrl = $('#upload-by-url');
var $mainErrMsg = $('#main-err-msg');
var $mainErrMsgText = $('#main-err-msg-text');
var $playlistsList = $('#playlists-list');
var $playlists = $('#playlists');
var $upload = $('#upload');
var $trackDisplay = $('#track-display');
var $libHeader = $('#lib-window-header');
var $queueHeader = $('#queue-header');
var $autoQueueUploads = $('#auto-queue-uploads');
var uploadInput = document.getElementById("upload-input");
var $uploadWidget = $("#upload-widget");
var $settingsRegister = $('#settings-register');
var $settingsShowAuth = $('#settings-show-auth');
var $settingsAuthCancel = $('#settings-auth-cancel');
var $settingsAuthSave = $('#settings-auth-save');
var $settingsAuthEdit = $('#settings-auth-edit');
var $settingsAuthRequest = $('#settings-auth-request');
var $settingsAuthLogout = $('#settings-auth-logout');
var streamUrlDom = document.getElementById('settings-stream-url');
var $authPermRead = $('#auth-perm-read');
var $authPermAdd = $('#auth-perm-add');
var $authPermControl = $('#auth-perm-control');
var $authPermAdmin = $('#auth-perm-admin');
var $lastFmSignOut = $('#lastfm-sign-out');
var lastFmAuthUrlDom = document.getElementById('lastfm-auth-url');
var $settingsLastFmIn = $('#settings-lastfm-in');
var $settingsLastFmOut = $('#settings-lastfm-out');
var settingsLastFmUserDom = document.getElementById('settings-lastfm-user');
var $toggleScrobble = $('#toggle-scrobble');
var $shortcuts = $('#shortcuts');
var $editTagsDialog = $('#edit-tags');
var $queueMenu = $('#menu-queue');
var $libraryMenu = $('#menu-library');
var $toggleHardwarePlayback = $('#toggle-hardware-playback');
var $newPlaylistBtn = $('#new-playlist-btn');
var $emptyLibraryMessage = $('#empty-library-message');
var $libraryNoItems = $('#library-no-items');
var $libraryArtists = $('#library-artists');
var $volNum = $('#vol-num');
var $volWarning = $('#vol-warning');
var $ensureAdminDiv = $('#ensure-admin');
var $ensureAdminBtn = $('#ensure-admin-btn');
var $authShowPassword = $('#auth-show-password');
var $authUsername = $('#auth-username');
var $authUsernameDisplay = $('#auth-username-display');
var $authPassword = $('#auth-password');
var $settingsUsers = $('#settings-users');
var $settingsUsersSelect = $('#settings-users-select');
var $settingsRequests = $('#settings-requests');
var $settingsRequest = $('#settings-request');
var $userPermRead = $('#user-perm-read');
var $userPermAdd = $('#user-perm-add');
var $userPermControl = $('#user-perm-control');
var $userPermAdmin = $('#user-perm-admin');
var $settingsDeleteUser = $('#settings-delete-user');
var $requestReplace = $('#request-replace');
var $requestName = $('#request-name');
var $requestApprove = $('#request-approve');
var $requestDeny = $('#request-deny');

var tabs = {
  library: {
    $pane: $('#library-pane'),
    $tab: $('#library-tab'),
  },
  upload: {
    $pane: $('#upload-pane'),
    $tab: $('#upload-tab'),
  },
  playlists: {
    $pane: $('#playlists-pane'),
    $tab: $('#playlists-tab'),
  },
  settings: {
    $pane: $('#settings-pane'),
    $tab: $('#settings-tab'),
  },
};

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

function scrollLibraryToSelection() {
  var helpers = getSelectionHelpers();
  if (!helpers) return;
  delete helpers.queue;
  scrollThingToSelection($library, helpers);
}

function scrollPlaylistToSelection(){
  var helpers = getSelectionHelpers();
  if (!helpers) return;
  delete helpers.track;
  delete helpers.artist;
  delete helpers.album;
  scrollThingToSelection($queueItems, helpers);
}

function scrollThingToSelection($scrollArea, helpers){
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
    var scrollAreaTop = $scrollArea.offset().top;
    var selectionTop = topPos - scrollAreaTop;
    var selectionBottom = bottomPos - scrollAreaTop - $scrollArea.height();
    var scrollAmt = $scrollArea.scrollTop();
    if (selectionTop < 0) {
      return $scrollArea.scrollTop(scrollAmt + selectionTop);
    } else if (selectionBottom > 0) {
      return $scrollArea.scrollTop(scrollAmt + selectionBottom);
    }
  }
}

function downloadKeys(keys, zipName) {
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
  var $zipNameInput = $(document.createElement('input'));
  $zipNameInput.attr('type', 'hidden');
  $zipNameInput.attr('name', 'zipName');
  $zipNameInput.attr('value', zipName);
  $form.append($zipNameInput);

  $form.submit();
}

function getDragPosition(x, y){
  var ref$;
  var result = {};
  for (var i = 0, len$ = (ref$ = $queueItems.find(".pl-item").get()).length; i < len$; ++i) {
    var item = ref$[i];
    var $item = $(item);
    var middle = $item.offset().top + $item.height() / 2;
    var track = player.queue.itemTable[$item.attr('data-id')];
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
  $queueBtnRepeat
    .button("option", "label", "Repeat: " + repeatModeName)
    .prop("checked", player.repeat !== PlayerClient.REPEAT_OFF)
    .button("refresh");
}

function updateHaveAdminUserUi() {
  $ensureAdminDiv.toggle(!haveAdminUser);
}

function renderQueue(){
  var itemList = player.queue.itemList || [];
  var scrollTop = $queueItems.scrollTop();

  // add the missing dom entries
  var i;
  var playlistItemsDom = $queueItems.get(0);
  for (i = playlistItemsDom.childElementCount; i < itemList.length; i += 1) {
    $queueItems.append(
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
  var $domItems = $queueItems.children();
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
    var timeText = player.isScanning(track) ? "scan" : formatTime(track.duration);
    $domItem.find('.time').text(timeText);
  }

  refreshSelection();
  labelPlaylistItems();
  $queueItems.scrollTop(scrollTop);
}

function labelPlaylistItems() {
  var item;
  var curItem = player.currentItem;
  $queueItems.find(".pl-item")
    .removeClass('current')
    .removeClass('old')
    .removeClass('random');
  if (curItem != null && dynamicModeOn) {
    for (var index = 0; index < curItem.index; ++index) {
      item = player.queue.itemList[index];
      var itemId = item && item.id;
      if (itemId != null) {
        $("#playlist-track-" + itemId).addClass('old');
      }
    }
  }
  for (var i = 0; i < player.queue.itemList.length; i += 1) {
    item = player.queue.itemList[i];
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
  if (player.queue == null) return null;
  if (player.queue.itemTable == null) return null;
  if (player.searchResults == null) return null;
  if (player.searchResults.artistTable == null) return null;
  return {
    queue: {
      ids: selection.ids.queue,
      table: player.queue.itemTable,
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
  if (!helpers) return;
  $queueItems.find(".pl-item").removeClass('selected').removeClass('cursor');
  $libraryArtists.find(".clickable").removeClass('selected').removeClass('cursor');
  $playlistsList.find(".clickable").removeClass('selected').removeClass('cursor');
  if (selection.type == null) return;
  for (var selectionType in helpers) {
    var helper = helpers[selectionType];
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
    if (selection.cursor != null && selectionType === selection.type) {
      var validIds = getValidIds(selectionType);
      if (validIds[selection.cursor] == null) {
        // server just deleted our current cursor item.
        // select another of our ids randomly, if we have any.
        selection.cursor = Object.keys(helper.ids)[0];
        selection.rangeSelectAnchor = selection.cursor;
        selection.rangeSelectAnchorType = selectionType;
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

function getValidIds(selectionType) {
  switch (selectionType) {
    case 'queue':  return player.queue.itemTable;
    case 'artist': return player.library.artistTable;
    case 'album':  return player.library.albumTable;
    case 'track':  return player.library.trackTable;
    case 'stored_playlist':  return player.stored_playlist_table;
    case 'stored_playlist_item':  return player.stored_playlist_item_table;
  }
  throw new Error("BadSelectionType");
}

function artistId(s) {
  return "lib-artist-" + toHtmlId(s);
}

function artistDisplayName(name) {
  return name || '[Unknown Artist]';
}

var triggerRenderLibrary = makeRenderCall(renderLibrary, 100);
var triggerRenderQueue = makeRenderCall(renderQueue, 100);
var triggerPlaylistsUpdate = makeRenderCall(renderPlaylists, 100);

function makeRenderCall(renderFn, interval) {
  var renderTimeout = null;
  var renderWanted = false;

  return ensureRenderHappensSoon;

  function ensureRenderHappensSoon() {
    if (renderTimeout) {
      renderWanted = true;
      return;
    }

    renderFn();
    renderWanted = false;
    renderTimeout = setTimeout(checkRender, interval);
  }

  function checkRender() {
    renderTimeout = null;
    if (renderWanted) {
      ensureRenderHappensSoon();
    }
  }
}

function renderPlaylists() {
  var playlistList = player.stored_playlists;
  var scrollTop = $playlists.scrollTop();

  // add the missing dom entries
  var i;
  var playlistListDom = $playlistsList.get(0);
  for (i = playlistListDom.childElementCount; i < playlistList.length; i += 1) {
    $playlistsList.append(
      '<li>' +
        '<div class="clickable expandable" data-type="stored_playlist">' +
          '<div class="ui-icon"></div>' +
          '<span></span>' +
        '</div>' +
        '<ul></ul>' +
      '</li>');
  }
  // remove the extra dom entries
  var domItem;
  while (playlistList.length < playlistListDom.childElementCount) {
    playlistListDom.removeChild(playlistListDom.lastChild);
  }

  // overwrite existing dom entries
  var playlist;
  var $domItems = $playlistsList.children();
  for (i = 0; i < playlistList.length; i += 1) {
    domItem = $domItems[i];
    playlist = playlistList[i];
    $(domItem).data('cached', false);
    var divDom = domItem.children[0];
    divDom.setAttribute('id', toStoredPlaylistId(playlist.id));
    divDom.setAttribute('data-key', playlist.id);
    var iconDom = divDom.children[0];
    $(iconDom)
      .addClass(ICON_COLLAPSED)
      .removeClass(ICON_EXPANDED);
    var spanDom = divDom.children[1];
    spanDom.textContent = playlist.name;
    var ulDom = domItem.children[1];
    ulDom.style.display = 'block';
    while (ulDom.firstChild) {
      ulDom.removeChild(ulDom.firstChild);
    }
  }

  $playlists.scrollTop(scrollTop);
  refreshSelection();
  // TODO expandPlaylistsToSelection()
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
  expandLibraryToSelection();

  function expandStuff($liSet) {
    if (nodeCount >= AUTO_EXPAND_LIMIT) return;
    for (var i = 0; i < $liSet.length; i += 1) {
      var li = $liSet[i];
      var $li = $(li);
      var $ul = $li.children("ul");
      var $subLiSet = $ul.children("li");
      var proposedNodeCount = nodeCount + $subLiSet.length;
      if (proposedNodeCount <= AUTO_EXPAND_LIMIT) {
        toggleLibraryExpansion($li);
        $ul = $li.children("ul");
        $subLiSet = $ul.children("li");
        nodeCount = proposedNodeCount;
        expandStuff($subLiSet);
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

  $volSlider.slider('option', 'value', player.volume);
  $volNum.text(Math.round(player.volume * 100));
  $volWarning.toggle(player.volume > 1);
}

function renderNowPlaying() {
  var track = null;
  if (player.currentItem != null) {
    track = player.currentItem.track;
  }
  var trackDisplay;
  if (track != null) {
    trackDisplay = track.name + " - " + track.artistName;
    if (track.albumName.length) {
      trackDisplay += " - " + track.albumName;
    }
    document.title = trackDisplay + " - " + BASE_TITLE;
    if (/Groove Basin/.test(track.name)) {
      $("html").addClass('groovebasin');
    } else {
      $("html").removeClass('groovebasin');
    }
    if (/Never Gonna Give You Up/.test(track.name) && /Rick Astley/.test(track.artistName)) {
      $("html").addClass('nggyu');
    } else {
      $("html").removeClass('nggyu');
    }
  } else {
    trackDisplay = "&nbsp;";
    document.title = BASE_TITLE;
  }
  $trackDisplay.html(trackDisplay);
  var oldClass;
  var newClass;
  if (player.isPlaying === true) {
    oldClass = 'ui-icon-play';
    newClass = 'ui-icon-pause';
  } else {
    oldClass = 'ui-icon-pause';
    newClass = 'ui-icon-play';
  }
  $nowplaying.find(".toggle span").removeClass(oldClass).addClass(newClass);
  $trackSlider.slider("option", "disabled", player.isPlaying == null);
  updateSliderPos();
  renderVolumeSlider();
}

function render(){
  var hide_main_err = load_status === LoadStatus.GoodToGo;
  $queueWindow.toggle(hide_main_err);
  $leftWindow.toggle(hide_main_err);
  $nowplaying.toggle(hide_main_err);
  $mainErrMsg.toggle(!hide_main_err);
  if (!hide_main_err) {
    document.title = BASE_TITLE;
    $mainErrMsgText.text(load_status);
    return;
  }
  renderQueue();
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

function renderPlaylist($ul, playlist) {
  playlist.itemList.forEach(function(item) {
    debugger;
    $ul.append(
      '<li>' +
        '<div class="clickable" data-type="stored_playlist_item">' +
          '<span></span>' +
        '</div>' +
      '</li>');
    var liDom = $ul.get(0).lastChild;
    var divDom = liDom.children[0];
    divDom.setAttribute('id', toStoredPlaylistItemId(item.id));
    divDom.setAttribute('data-key', item.id);
    var spanDom = divDom.children[0];
    var track = item.track;
    var caption = track.artistName + " - " + track.name;
    spanDom.textContent = caption;
  });
}

function genericToggleExpansion($li, options) {
  var topLevelType = options.topLevelType;
  var renderDom = options.renderDom;
  var $div = $li.find("> div");
  var $ul = $li.find("> ul");
  if ($div.attr('data-type') === topLevelType) {
    if (!$li.data('cached')) {
      $li.data('cached', true);
      var key = $div.attr('data-key');
      renderDom($ul, key);

      $ul.toggle();
      refreshSelection();
    }
  }
  $ul.toggle();
  var oldClass = ICON_EXPANDED;
  var newClass = ICON_COLLAPSED;
  if ($ul.is(":visible")) {
    var tmp = oldClass;
    oldClass = newClass;
    newClass = tmp;
  }
  $div.find("div").removeClass(oldClass).addClass(newClass);
}

function toggleLibraryExpansion($li) {
  genericToggleExpansion($li, {
    topLevelType: 'artist',
    renderDom: function($ul, key) {
      var albumList = player.searchResults.artistTable[key].albumList;
      renderArtist($ul, albumList);
    },
  });
}

function togglePlaylistExpansion($li) {
  genericToggleExpansion($li, {
    topLevelType: 'stored_playlist',
    renderDom: function($ul, key) {
      var playlist = player.stored_playlist_table[key];
      renderPlaylist($ul, playlist);
    },
  });
}

function maybeDeleteTracks(keysList) {
  var fileList = keysList.map(function(key) {
    return player.library.trackTable[key].file;
  });
  var listText = fileList.slice(0, 7).join("\n  ");
  if (fileList.length > 7) {
    listText += "\n  ...";
  }
  var songText = fileList.length === 1 ? "song" : "songs";
  var message = "You are about to delete " + fileList.length + " " + songText + " permanently:\n\n  " + listText;
  if (!confirm(message)) return false;
  player.deleteTracks(keysList);
  return true;
}

function handleDeletePressed(shift) {
  var keysList;
  if (selection.isLibrary()) {
    keysList = selection.toTrackKeys();
    maybeDeleteTracks(keysList);
  } else if (selection.isStoredPlaylist()) {
    if (shift) {
      keysList = selection.toTrackKeys();
      maybeDeleteTracks(keysList);
    } else {
      maybeDeleteSelectedPlaylists();
    }
  } else if (selection.isQueue()) {
    if (shift) {
      keysList = [];
      for (var id in selection.ids.queue) {
        keysList.push(player.queue.itemTable[id].track.key);
      }
      if (!maybeDeleteTracks(keysList)) return;
    }
    var sortKey = player.queue.itemTable[selection.cursor].sortKey;
    player.removeIds(Object.keys(selection.ids.queue));
    var item = null;
    for (var i = 0; i < player.queue.itemList.length; i++) {
      item = player.queue.itemList[i];
      if (item.sortKey > sortKey) {
        // select the very next one
        break;
      }
      // if we deleted the last item, select the new last item.
    }
    // if there's no items, select nothing.
    if (item != null) {
      selection.selectOnly('queue', item.id);
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
      defaultIndex = player.currentItem ? player.currentItem.index - 1 : player.queue.itemList.length - 1;
      dir = -1;
    } else {
      // down
      defaultIndex = player.currentItem ? player.currentItem.index + 1 : 0;
      dir = 1;
    }
    if (defaultIndex >= player.queue.itemList.length) {
      defaultIndex = player.queue.itemList.length - 1;
    } else if (defaultIndex < 0) {
      defaultIndex = 0;
    }
    if (event.altKey) {
      if (selection.isQueue()) {
        player.shiftIds(selection.ids.queue, dir);
      }
    } else {
      if (selection.isQueue()) {
        nextPos = player.queue.itemTable[selection.cursor].index + dir;
        if (nextPos < 0 || nextPos >= player.queue.itemList.length) {
          return;
        }
        selection.cursor = player.queue.itemList[nextPos].id;
        if (!event.ctrlKey && !event.shiftKey) {
          // single select
          selection.clear();
          selection.ids.queue[selection.cursor] = true;
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
        if (player.queue.itemList.length === 0) return;
        selection.selectOnly('queue', player.queue.itemList[defaultIndex].id);
      }
      refreshSelection();
    }
    if (selection.isQueue()) scrollPlaylistToSelection();
    if (selection.isLibrary()) scrollLibraryToSelection();
  }
  function leftRightHandler(event){
    var dir = event.which === 37 ? -1 : 1;
    if (selection.isLibrary()) {
      var helpers = getSelectionHelpers();
      if (!helpers) return;
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
        bumpVolume(-0.1);
      }
  };
  var volumeUpHandler = {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(){
        bumpVolume(0.1);
      }
  };
  return {
    // Enter
    13: {
      ctrl: false,
      alt: null,
      shift: null,
      handler: function(event){
        if (selection.isQueue()) {
          player.seek(selection.cursor, 0);
          player.play();
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
        clickTab(tabs.library);
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
    // i
    73: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: function(){
        clickTab(tabs.upload);
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
          clickTab(tabs.library);
          $libFilter.focus().select();
        }
      },
    },
  };
})();

function bumpVolume(v) {
  if (streaming.getTryingToStream()) {
    streaming.setVolume(streaming.getVolume() + v);
  } else {
    player.setVolume(player.volume + v);
  }
}

function removeContextMenu() {
  if ($queueMenu.is(":visible")) {
    $queueMenu.hide();
    return true;
  }
  if ($libraryMenu.is(":visible")) {
    $libraryMenu.hide();
    return true;
  }
  return false;
}

function isArtistExpanded(artist){
  var artistHtmlId = artistId(artist.key);
  var artistElem = document.getElementById(artistHtmlId);
  var $li = $(artistElem).closest('li');
  if (!$li.data('cached')) return false;
  return $li.find("> ul").is(":visible");
}

function expandArtist(artist) {
  if (isArtistExpanded(artist)) return;

  var artistElem = document.getElementById(artistId(artist.key));
  var $li = $(artistElem).closest('li');
  toggleLibraryExpansion($li);
}

function isAlbumExpanded(album){
  var albumElem = document.getElementById(toAlbumId(album.key));
  var $li = $(albumElem).closest('li');
  return $li.find("> ul").is(":visible");
}

function expandAlbum(album) {
  if (isAlbumExpanded(album)) return;

  expandArtist(album.artist);
  var elem = document.getElementById(toAlbumId(album.key));
  var $li = $(elem).closest('li');
  toggleLibraryExpansion($li);
}

function expandLibraryToSelection() {
  if (!selection.isLibrary()) return;
  for (var trackKey in selection.ids.track) {
    var track = player.library.trackTable[trackKey];
    expandAlbum(track.album);
  }
  for (var albumKey in selection.ids.album) {
    var album = player.library.albumTable[albumKey];
    expandArtist(album.artist);
  }
  scrollLibraryToSelection();
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
  var min_pos = player.queue.itemTable[anchor].index;
  var max_pos = player.queue.itemTable[selection.cursor].index;
  if (max_pos < min_pos) {
    var tmp = min_pos;
    min_pos = max_pos;
    max_pos = tmp;
  }
  for (var i = min_pos; i <= max_pos; i++) {
    selection.ids.queue[player.queue.itemList[i].id] = true;
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
  if (!localState.authPassword || !localState.authUsername) return;
  socket.send('login', {
    username: localState.authUsername,
    password: localState.authPassword,
  });
}

function settingsAuthSave() {
  localState.authUsername = $authUsername.val();
  localState.authPassword = $authPassword.val();
  saveLocalState();
  sendAuth();
  hideShowAuthEdit(false);
}

function settingsAuthCancel() {
  hideShowAuthEdit(false);
}

function hideShowAuthEdit(visible) {
  $settingsRegister.toggle(visible);
  $settingsShowAuth.toggle(!visible);
}

function performDrag(event, callbacks){
  abortDrag();
  var start_drag_x = event.pageX;
  var start_drag_y = event.pageY;
  abortDrag = function(){
    $document.off('mousemove', onDragMove).off('mouseup', onDragEnd);
    if (started_drag) {
      $queueItems.find(".pl-item").removeClass('border-top').removeClass('border-bottom');
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
    $queueItems.find(".pl-item").removeClass('border-top').removeClass('border-bottom');
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
var plBtnRepeatLabel = document.getElementById('queue-btn-repeat-label');
function setUpPlayQueueUi() {
  $queueWindow.on('click', 'button.clear', function(event){
    player.clear();
  });
  $queueWindow.on('mousedown', 'button.clear', stopPropagation);

  $queueWindow.on('click', 'button.shuffle', function(){
    player.shuffle();
  });
  $queueWindow.on('mousedown', 'button.shuffle', stopPropagation);

  $queueBtnRepeat.on('click', nextRepeatState);
  plBtnRepeatLabel.addEventListener('mousedown', stopPropagation, false);

  $dynamicMode.on('click', function(){
    var value = $(this).prop("checked");
    setDynamicMode(value);
    return false;
  });
  dynamicModeLabel.addEventListener('mousedown', stopPropagation, false);

  $queueItems.on('dblclick', '.pl-item', function(event){
    var trackId = $(this).attr('data-id');
    player.seek(trackId, 0);
    player.play();
  });
  $queueItems.on('contextmenu', function(event){
    return event.altKey;
  });
  $queueItems.on('mousedown', '.pl-item', function(event){
    var trackId, skipDrag;
    if (started_drag) return true;
    $(document.activeElement).blur();
    if (event.which === 1) {
      event.preventDefault();
      removeContextMenu();
      trackId = $(this).attr('data-id');
      skipDrag = false;
      if (!selection.isQueue()) {
        selection.selectOnly('queue', trackId);
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
      } else if (selection.ids.queue[trackId] == null) {
        selection.selectOnly('queue', trackId);
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
              for (var id in selection.ids.queue) {
                results$.push(id);
              }
              return results$;
            })(), result.previous_key, result.next_key);
          },
          cancel: function(){
            selection.selectOnly('queue', trackId);
            refreshSelection();
          }
        });
      }
    } else if (event.which === 3) {
      if (event.altKey) return;
      event.preventDefault();
      removeContextMenu();
      trackId = $(this).attr('data-id');
      if (!selection.isQueue() || selection.ids.queue[trackId] == null) {
        selection.selectOnly('queue', trackId);
        refreshSelection();
      }
      if (!selection.isMulti()) {
        var item = player.queue.itemTable[trackId];
        downloadMenuZipName = null;
        $queueMenu.find('.download').attr('href', encodeDownloadHref(item.track.file));
      } else {
        downloadMenuZipName = "songs";
        $queueMenu.find('.download').attr('href', '#');
      }
      $queueMenu.show().offset({
        left: event.pageX + 1,
        top: event.pageY + 1
      });
      updateMenuDisableState($queueMenu);
    }
  });
  $queueItems.on('mousedown', function(){
    return false;
  });
  $queueMenu.menu();
  $queueMenu.on('mousedown', function(){
    return false;
  });
  $queueMenu.on('click', '.remove', function(){
    handleDeletePressed(false);
    removeContextMenu();
    return false;
  });
  $queueMenu.on('click', '.download', onDownloadContextMenu);
  $queueMenu.on('click', '.delete', onDeleteContextMenu);
  $queueMenu.on('click', '.edit-tags', onEditTagsContextMenu);
}

function niceDateString() {
  var now = new Date();
  var year = 1900 + now.getYear();
  var month = zfill(now.getMonth() + 1, 2);
  var day = zfill(now.getDate(), 2);
  return year + '-' + month + '-' + day;
}

function setUpPlaylistsUi() {
  $newPlaylistBtn.on('click', function(event) {
    player.createPlaylist("New Playlist " + niceDateString());
  });
  genericTreeUi($playlistsList, {
    toggleExpansion: togglePlaylistExpansion,
    isSelectionOwner: function() {
      return selection.isStoredPlaylist();
    },
  });
}

function stopPropagation(event) {
  event.stopPropagation();
}

function onDownloadContextMenu() {
  removeContextMenu();

  if (downloadMenuZipName) {
    downloadKeys(selection.toTrackKeys(), downloadMenuZipName);
    return false;
  }

  return true;
}
function onDeleteContextMenu() {
  if (!havePerm('admin')) return false;
  removeContextMenu();
  handleDeletePressed(true);
  return false;
}
var editTagsTrackKeys = null;
var editTagsTrackIndex = null;
function onEditTagsContextMenu() {
  if (!havePerm('admin')) return false;
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
    var snap = 0.05;
    var val = ui.value;
    if (Math.abs(val - 1) < snap) {
      val = 1;
    }
    player.setVolume(val);
    $volNum.text(Math.round(val * 100));
    $volWarning.toggle(val > 1);
  }
  $volSlider.slider({
    step: 0.01,
    min: 0,
    max: 2,
    change: setVol,
    slide: setVol,
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

function clickTab(tab) {
  unselectTabs();
  tab.$tab.addClass('ui-state-active');
  tab.$pane.show();
  handleResize();
}

function setUpTabListener(tab) {
  tab.$tab.on('click', function(event) {
    clickTab(tab);
  });
}

function setUpTabsUi() {
  for (var name in tabs) {
    var tab = tabs[name];
    setUpTabListener(tab);
  }
}

function unselectTabs() {
  for (var name in tabs) {
    var tab = tabs[name];
    tab.$tab.removeClass('ui-state-active');
    tab.$pane.hide();
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
  $authPermRead.toggle(havePerm('read'));
  $authPermAdd.toggle(havePerm('add'));
  $authPermControl.toggle(havePerm('control'));
  $authPermAdmin.toggle(havePerm('admin'));
  streamUrlDom.setAttribute('href', streaming.getUrl());
  $settingsAuthRequest.toggle(myUser.registered && !myUser.requested && !myUser.approved);
  $settingsAuthLogout.toggle(myUser.registered);
  $settingsAuthEdit.button('option', 'label', myUser.registered ? 'Edit' : 'Register');
  $settingsUsers.toggle(havePerm('admin'));
  $settingsRequests.toggle(havePerm('admin') &&
      sortedApprovalRequests && sortedApprovalRequests.length > 0);

  var i, user;
  if (sortedApprovedUsers) {
    var selectedUserId = $settingsUsersSelect.val();
    $settingsUsersSelect.empty();
    for (i = 0; i < sortedApprovedUsers.length; i += 1) {
      user = sortedApprovedUsers[i];
      $settingsUsersSelect.append($("<option/>", {
        value: user.id,
        text: user.name,
      }));
      selectedUserId = selectedUserId || user.id;
    }
    $settingsUsersSelect.val(selectedUserId);
    updatePermsForSelectedUser();
  }
  if (sortedApprovalRequests) {
    var request = sortedApprovalRequests[0];
    $requestReplace.empty();
    for (i = 0; i < sortedApprovedUsers.length; i += 1) {
      user = sortedApprovedUsers[i];
      if (user.id === GUEST_USER_ID) {
        user = request;
      }
      $requestReplace.append($("<option/>", {
        value: user.id,
        text: user.name,
      }));
    }
    $requestReplace.val(request.id);
    $requestName.val(request.name);
  }
}

function sortApprovedUsers() {
  if (!approvedUsers) {
    sortedApprovedUsers = null;
    return;
  }
  sortedApprovedUsers = [];
  for (var id in approvedUsers) {
    var user = approvedUsers[id];
    user.id = id;
    sortedApprovedUsers.push(user);
  }
  sortedApprovedUsers.sort(compareUserNames);
}

function sortApprovalRequests() {
  if (!approvalRequests) {
    sortedApprovalRequests = null;
    return;
  }
  sortedApprovalRequests = [];
  for (var id in approvalRequests) {
    var user = approvalRequests[id];
    user.id = id;
    sortedApprovalRequests.push(user);
  }
  sortedApprovalRequests.sort(compareUserNames);
}

function compareUserNames(a, b) {
  var lowerA = a.name.toLowerCase();
  var lowerB = b.name.toLowerCase();
  if (a.id === GUEST_USER_ID) {
    return -1;
  } else if (b.id === GUEST_USER_ID) {
    return 1;
  } else if (lowerA < lowerB) {
    return -1;
  } else if (lowerA > lowerB) {
    return 1;
  } else {
    return 0;
  }
}

function updateSettingsAdminUi() {
  $toggleHardwarePlayback
    .button('option', 'label', hardwarePlaybackOn ? 'On' : 'Off')
    .prop('checked', hardwarePlaybackOn)
    .button('refresh');
}

function setUpSettingsUi(){
  $toggleScrobble.button();
  $toggleHardwarePlayback.button();
  $lastFmSignOut.button();
  $settingsAuthCancel.button();
  $settingsAuthSave.button();
  $settingsAuthEdit.button();
  $settingsAuthLogout.button();
  $ensureAdminBtn.button();
  $settingsAuthRequest.button();
  $userPermRead.button();
  $userPermAdd.button();
  $userPermControl.button();
  $userPermAdmin.button();
  $settingsDeleteUser.button();

  $ensureAdminDiv.on('click', function(event) {
    socket.send('ensureAdminUser');
  });

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
  $toggleHardwarePlayback.on('click', function(event) {
    var value = $(this).prop('checked');
    socket.send('hardwarePlayback', value);
    updateSettingsAdminUi();
  });
  $settingsAuthEdit.on('click', function(event) {
    $authUsername.val(localState.authUsername);
    $authPassword.val(localState.authPassword);
    hideShowAuthEdit(true);
    $authUsername.focus().select();
  });
  $settingsAuthSave.on('click', function(event){
    settingsAuthSave();
  });
  $settingsAuthCancel.on('click', function(event) {
    settingsAuthCancel();
  });
  $authUsername.on('keydown', handleUserOrPassKeyDown);
  $authPassword.on('keydown', handleUserOrPassKeyDown);
  $authShowPassword.on('change', function(event) {
    var showPw = $authShowPassword.prop('checked');
    $authPassword.get(0).type = showPw ? 'text' : 'password';
  });
  $settingsAuthRequest.on('click', function(event) {
    socket.send('requestApproval');
    myUser.requested = true;
    updateSettingsAuthUi();
  });
  $settingsAuthLogout.on('click', function(event) {
    localState.authUsername = null;
    localState.authPassword = null;
    saveLocalState();
    socket.send('logout');
    myUser.registered = false;
    updateSettingsAuthUi();
  });
  $userPermRead.on('change', updateSelectedUserPerms);
  $userPermAdd.on('change', updateSelectedUserPerms);
  $userPermControl.on('change', updateSelectedUserPerms);
  $userPermAdmin.on('change', updateSelectedUserPerms);
  $settingsUsersSelect.on('change', updatePermsForSelectedUser);

  $settingsDeleteUser.on('click', function(event) {
    var selectedUserId = $settingsUsersSelect.val();
    socket.send('deleteUsers', [selectedUserId]);
  });

  $requestApprove.on('click', function(event) {
    handleApproveDeny(true);
  });
  $requestDeny.on('click', function(event) {
    handleApproveDeny(false);
  });
}

function handleApproveDeny(approved) {
  var request = sortedApprovalRequests[0];
  socket.send('approve', [{
    id: request.id,
    replaceId: $requestReplace.val(),
    approved: approved,
    name: $requestName.val(),
  }]);
}

function updatePermsForSelectedUser() {
  var selectedUserId = $settingsUsersSelect.val();
  var user = approvedUsers[selectedUserId];
  $userPermRead.prop('checked', user.perms.read).button('refresh');
  $userPermAdd.prop('checked', user.perms.add).button('refresh');
  $userPermControl.prop('checked', user.perms.control).button('refresh');
  $userPermAdmin.prop('checked', user.perms.admin).button('refresh');

  $settingsDeleteUser.prop('disabled', selectedUserId === GUEST_USER_ID).button('refresh');
}

function updateSelectedUserPerms(event) {
  socket.send('updateUser', {
    userId: $settingsUsersSelect.val(),
    perms: {
      read: $userPermRead.prop('checked'),
      add: $userPermAdd.prop('checked'),
      control: $userPermControl.prop('checked'),
      admin: $userPermAdmin.prop('checked'),
    },
  });
  return false;
}

function handleUserOrPassKeyDown(event) {
  event.stopPropagation();
  if (event.which === 27) {
    settingsAuthCancel();
  } else if (event.which === 13) {
    settingsAuthSave();
  }
}

var searchTimer = null;
function ensureSearchHappensSoon() {
  if (searchTimer != null) {
    clearTimeout(searchTimer);
  }
  // give the user a small timeout between key presses to finish typing.
  // otherwise, we might be bogged down displaying the search results for "a" or the like.
  searchTimer = setTimeout(function() {
    player.search($libFilter.val());
    searchTimer = null;
  }, 100);
}

function setUpLibraryUi(){
  $libFilter.on('keydown', function(event){
    var keys, i, ref$, len$, artist, j$, ref1$, len1$, album, k$, ref2$, len2$, track;
    event.stopPropagation();
    switch (event.which) {
    case 27: // Escape
      if ($(event.target).val().length === 0) {
        $(event.target).blur();
      } else {
        setTimeout(function(){
          $libFilter.val("");
          // queue up a search refresh now, because if the user holds Escape,
          // it will blur the search box, and we won't get a keyup for Escape.
          ensureSearchHappensSoon();
        }, 0);
      }
      return false;
    case 13: // Enter
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
      $libFilter.blur();
      return false;
    case 38:
      selection.selectOnly('artist', player.searchResults.artistList[player.searchResults.artistList.length - 1].key);
      refreshSelection();
      $libFilter.blur();
      return false;
    }
  });
  $libFilter.on('keyup', function(event){
    ensureSearchHappensSoon();
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
  $libraryMenu.on('click', '.delete-playlist', onDeletePlaylistContextMenu);
  $libraryMenu.on('click', '.remove', onRemoveFromPlaylistContextMenu);
}

function maybeDeleteSelectedPlaylists() {
  var ids = Object.keys(selection.ids.stored_playlist);
  var nameList = ids.map(function(id) {
    return player.stored_playlist_table[id].name;
  });
  var listText = nameList.slice(0, 7).join("\n  ");
  if (nameList.length > 7) {
    listText += "\n  ...";
  }
  var playlistText = nameList.length === 1 ? "playlist" : "playlists";
  var message = "You are about to delete " + nameList.length + " " + playlistText +
    " permanently:\n\n  " + listText;
  if (!confirm(message)) return false;
  player.deletePlaylists(ids);
  return true;
}

function onDeletePlaylistContextMenu() {
  maybeDeleteSelectedPlaylists();
  removeContextMenu();
  return false;
}

function onRemoveFromPlaylistContextMenu() {
  // TODO
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
      var $deletePlaylistLi = $libraryMenu.find('.delete-playlist').closest('li');
      var $removeFromPlaylistLi = $libraryMenu.find('.remove').closest('li');
      if (type === 'stored_playlist') {
        $deletePlaylistLi.show();
      } else {
        $deletePlaylistLi.hide();
      }
      if (type === 'stored_playlist_item') {
        $removeFromPlaylistLi.show();
      } else {
        $removeFromPlaylistLi.hide();
      }

      var $downloadItem = $libraryMenu.find('.download');
      if (track) {
        downloadMenuZipName = null;
        $downloadItem.attr('href', encodeDownloadHref(track.file));
      } else {
        downloadMenuZipName = zipNameForSelCursor();
        $downloadItem.attr('href', '#');
      }
      $libraryMenu.show().offset({
        left: event.pageX + 1,
        top: event.pageY + 1
      });
      updateMenuDisableState($libraryMenu);
    }
  });
  $elem.on('mousedown', function(){
    return false;
  });
}

function encodeDownloadHref(file) {
  // be sure to escape #hashtags
  return 'library/' + encodeURI(file).replace(/#/g, "%23");
}

function zipNameForSelCursor() {
  switch (selection.type) {
    case 'artist':
      return player.library.artistTable[selection.cursor].name;
    case 'album':
      return player.library.albumTable[selection.cursor].name;
    case 'track':
      return "songs";
    case 'stored_playlist':
      return player.stored_playlist_table[selection.cursor].name;
    case 'stored_playlist_item':
      return "songs";
    default:
      throw new Error("bad selection cursor type: " + selection.type);
  }
}

function updateMenuDisableState($menu) {
  var menuPermDoms = {
    admin: $menu.find('.delete,.edit-tags'),
    control: $menu.find('.remove,.delete-playlist'),
  };
  for (var permName in menuPermDoms) {
    var $item = menuPermDoms[permName];
    if (havePerm(permName)) {
      $item
        .removeClass('ui-state-disabled')
        .attr('title', '');
    } else {
      $item
        .addClass('ui-state-disabled')
        .attr('title', "Insufficient privileges. See Settings.");
    }
  }
}

function setUpUi(){
  setUpGenericUi();
  setUpPlayQueueUi();
  setUpPlaylistsUi();
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

function toStoredPlaylistItemId(s) {
  return "stored-pl-item-" + toHtmlId(s);
}

function toStoredPlaylistId(s) {
  return "stored-pl-pl-" + toHtmlId(s);
}

function handleResize(){
  $nowplaying.width(MARGIN);
  $queueWindow.height(MARGIN);
  $leftWindow.height(MARGIN);
  $library.height(MARGIN);
  $upload.height(MARGIN);
  $queueItems.height(MARGIN);
  $nowplaying.width($document.width() - MARGIN * 2);
  var second_layer_top = $nowplaying.offset().top + $nowplaying.height() + MARGIN;
  $leftWindow.offset({
    left: MARGIN,
    top: second_layer_top
  });
  $queueWindow.offset({
    left: $leftWindow.offset().left + $leftWindow.width() + MARGIN,
    top: second_layer_top
  });
  $queueWindow.width($window.width() - $queueWindow.offset().left - MARGIN);
  $leftWindow.height($window.height() - $leftWindow.offset().top);
  $queueWindow.height($leftWindow.height() - MARGIN);
  var tabContentsHeight = $leftWindow.height() - $tabs.height() - MARGIN;
  $library.height(tabContentsHeight - $libHeader.height());
  $upload.height(tabContentsHeight);
  $queueItems.height($queueWindow.height() - $queueHeader.position().top - $queueHeader.height());
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
  socket.on('hardwarePlayback', function(isOn) {
    hardwarePlaybackOn = isOn;
    updateSettingsAdminUi();
  });
  socket.on('LastFmApiKey', updateLastFmApiKey);
  socket.on('user', function(data) {
    myUser = data;
    $authUsernameDisplay.text(myUser.name);
    if (!localState.authUsername || !localState.authPassword) {
      // We didn't have a user account saved. The server assigned us a name.
      // Generate a password and call dibs on the account.
      localState.authUsername = myUser.name;
      localState.authPassword = uuid();
      saveLocalState();
      sendAuth();
    } else {
      socket.send('subscribe', {name: 'dynamicModeOn'});
      socket.send('subscribe', {name: 'hardwarePlayback'});
      socket.send('subscribe', {name: 'haveAdminUser'});
      socket.send('subscribe', {name: 'approvedUsers'});
      socket.send('subscribe', {name: 'requests'});
      player.resubscribe();
    }
    updateSettingsAuthUi();
  });
  socket.on('token', function(token) {
    document.cookie = "token=" + token + "; path=/";
  });
  socket.on('volumeUpdate', function(vol) {
    player.volume = vol;
    renderVolumeSlider();
  });
  socket.on('dynamicModeOn', function(data) {
    dynamicModeOn = data;
    renderPlaylistButtons();
    triggerRenderQueue();
  });
  socket.on('haveAdminUser', function(data) {
    haveAdminUser = data;
    updateHaveAdminUserUi();
  });
  socket.on('approvedUsers', function(data) {
    approvedUsers = data;
    sortApprovedUsers();
    updateSettingsAuthUi();
  });
  socket.on('requests', function(data) {
    approvalRequests = data;
    sortApprovalRequests();
    updateSettingsAuthUi();
  });
  socket.on('connect', function(){
    sendAuth();
    load_status = LoadStatus.GoodToGo;
    render();
  });
  player = new PlayerClient(socket);
  player.on('libraryupdate', triggerRenderLibrary);
  player.on('queueUpdate', triggerRenderQueue);
  player.on('scanningUpdate', triggerRenderQueue);
  player.on('playlistsUpdate', triggerPlaylistsUpdate);
  player.on('statusupdate', function(){
    renderNowPlaying();
    renderPlaylistButtons();
    labelPlaylistItems();
  });
  socket.on('disconnect', function(){
    load_status = LoadStatus.NoServer;
    render();
  });
  socket.on('error', function(err) {
    console.error(err);
  });
  setUpUi();
  streaming.init(player, socket, localState, saveLocalState);
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

function zfill(number, size) {
  number = String(number);
  while (number.length < size) number = "0" + number;
  return number;
}

function havePerm(permName) {
  return !!(myUser && myUser.perms[permName]);
}
