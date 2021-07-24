#include "raft/raft.h"
#include "raft/raft_fsm.h"

class Application: public RaftFSM{
    int Apply();
}


int main(){

    return 0;
}
