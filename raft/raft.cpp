#include "raft.h"
#include "lotus/util.h"
#include "lotus/timedriver.h"
#include <algorithm>
#include <string>
#include <stdio.h>
#include <time.h>
#include <unistd.h>


raft::LogEntry *newLogEntry(raft::RaftLogType type, uint64_t term, uint64_t index, const std::string &data){
    raft::LogEntry *e = new raft::LogEntry();
    e->set_type(type);
    e->set_term(term);
    e->set_index(index);
    e->set_data(data);
    return e;
}

Raft::Raft(const RaftOptions &opt): 
    id_(opt.raftid),
    app_(opt.stm),
    trans_(opt.tran){

    term_ = 0;
    voted_for_ = -1;
    state_ = raft::FOLLOWER;

    commit_idx_ = 0;
    applied_idx_ = 0;
    reconf_idx_ = -1;

    leader_ = nullptr;
    local_ = addRaftNode(opt.nodeid, opt.addr, true);

    lasttime_heartbeat_ = microsec();
    lasttime_election_ = 0;

    timeout_election_ =  randTimeoutElection();
    timeout_request_ = 200*1000;
    timeout_heartbeat_ = 5000*1000;
    ticker_ = opt.clocker->run_every(std::bind(&Raft::tick, this), 100*1000);

    stoped_ = false;
}

int Raft::Propose(const std::string &data){
    if(!IsLeader()){
        return -1;
    }

    if(IsStoped()){
        return -1;
    }

    if(nodes_.size()==1){ //FIXME
        app_->Apply(data, ++applied_idx_, RAFT_OK);
        return 0;
    }

    raft::LogEntry * e = newLogEntry(raft::LOGTYPE_NORMAL, term_, 1+getCurrentIndex(), data);
    writeAhead(e);
    sendAppendEntries();
    return 0;
}

int Raft::writeAhead(raft::LogEntry *e) {
    log_.appendEntry(e);
    if(e->type()==raft::LOGTYPE_ADD_NONVOTING_NODE ||
            e->type()==raft::LOGTYPE_ADD_NODE ||
            e->type()==raft::LOGTYPE_REMOVE_NODE ){
        reconf_idx_ = getCurrentIndex();
    }
    return 0;
}

void Raft::sendAppendEntries(){
    for (auto &it : nodes_) {
        RaftNode *node = it.second;
        if (node == local_) {
            continue;
        }
        sendAppendEntriesTo(node);
    }
}

void Raft::sendAppendEntriesTo(RaftNode *to){
    auto req = new raft::AppendEntriesRequest;
    req->set_nodeid(local_->GetNodeId());
    req->set_term(term_);
    req->set_commit(commit_idx_);

    fprintf(stderr, "[Raft] leader append entries,  nodeid:%d term:%d commit_idx:%d\n", req->nodeid(), req->term(), req->commit());

    int next_idx = to->GetNextIndex();
    const raft::LogEntry* nex = log_.getEntry(next_idx);
    if (nex != nullptr) {
        raft::LogEntry *e = req->add_entries();
        e->set_term(nex->term());
        e->set_index(nex->index());
        e->set_type(nex->type());
        e->set_data(nex->data());
        fprintf(stderr, "[RAFT] leader prepare a LOG term:%d,index:%d,type:%d\n", nex->term(), nex->index(), nex->type());
    } else {
        //TODO: NEED TO SNAPSHOT
        fprintf(stderr, "WARNING, getEntry null, next_idx=%d\n", next_idx);
    }

    req->set_prev_log_term(0);
    req->set_prev_log_index(0);
    if (next_idx > 1) {
        int prev_idx = next_idx - 1;
        req->set_prev_log_index(prev_idx);
        const raft::LogEntry * prev = log_.getEntry(prev_idx);
        if (prev) {
            req->set_prev_log_term(prev->term());
        } else {
            fprintf(stderr, "WARNING, get prev Entry null, prev_idx=%d\n", prev_idx);
        }
    }

    auto msg = std::make_shared<raft::RaftMessage>();
    msg->set_raftid(id_);
    msg->set_allocated_ae_req(req);
    trans_->Send(to->GetAddress(), msg);
}

