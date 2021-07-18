
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

void Raft::sinceLastPeriod(){
    usec_since_last = nowtime_us();
    time_elapsed += usec_since_last;
}

int Raft::Forward(){
    sinceLastPeriod();

    if (state == RAFT_STATE_LEADER) {
        forwardLeader();
    } else if (state == RAFT_STATE_FOLLOWER) {
        forwardFollower();
    } else if (state == RAFT_STATE_CANDIDATE) {
        forwardCandidate();
    }

    if (applied_idx < commit_idx) {
        if (-1 == applyEntry()) {
            return -1;
        }
    }
    return 0;
}

//for leader
void Raft::forwardLeader(){
    if (time_elapsed >= request_timeout) {
        sendAppendEntries();
    }
}

void Raft::sendAppendentries() {
     me->timeout_elapsed = 0;
     for (i = 0; i < num_nodes; i++) {
         if (nodes[i] != local) {
             sendAppendentriesTo(nodes[i]);
         }
    }
}

void Raft::sendAppendentries(RaftNode *node){
    assert(node);
    assert(node != local);

    AppendEntriesRequest req;
    req.term = this->term_;
    req.leader_commit = this->commit_idx_;

    int next_idx = node->GetNextIndex();
    RaftEntry* e = getEntry(next_idx);
    if (e) {
        req.entries = e;
        req.n_entries = 1; //TODO send more than 1
    }

    if (next_idx>1) {
        RaftEntry* prev = getEntry(next_idx - 1);
        req.prev_log_idx = next_idx - 1;
        if (prev) {
            req.prev_log_term = prev->term;
        }
    }

    node->sendAppendentries(&req);
    return 0;
}

int Raft::appendEntry(RaftEntry *e) {
    if(e->isConfigChange()){
        reconf_idx_ = getCurrentIndex();
    }
    return _log.appendEntry(e);
}

int Raft::recvAppendEntries(RaftNode *from, AppendEntriesRequest *msg, AppendEntriesResponse *rsp) {
    time_elapsed_ = 0;

    rsp->success = false;
    rsp->first_idx = 0;
    rsp->term = term_;

    if (0 < msg->n_entries) { //print request
       fprintf(stderr, "Receive appendentries from: %lx, t:%d ci:%d lc:%d pli:%d plt:%d #%d",
           local->NodeId(),
           msg->term,
           getCurrentIndex(),
           msg->leader_commit,
           msg->prev_log_idx,
           msg->prev_log_term,
           msg->n_entries);
    }

    if (isCandidate() && term_ == msg->term) {
        voted_for_ = -1;
        becomeFollower();
    } else if (term_ < msg->term) {
        term_ = msg->term;
        rsp->term = msg->term;
        becomeFollower();
    } else if (term_ > msg->term) {
        fprintf(stderr, "msg's term:%d < term_:%d", msg->term, current_term);
        rsp->current_idx = getCurrentIndex();
        return -1;
    }

    if (msg->prev_log_idx > 0) {
        RaftEntry *e = log_.getEntry(msg->prev_log_idx);
        if (!e || msg->prev_log_idx > getCurrentIndex()) { //second condition will be false?
            rsp->current_idx = getCurrentIndex();
            return -1;
        }

        if (e->term != msg->prev_log_term) {
            fprintf("msg term doesn't match prev_term (ie. %d vs %d) ci:%d pli:%d", 
                    e->term, msg->prev_log_term, getCurrentIndex(), msg->prev_log_idx);
            assert(commit_idx_ < msg->prev_log_idx);
            log_.delFrom(msg->prev_log_idx);
            rsp->current_idx = msg->prev_log_idx - 1;
            return -1;
        }
    }

    if (msg->n_entries==0 && msg->prev_log_idx>0 && msg->prev_log_idx+1<getCurrentIndex()) {
        assert(commit_idx_ < msg->prev_log_idx + 1);
        log_.delFrom(msg->prev_log_idx + 1);
    }

    rsp->current_idx = msg->prev_log_idx;

    for (int i = 0; i < msg->n_entries; ++i) {
        RaftEntry* e = &msg->entries[i];
        int index = msg->prev_log_idx + 1 + i;
        rsp->current_idx = index;

        RaftEntry* existing = log_.getEntry(index);
        if (!existing) {
            break;
        }
        if (existing->term != e->term) {
            assert(commit_idx_ < index);
            log_.delFrom(index);
            break;
        }
    }

    for (; i < msg->n_entries; i++) {
        int res = log_.appendEntry(&msg->entries[i]);
        if (-1 == res) {
            rsp->current_idx = msg->prev_log_idx - 1;
            return -1;
        }
        rsp->current_idx_ = msg->prev_log_idx + 1 + i;
    }

    if (getCurrentIndex() < msg->leader_commit) {
        int last_log_idx = max(getCurrentIndex(), 1);
        commit_idx_ = min(last_log_idx, msg->leader_commit);
    }

    leader_ = from;

    rsp->success = true;
    rsp->first_idx = msg->prev_log_idx + 1;
    return 0;
}

