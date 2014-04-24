var Duplex = require('stream').Duplex;
var util = require('util');

module.exports = ProtocolParser;

util.inherits(ProtocolParser, Duplex);
function ProtocolParser(options) {
  var streamOptions = extend(extend({}, options.streamOptions || {}), {decodeStrings: false});
  Duplex.call(this, streamOptions);
  this.player = options.player;

  this.buffer = "";
  this.alreadyClosed = false;
}

ProtocolParser.prototype._read = function(size) {};

ProtocolParser.prototype._write = function(chunk, encoding, callback) {
  var self = this;

  var lines = chunk.split("\n");
  self.buffer += lines[0];
  if (lines.length === 1) return callback();
  handleLine(self.buffer);
  var lastIndex = lines.length - 1;
  for (var i = 1; i < lastIndex; i += 1) {
    handleLine(lines[i]);
  }
  self.buffer = lines[lastIndex];
  callback();

  function handleLine(line) {
    var jsonObject;
    try {
      jsonObject = JSON.parse(line);
    } catch (err) {
      console.warn("received invalid json:", err.message);
      self.sendMessage("error", "invalid json: " + err.message);
      return;
    }
    if (typeof jsonObject !== 'object') {
      console.warn("received json not an object:", jsonObject);
      self.sendMessage("error", "expected json object");
      return;
    }
    self.emit('message', jsonObject.name, jsonObject.args);
  }
};

ProtocolParser.prototype.sendMessage = function(name, args) {
  if (this.alreadyClosed) return;
  var jsonObject = {name: name, args: args};
  this.push(JSON.stringify(jsonObject));
};

ProtocolParser.prototype.close = function() {
  if (this.alreadyClosed) return;
  this.push(null);
  this.alreadyClosed = true;
};

function extend(o, src) {
  for (var key in src) o[key] = src[key];
  return o;
}
