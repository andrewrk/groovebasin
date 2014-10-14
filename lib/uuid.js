var crypto = require('crypto');
var htmlSafe = {'/': '_', '+': '-'};

// random string which is safe to put in an html id
module.exports = uuid;
uuid.len = len;

function uuid() {
  return len(24);
}

function len(size) {
  return rando(size).toString('base64').replace(/[\/\+]/g, function(x) {
    return htmlSafe[x];
  });
}

function rando(size) {
  try {
    return crypto.randomBytes(size);
  } catch (err) {
    return crypto.pseudoRandomBytes(size);
  }
}
