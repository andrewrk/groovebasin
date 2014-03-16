var EventEmitter = require('events').EventEmitter;
var util = require('util');

module.exports = MpdApiServer;

// stuff that is global to all connected mpd clients
util.inherits(MpdApiServer, EventEmitter);
function MpdApiServer(player) {
  var self = this;
  EventEmitter.call(self);
  self.gbIdToMpdId = {};
  self.mpdIdToGbId = {};
  self.nextMpdId = 0;
  self.singleMode = false;
  self.clients = [];

  player.on('volumeUpdate', onVolumeUpdate);
  player.on('repeatUpdate', updateOptionsSubsystem);
  player.on('dynamicModeOn', updateOptionsSubsystem);
  player.on('playlistUpdate', onPlaylistUpdate);
  player.on('deleteDbTrack', updateDatabaseSubsystem);
  player.on('addDbTrack', updateDatabaseSubsystem);
  player.on('updateDbTrack', updateDatabaseSubsystem);

  function onVolumeUpdate() {
    self.subsystemUpdate('mixer');
  }
  function onPlaylistUpdate() {
    // TODO make these updates more fine grained
    self.subsystemUpdate('playlist');
    self.subsystemUpdate('player');
  }
  function updateOptionsSubsystem() {
    self.subsystemUpdate('options');
  }
  function updateDatabaseSubsystem() {
    self.subsystemUpdate('database');
  }
}

MpdApiServer.prototype.handleClientEnd = function(client) {
  var index = this.clients.indexOf(client);
  if (index !== -1) this.clients.splice(index, 1);
};
MpdApiServer.prototype.handleNewClient = function(client) {
  this.clients.push(client);
};

MpdApiServer.prototype.subsystemUpdate = function(subsystem) {
  this.clients.forEach(function(client) {
    client.updatedSubsystems[subsystem] = true;
    if (client.isIdle) client.handleIdle();
  });
};

MpdApiServer.prototype.toMpdId = function(grooveBasinId) {
  var mpdId = this.gbIdToMpdId[grooveBasinId];
  if (!mpdId) {
    mpdId = this.nextMpdId++;
    this.gbIdToMpdId[grooveBasinId] = mpdId;
    this.mpdIdToGbId[mpdId] = grooveBasinId;
  }
  return mpdId;
};

MpdApiServer.prototype.fromMpdId = function(mpdId) {
  return this.mpdIdToGbId[mpdId];
};

MpdApiServer.prototype.setSingleMode = function(mode) {
  this.singleMode = mode;
  this.subsystemUpdate('options');
};
