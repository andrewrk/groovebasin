module.exports = Stream;

function Stream(gb) {
  this.gb = gb;
  this.port = null;
  this.format = null;
  setup(this);
}

function setup(self) {
  self.gb.on('aboutToSaveState', function(state) {
    state.status.stream_httpd_port = self.port;
    state.status.stream_httpd_format = self.format;
  });
  self.gb.on('stateRestored', function(state) {
    var ahttpd = state.mpd_conf.audio_httpd;
    self.port = ahttpd.port;
    self.format = ahttpd.format;
  });
}
