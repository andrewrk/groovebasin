appjs=public/app.js
appcss=public/app.css
views=views/*.handlebars
src=src/*.coffee
scss=src/app.scss


ifeq ($(DEBUG),) # if not debugging
coffee_min=| uglifyjs
handlebars_min=-m
sass_min=-t compressed
endif


.PHONY: build clean

build: $(appjs)  $(appcss)

$(appjs): $(views) $(src)
	cat $(src) | coffee -ps $(coffee_min) >$(appjs)
	handlebars $(views) $(handlebars_min) -k if -k each -k hash >>$(appjs)

$(appcss): $(scss)
	sass --no-cache --scss $(sass_min) $(scss) $(appcss)

clean:
	rm -f $(appjs) $(appcss)

