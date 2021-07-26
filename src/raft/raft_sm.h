#ifndef _RAFT_STATE_MACHINE_H_
#define _RAFT_STATE_MACHINE_H_

#include <string>
#include <stdint.h>

typedef struct {
    int action; //-1:remove, 1:add
    int nodeid;
    int peerid;
    int type;   //0:normal, 1:leaner
} ConfChange;

class RaftStateMachine{
public:
    virtual int Apply(const std::string data);
    virtual int ApplyMemberChange(const ConfChange &alter, uint64_t index);
    virtual uint64_t GetAppliedIndex();
    virtual int LeaderOver(uint64_t term, uint64_t leader);
};

#endif
