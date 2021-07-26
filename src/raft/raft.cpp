#include "raft.h"
#include "util.h"
#include <algorithm>
#include <stdio.h>

Raft::Raft(int id, RaftStateMachine *app): 
    id_(id),
    app_(app){
    term_ = 0;
    voted_for_ = -1;
    commit_idx_ = 0;
    applied_idx_ = 0;
    time_elapsed_ = 0;
    timeout_request_ = 200;
    timeout_election_ = 1000;
    reconf_idx_ = -1;
    state_(RAFT_STATE::FOLLOWER);
    leader_ = NULL;
    local_ = NULL;
}

int Raft::Propose(const std::string &data){
    if(!isLeader()){
        return -1;
    }

    if(nodes_.size()==1){
        app_->Apply(data);
        return 0;
    }

    raft::LogEntry *e = new raft::LogEntry();
    e->set_term(term_);
    e->set_index(getCurrentIndex());
    e->set_data(data);

    appendEntry(e);
    sendAppendEntries();
    return 0;
}

int Raft::ChangeMember(int action, std::string addr) {
    if(reconf_idx_!=-1){
        return -1;
    }

    raft::LogEntry *e = new raft::LogEntry();
    e->set_type(action>0 ? raft::LOGTYPE_ADD_NODE : raft::LOGTYPE_REMOVE_NODE);
    e->set_term(term_);
    e->set_index(getCurrentIndex());
    e->set_data(addr);

    reconf_idx_ = e->index();

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
    raft::AppendEntriesRequest req;
    req.set_term(term_);
    req.set_commit(commit_idx_);

    raft::LogEntry *e = req.add_entries();
    int next_idx = node->GetNextIndex();
    const raft::LogEntry* nex = log_.getEntry(next_idx);
    if (nex) {
        e->set_term(nex->term());
        e->set_index(nex->index());
        e->set_type(nex->type());
        e->set_data(nex->data());
    }

    if (next_idx > 1) {
        req.set_last_index(next_idx - 1);
        const raft::LogEntry * prev = log_.getEntry(next_idx - 1);
        if (prev) {
            req.set_last_term(prev->term());
        }
    }

    fprintf(stderr, "sending appendentries node: ci:%d t:%d lc:%d pli:%d plt:%d",
            getCurrentIndex(),
            req.term(),
            req.commit(),
            req.last_index(),
            req.last_term());

    {
        raft::RaftMessage msg;
        msg.set_type(raft::RaftMessage::MSGTYPE_APPENDLOG_REQUEST);
        msg.set_raftid(id_);
        msg.set_ae_req(&req);
        trans_.Send(addr_, &msg);
    }
}

int Raft::appendEntry(raft::LogEntry *e) {
    if(e->type()==raft::LOGTYPE_ADD_NONVOTING_NODE ||
            e->type()==raft::LOGTYPE_ADD_NODE ||
            e->type()==raft::LOGTYPE_REMOVE_NODE ){
        reconf_idx_ = getCurrentIndex();
    }
    log_.appendEntry(e);
    return 0;
}

