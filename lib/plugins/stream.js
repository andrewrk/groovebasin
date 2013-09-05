module.exports = Stream;

function Stream(gb) {
  this.gb = gb;
  this.port = null;
  this.format = null;
  setup(this);
}

function setup(self) {
  self.gb.on('stateRestored', function(state) {
    var ahttpd = state.mpd_conf.audio_httpd;
    self.port = ahttpd.port;
    self.format = ahttpd.format;
  });
  self.gb.on('socketConnect', function(client) {
    client.emit('StreamInfo', {
      port: self.port,
      format: self.format,
    });
  });
}
