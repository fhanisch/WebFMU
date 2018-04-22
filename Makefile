all: mat webfmuserver install

mat: matIO/matio.c
	gcc -c -D LIB -o matio.o matIO/matio.c
	ar cr libmatio.a matio.o

webfmuserver: src/main.c libmatio.a
	gcc -std=c11 -o webfmuserver src/main.c -L . -ldl -lmatio

spacestation: src/space_station.c
	gcc -std=c11 -shared -Wall -o fmu/space_station_fmu.so src/space_station.c -lm

install:
	sudo cp webfmuserver /usr/local/webfmu
	sudo cp -r fmu /usr/local/webfmu
	sudo cp -r html /usr/local/webfmu
	sudo cp -r res /usr/local/webfmu
	