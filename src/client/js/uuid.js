// all these characters are safe to put in an HTML id
var b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
var crypto = window.crypto;
var arr = new Uint8Array(24);
module.exports = uuid;
function uuid() {
  crypto.getRandomValues(arr);
  var s = "";
  for (var m = 0, t = 0; t < arr.length; m = (m + 1) % 4) {
    var x;
    if (m === 0) {
      x = arr[t] >> 2;
      t += 1;
    } else if (m === 1) {
      x = ((0x3 & arr[t-1]) << 4) | (arr[t] >> 4);
    } else if (m === 2) {
      x = ((0xf & arr[t]) << 2) | (arr[t+1] >> 6);
      t += 1;
    } else { // m === 3
      x = arr[t] & 0x3f;
      t += 1;
    }
    s += b64[x];
  }
  return s;
}
