coffee=iced
appjs=public/app.js
appcss=public/app.css
views=views/*.handlebars
src=src/*.coffee
scss=src/app.scss

testjs=public/test.js
testsrc=test/test.coffee src/mpd.coffee
testview=test/view.handlebars

ifeq ($(DEBUG),) # if not debugging
coffee_min=| uglifyjs
handlebars_min=-m
sass_min=-t compressed
endif

or_die = || (rm -f $@; exit 1)

.PHONY: build clean

build: $(appjs) $(appcss) $(testjs) $(testhtml)

$(appjs): $(views) $(src)
	(cat $(src) | $(coffee) -ps $(coffee_min) >$@) $(or_die)
	(handlebars $(views) $(handlebars_min) -k if -k each -k hash >>$@) $(or_die)

$(appcss): $(scss)
	sass --no-cache --scss $(sass_min) $(scss) $(appcss)

$(testjs): $(testview) $(testsrc)
	cat $(testsrc) | $(coffee) -ps >$(testjs)
	handlebars $(testview) >>$(testjs)

clean:
	rm -f $(appjs) $(appcss) $(testjs)

