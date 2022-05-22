#ifndef _RAFT_RAFTNODE_H_
#define _RAFT_RAFTNODE_H_

#include "lotus/address.h"
#include "proto/raftmsg.pb.h"


class RaftNode{
public:
    enum RAFT_FLAG{
        RAFT_FLAG_VOTING = 1,
        RAFT_FLAG_VOTEDME = 2,
        RAFT_FLAG_NEWLOG = 4,
    };

    RaftNode(int node_id, const address_t &addr) :
        node_id_(node_id),
        addr_(addr){
        next_idx_ = 1;
        match_idx_ = 0;
        flags_ = RAFT_FLAG_VOTING;
    }

    void VoteForMe(bool vote) {
        if(vote){
            flags_ |= RAFT_FLAG_VOTEDME;
        }else{
            flags_ &= ~RAFT_FLAG_VOTEDME;
        }
    }

    bool HasVotedForMe() const {
        return (flags_ & RAFT_FLAG_VOTEDME) != 0;
    }

    void SetVoting(bool voting) {
        if(voting){
            flags_ |= RAFT_FLAG_VOTING;
        }else{
            flags_ &= ~RAFT_FLAG_VOTING;
        }
    }

    bool IsVoting() const {
        return flags_ & RAFT_FLAG_VOTING;
    }

    int GetNodeId() const {
        return node_id_;
    }

    int GetNextIndex() const {
        return next_idx_;
    }

    void SetNextIndex(int next) {
        next_idx_ = next;
    }

    int GetMatchIndex() const {
        return match_idx_;
    }

    void SetMatchIndex(int match) {
        match_idx_ = match;
    }

    const address_t *GetAddress() const {
        return &addr_;
    }

    void SetState(raft::RaftState st){
        state_ = st;
    }

    raft::RaftState GetState(){
        return state_;
    }

    void print() const {
        fprintf(stderr, "    [RaftNode] nodeid:%d nextidx:%d matchidx:%d flags:%d addr:%s %d\n", 
                node_id_, next_idx_, match_idx_, flags_, addr_.ip.c_str(), addr_.port);
    }

private:
    int node_id_;
    int next_idx_;
    int match_idx_;
    int flags_;

    const address_t addr_;
    raft::RaftState state_;
};

#endif
