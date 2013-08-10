var fs, path, Plugin;
fs = require('fs');
path = require('path');
Plugin = require('../plugin');
module.exports = (function(superclass){
  exports.displayName = 'exports';
  var prototype = extend$(exports, superclass).prototype, constructor = exports;
  function exports(bus){
    var this$ = this instanceof ctor$ ? this : new ctor$;
    superclass.apply(this$, arguments);
    bus.on('save_state', bind$(this$, 'saveState'));
    bus.on('mpd', function(mpd){
      this$.mpd = mpd;
    });
    bus.on('socket_connect', bind$(this$, 'onSocketConnection'));
    bus.on('restore_state', function(state){
      if ((this$.music_directory = state.mpd_conf.music_directory) == null) {
        this$.is_enabled = false;
        console.warn("No music directory set. Delete disabled.");
        return;
      }
    });
    return this$;
  } function ctor$(){} ctor$.prototype = prototype;
  prototype.saveState = function(state){
    state.status.delete_enabled = this.is_enabled;
  };
  prototype.onSocketConnection = function(client){
    var this$ = this;
    client.on('DeleteFromLibrary', function(data){
      var files, file;
      if (!client.permissions.admin) {
        console.warn("User without admin permission trying to delete songs");
        return;
      }
      files = JSON.parse(data);
      file = null;
      function next(err){
        var file;
        if (err) {
          console.error("deleting " + file + ": " + err.stack);
        } else if (file != null) {
          console.info("deleted " + file);
        }
        if ((file = files.shift()) == null) {
          this$.mpd.scanFiles(files);
        } else {
          fs.unlink(path.join(this$.music_directory, file), next);
        }
      }
      next();
    });
  };
  return exports;
}(Plugin));
function extend$(sub, sup){
  function fun(){} fun.prototype = (sub.superclass = sup).prototype;
  (sub.prototype = new fun).constructor = sub;
  if (typeof sup.extended == 'function') sup.extended(sub);
  return sub;
}
function bind$(obj, key){
  return function(){ return obj[key].apply(obj, arguments) };
}