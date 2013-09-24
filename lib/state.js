#!/usr/bin/env node

/* this command line utility displays or updates information from the db */

var getDb = require('./db');
var db = getDb(process.env.DB_PATH);

var args = process.argv.slice(2);

if (args.length === 0) {
  dump();
} else {
  processArgs();
}

function processArgs() {
  var openThings = 0;
  var putFlag = false;
  var putKey = null;
  var delFlag = false;
  var getFlag = false;
  process.argv.slice(2).forEach(function(arg) {
    if (getFlag) {
      get(arg);
      getFlag = false;
    } else if (delFlag) {
      del(arg);
      delFlag = false;
    } else if (putKey != null) {
      put(putKey, arg);
      putFlag = false;
    } else if (putFlag) {
      putKey = arg;
    } else if (arg === '--get') {
      getFlag = true;
    } else if (arg === '--put') {
      putFlag = true;
    } else if (arg === '--del') {
      delFlag = true;
    } else {
      console.error("Unexpected argument:", arg);
      process.exit(1);
    }
  });

  function get(key) {
    openAThing();
    db.get(key, function(err, val) {
      closeAThing();
      if (err) {
        console.error("Error getting", key, err.stack);
        return;
      }
      console.log(key, "=", val);
    });
  }
  function put(key, val) {
    openAThing();
    db.put(key, val, function(err) {
      closeAThing();
      if (err) {
        console.error("Error putting", key, err.stack);
        return;
      }
      console.log("put", key, "=", val);
    });
  }
  function del(key) {
    openAThing();
    db.del(key, function(err, val) {
      closeAThing();
      if (err) {
        console.error("Error deleting", key, err.stack);
        return;
      }
      console.log("del", key);
    });
  }

  function openAThing() {
    openThings += 1;
  }

  function closeAThing() {
    openThings -= 1;
    if (openThings === 0) {
      db.close();
    }
  }
}

function dump() {
  var stream = db.createReadStream();
  stream.on('data', function(data) {
    console.log(data.key, "=", data.value);
  });
  stream.on('error', function(err) {
    console.error(err.stack);
  });
  stream.on('close', function() {
    db.close();
  });
}

