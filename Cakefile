fs = require("fs")
path = require("path")

# returns a list of all files in the folder and subfolders
walk = (start, test) ->
  results = []
  processDir = (dir) ->
    names = fs.readdirSync(dir)
    for name in names
      file_path = "#{dir}/#{name}"
      stat = fs.statSync(file_path)
      if stat.isDirectory()
        processDir file_path
      else
        results.push file_path
  processDir start
  results


# explicit list of client src files, in dependency order
client_src_files = [
  "src/client/util.coffee"
  "src/shared/mpd.coffee"
  "src/client/socketmpd.coffee"
  "src/client/app.coffee"
]

makeMakefile = (o) ->
  """
  # input
  client_src=#{o.client_src_files}
  server_src=src/server/server.coffee
  styles=src/client/app.styl

  # output
  appjs=public/app.js
  appcss=public/app.css
  
  # compilers
  coffee=./node_modules/coffee-script/bin/coffee
  handlebars=./node_modules/handlebars/bin/handlebars
  stylus=./node_modules/stylus/bin/stylus

  .PHONY: build clean watch
  SHELL=bash

  build: $(serverjs) $(appjs) $(appcss) #{o.server_js_files}
  \t@: # suppress "Nothing to be done" message.

  #{o.server_js_rules}

  $(appjs): #{o.view_files} #{o.client_src_files}
  \t$(handlebars) #{o.view_files} >$@.tmp
  \tfor f in $(client_src); do $(coffee) -p -c $$f >>$@.tmp; done
  \tmv $@{.tmp,}

  $(appcss): $(styles)
  \t$(stylus) <$(styles) >$@.tmp
  \tmv $@{.tmp,}

  clean:
  \trm -f ./$(appjs){,.tmp}
  \trm -f ./$(appcss){,.tmp}
  \trm -rf ./lib
  \trm -f ./public/library
  \trm -f ./Makefile
  """

makeJsRule = (src, dest) ->
  """
  #{dest}: #{src}
  \tmkdir -p #{path.dirname(dest)}
  \t$(coffee) -cbj #{dest} #{src}

  """

{spawn} = require("child_process")
exec = (cmd, args=[], cb=->) ->
  bin = spawn(cmd, args)
  bin.stdout.on 'data', (data) ->
    process.stdout.write data
  bin.stderr.on 'data', (data) ->
    process.stderr.write data
  bin.on 'exit', cb

changeExtension = (filename, new_ext) ->
  ext = path.extname(filename)
  new_path = filename.substring(0, filename.length - ext.length)
  new_path + new_ext

configure = ->
  js_rules = []
  js_files = []
  for src in walk("./src/server").concat(walk("./src/shared"))
    if /\.coffee$/.test(src)
      dest = changeExtension(src, ".js").replace("./src/server/", "./lib/").replace("./src/shared/", "./lib/")
      js_rules.push(makeJsRule(src, dest))
      js_files.push(dest)

  view_files = (f for f in walk("./src/client/views") when /\.handlebars$/.test(f))

  makefile = makeMakefile
    view_files: view_files.join(" ")
    server_js_rules: js_rules.join("\n")
    server_js_files: js_files.join(" ")
    client_src_files: client_src_files.join(" ")

  fs.writeFileSync "./Makefile", makefile, 'utf8'

build = -> exec "make"
clean = -> exec "make", ["clean"]

task "build", ->
  configure()
  build()

task "clean", ->
  configure()
  clean()

task "configure", ->
  configure()
