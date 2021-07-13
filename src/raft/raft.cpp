
#include "raft.h"

Raft::Raft(RaftFSM *app): app_(app){
    current_term_ = 0;
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

    msg_appendentries_t ae;
    ae.term = me->current_term;
    ae.leader_commit = get_commit_idx(me_);

    msg_entry_t mety;
    int nex_idx = node->get_next_idx();
    raft_entry_t* ety = getEntry(next_idx);
    if (ety) {
        mety.term = ety->term;
        mety.id = ety->id;
        mety.type = ety->type;
        mety.data.len = ety->data.len;
        mety.data.buf = ety->data.buf;
        ae.entries = &mety;
        ae.n_entries = 1; //TODO send more than 1
    }

    if (next_idx>1) {
        raft_entry_t* prev_ety = getEntry(next_idx - 1);
        ae.prev_log_idx = next_idx - 1;
        if (prev_ety) {
            ae.prev_log_term = prev_ety->term;
        }
    }

    node->sendAppendentries(&ae);
    return 0;
}

int Raft::appendEntry(LogEntry *e) {
    if(e->isConfigChange()){
        reconf_idx_ = _log.getCurrentIndex();
    }
    return _log.appendEntry(e);
}

int Raft::recvAppendentries (
    RaftNode* node,
    AppendEntriesRequest *msg,
    AppendEntriesResponse *rsp
    ) {
    rsp->success = 0;
    rsp->first_idx = 0;
    rsp->term = current_term_;

    time_elapsed = 0;

    if (0 < msg->n_entries) { //print request
       fprintf(stderr, "Receive appendentries from: %lx, t:%d ci:%d lc:%d pli:%d plt:%d #%d",
           node->NodeId(),
           msg->term,
           raft_get_current_idx(),
           msg->leader_commit,
           msg->prev_log_idx,
           msg->prev_log_term,
           msg->n_entries);
    }

    if (isCandidate() && current_term_ == msg->term) {
        voted_for_ = -1;
        becomeFollower();
    } else if (current_term_ < msg->term) {
        current_term_ = msg->term;
        rsp->term = msg->term;
        becomeFollower();
    } else if (current_term_ > msg->term) {
        fprintf(stderr, "msg's term:%d < current_term_:%d", msg->term, current_term);
        rsp->current_idx = log_.getCurrentInex();
        return -1;
    }

    if (msg->prev_log_idx > 0) {
        LogEntry *e = log_.getEntry(msg->prev_log_idx);
        if (!e || msg->prev_log_idx > log_.getCurrentIndex()) { //second condition will be false?
            rsp->current_idx = log_.getCurrentIndex();
            return -1;
        }

        if (e->term != msg->prev_log_term) {
            fprintf("msg term doesn't match prev_term (ie. %d vs %d) ci:%d pli:%d", e->term, msg->prev_log_term, 
            _get_current_idx(me_), msg->prev_log_idx);
            assert(commit_idx < msg->prev_log_idx);
            log_.delFrom(msg->prev_log_idx);
            rsp->current_idx = msg->prev_log_idx - 1;
            return -1;
        }
    }

    if (msg->n_entries == 0 && 0 < msg->prev_log_idx && msg->prev_log_idx + 1 < log_.getCurrentIndex())
    {
        assert(commit_idx_ < msg->prev_log_idx + 1);
        log_.delFrom(msg->prev_log_idx + 1);
    }

    rsp->current_idx = msg->prev_log_idx;

    for (int i = 0; i < msg->n_entries; ++i) {
        LogEntry* e = &msg->entries[i];
        int index = msg->prev_log_idx + 1 + i;
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

    for (; i < msg->n_entries; i++) {
        int e = log_.appendEntry(&msg->entries[i]);
        if (-1 == e) {
            rsp->current_idx = msg->prev_log_idx - 1;
            return -1;
        }
        rsp->current_idx_ = msg->prev_log_idx + 1 + i;
    }

    if (log_.getCurrentIndex() < msg->leader_commit) {
        int last_log_idx = max(log_.getCurrentIndex(), 1);
        commit_idx_ = min(last_log_idx, msg->leader_commit);
    }

    leader_ = node_;

    rsp->success = 1;
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

void Raft::electionStart(){
    fprintf(stderr, "election starting: %d %d, term: %d ci: %d", 
            election_timeout, time_elapsed, current_term, local->get_current_idx());

    becomeCandidate();
}

void Raft::becomeCandidate(){
    fprintf(stderr, "becoming candidate");
    local->incr_current_term();

    for (int i = 0; i < num_nodes; i++) {
        nodes[i]->VoteForMe(0);
    }

    raft_vote(me_, me->node);
    leader = NULL;

    setState(RAFT_STATE_CANDIDATE);

    timeout_elapsed = rand() % me->election_timeout;

    for (i = 0; i < me->num_nodes; i++) {
        if (nodes[i]!=local && nodes[i]->isVoting()){
            sendRequestVote(nodes[i]);
        }
    }
}

void Raft::sendRequestVode(RaftNode * node){
    msg_requestvote_t req;

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
}

int Raft::applyEntry()
    if (applied_idx >= commit_idx) {
        return -1;
    }

    int log_idx = applied_idx + 1;
    raft_entry_t* e = getEntry(log_idx);
    if (!e) {
        return -1;
    }

    fprintf(stderr, "applying log: %d, id: %d size: %d", applied_idx, e->id, e->data.len);

    Apply(e);
    applied_idx++;

    if (log_idx == reconf_log_idx){
        reconf_log_idx = -1;
    }
    return 0;
}
