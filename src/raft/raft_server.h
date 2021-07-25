#include <stdio.h>
#include "lotus/server.h"
#include "lotus/protocol.h"

typedef struct {
    int id_; //group id
    char *ip;
    int port;
    RaftStateMachine * stm_;
} RaftOptions;


class RaftServer : public server_t {
public:
    RaftServer():
        trans_(new Transport()){
    }

    int Create(const RaftOptions opt, Raft **raft) {
        rafts_[opt.id] = new Raft(opt.stm_);
        address_t *addr = new address_t(opt.ip, opt.port);
        trans_.Start(addr, this);
        *raft = rafts_[opt.id];
        rafts_[opt.id]->addRaftNode(0,true); //TODO
        return 0;
    }

    int Remove(int64_t id) {
        rafts_.erase(id); //TODO
        return 0; 
    } 

    Raft *GetRaft(int64_t id) {
        auto it = rafts_.find(id);
        if (it!=rafts_.end()) {
            return it.second;
        }
        return nullptr;
    }

    void Start() {
        trans_.Run();
    }

    void Stop() {
        trans_.Stop();
    }

public
    int process(request_t *req, response_t *rsp) override {
        fprintf(stderr, "rpc server process.\n");
        fprintf(stderr, "rpc req=%s.\n", req->data());

        RaftMessage raft_req;
        raft_req.Decode(req->data, req->len);

        RaftMessage raft_rsp
        dispatch(&raft_req, &raft_rsp);

        //TODO
        raft_rsp.encode(buff, *len);

        rsp->setbody(buff, len);
        return 0;
    }

private:
    int dispatch(RaftMessage *req, RaftMessage *rsp) {
        if(req->type==MSGTYPE_APPENDLOG){
            //TODO 
        }else if(req->type==MSGTYPE_HANDSHAKE){
            //TODO 
        }else if(req->type==MSGTYPE_VOTE){
            //TODO
        }
        return 0; 
    } 

private:
    std::map<uint64_t, Raft*> rafts_;
    std::unique_ptr<Transport> trans_;
};
