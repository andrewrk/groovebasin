var fs = require('fs');
var path = require('path');

module.exports = Delete;

function Delete(gb) {
  this.gb = gb;
  this.is_enabled = true;
  setup(this);
}

function setup(self) {
  self.gb.on('aboutToSaveState', function(state) {
    state.status.delete_enabled = self.is_enabled;
  });
  self.gb.on('socketConnect', onSocketConnection.bind(self));
  self.gb.on('stateRestored', function(state) {
    self.music_directory = state.mpd_conf.music_directory;
    if (self.music_directory == null) {
      self.is_enabled = false;
      console.warn("No music directory set. Delete disabled.");
      return;
    }
  });
}

function onSocketConnection(client) {
  var self = this;
  client.on('DeleteFromLibrary', function(data) {
    if (!client.permissions.admin) {
      console.warn("User without admin permission trying to delete songs");
      return;
    }
    var files = JSON.parse(data);
    var file = null;
    function next(err){
      var file;
      if (err) {
        console.error("deleting " + file + ": " + err.stack);
      } else if (file != null) {
        console.info("deleted " + file);
      }
      if ((file = files.shift()) == null) {
        self.gb.rescanLibrary();
      } else {
        fs.unlink(path.join(self.music_directory, file), next);
        // TODO remove from library?
      }
    }
    next();
  });
}
