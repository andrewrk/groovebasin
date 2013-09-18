var LastFmNode = require('lastfm').LastFmNode;

module.exports = LastFm;

function LastFm(bus) {
  var self = this;
  self.previous_now_playing_id = null;
  self.last_playing_item = null;
  self.playing_start = new Date();
  self.playing_time = 0;
  self.previous_play_state = null;


  console.error("TODO: fix last.fm plugin (disabled)");
  return;
  setTimeout(bind$(self, 'flushScrobbleQueue'), 120000);
  bus.on('save_state', bind$(self, 'saveState'));
  bus.on('restore_state', bind$(self, 'restoreState'));
  bus.on('socket_connect', bind$(self, 'onSocketConnection'));
}
LastFm.prototype.restoreState = function(state){
  var ref$;
  this.scrobblers = (ref$ = state.lastfm_scrobblers) != null ? ref$ : {};
  this.scrobbles = (ref$ = state.scrobbles) != null ? ref$ : [];
  this.api_key = (ref$ = state.lastfm_api_key) != null ? ref$ : "7d831eff492e6de5be8abb736882c44d";
  this.api_secret = (ref$ = state.lastfm_secret) != null ? ref$ : "8713e8e893c5264608e584a232dd10a0";
  this.lastfm = new LastFmNode({
    api_key: this.api_key,
    secret: this.api_secret
  });
};
LastFm.prototype.saveState = function(state){
  state.lastfm_scrobblers = this.scrobblers;
  state.scrobbles = this.scrobbles;
  state.status.lastfm_api_key = this.api_key;
  state.lastfm_secret = this.api_secret;
};
LastFm.prototype.onSocketConnection = function(socket){
  var self = this;
  socket.on('LastfmGetSession', function(data){
    self.lastfm.request("auth.getSession", {
      token: data,
      handlers: {
        success: function(data){
          var ref$;
          delete self.scrobblers[data != null ? (ref$ = data.session) != null ? ref$.name : void 8 : void 8];
          socket.emit('LastfmGetSessionSuccess', JSON.stringify(data));
        },
        error: function(error){
          console.error("error from last.fm auth.getSession: " + error.message);
          socket.emit('LastfmGetSessionError', JSON.stringify(error));
        }
      }
    });
  });
  socket.on('LastfmScrobblersAdd', function(data){
    var params;
    params = JSON.parse(data);
    if (self.scrobblers[params.username] != null) {
      return;
    }
    self.scrobblers[params.username] = params.session_key;
    self.emit('state_changed');
  });
  socket.on('LastfmScrobblersRemove', function(data){
    var params, session_key;
    params = JSON.parse(data);
    session_key = self.scrobblers[params.username];
    if (session_key === params.session_key) {
      delete self.scrobblers[params.username];
      self.emit('state_changed');
    } else {
      console.warn("Invalid session key from user trying to remove scrobbler: " + params.username);
    }
  });
};
LastFm.prototype.flushScrobbleQueue = function(){
  var max_simultaneous, count, params, self = this;
  max_simultaneous = 10;
  count = 0;
  while ((params = this.scrobbles.shift()) != null && count++ < max_simultaneous) {
    console.info("scrobbling " + params.track + " for session " + params.sk);
    params.handlers = {
      error: fn$
    };
    this.lastfm.request('track.scrobble', params);
  }
  this.emit('state_changed');
  function fn$(error){
    console.error("error from last.fm track.scrobble: " + error.message);
    if ((error != null ? error.code : void 8) == null || error.code === 11 || error.code === 16) {
      self.scrobbles.push(params);
      self.emit('state_changed');
    }
  }
};
LastFm.prototype.queueScrobble = function(params){
  this.scrobbles.push(params);
  this.emit('state_changed');
};
LastFm.prototype.checkTrackNumber = function(trackNumber){
  if (parseInt(trackNumber, 10) >= 0) {
    return trackNumber;
  } else {
    return "";
  }
};
LastFm.prototype.checkScrobble = function(){
  // TODO: implement
};
LastFm.prototype.updateNowPlaying = function(){
  // TODO: implement
};
function bind$(obj, key){
  return function(){ return obj[key].apply(obj, arguments) };
}
