Plugin = require('../plugin').Plugin
fs = require 'fs'

exports.Plugin = class Download extends Plugin
  constructor: ->
    super()
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

