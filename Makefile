gcc: mat_gcc spacestation_gcc webfmuserver_gcc install_raspi
msvc: mat_msvc spacestation_msvc webfmuserver_msvc

mat_gcc: matIO/matio.c
	gcc -c -D LIB -o matio.o matIO/matio.c
	ar cr libmatio.a matio.o

mat_msvc: matIO/matio.c
	cl /nologo /c /W4 /D LIB matIO/matio.c
	lib /nologo matio.obj /out:matio.lib

webfmuserver_gcc: src/main.c libmatio.a
	gcc -std=c11 -Wall -o webfmuserver src/main.c -L . -ldl -lmatio -pthread

webfmuserver_msvc: src/main.c matio.lib
	cl /nologo /W3 /D WINDOWS src/main.c /link Ws2_32.lib matio.lib /out:webfmuserver.exe

spacestation_gcc: src/space_station.c
	gcc -std=c11 -shared -Wall -o fmu/space_station_fmu.so src/space_station.c -lm

spacestation_msvc: src/space_station.c
	cl /nologo /W3 src/space_station.c /link /DLL /out:fmu/space_station_fmu.dll

install_raspi:
	sudo cp webfmuserver /usr/local/webfmu
	sudo cp -r fmu /usr/local/webfmu
	sudo cp -r html /usr/local/webfmu
	sudo cp -r res /usr/local/webfmu
	