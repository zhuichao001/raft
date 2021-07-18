
#include<time.h>

enum RAFT_STATE{
    NONE = 0,
    FOLLOWER ,
    CANDIDATE,
    LEADER ,
    LEANER ,
    LIMIT_NUM
};

class Raft{
public:
    Raft(RaftFSM *app);

    int Forward();

    int Propose(RaftEntry *e);

    int OnReceive(RaftEntryRequest *e, RaftEntryResponse *r);

private: //for leader
    void forwardLeader();

    void appendEntry(RaftEntry *e);

    void sendAppendEntries();

    void sendAppendEntriesTo(RaftNode *node);

    void recvAppendEntriesResponse();

private: //for follower
    void forwardFollower(){

    void startElection();

    void becomeCandidate();

    void raftVoteFor(RaftNode *node);

    int recvAppendEntries(AppendEntriesRequest *msg, AppendEntriesResponse *rsp);

    int recvVoteRequest(VoteRequest *req, VoteResponse *rsp);

private: //for candidate
    void forwardCandidate();

    void sendAppendEntries();

    void becomeLeader();

    void becomeFollower();

    int getVotesNum();

    int recvVoteResponse(VoteResponse *rsp);

private:
    int applyEntry();

    void setState(int st);

    void sinceLastPeriod();

    int getCurrentIndex(){
        return log_.getCurrentIndex();
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

    bool shouldGrantVote(VoteRequest* req);

    void voteFor(int nodeid);

privarte:
    RaftLog log_;

    int term_;
    int voted_for_;
    int state_; //FOLLOWER,LEADER,CANDIDATE

    int commit_idx_;
    int applied_idx_;
    int reconf_idx_;

    int time_elapsed_; //since last time
    int timeout_election_;
    int timeout_request_;

    std::map<int, RaftNode*> nodes_;
    RaftNode * leader_;
    RaftNode * local_;

    RaftFSM * app_;
};
