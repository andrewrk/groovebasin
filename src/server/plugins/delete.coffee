fs = require('fs')
path = require('path')
{Plugin} = require('../plugin')

# ability to delete songs from your library
exports.Plugin = class extends Plugin
  constructor: ->
    super

  saveState: (state) =>
    state.status.delete_enabled = @is_enabled

  onSocketConnection: (socket, getPermissions) =>
    socket.on 'DeleteFromLibrary', (data) =>
      if not getPermissions().admin
        @log.warn "User without admin permission trying to delete songs"
        return
      files = JSON.parse data.toString()
      file = null
      next = (err) =>
        if err
          @log.error "deleting #{file}: #{err.toString()}"
        else if file?
          @log.info "deleted #{file}"
        if not (file = files.shift())?
          @mpd.scanFiles files
        else # tail call recursion, bitch
          fs.unlink path.join(@music_lib_path, file), next
      next()

  setMpd: (@mpd) =>

  setConf: (conf, conf_path) =>
    if conf.music_directory?
      @music_lib_path = conf.music_directory
    else
      @is_enabled = false
      @log.warn "Delete disabled - music directory not found in #{conf_path}"
