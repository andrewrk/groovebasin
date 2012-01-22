appjs=public/app.js
views=views/*.handlebars
src=src/*.coffee

ifeq ($(DEBUG),) # if debugging
coffee_min=| uglifyjs
handlebars_min=-m
endif


.PHONY: build clean

build: $(appjs)

$(appjs): $(views) $(src)
	coffee -p -j -c src/ $(coffee_min) >$(appjs)
	handlebars $(views) $(handlebars_min) -k if >>$(appjs)

clean:
	rm -f $(appjs)

