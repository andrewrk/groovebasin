coffee=coffee
appjs=public/app.js
appcss=public/app.css
views=views/*.handlebars
src=src/*.coffee
scss=src/app.scss

testjs=public/test.js
testsrc=test/test.coffee src/mpd.coffee
testview=test/view.handlebars

or_die = || (rm -f $@; exit 1)

.PHONY: build clean

build: $(appjs) $(appcss) $(testjs) $(testhtml)

$(appjs): $(views) $(src)
	(coffee -p -c $(src) >$@) $(or_die)
	(handlebars $(views) -k if -k each -k hash >>$@) $(or_die)

$(appcss): $(scss)
	sass --no-cache --scss $(scss) $(appcss)

$(testjs): $(testview) $(testsrc)
	coffee -p -c $(testsrc) >$(testjs)
	handlebars $(testview) >>$(testjs)

clean:
	rm -f $(appjs) $(appcss) $(testjs)

