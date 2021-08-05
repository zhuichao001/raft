#ifndef _RAFT_OPTIONS_H_
#define _RAFT_OPTIONS_H_


class address_t;
class Transport;
class RaftStateMachine;
class timedriver_t;

typedef struct {
    int id; //raft replicate group id
    int nodeid;
    address_t addr;
    Transport *tran;
    RaftStateMachine *stm;
    timedriver_t *clocker;
} RaftOptions;

#endif
