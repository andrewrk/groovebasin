var _exports, bad_char_re;
_exports = typeof exports != 'undefined' && exports !== null
  ? exports
  : window.Util = {};
_exports.schedule = function(delay, func){
  return setInterval(func, delay * 1000);
};
_exports.wait = function(delay, func){
  return setTimeout(func, delay * 1000);
};
_exports.shuffle = function(array){
  var top, current, tmp;
  top = array.length;
  while (--top > 0) {
    current = Math.floor(Math.random() * (top + 1));
    tmp = array[current];
    array[current] = array[top];
    array[top] = tmp;
  }
};
_exports.formatTime = function(seconds){
  var minutes, hours, zfill;
  seconds = Math.floor(seconds);
  minutes = Math.floor(seconds / 60);
  seconds -= minutes * 60;
  hours = Math.floor(minutes / 60);
  minutes -= hours * 60;
  zfill = function(n){
    if (n < 10) {
      return "0" + n;
    } else {
      return n + "";
    }
  };
  if (hours !== 0) {
    return hours + ":" + zfill(minutes) + ":" + zfill(seconds);
  } else {
    return minutes + ":" + zfill(seconds);
  }
};
bad_char_re = new RegExp('[^a-zA-Z0-9-]', 'gm');
_exports.toHtmlId = function(string){
  return string.replace(bad_char_re, function(c){
    return "_" + c.charCodeAt(0) + "_";
  });
};
_exports.compareArrays = function(arr1, arr2){
  var i1, len$, val1, val2, diff;
  for (i1 = 0, len$ = arr1.length; i1 < len$; ++i1) {
    val1 = arr1[i1];
    val2 = arr2[i1];
    diff = (val1 != null
      ? val1
      : -1) - (val2 != null
      ? val2
      : -1);
    if (diff !== 0) {
      return diff;
    }
  }
  return 0;
};
_exports.parseQuery = function(query){
  var obj, i$, ref$, valset, len$, ref1$, param, val;
  obj = {};
  if (query == null) {
    return obj;
  }
  for (i$ = 0, len$ = (ref$ = (fn$())).length; i$ < len$; ++i$) {
    ref1$ = ref$[i$], param = ref1$[0], val = ref1$[1];
    obj[unescape(param)] = unescape(val);
  }
  return obj;
  function fn$(){
    var i$, ref$, len$, results$ = [];
    for (i$ = 0, len$ = (ref$ = query.split('&')).length; i$ < len$; ++i$) {
      valset = ref$[i$];
      results$.push(valset.split('='));
    }
    return results$;
  }
};