int Raft::recvAppendEntries(const raft::AppendEntriesRequest *msg, raft::AppendEntriesResponse *rsp) {
    time_elapsed_ = 0;

    rsp->set_success(false);
    rsp->set_first_index(0);
    rsp->set_term(term_);

    //print request
    fprintf(stderr, "Receive AppendEntries from: %lx, t:%d ci:%d lc:%d pli:%d plt:%d",
        local_->NodeId(),
        msg->term(),
        getCurrentIndex(),
        msg->commit(),
        msg->last_index(),
        msg->last_term());

    if (isCandidate() && term_ == msg->term()) {
        voted_for_ = -1;
        becomeFollower();
    } else if (term_ < msg->term()) {
        term_ = msg->term();
        rsp->set_term(msg->term());
        becomeFollower();
    } else if (term_ > msg->term()) {
        fprintf(stderr, "msg's term:%d < local term_:%d", msg->term(), term_);
        rsp->set_current_index(getCurrentIndex());
        return -1;
    }

    if (msg->last_index() > 0) {
        raft::LogEntry *e = log_.getEntry(msg->last_index());
        if (!e || msg->last_index() > getCurrentIndex()) { //second condition will be false?
            rsp->set_current_index(getCurrentIndex());
            return -1;
        }

        if (e->term() != msg->last_term()) {
            fprintf("msg term doesn't match prev_term (ie. %d vs %d) ci:%d pli:%d", 
                    e->term(), msg->last_term(), getCurrentIndex(), msg->last_index());
            assert(commit_idx_ < msg->last_index());
            log_.delFrom(msg->last_index());
            rsp->set_current_index(msg->last_index() - 1);
            return -1;
        }
    }

    leader_ = node_from;

    if (msg->entries_size()==0 && msg->last_index()>0 && msg->last_index()+1<getCurrentIndex()) {
        assert(commit_idx_ < msg->last_index()+1);
        log_.delFrom(msg->last_index()+1);
    }

    rsp->set_current_index(msg->last_index());

    for (int i = 0; i < msg->entries_size(); ++i) {
        const raft::LogEntry* e = &msg->entries(i);
        int index = msg->last_index() + 1 + i;
        rsp->set_current_index(index);

        const raft::LogEntry* existing = log_.getEntry(index);
        if (!existing) {
            break;
        }
        if (existing->term() != e->term()) {
            assert(commit_idx_ < index);
            log_.delFrom(index);
            break;
        }
    }

    for (int i=0; i < msg->entries_size(); ++i) {
        int res = log_.appendEntry(&msg->entries(i));
        if (res == -1) {
            rsp->set_current_index(msg->last_index()-1);
            return -1;
        }
        rsp->set_current_index(msg->last_index()+1+i);
    }

    if (getCurrentIndex() < msg->commit()) {
        int last_log_idx = std::max(getCurrentIndex(), 1);
        commit_idx_ = std::min(last_log_idx, msg->commit());
    }

    applyEntry();

    rsp->set_success(true);
    rsp->set_first_index(msg->last_index() + 1);
    return 0;
}

int Raft::recvAppendEntriesResponse(const raft::AppendEntriesResponse *r) {
    fprintf(stderr, "received appendentries response %s ci:%d rci:%d 1stidx:%d",
            r->success() == 1 ? "SUCCESS" : "fail",
            getCurrentIndex(),
            r->current_index(),
            r->first_index());

    if (!isLeader()) {
        return -1;
    }

    if (!local_) {
        return 0;
    }

    if (r->current_index() != 0 && r->current_index() <= local_->GetMatchIndex()) {
        return 0;
    }

    if (term_ < r->term()) {
        term_ = r->term();
        becomeFollower();
        return 0;
    } else if (term_ != r->term()) {
        return 0;
    }

    if (!r->success()) {
        int next_idx = local_->GetNextIndex();
        if (r->current_index() < next_idx - 1) {
            local_->SetNextIndex(std::min(r->current_index() + 1, getCurrentIndex()));
        } else {
            local_->SetNextIndex(next_idx - 1);
        }

        local_->sendAppendEntries();
        return 0;
    }

    assert(r->current_index() <= getCurrentIndex());

    local_->SetMatchIndex(r->current_index());
    local_->SetNextIndex(r->current_index() + 1);

    // Update commit idx
    int meet = 0;
    for (auto &it : nodes_) {
        RaftNode *node = it.second;
        if(!node->IsVoting()){
            continue;
        }

        if (node == local_) {
            ++meet;
            continue;
        }

        int match_idx = node->GetMatchIndex();
        if (match_idx > 0) {
            const raft::LogEntry *e = log_.getEntry(match_idx);
            if (e->term() == term_ && r->current_index() <= match_idx) {
                ++meet;
            }
        }
    }

    if (nodes_.size() / 2 < meet && commit_idx_ < r->current_index()) {
        commit_idx_ = r->current_index();
    }

    if (log_.getEntry(local_->GetNextIndex())!=nullptr) {
        local_->sendAppendEntries();
    }

    applyEntry();
    return 0;
}

void Raft::recvHeartbeatResponse(const raft::HeartbeatResponse *rsp){
    //TODO
}

