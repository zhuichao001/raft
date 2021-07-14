
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

    //for application
    int Propose(std::string udata);

    int Process(LogEntry *e);

private: //for leader
    void forwardLeader();

    void appendEntry(LogEntry *e);

    void sendAppendentries() ;

    void sendAppendentriesTo(RaftNode *node);

private: //for follower
    void forwardFollower(){

    void startElection();

    void becomeCandidate();

    void raftVoteFor(RaftNode *node);

private: //for candidate
    void forwardCandidate();

    void sendAppendEntries();

    void becomeLeader();

private:
    int applyEntry();

    void setState(int st);

    void sinceLastPeriod();

    int getCurrentIndex(){
        return 0; //TODO
    }


privarte:
    RaftLog log_;

    int current_term_;
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
