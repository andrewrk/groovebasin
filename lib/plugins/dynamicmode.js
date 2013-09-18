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
  // TODO: implement
};
DynamicMode.prototype.updateStickers = function(){
  // TODO: implement
};
DynamicMode.prototype.getRandomSongFiles = function(count){
  // TODO: implement
};
