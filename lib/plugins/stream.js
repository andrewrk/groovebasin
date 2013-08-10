var Plugin, Stream;
Plugin = require('../plugin');
module.exports = Stream = (function(superclass){
  Stream.displayName = 'Stream';
  var prototype = extend$(Stream, superclass).prototype, constructor = Stream;
  function Stream(bus){
    var this$ = this instanceof ctor$ ? this : new ctor$;
    superclass.apply(this$, arguments);
    this$.port = null;
    this$.format = null;
    bus.on('save_state', function(state){
      state.status.stream_httpd_port = this$.port;
      state.status.stream_httpd_format = this$.format;
    });
    bus.on('restore_state', function(state){
      var ref$;
      ref$ = state.mpd_conf.audio_httpd, this$.port = ref$.port, this$.format = ref$.format;
    });
    return this$;
  } function ctor$(){} ctor$.prototype = prototype;
  return Stream;
}(Plugin));
function extend$(sub, sup){
  function fun(){} fun.prototype = (sub.superclass = sup).prototype;
  (sub.prototype = new fun).constructor = sub;
  if (typeof sup.extended == 'function') sup.extended(sub);
  return sub;
}