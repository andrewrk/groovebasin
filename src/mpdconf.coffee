# parses an mpd conf file, returning an object with all the values
exports.parse = (file_contents) ->
  obj = {}
  stack = []
  audio_outputs = []
  for line in file_contents.split("\n")
    line = line.trim()
    continue if line.length == 0
    continue if line.substring(0, 1) == "#"

    if line.substring(0, 1) == "}"
      obj = stack.pop()
    else
      parts = line.match(/([^\s]*)\s+([^#]*)/)
      key = parts[1]
      val = parts[2]
      if val == "{"
        stack.push obj
        if key == 'audio_output'
          audio_outputs.push new_obj = {}
        else
          obj[key] = new_obj = {}
        obj = new_obj
      else
        obj[key] = JSON.parse(val)

  # arrange audio_outputs by type
  obj.audio_output = {}
  for audio_output in audio_outputs
    obj.audio_output[audio_output.type] = audio_output

  return obj
