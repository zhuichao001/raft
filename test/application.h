#include <string>
#include "raft.h"
#include "raft_sm.h"

class Application: public RaftStateMachine{
public:
    virtual ~Application(){
    }

    int Apply(const std::string data) override {
        fprintf(stderr, "Apply :%s\n", data.c_str());
        msg_ = data;
        return 0;
    }

    int ApplyMemberChange(const ConfChange &at, uint64_t index) override {
        fprintf(stderr, "member change: %d %d %d\n", at.action, at.nodeid, at.peerid);
        return 0;
    }

    uint64_t GetAppliedIndex() override {
        return applied_index_;
    }

    int OnTransferLeader(bool isleader) override {
        fprintf(stderr, "leader transfer, isleader:%d\n", isleader);
        return 0;
    }

    bool IsLeader(){
        return raft_->IsLeader();
    }

public:
    //for user interface
    void Set(const std::string &msg) {
        fprintf(stderr, "app Set:%s\n", msg.c_str());
        raft_->Propose(msg);
    }

    std::string Get(){
        fprintf(stderr, "app Get:%s\n", msg_.c_str());
        return msg_;
    }

    uint64_t applied_index_;
    Raft *raft_;
    std::string msg_;
};
