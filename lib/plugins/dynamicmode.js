var history_size = 10;
var future_size = 10;
var LAST_QUEUED_STICKER = "groovebasin.last-queued";

module.exports = DynamicMode;

function DynamicMode(gb) {
  this.gb = gb;
  this.previous_ids = {};
  this.got_stickers = false;
  this.last_queued = {};
  this.is_enabled = false;

  console.error("TODO: update dynamic mode to work directly with PlayerServer");
  return;

  this.gb.on('aboutToSaveState', this.saveState.bind(this));
  this.gb.on('stateRestored', this.restoreState.bind(this));
  this.gb.on('playerServerInit', this.playerServerInit.bind(this));
  this.gb.on('socketConnect', this.onSocketConnection.bind(this));
}

DynamicMode.prototype.restoreState = function(state){
  this.is_on = state.status.dynamic_mode == null ? false : state.status.dynamic_mode;
};
DynamicMode.prototype.saveState = function(state){
  state.status.dynamic_mode = this.is_on;
  state.status.dynamic_mode_enabled = this.is_enabled;
};
DynamicMode.prototype.playerServerInit = function(){
  this.gb.playerServer.on('statusupdate', this.checkDynamicMode.bind(this));
  this.gb.playerServer.on('playlistupdate', this.checkDynamicMode.bind(this));
  this.gb.playerServer.on('libraryupdate', this.updateStickers.bind(this));
  this.updateStickers();
};
DynamicMode.prototype.onSocketConnection = function(socket){
  var self = this;
  socket.on('DynamicMode', function(data){
    var args, did_anything, key, value;
    if (!self.is_enabled) {
      return;
    }
    args = JSON.parse(data);
    did_anything = false;
    for (key in args) {
      value = args[key];
      if (key === 'dynamic_mode') {
        if (self.is_on === value) {
          continue;
        }
        did_anything = true;
        self.is_on = value;
      }
    }
    if (did_anything) {
      self.checkDynamicMode();
      self.emit('status_changed');
    }
  });
  socket.on('Permissions', this.updateStickers.bind(this));
};
DynamicMode.prototype.checkDynamicMode = function(){
  var reason;
  if ((reason = this.checkDynamicModeOrWhyNot()) != null) {
    console.log("DynamicMode: not updating because: " + reason);
  }
};
DynamicMode.prototype.checkDynamicModeOrWhyNot = function(){
  var item_list, ref$, current_id, current_index, all_ids, new_files, last_key, i, len$, item, now, i$, file, delete_count, add_count, self = this;
  if (!this.is_enabled) {
    return "disabled";
  }
  if (!Object.keys(this.mpd.library.track_table).length) {
    return "no tracks";
  }
  if (!this.got_stickers) {
    return "no stickers";
  }
  item_list = this.mpd.playlist.item_list;
  current_id = (ref$ = this.mpd.status.current_item) != null ? ref$.id : void 8;
  current_index = -1;
  all_ids = {};
  new_files = [];
  last_key = null;
  for (i = 0, len$ = item_list.length; i < len$; ++i) {
    item = item_list[i];
    if (!(item.track != null && item.id != null)) {
      return "item with no track";
    }
    if (item.id === current_id) {
      current_index = i;
    }
    if (last_key == null || last_key < item.sort_key) {
      last_key = item.sort_key;
    }
    all_ids[item.id] = true;
    if (this.previous_ids[item.id] == null) {
      new_files.push(item.track.file);
    }
  }
  now = new Date();
  this.mpd.setStickers(new_files, LAST_QUEUED_STICKER, JSON.stringify(now), function(err){
    if (err) {
      console.warn("dynamic mode set stickers error:", err);
    }
  });
  for (i$ = 0, len$ = new_files.length; i$ < len$; ++i$) {
    file = new_files[i$];
    this.last_queued[file] = now;
  }
  if (current_index === -1) {
    current_index = 0;
  }
  if (this.is_on) {
    delete_count = Math.max(current_index - history_size, 0);
    if (history_size < 0) {
      delete_count = 0;
    }
    this.mpd.removeIds((function(){
      var to$, results$ = [];
      for (i = 0, to$ = delete_count; i < to$; ++i) {
        results$.push(item_list[i].id);
      }
      return results$;
    }()));
    add_count = Math.max(future_size - (item_list.length - current_index), 0);
    this.mpd.queueFiles(this.getRandomSongFiles(add_count), last_key, null, true);
  }
  this.previous_ids = all_ids;
  if (delete_count + add_count > 0) {
    this.emit('status_changed');
  }
  return null;
};
DynamicMode.prototype.updateStickers = function(){
  var self = this;
  this.mpd.findStickers('/', LAST_QUEUED_STICKER, function(err, stickers){
    var sticker, file, value, track;
    if (err) {
      console.error('dynamicmode find sticker error:', err);
      return;
    }
    for (sticker in stickers) {
      file = sticker[0], value = sticker[1];
      track = self.mpd.library.track_table[file];
      self.last_queued[file] = new Date(value);
    }
    self.got_stickers = true;
  });
};
DynamicMode.prototype.getRandomSongFiles = function(count){
  var never_queued, sometimes_queued, file, ref$, track, max_weight, triangle_area, rectangle_area, total_size, files, i, index, self = this;
  if (count === 0) {
    return [];
  }
  never_queued = [];
  sometimes_queued = [];
  for (file in (ref$ = this.mpd.library.track_table)) {
    track = ref$[file];
    if (this.last_queued[file] != null) {
      sometimes_queued.push(track);
    } else {
      never_queued.push(track);
    }
  }
  sometimes_queued.sort(function(a, b){
    return self.last_queued[b.file].getTime() - self.last_queued[a.file].getTime();
  });
  max_weight = sometimes_queued.length;
  triangle_area = Math.floor(max_weight * max_weight / 2);
  if (max_weight === 0) {
    max_weight = 1;
  }
  rectangle_area = max_weight * never_queued.length;
  total_size = triangle_area + rectangle_area;
  if (total_size === 0) {
    return [];
  }
  files = [];
  for (i = 0; i < count; ++i) {
    index = Math.random() * total_size;
    if (index < triangle_area) {
      track = sometimes_queued[Math.floor(Math.sqrt(index))];
    } else {
      track = never_queued[Math.floor((index - triangle_area) / max_weight)];
    }
    files.push(track.file);
  }
  return files;
};
