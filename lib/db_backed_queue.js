var Pend = require('pend');

module.exports = DbBackedQueue;

function DbBackedQueue(options) {
  this.db = options.db;
  this.dispatch = options.dispatch;
  this.keyPrefix = options.keyPrefix;
  this.pend = new Pend();
  this.pend.max = options.max;
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
  self.pend.go(function(cb) {
    self.dispatch(id, obj, function(err) {
      // if an error is returned, don't delete the queue item
      if (err) return;
      self.db.del(self.keyPrefix + id, cb);
    });
  });
}
