var fs = require("fs");
var path = require("path");
var chokidar = require("chokidar");
var watchify = require("watchify");
var browserify = watchify.browserify;
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
      var watcher = chokidar.watch('src/client/views', {
        ignoreInitial: true,
        ignored: isDotFile,
        persistent: true,
      });
      watcher.on('change', generateViews);
      watcher.on('add', generateViews);
      watcher.on('unlink', generateViews);
      generateViews();
    } else {
      handlebars();
    }

    function generateViews() {
      handlebars();
      util.log("generated public/views.js");
    }

    function writeBundle() {
      var outStream = b.bundle();
      outStream.on('error', function(err) {
        util.log("error " + err.message);
      });
      outStream.on('close', function() {
        util.log("generated " + appOut);
      });
      outStream.pipe(fs.createWriteStream(appOut));
    }
  });
}

function watch(){
  build('w');
}

function isDotFile(fullPath) {
  var basename = path.basename(fullPath);
  return (/^\./).test(basename) || (/~$/).test(basename);
}
