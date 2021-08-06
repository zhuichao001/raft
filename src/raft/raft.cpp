#include "raft.h"
#include "util.h"
#include "lotus/util.h"
#include "lotus/timedriver.h"
#include <algorithm>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

Raft::Raft(const RaftOptions &opt): 
    id_(opt.raftid),
    app_(opt.stm),
    trans_(opt.tran){
    term_ = 0;
    voted_for_ = -1;
    commit_idx_ = 0;
    applied_idx_ = 0;
    lasttime_heartbeat_ = microsec();
    timeout_election_ = 3000*1000;
    timeout_request_ = 200*1000;
    timeout_heartbeat_ = 5000*1000;
    reconf_idx_ = -1;
    state_ = RAFT_STATE::FOLLOWER;
    leader_ = nullptr;
    local_ = addRaftNode(opt.nodeid, opt.addr, true);
    ticker_ = opt.clocker->run_every(std::bind(&Raft::tick, this), 100*1000);
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
    e->set_index(1+getCurrentIndex());
    e->set_data(data);

    appendEntry(e);
    sendAppendEntries();
    return 0;
}

int Raft::changeMember(raft::RaftLogType type, const raft::Peer *peer) {
    if(reconf_idx_!=-1){
        return -1;
    }

    raft::LogEntry *e = new raft::LogEntry();
    e->set_type(type);
    e->set_term(term_);
    e->set_index(getCurrentIndex());

    std::string data;
    peer->SerializeToString(&data);
    e->set_data(data);

    reconf_idx_ = e->index();

    appendEntry(e);
    sendAppendEntries();

    if(type==raft::LOGTYPE_ADD_NODE){
        address_t addr(peer->ip().c_str(), int(peer->port()));
        addRaftNode(peer->nodeid(), addr, false);
        fprintf(stderr, "add member,nodeid:%d, port:%d\n", peer->nodeid(), peer->port());
    }else if(type==raft::LOGTYPE_REMOVE_NODE){
        delRaftNode(peer->nodeid());
        fprintf(stderr, "del member,nodeid:%d\n", peer->nodeid());
    }

    return 0;
}

void Raft::sendAppendEntries(){
    for (auto &it : nodes_) {
        RaftNode *node = it.second;
        if (node == local_) {
            continue;
        }
        sendAppendEntries(node);
    }
}

void Raft::sendAppendEntries(RaftNode *to){
    raft::AppendEntriesRequest *req = new raft::AppendEntriesRequest();
    req->set_nodeid(local_->GetNodeId());
    req->set_term(term_);
    req->set_commit(commit_idx_);

    fprintf(stderr, "|||nodeid:%d, term:%d, commit_idx:%d\n", req->nodeid(), req->term(), req->commit());

    req->set_prev_log_term(0);
    req->set_prev_log_index(0);

    int next_idx = to->GetNextIndex();
    const raft::LogEntry* nex = log_.getEntry(next_idx);
    if (nex != nullptr) {
        raft::LogEntry *e = req->add_entries();
        e->set_term(nex->term());
        e->set_index(nex->index());
        e->set_type(nex->type());
        e->set_data(nex->data());
        fprintf(stderr, "[RAFT] LOG term:%d,index:%d,type:%d\n", nex->term(), nex->index(), nex->type());
    } else {
        fprintf(stderr, "WARNING, getEntry null, next_idx=%d\n", next_idx);
    }

    if (next_idx > 1) {
        req->set_prev_log_index(next_idx - 1);
        const raft::LogEntry * prev = log_.getEntry(next_idx - 1);
        if (prev) {
            req->set_prev_log_term(prev->term());
        }
    }

    std::shared_ptr<raft::RaftMessage> msg = std::make_shared<raft::RaftMessage>();
    msg->set_type(raft::RaftMessage::MSGTYPE_APPENDLOG_REQUEST);
    msg->set_raftid(id_);
    msg->set_allocated_ae_req(req);
    trans_->Send(to->GetAddress(), msg);
}

int Raft::appendEntry(raft::LogEntry *e) {
    log_.appendEntry(e);
    if(e->type()==raft::LOGTYPE_ADD_NONVOTING_NODE ||
            e->type()==raft::LOGTYPE_ADD_NODE ||
            e->type()==raft::LOGTYPE_REMOVE_NODE ){
        reconf_idx_ = getCurrentIndex();
    }
    return 0;
}