//for follower
void Raft::forwardFollower(){
    if (time_elapsed >= election_timeout) {
        if (1 < num_nodes) {
            electionStart();
        }
    }
}

void Raft::becomeCandidate(){
    fprintf(stderr, "becoming candidate");
    local->incr_current_term();

    for (int i = 0; i < num_nodes; i++) {
        nodes[i]->VoteForMe(0);
    }

    raft_vote(me_, me->node);
    leader = NULL;

    setState(RAFT_STATE::CANDIDATE);

    timeout_elapsed = rand() % timeout_election;

    for (i = 0; i < me->num_nodes; i++) {
        if (nodes[i]!=local && nodes[i]->isVoting()){
            sendRequestVote(nodes[i]);
        }
    }
}

//for candidator
void Raft::becomeLeader(){
    fprintf(stderr, "becoming leader term:%d", term_);
    setState(RAFT_STATE::LEADER);

    for (auto & it : nodes_) {
        RaftNode * node = it.second;
        if (local_ == node || !node->IsVoting()) {
            continue;
        }

        node->SetNextIndex(getCurrentIndex() + 1);
        node->setMatchIndex(0);
        sendAppendentries(node);
    }
}

void Raft::becomeFollower(){
    fprintf(stderr, "becoming follower term:%d", term_);
    setState(RAFT_STATE::FOLLOWER);
}

void Raft::sendRequestVode(RaftNode * node){
    VoteRequest req;

    assert(node);
    assert(node != me->node);

    frpintf(stderr, "%d sending requestvote to: %d", local->NodeId(), node->NodeId());

    req.term = current_term;
    req.last_log_idx = raft_get_current_idx(me_); //TODO
    req.last_log_term = raft_get_last_log_term(me_); //TODO
    req.candidate_id = local->NodeId(); 
    sendRequestvote(node, &req);
    return 0;
}

void Raft::raftVoteFor(RaftNode *node){
    vote_for = node->NodeId();
    persist_vote(vote_for); //TODO
}

void Raft::forwardCandidate(){
    //TODO
}

void Raft::sendAppendEntries(){
}

int Raft::Propose(RaftEntry *e){
    if(e.isReconfig() && reconfig_idx!=-1){
        return -1;
    }

    if(!isLeader()){
        return -1;
    }

    e->term = current_term;
    appendEntry(e);
    return 0;
}

int Raft::OnReceive(RaftEntryRequest *e, RaftEntryResponse *r){
    assert(e!=nullptr && r!=nullptr);

    if (e.isConfigChange()) {
        if (-1 != reconf_idx_) {
            return -1;
        }
    }

    if (!isLeader()) {
        return -1;
    }
            
    fprintf(stderr, NULL, "received entry t:%d id: %d idx: %d", term_, e->id, getCurrentIndex() + 1);

    e->term = term_;
    appendEntry(e);

    for ( auto &it : nodes_) {
        RaftNode * node = it.second;
        if (local_ == node || !node || !node->IsVoting()){
            continue;
        }

        int next_idx = node->GetNextIndex();
        if (next_idx == getCurrentIndex()){
            sendAppendentries(node);
        }
    }

    if (nodes_.size()==1) {
        commit_idx_ = getCurrentIndex();
    }

    r->id = e->id;
    r->idx = getCurrentIndex();
    r->term = term_;

    if (e->isConfigChange()) {
        reconf_idx_ = getCurrentIndex();
    }
    return 0;
}


