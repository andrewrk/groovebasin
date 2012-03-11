Plugin = require('../plugin').Plugin
exports.Plugin = class Stream extends Plugin
  constructor: ->
    super()
    @stream_httpd_port = null
    @is_enabled = false

  saveState: (state) =>
    state.status.stream_httpd_port = @stream_httpd_port

  setConf: (conf, conf_path) =>
    @is_enabled = true
    unless (@stream_httpd_port = conf.audio_output?.httpd?.port)?
      @is_enabled = false
      @log.warn "httpd audio_output not enabled in #{conf_path}. Streaming disabled."
