var $ = window.$;

var shuffle = require('mess');
var humanSize = require('human-size');
var PlayerClient = require('./playerclient');
var Socket = require('./socket');
var uuid = require('./uuid');

var autoDjOn = false;
var hardwarePlaybackOn = false;
var haveAdminUser = true;

var eventsListScrolledToBottom = true;
var isBrowserTabActive = true;

var tryingToStream = false;
var actuallyStreaming = false;
var actuallyPlaying = false;
var stillBuffering = false;
var streamAudio = new Audio();

var selection = {
  ids: {
    queue: {},
    artist: {},
    album: {},
    track: {},
    playlist: {},
    playlistItem: {}
  },
  cursor: null,
  rangeSelectAnchor: null,
  rangeSelectAnchorType: null,
  cursorType: null,
  isLibrary: function(){
    return this.cursorType === 'artist' || this.cursorType === 'album' || this.cursorType === 'track';
  },
  isQueue: function(){
    return this.cursorType === 'queue';
  },
  isPlaylist: function(){
    return this.cursorType === 'playlist' || this.cursorType === 'playlistItem';
  },
  clear: function(){
    this.ids.artist = {};
    this.ids.album = {};
    this.ids.track = {};
    this.ids.queue = {};
    this.ids.playlist = {};
    this.ids.playlistItem = {};
  },
  fullClear: function(){
    this.clear();
    this.cursorType = null;
    this.cursor = null;
    this.rangeSelectAnchor = null;
    this.rangeSelectAnchorType = null;
  },
  selectOnly: function(selName, key){
    this.clear();
    this.cursorType = selName;
    this.ids[selName][key] = true;
    this.cursor = key;
    this.rangeSelectAnchor = key;
    this.rangeSelectAnchorType = selName;
  },
  selectAll: function() {
    this.clear();
    if (selection.isQueue()) {
      selectAllQueue();
    } else if (selection.isLibrary()) {
      selectAllLibrary();
    } else if (selection.isPlaylist()) {
      selectAllPlaylists();
    } else if (player.queue.itemList.length > 0) {
      this.fullClear();
      this.selectOnly('queue', player.queue.itemList[0].id);
      selectAllQueue();
    }
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
    } else if (this.isPlaylist()) {
      result = 2;
      for (k in this.ids.playlist) {
        if (!--result) return true;
      }
      for (k in this.ids.playlistItem) {
        if (!--result) return true;
      }
      return false;
    } else {
      return false;
    }
  },
  getPos: function(type, key){
    if (type == null) type = this.cursorType;
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
    } else if (this.isPlaylist()) {
      val = {
        type: 'playlist',
        playlist: null,
        playlistItem: null
      };
      if (key != null) {
        switch (type) {
          case 'playlistItem':
            val.playlistItem = player.playlistItemTable[key];
            val.playlist = val.playlistItem.playlist;
            break;
          case 'playlist':
            val.playlist = player.playlistTable[key];
            break;
        }
      } else {
        val.playlist = player.playlistList[0];
      }
    } else {
      throw new Error("NothingSelected");
    }
    return val;
  },
  posToArr: function(pos){
    if (pos.type === 'library') {
      return [
        pos.artist && pos.artist.index,
        pos.album && pos.album.index,
        pos.track && pos.track.index,
      ];
    } else if (pos.type === 'playlist') {
      return [
        pos.playlist && pos.playlist.index,
        pos.playlistItem && pos.playlistItem.index,
      ];
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
    } else if (pos.type === 'playlist') {
      return pos.playlist != null;
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
    } else if (pos.type === 'playlist') {
      if (pos.playlistItem != null) {
        selection.ids.playlistItem[pos.playlistItem.id] = true;
      } else if (pos.playlist != null) {
        selection.ids.playlist[pos.playlist.id] = true;
      }
    } else {
      throw new Error("NothingSelected");
    }
  },
  selectOnlyFirstPos: function(type) {
    if (type === 'library') {
      this.selectOnly('artist', player.searchResults.artistList[0].key);
    } else if (type === 'queue') {
      this.selectOnly('queue', player.queue.itemList[0].id);
    } else if (type === 'playlist') {
      this.selectOnly('playlist', player.playlistList[0].id);
    } else {
      throw new Error("unrecognized type: " + type);
    }
  },
  selectOnlyLastPos: function(type) {
    if (type === 'library') {
      var lastArtist = player.searchResults.artistList[player.searchResults.artistList.length - 1];
      if (isArtistExpanded(lastArtist)) {
        var lastAlbum = lastArtist.albumList[lastArtist.albumList.length - 1];
        if (isAlbumExpanded(lastAlbum)) {
          this.selectOnly('track', lastAlbum.trackList[lastAlbum.trackList.length - 1].key);
        } else {
          this.selectOnly('album', lastAlbum.key);
        }
      } else {
        this.selectOnly('artist', lastArtist.key);
      }
    } else if (type === 'queue') {
      this.selectOnly('queue', player.queue.itemList[player.queue.itemList.length - 1].id);
    } else if (type === 'playlist') {
      var lastPlaylist = player.playlistList[player.playlistList.length - 1];
      if (isPlaylistExpanded(lastPlaylist)) {
        this.selectOnly('playlistItem', lastPlaylist.itemList[lastPlaylist.itemList.length - 1].id);
      } else {
        this.selectOnly('playlist', lastPlaylist.id);
      }
    } else {
      throw new Error("unrecognized type: " + type);
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
    } else if (pos.type === 'playlist') {
      if (pos.playlistItem != null) {
        pos.playlistItem = pos.playlistItem.playlist.itemList[pos.playlistItem.index + 1];
        if (pos.playlistItem == null) {
          pos.playlist = player.playlistList[pos.playlist.index + 1];
        }
      } else if (pos.playlist != null) {
        if (isPlaylistExpanded(pos.playlist)) {
          pos.playlistItem = pos.playlist.itemList[0];
          if (pos.playlistItem == null) {
            pos.playlist = player.playlistList[pos.playlist.index + 1];
          }
        } else {
          pos.playlist = player.playlistList[pos.playlist.index + 1];
        }
      }
    } else {
      throw new Error("NothingSelected");
    }
  },
  decrementPos: function(pos) {
    if (pos.type === 'library') {
      if (pos.track != null) {
        pos.track = pos.track.album.trackList[pos.track.index - 1];
      } else if (pos.album != null) {
        pos.album = pos.artist.albumList[pos.album.index - 1];
        if (pos.album != null && isAlbumExpanded(pos.album)) {
          pos.track = pos.album.trackList[pos.album.trackList.length - 1];
        }
      } else if (pos.artist != null) {
        pos.artist = player.searchResults.artistList[pos.artist.index - 1];
        if (pos.artist != null && isArtistExpanded(pos.artist)) {
          pos.album = pos.artist.albumList[pos.artist.albumList.length - 1];
          if (pos.album != null && isAlbumExpanded(pos.album)) {
            pos.track = pos.album.trackList[pos.album.trackList.length - 1];
          }
        }
      }
    } else if (pos.type === 'playlist') {
      if (pos.playlistItem) {
        pos.playlistItem = pos.playlistItem.playlist.itemList[pos.playlistItem.index - 1];
      } else if (pos.playlist) {
        pos.playlist = player.playlistList[pos.playlist.index - 1];
        if (pos.playlist && isPlaylistExpanded(pos.playlist)) {
          pos.playlistItem = pos.playlist.itemList[pos.playlist.itemList.length - 1];
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
    } else if (this.isPlaylist()) {
      return playlistToTrackKeys();
    } else {
      return [];
    }

    function libraryToTrackKeys() {
      var key;
      var trackSet = {};
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
        trackSet[track.key] = this$.posToArr(getTrackSelPos(track));
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
      return trackSetToKeys(trackSet);
    }
    function queueToTrackKeys(){
      var keys = [];
      for (var key in selection.ids.queue) {
        keys.push(player.queue.itemTable[key].track.key);
      }
      if (random) shuffle(keys);
      return keys;
    }
    function playlistToTrackKeys(){
      var trackSet = {};
      function renderQueue(playlist){
        var i, ref$, len$, item;
        for (i = 0, len$ = (ref$ = playlist.itemList).length; i < len$; ++i) {
          item = ref$[i];
          renderPlaylistItem(item);
        }
      }
      function renderPlaylistItem(item){
        trackSet[item.track.key] = this$.posToArr(getItemSelPos(item));
      }
      function getItemSelPos(item){
        return {
          type: 'playlist',
          playlist: item.playlist,
          playlistItem: item
        };
      }
      for (var key in selection.ids.playlist) {
        renderQueue(player.playlistTable[key]);
      }
      for (key in selection.ids.playlistItem) {
        renderPlaylistItem(player.playlistItemTable[key]);
      }
      return trackSetToKeys(trackSet);
    }

    function trackSetToKeys(trackSet){
      var key;
      var keys = [];
      if (random) {
        for (key in trackSet) {
          keys.push(key);
        }
        shuffle(keys);
        return keys;
      }
      var trackArr = [];
      for (key in trackSet) {
        trackArr.push({
          key: key,
          pos: trackSet[key],
        });
      }
      trackArr.sort(function(a, b) {
        return compareArrays(a.pos, b.pos);
      });
      for (var i = 0; i < trackArr.length; i += 1) {
        var track = trackArr[i];
        keys.push(track.key);
      }
      return keys;
    }
  },
  scrollTo: function() {
    var helpers = this.getHelpers();
    if (!helpers) return;
    if (this.isQueue()) {
      scrollThingToSelection($queueItems, {
        queue: helpers.queue,
      });
    } else if (this.isLibrary()) {
      scrollThingToSelection($library, {
        track: helpers.track,
        artist: helpers.artist,
        album: helpers.album,
      });
    } else if (this.isPlaylist()) {
      scrollThingToSelection($playlistsList, {
        playlist: helpers.playlist,
        playlistItem: helpers.playlistItem,
      });
    }
  },
  getHelpers: function() {
    if (player == null) return null;
    if (player.queue == null) return null;
    if (player.queue.itemTable == null) return null;
    if (player.searchResults == null) return null;
    if (player.searchResults.artistTable == null) return null;
    return {
      queue: {
        ids: this.ids.queue,
        table: player.queue.itemTable,
        $getDiv: function(id){
          return $("#" + toQueueItemId(id));
        },
        toggleExpansion: null,
      },
      artist: {
        ids: this.ids.artist,
        table: player.searchResults.artistTable,
        $getDiv: function(id){
          return $("#" + toArtistId(id));
        },
        toggleExpansion: toggleLibraryExpansion,
      },
      album: {
        ids: this.ids.album,
        table: player.searchResults.albumTable,
        $getDiv: function(id){
          return $("#" + toAlbumId(id));
        },
        toggleExpansion: toggleLibraryExpansion,
      },
      track: {
        ids: this.ids.track,
        table: player.searchResults.trackTable,
        $getDiv: function(id){
          return $("#" + toTrackId(id));
        },
        toggleExpansion: toggleLibraryExpansion,
      },
      playlist: {
        ids: this.ids.playlist,
        table: player.playlistTable,
        $getDiv: function(id){
          return $("#" + toPlaylistId(id));
        },
        toggleExpansion: togglePlaylistExpansion,
      },
      playlistItem: {
        ids: this.ids.playlistItem,
        table: player.playlistItemTable,
        $getDiv: function(id){
          return $("#" + toPlaylistItemId(id));
        },
        toggleExpansion: togglePlaylistExpansion,
      },
    };
  },
};
var BASE_TITLE = document.title;
var MARGIN = 10;
var AUTO_EXPAND_LIMIT = 30;
var ICON_COLLAPSED = 'ui-icon-triangle-1-e';
var ICON_EXPANDED = 'ui-icon-triangle-1-se';
var myUser = {
  perms: {},
};
var socket = null;
var player = null;
var userIsSeeking = false;
var userIsVolumeSliding = false;
var startedDrag = false;
var abortDrag = function(){};
var lastFmApiKey = null;
var LoadStatus = {
  Init: 'Loading...',
  NoServer: 'Server is down.',
  GoodToGo: '[good to go]'
};
var repeatModeNames = ["Off", "One", "All"];
var loadStatus = LoadStatus.Init;

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
var $streamBtn = $('#stream-btn');
var $clientVolSlider = $('#client-vol-slider');
var $clientVol = $('#client-vol');
var $queueWindow = $('#queue-window');
var $leftWindow = $('#left-window');
var $queueItems = $('#queue-items');
var $autoDj = $('#auto-dj');
var $queueBtnRepeat = $('#queue-btn-repeat');
var $tabs = $('#tabs');
var $library = $('#library');
var $libFilter = $('#lib-filter');
var $trackSlider = $('#track-slider');
var $nowPlaying = $('#nowplaying');
var $nowPlayingElapsed = $nowPlaying.find('.elapsed');
var $nowPlayingLeft = $nowPlaying.find('.left');
var $volSlider = $('#vol-slider');
var $settings = $('#settings');
var $uploadByUrl = $('#upload-by-url');
var $importByName = $('#import-by-name');
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
var $queueMenu = $('#queue-menu');
var $libraryMenu = $('#library-menu');
var $toggleHardwarePlayback = $('#toggle-hardware-playback');
var $toggleHardwarePlaybackLabel = $('#toggle-hardware-playback-label');
var $newPlaylistName = $('#new-playlist-name');
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
var $eventsOnlineUsers = $('#events-online-users');
var $eventsList = $('#events-list');
var $chatBox = $('#chat-box');
var $chatBoxInput = $('#chat-box-input');
var $queueDuration = $('#queue-duration');
var $queueDurationLabel = $('#queue-duration-label');
var $importProgress = $('#import-progress');
var $importProgressList = $('#import-progress-list');
var autoDjLabel = document.getElementById('auto-dj-label');
var plBtnRepeatLabel = document.getElementById('queue-btn-repeat-label');
var $queueMenuPlaylistSubmenu = $('#queue-menu-playlist-submenu');
var $libraryMenuPlaylistSubmenu = $('#library-menu-playlist-submenu');

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
  events: {
    $pane: $('#events-pane'),
    $tab: $('#events-tab'),
  },
  settings: {
    $pane: $('#settings-pane'),
    $tab: $('#settings-tab'),
  },
};
var activeTab = tabs.library;
var $eventsTabSpan = tabs.events.$tab.find('span');
var $importTabSpan = tabs.upload.$tab.find('span');

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

