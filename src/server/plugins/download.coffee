Plugin = require('../plugin')
fs = require 'fs'
zipstream = require 'zipstream'
path = require 'path'
{safePath} = require '../futils'

module.exports = class Download extends Plugin
  constructor: (bus) ->
    super
    @is_enabled = false
    @is_ready = false # not until we set up library link

    bus.on 'app', @setUpRoutes
    bus.on 'save_state', @saveState
    bus.on 'mpd_conf', @setConf

  saveState: (state) =>
    state.status.download_enabled = @is_enabled

  setConf: (conf, conf_path) =>
    @is_enabled = true

    unless conf.music_directory?
      @is_enabled = false
      console.warn "music_directory not found in #{conf_path}. Download disabled."
      return

    # set up library link
    library_link = "./public/library"
    fs.unlink library_link, (err) =>
      # ignore this error. we'll pay attention to the link one.
      fs.symlink conf.music_directory, library_link, (error) =>
        if error
          @is_enabled = false
          console.warn "Unable to link public/library to #{conf.music_directory}: #{error}. Download disabled."
          @emit 'ready'
          return
        fs.readdir library_link, (error) =>
          if error
            @is_enabled = false
            console.warn "Unable to access music directory: #{error}. Download disabled."
            @emit 'ready'
            return
          @emit 'ready'

  setUpRoutes: (app) =>
    app.get '/library/', @checkEnabledMiddleware, (req, res) =>
      @downloadPath "", "library.zip", res
    app.get /^\/library\/(.*)\/$/, @checkEnabledMiddleware, (req, res) =>
      path = req.params[0]
      relative_path = "/" + path
      zip_name = safePath(path.replace(/\//g, " - ")) + ".zip"
      @downloadPath relative_path, zip_name, res

  downloadPath: (relative_path, zip_name, response) =>
    prefix = "./public/library"
    walk path.join(prefix, relative_path), (err, files) ->
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
    return done(err) if err
    i = 0
    next = ->
      file = list[i++]
      return done(null, results) unless file?
      file = dir + '/' + file
      fs.stat file, (err, stat) ->
        return done(err) if err
        if stat.isDirectory()
          walk file, (err, res) ->
            return done(err) if err
            results = results.concat res
            next()
        else
          results.push file
          next()
    next()
