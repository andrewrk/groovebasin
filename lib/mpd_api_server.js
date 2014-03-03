var EventEmitter = require('events').EventEmitter;
var util = require('util');

module.exports = MpdApiServer;

// stuff that is global to all connected mpd clients
util.inherits(MpdApiServer, EventEmitter);
function MpdApiServer() {
  EventEmitter.call(this);
  this.gbIdToMpdId = {};
  this.mpdIdToGbId = {};
  this.nextMpdId = 0;
  this.singleMode = false;
}

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
  this.emit('singleMode');
};
