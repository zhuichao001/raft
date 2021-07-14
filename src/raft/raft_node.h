
class RaftNode{
public:
    enum RAFT_FLAG{
        RAFT_FLAG_VOTING = 1,
        RAFT_FLAG_VOTEDME = 2,
        RAFT_FLAG_NEWLOG = 4,
    };

    RaftNode(Session *se, int id) : 
        session_(se),
        id_(id) {
        next_idx_ = 1;
        match_idx_ = 0;
        flags = RAFT_NODE_VOTING; //TODO
    }

    void VoteForMe(bool vote) {
        if(vote){
            flags_ |= RAFT_FLAG_VOTEDME;
        }else{
            flags_ &= ~RAFT_FLAG_VOTEDME;
        }
    }

    bool IsVoteForMe(){
        return (flags_  & RAFT_FLAG_VOTEDME)!=0;
    }

    void SetVoting(bool voting){
        if(voting){
            flags_ |= RAFT_FLAG_VOTING;
        }else{
            flags_ &= ~RAFT_FLAG_VOTING;
        }
    }

    void IsVoting(){
        return flags_ &= RAFT_FLAG_VOTEDME;
    }

    int NodeId(){
        return id_;
    }

    int GetNextIndex(){
        return next_idx_;
    }

    void SetNextIndex(int next){
        next_idx_ = next;
    }

    int GetMatchIndex(){
        return match_idx_;
    }

    void SetMatchIndex(int match){
        match_idx_ = match;
    }

private:
    RpcClient * dialer_;
    int next_idx_;
    int match_idx_;
    int flags_;
    int id_;
};
