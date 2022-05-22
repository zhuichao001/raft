#ifndef _RAFT_STATE_MACHINE_H_
#define _RAFT_STATE_MACHINE_H_

#include <string>
#include <stdint.h>
#include "proto/raftmsg.pb.h"

typedef struct {
    int action; //-1:remove, 1:add
    int nodeid;
    int peerid;
    int type;   //0:normal, 1:leaner
} ConfChange;


enum RaftError{
    RAFT_OK = 0,
    RAFT_ERR_NO_LEADER =1,
    RAFT_ERR_BUSY =2,
};

enum ConfChangeType{
    ADD_PEER = 1,
    DEL_PEER = 2,
};

class RaftStateMachine{
public:
    virtual int Apply(const std::string data, int raft_index, RaftError error) = 0;
    virtual int ApplyMemberChange(const raft::Peer &peer, ConfChangeType cctype,int raft_index, RaftError error) = 0;
    virtual uint64_t GetAppliedIndex() = 0;
    virtual int OnTransferLeader(bool isleader) = 0;

    //GetSnapshotReader() =0;
    //GetSnapshotWriter() =0;
};

#endif
