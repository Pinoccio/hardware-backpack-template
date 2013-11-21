
RST=$(wildcard *.rst)
HTML=$(RST:.rst=.html)

all: $(HTML)

%.html: %.rst
	rst2html --stylesheet=style.css $^ $@
