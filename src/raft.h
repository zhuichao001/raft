#ifndef _RAFT_RAFT_H_
#define _RAFT_RAFT_H_

#include <memory>
#include <map>
#include <time.h>
#include "util.h"
#include "raft_sm.h"
#include "raft_node.h"
#include "raft_log.h"
#include "options.h"
#include "transport.h"
#include "lotus/timer.h"
#include "proto/raftmsg.pb.h"

enum RAFT_STATE {
    NONE = 0,
    LEADER = 1,
    FOLLOWER = 2,
    CANDIDATE = 3,
    LEANER = 4,
};

class Raft{
public:
    Raft(const RaftOptions &opt);

    int Propose(const std::string &data);

    bool IsLeader(){
        return leader_ == local_;
    }

private: //for leader
    void sendAppendEntries();

    void sendAppendEntries(RaftNode *node);

    int recvAppendEntriesResponse(const raft::AppendEntriesResponse *r);

    //for external topology management
    void recvConfChangeRequest(const raft::MemberChangeRequest *req, raft::MemberChangeResponse *rsp);
    void recvConfChangeResponse(raft::MemberChangeResponse *rsp);

private: //for follower
    void tick();

    void startElection();

    void becomeCandidate();

    int recvAppendEntries(const raft::AppendEntriesRequest *msg, raft::AppendEntriesResponse *rsp);

    int recvVoteRequest(const raft::VoteRequest *req, raft::VoteResponse *rsp);

private: //for candidate
    void becomeLeader();

    void becomeFollower();

    bool winQuorumVotes();

    int sendVoteRequest(RaftNode *node);

    int recvVoteResponse(const raft::VoteResponse *rsp);

private: //common
    int writeAhead(raft::LogEntry *e);

    int applyEntry();

    RaftNode *addRaftNode(int nodeid, const address_t &addr, bool is_self, bool is_voting=true);

    int delRaftNode(int nodeid);

    bool shouldGrantVote(const raft::VoteRequest* req);

    int voteBy(const int nodeid);

    void printRaftNodes();

    void setState(int st) {
        state_ = st;
    }

    uint64_t getCurrentIndex(){
        return log_.getCurrentIndex();
    }

    uint64_t getLastLogTerm(){
        int idx = log_.getCurrentIndex();
        if (idx>0) {
            const raft::LogEntry *e = log_.getEntry(idx);
            if (e) {
                return e->term();
            }
        }
        return 0;
    }

    bool isLeader(){
        return RAFT_STATE::LEADER == state_;
    }

    bool isFollower(){
        return RAFT_STATE::FOLLOWER == state_;
    }

    bool isCandidate(){
        return RAFT_STATE::CANDIDATE == state_;
    }

    bool isAlreadyVoted(){
        return voted_for_ != -1;
    }

    void clearVotes();

    void updateCommitIndex(int peer_matchidx);

    int randTimeoutElection(){
        return randint(1000, 3000)*1000;
    }

    bool isStoped(){
        return stoped;
    }

private:
    int id_; //raft group id
    RaftStateMachine *app_;
    std::shared_ptr<Transport> trans_;

    uint64_t term_;     // current term
    int voted_for_;     // candidate propose vote 
    int state_;         // FOLLOWER, LEADER, CANDIDATE

    uint64_t commit_idx_;
    uint64_t applied_idx_;
    uint64_t reconf_idx_;

    RaftNode *leader_;
    RaftNode *local_;

    uint64_t timeout_election_;
    uint64_t timeout_request_;
    uint64_t timeout_heartbeat_;
    uint64_t lasttime_heartbeat_;
    uint64_t lasttime_election_;
    lotus::timer_t *ticker_;

    std::map<const int, RaftNode*> nodes_;
    RaftLog log_;

    bool stoped;

    friend class Transport;
    friend class RaftServer;
};

#endif
