// All these characters are safe to put in an HTML id.
var base64_url_safe = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
module.exports = randomId;
var crypto = window.crypto;
var len = 8; // 8 characters, 6 bits each. 48 bits of entropy.
var arr = new Uint8Array(len);
function randomId() {
  crypto.getRandomValues(arr);
  var s = "";
  for (var i = 0; i < len; i++) {
    s += base64_url_safe[arr[i] & 0x3f];
  }
  return s;
}
