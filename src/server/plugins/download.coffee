Plugin = require('../plugin').Plugin
fs = require 'fs'
zipstream = require 'zipstream'

exports.Plugin = class Download extends Plugin
  constructor: ->
    super
    @is_enabled = false

  saveState: (state) =>
    state.status.download_enabled = @is_enabled

  setConf: (conf, conf_path) =>
    @is_enabled = true

    unless conf.music_directory?
      @is_enabled = false
      @log.warn "music_directory not found in #{conf_path}. Download disabled."
      return

    # set up library link
    library_link = "./public/library"
    try fs.unlinkSync library_link
    try
      fs.symlinkSync conf.music_directory, library_link
    catch error
      @is_enabled = false
      @log.warn "Unable to link public/library to #{conf.music_directory}: #{error}. Download disabled."
      return
    try
      fs.readdirSync library_link
    catch error
      @is_enabled = false
      @log.warn "Unable to access music directory: #{error}. Download disabled."
      return

  setUpRoutes: (app) =>
    app.get '/library/', (req, res) =>
      @downloadPath "", "library.zip", res
    app.get /^\/library\/(.*)\/$/, (req, res) =>
      path = req.params[0]
      relative_path = "/" + path
      zip_name = windowsSafePath(path.replace(/\//g, " - ")) + ".zip"
      @downloadPath relative_path, zip_name, res

  downloadPath: (relative_path, zip_name, response) =>
    @log.debug "request to download a library directory: #{relative_path}"

    prefix = "./public/library"
    walk prefix + relative_path, (err, files) ->
      if err
        response.writeHead 404, {}
        response.end()
        return
      response.writeHead 200,
        "Content-Type": "application/zip"
        "Content-Disposition": "attachment; filename=#{zip_name}"
      zip = zipstream.createZip {}
      zip.pipe response
      i = 0
      nextFile = ->
        file_path = files[i++]
        if file_path?
          options =
            "name": file_path.substr prefix.length + 1
            "store": true
          zip.addFile fs.createReadStream(file_path), options, nextFile
        else
          zip.finalize ->
            response.end()
      nextFile()

# translated from http://stackoverflow.com/a/5827895/367916
walk = (dir, done) ->
  results = []
  fs.readdir dir, (err, list) ->
    return done(err) if err?
    i = 0
    next = ->
      file = list[i++]
      return done(null, results) unless file?
      file = dir + '/' + file
      fs.stat file, (err, stat) ->
        if stat?.isDirectory()
          walk file, (err, res) ->
            results = results.concat res
            next()
        else
          results.push file
          next()
    next()

windowsSafePath = (string) ->
  # http://msdn.microsoft.com/en-us/library/windows/desktop/aa365247%28v=vs.85%29.aspx
  # this is a good start
  string.replace /<|>|:|"|\/|\\|\||\?|\*/g, "_"
