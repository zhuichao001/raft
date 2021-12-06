all:
	g++ -std=c++11 -g -o server \
	../src/util.cpp \
	../src/transport.cpp \
	../src/raft.cpp \
	../src/proto/raftmsg.pb.cc \
	main.cpp \
	-I../src/ -I../../lotus/ \
	-L../../lotus/lotus/ -llotus \
	-L/usr/local/lib/ \
	-lprotobuf \
	-lpthread
