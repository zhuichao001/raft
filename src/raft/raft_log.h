#include<dequeue>


class RaftLog {
public:
    RaftLog(){
        base_idex = 0;
    }

    int appendEntry(RaftEntry *e){
        if(e->id <= 0){
            return -1;
        }

        while(!entries.empty() && e->id < entries.back()->id){
            entries.pop_back();
        }
        entries.push_back(e);
        return 0;
    }

    RaftEntry *getEntry(int idx){
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
    std::dequeue<RaftEntry*> entries;
    int base_idx; //start index
};