int Raft::recvAppendEntries(const raft::AppendEntriesRequest *msg, raft::AppendEntriesResponse *rsp) {
    lasttime_heartbeat_ = microsec();
    rsp->set_success(true);
    rsp->set_nodeid(local_->GetNodeId());
    rsp->set_first_index(0);
    rsp->set_term(term_);

    fprintf(stderr, "Receive AppendEntries local-nodeid: %lx, from:%d term:%d commit:%d curidx:%d pli:%d plt:%d\n",
        local_->GetNodeId(),
        msg->nodeid(),
        msg->term(),
        msg->commit(),
        getCurrentIndex(),
        msg->prev_log_index(),
        msg->prev_log_term());

    if(nodes_.find(msg->nodeid()) == nodes_.end()){
        rsp->set_success(-1);
        return -1;
    }

    if (isCandidate() && term_ == msg->term()) {
        fprintf(stderr, "[RAFT] candidate become follower\n");
        voted_for_ = -1;
        becomeFollower(nodes_[msg->nodeid()]);
    } else if (term_ < msg->term()) { //join
        fprintf(stderr, "[RAFT] become new follower because less term\n");
        term_ = msg->term();
        becomeFollower(nodes_[msg->nodeid()]);
    } else if (term_ > msg->term()) {
        fprintf(stderr, "[RAFT] discard, msg's term:%d < local term_:%d", msg->term(), term_);
        rsp->set_current_index(getCurrentIndex());
        return -1;
    }

    if (msg->prev_log_index() > 0) {
        fprintf(stderr, "prev_log_index:%d\n", msg->prev_log_index());
        const raft::LogEntry *e = log_.getEntry(msg->prev_log_index());
        if (!e || msg->prev_log_index() > getCurrentIndex()) { //second condition will be false?
            fprintf(stderr, "msg's prev log is nullptr, prev_log_index:%d\n", msg->prev_log_index());
            rsp->set_current_index(getCurrentIndex());
            return -1;
        }

        if (e->term() != msg->prev_log_term()) {
            fprintf(stderr, "msg term doesn't match prev_term (ie. %d vs %d) ci:%d cix:%d pli:%d\n", 
                    e->term(), msg->prev_log_term(), getCurrentIndex(), commit_idx_, msg->prev_log_index());
            assert(commit_idx_ < msg->prev_log_index());
            log_.truncate(msg->prev_log_index());
            rsp->set_current_index(msg->prev_log_index() - 1);
            return -1;
        }
    }

    fprintf(stderr, "[RAFT] msg from nodeid=%d\n", msg->nodeid());

    if (msg->entries_size()==0 && msg->prev_log_index()>0 && msg->prev_log_index()+1<getCurrentIndex()) {
        assert(commit_idx_ < msg->prev_log_index()+1);
        fprintf(stderr, "[RAFT] truncate from =%d\n", msg->prev_log_index()+1);
        log_.truncate(msg->prev_log_index()+1);
    }

    rsp->set_term(msg->term());
    rsp->set_current_index(msg->prev_log_index());

    int i = 0;
    for (; i < msg->entries_size(); ++i) {
        int index = msg->prev_log_index() + 1 + i;
        const raft::LogEntry* existing = log_.getEntry(index);
        if (existing==nullptr) {
            break;
        }

        const raft::LogEntry* e = &msg->entries(i);
        if (existing->term() != e->term()) {
            assert(commit_idx_ < index);
            log_.truncate(index);
            break;
        }
    }

    for (; i < msg->entries_size(); ++i) {
        auto e = &msg->entries(i);
        fprintf(stderr, "append log entry, cur_index:%d term:%d index:%d\n", getCurrentIndex(), e->term(), e->index());
        raft::LogEntry * entry = newLogEntry(e->type(), e->term(), e->index(), e->data());
        int res = writeAhead(entry);
        if (res == -1) {
            fprintf(stderr, "error: recvAppendEntries writeAhead failed\n");
            rsp->set_current_index(msg->prev_log_index()-1);
            return -1;
        }
        rsp->set_current_index(msg->prev_log_index()+1+i);
    }

    if (getCurrentIndex() <= msg->commit()) {
        uint64_t last_log_idx = std::max(getCurrentIndex(), uint64_t(1));
        commit_idx_ = std::min(last_log_idx, msg->commit());
        fprintf(stderr, "    set commit:%d\n", commit_idx_);
    }

    applyEntry();
    rsp->set_first_index(msg->prev_log_index() + 1);
    return 0;
}