int Raft::applyEntry(){
    if (applied_idx_ >= commit_idx_) {
        return -1;
    }

    while(applied_idx_ < commit_idx_){
        int log_idx = applied_idx_ + 1;
        const raft::LogEntry *e = getEntry(log_idx);
        if (!e) {
            return -1;
        }

        fprintf(stderr, "applying log: %d, id: %d size: %d", applied_idx_, e->id, e->data.len);

        app_.Apply(e);
        ++applied_idx_;

        if (log_idx == reconf_idx_){
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

void Raft::recvHeartbeat(const raft::HeartbeatRequest *req, raft::HeartbeatResponse *rsp){
    time_elapsed_ = 0;
    if (term_ < req->term()) {
        term_ = req->term();
        leader_ = nodes_[req->node_id];
        becomeFollower();
    }

    rsp->success = true;
    rsp->node_id = leader_->GetNodeId();
}

void Raft::becomeCandidate(){
    fprintf(stderr, "becoming candidate");
    term_ += 1;

    for (int i = 0; i < num_nodes; i++) {
        nodes[i]->VoteForMe(0); //clear flag
    }

    voteFor(local_->NodeId());
    leader_ = nullptr;

    setState(RAFT_STATE::CANDIDATE);

    for (auto &it : nodes_) {
        RaftNode *node = it.second;
        if (node!=local_ && node->IsVoting()){
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
        node->SetMatchIndex(0);
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
    req.set_term(current_term);
    req.set_last_index(getCurrentIndex());
    req.set_last_term(getLastLogTerm());
    req.set_candidate(local->NodeId()); 
    node->sendVoteRequest(&req);
    return 0;
}

void Raft::raftVoteFor(RaftNode *node){
    vote_for = node->NodeId();
    persist_vote(vote_for); //TODO
}

void Raft::startElection() {
    fprintf(stderr, "election starting: %d %d, term: %d ci: %d", 
            timeout_election_, time_elapsed_, term_, getCurrentIndex());
    becomeCandidate();
}


bool Raft::shouldGrantVote(raft::VoteRequest* req) {
    if (req->term() < term_){
        return false;
    }

    if (isAlreadyVoted()) {
        return false;
    }

    int current_idx = getCurrentIndex();
    if (0 == current_idx) {
        return true;
    }

    raft::LogEntry* e = getEntryFromIndex(current_idx);
    if (e->term() < req->last_term()) {
        return true;
    }

    if (req->last_term() == e->term() && current_idx <= req->last_index()) {
        return true;
    }

    return false;
}

int Raft::sendVoteRequest(RaftNode *to_node){
    assert(to_node);
    assert(to_node != local);

    fprintf(stderr, "sending vote request to: %d", to_node->GetNodeId());

    VoteReqeust req
    req.set_term(term_);
    req.set_last_index(getCurrentIndex());
    req.set_last_term(getLastLogTerm());
    req.set_candidate(local->GetNodeId());

    {
        raft::RaftMessage msg;
        msg.set_type(raft::MSGTYPE_VOTE_REQUEST);
        msg.set_raftid(raft_id_);
        msg.set_vt_req(req);
        trans_.Send(addr_, &msg);
    }
    return 0;
}

int Raft::recvVoteResponse(const raft::VoteResponse *rsp) {
    if (!isCandidate()) {   
        return 0;
    } else if (term_ < r->term()) {   
        term_ = r->term();
        becomeFollower();
        return 0;
    } else if (term_ != r->term()) {   
        return 0;
    }

    if (rsp->granted_for()) {   
        voteFor(local_->NodeId());
        int votes = getVotesNum();
        if (votes > nodes.size()/2) {
            becomeLeader();
        }
    }

    return 0;
}

int Raft::recvVoteRequest(const raft::VoteRequest *req, raft::VoteResponse *rsp){
    if (term_ < req->term()) {
        term_ = req->term();
        becomeFollower();
    }

    if (shouldGrantVote(req)) {
        assert(!isLeader() && isCandidate());

        voteFor(req->candidate_id);
        rsp->set_grant_for(true);

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
        if (node->IsVoting() && node->HasVoteForMe()) {
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
