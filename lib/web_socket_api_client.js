var EventEmitter = require('events').EventEmitter;
var util = require('util');

module.exports = WebSocketApiClient;

util.inherits(WebSocketApiClient, EventEmitter);
function WebSocketApiClient(ws) {
  EventEmitter.call(this);
  this.ws = ws;
  this.initialize();
}

WebSocketApiClient.prototype.sendMessage = function(name, args) {
  try {
    this.ws.send(JSON.stringify({
      name: name,
      args: args,
    }));
  } catch (err) {
    // nothing to do
    // client might have disconnected by now
  }
};

WebSocketApiClient.prototype.close = function() {
  this.ws.close();
};

WebSocketApiClient.prototype.initialize = function() {
  var self = this;
  self.ws.on('message', function(data, flags) {
    if (flags.binary) {
      console.warn("ignoring binary web socket message");
      return;
    }
    var msg;
    try {
      msg = JSON.parse(data);
    } catch (err) {
      console.warn("received invalid JSON from web socket:", err.message);
      return;
    }
    self.emit('message', msg.name, msg.args);
  });
  self.ws.on('error', function(err) {
    console.error("web socket error:", err.stack);
  });
  self.ws.on('close', function() {
    self.emit('close');
  });
};