int Raft::recvAppendEntries(const raft::AppendEntriesRequest *msg, raft::AppendEntriesResponse *rsp) {
    lasttime_heartbeat_ = microsec();

    rsp->set_nodeid(local_->GetNodeId());
    rsp->set_success(true);
    rsp->set_first_index(0);
    rsp->set_term(term_);

    //print request
    fprintf(stderr, "Receive AppendEntries local nodeid: %lx, from:%d term:%d commit:%d curidx:%d pli:%d plt:%d\n",
        local_->GetNodeId(),
        msg->nodeid(),
        msg->term(),
        msg->commit(),
        getCurrentIndex(),
        msg->prev_log_index(),
        msg->prev_log_term());

    if (isCandidate() && term_ == msg->term()) {
        voted_for_ = -1;
        becomeFollower();
        fprintf(stderr, "[RAFT] candidate become follower\n");
    } else if (term_ < msg->term()) {
        term_ = msg->term();
        rsp->set_term(msg->term());
        becomeFollower();
        fprintf(stderr, "[RAFT] become new follower\n");
    } else if (term_ > msg->term()) {
        fprintf(stderr, "msg's term:%d < local term_:%d", msg->term(), term_);
        rsp->set_current_index(getCurrentIndex());
        return -1;
    }

    if (msg->prev_log_index() > 0) {
        const raft::LogEntry *e = log_.getEntry(msg->prev_log_index());
        if (!e || msg->prev_log_index() > getCurrentIndex()) { //second condition will be false?
            fprintf(stderr, "msg's prev log is nullptr, prev_log_index:%d\n", msg->prev_log_index());
            rsp->set_current_index(getCurrentIndex());
            return -1;
        }

        if (e->term() != msg->prev_log_term()) {
            fprintf(stderr, "msg term doesn't match prev_term (ie. %d vs %d) ci:%d pli:%d", 
                    e->term(), msg->prev_log_term(), getCurrentIndex(), msg->prev_log_index());
            assert(commit_idx_ < msg->prev_log_index());
            log_.truncate(msg->prev_log_index());
            rsp->set_current_index(msg->prev_log_index() - 1);
            return -1;
        }
    }

    leader_ = nodes_[msg->nodeid()];
    fprintf(stderr, "[RAFT] msg from nodeid=%d\n", msg->nodeid());

    if (msg->entries_size()==0 && msg->prev_log_index()>0 && msg->prev_log_index()+1<getCurrentIndex()) {
        assert(commit_idx_ < msg->prev_log_index()+1);
        fprintf(stderr, "[RAFT] truncate from =%d\n", msg->prev_log_index()+1);
        log_.truncate(msg->prev_log_index()+1);
    }

    rsp->set_current_index(msg->prev_log_index());

    for (int i = 0; i < msg->entries_size(); ++i) {
        const raft::LogEntry* e = &msg->entries(i);
        int index = msg->prev_log_index() + 1 + i;
        rsp->set_current_index(index);

        const raft::LogEntry* existing = log_.getEntry(index);
        if (!existing) {
            break;
        }
        if (existing->term() != e->term()) {
            assert(commit_idx_ < index);
            log_.truncate(index);
            break;
        }
    }

    for (int i=0; i < msg->entries_size(); ++i) {
        int res = log_.appendEntry(&msg->entries(i));
        if (res == -1) {
            fprintf(stderr, "error: recvAppendEntries appendEntry failed\n");
            rsp->set_success(true);
            rsp->set_current_index(msg->prev_log_index()-1);
            return -1;
        }
        fprintf(stderr, "appendEntry, current_idx:%d\n", getCurrentIndex());
        rsp->set_current_index(msg->prev_log_index()+1+i);
    }

    if (getCurrentIndex() <= msg->commit()) {
        uint64_t last_log_idx = std::max(getCurrentIndex(), uint64_t(1));
        fprintf(stderr, "    current_index:%d, commit:%d\n", getCurrentIndex(), msg->commit());
        commit_idx_ = std::min(last_log_idx, msg->commit());
    }

    applyEntry();

    rsp->set_success(true);
    rsp->set_first_index(msg->prev_log_index() + 1);

    return 0;
}