function selectAllQueue() {
  player.queue.itemList.forEach(function(item) {
    selection.ids.queue[item.id] = true;
  });
}

function selectAllLibrary() {
  player.searchResults.artistList.forEach(function(artist) {
    selection.ids.artist[artist.key] = true;
  });
}

function selectAllPlaylists() {
  player.playlistList.forEach(function(playlist) {
    selection.ids.playlist[playlist.id] = true;
  });
}

function scrollThingToSelection($scrollArea, helpers){
  var topPos = null;
  var bottomPos = null;
  var helper;
  for (var selName in helpers) {
    helper = helpers[selName];
    for (var id in helper.ids) {
      checkPos(id);
    }
    if (selection.cursor && selName === selection.cursorType) {
      checkPos(selection.cursor);
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

  function checkPos(id) {
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

function getDragPosition(x, y){
  var ref$;
  var result = {};
  for (var i = 0, len$ = (ref$ = $queueItems.find(".pl-item").get()).length; i < len$; ++i) {
    var item = ref$[i];
    var $item = $(item);
    var middle = $item.offset().top + $item.height() / 2;
    var track = player.queue.itemTable[$item.attr('data-id')];
    if (middle < y) {
      if (result.previousKey == null || track.sortKey > result.previousKey) {
        result.$previous = $item;
        result.previousKey = track.sortKey;
      }
    } else {
      if (result.nextKey == null || track.sortKey < result.nextKey) {
        result.$next = $item;
        result.nextKey = track.sortKey;
      }
    }
  }
  return result;
}

function renderQueueButtons(){
  $autoDj
    .prop("checked", autoDjOn)
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
  for (i = 0; i < itemList.length; i += 1) {
    var $domItem = $($domItems[i]);
    var item = itemList[i];
    $domItem.attr('id', toQueueItemId(item.id));
    $domItem.attr('data-id', item.id);
    var track = item.track;
    $domItem.find('.track').text(track.track || "");
    $domItem.find('.title').text(track.name || "");
    $domItem.find('.artist').text(track.artistName || "");
    $domItem.find('.album').text(track.albumName || "");
    var timeText = player.isScanning(track) ? "scan" : formatTime(track.duration);
    $domItem.find('.time').text(timeText);
  }

  refreshSelection();
  labelQueueItems();
  $queueItems.scrollTop(scrollTop);
}

function updateQueueDuration() {
  var duration = 0;
  var allAreKnown = true;

  if (selection.isQueue()) {
    selection.toTrackKeys().forEach(addKeyDuration);
    $queueDurationLabel.text("Selection:");
  } else {
    player.queue.itemList.forEach(addItemDuration);
    $queueDurationLabel.text("Play Queue:");
  }
  $queueDuration.text(formatTime(duration) + (allAreKnown ? "" : "?"));

  function addKeyDuration(key) {
    var track = player.library.trackTable[key];
    if (track) {
      addDuration(track);
    }
  }
  function addItemDuration(item) {
    addDuration(item.track);
  }
  function addDuration(track) {
    duration += Math.max(0, track.duration);
    if (player.isScanning(track)) {
      allAreKnown = false;
    }
  }
}

function labelQueueItems() {
  var item;
  var curItem = player.currentItem;
  $queueItems.find(".pl-item")
    .removeClass('current')
    .removeClass('old')
    .removeClass('random');
  if (curItem != null && autoDjOn) {
    for (var index = 0; index < curItem.index; ++index) {
      item = player.queue.itemList[index];
      var itemId = item && item.id;
      if (itemId != null) {
        $("#" + toQueueItemId(itemId)).addClass('old');
      }
    }
  }
  for (var i = 0; i < player.queue.itemList.length; i += 1) {
    item = player.queue.itemList[i];
    if (item.isRandom) {
      $("#" + toQueueItemId(item.id)).addClass('random');
    }
  }
  if (curItem != null) {
    $("#" + toQueueItemId(curItem.id)).addClass('current');
  }
}

function refreshSelection() {
  var helpers = selection.getHelpers();
  if (!helpers) {
    updateQueueDuration();
    return;
  }
  $queueItems.find(".pl-item").removeClass('selected').removeClass('cursor');
  $libraryArtists.find(".clickable").removeClass('selected').removeClass('cursor');
  $playlistsList.find(".clickable").removeClass('selected').removeClass('cursor');
  if (selection.cursorType == null) {
    updateQueueDuration();
    return;
  }
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
    if (selection.cursor != null && selectionType === selection.cursorType) {
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
  updateQueueDuration();
}

function getValidIds(selectionType) {
  switch (selectionType) {
    case 'queue':  return player.queue.itemTable;
    case 'artist': return player.library.artistTable;
    case 'album':  return player.library.albumTable;
    case 'track':  return player.library.trackTable;
    case 'playlist':  return player.playlistTable;
    case 'playlistItem':  return player.playlistItemTable;
  }
  throw new Error("BadSelectionType");
}

function artistDisplayName(name) {
  return name || '[Unknown Artist]';
}

var triggerRenderLibrary = makeRenderCall(renderLibrary, 100);
var triggerRenderQueue = makeRenderCall(renderQueue, 100);
var triggerPlaylistsUpdate = makeRenderCall(updatePlaylistsUi, 100);

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

function updatePlaylistsUi() {
  updatePlaylistsSubmenus($queueMenu, $queueMenuPlaylistSubmenu);
  updatePlaylistsSubmenus($libraryMenu, $libraryMenuPlaylistSubmenu);
  renderPlaylists();
}

function updatePlaylistsSubmenus($parentMenu, $menu) {
  var playlistList = player.playlistList;

  // add the missing dom entries
  var i;
  var menuDom = $menu.get(0);
  for (i = menuDom.childElementCount; i < playlistList.length; i += 1) {
    $menu.append('<li></li>');
  }
  // remove the extra dom entries
  var domItem;
  while (playlistList.length < menuDom.childElementCount) {
    menuDom.removeChild(menuDom.lastChild);
  }

  // overwrite existing dom entries
  var playlist;
  var $domItems = $menu.children();
  for (i = 0; i < playlistList.length; i += 1) {
    domItem = $domItems[i];
    playlist = playlistList[i];
    domItem.setAttribute('data-key', playlist.id);
    domItem.textContent = playlist.name;
  }

  $parentMenu.menu('refresh');
}

function renderPlaylists() {
  var playlistList = player.playlistList;
  var scrollTop = $playlists.scrollTop();

  // add the missing dom entries
  var i;
  var playlistListDom = $playlistsList.get(0);
  for (i = playlistListDom.childElementCount; i < playlistList.length; i += 1) {
    $playlistsList.append(
      '<li>' +
        '<div class="clickable expandable" data-type="playlist">' +
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
    divDom.setAttribute('id', toPlaylistId(playlist.id));
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
    divDom.setAttribute('id', toArtistId(artist.key));
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
  $nowPlayingElapsed.html(formatTime(elapsed));
  $nowPlayingLeft.html(formatTime(duration));
}

function renderVolumeSlider() {
  if (userIsVolumeSliding) return;

  $volSlider.slider('option', 'value', player.volume);
  $volNum.text(Math.round(player.volume * 100));
  $volWarning.toggle(player.volume > 1);
}

function getNowPlayingText(track) {
  if (!track) {
    return "(Deleted Track)";
  }
  var str = track.name + " - " + track.artistName;
  if (track.albumName) {
    str += " - " + track.albumName;
  }
  return str;
}

function renderNowPlaying() {
  var track = null;
  if (player.currentItem != null) {
    track = player.currentItem.track;
  }

  updateTitle();
  var trackDisplay;
  if (track != null) {
    trackDisplay = getNowPlayingText(track);
  } else {
    trackDisplay = "&nbsp;";
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
  $nowPlaying.find(".toggle span").removeClass(oldClass).addClass(newClass);
  $trackSlider.slider("option", "disabled", player.isPlaying == null);
  updateSliderPos();
  renderVolumeSlider();
}

function render(){
  var hideMainErr = loadStatus === LoadStatus.GoodToGo;
  $queueWindow.toggle(hideMainErr);
  $leftWindow.toggle(hideMainErr);
  $nowPlaying.toggle(hideMainErr);
  $mainErrMsg.toggle(!hideMainErr);
  if (!hideMainErr) {
    document.title = BASE_TITLE;
    $mainErrMsgText.text(loadStatus);
    return;
  }
  renderQueue();
  renderQueueButtons();
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
    $ul.append(
      '<li>' +
        '<div class="clickable" data-type="playlistItem">' +
          '<span></span>' +
        '</div>' +
      '</li>');
    var liDom = $ul.get(0).lastChild;
    var divDom = liDom.children[0];
    divDom.setAttribute('id', toPlaylistItemId(item.id));
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
    topLevelType: 'playlist',
    renderDom: function($ul, key) {
      var playlist = player.playlistTable[key];
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
  assumeCurrentSelectionIsDeleted();
  player.deleteTracks(keysList);
  return true;
}

function assumeCurrentSelectionIsDeleted() {
  if (selection.isQueue()) {
    var sortKey = player.queue.itemTable[selection.cursor].sortKey;
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

function handleDeletePressed(shift) {
  var keysList;
  if (selection.isLibrary()) {
    keysList = selection.toTrackKeys();
    maybeDeleteTracks(keysList);
  } else if (selection.isPlaylist()) {
    if (shift) {
      keysList = selection.toTrackKeys();
      if (maybeDeleteTracks(keysList)) {
        player.deletePlaylists(selection.ids.playlist);
      }
    } else {
      var table = extend({}, selection.ids.playlistItem);
      for (var playlistId in selection.ids.playlist) {
        var playlist = player.playlistTable[playlistId];
        for (var itemId in playlist.itemTable) {
          table[itemId] = true;
        }
      }
      player.removeItemsFromPlaylists(table);
    }
  } else if (selection.isQueue()) {
    if (shift) {
      keysList = [];
      for (var id in selection.ids.queue) {
        keysList.push(player.queue.itemTable[id].track.key);
      }
      maybeDeleteTracks(keysList);
    } else {
      var idsToRemove = Object.keys(selection.ids.queue);
      assumeCurrentSelectionIsDeleted();
      player.removeIds(idsToRemove);
    }
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

function setAutoDj(value) {
  autoDjOn = value;
  player.sendCommand('autoDjOn', autoDjOn);
}

function toggleAutoDj(){
  setAutoDj(!autoDjOn);
}

function nextRepeatState(){
  player.setRepeatMode((player.repeat + 1) % repeatModeNames.length);
}

var keyboardHandlers = (function(){
  function upDownHandler(ev){
    var defaultIndex, dir, nextPos;
    if (ev.which === 38) {
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
    if (ev.altKey) {
      if (selection.isQueue()) {
        player.shiftIds(selection.ids.queue, dir);
      } else if (selection.isPlaylist()) {
        player.playlistShiftIds(selection.ids.playlistItem, dir);
      }
    } else {
      if (selection.isQueue()) {
        nextPos = player.queue.itemTable[selection.cursor].index + dir;
        if (nextPos < 0 || nextPos >= player.queue.itemList.length) {
          return;
        }
        selection.cursor = player.queue.itemList[nextPos].id;
        if (!ev.ctrlKey && !ev.shiftKey) {
          // select single
          selection.selectOnly(selection.cursorType, selection.cursor);
        } else if (!ev.ctrlKey && ev.shiftKey) {
          // select range
          selectQueueRange();
        } else {
          // ghost selection
          selection.rangeSelectAnchor = selection.cursor;
          selection.rangeSelectAnchorType = selection.cursorType;
        }
      } else if (selection.isLibrary()) {
        nextPos = selection.getPos();
        if (dir > 0) {
          selection.incrementPos(nextPos);
        } else {
          selection.decrementPos(nextPos);
        }
        if (nextPos.artist == null) return;
        if (nextPos.track != null) {
          selection.cursorType = 'track';
          selection.cursor = nextPos.track.key;
        } else if (nextPos.album != null) {
          selection.cursorType = 'album';
          selection.cursor = nextPos.album.key;
        } else {
          selection.cursorType = 'artist';
          selection.cursor = nextPos.artist.key;
        }
        if (!ev.ctrlKey && !ev.shiftKey) {
          // select single
          selection.selectOnly(selection.cursorType, selection.cursor);
        } else if (!ev.ctrlKey && ev.shiftKey) {
          // select range
          selectTreeRange();
        } else {
          // ghost selection
          selection.rangeSelectAnchor = selection.cursor;
          selection.rangeSelectAnchorType = selection.cursorType;
        }
      } else if (selection.isPlaylist()) {
        nextPos = selection.getPos();
        if (dir > 0) {
          selection.incrementPos(nextPos);
        } else {
          selection.decrementPos(nextPos);
        }
        if (!nextPos.playlist) return;
        if (nextPos.playlistItem) {
          selection.cursorType = 'playlistItem';
          selection.cursor = nextPos.playlistItem.id;
        } else {
          selection.cursorType = 'playlist';
          selection.cursor = nextPos.playlist.id;
        }
        if (!ev.ctrlKey && !ev.shiftKey) {
          selection.selectOnly(selection.cursorType, selection.cursor);
        } else if (!ev.ctrlKey && ev.shiftKey) {
          selectTreeRange();
        } else {
          selection.rangeSelectAnchor = selection.cursor;
          selection.rangeSelectAnchorType = selection.cursorType;
        }
      } else {
        if (player.queue.itemList.length === 0) return;
        selection.selectOnly('queue', player.queue.itemList[defaultIndex].id);
      }
      refreshSelection();
    }
    selection.scrollTo();
  }
  function leftRightHandler(ev){
    var dir = ev.which === 37 ? -1 : 1;
    var helpers = selection.getHelpers();
    if (!helpers) return;
    var helper = helpers[selection.cursorType];
    if (helper.toggleExpansion) {
      var selectedItem = helper.table[selection.cursor];
      var isExpandedFuncs = {
        artist: isArtistExpanded,
        album: isAlbumExpanded,
        track: alwaysTrue,
        playlist: isPlaylistExpanded,
        playlistItem: alwaysTrue,
      };
      var isExpanded = isExpandedFuncs[selection.cursorType](selectedItem);
      var $li = helper.$getDiv(selection.cursor).closest("li");
      if (dir > 0) {
        if (!isExpanded) {
          helper.toggleExpansion($li);
        }
      } else {
        if (isExpanded) {
          helper.toggleExpansion($li);
        }
      }
    } else {
      if (ev.ctrlKey) {
        if (dir > 0) {
          player.next();
        } else {
          player.prev();
        }
      } else if (ev.shiftKey) {
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
      handler: function(ev){
        if (selection.isQueue()) {
          player.seek(selection.cursor, 0);
          player.play();
        } else {
          queueSelection(ev);
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
        if (startedDrag) {
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
      handler: function(ev) {
        if (ev.ctrlKey) {
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
      handler: function(ev) {
        if ((havePerm('admin') && ev.shiftKey) ||
           (havePerm('control') && !ev.shiftKey))
        {
          handleDeletePressed(ev.shiftKey);
        }
      },
    },
    // =
    61: volumeUpHandler,
    // Ctrl+A
    65: {
      ctrl: true,
      alt: false,
      shift: false,
      handler: function() {
        selection.selectAll();
        refreshSelection();
      },
    },
    // d
    68: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: toggleAutoDj,
    },
    // e
    69: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: function(){
        clickTab(tabs.settings);
      },
    },
    // H
    72: {
      ctrl: false,
      alt: false,
      shift: true,
      handler: onShuffleContextMenu,
    },
    // p
    80: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: function() {
        clickTab(tabs.playlists);
        $newPlaylistName.focus().select();
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
      handler: toggleStreamStatus
    },
    // t
    84: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: function() {
        clickTab(tabs.events);
        $chatBoxInput.focus().select();
        scrollEventsToBottom();
      },
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
      handler: function(ev){
        if (ev.shiftKey) {
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
  if (tryingToStream) {
    setStreamVolume(streamAudio.volume + v);
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

function isPlaylistExpanded(playlist){
  var $li = $("#" + toPlaylistId(playlist.id)).closest("li");
  if (!$li.data('cached')) return false;
  return $li.find("> ul").is(":visible");
}

function isArtistExpanded(artist){
  var artistHtmlId = toArtistId(artist.key);
  var artistElem = document.getElementById(artistHtmlId);
  var $li = $(artistElem).closest('li');
  if (!$li.data('cached')) return false;
  return $li.find("> ul").is(":visible");
}

function expandArtist(artist) {
  if (isArtistExpanded(artist)) return;

  var artistElem = document.getElementById(toArtistId(artist.key));
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
  selection.scrollTo();
}

function queueSelection(ev){
  var keys = selection.toTrackKeys(ev.altKey);
  if (ev.shiftKey) {
    player.queueTracksNext(keys);
  } else {
    player.queueOnQueue(keys);
  }
  return false;
}

function toggleSelectionUnderCursor() {
  var key = selection.cursor;
  var type = selection.cursorType;
  if (selection.ids[type][key] != null) {
    delete selection.ids[type][key];
  } else {
    selection.ids[type][key] = true;
  }
}

function selectQueueRange() {
  selection.clear();
  var anchor = selection.rangeSelectAnchor;
  if (anchor == null) anchor = selection.cursor;
  var minPos = player.queue.itemTable[anchor].index;
  var maxPos = player.queue.itemTable[selection.cursor].index;
  if (maxPos < minPos) {
    var tmp = minPos;
    minPos = maxPos;
    maxPos = tmp;
  }
  for (var i = minPos; i <= maxPos; i++) {
    selection.ids.queue[player.queue.itemList[i].id] = true;
  }
}
function selectTreeRange() {
  selection.clear();
  var oldPos = selection.getPos(selection.rangeSelectAnchorType, selection.rangeSelectAnchor);
  var newPos = selection.getPos(selection.cursorType, selection.cursor);
  if (compareArrays(selection.posToArr(oldPos), selection.posToArr(newPos)) > 0) {
    var tmp = oldPos;
    oldPos = newPos;
    newPos = tmp;
  }
  while (selection.posInBounds(oldPos)) {
    selection.selectPos(oldPos);
    if (selection.posEqual(oldPos, newPos)) {
      break;
    }
    selection.incrementPos(oldPos);
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

function changeUserName(username) {
  if (!username) return false;
  localState.authUsername = username;
  saveLocalState();
  sendAuth();
  return true;
}

function settingsAuthCancel() {
  hideShowAuthEdit(false);
}

function hideShowAuthEdit(visible) {
  $settingsRegister.toggle(visible);
  $settingsShowAuth.toggle(!visible);
}

function performDrag(ev, callbacks){
  abortDrag();
  var startDragX = ev.pageX;
  var startDragY = ev.pageY;
  abortDrag = function(){
    $document.off('mousemove', onDragMove).off('mouseup', onDragEnd);
    if (startedDrag) {
      $queueItems.find(".pl-item").removeClass('border-top').removeClass('border-bottom');
      startedDrag = false;
    }
    abortDrag = function(){};
  };
  function onDragMove(ev){
    var dist, result;
    if (!startedDrag) {
      dist = Math.pow(ev.pageX - startDragX, 2) + Math.pow(ev.pageY - startDragY, 2);
      if (dist > 64) {
        startedDrag = true;
      }
      if (!startedDrag) {
        return;
      }
    }
    result = getDragPosition(ev.pageX, ev.pageY);
    $queueItems.find(".pl-item").removeClass('border-top').removeClass('border-bottom');
    if (result.$next != null) {
      result.$next.addClass("border-top");
    } else if (result.$previous != null) {
      result.$previous.addClass("border-bottom");
    }
  }
  function onDragEnd(ev){
    if (ev.which !== 1) {
      return false;
    }
    if (startedDrag) {
      callbacks.complete(getDragPosition(ev.pageX, ev.pageY), ev);
    } else {
      callbacks.cancel();
    }
    abortDrag();
  }
  $document.on('mousemove', onDragMove).on('mouseup', onDragEnd);
  onDragMove(ev);
}

function setUpGenericUi(){
  $document.on('mouseover', '.hoverable', function(ev){
    $(this).addClass("ui-state-hover");
  });
  $document.on('mouseout', '.hoverable', function(ev){
    $(this).removeClass("ui-state-hover");
  });
  $(".jquery-button").button().on('click', blur);
  $document.on('mousedown', function(){
    removeContextMenu();
    selection.fullClear();
    refreshSelection();
  });
  $document.on('keydown', function(ev){
    var handler = keyboardHandlers[ev.which];
    if (handler == null) return true;
    if (handler.ctrl  != null && handler.ctrl  !== ev.ctrlKey)  return true;
    if (handler.alt   != null && handler.alt   !== ev.altKey)   return true;
    if (handler.shift != null && handler.shift !== ev.shiftKey) return true;
    handler.handler(ev);
    return false;
  });
  $shortcuts.on('keydown', function(ev) {
    ev.stopPropagation();
    if (ev.which === 27) {
      $shortcuts.dialog('close');
    }
  });
}

function blur() {
  $(this).blur();
}

function setUpPlayQueueUi() {
  $queueBtnRepeat.on('click', nextRepeatState);
  plBtnRepeatLabel.addEventListener('mousedown', stopPropagation, false);

  $autoDj.on('click', function(){
    var value = $(this).prop("checked");
    setAutoDj(value);
    return false;
  });
  autoDjLabel.addEventListener('mousedown', stopPropagation, false);

  $queueItems.on('dblclick', '.pl-item', function(ev){
    var trackId = $(this).attr('data-id');
    player.seek(trackId, 0);
    player.play();
  });
  $queueItems.on('contextmenu', '.pl-item', function(ev){
    return ev.altKey;
  });
  $queueItems.on('mousedown', '.pl-item', function(ev){
    var trackId, skipDrag;
    if (startedDrag) return true;
    $(document.activeElement).blur();
    if (ev.which === 1) {
      ev.preventDefault();
      removeContextMenu();
      trackId = $(this).attr('data-id');
      skipDrag = false;
      if (!selection.isQueue()) {
        selection.selectOnly('queue', trackId);
      } else if (ev.ctrlKey || ev.shiftKey) {
        skipDrag = true;
        if (ev.shiftKey && !ev.ctrlKey) {
          // range select click
          selection.cursor = trackId;
          selectQueueRange();
        } else if (!ev.shiftKey && ev.ctrlKey) {
          // individual item selection toggle
          selection.cursor = trackId;
          selection.rangeSelectAnchor = trackId;
          selection.rangeSelectAnchorType = selection.cursorType;
          toggleSelectionUnderCursor();
        }
      } else if (selection.ids.queue[trackId] == null) {
        selection.selectOnly('queue', trackId);
      }
      refreshSelection();
      if (!skipDrag) {
        performDrag(ev, {
          complete: function(result, ev){
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
            })(), result.previousKey, result.nextKey);
          },
          cancel: function(){
            selection.selectOnly('queue', trackId);
            refreshSelection();
          }
        });
        return false;
      }
    } else if (ev.which === 3) {
      if (ev.altKey) return false;
      ev.preventDefault();
      removeContextMenu();
      trackId = $(this).attr('data-id');
      if (!selection.isQueue() || selection.ids.queue[trackId] == null) {
        selection.selectOnly('queue', trackId);
        refreshSelection();
      }
      if (!selection.isMulti()) {
        var item = player.queue.itemTable[trackId];
        $queueMenu.find('.download').attr('href', encodeDownloadHref(item.track.file));
      } else {
        $queueMenu.find('.download').attr('href', makeMultifileDownloadHref());
      }
      $queueMenu.show().offset({
        left: ev.pageX + 1,
        top: ev.pageY + 1
      });
      updateMenuDisableState($queueMenu);
    }
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
  $queueMenuPlaylistSubmenu.on('click', 'li', onAddToPlaylistContextMenu);
  $queueMenu.on('click', '.shuffle', onShuffleContextMenu);
}

function onShuffleContextMenu(ev) {
  if (!selection.cursor || selection.isQueue()) {
    var ids = Object.keys(selection.ids.queue);
    if (ids.length === 0) {
      ids = Object.keys(player.queue.itemTable);
    }
    player.shuffleQueueItems(ids);
  } else if (selection.isPlaylist()) {
    if (selection.cursorType === 'playlistItem') {
      player.shufflePlaylistItems(selection.ids.playlistItem);
    } else if (selection.cursorType === 'playlist') {
      player.shufflePlaylists(selection.ids.playlist);
    }
  }
  removeContextMenu();
  return false;
}

function setUpPlaylistsUi() {
  $newPlaylistName.on('keydown', function(ev) {
    ev.stopPropagation();

    if (ev.which === 27) {
      $newPlaylistName.val("").blur();
    } else if (ev.which === 13) {
      var name = $newPlaylistName.val().trim();
      if (name.length > 0) {
        player.createPlaylist(name);
        $newPlaylistName.val("");
      }
    } else if (ev.which === 40) {
      // down
      selection.selectOnlyFirstPos('playlist');
      selection.scrollTo();
      refreshSelection();
      $newPlaylistName.blur();
    } else if (ev.which === 38) {
      // up
      selection.selectOnlyLastPos('playlist');
      selection.scrollTo();
      refreshSelection();
      $newPlaylistName.blur();
    }
  });

  genericTreeUi($playlistsList, {
    toggleExpansion: togglePlaylistExpansion,
    isSelectionOwner: function() {
      return selection.isPlaylist();
    },
  });
}

function stopPropagation(ev) {
  ev.stopPropagation();
}

function onDownloadContextMenu() {
  removeContextMenu();
  return true;
}

function onDeleteContextMenu() {
  if (!havePerm('admin')) return false;
  removeContextMenu();
  handleDeletePressed(true);
  return false;
}

function onAddToPlaylistContextMenu() {
  if (!havePerm('control')) return false;
  var keysList = selection.toTrackKeys();
  var playlistId = $(this).attr('data-key');
  player.queueOnPlaylist(playlistId, keysList);
  removeContextMenu();
  return true;
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
  $editTagsDialog.find("input").on("keydown", function(ev) {
    ev.stopPropagation();
    if (ev.which === 27) {
      $editTagsDialog.dialog('close');
    } else if (ev.which === 13) {
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

  function onFocus(ev) {
    editTagsFocusDom = ev.target;
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
    change: function(ev, ui){
      updateSliderUi(ui.value);
      if (ev.originalEvent == null) {
        return;
      }
      if (!player.currentItem) return;
      player.seek(null, ui.value * player.currentItem.track.duration);
    },
    slide: function(ev, ui){
      updateSliderUi(ui.value);
      if (!player.currentItem) return;
      $nowPlayingElapsed.html(formatTime(ui.value * player.currentItem.track.duration));
    },
    start: function(ev, ui){
      userIsSeeking = true;
    },
    stop: function(ev, ui){
      userIsSeeking = false;
    }
  });
  function setVol(ev, ui){
    if (ev.originalEvent == null) return;
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
    start: function(ev, ui){
      userIsVolumeSliding = true;
    },
    stop: function(ev, ui){
      userIsVolumeSliding = false;
    }
  });
  setInterval(updateSliderPos, 100);
  function setUpMouseDownListener(cls, action){
    $nowPlaying.on('mousedown', "li." + cls, function(ev){
      action();
      return false;
    });
  }
}

function clickTab(tab) {
  unselectTabs();
  tab.$tab.addClass('ui-state-active');
  tab.$pane.show();
  activeTab = tab;
  handleResize();
  if (tab === tabs.events) {
    player.markAllEventsSeen();
    renderUnseenChatCount();
  }
}

function setUpTabListener(tab) {
  tab.$tab.on('click', function(ev) {
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

  if (localState.autoQueueUploads) {
    formData.append('autoQueue', '1');
  }

  for (var i = 0; i < files.length; i += 1) {
    var file = files[i];
    formData.append("size", String(file.size));
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
  $autoQueueUploads.on('click', function(ev) {
    var value = $(this).prop('checked');
    localState.autoQueueUploads = value;
    saveLocalState();
    setAutoUploadBtnState();
  });
  uploadInput.addEventListener('change', onChange, false);

  function onChange(e) {
    uploadFiles(this.files);
  }

  $uploadByUrl.on('keydown', function(ev){
    ev.stopPropagation();
    if (ev.which === 27) {
      $uploadByUrl.val("").blur();
    } else if (ev.which === 13) {
      importUrl();
    }
  });

  $importByName.on('keydown', function(ev) {
    ev.stopPropagation();
    if (ev.which === 27) {
      $importByName.val("").blur();
    } else if (ev.which === 13 && ev.ctrlKey) {
      importNames();
    }
  });
}

function importUrl() {
  var url = $uploadByUrl.val();
  $uploadByUrl.val("").blur();
  socket.send('importUrl', {
    url: url,
    autoQueue: !!localState.autoQueueUploads,
  });
}

function importNames() {
  var namesText = $importByName.val();
  var namesList = namesText.split("\n").map(trimIt).filter(truthy);
  $importByName.val("").blur();
  socket.send('importNames', {
    names: namesList,
    autoQueue: !!localState.autoQueueUploads,
  });
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
  var authUrl = "https://www.last.fm/api/auth?api_key=" +
        encodeURIComponent(lastFmApiKey) + "&cb=" +
        encodeURIComponent(location.protocol + "//" + location.host + "/");
  lastFmAuthUrlDom.setAttribute('href', authUrl);
  $toggleScrobble
    .button('option', 'label', localState.lastfm.scrobbling_on ? 'On' : 'Off')
    .prop('checked', localState.lastfm.scrobbling_on)
    .button('refresh');
}

function updateSettingsAuthUi() {
  var i, user;
  var request = null;
  var selectedUserId = $settingsUsersSelect.val();
  $settingsUsersSelect.empty();
  for (i = 0; i < player.usersList.length; i += 1) {
    user = player.usersList[i];
    if (user.approved) {
      $settingsUsersSelect.append($("<option/>", {
        value: user.id,
        text: user.name,
      }));
      selectedUserId = selectedUserId || user.id;
    }
    if (!user.approved && user.requested) {
      request = request || user;
    }
  }
  $settingsUsersSelect.val(selectedUserId);
  updatePermsForSelectedUser();

  if (request) {
    $requestReplace.empty();
    for (i = 0; i < player.usersList.length; i += 1) {
      user = player.usersList[i];
      if (user.id === PlayerClient.GUEST_USER_ID) {
        user = request;
      }
      if (user.approved || user === request) {
        $requestReplace.append($("<option/>", {
          value: user.id,
          text: user.name,
        }));
      }
    }
    $requestReplace.val(request.id);
    $requestName.val(request.name);
  }

  $authPermRead.toggle(havePerm('read'));
  $authPermAdd.toggle(havePerm('add'));
  $authPermControl.toggle(havePerm('control'));
  $authPermAdmin.toggle(havePerm('admin'));
  streamUrlDom.setAttribute('href', getStreamUrl());
  $settingsAuthRequest.toggle(myUser.registered && !myUser.requested && !myUser.approved);
  $settingsAuthLogout.toggle(myUser.registered);
  $settingsAuthEdit.button('option', 'label', myUser.registered ? 'Edit' : 'Register');
  $settingsUsers.toggle(havePerm('admin'));
  $settingsRequests.toggle(havePerm('admin') && !!request);

  $toggleHardwarePlayback
    .prop('disabled', !havePerm('admin'))
    .button('refresh');
  $toggleHardwarePlaybackLabel.attr('title', havePerm('admin') ? "" : "Requires admin privilege.");
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

  $ensureAdminDiv.on('click', function(ev) {
    socket.send('ensureAdminUser');
  });

  $lastFmSignOut.on('click', function(ev) {
    localState.lastfm.username = null;
    localState.lastfm.session_key = null;
    localState.lastfm.scrobbling_on = false;
    saveLocalState();
    updateLastFmSettingsUi();
    return false;
  });
  $toggleScrobble.on('click', function(ev) {
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
      sessionKey: localState.lastfm.session_key
    };
    socket.send(msg, params);
    updateLastFmSettingsUi();
  });
  $toggleHardwarePlayback.on('click', function(ev) {
    var value = $(this).prop('checked');
    socket.send('hardwarePlayback', value);
    updateSettingsAdminUi();
  });
  $settingsAuthEdit.on('click', function(ev) {
    $authUsername.val(localState.authUsername);
    $authPassword.val(localState.authPassword);
    hideShowAuthEdit(true);
    $authUsername.focus().select();
  });
  $settingsAuthSave.on('click', function(ev){
    settingsAuthSave();
  });
  $settingsAuthCancel.on('click', function(ev) {
    settingsAuthCancel();
  });
  $authUsername.on('keydown', handleUserOrPassKeyDown);
  $authPassword.on('keydown', handleUserOrPassKeyDown);
  $authShowPassword.on('change', function(ev) {
    var showPw = $authShowPassword.prop('checked');
    $authPassword.get(0).type = showPw ? 'text' : 'password';
  });
  $settingsAuthRequest.on('click', function(ev) {
    socket.send('requestApproval');
    myUser.requested = true;
    updateSettingsAuthUi();
  });
  $settingsAuthLogout.on('click', function(ev) {
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

  $settingsDeleteUser.on('click', function(ev) {
    var selectedUserId = $settingsUsersSelect.val();
    socket.send('deleteUsers', [selectedUserId]);
  });

  $requestApprove.on('click', function(ev) {
    handleApproveDeny(true);
  });
  $requestDeny.on('click', function(ev) {
    handleApproveDeny(false);
  });
}

function handleApproveDeny(approved) {
  var request = null;
  for (var i = 0; i < player.usersList.length; i += 1) {
    var user = player.usersList[i];
    if (!user.approved && user.requested) {
      request = user;
      break;
    }
  }
  if (!request) return;
  socket.send('approve', [{
    id: request.id,
    replaceId: $requestReplace.val(),
    approved: approved,
    name: $requestName.val(),
  }]);
}

function updatePermsForSelectedUser() {
  var selectedUserId = $settingsUsersSelect.val();
  var user = player.usersTable[selectedUserId];
  if (!user) return;
  $userPermRead.prop('checked', user.perms.read).button('refresh');
  $userPermAdd.prop('checked', user.perms.add).button('refresh');
  $userPermControl.prop('checked', user.perms.control).button('refresh');
  $userPermAdmin.prop('checked', user.perms.admin).button('refresh');

  $settingsDeleteUser.prop('disabled', selectedUserId === PlayerClient.GUEST_USER_ID).button('refresh');
}

function updateSelectedUserPerms(ev) {
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

function handleUserOrPassKeyDown(ev) {
  ev.stopPropagation();
  if (ev.which === 27) {
    settingsAuthCancel();
  } else if (ev.which === 13) {
    settingsAuthSave();
  }
}

var chatCommands = {
  nick: changeUserName,
  me: displaySlashMe,
};

function setUpEventsUi() {
  $eventsList.on('scroll', function(ev) {
    eventsListScrolledToBottom = ($eventsList.get(0).scrollHeight - $eventsList.scrollTop()) === $eventsList.outerHeight();
  });
  $chatBoxInput.on('keydown', function(ev) {
    ev.stopPropagation();
    if (ev.which === 27) {
      $chatBoxInput.blur();
      return false;
    } else if (ev.which === 13) {
      var msg = $chatBoxInput.val().trim();
      if (!msg.length) return false;
      var match = msg.match(/^\/([^\/]\w*)\s*(.*)$/);
      if (match) {
        var chatCommand = chatCommands[match[1]];
        if (chatCommand) {
          if (!chatCommand(match[2])) {
            // command failed; no message sent
            return false;
          }
        } else {
          // don't clear the text box; invalid command
          return false;
        }
      } else {
        // replace starting '//' with '/'
        socket.send('chat', { text: msg.replace(/^\/\//, '/') });
      }
      setTimeout(clearChatInputValue, 0);
      return false;
    }
  });
}

function displaySlashMe(message) {
  if (!message) return false;
  socket.send('chat', {
    text: message,
    displayClass: 'me',
  });
  return true;
}

function clearChatInputValue() {
  $chatBoxInput.val("");
}

function renderUnseenChatCount() {
  var eventsTabText = (player.unseenChatCount > 0) ?
    ("Chat (" + player.unseenChatCount + ")") : "Chat";
  $eventsTabSpan.text(eventsTabText);
  updateTitle();
}

function updateTitle() {
  var track = player.currentItem && player.currentItem.track;
  var prefix = (player.unseenChatCount > 0) ? ("(" + player.unseenChatCount + ") ") : "";
  if (track) {
    document.title = prefix + getNowPlayingText(track) + " - " + BASE_TITLE;
  } else {
    document.title = prefix + BASE_TITLE;
  }
}

function renderImportProgress() {
  var importProgressListDom = $importProgressList.get(0);
  var scrollTop = $importProgressList.scrollTop();

  var importTabText = (player.importProgressList.length > 0) ?
    ("Import (" + player.importProgressList.length + ")") : "Import";
  $importTabSpan.text(importTabText);

  // add the missing dom entries
  var i, ev;
  for (i = importProgressListDom.childElementCount; i < player.importProgressList.length; i += 1) {
    $importProgressList.append(
      '<li class="progress">' +
        '<span class="name"></span> ' +
        '<span class="percent"></span>' +
      '</li>');
  }
  // remove extra dom entries
  var domItem;
  while (player.importProgressList.length < importProgressListDom.childElementCount) {
    importProgressListDom.removeChild(importProgressListDom.lastChild);
  }
  // overwrite existing dom entries
  var $domItems = $importProgressList.children();
  for (i = 0; i < player.importProgressList.length; i += 1) {
    var $domItem = $($domItems[i]);
    ev = player.importProgressList[i];
    $domItem.find('.name').text(ev.filenameHintWithoutPath);
    var percent = humanSize(ev.bytesWritten, 1);
    if (ev.size) {
      percent += " / " + humanSize(ev.size, 1);
    }
    $domItem.find('.percent').text(percent);
  }

  $importProgress.toggle(player.importProgressList.length > 0);
  $importProgressList.scrollTop(scrollTop);
}

function renderEvents() {
  var eventsListDom = $eventsList.get(0);
  var scrollTop = $eventsList.scrollTop();

  renderUnseenChatCount();

  // add the missing dom entries
  var i, ev;
  for (i = eventsListDom.childElementCount; i < player.eventsList.length; i += 1) {
    $eventsList.append(
      '<div class="event">' +
        '<span class="name"></span>' +
        '<span class="msg"></span>' +
        '<div style="clear: both;"></div>' +
      '</div>');
  }
  // remove extra dom entries
  var domItem;
  while (player.eventsList.length < eventsListDom.childElementCount) {
    eventsListDom.removeChild(eventsListDom.lastChild);
  }
  // overwrite existing dom entries
  var $domItems = $eventsList.children();
  for (i = 0; i < player.eventsList.length; i += 1) {
    var $domItem = $($domItems[i]);
    ev = player.eventsList[i];
    var userText = ev.user ? ev.user.name : "*";

    $domItem.removeClass().addClass('event').addClass(ev.type);
    $domItem.find('.name').text(userText).attr('title', ev.date.toString());

    $domItem.find('.msg').html(getEventMessageHtml(ev));

    if (ev.displayClass) {
      $domItem.addClass('chat-me');
    }
  }

  if (eventsListScrolledToBottom) {
    scrollEventsToBottom();
  } else {
    $eventsList.scrollTop(scrollTop);
  }
}

function getEventMessageHtml(ev) {
  var fn = eventTypeMessageFns[ev.type];
  if (!fn) throw new Error("Unknown event type: " + ev.type);
  var flags = {safe: false};
  var text = fn(ev, flags);
  return flags.safe ? text : escapeHtml(text);
}

function linkify(text) {
  return text.replace(/(\b(https?|ftp|file):\/\/[\-A-Z0-9+&@#\/\[\]%?=~_|!:,.;]*[\-A-Z0-9+&@#\/\[\]%=~_|])/ig, '<a href="$1" target="_blank">$1</a>');
}

var escapeHtmlReplacements = { "&": "&amp;", '"': "&quot;", "<": "&lt;", ">": "&gt;" };

function escapeHtml(str) {
  return str.replace(/[&"<>]/g, function (m) {
    return escapeHtmlReplacements[m];
  });
}

function scrollEventsToBottom() {
  eventsListScrolledToBottom = true;
  $eventsList.scrollTop(1000000);
}

function eventPlaylistName(ev) {
  return ev.playlist ? ("playlist " + ev.playlist.name) : "(deleted playlist)";
}

function getEventNowPlayingText(ev) {
  if (ev.track) {
    return getNowPlayingText(ev.track);
  } else if (ev.text) {
    return "(Deleted Track) " + ev.text;
  } else {
    return "(No Track)";
  }
}

var eventTypeMessageFns = {
  autoDj: function(ev) {
    return "toggled Auto DJ";
  },
  autoPause: function(ev) {
    return "auto pause because nobody is listening";
  },
  chat: function(ev, flags) {
    flags.safe = true;
    return linkify(escapeHtml(ev.text));
  },
  clearQueue: function(ev) {
    return "cleared the queue";
  },
  connect: function(ev) {
    return "connected";
  },
  currentTrack: function(ev) {
    return "Now playing: " + getEventNowPlayingText(ev);
  },
  import: function(ev) {
    var prefix = ev.user ? "imported " : "anonymous user imported ";
    if (ev.pos > 1) {
      return prefix + ev.pos + " tracks";
    } else {
      return prefix + getEventNowPlayingText(ev);
    }
  },
  login: function(ev) {
    return "logged in";
  },
  move: function(ev) {
    return "moved queue items";
  },
  part: function(ev) {
    return "disconnected";
  },
  pause: function(ev) {
    return "pressed pause";
  },
  play: function(ev) {
    return "pressed play";
  },
  playlistAddItems: function(ev) {
    if (ev.pos === 1) {
      return "added " + getEventNowPlayingText(ev) + " to " + eventPlaylistName(ev);
    } else {
      return "added " + ev.pos + " tracks to " + eventPlaylistName(ev);
    }
  },
  playlistCreate: function(ev) {
    return "created " + eventPlaylistName(ev);
  },
  playlistDelete: function(ev) {
    return "deleted playlist " + ev.text;
  },
  playlistMoveItems: function(ev) {
    if (ev.playlist) {
      return "moved " + ev.pos + " tracks in " + eventPlaylistName(ev);
    } else {
      return "moved " + ev.pos + " tracks in playlists";
    }
  },
  playlistRemoveItems: function(ev) {
    if (ev.playlist) {
      if (ev.pos === 1) {
        return "removed " + getEventNowPlayingText(ev) + " from " + eventPlaylistName(ev);
      } else {
        return "removed " + ev.pos + " tracks from " + eventPlaylistName(ev);
      }
    } else {
      return "removed " + ev.pos + " tracks from playlists";
    }
  },
  playlistRename: function(ev) {
    var name = ev.playlist ? ev.playlist.name : "(Deleted Playlist)";
    return "renamed playlist " + ev.text + " to " + name;
  },
  queue: function(ev) {
    if (ev.pos === 1) {
      return "added to the queue: " + getEventNowPlayingText(ev);
    } else {
      return "added " + ev.pos + " tracks to the queue";
    }
  },
  remove: function(ev) {
    if (ev.pos === 1) {
      return "removed from the queue: " + getEventNowPlayingText(ev);
    } else {
      return "removed " + ev.pos + " tracks from the queue";
    }
  },
  register: function(ev) {
    return "registered";
  },
  seek: function(ev) {
    if (ev.pos === 0) {
      return "chose a different song";
    } else {
      return "seeked to " + formatTime(ev.pos);
    }
  },
  shuffle: function(ev) {
    return "shuffled the queue";
  },
  stop: function(ev) {
    return "pressed stop";
  },
  streamStart: function(ev) {
    if (ev.user) {
      return "started streaming";
    } else {
      return "anonymous user started streaming";
    }
  },
  streamStop: function(ev) {
    if (ev.user) {
      return "stopped streaming";
    } else {
      return "anonymous user stopped streaming";
    }
  },
};

function renderOnlineUsers() {
  var i;
  var user;
  var sortedConnectedUsers = [];
  for (i = 0; i < player.usersList.length; i += 1) {
    user = player.usersList[i];
    if (user.connected) {
      sortedConnectedUsers.push(user);
    }
  }

  var scrollTop = $eventsOnlineUsers.scrollTop();


  // add the missing dom entries
  var onlineUserDom = $eventsOnlineUsers.get(0);
  var heightChanged = onlineUserDom.childElementCount !== sortedConnectedUsers.length;
  for (i = onlineUserDom.childElementCount; i < sortedConnectedUsers.length; i += 1) {
    $eventsOnlineUsers.append(
      '<div class="user">' +
        '<span class="streaming ui-icon ui-icon-signal-diag"></span>' +
        '<span class="name"></span>' +
      '</div>');
  }
  // remove extra dom entries
  var domItem;
  while (sortedConnectedUsers.length < onlineUserDom.childElementCount) {
    onlineUserDom.removeChild(onlineUserDom.lastChild);
  }
  // overwrite existing dom entries
  var $domItems = $eventsOnlineUsers.children();
  for (i = 0; i < sortedConnectedUsers.length; i += 1) {
    var $domItem = $($domItems[i]);
    user = sortedConnectedUsers[i];
    $domItem.find('.name').text(user.name);
    $domItem.find('.streaming').toggle(user.streaming);
  }

  $eventsOnlineUsers.scrollTop(scrollTop);

  if (heightChanged) {
    handleResize();
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
  $libFilter.on('keydown', function(ev){
    var keys, i, ref$, len$, artist, j$, ref1$, len1$, album, k$, ref2$, len2$, track;
    ev.stopPropagation();
    switch (ev.which) {
    case 27: // Escape
      if ($(ev.target).val().length === 0) {
        $(ev.target).blur();
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
      if (ev.altKey) shuffle(keys);
      if (keys.length > 2000) {
        if (!confirm("You are about to queue " + keys.length + " songs.")) {
          return false;
        }
      }
      if (ev.shiftKey) {
        player.queueTracksNext(keys);
      } else {
        player.queueOnQueue(keys);
      }
      return false;
    case 40:
      selection.selectOnlyFirstPos('library');
      selection.scrollTo();
      refreshSelection();
      $libFilter.blur();
      return false;
    case 38:
      selection.selectOnlyLastPos('library');
      selection.scrollTo();
      refreshSelection();
      $libFilter.blur();
      return false;
    }
  });
  $libFilter.on('keyup', ensureSearchHappensSoon);
  $libFilter.on('cut', ensureSearchHappensSoon);
  $libFilter.on('paste', ensureSearchHappensSoon);
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
    player.queueOnQueue(selection.toTrackKeys());
    removeContextMenu();
    return false;
  });
  $libraryMenu.on('click', '.queue-next', function(){
    player.queueTracksNext(selection.toTrackKeys());
    removeContextMenu();
    return false;
  });
  $libraryMenu.on('click', '.queue-random', function(){
    player.queueOnQueue(selection.toTrackKeys(true));
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
  $libraryMenuPlaylistSubmenu.on('click', 'li', onAddToPlaylistContextMenu);
  $libraryMenu.on('click', '.shuffle', onShuffleContextMenu);
}

function maybeDeleteSelectedPlaylists() {
  var ids = Object.keys(selection.ids.playlist);
  var nameList = [];
  for (var id in selection.ids.playlist) {
    nameList.push(player.playlistTable[id].name);
  }
  var listText = nameList.slice(0, 7).join("\n  ");
  if (nameList.length > 7) {
    listText += "\n  ...";
  }
  var playlistText = nameList.length === 1 ? "playlist" : "playlists";
  var message = "You are about to delete " + nameList.length + " " + playlistText +
    " permanently:\n\n  " + listText;
  if (!confirm(message)) return false;
  player.deletePlaylists(selection.ids.playlist);
  return true;
}

function onDeletePlaylistContextMenu() {
  maybeDeleteSelectedPlaylists();
  removeContextMenu();
  return false;
}

function onRemoveFromPlaylistContextMenu() {
  player.removeItemsFromPlaylists(selection.ids.playlistItem);
  removeContextMenu();
}

function genericTreeUi($elem, options){
  $elem.on('mousedown', 'div.expandable > div.ui-icon', function(ev){
    options.toggleExpansion($(this).closest('li'));
    return false;
  });
  $elem.on('dblclick', 'div.expandable > div.ui-icon', function(){
    return false;
  });
  $elem.on('dblclick', 'div.clickable', queueSelection);
  $elem.on('contextmenu', function(ev){
    return ev.altKey;
  });
  $elem.on('mousedown', '.clickable', function(ev){
    $(document.activeElement).blur();
    var $this = $(this);
    var type = $this.attr('data-type');
    var key = $this.attr('data-key');
    if (ev.which === 1) {
      leftMouseDown(ev);
    } else if (ev.which === 3) {
      if (ev.altKey) {
        return;
      }
      rightMouseDown(ev);
    }
    function leftMouseDown(ev){
      ev.preventDefault();
      removeContextMenu();
      var skipDrag = false;
      if (!options.isSelectionOwner()) {
        selection.selectOnly(type, key);
      } else if (ev.ctrlKey || ev.shiftKey) {
        skipDrag = true;
        selection.cursor = key;
        selection.cursorType = type;
        if (!ev.shiftKey && !ev.ctrlKey) {
          selection.selectOnly(type, key);
        } else if (ev.shiftKey) {
          selectTreeRange();
        } else if (ev.ctrlKey) {
          toggleSelectionUnderCursor();
        }
      } else if (selection.ids[type][key] == null) {
        selection.selectOnly(type, key);
      }
      refreshSelection();
      if (!skipDrag) {
        performDrag(ev, {
          complete: function(result, ev){
            var delta = {
              top: 0,
              bottom: 1
            };
            var keys = selection.toTrackKeys(ev.altKey);
            player.queueOnQueue(keys, result.previousKey, result.nextKey);
          },
          cancel: function(){
            selection.selectOnly(type, key);
            refreshSelection();
          }
        });
      }
    }
    function rightMouseDown(ev){
      ev.preventDefault();
      removeContextMenu();
      if (!options.isSelectionOwner() || selection.ids[type][key] == null) {
        selection.selectOnly(type, key);
        refreshSelection();
      }
      var singleTrack = null;
      if (!selection.isMulti()) {
        if (type === 'track') {
          singleTrack = player.searchResults.trackTable[key];
        } else if (type === 'playlistItem') {
          singleTrack = player.playlistItemTable[key].track;
        }
      }
      var $deletePlaylistLi = $libraryMenu.find('.delete-playlist').closest('li');
      var $removeFromPlaylistLi = $libraryMenu.find('.remove').closest('li');
      var $shuffle = $libraryMenu.find('.shuffle').closest('li');
      if (type === 'playlist') {
        $deletePlaylistLi.show();
      } else {
        $deletePlaylistLi.hide();
      }
      if (type === 'playlistItem') {
        $removeFromPlaylistLi.show();
      } else {
        $removeFromPlaylistLi.hide();
      }

      if (type === 'playlist' || type === 'playlistItem') {
        $shuffle.show();
      } else {
        $shuffle.hide();
      }

      var $downloadItem = $libraryMenu.find('.download');
      if (singleTrack) {
        $downloadItem.attr('href', encodeDownloadHref(singleTrack.file));
      } else {
        $downloadItem.attr('href', makeMultifileDownloadHref());
      }
      $libraryMenu.show().offset({
        left: ev.pageX + 1,
        top: ev.pageY + 1
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

function makeMultifileDownloadHref() {
  var keys = selection.toTrackKeys();
  return "/download/keys?" + keys.join("&");
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
  setUpEventsUi();
  setUpStreamUi();
}

function setUpStreamUi() {
  $streamBtn.button({
    icons: {
      primary: "ui-icon-signal-diag"
    }
  });
  $streamBtn.on('click', toggleStreamStatus);
  $clientVolSlider.slider({
    step: 0.01,
    min: 0,
    max: 1,
    value: localState.clientVolume || 1,
    change: setVol,
    slide: setVol,
  });
  $clientVol.hide();
}

function toQueueItemId(s) {
  return "playlist-track-" + s;
}

function toArtistId(s) {
  return "lib-artist-" + toHtmlId(s);
}

function toAlbumId(s) {
  return "lib-album-" + toHtmlId(s);
}

function toTrackId(s) {
  return "lib-track-" + s;
}

function toPlaylistItemId(s) {
  return "pl-item-" + s;
}

function toPlaylistId(s) {
  // need toHtmlId because jQuery throws a fit with "(incoming)"
  return "pl-pl-" + toHtmlId(s);
}

function handleResize() {
  var eventsScrollTop = $eventsList.scrollTop();

  $nowPlaying.width(MARGIN);

  setAllTabsHeight(MARGIN);
  $queueWindow.height(MARGIN);
  $leftWindow.height(MARGIN);
  $library.height(MARGIN);
  $upload.height(MARGIN);
  $queueItems.height(MARGIN);
  $nowPlaying.width($document.width() - MARGIN * 2);
  var secondLayerTop = $nowPlaying.offset().top + $nowPlaying.height() + MARGIN;
  $leftWindow.offset({
    left: MARGIN,
    top: secondLayerTop
  });
  $queueWindow.offset({
    left: $leftWindow.offset().left + $leftWindow.width() + MARGIN,
    top: secondLayerTop
  });
  $queueWindow.width($window.width() - $queueWindow.offset().left - MARGIN);
  $leftWindow.height($window.height() - $leftWindow.offset().top);
  $queueWindow.height($leftWindow.height() - MARGIN);
  var tabContentsHeight = $leftWindow.height() - $tabs.height() - MARGIN;
  $library.height(tabContentsHeight - $libHeader.height());
  $upload.height(tabContentsHeight);
  $eventsList.height(tabContentsHeight - $eventsOnlineUsers.height() - $chatBox.height());
  $playlists.height(tabContentsHeight - $newPlaylistName.outerHeight());

  setAllTabsHeight(tabContentsHeight);
  $queueItems.height($queueWindow.height() - $queueHeader.position().top - $queueHeader.height());

  if (eventsListScrolledToBottom) {
    scrollEventsToBottom();
  }
}

function refreshPage() {
  location.href = location.protocol + "//" + location.host + "/";
}

function setAllTabsHeight(h) {
  for (var name in tabs) {
    var tab = tabs[name];
    tab.$pane.height(h);
  }
}

function onStreamLabelDown(ev) {
  ev.stopPropagation();
}

function getStreamerCount() {
  var count = player.streamers;
  player.usersList.forEach(function(user) {
    if (user.streaming) count += 1;
  });
  return count;
}

function getStreamStatusLabel() {
  if (tryingToStream) {
    if (actuallyStreaming) {
      if (stillBuffering) {
        return "Buffering";
      } else {
        return "On";
      }
    } else {
      return "Paused";
    }
  } else {
    return "Off";
  }
}

function getStreamButtonLabel() {
  return getStreamerCount() + " Stream: " + getStreamStatusLabel();
}

function renderStreamButton(){
  var label = getStreamButtonLabel();
  $streamBtn
    .button("option", "label", label)
    .prop("checked", tryingToStream)
    .button("refresh");
  $clientVol.toggle(tryingToStream);
}

function toggleStreamStatus() {
  tryingToStream = !tryingToStream;
  sendStreamingStatus();
  renderStreamButton();
  updateStreamPlayer();
  return false;
}

function sendStreamingStatus() {
  socket.send("setStreaming", tryingToStream);
}

function getStreamUrl() {
  // keep the URL relative so that reverse proxies can work
  return "stream.mp3";
}

function onStreamPlaying() {
  stillBuffering = false;
  renderStreamButton();
}

function clearStreamBuffer() {
  if (tryingToStream) {
    tryingToStream = !tryingToStream;
    updateStreamPlayer();
    tryingToStream = !tryingToStream;
    updateStreamPlayer();
  }
}

function updateStreamPlayer() {
  if (actuallyStreaming !== tryingToStream || actuallyPlaying !== player.isPlaying) {
    if (tryingToStream) {
      streamAudio.src = getStreamUrl();
      streamAudio.load();
      if (player.isPlaying) {
        streamAudio.play();
        stillBuffering = true;
        actuallyPlaying = true;
      } else {
        streamAudio.pause();
        stillBuffering = false;
        actuallyPlaying = false;
      }
    } else {
      streamAudio.pause();
      streamAudio.src = "";
      streamAudio.load();
      stillBuffering = false;
      actuallyPlaying = false;
    }
    actuallyStreaming = tryingToStream;
  }
  renderStreamButton();
}

function setVol(ev, ui) {
  if (ev.originalEvent == null) return;
  setStreamVolume(ui.value);
}

function setStreamVolume(v) {
  if (v < 0) v = 0;
  if (v > 1) v = 1;
  streamAudio.volume = v;
  localState.clientVolume = v;
  saveLocalState();
  $clientVolSlider.slider('option', 'value', streamAudio.volume);
}

window.addEventListener('focus', onWindowFocus, false);
window.addEventListener('blur', onWindowBlur, false);
streamAudio.addEventListener('playing', onStreamPlaying, false);
document.getElementById('stream-btn-label').addEventListener('mousedown', onStreamLabelDown, false);
$document.ready(function(){
  loadLocalState();
  socket = new Socket();
  var queryObj = parseQueryString();
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
  socket.on('autoDjOn', function(data) {
    autoDjOn = data;
    renderQueueButtons();
    triggerRenderQueue();
  });
  socket.on('haveAdminUser', function(data) {
    haveAdminUser = data;
    updateHaveAdminUserUi();
  });
  socket.on('connect', function(){
    sendAuth();
    sendStreamingStatus();
    socket.send('subscribe', {name: 'autoDjOn'});
    socket.send('subscribe', {name: 'hardwarePlayback'});
    socket.send('subscribe', {name: 'haveAdminUser'});
    loadStatus = LoadStatus.GoodToGo;
    render();
    ensureSearchHappensSoon();
  });
  player = new PlayerClient(socket);
  player.on('users', function() {
    updateSettingsAuthUi();
    renderEvents();
    renderOnlineUsers();
    renderStreamButton();
  });
  player.on('importProgress', renderImportProgress);
  player.on('libraryupdate', triggerRenderLibrary);
  player.on('queueUpdate', triggerRenderQueue);
  player.on('scanningUpdate', triggerRenderQueue);
  player.on('playlistsUpdate', triggerPlaylistsUpdate);
  player.on('statusupdate', function(){
    renderNowPlaying();
    renderQueueButtons();
    labelQueueItems();
  });
  player.on('events', function() {
    if (activeTab === tabs.events && isBrowserTabActive) {
      player.markAllEventsSeen();
    }
    renderEvents();
  });
  player.on('currentTrack', updateStreamPlayer);
  player.on('streamers', renderStreamButton);
  socket.on('seek', clearStreamBuffer);
  socket.on('disconnect', function(){
    loadStatus = LoadStatus.NoServer;
    render();
  });
  socket.on('error', function(err) {
    console.error(err);
  });

  setUpUi();
  render();
  $window.resize(handleResize);
  window._debug_player = player;
});

function onWindowFocus() {
  isBrowserTabActive = true;
  if (activeTab === tabs.events) {
    player.markAllEventsSeen();
    renderUnseenChatCount();
  }
}

function onWindowBlur() {
  isBrowserTabActive = false;
}

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

function toHtmlId(string) {
  return string.replace(/[^a-zA-Z0-9-]/gm, function(c) {
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

function parseQueryString(s) {
  s = s || location.search.substring(1);
  var o = {};
  var pairs = s.split('&');
  pairs.forEach(function(pair) {
    var keyValueArr = pair.split('=');
    o[keyValueArr[0]] = keyValueArr[1];
  });
  return o;
}

function trimIt(s) {
  return s.trim();
}

function truthy(x) {
  return !!x;
}

function alwaysTrue() {
  return true;
}

function extend(dest, src) {
  for (var name in src) {
    dest[name] = src[name];
  }
  return dest;
}
