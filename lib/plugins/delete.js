var Plugin = require('../plugin');
var util = require('util');
var fs = require('fs');
var path = require('path');

module.exports = Delete;

util.inherits(Delete, Plugin);
function Delete(bus) {
  var self = this;
  Plugin.call(self);
  bus.on('save_state', function(state) {
    state.status.delete_enabled = this.is_enabled;
  });
  bus.on('mpd', function(mpd){
    self.mpd = mpd;
  });
  bus.on('socket_connect', onSocketConnection.bind(self));
  bus.on('restore_state', function(state) {
    if ((self.music_directory = state.mpd_conf.music_directory) == null) {
      self.is_enabled = false;
      console.warn("No music directory set. Delete disabled.");
      return;
    }
  });
}
function onSocketConnection(client) {
  var self = this;
  client.on('DeleteFromLibrary', function(data){
    var files, file;
    if (!client.permissions.admin) {
      console.warn("User without admin permission trying to delete songs");
      return;
    }
    files = JSON.parse(data);
    file = null;
    function next(err){
      var file;
      if (err) {
        console.error("deleting " + file + ": " + err.stack);
      } else if (file != null) {
        console.info("deleted " + file);
      }
      if ((file = files.shift()) == null) {
        self.mpd.scanFiles(files);
      } else {
        fs.unlink(path.join(self.music_directory, file), next);
      }
    }
    next();
  });
}
