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

        if(e->index() <= 0){
            return -1;
        }

        while(!entries_.empty() && e->index() < entries_.back()->index()){
            fprintf(stderr, "[RAFT-LOG]when  puhsh, pop a entry\n");
            entries_.pop_back();
        }
        entries_.push_back(e);

        fprintf(stderr, "[RAFT-LOG] RaftLog::appendEntry, currentIndex:%d e.term:%d, e.index:%d\n", getCurrentIndex(), e->term(), e->index());
        return 0;
    }

    const raft::LogEntry *getEntry(int idx){
        assert(idx>0);
        if(idx>base_idx_+entries_.size() || idx<base_idx_){
            return nullptr;
        }

        auto e = entries_[idx-1-base_idx_];
        fprintf(stderr, "[RAFT-LOG] RaftLog::getEntry, Index:%d e.term:%d, e.index:%d\n", idx, e->term(), e->index());
        return e;
    }

    void truncate(int idx){
        fprintf(stderr, "[RAFT-LOG]truncate idx:%d\n", idx);
        idx -= 1+base_idx_;
        if(idx>=0 && idx<entries_.size()){
            entries_.erase(entries_.begin()+idx, entries_.end());
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