int Raft::recvAppendEntriesResponse(const raft::AppendEntriesResponse *r) {
    if (!isLeader()) {
        return -1;
    }

    if (local_==nullptr) {
        return 0;
    }

    if (nodes_.find(r->nodeid()) == nodes_.end()) {
        fprintf(stderr, "nodeid:%d not found\n", r->nodeid());
        return -1;
    }
    RaftNode *peer = nodes_[r->nodeid()];

    if (r->current_index() != 0 && r->current_index() <= local_->GetMatchIndex()) {
        return 0;
    }

    if (term_ < r->term()) {
        term_ = r->term();
        becomeFollower(peer); //initial state
        return 0;
    } else if (term_ > r->term()) {
        return 0;
    }

    if (!r->success()) {
        fprintf(stderr, "recvAppendEntriesResponse failed\n");
        
        int next_idx = peer->GetNextIndex();
        if (r->current_index() < next_idx - 1) {
            peer->SetNextIndex(std::min(r->current_index() + 1, getCurrentIndex()));
        } else {
            peer->SetNextIndex(next_idx - 1);
        }

        sendAppendEntriesTo(peer);
        return 0;
    }

    assert(r->current_index() <= getCurrentIndex());

    peer->SetMatchIndex(r->current_index());
    peer->SetNextIndex(r->current_index() + 1);

    fprintf(stderr, "[RAFT] peer, match_idx:%d, next_idx:%d\n", peer->GetMatchIndex(), peer->GetNextIndex());

    updateCommitIndex(r->current_index());

    applyEntry();
    return 0;
}

void Raft::updateCommitIndex(int peer_matchidx){
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
            if (e->term() == term_ && peer_matchidx <= match_idx) {
                ++meet;
            }
        }
    }

    fprintf(stderr, "meet:%d, commit:%d\n", meet, commit_idx_);

    if (nodes_.size() / 2 < meet && commit_idx_ < peer_matchidx) {
        commit_idx_ = peer_matchidx;
    }

    fprintf(stderr, "commit:%d\n", commit_idx_);
}

int Raft::applyEntry(){
    fprintf(stderr, "[RAFT apply] try to apply entry.\n");

    if (applied_idx_ >= commit_idx_) {
        fprintf(stderr, "[RAFT apply] ignore because applied_idx=%d >= commit_idx=%d.\n", applied_idx_, commit_idx_);
        return -1;
    }

    while(applied_idx_ < commit_idx_){
        int idx = applied_idx_ + 1;
        const raft::LogEntry *e = log_.getEntry(idx);
        if (!e) {
            return -1;
        }

        if(e->type()==raft::LOGTYPE_NORMAL){
            fprintf(stderr, "[RAFT apply] normal applied_idx: %d, logidx: %d size: %d\n", applied_idx_, e->index(), e->data().size());
            app_->Apply(e->data(), idx, RAFT_OK);
        }else if(e->type()==raft::LOGTYPE_ADD_NODE){
            raft::Peer peer;
            peer.ParseFromString(e->data());
            if(peer.nodeid()!=local_->GetNodeId()){
                address_t addr(peer.ip().c_str(), peer.port());
                addRaftNode(peer.nodeid(), addr, false);
                app_->ApplyMemberChange(peer, ADD_PEER, idx, RAFT_OK);
            }
            fprintf(stderr, "[RAFT apply] confchange peer, add nodeid:%d, ip:%s, port:%d\n", peer.nodeid(), peer.ip().c_str(), peer.port());
            printRaftNodes();
        }else if(e->type()==raft::LOGTYPE_REMOVE_NODE){
            raft::Peer peer;
            peer.ParseFromString(e->data());
            delRaftNode(peer.nodeid());
            if(peer.nodeid()==local_->GetNodeId()){
                stoped_ = true;
            }
            app_->ApplyMemberChange(peer, DEL_PEER, idx, RAFT_OK);
            fprintf(stderr, "[RAFT apply] confchange peer, del nodeid:%d\n", peer.nodeid());
            printRaftNodes();
        }
        ++applied_idx_;

        if (idx == reconf_idx_){
            reconf_idx_ = -1;
        }
    }
    return 0;
}

