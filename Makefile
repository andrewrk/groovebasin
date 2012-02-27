appjs=public/app.js
appcss=public/app.css
serverjs=groovebasind
views=views/*.handlebars
client_src=src/mpd.coffee src/socketmpd.coffee src/app.coffee
server_src=src/daemon.coffee
scss=src/app.scss
mpd_lib=lib/mpd.js
mpd_lib_src=src/mpd.coffee

coffee=node_modules/coffee-script/bin/coffee
handlebars=node_modules/handlebars/bin/handlebars

or_die = || (rm -f $@; exit 1)

.PHONY: build clean

build: $(serverjs) $(appjs) $(appcss)

$(serverjs): $(server_src) $(mpd_lib) lib/mpdconf.js
	(echo "#!/usr/bin/env node" >$@) $(or_die)
	($(coffee) -p -c $(server_src) >>$@) $(or_die)
	(chmod +x $@) $(or_die)

$(mpd_lib): $(mpd_lib_src)
	(mkdir -p lib) $(or_die)
	($(coffee) -p -c $(mpd_lib_src) >$@) $(or_die)

lib/mpdconf.js: src/mpdconf.coffee
	(mkdir -p lib) $(or_die)
	($(coffee) -p -c src/mpdconf.coffee >$@) $(or_die)

$(appjs): $(views) $(client_src)
	($(coffee) -p -c $(client_src) >$@) $(or_die)
	($(handlebars) $(views) -k if -k each -k hash >>$@) $(or_die)

$(appcss): $(scss)
	sass --no-cache --scss $(scss) $(appcss)

clean:
	rm -f $(appjs)
	rm -f $(appcss)
	rm -f $(serverjs)
	rm -rf ./lib/

