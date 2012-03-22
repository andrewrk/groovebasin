Plugin = require('../plugin').Plugin
mpd = require '../mpd'
url = require 'url'
formidable = require 'formidable'
util = require 'util'
mkdirp = require 'mkdirp'
fs = require 'fs'


bad_file_chars = {}
bad_file_chars[c] = "_" for c in '/\\?%*:|"<>'
fileEscape = (filename) ->
  out = ""
  for c in filename
    out += bad_file_chars[c] ? c
  out
zfill = (n) ->
  if n < 10 then "0" + n else "" + n
getSuggestedPath = (track, default_name=mpd.trackNameFromFile(track.file)) ->
  path = ""
  path += "#{fileEscape track.album_artist_name}/" if track.album_artist_name
  path += "#{fileEscape track.album_name}/" if track.album_name
  path += "#{fileEscape zfill track.track} " if track.track
  ext = getExtension(track.file)
  if track.name is mpd.trackNameFromFile(track.file)
    path += fileEscape default_name
  else
    path += fileEscape track.name
    path += ext
  return path
getExtension = (filename) ->
  if (pos = filename.lastIndexOf('.')) is -1 then "" else "." + filename.substring(pos+1)
stripFilename = (path) ->
  parts = path.split('/')
  parts[0...parts.length-1].join('/')

exports.Plugin = class Upload extends Plugin
  constructor: ->
    super()
    @is_enabled = false
    @random_ids = null

  restoreState: (state) =>
    @want_to_queue = state.want_to_queue ? []

  saveState: (state) =>
    state.want_to_queue = @want_to_queue
    state.status.upload_enabled = @is_enabled

  setConf: (conf, conf_path) =>
    @is_enabled = true
    unless conf.bind_to_address?.unix_socket?
      @is_enabled = false
      @log.warn "bind_to_address does not have a unix socket enabled in #{conf_path}. Uploading disabled."
    unless conf.bind_to_address?.network == "localhost"
      @is_enabled = false
      @log.warn "bind_to_address does not have a definition that is 'localhost' in #{conf_path}. Uploading disabled."
    if conf.music_directory?
      @music_lib_path = conf.music_directory
      @music_lib_path += '/' if @music_lib_path.substring(@music_lib_path.length - 1, 1) isnt '/'
    else
      @is_enabled = false
      @log.warn "music directory not found in #{conf_path}. Uploading disabled."

  setMpd: (@mpd) =>
    @mpd.on 'libraryupdate', @flushWantToQueue

  handleRequest: (request, response) =>
    parsed_url = url.parse(request.url)
    return false unless parsed_url.pathname is '/upload' and request.method is 'POST'
    unless @is_enabled
      response.writeHead 500, {'content-type': 'text/plain'}
      response.end JSON.stringify {success: false, reason: "Uploads disabled"}
      return true

    form = new formidable.IncomingForm()
    form.parse request, (err, fields, file) =>
      tmp_with_ext = file.qqfile.path + getExtension(file.qqfile.filename)
      @moveFile file.qqfile.path, tmp_with_ext, =>
        @mpd.getFileInfo "file://#{tmp_with_ext}", (track) =>
          suggested_path = getSuggestedPath(track, file.qqfile.filename)
          dest = @music_lib_path + suggested_path
          mkdirp stripFilename(dest), (err) =>
            if err
              @log.error err
            else
              @moveFile tmp_with_ext, dest, =>
                @want_to_queue.push suggested_path
                @onStateChanged()
                @log.info "Track was uploaded: #{dest}"

    response.writeHead 200, {'content-type': 'text/html'}
    response.end JSON.stringify {success: true}
    return true

  onSendStatus: (status) =>
    @random_ids = status?.random_ids

  queueFilesPos: =>
    pos = @mpd.playlist.item_list.length
    return pos unless @random_ids?
    for item, i in @mpd.playlist.item_list
      return i if @random_ids[item.id]?

  flushWantToQueue: =>
    i = 0
    files = []
    while i < @want_to_queue.length
      file = @want_to_queue[i]
      if @mpd.library.track_table[file]?
        files.push file
        @want_to_queue.splice i, 1
      else
        i++
    @mpd.queueFiles files, @queueFilesPos()
    @onStateChanged() if files.length

  moveFile: (source, dest, cb=->) =>
    in_stream = fs.createReadStream(source)
    out_stream = fs.createWriteStream(dest)
    out_stream.on 'error', (error) => @log.error error
    util.pump in_stream, out_stream, -> fs.unlink source, cb