int Raft::recvAppendEntriesResponse(const raft::AppendEntriesResponse *r) {
    if (!isLeader()) {
        return -1;
    }

    if (!local_) {
        return 0;
    }

    if (nodes_.find(r->nodeid()) == nodes_.end()){
        fprintf(stderr, "nodeid:%d not found\n", r->nodeid());
        return -1;
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
        fprintf(stderr, "recvAppendEntriesResponse failed\n");
        
        int next_idx = local_->GetNextIndex();
        if (r->current_index() < next_idx - 1) {
            local_->SetNextIndex(std::min(r->current_index() + 1, getCurrentIndex()));
        } else {
            local_->SetNextIndex(next_idx - 1);
        }

        sendAppendEntries();
        return 0;
    }

    assert(r->current_index() <= getCurrentIndex());

    RaftNode *peer = nodes_[r->nodeid()];
    peer->SetMatchIndex(r->current_index());
    peer->SetNextIndex(r->current_index() + 1);

    fprintf(stderr, "[RAFT] local, match_idx:%d, next_idx:%d\n", local_->GetMatchIndex(), local_->GetNextIndex());
    fprintf(stderr, "[RAFT] peer, match_idx:%d, next_idx:%d\n", peer->GetMatchIndex(), peer->GetNextIndex());

    // Update commit idx
    int meet = 0;
    for (auto &it : nodes_) {
        RaftNode *node = it.second;
        fprintf(stderr, "  [RAFT] for node id:%d,match_idx:%d, next_idx:%d\n", node->GetNodeId(), node->GetMatchIndex(), node->GetNextIndex());
        if(!node->IsVoting()){
            fprintf(stderr, "nodeid:%d is not voting\n", node->GetNodeId());
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

    fprintf(stderr, "meet:%d, commit:%d\n", meet, commit_idx_);

    if (nodes_.size() / 2 < meet && commit_idx_ < r->current_index()) {
        commit_idx_ = r->current_index();
    }

    fprintf(stderr, "commit:%d\n", commit_idx_);

    applyEntry();
    return 0;
}

void Raft::recvConfChangeRequest(raft::MemberChangeRequest *req, raft::MemberChangeResponse *rsp){
    changeMember(req->type(), req->mutable_peer());
    rsp->set_ok(true);
    rsp->set_term(term_);
    const address_t *addr = local_->GetAddress();
    raft::Peer *peer = new raft::Peer;
    {
        peer->set_raftid(id_);
        peer->set_nodeid(local_->GetNodeId());
        peer->set_ip(addr->ip);
        peer->set_port(addr->port);
    }
    rsp->set_allocated_peer(peer);
}

void Raft::recvConfChangeResponse(raft::MemberChangeResponse *rsp){
    raft::Peer *peer = rsp->mutable_peer();
    address_t addr(peer->ip().c_str(), int(peer->port()));
    leader_ = addRaftNode(peer->nodeid(), addr, false);
}

int Raft::applyEntry(){
    fprintf(stderr, "[RAFT] try to apply entry.\n");

    if (applied_idx_ >= commit_idx_) {
        fprintf(stderr, "[RAFT] ignore because applied_idx=%d >= commit_idx=%d.\n", applied_idx_, commit_idx_);
        return -1;
    }

    while(applied_idx_ < commit_idx_){
        int idx = applied_idx_ + 1;
        const raft::LogEntry *e = log_.getEntry(idx);
        if (!e) {
            return -1;
        }

        fprintf(stderr, "applying log: %d, id: %d size: %d", applied_idx_, e->index(), e->data().size());

        app_->Apply(e->data());
        ++applied_idx_;

        if (idx == reconf_idx_){
            reconf_idx_ = -1;
        }
    }
    return 0;
}

void Raft::tick(){
    switch(state_){
        case RAFT_STATE::FOLLOWER:
            if (microsec()-lasttime_heartbeat_ >= timeout_heartbeat_) {
                fprintf(stderr, "[RAFT] tick timeout, follower startElection\n");
                startElection();
            }
            break;
        case RAFT_STATE::CANDIDATE:
            if (microsec()-lasttime_election_ >= timeout_election_) {
                fprintf(stderr, "[RAFT] tick timeout, candidate restart Election\n");
                startElection();
            }
            break;
        case RAFT_STATE::LEADER:
            if (microsec()-lasttime_heartbeat_ >= 2*timeout_heartbeat_/3) {
                lasttime_heartbeat_ = microsec();
                fprintf(stderr, "[RAFT] tick timeout, leader send heartbeat\n");
                sendAppendEntries(); //heartbeat
            }
            break;
        default:
            return;
    }
}

void Raft::becomeCandidate(){
    term_ += 1;
    fprintf(stderr, "[RAFT] becoming candidate, term:%d\n", term_);

    for (auto &it : nodes_) {
        RaftNode *node = it.second;
        node->VoteForMe(false); //clear flag
    }

    voteFor(local_->GetNodeId());
    leader_ = nullptr;

    setState(RAFT_STATE::CANDIDATE);

    for (auto &it : nodes_) {
        RaftNode *node = it.second;
        if (node!=local_ && node->IsVoting()){
            sendVoteRequest(node);
        }
    }

    if(nodes_.size()==1){
        becomeLeader();
    }
}

void Raft::becomeLeader(){ //for candidator
    fprintf(stderr, "becoming leader term:%d\n", term_);
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
    lasttime_heartbeat_ = microsec();
    setState(RAFT_STATE::FOLLOWER);
    fprintf(stderr, "becoming follower, leader:%d, term:%d\n", leader_->GetNodeId(), term_);
}

int Raft::voteFor(const int nodeid){
    voted_for_ = nodeid;
    std::map<const int, RaftNode*>::iterator it = nodes_.find(nodeid);
    if(it==nodes_.end()){
        return -1;
    }
    return 0;
}

void Raft::startElection() {
    lasttime_election_ = microsec();
    becomeCandidate();
}

bool Raft::shouldGrantVote(const raft::VoteRequest* req) {
    if (req->term() < term_){
        return false;
    }

    if (isAlreadyVoted()) {
        return false;
    }

    int current_idx = getCurrentIndex();
    if (current_idx==0) {
        return true;
    }

    const raft::LogEntry *e = log_.getEntry(current_idx);
    if (e->term() < req->last_term()) {
        return true;
    }

    if (req->last_term() == e->term() && current_idx <= req->last_index()) {
        return true;
    }

    return false;
}

int Raft::sendVoteRequest(RaftNode *to){
    assert(to);
    assert(to != local_);

    fprintf(stderr, "%d sending requestvote to: %d", local_->GetNodeId(), to->GetNodeId());

    raft::VoteRequest *req = new raft::VoteRequest;
    req->set_term(term_);
    req->set_last_index(getCurrentIndex());
    req->set_last_term(getLastLogTerm());
    req->set_candidate(local_->GetNodeId());
    {
        std::shared_ptr<raft::RaftMessage> msg = std::make_shared<raft::RaftMessage>();
        msg->set_type(raft::RaftMessage::MSGTYPE_VOTE_REQUEST);
        msg->set_raftid(id_);
        msg->set_allocated_vt_req(req);
        trans_->Send(to->GetAddress(), msg);
    }
    return 0;
}

int Raft::recvVoteResponse(const raft::VoteResponse *r) {
    if (!isCandidate()) {   
        return 0;
    } 

    if (term_ < r->term()) {   
        term_ = r->term();
        becomeFollower();
        return 0;
    } else if (term_ != r->term()) {   
        return 0;
    }

    if (r->agree()) {   
        voteFor(local_->GetNodeId());
        int votes = getVotesNum();
        if (votes > nodes_.size()/2) {
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

        voteFor(req->candidate());
        rsp->set_agree(true);

        leader_ = nullptr;
    } else {
        rsp->set_agree(false);
    }

    rsp->set_term(term_);
    return 0;
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

    if (voted_for_ == local_->GetNodeId()) {
        votes += 1;
    }

    return votes;
}

RaftNode *Raft::addRaftNode(int nodeid, const address_t &addr, bool is_self, bool is_voting){
    if (nodes_.find(nodeid) != nodes_.end()){
        return nodes_[nodeid];
    }

    nodes_[nodeid] = new RaftNode(nodeid, addr);
    nodes_[nodeid]->SetVoting(is_voting);
    if(is_self){
        local_ = nodes_[nodeid];
    }
    return nodes_[nodeid];
}

int Raft::delRaftNode(int nodeid){
    if (nodes_.find(nodeid) != nodes_.end()){
         delete nodes_[nodeid];
    }
    nodes_.erase(nodeid);
    return 0;
}
