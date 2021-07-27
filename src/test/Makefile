all:
	g++ -std=c++11 -o server \
	../raft/transport.cpp \
	../raft/raft.cpp \
	../raft/proto/raftmsg.pb.cc \
	main.cpp \
	-I../ -I../../../lotus/ \
	-L../../../lotus/lotus/ -llotus \
	-L/usr/local/lib/ \
	-lprotobuf
