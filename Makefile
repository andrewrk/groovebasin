appjs=public/app.js
appcss=public/app.css
serverjs=groovebasind
views=views/*.handlebars
client_src=src/mpd.coffee src/socketmpd.coffee src/app.coffee
server_src=src/daemon.coffee
styles=src/app.styl
lib=lib
mpd_lib=$(lib)/mpd.js
mpd_lib_src=src/mpd.coffee

coffee=node_modules/coffee-script/bin/coffee
handlebars=node_modules/handlebars/bin/handlebars
stylus=node_modules/stylus/bin/stylus

.PHONY: build clean watch

build: $(serverjs) $(appjs) $(appcss)

$(serverjs): $(server_src) $(mpd_lib) $(lib)/mpdconf.js
	echo "#!/usr/bin/env node" >$@
	$(coffee) -p -c $(server_src) >>$@
	chmod +x $@

$(mpd_lib): $(mpd_lib_src)
	mkdir -p $(lib)
	$(coffee) -p -c $(mpd_lib_src) >$@

lib/mpdconf.js: src/mpdconf.coffee
	mkdir -p $(lib)
	$(coffee) -p -c src/mpdconf.coffee >$@

$(appjs): $(views) $(client_src)
	$(coffee) -p -c $(client_src) >$@
	$(handlebars) $(views) -k if -k each -k hash >>$@

$(appcss): $(styles)
	$(stylus) <$(styles) >$@

clean:
	rm -f ./$(appjs)
	rm -f ./$(appcss)
	rm -f ./$(serverjs)
	rm -rf ./$(lib)

watch:
	bash -c 'set -e; while [ 1 ]; do make; sleep 0.5; done'
