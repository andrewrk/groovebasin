appjs=public/app.js
appcss=public/app.css
serverjs=server.js
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
SHELL=bash

build: $(serverjs) $(appjs) $(appcss)

$(serverjs): $(server_src) $(mpd_lib) $(lib)/mpdconf.js
	$(coffee) -p -c $(server_src) >$@.tmp
	chmod +x $@.tmp
	mv $@{.tmp,}

$(lib):
	mkdir -p $(lib)

$(mpd_lib): $(mpd_lib_src) | $(lib)
	$(coffee) -p -c $(mpd_lib_src) >$@.tmp
	mv $@{.tmp,}

$(lib)/mpdconf.js: src/mpdconf.coffee | $(lib)
	$(coffee) -p -c src/mpdconf.coffee >$@.tmp
	mv $@{.tmp,}

$(appjs): $(views) $(client_src)
	$(coffee) -p -c $(client_src) >$@.tmp
	$(handlebars) $(views) -k if -k each -k hash >>$@.tmp
	mv $@{.tmp,}

$(appcss): $(styles)
	$(stylus) <$(styles) >$@.tmp
	mv $@{.tmp,}

clean:
	rm -f ./$(appjs){,.tmp}
	rm -f ./$(appcss){,.tmp}
	rm -f ./$(serverjs){,.tmp}
	rm -rf ./$(lib)
	rm -f ./public/library

watch:
	bash -c 'while [ 1 ]; do make --no-print-directory; sleep 0.5; done'
