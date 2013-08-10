var path, mutagen, walk, EventEmitter, trackNameFromFile, Library;
path = require('path');
mutagen = require('mutagen');
walk = require('walk');
EventEmitter = require('events').EventEmitter;
trackNameFromFile = require('./futils').trackNameFromFile;
module.exports = Library = (function(superclass){
  Library.displayName = 'Library';
  var prototype = extend$(Library, superclass).prototype, constructor = Library;
  function Library(bus){
    var this$ = this instanceof ctor$ ? this : new ctor$;
    this$.bus = bus;
    this$.bus.on('save_state', bind$(this$, 'onSaveState'));
    return this$;
  } function ctor$(){} ctor$.prototype = prototype;
  prototype.onSaveState = function(state){
    this.music_lib_path = state.mpd_conf.music_directory;
    this.startScan();
  };
  constructor.parseMaybeUndefNumber = function(n){
    n = parseInt(n, 10);
    if (isNaN(n)) {
      n = null;
    }
    return n;
  };
  prototype.get_library = function(cb){
    if (this.scan_complete) {
      cb(this.library);
    } else {
      this.once('library', cb);
    }
  };
  prototype.startScan = function(){
    var start_time, files, walker, this$ = this;
    if (this.library != null) {
      return;
    }
    this.library = {};
    console.log('starting library scan');
    start_time = new Date().getTime();
    files = [];
    walker = walk.walk(this.music_lib_path, {
      followLinks: false
    });
    walker.on('file', function(root, fileStats, next){
      files.push(path.join(root, fileStats.name));
      next();
    });
    walker.on('end', function(){
      mutagen.read(files, function(err, tagses){
        var i, ref$, len$, file, tags, good, key, local_file, ref1$, ref2$;
        if (err) {
          console.log(err.stderr);
          return;
        }
        for (i = 0, len$ = (ref$ = files).length; i < len$; ++i) {
          file = ref$[i];
          tags = tagses[i];
          good = false;
          for (key in tags) {
            good = true;
            break;
          }
          if (!good) {
            continue;
          }
          local_file = file.substr(this$.music_lib_path.length + 1);
          this$.library[local_file] = {
            file: local_file,
            name: ((ref1$ = tags.title) != null ? (ref2$ = ref1$[0]) != null ? ref2$.trim() : void 8 : void 8) || trackNameFromFile(file),
            artist_name: ((ref1$ = tags.artist) != null ? (ref2$ = ref1$[0]) != null ? ref2$.trim() : void 8 : void 8) || "",
            artist_disambiguation: "",
            album_artist_name: ((ref1$ = tags.artist) != null ? (ref2$ = ref1$[0]) != null ? ref2$.trim() : void 8 : void 8) || "",
            album_name: ((ref1$ = tags.album) != null ? (ref2$ = ref1$[0]) != null ? ref2$.trim() : void 8 : void 8) || "",
            track: constructor.parseMaybeUndefNumber((ref1$ = tags.tracknumber) != null ? ref1$[0] : void 8),
            time: parseInt(tags['info/length'], 10),
            year: constructor.parseMaybeUndefNumber((ref1$ = tags.date) != null ? ref1$[0] : void 8)
          };
        }
        console.log("library scan complete. " + Math.round(new Date().getTime() - start_time) + "ms.");
        this$.scan_complete = true;
        this$.emit('library', this$.library);
      });
    });
  };
  return Library;
}(EventEmitter));
function extend$(sub, sup){
  function fun(){} fun.prototype = (sub.superclass = sup).prototype;
  (sub.prototype = new fun).constructor = sub;
  if (typeof sup.extended == 'function') sup.extended(sub);
  return sub;
}
function bind$(obj, key){
  return function(){ return obj[key].apply(obj, arguments) };
}