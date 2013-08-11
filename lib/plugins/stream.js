var Plugin = require('../plugin');
var util = require('util');

module.exports = Stream;

util.inherits(Stream, Plugin);
function Stream(bus) {
  var self = this;
  Plugin.call(self);
  self.port = null;
  self.format = null;
  bus.on('save_state', function(state){
    state.status.stream_httpd_port = self.port;
    state.status.stream_httpd_format = self.format;
  });
  bus.on('restore_state', function(state){
    var ref$;
    ref$ = state.mpd_conf.audio_httpd, self.port = ref$.port, self.format = ref$.format;
  });
}
