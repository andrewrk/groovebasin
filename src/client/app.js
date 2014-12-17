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
      function selRenderArtist(artist){
        for (var i = 0; i < artist.albumList.length; i += 1) {
          var album = artist.albumList[i];
          selRenderAlbum(album);
        }
      }
      function selRenderAlbum(album){
        for (var i = 0; i < album.trackList.length; i += 1) {
          var track = album.trackList[i];
          selRenderTrack(track);
        }
      }
      function selRenderTrack(track){
        trackSet[track.key] = selection.posToArr(getTrackSelPos(track));
      }
      function getTrackSelPos(track){
        return {
          type: 'library',
          artist: track.album.artist,
          album: track.album,
          track: track
        };
      }
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
        for (var i = 0; i < playlist.itemList.length; i += 1) {
          var item = playlist.itemList[i];
          renderPlaylistItem(item);
        }
      }
      function renderPlaylistItem(item){
        trackSet[item.track.key] = selection.posToArr(getItemSelPos(item));
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
      scrollThingToSelection(queueItemsDom, {
        queue: helpers.queue,
      });
    } else if (this.isLibrary()) {
      scrollThingToSelection(libraryDom, {
        track: helpers.track,
        artist: helpers.artist,
        album: helpers.album,
      });
    } else if (this.isPlaylist()) {
      scrollThingToSelection(playlistsListDom, {
        playlist: helpers.playlist,
        playlistItem: helpers.playlistItem,
      });
    }
  },
  scrollToCursor: function() {
    var helpers = this.getHelpers();
    if (!helpers) return;
    if (this.isQueue()) {
      scrollThingToCursor(queueItemsDom, helpers);
    } else if (this.isLibrary()) {
      scrollThingToCursor(libraryDom, helpers);
    } else if (this.isPlaylist()) {
      scrollThingToCursor(playlistsListDom, helpers);
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
        getDiv: function(id) {
          return document.getElementById(toQueueItemId(id));
        },
        toggleExpansion: null,
      },
      artist: {
        ids: this.ids.artist,
        table: player.searchResults.artistTable,
        getDiv: function(id) {
          return document.getElementById(toArtistId(id));
        },
        toggleExpansion: toggleLibraryExpansion,
      },
      album: {
        ids: this.ids.album,
        table: player.searchResults.albumTable,
        getDiv: function(id) {
          return document.getElementById(toAlbumId(id));
        },
        toggleExpansion: toggleLibraryExpansion,
      },
      track: {
        ids: this.ids.track,
        table: player.searchResults.trackTable,
        getDiv: function(id) {
          return document.getElementById(toTrackId(id));
        },
        toggleExpansion: toggleLibraryExpansion,
      },
      playlist: {
        ids: this.ids.playlist,
        table: player.playlistTable,
        getDiv: function(id) {
          return document.getElementById(toPlaylistId(id));
        },
        toggleExpansion: togglePlaylistExpansion,
      },
      playlistItem: {
        ids: this.ids.playlistItem,
        table: player.playlistItemTable,
        getDiv: function(id) {
          return document.getElementById(toPlaylistItemId(id));
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
var abortDrag = noop;
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
var streamBtnDom = document.getElementById('stream-btn');
var clientVolDom = document.getElementById('client-vol');
var queueWindowDom = document.getElementById('queue-window');
var leftWindowDom = document.getElementById('left-window');
var queueItemsDom = document.getElementById('queue-items');
var autoDjDom = document.getElementById('auto-dj');
var queueBtnRepeatDom = document.getElementById('queue-btn-repeat');
var tabsDom = document.getElementById('tabs');
var libraryDom = document.getElementById('library');
var libFilterDom = document.getElementById('lib-filter');
var nowPlayingDom = document.getElementById('nowplaying');
var nowPlayingElapsedDom = document.getElementById('nowplaying-time-elapsed');
var nowPlayingLeftDom = document.getElementById('nowplaying-time-left');
var nowPlayingToggleDom = document.getElementById('nowplaying-toggle');
var nowPlayingToggleIconDom = document.getElementById('nowplaying-toggle-icon');
var nowPlayingPrevDom = document.getElementById('nowplaying-prev');
var nowPlayingNextDom = document.getElementById('nowplaying-next');
var nowPlayingStopDom = document.getElementById('nowplaying-stop');
var uploadByUrlDom = document.getElementById('upload-by-url');
var importByNameDom = document.getElementById('import-by-name');
var mainErrMsgDom = document.getElementById('main-err-msg');
var mainErrMsgTextDom = document.getElementById('main-err-msg-text');
var playlistsListDom = document.getElementById('playlists-list');
var playlistsDom = document.getElementById('playlists');
var uploadDom = document.getElementById('upload');
var trackDisplayDom = document.getElementById('track-display');
var libHeaderDom = document.getElementById('lib-window-header');
var queueHeaderDom = document.getElementById('queue-header');
var autoQueueUploadsDom = document.getElementById('auto-queue-uploads');
var uploadInput = document.getElementById("upload-input");
var uploadWidgetDom = document.getElementById('upload-widget');
var settingsRegisterDom = document.getElementById('settings-register');
var settingsShowAuthDom = document.getElementById('settings-show-auth');
var settingsAuthCancelDom = document.getElementById('settings-auth-cancel');
var settingsAuthSaveDom = document.getElementById('settings-auth-save');
var settingsAuthEditDom = document.getElementById('settings-auth-edit');
var settingsAuthRequestDom = document.getElementById('settings-auth-request');
var settingsAuthLogoutDom = document.getElementById('settings-auth-logout');
var streamUrlDom = document.getElementById('settings-stream-url');
var authPermReadDom = document.getElementById('auth-perm-read');
var authPermAddDom = document.getElementById('auth-perm-add');
var authPermControlDom = document.getElementById('auth-perm-control');
var authPermAdminDom = document.getElementById('auth-perm-admin');
var lastFmSignOutDom = document.getElementById('lastfm-sign-out');
var lastFmAuthUrlDom = document.getElementById('lastfm-auth-url');
var settingsLastFmInDom = document.getElementById('settings-lastfm-in');
var settingsLastFmOutDom = document.getElementById('settings-lastfm-out');
var settingsLastFmUserDom = document.getElementById('settings-lastfm-user');
var toggleScrobbleDom = document.getElementById('toggle-scrobble');
var shortcutsDom = document.getElementById('shortcuts');
var editTagsDialogDom = document.getElementById('edit-tags');
var toggleHardwarePlaybackDom = document.getElementById('toggle-hardware-playback');
var toggleHardwarePlaybackLabel = document.getElementById('toggle-hardware-playback-label');
var newPlaylistNameDom = document.getElementById('new-playlist-name');
var emptyLibraryMessageDom = document.getElementById('empty-library-message');
var libraryNoItemsDom = document.getElementById('library-no-items');
var libraryArtistsDom = document.getElementById('library-artists');
var volNumDom = document.getElementById('vol-num');
var volWarningDom = document.getElementById('vol-warning');
var ensureAdminDiv = document.getElementById('ensure-admin');
var authShowPasswordDom = document.getElementById('auth-show-password');
var authUsernameDom = document.getElementById('auth-username');
var authUsernameDisplayDom = document.getElementById('auth-username-display');
var authPasswordDom = document.getElementById('auth-password');
var settingsUsersDom = document.getElementById('settings-users');
var settingsUsersSelect = document.getElementById('settings-users-select');
var settingsRequestsDom = document.getElementById('settings-requests');
var userPermReadDom = document.getElementById('user-perm-read');
var userPermAddDom = document.getElementById('user-perm-add');
var userPermControlDom = document.getElementById('user-perm-control');
var userPermAdminDom = document.getElementById('user-perm-admin');
var settingsDelUserDom = document.getElementById('settings-delete-user');
var requestReplaceSelect = document.getElementById('request-replace');
var requestNameDom = document.getElementById('request-name');
var requestApproveDom = document.getElementById('request-approve');
var requestDenyDom = document.getElementById('request-deny');
var eventsOnlineUsersDom = document.getElementById('events-online-users');
var eventsListDom = document.getElementById('events-list');
var chatBoxDom = document.getElementById('chat-box');
var chatBoxInputDom = document.getElementById('chat-box-input');
var queueDurationDom = document.getElementById('queue-duration');
var queueDurationLabel = document.getElementById('queue-duration-label');
var importProgressDom = document.getElementById('import-progress');
var importProgressListDom = document.getElementById('import-progress-list');
var perDom = document.getElementById('edit-tags-per');
var perLabelDom = document.getElementById('edit-tags-per-label');
var prevDom = document.getElementById('edit-tags-prev');
var nextDom = document.getElementById('edit-tags-next');
var editTagsFocusDom = document.getElementById('edit-tag-name');
var eventsTabSpan = document.getElementById('events-tab-label');
var importTabSpan = document.getElementById('import-tab-label');

// needed for jQuery UI
var $shortcuts = $(shortcutsDom);
var $queueBtnRepeat = $(queueBtnRepeatDom);
var $autoDj = $(autoDjDom);
var $editTagsDialog = $(editTagsDialogDom);
var $autoQueueUploads = $(autoQueueUploadsDom);
var $toggleScrobble = $(toggleScrobbleDom);
var $toggleHardwarePlayback = $(toggleHardwarePlaybackDom);
var $settingsAuthEdit = $(settingsAuthEditDom);
var $settingsAuthSave = $(settingsAuthSaveDom);
var $settingsAuthCancel = $(settingsAuthCancelDom);
var $settingsAuthRequest = $(settingsAuthRequestDom);
var $settingsAuthLogout = $(settingsAuthLogoutDom);
var $userPermRead = $(userPermReadDom);
var $userPermAdd = $(userPermAddDom);
var $userPermControl = $(userPermControlDom);
var $userPermAdmin = $(userPermAdminDom);
var $settingsDeleteUser = $(settingsDelUserDom);
var $streamBtn = $(streamBtnDom);
var $clientVolSlider = $('#client-vol-slider');
var $trackSlider = $('#track-slider');
var $volSlider = $('#vol-slider');
var $libraryMenu = $('#library-menu');
var $ensureAdminBtn = $('#ensure-admin-btn');
var $libraryMenuPlaylistSubmenu = $('#library-menu-playlist-submenu');

var tabs = {
  library: {
    pane: document.getElementById('library-pane'),
    tab: document.getElementById('library-tab'),
  },
  upload: {
    pane: document.getElementById('upload-pane'),
    tab: document.getElementById('upload-tab'),
  },
  playlists: {
    pane: document.getElementById('playlists-pane'),
    tab: document.getElementById('playlists-tab'),
  },
  events: {
    pane: document.getElementById('events-pane'),
    tab: document.getElementById('events-tab'),
  },
  settings: {
    pane: document.getElementById('settings-pane'),
    tab: document.getElementById('settings-tab'),
  },
};
var activeTab = tabs.library;
var triggerRenderLibrary = makeRenderCall(renderLibrary, 100);
var triggerRenderQueue = makeRenderCall(renderQueue, 100);
var triggerPlaylistsUpdate = makeRenderCall(updatePlaylistsUi, 100);
var triggerResize = makeRenderCall(resizeDomElements, 20);
var keyboardHandlers = (function(){
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
    // e, E
    69: {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(ev) {
        if (ev.shiftKey) {
          onEditTagsContextMenu(ev);
        } else {
          clickTab(tabs.settings);
        }
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
        newPlaylistNameDom.focus();
        newPlaylistNameDom.select();
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
        chatBoxInputDom.focus();
        chatBoxInputDom.select();
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
        uploadByUrlDom.focus();
        uploadByUrlDom.select();
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
            height: window.innerHeight - 40,
          });
          $shortcuts.focus();
        } else {
          clickTab(tabs.library);
          libFilterDom.focus();
          libFilterDom.select();
          selection.fullClear();
          refreshSelection();
        }
      },
    },
  };

  function upDownHandler(ev) {
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
    selection.scrollToCursor();
  }

  function leftRightHandler(ev) {
    var dir = ev.which === 37 ? -1 : 1;
    var helpers = selection.getHelpers();
    if (!helpers) return;
    var helper = helpers[selection.cursorType];
    if (helper && helper.toggleExpansion) {
      var selectedItem = helper.table[selection.cursor];
      var isExpandedFuncs = {
        artist: isArtistExpanded,
        album: isAlbumExpanded,
        track: alwaysTrue,
        playlist: isPlaylistExpanded,
        playlistItem: alwaysTrue,
      };
      var isExpanded = isExpandedFuncs[selection.cursorType](selectedItem);
      var li = helper.getDiv(selection.cursor).parentNode;
      if (dir > 0) {
        if (!isExpanded) {
          helper.toggleExpansion(li);
        }
      } else {
        if (isExpanded) {
          helper.toggleExpansion(li);
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
})();

var editTagsTrackKeys = null;
var editTagsTrackIndex = null;

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
var chatCommands = {
  nick: changeUserName,
  me: displaySlashMe,
};
var escapeHtmlReplacements = { "&": "&amp;", '"': "&quot;", "<": "&lt;", ">": "&gt;" };

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
var searchTimer = null;

window.addEventListener('focus', onWindowFocus, false);
window.addEventListener('blur', onWindowBlur, false);
streamAudio.addEventListener('playing', onStreamPlaying, false);
document.getElementById('stream-btn-label').addEventListener('mousedown', onStreamLabelDown, false);

init();

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

function scrollThingToCursor(scrollArea, helpers) {
  var helper = helpers[selection.cursorType];
  var div = helper.getDiv(selection.cursor);
  var itemTop = div.getBoundingClientRect().top;
  var itemBottom = itemTop + div.clientHeight;
  scrollAreaIntoView(scrollArea, itemTop, itemBottom);
}

function scrollAreaIntoView(scrollArea, itemTop, itemBottom) {
  var scrollAreaTop = scrollArea.getBoundingClientRect().top;
  var selectionTop = itemTop - scrollAreaTop;
  var selectionBottom = itemBottom - scrollAreaTop - scrollArea.clientHeight;
  var scrollAmt = scrollArea.scrollTop;
  if (selectionTop < 0) {
    scrollArea.scrollTop = scrollAmt + selectionTop;
  } else if (selectionBottom > 0) {
    scrollArea.scrollTop = scrollAmt + selectionBottom;
  }
}

function scrollThingToSelection(scrollArea, helpers){
  var topPos = null;
  var bottomPos = null;

  var helper;
  for (var selName in helpers) {
    helper = helpers[selName];
    for (var id in helper.ids) {
      var div = helper.getDiv(id);
      var itemTop = div.getBoundingClientRect().top;
      var itemBottom = itemTop + div.clientHeight;
      if (topPos == null || itemTop < topPos) {
        topPos = itemTop;
      }
      if (bottomPos == null || itemBottom > bottomPos) {
        bottomPos = itemBottom;
      }
    }
  }

  if (topPos != null) {
    scrollAreaIntoView(scrollArea, topPos, bottomPos);
  }
}

function getDragPosition(x, y) {
  var result = {};
  var plItemDom = queueItemsDom.querySelectorAll(".pl-item");
  for (var i = 0; i < plItemDom.length; ++i) {
    var item = plItemDom[i];
    var $item = $(item);
    var middle = $item.offset().top + $item.height() / 2;
    var track = player.queue.itemTable[$item.get(0).getAttribute('data-id')];
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

function renderQueueButtons() {
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
  ensureAdminDiv.style.display = haveAdminUser ? "none" : "";
}

function renderQueue(){
  var itemList = player.queue.itemList || [];
  var scrollTop = queueItemsDom.scrollTop;

  // add the missing dom entries
  var i;
  for (i = queueItemsDom.childElementCount; i < itemList.length; i += 1) {
    queueItemsDom.insertAdjacentHTML('beforeend',
      '<div class="pl-item">' +
        '<span class="track"></span>' +
        '<span class="title"></span>' +
        '<span class="artist"></span>' +
        '<span class="album"></span>' +
        '<span class="time"></span>' +
      '</div>');
  }
  // remove the extra dom entries
  while (itemList.length < queueItemsDom.childElementCount) {
    queueItemsDom.removeChild(queueItemsDom.lastChild);
  }

  // overwrite existing dom entries
  var domItems = queueItemsDom.children;
  for (i = 0; i < itemList.length; i += 1) {
    var domItem = domItems[i];
    var item = itemList[i];
    domItem.setAttribute('id', toQueueItemId(item.id));
    domItem.setAttribute('data-id', item.id);
    var track = item.track;
    domItem.children[0].textContent = track.track || "";
    domItem.children[1].textContent = track.name || "";
    domItem.children[2].textContent = track.artistName || "";
    domItem.children[3].textContent = track.albumName || "";

    var timeText = player.isScanning(track) ? "scan" : formatTime(track.duration);
    domItem.children[4].textContent = timeText;
  }

  refreshSelection();
  labelQueueItems();
  queueItemsDom.scrollTop = scrollTop;
}

function updateQueueDuration() {
  var duration = 0;
  var allAreKnown = true;

  if (selection.isQueue()) {
    selection.toTrackKeys().forEach(addKeyDuration);
    queueDurationLabel.textContent = "Selection:";
  } else {
    player.queue.itemList.forEach(addItemDuration);
    queueDurationLabel.textContent = "Play Queue:";
  }
  queueDurationDom.textContent = formatTime(duration) + (allAreKnown ? "" : "?");

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

function removeSelectedAndCursorClasses(domItem) {
  domItem.classList.remove('selected');
  domItem.classList.remove('cursor');
}

function removeCurrentOldAndRandomClasses(domItem) {
  domItem.classList.remove('current');
  domItem.classList.remove('old');
  domItem.classList.remove('random');
}

function labelQueueItems() {
  var item;
  var curItem = player.currentItem;
  Array.prototype.forEach.call(queueItemsDom.getElementsByClassName('pl-item'), removeCurrentOldAndRandomClasses);
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
  Array.prototype.forEach.call(queueItemsDom.getElementsByClassName('pl-item'), removeSelectedAndCursorClasses);
  Array.prototype.forEach.call(libraryArtistsDom.getElementsByClassName('clickable'), removeSelectedAndCursorClasses);
  Array.prototype.forEach.call(playlistsListDom.getElementsByClassName('clickable'), removeSelectedAndCursorClasses);

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
      helper.getDiv(id).classList.add('selected');
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
        helper.getDiv(selection.cursor).classList.add('cursor');
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
  var scrollTop = playlistsDom.scrollTop;

  // add the missing dom entries
  var i;
  for (i = playlistsListDom.childElementCount; i < playlistList.length; i += 1) {
    playlistsListDom.insertAdjacentHTML('beforeend',
      '<li>' +
        '<div class="clickable expandable" data-type="playlist">' +
          '<div class="ui-icon"></div>' +
          '<span></span>' +
        '</div>' +
        '<ul></ul>' +
      '</li>');
  }
  // remove the extra dom entries
  while (playlistList.length < playlistsListDom.childElementCount) {
    playlistsListDom.removeChild(playlistsListDom.lastChild);
  }

  // overwrite existing dom entries
  var playlist;
  var domItems = playlistsListDom.children;
  for (i = 0; i < playlistList.length; i += 1) {
    var domItem = domItems[i];
    playlist = playlistList[i];
    domItem.setAttribute('data-cached', "");
    var divDom = domItem.children[0];
    divDom.setAttribute('id', toPlaylistId(playlist.id));
    divDom.setAttribute('data-key', playlist.id);
    var iconDom = divDom.children[0];
    iconDom.classList.add(ICON_COLLAPSED);
    iconDom.classList.remove(ICON_EXPANDED);
    var spanDom = divDom.children[1];
    spanDom.textContent = playlist.name;
    var ulDom = domItem.children[1];
    ulDom.style.display = 'block';
    while (ulDom.firstChild) {
      ulDom.removeChild(ulDom.firstChild);
    }
  }

  playlistsDom.scrollTop = scrollTop;
  refreshSelection();
  // TODO expandPlaylistsToSelection()
}

function renderLibrary() {
  var artistList = player.searchResults.artistList || [];
  var scrollTop = libraryDom.scrollTop;

  emptyLibraryMessageDom.textContent = player.haveFileListCache ? "No Results" : "loading...";
  libraryNoItemsDom.style.display = artistList.length ? "none" : "";

  // add the missing dom entries
  var i;
  for (i = libraryArtistsDom.childElementCount; i < artistList.length; i += 1) {
    libraryArtistsDom.insertAdjacentHTML('beforeend',
      '<li>' +
        '<div class="clickable expandable" data-type="artist">' +
          '<div class="ui-icon"></div>' +
          '<span></span>' +
        '</div>' +
        '<ul></ul>' +
      '</li>');
  }
  // remove the extra dom entries
  while (artistList.length < libraryArtistsDom.childElementCount) {
    libraryArtistsDom.removeChild(libraryArtistsDom.lastChild);
  }

  // overwrite existing dom entries
  var artist;
  var domItems = libraryArtistsDom.children;
  for (i = 0; i < artistList.length; i += 1) {
    var domItem = domItems[i];
    artist = artistList[i];
    domItem.setAttribute('data-cached', "");
    var divDom = domItem.children[0];
    divDom.setAttribute('id', toArtistId(artist.key));
    divDom.setAttribute('data-key', artist.key);
    var iconDom = divDom.children[0];
    iconDom.classList.add(ICON_COLLAPSED);
    iconDom.classList.remove(ICON_EXPANDED);
    var spanDom = divDom.children[1];
    spanDom.textContent = artistDisplayName(artist.name);
    var ulDom = domItem.children[1];
    ulDom.style.display = 'block';
    while (ulDom.firstChild) {
      ulDom.removeChild(ulDom.firstChild);
    }
  }

  var nodeCount = artistList.length;
  expandStuff(domItems);
  libraryDom.scrollTop = scrollTop;
  refreshSelection();
  expandLibraryToSelection();

  function expandStuff(liSet) {
    if (nodeCount >= AUTO_EXPAND_LIMIT) return;
    for (var i = 0; i < liSet.length; i += 1) {
      var li = liSet[i];
      if (nodeCount <= AUTO_EXPAND_LIMIT) {
        var ul = li.children[1];
        if (!ul) continue;
        toggleLibraryExpansion(li);
        nodeCount += ul.children.length;
        expandStuff(ul.children);
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
  nowPlayingElapsedDom.textContent = formatTime(elapsed);
  nowPlayingLeftDom.textContent = formatTime(duration);
}

function renderVolumeSlider() {
  if (userIsVolumeSliding) return;

  $volSlider.slider('option', 'value', player.volume);
  volNumDom.textContent = Math.round(player.volume * 100);
  volWarningDom.style.display = (player.volume > 1) ? "" : "none";
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
  if (track) {
    trackDisplayDom.textContent = getNowPlayingText(track);
  } else {
    trackDisplayDom.innerHTML = "&nbsp;";
  }
  var oldClass = (player.isPlaying === true) ? 'ui-icon-play' : 'ui-icon-pause';
  var newClass = (player.isPlaying === true) ? 'ui-icon-pause': 'ui-icon-play';
  nowPlayingToggleIconDom.classList.remove(oldClass);
  nowPlayingToggleIconDom.classList.add(newClass);
  $trackSlider.slider("option", "disabled", player.isPlaying == null);
  updateSliderPos();
  renderVolumeSlider();
}

function render() {
  var hideMainErr = (loadStatus === LoadStatus.GoodToGo);
  queueWindowDom.style.display= hideMainErr ? "" : "none";
  leftWindowDom.style.display = hideMainErr ? "" : "none";
  nowPlayingDom.style.display = hideMainErr ? "" : "none";
  mainErrMsgDom.style.display = hideMainErr ? "none" : "";
  if (!hideMainErr) {
    document.title = BASE_TITLE;
    mainErrMsgTextDom.textContent = loadStatus;
    return;
  }
  renderQueue();
  renderQueueButtons();
  renderLibrary();
  renderNowPlaying();
  updateSettingsAuthUi();
  updateLastFmSettingsUi();
  resizeDomElements();
}

function renderArtist(ul, albumList) {
  var $ul = $(ul);
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

function renderPlaylist(ul, playlist) {
  var $ul = $(ul);
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

function isDomItemVisible(domItem) {
  return domItem.offsetParent !== null;
}

function toggleDisplay(domItem) {
  domItem.style.display = isDomItemVisible(domItem) ? "none" : "";
}

function genericToggleExpansion(li, options) {
  var topLevelType = options.topLevelType;
  var renderDom = options.renderDom;
  var div = li.children[0];
  var ul = li.children[1];
  if (div.getAttribute('data-type') === topLevelType &&
      !li.getAttribute('data-cached'))
  {
    li.setAttribute('data-cached', "1");
    var key = div.getAttribute('data-key');
    renderDom(ul, key);
    refreshSelection();
  } else {
    toggleDisplay(ul);
  }
  var isVisible = isDomItemVisible(ul);
  var oldClass = isVisible ? ICON_COLLAPSED : ICON_EXPANDED;
  var newClass = isVisible ? ICON_EXPANDED  : ICON_COLLAPSED;
  div.children[0].classList.remove(oldClass);
  div.children[0].classList.add(newClass);
}

function toggleLibraryExpansion(li) {
  genericToggleExpansion(li, {
    topLevelType: 'artist',
    renderDom: function(ul, key) {
      var albumList = player.searchResults.artistTable[key].albumList;
      renderArtist(ul, albumList);
    },
  });
}

function togglePlaylistExpansion(li) {
  genericToggleExpansion(li, {
    topLevelType: 'playlist',
    renderDom: function(ul, key) {
      var playlist = player.playlistTable[key];
      renderPlaylist(ul, playlist);
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

function nextRepeatState(ev) {
  player.setRepeatMode((player.repeat + 1) % repeatModeNames.length);
}

function bumpVolume(v) {
  if (tryingToStream) {
    setStreamVolume(streamAudio.volume + v);
  } else {
    player.setVolume(player.volume + v);
  }
}

function removeContextMenu() {
  if ($libraryMenu.is(":visible")) {
    $libraryMenu.hide();
    return true;
  }
  return false;
}

function isPlaylistExpanded(playlist){
  var $li = $("#" + toPlaylistId(playlist.id)).closest("li");
  if (!$li.get(0).getAttribute('data-cached')) return false;
  return $li.find("> ul").is(":visible");
}

function isArtistExpanded(artist){
  var artistHtmlId = toArtistId(artist.key);
  var artistElem = document.getElementById(artistHtmlId);
  var $li = $(artistElem).closest('li');
  if (!$li.get(0).getAttribute('data-cached')) return false;
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

function queueSelection(ev) {
  ev.preventDefault();
  ev.stopPropagation();
  var keys = selection.toTrackKeys(ev.altKey);
  if (ev.shiftKey) {
    player.queueTracksNext(keys);
  } else {
    player.queueOnQueue(keys);
  }
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

function settingsAuthSave(ev) {
  localState.authUsername = authUsernameDom.value;
  localState.authPassword = authPasswordDom.value;
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

function settingsAuthCancel(ev) {
  hideShowAuthEdit(false);
}

function hideShowAuthEdit(visible) {
  settingsRegisterDom.style.display = visible ? "" : "none";
  settingsShowAuthDom.style.display = visible ? "none" : "";
}

function removeAllQueueItemBorders() {
  Array.prototype.forEach.call(queueItemsDom.getElementsByClassName('pl-item'), function(domItem) {
    domItem.classList.remove('border-top');
    domItem.classList.remove('border-bottom');
  });
}

function performDrag(ev, callbacks) {
  abortDrag();
  var startDragX = ev.pageX;
  var startDragY = ev.pageY;
  abortDrag = doAbortDrag;
  window.addEventListener('mousemove', onDragMove, false);
  window.addEventListener('mouseup', onDragEnd, false);
  onDragMove(ev);

  function doAbortDrag() {
    window.removeEventListener('mousemove', onDragMove, false);
    window.removeEventListener('mouseup', onDragEnd, false);
    if (startedDrag) {
      removeAllQueueItemBorders();
      startedDrag = false;
    }
    abortDrag = noop;
  }
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
    removeAllQueueItemBorders();
    if (result.$next != null) {
      result.$next.addClass("border-top");
    } else if (result.$previous != null) {
      result.$previous.addClass("border-bottom");
    }
  }
  function onDragEnd(ev) {
    if (ev.which !== 1) {
      ev.preventDefault();
      ev.stopPropagation();
      return;
    }
    if (startedDrag) {
      callbacks.complete(getDragPosition(ev.pageX, ev.pageY), ev);
    } else {
      callbacks.cancel();
    }
    abortDrag();
  }
}

function clearSelectionAndHideMenu() {
  removeContextMenu();
  selection.fullClear();
  refreshSelection();
}

function onWindowKeyDown(ev) {
  var handler = keyboardHandlers[ev.which];
  if (handler == null) return;
  if (handler.ctrl  != null && handler.ctrl  !== ev.ctrlKey)  return;
  if (handler.alt   != null && handler.alt   !== ev.altKey)   return;
  if (handler.shift != null && handler.shift !== ev.shiftKey) return;
  ev.preventDefault();
  ev.stopPropagation();
  handler.handler(ev);
}

function onShortcutsWindowKeyDown(ev) {
  ev.stopPropagation();
  if (ev.which === 27) {
    $shortcuts.dialog('close');
  }
}

function setUpGenericUi() {
  // $ when we get rid of jQuery UI, this code should be handled in CSS instead of JavaScript
  Array.prototype.forEach.call(document.getElementsByClassName('hoverable'), function(domItem) {
    domItem.addEventListener('mouseover', function(ev) {
      domItem.classList.add('ui-state-hover');
    }, false);
    domItem.addEventListener('mouseout', function(ev) {
      domItem.classList.remove('ui-state-hover');
    }, false);
  });

  $(".jquery-button")
    .button()
    .on('click', blurThis);

  window.addEventListener('mousedown', clearSelectionAndHideMenu, false);
  window.addEventListener('keydown', onWindowKeyDown, false);

  shortcutsDom.addEventListener('keydown', onShortcutsWindowKeyDown, false);
}

function blurThis() {
  this.blur();
}

function handleAutoDjClick(ev) {
  setAutoDj(autoDjDom.checked);
  ev.preventDefault();
  ev.stopPropagation();
}

function getFirstChildToward(parentDom, childDom) {
  if (childDom === parentDom) return null;
  for (;;) {
    var nextNode = childDom.parentNode;
    if (nextNode === parentDom) return childDom;
    childDom = nextNode;
  }
}

function onQueueItemsDblClick(ev) {
  var clickedPlItem = getFirstChildToward(queueItemsDom, ev.target);
  if (!clickedPlItem) return;

  var trackId = clickedPlItem.getAttribute('data-id');
  player.seek(trackId, 0);
  player.play();
}

function onQueueItemsContextMenu(ev) {
  if (ev.target === queueItemsDom) return;
  if (ev.altKey) return;
  ev.preventDefault();
  ev.stopPropagation();
}

function onQueueItemsMouseDown(ev) {
  var clickedPlItem = getFirstChildToward(queueItemsDom, ev.target);
  if (!clickedPlItem) return;
  if (startedDrag) return;
  ev.preventDefault();
  ev.stopPropagation();
  document.activeElement.blur();
  var trackId, skipDrag;
  if (ev.which === 1) {
    removeContextMenu();
    trackId = clickedPlItem.getAttribute('data-id');
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
          player.moveIds(Object.keys(selection.ids.queue), result.previousKey, result.nextKey);
        },
        cancel: function(){
          selection.selectOnly('queue', trackId);
          refreshSelection();
        }
      });
    }
  } else if (ev.which === 3) {
    if (ev.altKey) return;
    trackId = clickedPlItem.getAttribute('data-id');
    if (!selection.isQueue() || selection.ids.queue[trackId] == null) {
      selection.selectOnly('queue', trackId);
      refreshSelection();
    }
    popContextMenu('queue', ev.pageX, ev.pageY);
  }
}

function setUpPlayQueueUi() {
  queueBtnRepeatDom.addEventListener('click', nextRepeatState, false);
  autoDjDom.addEventListener('click', handleAutoDjClick, false);

  queueItemsDom.addEventListener('dblclick', onQueueItemsDblClick, false);
  queueItemsDom.addEventListener('contextmenu', onQueueItemsContextMenu, false);
  queueItemsDom.addEventListener('mousedown', onQueueItemsMouseDown, false);
}

function popContextMenu(type, x, y) {
  removeContextMenu();
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
    $removeFromPlaylistLi.find('a').text("Remove from Playlist");
  } else if (type === 'playlist') {
    $removeFromPlaylistLi.show();
    $removeFromPlaylistLi.find('a').text("Clear Playlist");
  } else if (type === 'queue') {
    $removeFromPlaylistLi.show();
    $removeFromPlaylistLi.find('a').text("Remove from Queue");
  } else {
    $removeFromPlaylistLi.hide();
  }

  if (type === 'playlist' || type === 'playlistItem' || type === 'queue') {
    $shuffle.show();
  } else {
    $shuffle.hide();
  }
  $libraryMenu.find('.download').attr('href', makeDownloadHref());
  updateMenuDisableState($libraryMenu);
  $libraryMenu.menu('refresh');

  // make it so that the mouse cursor is not immediately over the menu
  var leftPos = x + 1;
  var topPos = y + 1;
  // avoid menu going outside document boundaries
  if (leftPos + $libraryMenu.width() >= window.innerWidth) {
    leftPos = x - $libraryMenu.width() - 1;
  }
  if (topPos + $libraryMenu.height() >= window.innerHeight) {
    topPos = y - $libraryMenu.height() - 1;
  }

  $libraryMenu.show().offset({
    left: leftPos,
    top: topPos
  });
}

function onShuffleContextMenu(ev) {
  ev.preventDefault();
  ev.stopPropagation();
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
}

function onNewPlaylistNameKeyDown(ev) {
  ev.stopPropagation();

  if (ev.which === 27) {
    newPlaylistNameDom.value = "";
    newPlaylistNameDom.blur();
  } else if (ev.which === 13) {
    var name = newPlaylistNameDom.value.trim();
    if (name.length > 0) {
      player.createPlaylist(name);
      newPlaylistNameDom.value = "";
    }
  } else if (ev.which === 40) {
    // down
    selection.selectOnlyFirstPos('playlist');
    selection.scrollToCursor();
    refreshSelection();
    newPlaylistNameDom.blur();
  } else if (ev.which === 38) {
    // up
    selection.selectOnlyLastPos('playlist');
    selection.scrollToCursor();
    refreshSelection();
    newPlaylistNameDom.blur();
  }
}

function setUpPlaylistsUi() {
  newPlaylistNameDom.addEventListener('keydown', onNewPlaylistNameKeyDown, false);

  genericTreeUi($(playlistsListDom), {
    toggleExpansion: togglePlaylistExpansion,
    isSelectionOwner: function() {
      return selection.isPlaylist();
    },
  });
}

function stopPropagation(ev) {
  ev.stopPropagation();
}

function onDownloadContextMenu(ev) {
  removeContextMenu();
}

function onDeleteContextMenu(ev) {
  ev.preventDefault();
  ev.stopPropagation();
  if (!havePerm('admin')) return;
  removeContextMenu();
  handleDeletePressed(true);
}

function onAddToPlaylistContextMenu(ev) {
  ev.preventDefault();
  ev.stopPropagation();
  if (!havePerm('control')) return;
  var keysList = selection.toTrackKeys();
  var playlistId = this.getAttribute('data-key');
  player.queueOnPlaylist(playlistId, keysList);
  removeContextMenu();
}

function onEditTagsContextMenu(ev) {
  ev.preventDefault();
  ev.stopPropagation();
  if (!havePerm('admin')) return;
  removeContextMenu();
  editTagsTrackKeys = selection.toTrackKeys();
  editTagsTrackIndex = 0;
  showEditTags();
}

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
    domItem.readOnly = !propInfo.write;
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
    minWidth: 650,
    height: Math.min(640, window.innerHeight - 40),
  });
  perDom.checked = false;
  updateEditTagsUi();
  editTagsFocusDom.focus();
  editTagsFocusDom.select();
}

function setUpEditTagsUi() {
  Array.prototype.forEach.call(editTagsDialogDom.getElementsByTagName("input"), function(domItem) {
    domItem.addEventListener('keydown', onInputKeyDown, false);
  });
  for (var propName in EDITABLE_PROPS) {
    var domItem = document.getElementById('edit-tag-' + propName);
    var multiCheckBoxDom = document.getElementById('edit-tag-multi-' + propName);
    var listener = createChangeListener(multiCheckBoxDom);
    domItem.addEventListener('change', listener, false);
    domItem.addEventListener('keypress', listener, false);
    domItem.addEventListener('focus', onFocus, false);
  }

  function onInputKeyDown(ev) {
    ev.stopPropagation();
    if (ev.which === 27) {
      $editTagsDialog.dialog('close');
    } else if (ev.which === 13) {
      saveAndClose();
    }
  }

  function onFocus(ev) {
    editTagsFocusDom = ev.target;
  }

  function createChangeListener(multiCheckBoxDom) {
    return function() {
      multiCheckBoxDom.checked = true;
    };
  }
  document.getElementById('edit-tags-ok').addEventListener('click', saveAndClose, false);
  document.getElementById('edit-tags-cancel').addEventListener('click', closeDialog, false);
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

function onNowPlayingToggleMouseDown(ev) {
  ev.preventDefault();
  ev.stopPropagation();
  togglePlayback();
}

function onNowPlayingPrevMouseDown(ev) {
  ev.preventDefault();
  ev.stopPropagation();
  player.prev();
}

function onNowPlayingNextMouseDown(ev) {
  ev.preventDefault();
  ev.stopPropagation();
  player.next();
}

function onNowPlayingStopMouseDown(ev) {
  ev.preventDefault();
  ev.stopPropagation();
  player.stop();
}

function setUpNowPlayingUi() {
  nowPlayingToggleDom.addEventListener('mousedown', onNowPlayingToggleMouseDown, false);
  nowPlayingPrevDom.addEventListener('mousedown', onNowPlayingPrevMouseDown, false);
  nowPlayingNextDom.addEventListener('mousedown', onNowPlayingNextMouseDown, false);
  nowPlayingStopDom.addEventListener('mousedown', onNowPlayingStopMouseDown, false);

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
      nowPlayingElapsedDom.textContent = formatTime(ui.value * player.currentItem.track.duration);
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
    volNumDom.textContent = Math.round(val * 100);
    volWarningDom.style.display = (val > 1) ? "" : "none";
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
}

function clickTab(tab) {
  unselectTabs();
  tab.tab.classList.add('ui-state-active');
  tab.pane.style.display = "";
  activeTab = tab;
  triggerResize();
  if (tab === tabs.events) {
    player.markAllEventsSeen();
    renderUnseenChatCount();
  }
}

function setUpTabListener(tab) {
  tab.tab.addEventListener('click', function(ev) {
    clickTab(tab);
  }, false);
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
    tab.tab.classList.remove('ui-state-active');
    tab.pane.style.display = "none";
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
  var cancelBtnDom = document.createElement('button');
  cancelBtnDom.textContent = "Cancel";
  cancelBtnDom.addEventListener('click', onCancel, false);

  $(uploadWidgetDom).append($progressBar);
  uploadWidgetDom.appendChild(cancelBtnDom);

  var req = new XMLHttpRequest();
  req.upload.addEventListener('progress', onProgress, false);
  req.addEventListener('load', onLoad, false);
  req.open('POST', '/upload');
  req.send(formData);
  uploadInput.value = null;

  function onProgress(ev) {
    if (!ev.lengthComputable) return;
    var progress = ev.loaded / ev.total;
    $progressBar.progressbar("option", "value", progress * 100);
  }

  function onLoad(ev) {
    cleanup();
  }

  function onCancel(ev) {
    req.abort();
    cleanup();
  }

  function cleanup() {
    $progressBar.remove();
    cancelBtnDom.parentNode.removeChild(cancelBtnDom);
  }
}

function setAutoUploadBtnState() {
  $autoQueueUploads
    .button('option', 'label', localState.autoQueueUploads ? 'On' : 'Off')
    .prop('checked', localState.autoQueueUploads)
    .button('refresh');
}

function onAutoQueueUploadClick(ev) {
  localState.autoQueueUploads = autoQueueUploadsDom.checked;
  saveLocalState();
  setAutoUploadBtnState();
}

function onUploadByUrlKeyDown(ev) {
  ev.stopPropagation();
  if (ev.which === 27) {
    uploadByUrlDom.value = "";
    uploadByUrlDom.blur();
  } else if (ev.which === 13) {
    importUrl();
  }
}

function onImportByNameKeyDown(ev) {
  ev.stopPropagation();
  if (ev.which === 27) {
    importByNameDom.value = "";
    importByNameDom.blur();
  } else if (ev.which === 13 && ev.ctrlKey) {
    importNames();
  }
}

function onUploadInputChange(ev) {
  uploadFiles(this.files);
}

function setUpUploadUi() {
  $autoQueueUploads.button({ label: "..." });
  setAutoUploadBtnState();
  autoQueueUploadsDom.addEventListener('click', onAutoQueueUploadClick, false);
  uploadInput.addEventListener('change', onUploadInputChange, false);
  uploadByUrlDom.addEventListener('keydown', onUploadByUrlKeyDown, false);
  importByNameDom.addEventListener('keydown', onImportByNameKeyDown, false);
}

function importUrl() {
  var url = uploadByUrlDom.value;
  uploadByUrlDom.value = "";
  uploadByUrlDom.blur();
  socket.send('importUrl', {
    url: url,
    autoQueue: !!localState.autoQueueUploads,
  });
}

function importNames() {
  var namesText = importByNameDom.value;
  var namesList = namesText.split("\n").map(trimIt).filter(truthy);
  importByNameDom.value = "";
  importByNameDom.blur();
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
  settingsLastFmInDom.style.display = localState.lastfm.username ? "" : "none";
  settingsLastFmOutDom.style.display = localState.lastfm.username ? "none" : "";
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
  var i, user, newOption;
  var request = null;
  var selectedUserId = settingsUsersSelect.value;
  while (settingsUsersSelect.options.length) {
    settingsUsersSelect.remove(settingsUsersSelect.options.length - 1);
  }
  for (i = 0; i < player.usersList.length; i += 1) {
    user = player.usersList[i];
    if (user.approved) {
      newOption = document.createElement('option');
      newOption.textContent = user.name;
      newOption.value = user.id;
      settingsUsersSelect.add(newOption);
      selectedUserId = selectedUserId || user.id;
    }
    if (!user.approved && user.requested) {
      request = request || user;
    }
  }
  settingsUsersSelect.value = selectedUserId;
  updatePermsForSelectedUser();

  if (request) {
    while (requestReplaceSelect.options.length) {
      requestReplaceSelect.remove(requestReplaceSelect.options.length - 1);
    }
    for (i = 0; i < player.usersList.length; i += 1) {
      user = player.usersList[i];
      if (user.id === PlayerClient.GUEST_USER_ID) {
        user = request;
      }
      if (user.approved || user === request) {
        newOption = document.createElement('option');
        newOption.textContent = user.name;
        newOption.value = user.id;
        requestReplaceSelect.add(newOption);
      }
    }
    requestReplaceSelect.value = request.id;
    requestNameDom.value = request.name;
  }

  authPermReadDom.style.display = havePerm('read') ? "" : "none";
  authPermAddDom.style.display = havePerm('add') ? "" : "none";
  authPermControlDom.style.display = havePerm('control') ? "" : "none";
  authPermAdminDom.style.display = havePerm('admin') ? "" : "none";
  streamUrlDom.setAttribute('href', getStreamUrl());
  settingsAuthRequestDom.style.display =
    (myUser.registered && !myUser.requested && !myUser.approved) ? "" : "none";
  settingsAuthLogoutDom.style.display = myUser.registered ? "" : "none";
  $settingsAuthEdit.button('option', 'label', myUser.registered ? 'Edit' : 'Register');
  settingsUsersDom.style.display = havePerm('admin') ? "" : "none";
  settingsRequestsDom.style.display = (havePerm('admin') && !!request) ? "" : "none";

  $toggleHardwarePlayback
    .prop('disabled', !havePerm('admin'))
    .button('refresh');
  toggleHardwarePlaybackLabel.setAttribute('title', havePerm('admin') ? "" : "Requires admin privilege.");
}

function updateSettingsAdminUi() {
  $toggleHardwarePlayback
    .button('option', 'label', hardwarePlaybackOn ? 'On' : 'Off')
    .prop('checked', hardwarePlaybackOn)
    .button('refresh');
}

function sendEnsureAdminUser(ev) {
  socket.send('ensureAdminUser');
}

function onLastFmSignOutClick(ev) {
  ev.preventDefault();
  ev.stopPropagation();
  localState.lastfm.username = null;
  localState.lastfm.session_key = null;
  localState.lastfm.scrobbling_on = false;
  saveLocalState();
  updateLastFmSettingsUi();
}

function onToggleScrobbleClick(ev) {
  var msg;
  var value = toggleScrobbleDom.checked;
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
}

function onHardwarePlaybackClick(ev) {
  socket.send('hardwarePlayback', toggleHardwarePlaybackDom.checked);
  updateSettingsAdminUi();
}

function onSettingsAuthEditClick(ev) {
  authUsernameDom.value = localState.authUsername;
  authPasswordDom.value = localState.authPassword;
  hideShowAuthEdit(true);
  authUsernameDom.focus();
  authUsernameDom.select();
}

function onAuthShowPasswordChange(ev) {
  authPasswordDom.type = authShowPasswordDom.checked ? 'text' : 'password';
}

function onSettingsAuthRequestClick(ev) {
  socket.send('requestApproval');
  myUser.requested = true;
  updateSettingsAuthUi();
}

function onSettingsAuthLogoutClick(ev) {
  localState.authUsername = null;
  localState.authPassword = null;
  saveLocalState();
  socket.send('logout');
  myUser.registered = false;
  updateSettingsAuthUi();
}

function onSettingsDelUserClick(ev) {
  var selectedUserId = settingsUsersSelect.value;
  socket.send('deleteUsers', [selectedUserId]);
}

function onRequestApproveClick(ev) {
  handleApproveDeny(true);
}

function onRequestDenyClick(ev) {
  handleApproveDeny(false);
}

function setUpSettingsUi() {
  $toggleScrobble.button();
  $toggleHardwarePlayback.button();
  $(lastFmSignOutDom).button();
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

  ensureAdminDiv.addEventListener('click', sendEnsureAdminUser, false);
  lastFmSignOutDom.addEventListener('click', onLastFmSignOutClick, false);
  toggleScrobbleDom.addEventListener('click', onToggleScrobbleClick, false);
  toggleHardwarePlaybackDom.addEventListener('click', onHardwarePlaybackClick, false);
  settingsAuthEditDom.addEventListener('click', onSettingsAuthEditClick, false);
  settingsAuthSaveDom.addEventListener('click', settingsAuthSave, false);
  settingsAuthCancelDom.addEventListener('click', settingsAuthCancel, false);
  authUsernameDom.addEventListener('keydown', handleUserOrPassKeyDown, false);
  authPasswordDom.addEventListener('keydown', handleUserOrPassKeyDown, false);
  authShowPasswordDom.addEventListener('change', onAuthShowPasswordChange, false);
  settingsAuthRequestDom.addEventListener('click', onSettingsAuthRequestClick, false);
  settingsAuthLogoutDom.addEventListener('click', onSettingsAuthLogoutClick, false);

  userPermReadDom.addEventListener('change', updateSelectedUserPerms, false);
  userPermAddDom.addEventListener('change', updateSelectedUserPerms, false);
  userPermControlDom.addEventListener('change', updateSelectedUserPerms, false);
  userPermAdminDom.addEventListener('change', updateSelectedUserPerms, false);

  settingsUsersSelect.addEventListener('change', updatePermsForSelectedUser, false);
  settingsDelUserDom.addEventListener('click', onSettingsDelUserClick, false);

  requestApproveDom.addEventListener('click', onRequestApproveClick, false);
  requestDenyDom.addEventListener('click', onRequestDenyClick, false);
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
    replaceId: requestReplaceSelect.value,
    approved: approved,
    name: requestNameDom.value,
  }]);
}

function updatePermsForSelectedUser() {
  var selectedUserId = settingsUsersSelect.value;
  var user = player.usersTable[selectedUserId];
  if (!user) return;
  $userPermRead.prop('checked', user.perms.read).button('refresh');
  $userPermAdd.prop('checked', user.perms.add).button('refresh');
  $userPermControl.prop('checked', user.perms.control).button('refresh');
  $userPermAdmin.prop('checked', user.perms.admin).button('refresh');

  $settingsDeleteUser.prop('disabled', selectedUserId === PlayerClient.GUEST_USER_ID).button('refresh');
}

function updateSelectedUserPerms(ev) {
  ev.preventDefault();
  ev.stopPropagation();
  socket.send('updateUser', {
    userId: settingsUsersSelect.value,
    perms: {
      read: userPermReadDom.checked,
      add: userPermAddDom.checked,
      control: userPermControlDom.checked,
      admin: userPermAdminDom.checked,
    },
  });
}

function handleUserOrPassKeyDown(ev) {
  ev.stopPropagation();
  if (ev.which === 27) {
    settingsAuthCancel(ev);
  } else if (ev.which === 13) {
    settingsAuthSave(ev);
  }
}

function onEventsListScroll(ev) {
  eventsListScrolledToBottom =
    (eventsListDom.scrollHeight - eventsListDom.scrollTop) === eventsListDom.offsetHeight;
}

function onChatBoxInputKeyDown(ev) {
  ev.stopPropagation();
  if (ev.which === 27) {
    chatBoxInputDom.blur();
    ev.preventDefault();
    return;
  } else if (ev.which === 13) {
    var msg = chatBoxInputDom.value.trim();
    if (!msg.length) {
      ev.preventDefault();
      return;
    }
    var match = msg.match(/^\/([^\/]\w*)\s*(.*)$/);
    if (match) {
      var chatCommand = chatCommands[match[1]];
      if (chatCommand) {
        if (!chatCommand(match[2])) {
          // command failed; no message sent
          ev.preventDefault();
          return;
        }
      } else {
        // don't clear the text box; invalid command
        ev.preventDefault();
        return;
      }
    } else {
      // replace starting '//' with '/'
      socket.send('chat', { text: msg.replace(/^\/\//, '/') });
    }
    setTimeout(clearChatInputValue, 0);
    ev.preventDefault();
    return;
  }
}

function setUpEventsUi() {
  eventsListDom.addEventListener('scroll', onEventsListScroll, false);
  chatBoxInputDom.addEventListener('keydown', onChatBoxInputKeyDown, false);
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
  chatBoxInputDom.value = "";
}

function renderUnseenChatCount() {
  var eventsTabText = (player.unseenChatCount > 0) ?
    ("Chat (" + player.unseenChatCount + ")") : "Chat";
  eventsTabSpan.textContent = eventsTabText;
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
  var scrollTop = importProgressListDom.scrollTop;

  var importTabText = (player.importProgressList.length > 0) ?
    ("Import (" + player.importProgressList.length + ")") : "Import";
  importTabSpan.textContent = importTabText;

  // add the missing dom entries
  var i, ev;
  for (i = importProgressListDom.childElementCount; i < player.importProgressList.length; i += 1) {
    importProgressListDom.insertAdjacentHTML('beforeend',
      '<li class="progress">' +
        '<span class="name"></span> ' +
        '<span class="percent"></span>' +
      '</li>');
  }
  // remove extra dom entries
  while (player.importProgressList.length < importProgressListDom.childElementCount) {
    importProgressListDom.removeChild(importProgressListDom.lastChild);
  }
  // overwrite existing dom entries
  var domItems = importProgressListDom.children;
  for (i = 0; i < player.importProgressList.length; i += 1) {
    var domItem = domItems[i];
    ev = player.importProgressList[i];
    domItem.children[0].textContent = ev.filenameHintWithoutPath;
    var percent = humanSize(ev.bytesWritten, 1);
    if (ev.size) {
      percent += " / " + humanSize(ev.size, 1);
    }
    domItem.children[1].textContent = percent;
  }

  importProgressDom.style.display = (player.importProgressList.length > 0) ? "" : "none";
  importProgressListDom.scrollTop = scrollTop;
}

function renderEvents() {
  var scrollTop = eventsListDom.scrollTop;

  renderUnseenChatCount();

  // add the missing dom entries
  var i, ev;
  for (i = eventsListDom.childElementCount; i < player.eventsList.length; i += 1) {
    eventsListDom.insertAdjacentHTML('beforeend',
      '<div class="event">' +
        '<span class="name"></span>' +
        '<span class="msg"></span>' +
        '<div style="clear: both;"></div>' +
      '</div>');
  }
  // remove extra dom entries
  while (player.eventsList.length < eventsListDom.childElementCount) {
    eventsListDom.removeChild(eventsListDom.lastChild);
  }
  // overwrite existing dom entries
  var domItems = eventsListDom.children;
  for (i = 0; i < player.eventsList.length; i += 1) {
    var domItem = domItems[i];
    ev = player.eventsList[i];
    var userText = ev.user ? ev.user.name : "*";

    domItem.className = "";
    domItem.classList.add('event');
    domItem.classList.add(ev.type);
    if (ev.displayClass) domItem.classList.add('chat-me');
    domItem.children[0].textContent = userText;
    domItem.children[0].setAttribute('title', ev.date.toString());
    domItem.children[1].innerHTML = getEventMessageHtml(ev);
  }

  if (eventsListScrolledToBottom) {
    scrollEventsToBottom();
  } else {
    eventsListDom.scrollTop = scrollTop;
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


function escapeHtml(str) {
  return str.replace(/[&"<>]/g, function (m) {
    return escapeHtmlReplacements[m];
  });
}

function scrollEventsToBottom() {
  eventsListScrolledToBottom = true;
  eventsListDom.scrollTop = 1000000;
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

  var scrollTop = eventsOnlineUsersDom.scrollTop;


  // add the missing dom entries
  var heightChanged = eventsOnlineUsersDom.childElementCount !== sortedConnectedUsers.length;
  for (i = eventsOnlineUsersDom.childElementCount; i < sortedConnectedUsers.length; i += 1) {
    eventsOnlineUsersDom.insertAdjacentHTML('beforeend',
      '<div class="user">' +
        '<span class="streaming ui-icon ui-icon-signal-diag"></span>' +
        '<span class="name"></span>' +
      '</div>');
  }
  // remove extra dom entries
  while (sortedConnectedUsers.length < eventsOnlineUsersDom.childElementCount) {
    eventsOnlineUsersDom.removeChild(eventsOnlineUsersDom.lastChild);
  }
  // overwrite existing dom entries
  var domItems = eventsOnlineUsersDom.children;
  for (i = 0; i < sortedConnectedUsers.length; i += 1) {
    var domItem = domItems[i];
    user = sortedConnectedUsers[i];
    domItem.children[0].style.display = user.streaming ? "" : "none";
    domItem.children[1].textContent = user.name;
  }

  eventsOnlineUsersDom.scrollTop = scrollTop;

  if (heightChanged) {
    triggerResize();
  }
}

function ensureSearchHappensSoon() {
  if (searchTimer != null) {
    clearTimeout(searchTimer);
  }
  // give the user a small timeout between key presses to finish typing.
  // otherwise, we might be bogged down displaying the search results for "a" or the like.
  searchTimer = setTimeout(function() {
    player.search(libFilterDom.value);
    searchTimer = null;
  }, 100);
}

function onLibFilterKeyDown(ev) {
  ev.stopPropagation();
  switch (ev.which) {
  case 27: // Escape
    if ($(ev.target).val().length === 0) {
      $(ev.target).blur();
    } else {
      setTimeout(function(){
        libFilterDom.value = "";
        // queue up a search refresh now, because if the user holds Escape,
        // it will blur the search box, and we won't get a keyup for Escape.
        ensureSearchHappensSoon();
      }, 0);
    }
    ev.preventDefault();
    return;
  case 13: // Enter
    var keys = [];
    for (var i = 0; i < player.searchResults.artistList.length; i += 1) {
      var artist = player.searchResults.artistList[i];
      for (var j = 0; j < artist.albumList.length; j += 1) {
        var album = artist.albumList[j];
        for (var k = 0; k < album.trackList.length; k += 1) {
          var track = album.trackList[k];
          keys.push(track.key);
        }
      }
    }
    if (ev.altKey) shuffle(keys);
    if (keys.length > 2000) {
      if (!confirm("You are about to queue " + keys.length + " songs.")) {
        ev.preventDefault();
        return;
      }
    }
    if (ev.shiftKey) {
      player.queueTracksNext(keys);
    } else {
      player.queueOnQueue(keys);
    }
    ev.preventDefault();
    return;
  case 40:
    selection.selectOnlyFirstPos('library');
    selection.scrollToCursor();
    refreshSelection();
    libFilterDom.blur();
    ev.preventDefault();
    return;
  case 38:
    selection.selectOnlyLastPos('library');
    selection.scrollToCursor();
    refreshSelection();
    libFilterDom.blur();
    ev.preventDefault();
    return;
  }
}

function setUpLibraryUi() {
  libFilterDom.addEventListener('keydown', onLibFilterKeyDown, false);
  libFilterDom.addEventListener('keyup', ensureSearchHappensSoon, false);
  libFilterDom.addEventListener('cut', ensureSearchHappensSoon, false);
  libFilterDom.addEventListener('paste', ensureSearchHappensSoon, false);
  genericTreeUi($(libraryDom), {
    toggleExpansion: toggleLibraryExpansion,
    isSelectionOwner: function(){
      return selection.isLibrary();
    }
  });
  $libraryMenu.menu();
  $libraryMenu.on('mousedown', preventEventDefault);
  $libraryMenu.on('click', '.queue', function(ev) {
    ev.stopPropagation();
    ev.preventDefault();
    player.queueOnQueue(selection.toTrackKeys());
    removeContextMenu();
  });
  $libraryMenu.on('click', '.queue-next', function(ev) {
    ev.stopPropagation();
    ev.preventDefault();
    player.queueTracksNext(selection.toTrackKeys());
    removeContextMenu();
  });
  $libraryMenu.on('click', '.queue-random', function(ev) {
    ev.stopPropagation();
    ev.preventDefault();
    player.queueOnQueue(selection.toTrackKeys(true));
    removeContextMenu();
  });
  $libraryMenu.on('click', '.queue-next-random', function(ev) {
    ev.stopPropagation();
    ev.preventDefault();
    player.queueTracksNext(selection.toTrackKeys(true));
    removeContextMenu();
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

function onDeletePlaylistContextMenu(ev) {
  ev.stopPropagation();
  ev.preventDefault();
  maybeDeleteSelectedPlaylists();
  removeContextMenu();
}

function onRemoveFromPlaylistContextMenu(ev) {
  ev.stopPropagation();
  ev.preventDefault();
  handleDeletePressed(false);
  removeContextMenu();
}

function genericTreeUi($elem, options) {
  $elem.on('mousedown', 'div.expandable > div.ui-icon', function(ev) {
    ev.stopPropagation();
    ev.preventDefault();
    options.toggleExpansion(this.parentNode.parentNode);
  });
  $elem.on('dblclick', 'div.expandable > div.ui-icon', preventEventDefault);
  $elem.on('dblclick', 'div.clickable', queueSelection);
  $elem.on('contextmenu', function(ev) {
    return ev.altKey;
  });
  $elem.on('mousedown', '.clickable', function(ev){
    document.activeElement.blur();
    var type = this.getAttribute('data-type');
    var key = this.getAttribute('data-key');
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
      if (!options.isSelectionOwner() || selection.ids[type][key] == null) {
        selection.selectOnly(type, key);
        refreshSelection();
      }
      popContextMenu(type, ev.pageX, ev.pageY);
    }
  });
  $elem.on('mousedown', preventEventDefault);
}

function encodeDownloadHref(file) {
  // be sure to escape #hashtags
  return 'library/' + encodeURI(file).replace(/#/g, "%23");
}

function makeDownloadHref() {
  var keys = selection.toTrackKeys();
  if (keys.length === 1) {
    return encodeDownloadHref(player.library.trackTable[keys[0]].file);
  } else {
    return "/download/keys?" + keys.join("&");
  }
}

function updateMenuDisableState($menu) {
  var menuPermDoms = {
    admin: $menu.find('.delete,.edit-tags'),
    control: $menu.find('.remove,.delete-playlist,.add-to-playlist,.shuffle'),
    add: $menu.find('.queue,.queue-next,.queue-random,.queue-next-random'),
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
  streamBtnDom.addEventListener('click', toggleStreamStatus, false);
  $clientVolSlider.slider({
    step: 0.01,
    min: 0,
    max: 1,
    value: localState.clientVolume || 1,
    change: setVol,
    slide: setVol,
  });
  clientVolDom.style.display = 'none';
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
  // $ need toHtmlId because jQuery throws a fit with "(incoming)"
  return "pl-pl-" + toHtmlId(s);
}

function resizeDomElements() {
  var eventsScrollTop = eventsListDom.scrollTop;

  nowPlayingDom.style.width = (window.innerWidth - MARGIN * 2) + "px";
  var secondLayerTop = nowPlayingDom.getBoundingClientRect().top + nowPlayingDom.clientHeight + MARGIN;
  leftWindowDom.style.left = MARGIN + "px";
  leftWindowDom.style.top = secondLayerTop + "px";
  var queueWindowLeft = MARGIN + leftWindowDom.clientWidth + MARGIN;
  queueWindowDom.style.left = queueWindowLeft + "px";
  queueWindowDom.style.top = secondLayerTop + "px";
  queueWindowDom.style.width = (window.innerWidth - queueWindowLeft - MARGIN) + "px";
  leftWindowDom.style.height = (window.innerHeight - secondLayerTop) + "px";
  queueWindowDom.style.height = (leftWindowDom.clientHeight - MARGIN) + "px";
  var tabContentsHeight = leftWindowDom.clientHeight - tabsDom.clientHeight - MARGIN;
  libraryDom.style.height = (tabContentsHeight - libHeaderDom.clientHeight) + "px";
  uploadDom.style.height = tabContentsHeight + "px";
  eventsListDom.style.height = (tabContentsHeight - eventsOnlineUsersDom.clientHeight - chatBoxDom.clientHeight) + "px";
  playlistsDom.style.height = (tabContentsHeight - newPlaylistNameDom.offsetHeight) + "px";

  setAllTabsHeight(tabContentsHeight);
  queueItemsDom.style.height = (queueWindowDom.clientHeight - queueHeaderDom.offsetTop - queueHeaderDom.clientHeight) + "px";

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
    tab.pane.style.height = h + "px";
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
  clientVolDom.style.display = tryingToStream ? "" : "none";
}

function toggleStreamStatus(ev) {
  ev.stopPropagation();
  ev.preventDefault();
  tryingToStream = !tryingToStream;
  sendStreamingStatus();
  renderStreamButton();
  updateStreamPlayer();
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

function init() {
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
    authUsernameDisplayDom.textContent = myUser.name;
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
  window.addEventListener('resize', triggerResize, false);
  window._debug_player = player;
  window._debug_selection = selection;
}

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

function preventEventDefault(ev) {
  ev.preventDefault();
  ev.stopPropagation();
}

function extend(dest, src) {
  for (var name in src) {
    dest[name] = src[name];
  }
  return dest;
}

function noop() {}
