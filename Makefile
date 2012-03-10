# input
views=src/client/*.handlebars
client_src=src/client/util.coffee src/shared/mpd.coffee src/client/socketmpd.coffee src/client/app.coffee
server_src=src/server/server.coffee
styles=src/client/app.styl
# output
appjs=public/app.js
appcss=public/app.css
serverjs=server.js
# compilers
coffee=node_modules/coffee-script/bin/coffee
handlebars=node_modules/handlebars/bin/handlebars
stylus=node_modules/stylus/bin/stylus

.PHONY: build clean watch
SHELL=bash

build: .build.timestamp
	@: # suppress 'nothing to be done' message
.build.timestamp: $(serverjs) $(appjs) $(appcss)
	@touch $@
	@echo done building
	@echo

$(serverjs): $(server_src) lib/mpd.js lib/mpdconf.js
	$(coffee) -p -c $(server_src) >$@.tmp
	mv $@{.tmp,}

lib:
	mkdir -p lib

lib/mpd.js: src/shared/mpd.coffee | lib
	$(coffee) -p -c src/shared/mpd.coffee >$@.tmp
	mv $@{.tmp,}

lib/mpdconf.js: src/server/mpdconf.coffee | lib
	$(coffee) -p -c src/server/mpdconf.coffee >$@.tmp
	mv $@{.tmp,}

$(appjs): $(views) $(client_src)
	for f in $(client_src); do $(coffee) -p -c $$f >>$@.tmp; done
	$(handlebars) $(views) -k if -k each -k hash >>$@.tmp
	mv $@{.tmp,}

$(appcss): $(styles)
	$(stylus) <$(styles) >$@.tmp
	mv $@{.tmp,}

clean:
	rm -f ./$(appjs){,.tmp}
	rm -f ./$(appcss){,.tmp}
	rm -f ./$(serverjs){,.tmp}
	rm -rf ./lib
	rm -f ./public/library

watch:
	bash -c 'while [ 1 ]; do make --no-print-directory; sleep 0.5; done'
