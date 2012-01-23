appjs=public/app.js
views=views/*.handlebars
src=src/*.coffee

ifeq ($(DEBUG),) # if not debugging
coffee_min=| uglifyjs
handlebars_min=-m
endif


.PHONY: build clean

build: $(appjs)

$(appjs): $(views) $(src)
	cat $(src) | coffee -ps $(coffee_min) >$(appjs)
	handlebars $(views) $(handlebars_min) -k if >>$(appjs)

clean:
	rm -f $(appjs)

