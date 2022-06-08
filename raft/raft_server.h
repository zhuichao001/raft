#ifndef _RAFT_SERVER_H_
#define _RAFT_SERVER_H_

#include <string>
#include <mutex>
#include <functional>
#include <stdio.h>
#include "lotus/lotus.h"
#include "proto/raftmsg.pb.h"
#include "transport.h"
#include "raft.h"
#include "raft_sm.h"
#include "options.h"


class RaftServer : public rpc_service_t {
public:
    RaftServer(engine_t *eng):
        rpc_service_t(std::bind(&RaftServer::Process, this, std::placeholders::_1)),
        eng_(eng){
        trans_ = new Transport(eng_, this);
    }

    int Create(RaftOptions &opt, Raft **raft) {
        if(raft==nullptr){
            return -1;
        }

        //why the order affects timer's regular work
        opt.clocker = eng_;
        opt.tran = trans_;

        std::unique_lock<std::mutex> lock{mutex_};
        rafts_[opt.raftid] = new Raft(opt);

        *raft = rafts_[opt.raftid];
        return 0;
    }

    int Remove(int64_t raftid) {
        std::unique_lock<std::mutex> lock{mutex_};
        if(rafts_.find(raftid)==rafts_.end()){
            return -1;
        }
        rafts_[raftid]->Stop();
        rafts_.erase(raftid);
        return 0; 
    }

    int ChangeMember(int raftid, raft::RaftLogType type, address_t *leader_addr, address_t *local_addr, int nodeid){
        auto msg = std::make_shared<raft::RaftMessage>();
        msg->set_raftid(raftid);

        auto mc_req = new raft::MemberChangeRequest;
        mc_req->set_type(type);

        raft::Peer *p = mc_req->mutable_peer();
        p->set_raftid(raftid);
        p->set_nodeid(nodeid);
        p->set_ip(local_addr->ip);
        p->set_port(local_addr->port);

        msg->set_allocated_mc_req(mc_req);

        if(trans_->Send(leader_addr, msg)==0){
            fprintf(stderr, "SUCCESS JOIN CLUSTER raftid=%d, nodeid=%d leader=%s:%d\n", 
                    p->raftid(), p->nodeid(), p->ip().c_str(), p->port());
            return 0;
        } else {
            fprintf(stderr, "FAILED TO JOIN CLUSTER raftid=%d, nodeid=%d leader=%s:%d\n", 
                    p->raftid(), p->nodeid(), p->ip().c_str(), p->port());
            return -1;
        }
    }

    int SyncGetMemberList(int raftid, address_t *addr, std::vector<raft::Peer>){
        auto msg = std::make_shared<raft::RaftMessage>();
        msg->set_raftid(raftid);
        auto ml_req = new raft::MembersListRequest;
        ml_req->set_raftid(raftid);
        msg->set_allocated_ml_req(ml_req);
        if(trans_->Send(addr, msg)<0){
            return -1;
        }
        return 0;
    }

    void OnMembersListResponse(raft::MembersListResponse *rsp){
        for(auto p: rsp->peers()){
            if(peers_.find(rsp->raftid()) == peers_.end()){
                peers_[rsp->raftid()] = std::vector<raft::Peer*>();
            }
            raft::Peer *neo = new raft::Peer;
            neo->set_raftid(p.raftid());
            neo->set_nodeid(p.nodeid());
            neo->set_ip(p.ip());
            neo->set_port(p.port());
            neo->set_state(p.state());

            {
                std::unique_lock<std::mutex> lock{mutex_};
                peers_[rsp->raftid()].push_back(neo);
            }
        }
    }

    Raft *GetRaft(int64_t raftid) {
        std::unique_lock<std::mutex> lock{mutex_};
        auto it = rafts_.find(raftid);
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
    int Process(rpc_session_t *session) {
        rpc_response_t response;
        rpc_response_t *rsp = &response;
        rpc_request_t *req = session->request();

        auto in = std::make_shared<raft::RaftMessage>();
        auto out = std::make_shared<raft::RaftMessage>();

        in->ParseFromString(req->data());

        Raft *raft = GetRaft(in->raftid());
        if(raft==nullptr){
            rsp->seterrcode(-200);
            return -1;
        }
        out->set_raftid(in->raftid());

        if(raft->IsStoped()){
            rsp->seterrcode(-201);
            return -1;
        }

        switch(in->msg_case()){
            case raft::RaftMessage::kAeReq:
                raft->recvAppendEntries(in->mutable_ae_req(), out->mutable_ae_rsp());
                break;
            case raft::RaftMessage::kVtReq:
                raft->recvVoteRequest(&in->vt_req(), out->mutable_vt_rsp());
                break;
            case raft::RaftMessage::kMcReq:
                if(raft->membersChange(in->mc_req().type(), in->mc_req().peer())<0){
                    fprintf(stderr, "[RAFT SERVER] Change Members FAILED\n");
                }
                break;
            case raft::RaftMessage::kMlReq:
                if(raft->membersList(out->mutable_ml_rsp())<0){
                    fprintf(stderr, "[RAFT SERVER] Query Members FAILED\n");
                }
                break;
            default:
                fprintf(stderr, "[RAFT SERVER] unknown msg type:%d\n", in->msg_case());
        }

        std::string tmp;
        out->SerializeToString(&tmp);
        rsp->setbody(tmp.c_str(), tmp.size());
        session->reply(rsp);
        return 0;
    }

private:
    engine_t *eng_;

    std::map<uint64_t, Raft*> rafts_;  //guard by mutex_
    std::map<uint64_t, std::vector<raft::Peer*>> peers_; //guard by mutex_
    std::mutex mutex_;

    Transport *trans_;
    friend Transport;
};

#endif
