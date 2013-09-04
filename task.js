var fs = require("fs");
var path = require("path");
var watcher = require("watch");
var watchify = require("watchify");
var browserify = require("browserify");
var util = require("util");
var mkdirp = require("mkdirp");
var spawn = require("child_process").spawn;

var appOut = 'public/app.js';

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
  watch();
  exec("node-dev", ["lib/server.js"], {
    stdio: [process.stdin, process.stdout, process.stderr, 'ipc']
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
    opts[k] = options[k];
  }
  var bin = spawn(cmd, args, opts);
  bin.on('exit', cb);
}

function handlebars(){
  exec('handlebars', ['-f', 'public/views.js', 'src/client/views/']);
}

function build(watch){
  var npm_args = watch ? ['run', 'dev'] : ['run', 'build'];
  mkdirp('public', function(){
    var args = watch ? ['-w'] : [];
    var compile = watch ? watchify : browserify;
    var b = compile(path.resolve('src/client/app.js'));
    if (watch) b.on('update', writeBundle);
    writeBundle();
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

    function writeBundle() {
      util.log("generated " + appOut);
      b.bundle().pipe(fs.createWriteStream(appOut));
    }
  });
}

function watch(){
  build('w');
}
