var fs = require("fs");
var path = require("path");
var watcher = require("watch");
var util = require("util");
var mkdirp = require("mkdirp");
var spawn = require("child_process").spawn;

var tasks = {
  watch: watch,
  build: build,
  clean: clean,
  dev: dev,
};

tasks[process.argv[2]]();

function clean() {
  exec("rm", ['-rf', './public', './lib']);
}

function dev() {
  mkdirp("lib", function(){
    exec('touch', ["lib/server.js"], null, function(){
      watch();
      exec("node-dev", ["lib/server.js"], {
        stdio: [process.stdin, process.stdout, process.stderr, 'ipc']
      });
    });
  });
}

function noop() {}

function exec(cmd, args, options, cb){
  args = args || [];
  options = options || {};
  cb = cb || noop;
  var opts = {
    stdio: 'inherit'
  };
  for (var k in options) {
    var v = options[k];
    opts[k] = v;
  }
  var bin = spawn(cmd, args, opts);
  bin.on('exit', cb);
}

function handlebars(){
  exec('handlebars', ['-f', 'public/views.js', 'src/client/views/']);
}

function build(watch){
  var npm_args;
  npm_args = watch ? ['run', 'dev'] : ['run', 'build'];
  mkdirp('public', function(){
    var args;
    args = watch ? ['-w'] : [];
    exec('jspackage', args.concat([
        '-l', 'src/public/vendor',
        '-l', 'public',
        'src/client/app', 'public/app.js']));
    exec('stylus', args.concat(['-o', 'public/', '-c', '--include-css', 'src/client/styles']));
    if (watch) {
      watcher.watchTree('src/client/views', {
        ignoreDotFiles: true
      }, function(){
        handlebars();
        util.log("generated public/views.js");
      });
    } else {
      handlebars();
    }
  });
}

function watch(){
  build('w');
}
