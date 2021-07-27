#ifndef _RAFT_SERVER_H_
#define _RAFT_SERVER_H_

#include <stdio.h>
#include <string>
#include "lotus/server.h"
#include "lotus/protocol.h"
#include "proto/raftmsg.pb.h"
#include "transport.h"
#include "raft.h"
#include "raft_sm.h"
#include "options.h"



class RaftServer : public server_t {
public:
    RaftServer():
        trans_(new Transport(this)){
    }

    int Create(const RaftOptions &opt, Raft **raft) {
        if(raft==nullptr){
            return -1;
        }

        rafts_[opt.id] = new Raft(opt);

        trans_->Start(opt.addr, dynamic_cast<server_t*>(this));
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
        trans_->Run();
    }

    void Stop() {
        trans_->Stop();
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
    std::map<uint64_t, Raft*> rafts_;
    std::unique_ptr<Transport> trans_;
};

#endif
