#include <string>
#include "raft/raft.h"
#include "raft/raft_sm.h"

class Application: public RaftStateMachine{
public:
    virtual ~Application(){
    }

    int Apply(const std::string data, int raft_index, RaftError error) override {
        fprintf(stderr, "Apply :%s\n", data.c_str());
        msg_ = data;
        return 0;
    }

    int ApplyMemberChange(const raft::Peer &peer, ConfChangeType cctype,int raft_index, RaftError error) override {
        fprintf(stderr, "incoming peer: \n");
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
