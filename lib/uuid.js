var crypto = require('crypto');
var htmlSafe = {'/': '_', '+': '-'};

module.exports = uuid;
function uuid() {
  // random string which is safe to put in an html id
  return rando(24).toString('base64').replace(/[\/\+]/g, function(x) {
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
