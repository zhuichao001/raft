#ifndef _RAFT_RAFTLOG_H_
#define _RAFT_RAFTLOG_H_

#include <deque>
#include "proto/raftmsg.pb.h"


class RaftLog {
public:
    RaftLog(){
        base_idx_ = 0;
    }

    int appendEntry(const raft::LogEntry *e){
        fprintf(stderr, "[RAFT] log entry, term:%d, index:%d\n", e->term(), e->index());

        if(e->index() <= 0){
            return -1;
        }

        while(!entries_.empty() && e->index() < entries_.back()->index()){
            entries_.pop_back();
        }
        entries_.push_back(e);
        return 0;
    }

    const raft::LogEntry *getEntry(int idx){
        assert(idx>0);
        if(idx>base_idx_+entries_.size() || idx<base_idx_){
            return nullptr;
        }
        return entries_[idx-1-base_idx_];
    }

    void truncate(int idx){
        idx -= 1+base_idx_;
        if(idx>0 && idx<entries_.size()){
            entries_.erase(entries_.begin()+idx-1, entries_.end());
        }
    }

    int getCurrentIndex(){
        return base_idx_ + entries_.size();
    }

private:
    std::deque<const raft::LogEntry*> entries_;
    int base_idx_; //start index
};

#endif
