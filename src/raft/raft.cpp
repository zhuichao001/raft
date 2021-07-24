#include "raft.h"

Raft::Raft(RaftFSM *app): app_(app){
    term_ = 0;
    voted_for_ = -1;
    commit_idx_ = 0;
    applied_idx_ = 0;
    time_elapsed_ = 0;
    usec_since_last_ = nowtime_us();
    timeout_request_ = 200;
    timeout_election_ = 1000;
    reconf_log_idx_ = -1;
    state_(RAFT_STATE_FOLLOWER);
    leader_ = NULL;
    local_ = NULL;
}

int Raft::Propose(std::string data){
    if(!isLeader()){
        return -1;
    }

    LogEntry *e = new LogEntry();
    e->term = current_term;
    e->id = getCurrentIndex();
    e->data = data;

    appendEntry(e);
    sendAppendEntries();
    return 0;
}

int Raft::ChangeMember(int action, std::string addr) {
    if(e.isReconfig() && reconfig_idx!=-1){
        return -1;
    }

    LogEntry *e = new LogEntry();
    e->logtype = action>0 ? LOGTYPE_ADD_NODE : LOGTYPE_REMOVE_NODE;
    e->term = current_term;
    e->id = getCurrentIndex();
    e->data = addr;

    reconfig_idx = e->id;

    appendEntry(e);
    sendAppendEntries();

    return 0;
}

void Raft::sendAppendEntries(){
    time_elapsed_ = 0;
    for (auto &it : nodes_) {
        RaftNode *node = it.second;
        if (node == local_) {
            continue;
        }
        sendAppendEntries(node);
    }
}

void Raft::sendAppendEntries(RaftNode *node){
    AppendEntriesRequest req;
    req.term = term_;
    req.leader_commit = commit_idx_;

    LogEntry e;
    int next_idx = node->GetNextIndex();
    LogEntry* suc = getEntryFromIndex(next_idx);
    if (suc) {
        e.term = suc->term;
        e.id   = suc->id;
        e.type = suc->type;
        e.data = suc->data;

        req.entries = &e; //TODO send more than 1
        req.n_entries = 1;
    }

    if (next_idx > 1) {
        req.log_idx = next_idx - 1;
        LogEntry * prev = getEntryFromIndex(next_idx - 1);
        if (prev) {
            req.log_term = prev->term;
        }
    }

    fprintf(stderr, "sending appendentries node: ci:%d t:%d lc:%d pli:%d plt:%d",
            getCurrentIndex(),
            req.term,
            req.leader_commit,
            req.log_idx,
            req.log_term);

    node->SendAppendEntries(&req);
}

int Raft::appendEntry(LogEntry *e) {
    if(e->isForReconfig()){
        reconf_idx_ = getCurrentIndex();
    }
    _log.appendEntry(e);
    return 0;
}

int Raft::recvAppendEntries(RaftNode *node_from, AppendEntriesRequest *msg, AppendEntriesResponse *rsp) {
    time_elapsed_ = 0;

    rsp->success = false;
    rsp->first_idx = 0;
    rsp->term = term_;

    //print request
    fprintf(stderr, "Receive AppendEntries from: %lx, t:%d ci:%d lc:%d pli:%d plt:%d #%d",
        local->NodeId(),
        msg->term,
        getCurrentIndex(),
        msg->leader_commit,
        msg->log_idx,
        msg->log_term,
        msg->n_entries);

    if (isCandidate() && term_ == msg->term) {
        voted_for_ = -1;
        becomeFollower();
    } else if (term_ < msg->term) {
        term_ = msg->term;
        rsp->term = msg->term;
        becomeFollower();
    } else if (term_ > msg->term) {
        fprintf(stderr, "msg's term:%d < local term_:%d", msg->term, current_term);
        rsp->current_idx = getCurrentIndex();
        return -1;
    }

    if (msg->log_idx > 0) {
        LogEntry *e = log_.getEntry(msg->log_idx);
        if (!e || msg->log_idx > getCurrentIndex()) { //second condition will be false?
            rsp->current_idx = getCurrentIndex();
            return -1;
        }

        if (e->term != msg->log_term) {
            fprintf("msg term doesn't match prev_term (ie. %d vs %d) ci:%d pli:%d", 
                    e->term, msg->log_term, getCurrentIndex(), msg->log_idx);
            assert(commit_idx_ < msg->log_idx);
            log_.delFrom(msg->log_idx);
            rsp->current_idx = msg->log_idx - 1;
            return -1;
        }
    }

    leader_ = node_from;

    if (msg->n_entries==0 && msg->log_idx>0 && msg->log_idx+1<getCurrentIndex()) {
        assert(commit_idx_ < msg->log_idx + 1);
        log_.delFrom(msg->log_idx + 1);
    }

    rsp->current_idx = msg->log_idx;

    for (int i = 0; i < msg->n_entries; ++i) {
        LogEntry* e = &msg->entries[i];
        int index = msg->log_idx + 1 + i;
        rsp->current_idx = index;

        LogEntry* existing = log_.getEntry(index);
        if (!existing) {
            break;
        }
        if (existing->term != e->term) {
            assert(commit_idx_ < index);
            log_.delFrom(index);
            break;
        }
    }

    for (int i=0; i < msg->n_entries; ++i) {
        int res = log_.appendEntry(&msg->entries[i]);
        if (res == -1) {
            rsp->current_idx = msg->log_idx - 1;
            return -1;
        }
        rsp->current_idx_ = msg->log_idx + 1 + i;
    }

    if (getCurrentIndex() < msg->leader_commit) {
        int last_log_idx = max(getCurrentIndex(), 1);
        commit_idx_ = min(last_log_idx, msg->leader_commit);
    }

    applyEntry();

    rsp->success = true;
    rsp->first_idx = msg->log_idx + 1;
    return 0;
}

