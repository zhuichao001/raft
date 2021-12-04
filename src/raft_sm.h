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
    virtual int Apply(const std::string data) = 0;
    virtual int ApplyMemberChange(const ConfChange &at, uint64_t index) = 0;
    virtual uint64_t GetAppliedIndex() = 0;
    virtual int LeaderOver(uint64_t term, uint64_t leader) = 0;
};

#endif
