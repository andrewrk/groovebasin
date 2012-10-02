Plugin = require('../plugin').Plugin
Mpd = require '../../mpd.js/lib/mpd'
formidable = require 'formidable'
util = require 'util'
mkdirp = require 'mkdirp'
fs = require 'fs'
path = require 'path'
request = require 'request'
url = require 'url'
temp = require 'temp'


bad_file_chars = {}
bad_file_chars[c] = "_" for c in '/\\?%*:|"<>'
fileEscape = (filename) ->
  out = ""
  for c in filename
    out += bad_file_chars[c] ? c
  out
zfill = (n) -> (if n < 10 then "0" else "") + n
getSuggestedPath = (track, default_name=Mpd.trackNameFromFile(track.file)) ->
  _path = ""
  _path += "#{fileEscape track.album_artist_name}/" if track.album_artist_name
  _path += "#{fileEscape track.album_name}/" if track.album_name
  _path += "#{fileEscape zfill track.track} " if track.track
  ext = path.extname(track.file)
  if track.name is Mpd.trackNameFromFile(track.file)
    _path += fileEscape default_name
  else
    _path += fileEscape track.name
    _path += ext
  return _path
stripFilename = (_path) ->
  parts = _path.split('/')
  parts[0...parts.length-1].join('/')

exports.Plugin = class Upload extends Plugin
  constructor: ->
    super
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
    else
      @is_enabled = false
      @log.warn "music directory not found in #{conf_path}. Uploading disabled."

  setMpd: (@mpd) =>
    @mpd.on 'libraryupdate', @flushWantToQueue

  onSocketConnection: (socket) =>
    socket.on 'ImportTrackUrl', (data) =>
      url_string = data.toString()
      parsed_url = url.parse(data.toString())
      remote_filename = path.basename(parsed_url.pathname) + path.extname(parsed_url.pathname)
      temp_file = temp.path()
      cleanUp = =>
        fs.unlink(temp_file)
      cleanAndLogIfErr = (err) =>
        if err
          @log.error "Unable to import by URL. Error:", err, "URL:", url_string
        cleanUp()
      pipe = request(url_string).pipe(fs.createWriteStream(temp_file))
      pipe.on 'close', =>
        @importFile temp_file, remote_filename, cleanAndLogIfErr
      pipe.on 'error', cleanAndLogIfErr


  importFile: (temp_file, remote_filename, cb=->) =>
    tmp_with_ext = temp_file + path.extname(remote_filename)
    @moveFile temp_file, tmp_with_ext, (err) =>
      return cb(err) if err
      @mpd.getFileInfo "file://#{tmp_with_ext}", (err, track) =>
        return cb(err) if err
        suggested_path = getSuggestedPath(track, remote_filename)
        relative_path = path.join('incoming', suggested_path)
        dest = path.join(@music_lib_path, relative_path)
        mkdirp stripFilename(dest), (err) =>
          if err
            @log.error err
            return cb(err)
          @moveFile tmp_with_ext, dest, (err) =>
            @want_to_queue.push relative_path
            @onStateChanged()
            @log.info "Track was uploaded: #{dest}"
            cb(err)

  setUpRoutes: (app) =>
    app.post '/upload', (request, response) =>
      unless @is_enabled
        response.writeHead 500, {'content-type': 'text/plain'}
        response.end JSON.stringify {success: false, reason: "Uploads disabled"}
        return

      logErr = (err) => @log.error "Unable to import by uploading. Error: #{err}"
      logIfErr = (err) => if err then logIfErr(err)

      form = new formidable.IncomingForm()
      form.parse request, (err, fields, file) =>
        return logErr(err) if err
        @importFile file.qqfile.path, file.qqfile.filename, logIfErr

      response.writeHead 200, {'content-type': 'text/html'}
      response.end JSON.stringify {success: true}

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
    util.pump in_stream, out_stream, (err) =>
      if err
        @log.error error
        cb(err)
        return
      fs.unlink source, cb

