all: webfmuserver install

webfmuserver: src/main.c
	gcc -std=c11 -o webfmuserver src/main.c -ldl

spacestation: src/space_station.c
	gcc -std=c11 -shared -Wall -o fmu/space_station_fmu.so src/space_station.c -lm

install:
	sudo cp webfmuserver /usr/local/webfmu
	sudo cp -r fmu /usr/local/webfmu
	sudo cp -r html /usr/local/webfmu
	sudo cp -r res /usr/local/webfmu
	