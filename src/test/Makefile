all:
	g++ -o server \
	main.cpp \
	../raft/raft.cpp \
	../raft/proto/raftmsg.pb.cc \
	-I../ -I../../../lotus/ -std=c++11
