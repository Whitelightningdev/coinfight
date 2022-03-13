CXX = g++
CXXFLAGS = -g -Wall -std=c++17 -pthread -no-pie

INC=-I/usr/include -I/usr/include/python3.8/ -I../common -I./include/ `python3-config --includes`
LIBSERVER=-lboost_system -lsfml-graphics -lsfml-system -lboost_filesystem `python3-config --ldflags` -lpython3.8
LIBCLIENT=-lboost_system -lsfml-graphics -lsfml-system -lsfml-window -lGL -lGLU -lGLEW

install: all
	sudo apt install libsfml-dev

install-client: bin/client
	sudo apt install libsfml-dev

all: pre-build main-build

client: pre-build client-build
	cp assets/Andale_Mono.ttf bin/

server: pre-build server-build

pre-build:
	mkdir -p cpp/obj
	mkdir -p bin/

main-build: server-build client-build bin/coinfight_local

server-build: bin/server

client-build: bin/client bin/coinfight_local

cpp/obj/%.o: cpp/src/%.cpp
	$(CXX) $(CXXFLAGS) -c $^ -o $@ $(INC)

bin/coinfight_local: cpp/obj/coinfight_local.o cpp/obj/engine.o cpp/obj/vchpack.o cpp/obj/myvectors.o cpp/obj/cmds.o cpp/obj/common.o cpp/obj/coins.o cpp/obj/packets.o cpp/obj/events.o cpp/obj/input.o cpp/obj/graphics.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBCLIENT)

bin/server: cpp/obj/server.o cpp/obj/engine.o cpp/obj/vchpack.o cpp/obj/myvectors.o cpp/obj/cmds.o cpp/obj/common.o cpp/obj/coins.o cpp/obj/packets.o cpp/obj/sigWrapper.o cpp/obj/events.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBSERVER)
	cp py/* bin/
	cp secret.txt bin/secret.txt

bin/client: cpp/obj/client.o cpp/obj/engine.o cpp/obj/vchpack.o cpp/obj/myvectors.o cpp/obj/cmds.o cpp/obj/common.o cpp/obj/coins.o cpp/obj/graphics.o cpp/obj/input.o cpp/obj/packets.o cpp/obj/events.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBCLIENT)

bin/test: cpp/obj/test.o cpp/obj/engine.o cpp/obj/vchpack.o cpp/obj/myvectors.o cpp/obj/cmds.o cpp/obj/common.o cpp/obj/graphics.o cpp/obj/coins.o cpp/obj/input.o cpp/obj/sigWrapper.o cpp/obj/graphics.o cpp/obj/events.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBCLIENT) $(LIBSERVER)

cpp/obj/vchpack.o:
	$(CXX) $(CXXFLAGS) -c ../common/vchpack.cpp $^ -o $@

cpp/obj/myvectors.o:
	$(CXX) $(CXXFLAGS) -c ../common/myvectors.cpp $^ -o $@