int Raft::recvAppendEntriesResponse(AppendEntriesResponse *r) {
    fpirntf(stderr, "received appendentries response %s ci:%d rci:%d 1stidx:%d",
            r->success == 1 ? "SUCCESS" : "fail",
            getCurrentIndex(),
            r->current_idx,
            r->first_idx);

    if (!isleader()) {
        return -1;
    }

    if (!local_) {
        return 0;
    }

    if (r->current_idx != 0 && r->current_idx <= local_->GetMatchIndex()) {
        return 0;
    }

    if (term_ < r->term) {
        term_ = r->term;
        becomeFollower();
        return 0;
    } else if (term_ != r->term) {
        return 0;
    }

    if (!r->success) {
        int next_idx = local_->getNextIndex();
        if (r->current_idx < next_idx - 1) {
            local_->setNextIndex(min(r->current_idx + 1, getCurrentIndex()));
        } else {
            local_->setNextIndex(next_idx - 1);
        }

        local_->sendAppendEntries();
        return 0;
    }

    assert(r->current_idx <= getCurrentIndex());

    local_->setMatchIndex(r->current_idx);
    local_->setNextIndex(r->current_idx + 1);

    // Update commit idx
    int meet = 0;
    for (auto &it : nodes_) {
        RaftNode *node = it->second;
        if(!node->isVoting()){
            continue;
        }

        if (node == local_) {
            ++meet;
            continue;
        }

        int match_idx = node->getMatchIndex();
        if (match_idx > 0) {
            LogEntry *e = getEntryFromIndex(match_idx);
            if (e->term == term_ && r->current_idx <= match_idx) {
                ++meet;
            }
        }
    }

    if (nodes.size() / 2 < meet && commit_idx_ < r->current_idx) {
        commit_idx_ = r->current_idx;
    }

    if (getEntryFromIndex(local_->GetNextIndex())) {
        local_->sendAppendEntries();
    }

    applyEntry();
    return 0;
}

int Raft::applyEntry()
    if (applied_idx_ >= commit_idx_) {
        return -1;
    }

    while(applied_idx_ < commmit_idx_){
        int log_idx = applied_idx_ + 1;
        LogEntry *e = getEntry(log_idx);
        if (!e) {
            return -1;
        }

        fprintf(stderr, "applying log: %d, id: %d size: %d", applied_idx_, e->id, e->data.len);

        app_.Apply(e);
        ++applied_idx_;

        if (log_idx == reconf_log_idx_){
            reconf_idx_ = -1;
        }
    }
    return 0;
}

void Raft::tick(){ //for follower
    if (time_elapsed >= election_timeout) {
        if (1 < num_nodes) {
            electionStart();
        }
    }
}

void Raft::becomeCandidate(){
    fprintf(stderr, "becoming candidate");
    term_ += 1;

    for (int i = 0; i < num_nodes; i++) {
        nodes[i]->VoteForMe(0); //clear flag
    }

    voteFor(local_->NodeId());
    leader = nullptr;

    setState(RAFT_STATE::CANDIDATE);

    for (auto &it : nodes_) {
        RaftNode *node = it.second;
        if (node!=local_ && node->isVoting()){
            sendVoteRequest(node);
        }
    }
}

