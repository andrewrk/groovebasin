var cpuCount = require('os').cpus().length;
var EventEmitter = require('events').EventEmitter;
var util = require('util');
var Pend = require('pend');

module.exports = DedupedQueue;

util.inherits(DedupedQueue, EventEmitter);
function DedupedQueue(options) {
  EventEmitter.call(this);

  this.maxAsync = options.maxAsync || cpuCount;
  this.processOne = options.processOne;

  this.pendingQueue = [];
  this.pendingSet = {};

  this.processingCount = 0;
  this.processingSet = {};

  this.paused = false;
}

DedupedQueue.prototype.clear = function() {
  this.pendingQueue.forEach(function(queueItem) {
    var err = new Error("dequeued");
    err.code = 'EDEQ';
    queueItem.cbs.forEach(function(cb) {
      cb(err);
    });
  });
  this.pendingQueue = [];
  this.pendingSet = {};
};

DedupedQueue.prototype.idInQueue = function(id) {
  return !!(this.pendingSet[id] || this.processingSet[id]);
};

DedupedQueue.prototype.add = function(id, item, cb) {
  var queueItem = this.pendingSet[id];
  if (queueItem) {
    if (cb) queueItem.cbs.push(cb);
    return;
  }
  queueItem = new QueueItem(id, item);
  if (cb) queueItem.cbs.push(cb);
  this.pendingSet[id] = queueItem;
  this.pendingQueue.push(queueItem);
  this.flush();
};

DedupedQueue.prototype.waitForId = function(id, cb) {
  var queueItem = this.pendingSet[id] || this.processingSet[id];
  if (!queueItem) return cb();
  queueItem.cbs.push(cb);
};

DedupedQueue.prototype.waitForProcessing = function(cb) {
  var pend = new Pend();
  for (var id in this.processingSet) {
    var queueItem = this.processingSet[id];
    pend.go(makePendFn(queueItem));
  }
  pend.wait(cb);

  function makePendFn(queueItem) {
    return function(cb) {
      queueItem.cbs.push(cb);
    };
  }
};

DedupedQueue.prototype.flush = function() {
  if (this.paused) return;
  // if an item cannot go into the processing queue because an item with the
  // same ID is already there, it goes into deferred
  var deferred = [];
  while (this.processingCount < this.maxAsync && this.pendingQueue.length > 0) {
    var queueItem = this.pendingQueue.shift();
    if (this.processingSet[queueItem.id]) {
      deferred.push(queueItem);
    } else {
      delete this.pendingSet[queueItem.id];
      this.processingSet[queueItem.id] = queueItem;
      this.processingCount += 1;
      this.startOne(queueItem);
    }
  }
  for (var i = 0; i < deferred.length; i += 1) {
    this.pendingQueue.push(deferred[i]);
  }
};

DedupedQueue.prototype.startOne = function(queueItem) {
  var self = this;
  var callbackCalled = false;
  self.processOne(queueItem.item, function(err) {
    if (callbackCalled) {
      self.emit('error', new Error("callback called more than once"));
      return;
    }
    callbackCalled = true;

    delete self.processingSet[queueItem.id];
    self.processingCount -= 1;
    if (queueItem.cbs.length === 0) {
      defaultCb(err);
    } else {
      for (var i = 0; i < queueItem.cbs.length; i += 1) {
        queueItem.cbs[i](err);
      }
    }
    self.flush();

    function defaultCb(err) {
      if (err) self.emit('error', err);
      self.emit('oneEnd');
    }
  });
};

DedupedQueue.prototype.setPause = function(isPaused) {
  this.paused = !!isPaused;
  this.flush();
};

function QueueItem(id, item) {
  this.id = id;
  this.item = item;
  this.cbs = [];
}
