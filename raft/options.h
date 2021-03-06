#ifndef _RAFT_OPTIONS_H_
#define _RAFT_OPTIONS_H_

#include<vector>


class address_t;
class Transport;
class RaftStateMachine;
class timedriver_t;

typedef struct {
    int raftid; //raft replicate group id

    int32_t nodeid;
    int32_t leader;

    address_t addr;
    Transport *tran;
    RaftStateMachine *stm;
    timedriver_t *clocker;
} RaftOptions;

#endif
