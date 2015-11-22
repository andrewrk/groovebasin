var shuffle = require('mess');
var humanSize = require('human-size');
var PlayerClient = require('./playerclient');
var Socket = require('./socket');
var uuid = require('./uuid');

var autoDjOn = false;
var hardwarePlaybackOn = false;
var haveAdminUser = true;
var streamEndpoint = null;

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
  selectOne: function(selName, key, selectOnly) {
    if (selectOnly) {
      this.clear();
      this.cursorType = selName;
      this.cursor = key;
      this.rangeSelectAnchor = key;
      this.rangeSelectAnchorType = selName;
    }
    this.ids[selName][key] = true;
  },
  selectOnly: function(selName, key) {
    return selection.selectOne(selName, key, true);
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
  isAtLeastNumSelected: function(num) {
    var result, k;
    if (this.isLibrary()) {
      result = num;
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
      result = num;
      for (k in this.ids.queue) {
        if (!--result) return true;
      }
      return false;
    } else if (this.isPlaylist()) {
      result = num;
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
  isMulti: function() {
    return this.isAtLeastNumSelected(2);
  },
  isEmpty: function() {
    return !this.isAtLeastNumSelected(1);
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
    } else if (this.isQueue()) {
      val = {
        type: 'queue',
        queue: key ? player.queue.itemTable[key] : player.queue.itemList[0],
      };
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
    } else if (pos.type === 'queue') {
      return pos.queue != null;
    } else {
      throw new Error("NothingSelected");
    }
  },
  selectOnlyPos: function(pos) {
    return this.selectPos(pos, true);
  },
  selectPos: function(pos, selectOnly) {
    if (pos.type === 'library') {
      if (pos.track) {
        return selection.selectOne('track', pos.track.key, selectOnly);
      } else if (pos.album) {
        return selection.selectOne('album', pos.album.key, selectOnly);
      } else if (pos.artist) {
        return selection.selectOne('artist', pos.artist.key, selectOnly);
      }
    } else if (pos.type === 'playlist') {
      if (pos.playlistItem) {
        return selection.selectOne('playlistItem', pos.playlistItem.id, selectOnly);
      } else if (pos.playlist) {
        return selection.selectOne('playlist', pos.playlist.id, selectOnly);
      }
    } else if (pos.type === 'queue') {
      return selection.selectOne('queue', pos.queue.id, selectOnly);
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
      if (pos.track) {
        pos.track = pos.track.album.trackList[pos.track.index + 1];
        if (!pos.track) {
          pos.album = pos.artist.albumList[pos.album.index + 1];
          if (!pos.album) {
            pos.artist = player.searchResults.artistList[pos.artist.index + 1];
          }
        }
      } else if (pos.album) {
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
      } else if (pos.artist) {
        if (isArtistExpanded(pos.artist)) {
          pos.album = pos.artist.albumList[0];
        } else {
          pos.artist = player.searchResults.artistList[pos.artist.index + 1];
        }
      }
    } else if (pos.type === 'playlist') {
      if (pos.playlistItem) {
        pos.playlistItem = pos.playlistItem.playlist.itemList[pos.playlistItem.index + 1];
        if (!pos.playlistItem) {
          pos.playlist = player.playlistList[pos.playlist.index + 1];
        }
      } else if (pos.playlist) {
        if (isPlaylistExpanded(pos.playlist)) {
          pos.playlistItem = pos.playlist.itemList[0];
          if (!pos.playlistItem) {
            pos.playlist = player.playlistList[pos.playlist.index + 1];
          }
        } else {
          pos.playlist = player.playlistList[pos.playlist.index + 1];
        }
      }
    } else if (pos.type === 'queue') {
      if (pos.queue) {
        pos.queue = player.queue.itemList[pos.queue.index + 1];
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
    } else if (pos.type === 'queue') {
      if (pos.queue) {
        pos.queue = player.queue.itemList[pos.queue.index - 1];
      }
    } else {
      throw new Error("NothingSelected");
    }
  },
  containsPos: function(pos) {
    if (!this.posInBounds(pos)) return false;
    if (pos.type === 'library') {
      if (pos.track) {
        return this.ids.track[pos.track.key];
      } else if (pos.album) {
        return this.ids.album[pos.album.key];
      } else if (pos.artist) {
        return this.ids.artist[pos.artist.key];
      }
    } else if (pos.type === 'playlist') {
      if (pos.playlistItem) {
        return this.ids.playlistItem[pos.playlistItem.id];
      } else if (pos.playlist) {
        return this.ids.playlist[pos.playlist.id];
      }
    } else if (pos.type === 'queue') {
      return this.ids.queue[pos.queue.id];
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
      return getKeysInOrder(trackSet);
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
      var playlistItemSet = {};
      function renderPlaylist(playlist){
        for (var i = 0; i < playlist.itemList.length; i += 1) {
          var item = playlist.itemList[i];
          renderPlaylistItem(item);
        }
      }
      function renderPlaylistItem(item){
        playlistItemSet[item.id] = selection.posToArr(getItemSelPos(item));
      }
      function getItemSelPos(item){
        return {
          type: 'playlist',
          playlist: item.playlist,
          playlistItem: item
        };
      }
      for (var key in selection.ids.playlist) {
        renderPlaylist(player.playlistTable[key]);
      }
      for (key in selection.ids.playlistItem) {
        renderPlaylistItem(player.playlistItemTable[key]);
      }
      var playlistItemKeys = getKeysInOrder(playlistItemSet);
      return playlistItemKeys.map(function(playlistItemKey) { return player.playlistItemTable[playlistItemKey].track.key; });
    }

    function getKeysInOrder(trackSet){
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
      scrollThingToCursor(playlistsDom, helpers);
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
var ICON_COLLAPSED = 'icon-triangle-1-e';
var ICON_EXPANDED = 'icon-triangle-1-se';
var myUser = {
  perms: {},
};
var socket = null;
var player = null;
var userIsSeeking = false;
var userIsVolumeSliding = false;
var startedDrag = false;
var abortDrag = noop;
var closeOpenDialog = noop;
var lastFmApiKey = null;
var LoadStatus = {
  Init: 'Loading...',
  NoServer: 'Server is down.',
  GoodToGo: '[good to go]'
};
var repeatModeNames = ["Off", "All", "One"];
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
var streamBtnLabel = document.getElementById('stream-btn-label');
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
var authPermPlaylistDom = document.getElementById('auth-perm-playlist');
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
var newPlaylistNameDom = document.getElementById('new-playlist-name');
var emptyLibraryMessageDom = document.getElementById('empty-library-message');
var libraryNoItemsDom = document.getElementById('library-no-items');
var libraryArtistsDom = document.getElementById('library-artists');
var volNumDom = document.getElementById('vol-num');
var volWarningDom = document.getElementById('vol-warning');
var ensureAdminDiv = document.getElementById('ensure-admin');
var ensureAdminBtn = document.getElementById('ensure-admin-btn');
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
var userPermPlaylistDom = document.getElementById('user-perm-playlist');
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
var prevDom = document.getElementById('edit-tags-prev');
var nextDom = document.getElementById('edit-tags-next');
var editTagsFocusDom = document.getElementById('edit-tag-name');
var trackSliderDom = document.getElementById('track-slider');
var clientVolSlider = document.getElementById('client-vol-slider');
var volSlider = document.getElementById('vol-slider');
var modalDom = document.getElementById('modal');
var modalContentDom = document.getElementById('modal-content');
var modalTitleDom = document.getElementById('modal-title');
var modalHeaderDom = document.getElementById('modal-header');
var blackoutDom = document.getElementById('blackout');
var contextMenuDom = document.getElementById('context-menu');
var addToPlaylistMenu = document.getElementById('add-to-playlist-menu');
var menuQueue = document.getElementById('menu-queue');
var menuQueueNext = document.getElementById('menu-queue-next');
var menuQueueRandom = document.getElementById('menu-queue-random');
var menuQueueNextRandom = document.getElementById('menu-queue-next-random');
var menuRemove = document.getElementById('menu-remove');
var menuAddToPlaylist = document.getElementById('menu-add-to-playlist');
var menuAddRemoveLabel = document.getElementById('menu-add-remove-label');
var menuShuffle = document.getElementById('menu-shuffle');
var menuDelete = document.getElementById('menu-delete');
var menuDeletePlaylist = document.getElementById('menu-delete-playlist');
var menuRenamePlaylist = document.getElementById('menu-rename-playlist');
var menuDownload = document.getElementById('menu-download');
var menuEditTags = document.getElementById('menu-edit-tags');
var addToPlaylistDialog = document.getElementById('add-to-playlist-dialog');
var addToPlaylistFilter = document.getElementById('add-to-playlist-filter');
var addToPlaylistList = document.getElementById('add-to-playlist-list');
var addToPlaylistNew = document.getElementById('add-to-playlist-new');
var addRemoveLabelDialog = document.getElementById('add-remove-label-dialog');
var addRemoveLabelFilter = document.getElementById('add-remove-label-filter');
var addRemoveLabelList = document.getElementById('add-remove-label-list');
var addRemoveLabelNew = document.getElementById('add-remove-label-new');

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
var triggerLabelsUpdate = makeRenderCall(updateLabelsUi, 100);
var triggerResize = makeRenderCall(resizeDomElements, 20);
var keyboardHandlers = (function() {
  var volumeDownHandler = {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function() {
        bumpVolume(-0.1);
      }
  };
  var volumeUpHandler = {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function() {
        bumpVolume(0.1);
      }
  };

  return {
    // Enter
    13: {
      ctrl: false,
      alt: null,
      shift: null,
      handler: function(ev) {
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
    // a
    65: {
      ctrl: null,
      alt: false,
      shift: false,
      handler: function(ev) {
        if (ev.ctrlKey) {
          selection.selectAll();
          refreshSelection();
        } else {
          onAddToPlaylistContextMenu(ev);
        }
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
    // l
    76: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: function(ev) {
        onAddRemoveLabelContextMenu(ev);
      },
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
    // r, R
    82: {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(ev) {
        if (ev.shiftKey) {
          maybeRenamePlaylistAtCursor();
        } else {
          nextRepeatState();
        }
      },
    },
    // s
    83: {
      ctrl: false,
      alt: false,
      shift: false,
      handler: toggleStreamStatusEvent
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
      handler: function() {
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
      shift: true,
      handler: function() {
        player.prev();
      },
    },
    // _ maybe?
    189: volumeDownHandler,
    // . >
    190: {
      ctrl: false,
      alt: false,
      shift: true,
      handler: function() {
        player.next();
      },
    },
    // ?
    191: {
      ctrl: false,
      alt: false,
      shift: null,
      handler: function(ev) {
        if (ev.shiftKey) {
          showKeyboardShortcuts(ev);
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
  labelCreate: function(ev) {
    return "created " + eventLabelName(ev);
  },
  labelRename: function(ev) {
    return "renamed " + eventLabelName(ev, ev.text) + " to " + eventLabelName(ev);
  },
  labelColorUpdate: function(ev) {
    if (ev.label) {
      return "changed color of " + eventLabelName(ev) + " from " + ev.text + " to " + ev.label.color;
    } else {
      return "changed color of (deleted label)";
    }
  },
  labelDelete: function(ev) {
    return "deleted " + eventLabelName(ev, ev.text);
  },
  labelAdd: function(ev) {
    if (ev.pos === 1) {
      if (ev.subCount === 1) {
        return "added " + eventLabelName(ev) + " to " + getEventNowPlayingText(ev);
      } else {
        return "added labels to " + getEventNowPlayingText(ev);
      }
    } else {
      return "added labels to " + ev.pos + " tracks";
    }
  },
  labelRemove: function(ev) {
    if (ev.pos === 1) {
      if (ev.subCount === 1) {
        return "removed " + eventLabelName(ev) + " from " + getEventNowPlayingText(ev);
      } else {
        return "removed labels from " + getEventNowPlayingText(ev);
      }
    } else {
      return "removed labels from " + ev.pos + " tracks";
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

var menuPermSelectors = {
  admin: [menuDelete, menuEditTags],
  control: [menuRemove, menuShuffle, menuQueue, menuQueueNext, menuQueueRandom, menuQueueNextRandom],
  playlist: [menuDeletePlaylist, menuAddToPlaylist, menuAddRemoveLabel],
};

var addToPlaylistDialogFilteredList = [];
var addRemoveLabelDialogFilteredList = [];

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
    var middle = item.getBoundingClientRect().top + item.clientHeight / 2;
    var track = player.queue.itemTable[item.getAttribute('data-id')];
    if (middle < y) {
      if (result.previousKey == null || track.sortKey > result.previousKey) {
        result.previous = item;
        result.previousKey = track.sortKey;
      }
    } else {
      if (result.nextKey == null || track.sortKey < result.nextKey) {
        result.next = item;
        result.nextKey = track.sortKey;
      }
    }
  }
  return result;
}

function renderAutoDj() {
  if (autoDjOn) {
    autoDjDom.classList.add('on');
  } else {
    autoDjDom.classList.remove('on');
  }
}

function renderQueueButtons() {
  renderAutoDj();
  var repeatModeName = repeatModeNames[player.repeat];

  queueBtnRepeatDom.value = "Repeat: " + repeatModeName;
  if (player.repeat === PlayerClient.REPEAT_OFF) {
    queueBtnRepeatDom.classList.remove("on");
  } else {
    queueBtnRepeatDom.classList.add("on");
  }
}

function updateHaveAdminUserUi() {
  ensureAdminDiv.style.display = haveAdminUser ? "none" : "";
}

function renderQueue() {
  var itemList = player.queue.itemList || [];
  var scrollTop = queueItemsDom.scrollTop;

  // add the missing dom entries
  var i;
  for (i = queueItemsDom.childElementCount; i < itemList.length; i += 1) {
    queueItemsDom.insertAdjacentHTML('beforeend',
      '<div class="pl-item">' +
        '<span class="track"></span>' +
        '<span class="time"></span>' +
        '<span class="middle">' +
          '<span class="title"></span>' +
          '<span class="artist"></span>' +
          '<span class="album"></span>' +
        '</span>' +
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

    var timeText = player.isScanning(track) ? "scan" : formatTime(track.duration);
    domItem.children[1].textContent = timeText;

    var middleDom = domItem.children[2];
    middleDom.children[0].textContent = track.name || "";
    middleDom.children[1].textContent = track.artistName || "";
    middleDom.children[2].textContent = track.albumName || "";

    var trackLabels = getTrackLabels(track);
    for (var label_i = 0; label_i < trackLabels.length; label_i += 1) {
      var label = trackLabels[label_i];
      var labelBoxDom = document.createElement('span');
      labelBoxDom.classList.add("label-box");
      labelBoxDom.style.backgroundColor = label.color;
      labelBoxDom.setAttribute('title', label.name);
      middleDom.children[0].appendChild(labelBoxDom);
    }
  }

  refreshSelection();
  labelQueueItems();
  queueItemsDom.scrollTop = scrollTop;
}

function getTrackLabels(track) {
  var labelList = Object.keys(track.labels).map(getLabelById);
  labelList.sort(compareNameAndId);
  return labelList;
}

function compareNameAndId(a, b) {
  var result = operatorCompare(a.name, b.name);
  if (result) return result;
  return operatorCompare(a.id, b.id);
}

function operatorCompare(a, b){
  if (a === b) {
    return 0;
  } else if (a > b) {
    return -1;
  } else {
    return 1;
  }
}

function getLabelById(labelId) {
  return player.library.labelTable[labelId];
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
  var item, domItem;
  var curItem = player.currentItem;
  Array.prototype.forEach.call(queueItemsDom.getElementsByClassName('pl-item'), removeCurrentOldAndRandomClasses);
  if (curItem != null && autoDjOn) {
    for (var index = 0; index < curItem.index; ++index) {
      item = player.queue.itemList[index];
      var itemId = item && item.id;
      if (itemId) {
        domItem = document.getElementById(toQueueItemId(itemId));
        if (domItem) {
          domItem.classList.add('old');
        }
      }
    }
  }
  for (var i = 0; i < player.queue.itemList.length; i += 1) {
    item = player.queue.itemList[i];
    if (item.isRandom) {
      domItem = document.getElementById(toQueueItemId(item.id));
      if (domItem) {
        domItem.classList.add('random');
      }
    }
  }
  if (curItem) {
    domItem = document.getElementById(toQueueItemId(curItem.id));
    if (domItem) {
      domItem.classList.add('current');
    }
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
      var selectedDomItem = helper.getDiv(id);
      if (selectedDomItem) {
        selectedDomItem.classList.add('selected');
      }
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
        var cursorDomItem = helper.getDiv(selection.cursor);
        if (cursorDomItem) {
          cursorDomItem.classList.add('cursor');
        }
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
  renderPlaylists();
  updateAddToPlaylistDialogDisplay();
}

function updateLabelsUi() {
  updateAddRemoveLabelDialogDisplay();
}

function popAddToPlaylistDialog() {
  popDialog(addToPlaylistDialog, "Add to Playlist", 400, Math.min(500, window.innerHeight - 40));
  addToPlaylistFilter.focus();
  addToPlaylistFilter.select();
}

function popAddRemoveLabelDialog() {
  popDialog(addRemoveLabelDialog, "Add/Remove Labels", 400, Math.min(500, window.innerHeight - 40));
  addRemoveLabelFilter.focus();
  addRemoveLabelFilter.select();
}

function updateAddToPlaylistDialogDisplay() {
  var loweredFilter = addToPlaylistFilter.value.toLowerCase();
  addToPlaylistDialogFilteredList = [];
  var exactMatch = false;
  player.playlistList.forEach(function(playlist) {
    if (playlist.name.toLowerCase().indexOf(loweredFilter) >= 0) {
      addToPlaylistDialogFilteredList.push(playlist);
      if (addToPlaylistFilter.value === playlist.name) {
        exactMatch = true;
      }
    }
  });

  addToPlaylistNew.textContent = "\"" + addToPlaylistFilter.value + "\" (create new)";
  addToPlaylistNew.style.display = (exactMatch || loweredFilter === "") ? "none" : "";


  // add the missing dom entries
  var i;
  for (i = addToPlaylistList.childElementCount; i < addToPlaylistDialogFilteredList.length; i += 1) {
    addToPlaylistList.appendChild(document.createElement('li'));
  }
  // remove the extra dom entries
  while (addToPlaylistDialogFilteredList.length < addToPlaylistList.childElementCount) {
    addToPlaylistList.removeChild(addToPlaylistList.lastChild);
  }

  // overwrite existing dom entries
  for (i = 0; i < addToPlaylistDialogFilteredList.length; i += 1) {
    var domItem = addToPlaylistList.children[i];
    var playlist = addToPlaylistDialogFilteredList[i];
    domItem.setAttribute('data-key', playlist.id);
    domItem.textContent = playlist.name;
  }
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
          '<div class="icon"></div>' +
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
  expandPlaylistsToSelection();
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
          '<div class="icon"></div>' +
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

  trackSliderDom.disabled = disabled;
  trackSliderDom.value = sliderPos;
  updateSliderUi();

  nowPlayingElapsedDom.textContent = formatTime(elapsed);
  nowPlayingLeftDom.textContent = formatTime(duration);
}

function renderVolumeSlider() {
  if (userIsVolumeSliding) return;

  volSlider.value = player.volume;
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
  var oldClass = (player.isPlaying === true) ? 'icon-play' : 'icon-pause';
  var newClass = (player.isPlaying === true) ? 'icon-pause': 'icon-play';
  nowPlayingToggleIconDom.classList.remove(oldClass);
  nowPlayingToggleIconDom.classList.add(newClass);
  trackSliderDom.disabled = (player.isPlaying == null);
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
  renderQueueButtons();
  renderLibrary();
  renderNowPlaying();
  updateSettingsAuthUi();
  updateLastFmSettingsUi();
  resizeDomElements();
}

function renderArtist(ul, albumList) {
  albumList.forEach(function(album) {
    ul.insertAdjacentHTML('beforeend',
      '<li>' +
        '<div class="clickable expandable" data-type="album">' +
          '<div class="icon icon-triangle-1-e"></div>' +
          '<span></span>' +
        '</div>' +
        '<ul style="display: none;"></ul>' +
      '</li>');
    var liDom = ul.lastChild;
    var divDom = liDom.children[0];
    divDom.setAttribute('id', toAlbumId(album.key));
    divDom.setAttribute('data-key', album.key);
    var spanDom = divDom.children[1];
    spanDom.textContent = album.name || '[Unknown Album]';

    var artistUlDom = liDom.children[1];
    album.trackList.forEach(function(track) {
      artistUlDom.insertAdjacentHTML('beforeend',
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
  playlist.itemList.forEach(function(item) {
    ul.insertAdjacentHTML('beforeend',
      '<li>' +
        '<div class="clickable" data-type="playlistItem">' +
          '<span></span>' +
        '</div>' +
      '</li>');
    var liDom = ul.lastChild;
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
  // only works if the domItem is not position absolute or fixed
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
  var nextPos = selection.getPos();
  while (selection.containsPos(nextPos)) {
    selection.incrementPos(nextPos);
  }
  selection.clear();
  if (selection.posInBounds(nextPos)) {
    selection.selectOnlyPos(nextPos);
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
      assumeCurrentSelectionIsDeleted();
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

function nobodyListening() {
  return getStreamerCount() === 0 && !hardwarePlaybackOn;
}

function togglePlayback(){
  if (player.isPlaying === true) {
    player.pause();
  } else if (player.isPlaying === false) {
    if (nobodyListening()) {
      toggleStreamStatus();
    }
    player.play();
  }
  // else we haven't received state from server yet
}

function toggleAutoDj(){
  autoDjOn = !autoDjOn;
  player.sendCommand('autoDjOn', autoDjOn);
  renderAutoDj();
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
  if (contextMenuDom.style.display !== 'none') {
    contextMenuDom.style.display = "none";
    return true;
  }
  return false;
}

function isPlaylistExpanded(playlist){
  var li = document.getElementById(toPlaylistId(playlist.id)).parentNode;
  if (!li.getAttribute('data-cached')) return false;
  return isDomItemVisible(li.lastChild);
}

function isArtistExpanded(artist){
  var li = document.getElementById(toArtistId(artist.key)).parentNode;
  if (!li.getAttribute('data-cached')) return false;
  return isDomItemVisible(li.lastChild);
}

function isAlbumExpanded(album){
  var albumElem = document.getElementById(toAlbumId(album.key));
  var li = albumElem.parentNode;
  return isDomItemVisible(li.lastChild);
}

function expandArtist(artist) {
  if (isArtistExpanded(artist)) return;

  var artistElem = document.getElementById(toArtistId(artist.key));
  var li = artistElem.parentNode;
  toggleLibraryExpansion(li);
}

function expandAlbum(album) {
  if (isAlbumExpanded(album)) return;

  expandArtist(album.artist);
  var elem = document.getElementById(toAlbumId(album.key));
  var li = elem.parentNode;
  toggleLibraryExpansion(li);
}

function expandPlaylist(playlist) {
  if (isPlaylistExpanded(playlist)) return;

  var playlistElem = document.getElementById(toPlaylistId(playlist.id));
  var li = playlistElem.parentNode;
  togglePlaylistExpansion(li);
}

function expandPlaylistsToSelection() {
  if (!selection.isPlaylist()) return;

  for (var itemId in selection.ids.playlistItem) {
    var playlist = player.playlistItemTable[itemId].playlist;
    expandPlaylist(playlist);
  }

  selection.scrollTo();
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
    if (result.next) {
      result.next.classList.add('border-top');
    } else if (result.previous) {
      result.previous.classList.add('border-bottom');
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

function isDialogOpen() {
  return closeOpenDialog !== noop;
}

function clearSelectionAndHideMenu() {
  if (isDialogOpen()) return;
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
    closeOpenDialog();
  }
}

function callCloseOpenDialog() {
  closeOpenDialog();
}

function onBlackoutKeyDown(ev) {
  if (ev.which === 27) {
    closeOpenDialog();
  }
}

function setUpGenericUi() {
  window.addEventListener('focus', onWindowFocus, false);
  window.addEventListener('blur', onWindowBlur, false);
  window.addEventListener('resize', triggerResize, false);
  window.addEventListener('mousedown', clearSelectionAndHideMenu, false);
  window.addEventListener('keydown', onWindowKeyDown, false);
  streamAudio.addEventListener('playing', onStreamPlaying, false);
  shortcutsDom.addEventListener('keydown', onShortcutsWindowKeyDown, false);
  document.getElementById('modal-close').addEventListener('click', callCloseOpenDialog, false);
  blackoutDom.addEventListener('keydown', onBlackoutKeyDown, false);
  blackoutDom.addEventListener('mousedown', callCloseOpenDialog, false);

  modalDom.addEventListener('keydown', onModalKeyDown, false);

  addToPlaylistFilter.addEventListener('keydown', onAddToPlaylistFilterKeyDown, false);
  addToPlaylistFilter.addEventListener('keyup', updateAddToPlaylistDialogDisplay, false);
  addToPlaylistFilter.addEventListener('cut', updateAddToPlaylistDialogDisplay, false);
  addToPlaylistFilter.addEventListener('paste', updateAddToPlaylistDialogDisplay, false);
  addToPlaylistNew.addEventListener('mousedown', onAddToPlaylistNewClick, false);
  addToPlaylistList.addEventListener('mousedown', onAddToPlaylistListClick, false);

  addRemoveLabelFilter.addEventListener('keydown', onAddRemoveLabelFilterKeyDown, false);
  addRemoveLabelFilter.addEventListener('keyup', updateAddRemoveLabelDialogDisplay, false);
  addRemoveLabelFilter.addEventListener('cut', updateAddRemoveLabelDialogDisplay, false);
  addRemoveLabelFilter.addEventListener('paste', updateAddRemoveLabelDialogDisplay, false);
  addRemoveLabelNew.addEventListener('mousedown', onAddRemoveLabelNewClick, false);
  addRemoveLabelList.addEventListener('mousedown', onAddRemoveLabelListClick, false);
  addRemoveLabelList.addEventListener('change', onAddRemoveLabelListChange, false);
}

function onModalKeyDown(ev) {
  ev.stopPropagation();
  switch (ev.which) {
  case 27: // Escape
    ev.preventDefault();
    closeOpenDialog();
    return;
  }
}

function onAddToPlaylistListClick(ev) {
  if (ev.button !== 0) return;
  ev.stopPropagation();
  ev.preventDefault();
  var clickedLi = getFirstChildToward(addToPlaylistList, ev.target);
  if (!clickedLi) return;
  if (!havePerm('playlist')) return;
  if (!ev.shiftKey) closeOpenDialog();
  var playlistId = clickedLi.getAttribute('data-key');
  player.queueOnPlaylist(playlistId, selection.toTrackKeys());
}

function onAddToPlaylistNewClick(ev) {
  ev.stopPropagation();
  ev.preventDefault();
  if (!havePerm('playlist')) return;
  if (!ev.shiftKey) closeOpenDialog();
  var playlist = player.createPlaylist(addToPlaylistFilter.value);
  player.queueOnPlaylist(playlist.id, selection.toTrackKeys());
}

function onAddToPlaylistFilterKeyDown(ev) {
  ev.stopPropagation();
  switch (ev.which) {
  case 27: // Escape
    ev.preventDefault();
    if (addToPlaylistFilter.value === "") {
      closeOpenDialog();
    } else {
      addToPlaylistFilter.value = "";
    }
    return;
  case 13: // Enter
    ev.preventDefault();
    if (addToPlaylistDialogFilteredList.length === 0) {
      onAddToPlaylistNewClick(ev);
    } else {
      var playlistId = addToPlaylistDialogFilteredList[0].id;
      player.queueOnPlaylist(playlistId, selection.toTrackKeys());
      if (!ev.shiftKey) {
        closeOpenDialog();
      }
    }
    return;
  }
}

function onAddRemoveLabelFilterKeyDown(ev) {
  ev.stopPropagation();
  switch (ev.which) {
  case 27: // Escape
    ev.preventDefault();
    if (addRemoveLabelFilter.value === "") {
      closeOpenDialog();
    } else {
      addRemoveLabelFilter.value = "";
    }
    return;
  case 13: // Enter
    ev.preventDefault();
    if (addRemoveLabelDialogFilteredList.length === 0) {
      onAddRemoveLabelNewClick(ev);
    } else {
      var labelId = addRemoveLabelDialogFilteredList[0].id;
      toggleLabelOnSelection(labelId);
      if (!ev.shiftKey) {
        closeOpenDialog();
      }
    }
    return;
  }
}

function updateAddRemoveLabelDialogDisplay(ev) {
  var loweredFilter = addRemoveLabelFilter.value.toLowerCase();
  addRemoveLabelDialogFilteredList = [];
  var exactMatch = false;
  player.library.labelList.forEach(function(label) {
    if (label.name.toLowerCase().indexOf(loweredFilter) >= 0) {
      addRemoveLabelDialogFilteredList.push(label);
      if (addRemoveLabelFilter.value === label.name) {
        exactMatch = true;
      }
    }
  });

  addRemoveLabelNew.textContent = "\"" + addRemoveLabelFilter.value + "\" (create new)";
  addRemoveLabelNew.style.display = (exactMatch || loweredFilter === "") ? "none" : "";


  // add the missing dom entries
  var i;
  for (i = addRemoveLabelList.childElementCount; i < addRemoveLabelDialogFilteredList.length; i += 1) {
    addRemoveLabelList.insertAdjacentHTML('beforeend',
      '<div class="label-dialog-item">' +
        '<input type="checkbox" class="label-dialog-checkbox">' +
        '<button class="button label-dialog-trash">' +
          '<label class="icon icon-trash"></label>' +
        '</button>' +
        '<button class="button label-dialog-rename">' +
          '<label class="icon icon-tag"></label>' +
        '</button>' +
        '<input type="color" class="label-dialog-color"></span>' +
        '<span class="label-dialog-name"></span>' +
      '</div>');
  }
  // remove the extra dom entries
  while (addRemoveLabelDialogFilteredList.length < addRemoveLabelList.childElementCount) {
    addRemoveLabelList.removeChild(addRemoveLabelList.lastChild);
  }

  var selectedTracks = selection.toTrackKeys().map(function(key) {
    return player.library.trackTable[key];
  });

  // overwrite existing dom entries
  for (i = 0; i < addRemoveLabelDialogFilteredList.length; i += 1) {
    var domItem = addRemoveLabelList.children[i];
    var labelDomItem = domItem.children[4];
    var label = addRemoveLabelDialogFilteredList[i];
    domItem.setAttribute('data-key', label.id);
    labelDomItem.textContent = label.name;

    var colorDomItem = domItem.children[3];
    colorDomItem.value = label.color;

    var checkboxDom = domItem.children[0];
    var allHaveLabel = true;
    var allMissingLabel = true;
    for (var track_i = 0; track_i < selectedTracks.length; track_i += 1) {
      var selectedTrack = selectedTracks[track_i];
      if (selectedTrack.labels[label.id]) {
        allMissingLabel = false;
      } else {
        allHaveLabel = false;
      }
    }
    if (allHaveLabel) {
      checkboxDom.checked = true;
      checkboxDom.indeterminate = false;
    } else if (allMissingLabel) {
      checkboxDom.checked = false;
      checkboxDom.indeterminate = false;
    } else {
      checkboxDom.checked = false;
      checkboxDom.indeterminate = true;
    }
  }
}

function onAddRemoveLabelListChange(ev) {
  ev.stopPropagation();
  ev.preventDefault();

  var clickedItem = getFirstChildToward(addRemoveLabelList, ev.target);
  if (!clickedItem) return;
  if (!havePerm('playlist')) return;
  var labelId = clickedItem.getAttribute('data-key');

  if (ev.target.classList.contains('label-dialog-color')) {
    player.updateLabelColor(labelId, ev.target.value);
  } else if (ev.target.classList.contains('label-dialog-checkbox')) {
    toggleLabelOnSelection(labelId);
  }
}

function onAddRemoveLabelListClick(ev) {
  if (ev.button !== 0) return;

  ev.stopPropagation();
  ev.preventDefault();

  var clickedItem = getFirstChildToward(addRemoveLabelList, ev.target);
  if (!clickedItem) return;
  if (!havePerm('playlist')) return;
  var labelId = clickedItem.getAttribute('data-key');
  var label = player.library.labelTable[labelId];

  var target = ev.target;
  if (target.tagName === 'LABEL') {
    target = target.parentNode;
  }

  if (target.classList.contains('label-dialog-trash')) {
      if (!confirm("You are about to delete the label \"" + label.name + "\"")) {
        return;
      }
      player.deleteLabels([labelId]);
  } else if (target.classList.contains('label-dialog-rename')) {
    var newName = prompt("Rename label \"" + label.name + "\" to:", label.name);
    player.renameLabel(labelId, newName);
  } else if (!ev.target.classList.contains("label-dialog-color") &&
             !ev.target.classList.contains("label-dialog-checkbox"))
  {
    var keepOpen = ev.shiftKey;
    if (!keepOpen) closeOpenDialog();

    toggleLabelOnSelection(labelId);

  }
}

function toggleLabelOnSelection(labelId) {
  var selectionTrackKeys = selection.toTrackKeys();
  var selectedTracks = selectionTrackKeys.map(function(key) {
    return player.library.trackTable[key];
  });

  var allHaveLabel = true;
  for (var track_i = 0; track_i < selectedTracks.length; track_i += 1) {
    var selectedTrack = selectedTracks[track_i];
    if (!selectedTrack.labels[labelId]) {
      allHaveLabel = false;
      break;
    }
  }
  if (allHaveLabel) {
    player.removeLabel(labelId, selectionTrackKeys);
  } else {
    player.addLabel(labelId, selectionTrackKeys);
  }
}

function onAddRemoveLabelNewClick(ev) {
  ev.stopPropagation();
  ev.preventDefault();
  if (!havePerm('playlist')) return;
  if (!ev.shiftKey) closeOpenDialog();
  var label = player.createLabel(addRemoveLabelFilter.value);
  player.addLabel(label.id, selection.toTrackKeys());
}

function handleAutoDjClick(ev) {
  toggleAutoDj();
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

function firstElemWithClass(parentDom, className, childDom) {
  for (;;) {
    if (childDom.classList.contains(className)) {
      return childDom;
    }
    childDom = childDom.parentNode;
    if (!childDom || parentDom === childDom) {
      return null;
    }
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

  menuDeletePlaylist.style.display = (type === 'playlist') ? "" : "none";
  menuRenamePlaylist.style.display = (type === 'playlist') ? "" : "none";
  if (type === 'playlistItem') {
    menuRemove.style.display = "";
    menuRemove.firstChild.textContent = "Remove from Playlist";
  } else if (type === 'playlist') {
    menuRemove.style.display = "";
    menuRemove.firstChild.textContent = "Clear Playlist";
  } else if (type === 'queue') {
    menuRemove.style.display = "";
    menuRemove.firstChild.textContent = "Remove from Queue";
  } else {
    menuRemove.style.display = "none";
  }

  menuShuffle.style.display =
    (type === 'playlist' || type === 'playlistItem' || type === 'queue') ? "" : "none";

  menuDownload.firstChild.setAttribute('href', makeDownloadHref());
  updateMenuDisableState(contextMenuDom);

  // must make it visible for width and height properties to exist
  contextMenuDom.style.display = "";

  // make it so that the mouse cursor is not immediately over the menu
  var leftPos = x + 1;
  var topPos = y + 1;
  // avoid menu going outside document boundaries
  if (leftPos + contextMenuDom.offsetWidth >= window.innerWidth) {
    leftPos = x - contextMenuDom.offsetWidth - 1;
  }
  if (topPos + contextMenuDom.offsetHeight >= window.innerHeight) {
    topPos = y - contextMenuDom.offsetHeight - 1;
  }
  contextMenuDom.style.left = leftPos + "px";
  contextMenuDom.style.top = topPos + "px";
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

  genericTreeUi(playlistsListDom, {
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

function onEditTagsContextMenu(ev) {
  ev.preventDefault();
  ev.stopPropagation();
  if (!havePerm('admin')) return;
  if (selection.isEmpty()) return;
  removeContextMenu();
  editTagsTrackKeys = selection.toTrackKeys();
  editTagsTrackIndex = 0;
  showEditTags();
}

function updateEditTagsUi() {
  var multiple = editTagsTrackKeys.length > 1;
  prevDom.disabled = !isBtnOn(perDom) || editTagsTrackIndex === 0;
  nextDom.disabled = !isBtnOn(perDom) || (editTagsTrackIndex === editTagsTrackKeys.length - 1);
  prevDom.style.visibility = multiple ? 'visible' : 'hidden';
  nextDom.style.visibility = multiple ? 'visible' : 'hidden';
  perDom.style.visibility = multiple ? 'visible' : 'hidden';
  var multiCheckBoxVisible = multiple && !isBtnOn(perDom);
  var trackKeysToUse = isBtnOn(perDom) ? [editTagsTrackKeys[editTagsTrackIndex]] : editTagsTrackKeys;

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
  popDialog(editTagsDialogDom, "Edit Tags", 650, Math.min(640, window.innerHeight - 40));
  updateBtnOn(perDom, false);
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
      closeOpenDialog();
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

  function togglePerTrack(ev) {
    updateBtnOn(perDom, !isBtnOn(perDom));
    updateEditTagsUi();
  }

  document.getElementById('edit-tags-ok').addEventListener('click', saveAndClose, false);
  document.getElementById('edit-tags-cancel').addEventListener('click', callCloseOpenDialog, false);
  perDom.addEventListener('click', togglePerTrack, false);
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
    var trackKeysToUse = isBtnOn(perDom) ? [editTagsTrackKeys[editTagsTrackIndex]] : editTagsTrackKeys;
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
    closeOpenDialog();
  }
}

function updateSliderUi() {
  var percent = parseFloat(trackSliderDom.value) * 100;
  trackSliderDom.style.backgroundSize = percent + "% 100%";
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

function onTrackSliderChange(ev) {
  updateSliderUi();
  if (!player.currentItem) return;
  player.seek(null, parseFloat(trackSliderDom.value) * player.currentItem.track.duration);
}

function onTrackSliderInput(ev) {
  updateSliderUi();
  if (!player.currentItem) return;
  nowPlayingElapsedDom.textContent = formatTime(parseFloat(trackSliderDom.value) * player.currentItem.track.duration);
}

function onTrackSliderMouseDown(ev) {
  userIsSeeking = true;
}

function onTrackSliderMouseUp(ev) {
  userIsSeeking = false;
}

function setServerVol(ev) {
  var snap = 0.05;
  var val = parseFloat(volSlider.value);
  if (Math.abs(val - 1) < snap) {
    val = 1;
  }
  player.setVolume(val);
  volNumDom.textContent = Math.round(val * 100);
  volWarningDom.style.display = (val > 1) ? "" : "none";
}


function setUpNowPlayingUi() {
  nowPlayingToggleDom.addEventListener('click', onNowPlayingToggleMouseDown, false);
  nowPlayingPrevDom.addEventListener('click', onNowPlayingPrevMouseDown, false);
  nowPlayingNextDom.addEventListener('click', onNowPlayingNextMouseDown, false);
  nowPlayingStopDom.addEventListener('click', onNowPlayingStopMouseDown, false);

  trackSliderDom.addEventListener('change', onTrackSliderChange, false);
  trackSliderDom.addEventListener('input', onTrackSliderInput, false);
  trackSliderDom.addEventListener('mousedown', onTrackSliderMouseDown, false);
  trackSliderDom.addEventListener('mouseup', onTrackSliderMouseUp, false);

  volSlider.addEventListener('change', setServerVol, false);
  volSlider.addEventListener('input', setServerVol, false);
  volSlider.addEventListener('mousedown', onVolSliderMouseDown, false);
  volSlider.addEventListener('mouseup', onVolSliderMouseUp, false);

  setInterval(updateSliderPos, 100);
}

function onVolSliderMouseDown(ev) {
  userIsVolumeSliding = true;
}

function onVolSliderMouseUp(ev) {
  userIsVolumeSliding = false;
}

function clickTab(tab) {
  unselectTabs();
  tab.tab.classList.add('active');
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
    tab.tab.classList.remove('active');
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

  var progressBar = document.createElement('progress');
  var cancelBtnDom = document.createElement('button');
  cancelBtnDom.classList.add('button');
  cancelBtnDom.textContent = "Cancel";
  cancelBtnDom.addEventListener('click', onCancel, false);

  uploadWidgetDom.appendChild(progressBar);
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
    progressBar.value = progress;
  }

  function onLoad(ev) {
    cleanup();
  }

  function onCancel(ev) {
    req.abort();
    cleanup();
  }

  function cleanup() {
    progressBar.parentNode.removeChild(progressBar);
    cancelBtnDom.parentNode.removeChild(cancelBtnDom);
  }
}

function setAutoUploadBtnState() {
  if (localState.autoQueueUploads) {
    autoQueueUploadsDom.classList.add('on');
    autoQueueUploadsDom.value = "On";
  } else {
    autoQueueUploadsDom.classList.remove('on');
    autoQueueUploadsDom.value = "Off";
  }
}

function onAutoQueueUploadClick(ev) {
  localState.autoQueueUploads = !localState.autoQueueUploads;
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
  } else if (ev.which === 13 && !ev.shiftKey) {
    importNames();
  }
}

function onUploadInputChange(ev) {
  uploadFiles(this.files);
}

function setUpUploadUi() {
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

  if (localState.lastfm.scrobbling_on) {
    toggleScrobbleDom.classList.add('on');
    toggleScrobbleDom.value = "On";
  } else {
    toggleScrobbleDom.classList.remove('on');
    toggleScrobbleDom.value = "Off";
  }
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
  authPermPlaylistDom.style.display = havePerm('playlist') ? "" : "none";
  authPermAdminDom.style.display = havePerm('admin') ? "" : "none";
  settingsAuthRequestDom.style.display =
    (myUser.registered && !myUser.requested && !myUser.approved) ? "" : "none";
  settingsAuthLogoutDom.style.display = myUser.registered ? "" : "none";
  settingsAuthEditDom.value = myUser.registered ? 'Edit' : 'Register';
  settingsUsersDom.style.display = havePerm('admin') ? "" : "none";
  settingsRequestsDom.style.display = (havePerm('admin') && !!request) ? "" : "none";

  toggleHardwarePlaybackDom.disabled = !havePerm('admin');
  toggleHardwarePlaybackDom.setAttribute('title', havePerm('admin') ? "" : "Requires admin privilege.");

  updateStreamUrlUi();
}

function updateStreamUrlUi() {
  streamUrlDom.setAttribute('href', streamEndpoint);
}

function updateSettingsAdminUi() {
  if (hardwarePlaybackOn) {
    toggleHardwarePlaybackDom.classList.add('on');
    toggleHardwarePlaybackDom.value = "On";
  } else {
    toggleHardwarePlaybackDom.classList.remove('on');
    toggleHardwarePlaybackDom.value = "Off";
  }
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
  localState.lastfm.scrobbling_on = !localState.lastfm.scrobbling_on;
  saveLocalState();
  var msg = localState.lastfm.scrobbling_on ? 'lastFmScrobblersAdd' : 'lastFmScrobblersRemove';
  var params = {
    username: localState.lastfm.username,
    sessionKey: localState.lastfm.session_key
  };
  socket.send(msg, params);
  updateLastFmSettingsUi();
}

function onHardwarePlaybackClick(ev) {
  hardwarePlaybackOn = !hardwarePlaybackOn;
  socket.send('hardwarePlayback', hardwarePlaybackOn);
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
  var revealPassword = !isBtnOn(authShowPasswordDom);
  updateBtnOn(authShowPasswordDom, revealPassword);
  authPasswordDom.type = revealPassword ? 'text' : 'password';
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
  ensureAdminBtn.addEventListener('click', sendEnsureAdminUser, false);
  lastFmSignOutDom.addEventListener('click', onLastFmSignOutClick, false);
  toggleScrobbleDom.addEventListener('click', onToggleScrobbleClick, false);
  toggleHardwarePlaybackDom.addEventListener('click', onHardwarePlaybackClick, false);
  settingsAuthEditDom.addEventListener('click', onSettingsAuthEditClick, false);
  settingsAuthSaveDom.addEventListener('click', settingsAuthSave, false);
  settingsAuthCancelDom.addEventListener('click', settingsAuthCancel, false);
  authUsernameDom.addEventListener('keydown', handleUserOrPassKeyDown, false);
  authPasswordDom.addEventListener('keydown', handleUserOrPassKeyDown, false);
  authShowPasswordDom.addEventListener('click', onAuthShowPasswordChange, false);
  settingsAuthRequestDom.addEventListener('click', onSettingsAuthRequestClick, false);
  settingsAuthLogoutDom.addEventListener('click', onSettingsAuthLogoutClick, false);

  userPermReadDom.addEventListener('click', updateSelectedUserPerms, false);
  userPermAddDom.addEventListener('click', updateSelectedUserPerms, false);
  userPermControlDom.addEventListener('click', updateSelectedUserPerms, false);
  userPermPlaylistDom.addEventListener('click', updateSelectedUserPerms, false);
  userPermAdminDom.addEventListener('click', updateSelectedUserPerms, false);

  settingsUsersSelect.addEventListener('change', updatePermsForSelectedUser, false);
  settingsDelUserDom.addEventListener('click', onSettingsDelUserClick, false);

  requestApproveDom.addEventListener('click', onRequestApproveClick, false);
  requestDenyDom.addEventListener('click', onRequestDenyClick, false);

  document.getElementById('keyboard-shortcuts-link').addEventListener('click', showKeyboardShortcuts, false);
}

function showKeyboardShortcuts(ev) {
  ev.preventDefault();
  ev.stopPropagation();
  popDialog(shortcutsDom, "Keyboard Shortcuts", 600, window.innerHeight - 40);
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

function isBtnOn(btn) {
  return btn.classList.contains('on');
}

function updateBtnOn(btn, on) {
  if (on) {
    btn.classList.add('on');
  } else {
    btn.classList.remove('on');
  }
}

function updatePermsForSelectedUser() {
  var selectedUserId = settingsUsersSelect.value;
  var user = player.usersTable[selectedUserId];
  if (!user) return;

  updateBtnOn(userPermReadDom, user.perms.read);
  updateBtnOn(userPermAddDom, user.perms.add);
  updateBtnOn(userPermControlDom, user.perms.control);
  updateBtnOn(userPermPlaylistDom, user.perms.playlist);
  updateBtnOn(userPermAdminDom, user.perms.admin);

  settingsDelUserDom.disabled = (selectedUserId === PlayerClient.GUEST_USER_ID);
}

function updateSelectedUserPerms(ev) {
  updateBtnOn(ev.target, !isBtnOn(ev.target));
  socket.send('updateUser', {
    userId: settingsUsersSelect.value,
    perms: {
      read: isBtnOn(userPermReadDom),
      add: isBtnOn(userPermAddDom),
      control: isBtnOn(userPermControlDom),
      playlist: isBtnOn(userPermPlaylistDom),
      admin: isBtnOn(userPermAdminDom),
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
  tabs.events.tab.textContent = eventsTabText;
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
  tabs.upload.tab.textContent = importTabText;

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

function eventLabelName(ev, name) {
  if (name) {
    return "label " + name;
  } else {
    return ev.label ? ("label " + ev.label.name) : "(deleted label)";
  }
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
        '<span class="streaming icon icon-signal-diag"></span>' +
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
    ev.preventDefault();
    if (libFilterDom.value.length === 0) {
      libFilterDom.blur();
    } else {
      // queue up a search refresh now, because if the user holds Escape,
      // it will blur the search box, and we won't get a keyup for Escape.
      setTimeout(clearBoxAndSearch, 0);
    }
    return;
  case 13: // Enter
    ev.preventDefault();
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
        return;
      }
    }
    if (ev.shiftKey) {
      player.queueTracksNext(keys);
    } else {
      player.queueOnQueue(keys);
    }
    return;
  case 40:
    ev.preventDefault();
    selection.selectOnlyFirstPos('library');
    selection.scrollToCursor();
    refreshSelection();
    libFilterDom.blur();
    return;
  case 38:
    ev.preventDefault();
    selection.selectOnlyLastPos('library');
    selection.scrollToCursor();
    refreshSelection();
    libFilterDom.blur();
    return;
  }

  function clearBoxAndSearch() {
    libFilterDom.value = "";
    ensureSearchHappensSoon();
  }
}

function setUpLibraryUi() {
  libFilterDom.addEventListener('keydown', onLibFilterKeyDown, false);
  libFilterDom.addEventListener('keyup', ensureSearchHappensSoon, false);
  libFilterDom.addEventListener('cut', ensureSearchHappensSoon, false);
  libFilterDom.addEventListener('paste', ensureSearchHappensSoon, false);
  genericTreeUi(libraryArtistsDom, {
    toggleExpansion: toggleLibraryExpansion,
    isSelectionOwner: function(){
      return selection.isLibrary();
    }
  });
  contextMenuDom.addEventListener('mousedown', preventEventDefault, false);

  menuQueue.addEventListener('click', function(ev) {
    ev.stopPropagation();
    ev.preventDefault();
    player.queueOnQueue(selection.toTrackKeys());
    removeContextMenu();
  }, false);
  menuQueueNext.addEventListener('click', function(ev) {
    ev.stopPropagation();
    ev.preventDefault();
    player.queueTracksNext(selection.toTrackKeys());
    removeContextMenu();
  }, false);
  menuQueueRandom.addEventListener('click', function(ev) {
    ev.stopPropagation();
    ev.preventDefault();
    player.queueOnQueue(selection.toTrackKeys(true));
    removeContextMenu();
  }, false);
  menuQueueNextRandom.addEventListener('click', function(ev) {
    ev.stopPropagation();
    ev.preventDefault();
    player.queueTracksNext(selection.toTrackKeys(true));
    removeContextMenu();
  }, false);
  menuDownload.addEventListener('click', onDownloadContextMenu, false);
  menuDelete.addEventListener('click', onDeleteContextMenu, false);
  menuEditTags.addEventListener('click', onEditTagsContextMenu, false);
  menuDeletePlaylist.addEventListener('click', onDeletePlaylistContextMenu, false);
  menuRenamePlaylist.addEventListener('click', onRenamePlaylistContextMenu, false);
  menuRemove.addEventListener('click', onRemoveFromPlaylistContextMenu, false);
  menuShuffle.addEventListener('click', onShuffleContextMenu, false);
  menuAddToPlaylist.addEventListener('click', onAddToPlaylistContextMenu, false);
  menuAddRemoveLabel.addEventListener('click', onAddRemoveLabelContextMenu, false);
}

function onAddToPlaylistContextMenu(ev) {
  ev.preventDefault();
  ev.stopPropagation();
  if (!havePerm('playlist')) return;
  if (selection.isEmpty()) return;
  removeContextMenu();
  popAddToPlaylistDialog();
}

function onAddRemoveLabelContextMenu(ev) {
  ev.preventDefault();
  ev.stopPropagation();
  if (!havePerm('playlist')) return;
  if (selection.isEmpty()) return;
  removeContextMenu();
  updateLabelsUi();
  popAddRemoveLabelDialog();
}

function maybeRenamePlaylistAtCursor() {
  if (selection.cursorType !== 'playlist') return;
  var playlist = player.playlistTable[selection.cursor];
  var newName = prompt("Rename playlist \"" + playlist.name + "\" to:", playlist.name);
  if (newName) {
    player.renamePlaylist(playlist, newName);
  }
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

function onRenamePlaylistContextMenu(ev) {
  ev.stopPropagation();
  ev.preventDefault();
  maybeRenamePlaylistAtCursor();
  removeContextMenu();
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

function blockContextMenu(ev) {
  if (ev.altKey) return;
  ev.preventDefault();
  ev.stopPropagation();
}

function genericTreeUi(elem, options) {
  elem.addEventListener('mousedown', onElemMouseDown, false);
  elem.addEventListener('contextmenu', blockContextMenu, false);
  elem.addEventListener('dblclick', onDblClick, false);

  function onElemMouseDown(ev) {
    ev.stopPropagation();
    ev.preventDefault();

    var expandableElem = firstElemWithClass(elem, 'expandable', ev.target);
    if (expandableElem && ev.target === expandableElem.children[0]) {
      options.toggleExpansion(expandableElem.parentNode);
      return;
    }

    var clickableElem = firstElemWithClass(elem, 'clickable', ev.target);
    if (!clickableElem) {
      return;
    }

    document.activeElement.blur();
    var type = clickableElem.getAttribute('data-type');
    var key = clickableElem.getAttribute('data-key');
    if (ev.which === 1) {
      leftMouseDown(ev);
    } else if (ev.which === 3) {
      if (ev.altKey) {
        return;
      }
      rightMouseDown(ev);
    }
    function leftMouseDown(ev){
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

  }

  function onDblClick(ev) {
    ev.stopPropagation();
    ev.preventDefault();

    var expandableElem = firstElemWithClass(elem, 'expandable', ev.target);
    if (expandableElem && ev.target === expandableElem.children[0]) {
      return;
    }
    var clickableElem = firstElemWithClass(elem, 'clickable', ev.target);
    if (clickableElem) {
      queueSelection(ev);
    }
  }
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

function updateMenuDisableState(menu) {
  for (var permName in menuPermSelectors) {
    var menuItemList = menuPermSelectors[permName];
    enableDisable(menuItemList, havePerm(permName));
  }

  function enableDisable(menuItemList, enable) {
    menuItemList.forEach(function(menuItem) {
      menuItem.setAttribute('title', enable ? '' : "Insufficient privileges. See Settings.");
      if (enable) {
        menuItem.classList.remove('disabled');
      } else {
        menuItem.classList.add('disabled');
      }
    });
  }
}

function setUpUi() {
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
  streamBtnDom.addEventListener('click', toggleStreamStatusEvent, false);
  clientVolSlider.addEventListener('change', setClientVol, false);
  clientVolSlider.addEventListener('input', setClientVol, false);

  clientVolSlider.value = localState.clientVolume || 1;
  setClientVol();
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
  return "pl-pl-" + s;
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

function getStreamerCount() {
  var count = player.anonStreamers;
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

function renderStreamButton() {
  streamBtnLabel.textContent = getStreamButtonLabel();
  updateBtnOn(streamBtnDom, tryingToStream);
  clientVolDom.style.display = tryingToStream ? "" : "none";
}

function toggleStreamStatus() {
  tryingToStream = !tryingToStream;
  sendStreamingStatus();
  renderStreamButton();
  updateStreamPlayer();
}

function toggleStreamStatusEvent(ev) {
  ev.stopPropagation();
  ev.preventDefault();
  toggleStreamStatus();
}

function sendStreamingStatus() {
  socket.send("setStreaming", tryingToStream);
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
      streamAudio.src = streamEndpoint;
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

function setClientVol() {
  setStreamVolume(clientVolSlider.value);
}

function setStreamVolume(v) {
  if (v < 0) v = 0;
  if (v > 1) v = 1;
  streamAudio.volume = v;
  localState.clientVolume = v;
  saveLocalState();
  clientVolSlider.value = streamAudio.volume;
}

function init() {
  loadLocalState();
  socket = new Socket();
  var queryObj = parseQueryString();
  if (queryObj.token) {
    socket.on('connect', function() {
      socket.send('lastFmGetSession', queryObj.token);
    });
    socket.on('lastFmGetSessionSuccess', function(params){
      localState.lastfm.username = params.session.name;
      localState.lastfm.session_key = params.session.key;
      localState.lastfm.scrobbling_on = false;
      saveLocalState();
      refreshPage();
    });
    socket.on('lastFmGetSessionError', function(message){
      alert("Error authenticating: " + message);
      refreshPage();
    });
    return;
  }
  socket.on('hardwarePlayback', function(isOn) {
    hardwarePlaybackOn = isOn;
    updateSettingsAdminUi();
  });
  socket.on('lastFmApiKey', updateLastFmApiKey);
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
  socket.on('streamEndpoint', function(data) {
    streamEndpoint = data;
    updateStreamPlayer();
    updateStreamUrlUi();
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
    socket.send('subscribe', {name: 'streamEndpoint'});
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
  player.on('libraryUpdate', function() {
    triggerRenderLibrary();
    triggerLabelsUpdate();
    triggerRenderQueue();
    renderNowPlaying();
    renderQueueButtons();
  });
  player.on('queueUpdate', triggerRenderQueue);
  player.on('scanningUpdate', triggerRenderQueue);
  player.on('playlistsUpdate', triggerPlaylistsUpdate);
  player.on('labelsUpdate', function() {
    triggerLabelsUpdate();
    triggerRenderQueue();
  });
  player.on('volumeUpdate', renderVolumeSlider);
  player.on('statusUpdate', function(){
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
  player.on('anonStreamers', renderStreamButton);
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

function popDialog(dom, title, width, height) {
  blackoutDom.style.display = "";

  dom.parentNode.removeChild(dom);
  modalContentDom.appendChild(dom);

  modalTitleDom.textContent = title;

  modalDom.style.left = (window.innerWidth / 2 - width / 2) + "px";
  modalDom.style.top = (window.innerHeight / 2 - height / 2) + "px";
  modalDom.style.width = width + "px";
  modalDom.style.height = height + "px";
  modalDom.style.display = "";

  modalContentDom.style.height = (height - modalHeaderDom.clientHeight - 20) + "px";

  dom.style.display = "";
  dom.focus();

  closeOpenDialog = function() {
    blackoutDom.style.display = "none";
    modalDom.style.display = "none";
    modalDom.style.display = "none";
    dom.style.display = "none";
    dom.parentNode.removeChild(dom);
    document.body.appendChild(dom);

    closeOpenDialog = noop;
  };
}