int Raft::membersList(raft::MembersListResponse *rsp){
    rsp->set_success(true);
    rsp->set_term(term_);
    rsp->set_raftid(id_);
    for (auto &it : nodes_) {
        RaftNode *node = it.second;
        raft::Peer * p = rsp->add_peers();
        p->set_raftid(id_);
        p->set_nodeid(node->GetNodeId());
        p->set_ip(node->GetAddress()->ip);
        p->set_port(node->GetAddress()->port);
        p->set_state(node->GetState());
    }
    return 0;
}

int Raft::membersChange(const raft::RaftLogType &type, const raft::Peer &p){
    if(!IsLeader()){
        return -1;
    }

    if(IsStoped()){
        return -1;
    }

    if(reconf_idx_!=-1){
        fprintf(stderr, "warning, last MembersChange is not complete.\n");
        return -1;
    }

    fprintf(stderr, "[On ChangeMember] raftid:%d, nodeid:%d, ip:%s, port:%d\n", p.raftid(), p.nodeid(), p.ip().c_str(), p.port());

    std::string data;
    p.SerializeToString(&data);
    raft::LogEntry * e = newLogEntry(type, term_, 1+getCurrentIndex(), data);
    reconf_idx_ = e->index();
    writeAhead(e);
    sendAppendEntries();
    return 0;
}

void Raft::tick(){
    switch(state_){
        case raft::FOLLOWER:
            if (microsec()-lasttime_heartbeat_ >= timeout_heartbeat_) {
                fprintf(stderr, "[RAFT] tick timeout, FOLLOWER startElection\n");
                startElection();
            }
            break;
        case raft::CANDIDATE:
            if (microsec()-lasttime_election_ >= timeout_election_) {
                fprintf(stderr, "[RAFT] tick timeout, CANDIDATE restart Election\n");
                startElection();
            }
            break;
        case raft::LEADER:
            if (microsec()-lasttime_heartbeat_ >= 2*timeout_heartbeat_/3) {
                lasttime_heartbeat_ = microsec();
                fprintf(stderr, "[RAFT] tick timeout, LEADER send heartbeat\n");
                sendAppendEntries(); //heartbeat
            }
            break;
        default:
            return;
    }
}

void Raft::becomeLeader(){ //for candidator
    fprintf(stderr, "becoming leader term:%d\n", term_);

    if(leader_!=nullptr){
        leader_->SetState(raft::FOLLOWER);
    }
    local_->SetState(raft::LEADER);

    leader_ = local_;
    clearVotes();

    for (auto & it : nodes_) {
        RaftNode * node = it.second;
        if (local_ == node || !node->IsVoting()) {
            continue;
        }

        node->SetNextIndex(getCurrentIndex() + 1);
        node->SetMatchIndex(0);
        sendAppendEntriesTo(node);
    }

    app_->OnTransferLeader(true);
}

void Raft::becomeFollower(RaftNode *senior){
    if(IsLeader()){
        app_->OnTransferLeader(false);
    }

    leader_ = senior;
    if(leader_!=nullptr){
        leader_->SetState(raft::LEADER);
    }

    local_->SetState(raft::FOLLOWER);

    lasttime_heartbeat_ = microsec();
    fprintf(stderr, "[RAFT] becoming follower, leader:%d, term:%d\n", leader_->GetNodeId(), term_);
}

