fs = require("fs")
path = require("path")
watcher = require("watch")
util = require("util")
mkdirp = require("mkdirp")

{spawn} = require("child_process")
exec = (cmd, args=[], cb=->) ->
  bin = spawn(cmd, args)
  bin.stdout.on 'data', (data) ->
    process.stdout.write data
  bin.stderr.on 'data', (data) ->
    process.stderr.write data
  bin.on 'exit', cb


handlebars = ->
  exec 'handlebars', ['-f', 'public/views.js', 'src/client/views/']

build = (watch)->
  mkdirp 'public', ->
    args = if watch then ['-w'] else []
    exec 'coffee', args.concat ['-cbo', 'lib/', 'src/server/']
    exec 'coffee', args.concat ['-cbo', 'lib/', 'src/shared/']
    exec 'jspackage', args.concat [
      '-l', 'src/shared/',
      '-l', 'src/public/vendor',
      'src/client/app', 'public/app.js'
    ]
    exec 'stylus', args.concat ['-o', 'public/', 'src/client/']

    # fuck you handlebars
    if watch
      watcher.watchTree 'src/client/views', ignoreDotFiles: true, ->
        handlebars()
        util.log "generated public/views.js"
    else
      handlebars()

watch = -> build('w')

task "watch", watch

task "build", build

task "clean", ->
  exec "rm", ['-rf', './public', './lib']

task "dev", ->
  watch()
  runServer = -> exec "node-dev", ["lib/server.js"]
  setTimeout runServer, 1000
