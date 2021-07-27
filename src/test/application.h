#include <string>
#include "raft/raft.h"
#include "raft/raft_sm.h"

class Application: public RaftStateMachine{
public:
    virtual ~Application(){
    }

    int Apply(const std::string data) override {
        fprintf(stderr, "apply :%s\n", data.c_str());
        return 0;
    }

    int ApplyMemberChange(const ConfChange &at, uint64_t index) override {
        fprintf(stderr, "member change: %d %d %d\n", at.action, at.nodeid, at.peerid);
    }

    uint64_t GetAppliedIndex() override {
        return applied_index_;
    }

    int LeaderOver(uint64_t term, uint64_t leader) override {
        fprintf(stderr, "leader over, term:%d, leader:%d\n", term, leader);
        return 0;
    }

public:
    //for user interface
    void Set(const std::string &msg) {
        raft_->Propose(msg);
    }

    std::string Get(){
        return msg;
    }

    uint64_t applied_index_;
    Raft *raft_;
    std::string msg;
};
