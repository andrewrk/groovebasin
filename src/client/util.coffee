_exports = exports ? window.Util = {}

_exports.schedule = (delay, func) -> window.setInterval(func, delay)
_exports.wait = (delay, func) -> setTimeout func, delay

_exports.shuffle = (array) ->
  top = array.length
  while --top > 0
    current = Math.floor(Math.random() * (top + 1))
    tmp = array[current]
    array[current] = array[top]
    array[top] = tmp

_exports.formatTime = (seconds) ->
  seconds = Math.floor seconds
  minutes = Math.floor seconds / 60
  seconds -= minutes * 60
  hours = Math.floor minutes / 60
  minutes -= hours * 60
  zfill = (n) ->
    if n < 10 then "0" + n else "" + n
  if hours != 0
    return "#{hours}:#{zfill minutes}:#{zfill seconds}"
  else
    return "#{minutes}:#{zfill seconds}"


# converts any string into an HTML id, guaranteed to be unique
ok_id_chars = {}
ok_id_chars[c] = true for c in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-"
_exports.toHtmlId = (string) ->
  out = ""
  for c in string
    if ok_id_chars[c]
      out += c
    else
      out += "_" + c.charCodeAt(0)
  return out

# compares 2 arrays with positive integers, returning > 0, 0, or < 0
_exports.compareArrays = (arr1, arr2) ->
  for val1, i1 in arr1
    val2 = arr2[i1]
    diff = (val1 ? -1) - (val2 ? -1)
    return diff if diff isnt 0
  return 0

_exports.parseQuery = (query) ->
  obj = {}
  return obj unless query?

  for [param, val] in (valset.split('=') for valset in query.split('&'))
    obj[unescape(param)] = unescape(val)
  
  return obj

