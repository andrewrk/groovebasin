var EventEmitter = require('events').EventEmitter;
var util = require('util');

module.exports = Socket;

util.inherits(Socket, EventEmitter);
function Socket() {
  var self = this;
  EventEmitter.call(self);
  self.isConnected = false;
  createWs();

  function createWs() {
    var host = window.document.location.host;
    var pathname = window.document.location.pathname;
    var match = host.match(/^(.+):(\d+)$/);
    var port = match ? parseInt(match[2], 10) : 80;
    var hostName = match ? match[1] : host;
    var wsUrl = 'ws://' + hostName + ':' + port + pathname;
    self.ws = new WebSocket(wsUrl);

    self.ws.addEventListener('message', onMessage, false);
    self.ws.addEventListener('error', timeoutThenCreateNew, false);
    self.ws.addEventListener('close', timeoutThenCreateNew, false);
    self.ws.addEventListener('open', onOpen, false);

    function onOpen() {
      self.isConnected = true;
      self.emit('connect');
    }

    function onMessage(event) {
      var msg = JSON.parse(event.data);
      self.emit(msg.name, msg.args);
    }

    function timeoutThenCreateNew() {
      self.ws.removeEventListener('error', timeoutThenCreateNew, false);
      self.ws.removeEventListener('close', timeoutThenCreateNew, false);
      self.ws.removeEventListener('open', onOpen, false);
      if (self.isConnected) {
        self.isConnected = false;
        self.emit('disconnect');
      }
      setTimeout(createWs, 1000);
    }
  }
}

Socket.prototype.send = function(name, args) {
  this.ws.send(JSON.stringify({
    name: name,
    args: args,
  }));
}
