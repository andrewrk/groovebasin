Plugin = require('../plugin').Plugin
exports.Plugin = class Stream extends Plugin
  constructor: ->
    super
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
        if httpd.quality?
          @log.warn "Use audio_output.bitrate for setting quality when using mp3 streaming in #{conf_path}"
      else if httpd.encoder is 'vorbis'
        @format = 'oga'
        if httpd.bitrate?
          @log.warn "Use audio_output.quality for setting quality when using vorbis streaming in #{conf_path}"
      else
        @format = 'unknown'
      if httpd.format isnt "44100:16:2"
        @log.warn "Recommended 44100:16:2 for audio_output.format in #{conf_path}"
    else
      @is_enabled = false
      @log.warn "httpd audio_output not enabled in #{conf_path}. Streaming disabled."
