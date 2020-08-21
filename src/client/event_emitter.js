var slice = Array.prototype.slice;

module.exports = EventEmitter;

function EventEmitter() {
  this.listeners = {};
}

EventEmitter.prototype.on = function(name, listener) {
  var listeners = this.listeners[name] = (this.listeners[name] || []) ;
  listeners.push(listener);
};

EventEmitter.prototype.emit = function(name) {
  var args = slice.call(arguments, 1);
  var listeners = this.listeners[name];
  if (!listeners) return;
  for (var i = 0; i < listeners.length; i += 1) {
    var listener = listeners[i];
    listener.apply(null, args);
  }
};

EventEmitter.prototype.removeListener = function(name, listener) {
  var listeners = this.listeners[name];
  if (!listeners) return;
  var badIndex = listeners.indexOf(listener);
  if (badIndex === -1) return;
  listeners.splice(badIndex, 1);
};
