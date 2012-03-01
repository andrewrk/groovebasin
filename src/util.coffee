exports = window.Util = {}

exports.schedule = (delay, func) -> window.setInterval(func, delay)
exports.wait = (delay, func) -> setTimeout func, delay

exports.shuffle = (array) ->
  top = array.length
  while --top > 0
    current = Math.floor(Math.random() * (top + 1))
    tmp = array[current]
    array[current] = array[top]
    array[top] = tmp

exports.formatTime = (seconds) ->
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
exports.toHtmlId = (string) ->
  out = ""
  for c in string
    if ok_id_chars[c]
      out += c
    else
      out += "_" + c.charCodeAt(0)
  return out