void Raft::becomeCandidate(){
    if(leader_!=nullptr){
        leader_->SetState(raft::UNKNOWN); //unclear previous leader's state
    }
    leader_ = nullptr;

    local_->SetState(raft::CANDIDATE);
    timeout_election_ =  randTimeoutElection();

    term_ += 1;
    fprintf(stderr, "[RAFT] becoming candidate, term:%d\n", term_);

    leader_ = nullptr;
    clearVotes();
    voteBy(local_->GetNodeId());
    if(nodes_.size()==1){
        becomeLeader();
        return;
    }

    for (auto &it : nodes_) {
        RaftNode *node = it.second;
        if (node!=local_ && node->IsVoting()){
            sendVoteRequest(node);
        }
    }
}

int Raft::voteBy(const int nodeid){
    auto it = nodes_.find(nodeid);
    if(it==nodes_.end()){
        return -1;
    }
    RaftNode *node = it->second;
    node->VoteForMe(true);
    voted_for_ = nodeid;
    return 0;
}

void Raft::clearVotes(){
    voted_for_ = -1;
    for (auto &it : nodes_) {
        RaftNode *node = it.second;
        node->VoteForMe(false);
    }
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
    fprintf(stderr, "sending requestvote, from %d to: %d\n", local_->GetNodeId(), to->GetNodeId());

    raft::VoteRequest *req = new raft::VoteRequest;
    {
        req->set_term(term_);
        req->set_candidate(local_->GetNodeId());
        req->set_last_index(getCurrentIndex());
        req->set_last_term(getLastLogTerm());
    }

    std::shared_ptr<raft::RaftMessage> msg = std::make_shared<raft::RaftMessage>();
    {
        msg->set_raftid(id_);
        msg->set_allocated_vt_req(req);
    }

    trans_->Send(to->GetAddress(), msg);
    return 0;
}

int Raft::recvVoteRequest(const raft::VoteRequest *req, raft::VoteResponse *rsp){
    fprintf(stderr, "receive vote response\n");

    if (term_ < req->term()) {
        fprintf(stderr, "yes %d become follower of %d\n", local_->GetNodeId(), req->candidate());
        term_ = req->term();
    }

    if (shouldGrantVote(req)) {
        //both leaded and candicate has voted for themselves, so will not grant vote
        assert(!isLeader() && !isCandidate());

        voteBy(req->candidate());
        becomeFollower(nullptr);

        rsp->set_agree(true);
        fprintf(stderr, "yes I vote %d\n", req->candidate());
    } else { 
        rsp->set_agree(false);
        fprintf(stderr, "sorry I will not vote %d\n", req->candidate());
    }

    rsp->set_nodeid(local_->GetNodeId());
    rsp->set_term(term_);
    return 0;
}

int Raft::recvVoteResponse(const raft::VoteResponse *r) {
    if (!isCandidate()) {   
        fprintf(stderr, "error on recvVoteResponse,  local is not candidate\n");
        return -1;
    }

    if (term_ < r->term()) {   
        fprintf(stderr, "yes candidate become follower, %d <%d\n", term_, r->term());
        term_ = r->term();
        becomeFollower(nullptr);
        return 0;
    } else if (term_ > r->term()) {
        //we should ignore an old message caused by bad network
        fprintf(stderr, "ignore VoteResponse %d >= %d\n", term_, r->term());
        return 0;
    }

    if (r->agree()) {   
        voteBy(r->nodeid());
        if (winQuorumVotes()) {
            becomeLeader();
        }
    }

    return 0;
}

bool Raft::winQuorumVotes() {
    int votes = 0;
    for (auto &it : nodes_) {
        RaftNode *node = it.second;
        if (node->IsVoting() && node->HasVotedForMe()) {
            votes += 1;
        }
    }
    return votes > nodes_.size()/2;
}

RaftNode *Raft::addRaftNode(int nodeid, const address_t &addr, bool is_self, bool is_voting){
    if (nodes_.find(nodeid) != nodes_.end()){ //has exist
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
    if (nodeid == local_->GetNodeId()){
        stoped_ = true;
    }

    if (leader_!=nullptr && nodeid==leader_->GetNodeId()){
        leader_ = nullptr;
    }

    if (nodes_.find(nodeid) != nodes_.end()){
         delete nodes_[nodeid];
    }
    nodes_.erase(nodeid);
    return 0;
}

void Raft::printRaftNodes(){
    for (auto it : nodes_){
        RaftNode *node = it.second;
        node->print();
    }
}
