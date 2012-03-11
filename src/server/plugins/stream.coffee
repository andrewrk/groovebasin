Plugin = require('../plugin').Plugin
exports.Plugin = class Stream extends Plugin
  constructor: ->
    super()
    @port = null
    @format = null
    @is_enabled = false

  saveState: (state) =>
    state.status.stream_httpd_port = @port
    state.status.stream_httpd_format = @format

  setConf: (conf, conf_path) =>
    @is_enabled = true
    if (httpd = conf.audio_output?.httpd)?
      @port = httpd.port
      if httpd.encoder is 'lame'
        @format = 'mp3'
      else if httpd.encoder is 'vorbis'
        @format = 'oga'
      else
        @format = 'unknown'
    else
      @is_enabled = false
      @log.warn "httpd audio_output not enabled in #{conf_path}. Streaming disabled."
