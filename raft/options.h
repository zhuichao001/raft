#ifndef _RAFT_OPTIONS_H_
#define _RAFT_OPTIONS_H_

#include<vector>


class address_t;
class Transport;
class RaftStateMachine;
class timedriver_t;

typedef struct {
    int raftid; //raft replicate group id

    uint64_t nodeid;
    uint64_t leader;

    address_t addr;
    Transport *tran;
    RaftStateMachine *stm;
    timedriver_t *clocker;
} RaftOptions;

#endif
