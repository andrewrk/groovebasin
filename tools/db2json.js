// This script converts an old groovebasin database to a JSON file so that it can be
// used in the zig rewrite of groovebasin.
//
// Instructions:
// 1. drop db2json.js into your old groovebasin instance
// 2. stop your old groovebasin instance so that it does not write to the db while
// you are dumping its data
// 3. `node db2json.js` (use the same node.js version as groovebasin is using,
// and it also wants to use the same leveldown dependency you already have
// installed). It hard-codes the input as `groovebasin.db`.
// 4. `groovebasin.db.json` is created which is 100% of the information from
// the database, in JSON format. It is one giant map of every key-value pair.

var fs = require('fs');
var path = require('path');
var leveldown = require('leveldown');

var defaultConfig = {
  dbPath: "groovebasin.db",
};

main();

function main() {
  var dbFilePath = "groovebasin.db";
  var db = leveldown(dbFilePath);
  db.open(function(err) {
    if (err) throw err;

    dbIterate(db, "", processOne, allDone);

    var map = {};

    function processOne(key, value) {
      map[key] = value;
    }

    function allDone(err) {
      if (err) throw err;
      fs.writeFile("groovebasin.db.json", JSON.stringify(map), function(err) {
        if (err) throw err;
        process.exit(0);
      });
    }

    function deserializeUser(payload) {
      return JSON.parse(payload);
    }
  });
}

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
  if (err) throw err;
}
