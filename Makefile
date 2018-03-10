all: webfmuserver install

webfmuserver: server.c
	gcc -std=c11 -o webfmuserver server.c -ldl

install:
	sudo cp webfmuserver /usr/local/webfmu
	sudo cp sites/index.html /usr/local/webfmu/sites
