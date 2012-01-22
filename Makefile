.PHONY: build clean

build:
	coffee -j public/app.js -c src/

clean:
	rm -f public/app.js

