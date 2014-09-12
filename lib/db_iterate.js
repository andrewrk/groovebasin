var log = require('./log');

module.exports = dbIterate;

function dbIterate(db, keyPrefix, processOne, cb) {
  var it = db.iterator({
    gte: keyPrefix,
    keyAsBuffer: false,
    valueAsBuffer: false,
  });
  itOne();
  function itOne() {
    it.next(function(err, key, value) {
      if (err) {
        it.end(onItEndErr);
        cb(err);
        return;
      }
      if (!key || key.indexOf(keyPrefix) !== 0) {
        it.end(cb);
      } else {
        processOne(key, value);
        itOne();
      }
    });
  }
}

function onItEndErr(err) {
  if (err) {
    log.error("Unable to close iterator:", err.stack);
  }
}

