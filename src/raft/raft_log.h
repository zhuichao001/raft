#include<dequeue>


class RaftLog {
public:
    RaftLog(){
        base_idex = 0;
    }

    int appendEntry(LogEntry *e){
        if(e->id <= 0){
            return -1;
        }

        while(!entries.empty() && e->id < entries.back()->id){
            entries.pop_back();
        }
        entries.push_back(e);
        return 0;
    }

    LogEntry *getEntry(int idx){
        assert(idx>0);
        if(idx>=base_idx+entries.size() || idx<base_idx){
            return nullptr;
        }
        return entries[idx-base_idx];
    }

    void delFrom(int idx){
        idx -= (1+base_idx);
        if(idx>=0 && idx<entries.size()){
            entries.erase(entries.begin()+idx, entries.end());
        }
    }

    int getCurrentIndex(){
        return base_idx + entries.size();
    }

private:
    std::dequeue<LogEntry*> entries;
    int base_idx; //start index
};
