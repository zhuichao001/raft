#ifndef _RAFT_OPTIONS_H_
#define _RAFT_OPTIONS_H_


class address_t;
class Transport;
class RaftStateMachine;

typedef struct {
    int id; //raft replicate group id
    address_t *addr;
    Transport *tran;
    RaftStateMachine * stm;
} RaftOptions;

#endif