int Raft::applyEntry()
    if (applied_idx_ >= commit_idx_) {
        return -1;
    }

    int log_idx = applied_idx_ + 1;
    RaftEntry *e = getEntry(log_idx);
    if (!e) {
        return -1;
    }

    fprintf(stderr, "applying log: %d, id: %d size: %d", 
            applied_idx_, e->id, e->data.len);

    app_.Apply(e);
    ++applied_idx_;

    if (log_idx == reconf_log_idx_){
        reconf_log_idx_ = -1;
    }
    return 0;
}

void Raft::startElection() {
    fprintf(stderr, "election starting: %d %d, term: %d ci: %d", 
            timeout_election_, time_elapsed, term_, getCurrentIndex());
    becomeCandidate();
}

int Raft::recvAppendentriesResponse(RaftNode* node, AppendEntriesResponse *r) {
    fpirntf(stderr, "received appendentries response %s ci:%d rci:%d 1stidx:%d",
            r->success == 1 ? "SUCCESS" : "fail",
            getCurrentIndex(),
            r->current_idx,
            r->first_idx);

    if (!isleader()) {
        return -1;
    }

    if (!node) {
        return 0;
    }

    if (r->current_idx != 0 && r->current_idx <= node->GetMatchIndex()) {
        return 0;
    }

    if (term_ < r->term) {
        term_ = r->term;
        becomeFollower();
        return 0;
    } else if (term_ != r->term) {
        return 0;
    }

    if (0 == r->success) {
        int next_idx = node->getNextIndex();
        if (r->current_idx < next_idx - 1) {
            node->setNextIndex(min(r->current_idx + 1, getCurrentIndex()));
        } else {
            node->setNextIndex(next_idx - 1);
        }

        node->sendAppendEntries(); //retry
        return 0;
    }

    assert(r->current_idx <= getCurrentIndex());

    node->setNextIndex(r->current_idx + 1);
    node->setMatchIndex(r->current_idx);

    if (!node->Isvoting() &&
            -1 == reconf_idx_ &&
            getCurrentIndex() <= r->current_idx + 1 &&
            0 == node->HasNewLog()) {
        node->setHasNewLog();
        me->cb.node_has_sufficient_logs(me_, me->udata, node); //TODO
    }

    /* Update commit idx */
    int votes = 1; /* include me */
    int point = r->current_idx;
    int i;
    for (auto &it : nodes_) {
        RaftNode *node = it->second;
        if (local_ == node || !node->isVoting()) {
            continue;
        }

        int match_idx = node->getMatchIndex();
        if (0 < match_idx) {
            RaftEntry *e = getEntryFromIndex(match_idx);
            if (e->term == term_ && point <= match_idx) {
                votes++;
            }
        }
    }

    if (nodes.size() / 2 < votes && getCommitIndex() < point) {
        commit_idx_ = point;
    }

    if (getEntryFromIndex(node->GetNextIndex())) {
        node->sendAppendEntries(node);
    }

    /* periodic applies committed entries lazily */

    return 0;
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
    RaftEntry* e = getEntryFromIndex(current_idx);
    if (e->term < req->last_log_term) {
        return true;
    }
    if (req->last_log_term == e->term && current_idx <= req->last_log_idx) {
        return true;
    }

    return false;
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

    if (r->granted_for) {   
        voteFor(local_);
        int votes = getVotesNum();
        if (votes > nodes.size()/2) {
            becomeLeader();
        }
    }

    return 0;
}
