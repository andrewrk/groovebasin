var fs = require('fs');
var path = require('path');

module.exports = Delete;

function Delete(gb) {
  this.gb = gb;
  this.gb.on('socketConnect', onSocketConnection.bind(this));
}

function onSocketConnection(client) {
  var self = this;
  client.on('DeleteFromLibrary', function(keys) {
    if (!client.permissions.admin) {
      console.warn("User without admin permission trying to delete songs");
      return;
    }
    keys.forEach(function(key) {
      self.gb.player.deleteFile(key);
    });
  });
}
