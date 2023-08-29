// all these characters are safe to put in an HTML id
var base64_url_safe = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
module.exports = uuid;
function uuid() {
  var s = "";
  for (var i = 0; i < 8; i++) {
    s += base64_url_safe[Math.floor(Math.random() * base64_url_safe.length)];
  }
  return s;
}
