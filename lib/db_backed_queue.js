var Pend = require('pend');

module.exports = DbBackedQueue;

function DbBackedQueue(options) {
  this.db = options.db;
  this.dispatch = options.dispatch;
  this.keyPrefix = options.keyPrefix;
  this.pend = new Pend();
  this.pend.max = options.max;
  this.activeIds = {};
}

DbBackedQueue.prototype.flush = function(cb) {
  var self = this;
  var stream = self.db.createReadStream({
    start: self.keyPrefix,
  });
  stream.on('data', function(data) {
    if (data.key.indexOf(self.keyPrefix) !== 0) {
      stream.removeAllListeners();
      stream.destroy();
      cb();
      return;
    }
    var id = data.key.substring(self.keyPrefix.length);
    var obj = JSON.parse(data.value);
    self.pendDispatch(id, obj);
  });
  stream.on('error', function(err) {
    stream.removeAllListeners();
    stream.destroy();
    cb(err);
  });
  stream.on('close', function() {
    cb();
  });
};

DbBackedQueue.prototype.enqueue = function(id, obj) {
  var self = this;
  self.db.put(self.keyPrefix + id, JSON.stringify(obj), function() {
    self.pendDispatch(id, obj);
  });
};

DbBackedQueue.prototype.pendDispatch = function(id, obj) {
  var self = this;
  var existing = self.activeIds[id];
  if (existing) {
    dispatch();
  } else {
    self.pend.go(dispatch);
  }

  function dispatch(cb) {
    cb = cb || noop;
    self.activeIds[id] = true;
    self.dispatch(id, obj, function(err) {
      delete self.activeIds[id];
      // if an error is returned, don't delete the queue item
      if (err) return;
      self.db.del(self.keyPrefix + id, cb);
    });
  }
}

function noop() {}