void Raft::becomeLeader(){ //for candidator
    fprintf(stderr, "becoming leader term:%d", term_);
    setState(RAFT_STATE::LEADER);
    leader_ = local_;

    for (auto & it : nodes_) {
        RaftNode * node = it.second;
        if (local_ == node || !node->IsVoting()) {
            continue;
        }

        node->SetNextIndex(getCurrentIndex() + 1);
        node->setMatchIndex(0);
        sendAppendEntries(node);
    }
}

void Raft::becomeFollower(){
    fprintf(stderr, "becoming follower term:%d", term_);
    setState(RAFT_STATE::FOLLOWER);
}

void Raft::sendVoteRequest(RaftNode * node){
    assert(node);
    assert(node != local_);

    frpintf(stderr, "%d sending requestvote to: %d", local->NodeId(), node->NodeId());

    VoteRequest req;
    req.term = current_term;
    req.log_idx = getCurrentIndex();
    req.log_term = getLastLogTerm();
    req.candidate_id = local->NodeId(); 
    node->sendVoteRequest(&req);
    return 0;
}

void Raft::raftVoteFor(RaftNode *node){
    vote_for = node->NodeId();
    persist_vote(vote_for); //TODO
}

void Raft::startElection() {
    fprintf(stderr, "election starting: %d %d, term: %d ci: %d", 
            timeout_election_, time_elapsed, term_, getCurrentIndex());
    becomeCandidate();
}


bool Raft::shouldGrantVote(VoteRequest* req) {
    if (req->term < term_){
        return false;
    }

    if (isAlreadyVoted()) {
        return false;
    }

    if (0 == current_idx) {
        return true;
    }

    int current_idx = getCurrentIndex();
    LogEntry* e = getEntryFromIndex(current_idx);
    if (e->term < req->last_log_term) {
        return true;
    }
    if (req->last_log_term == e->term && current_idx <= req->last_log_idx) {
        return true;
    }

    return false;
}

int Raft::sendVoteRequest(RaftNode *to_node){
    assert(to_node);
    assert(to_node != local);

    fprintf(stderr, "sending vote request to: %d", to_node->GetNodeId());

    VoteReqeust req
    req.term = term_;
    req.last_log_idx = getCurrentIndex();
    req.last_log_term = getLastLogTerm();
    req.candidate_id = local->GetNodeId();
    node->SendVoteRequest(&req);
    return 0;
}

int Raft::recvVoteResponse(VoteResponse *rsp) {
    if (!isCandidate()) {   
        return 0;
    } else if (term_ < r->term) {   
        term_ = r->term;
        becomeFollower();
        return 0;
    } else if (term_ != r->term) {   
        return 0;
    }

    if (rsp->granted_for) {   
        voteFor(local_->NodeId());
        int votes = getVotesNum();
        if (votes > nodes.size()/2) {
            becomeLeader();
        }
    }

    return 0;
}

int Raft::recvVoteRequest(VoteRequest *req, VoteResponse *rsp) {
    if (term_ < req->term) {
        term_ = req->term;
        becomeFollower();
    }

    if (shouldGrantVote(req)) {
        assert(!isLeader() && isCandidate());

        voteFor(req->candidate_id);
        rsp->grant_for = true;

        leader_ = nullptr;
        time_elapsed = 0;
    } else {
        rsp->grant_for = false;
    }

    rsp->term = term_;
    return 0;
}

void Raft::voteFor(int nodeid) {
    voted_for_ = nodeid;
    //TODO persist
}

int Raft::getVotesNum() {
    int votes = 0;
    for (auto &it : nodes_) {
        RaftNode *node = it.second;
        if (local_ == node) {
            continue;
        }
        if (noe->IsVoting() && node->HasVoteForMe()) {
            votes += 1;
        }
    }

    if (voted_for_ == local_->NodeId()) {
        votes += 1;
    }

    return votes;
}

RaftNode *Raft::addRaftNode(int nodeid, bool is_self, bool is_voting){
    if (nodes_.find(nodeid) != nodes_.end()){
        return nodes_[nodeid];
    }
    nodes_[nodeid] = new RaftNode(nodeid);
    nodes_[nodeid]->SetVoting(is_voting);
    if(is_self){
        local_ nodes_[nodeid];
    }
    return nodes_[nodeid];
}
