#ifndef _RAFT_SERVER_H_
#define _RAFT_SERVER_H_

#include <string>
#include <functional>
#include <stdio.h>
#include "lotus/server.h"
#include "lotus/engine.h"
#include "lotus/protocol.h"
#include "proto/raftmsg.pb.h"
#include "transport.h"
#include "raft.h"
#include "raft_sm.h"
#include "options.h"



class RaftServer : public server_t {
public:
    RaftServer() {
        eng_ = new engine_t();
        trans_ = new Transport(eng_, this);
    }

    int Create(RaftOptions &opt, Raft **raft) {
        if(raft==nullptr){
            return -1;
        }

        trans_->Start(opt.addr, dynamic_cast<server_t*>(this));

        //why the order affects timer's regular work
        opt.clocker = eng_;
        rafts_[opt.id] = new Raft(opt);

        *raft = rafts_[opt.id];
        return 0;
    }

    int Remove(int64_t id) {
        rafts_.erase(id); //TODO
        return 0; 
    }

    Raft *GetRaft(int64_t id) {
        auto it = rafts_.find(id);
        if (it!=rafts_.end()) {
            return it->second;
        }
        return nullptr;
    }

    void Start() {
        eng_->run();
    }

    void Stop() {
        eng_->stop();
    }

public:
    int process(request_t *req, response_t *rsp) override {
        fprintf(stderr, "rpc server process.\n");
        fprintf(stderr, "rpc req=%s.\n", req->data());

        raft::RaftMessage out;
        raft::RaftMessage in;
        in.ParseFromString(req->data());

        Raft *raf = rafts_[1];

        std::string tmp;
        switch(in.type()){
            case raft::RaftMessage::MSGTYPE_APPENDLOG_REQUEST:
                raf->recvAppendEntries(&in.ae_req(), out.mutable_ae_rsp());
                out.SerializeToString(&tmp);
                rsp->setbody(tmp.c_str(), tmp.size());
                break;
            case raft::RaftMessage::MSGTYPE_VOTE_REQUEST:
                raf->recvVoteRequest(&in.vt_req(), out.mutable_vt_rsp());
                out.SerializeToString(&tmp);
                rsp->setbody(tmp.c_str(), tmp.size());
                break;
            case raft::RaftMessage::MSGTYPE_HANDSHAKE_REQUEST:
                break;
            default:
                fprintf(stderr, "unknown msg type:%d\n", in.type());
        }
        return 0;
    }

private:
    engine_t *eng_;
    std::map<uint64_t, Raft*> rafts_;
    Transport *trans_;

    friend Transport;
};

#endif
