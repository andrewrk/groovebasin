var fs = require("fs");
var path = require("path");
var browserify = require('browserify');
var util = require("util");
var mkdirp = require("mkdirp");
var spawn = require("child_process").spawn;

var appOut = 'public/app.js';

mkdirp('public', function(){
  var b = browserify(path.resolve('src/client/app.js'));
  var outStream = b.bundle();
  outStream.pipe(fs.createWriteStream(appOut));
  exec('stylus', ['-o', 'public/', '-c', '--include-css', 'src/client/styles']);
  exec('handlebars', ['-f', 'public/views.js', 'src/client/views/']);
  util.log("generated public/views.js");
});

function noop() {}

function exec(cmd, args, options, cb){
  args = args || [];
  options = options || {};
  cb = cb || noop;
  var opts = {
    stdio: 'inherit'
  };
  for (var k in options) {
    opts[k] = options[k];
  }
  var bin = spawn(cmd, args, opts);
  bin.on('exit', cb);
}
