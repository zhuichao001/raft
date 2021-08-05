#include <iostream>
#include <string>
#include <thread>
#include <time.h>
#include <unistd.h>
#include "application.h"
#include "lotus/address.h"
#include "lotus/engine.h"
#include "raft/raft_server.h"

#include<stdio.h>
#include<getopt.h>

bool isleader = true;
int raftid;
int nodeid;
int port;
int leader_port; //leader address

engine_t eng;

int parse_opt(int argc, char *argv[]) {
    int index = 0;
    int c = 0;

    struct  option long_options[] = {
        {"raftid",  required_argument,  NULL, 'r'},
        {"nodeid",  required_argument,  NULL, 'i'},
        {"join",    required_argument,  NULL, 'j'},
        {"help",    no_argument,        NULL, 'h'},
    };

    while(EOF != (c = getopt_long(argc,argv,"hrij:", long_options, &index))) {
        switch(c) {
            case 'h':
                fprintf(stderr, "./server -i --join");
                break;
            case 'r':
                raftid = atoi(optarg);
                break;
            case 'i':
                nodeid = atoi(optarg);
                port = 9000+nodeid;
                break;
            case 'j':
                isleader = false;
                leader_port = atoi(optarg);
                break;
            case '?':
                printf("unknow option:%c\n",optopt);
                break;
            default:
                break;
        }
    }
    fprintf(stderr, "is_leader:%d, id:%d, port:%d, leader_port:%d\n", isleader, nodeid, port, leader_port);
    return 0;
}

int add_raft_node(){
    address_t addr("127.0.0.1", leader_port);
    dialer_t *cli = eng.open(&addr);

    raft::RaftMessage msg;
    {
        msg.set_raftid(raftid);
        msg.set_type(raft::RaftMessage::MSGTYPE_CONFCHANGE_REQUEST);

        raft::MemberChangeRequest req;
        req.set_type(raft::LOGTYPE_ADD_NODE);
        raft::Peer *peer = req.mutable_peer();
        peer->set_raftid(raftid);
        peer->set_nodeid(nodeid);
        peer->set_ip("127.0.0.1");
        peer->set_port(port);

        msg.set_allocated_mc_req(&req);
    }

    string tmp;
    msg.SerializeToString(&tmp);

    request_t req;
    req.setbody(tmp.c_str(), tmp.size());

    cli->call(&req, nullptr);
}

int main(int argc, char *argv[]){
    parse_opt(argc, argv);

    RaftServer ras(&eng);
    Application app;
    RaftOptions opt;
    {
        opt.addr = address_t("0.0.0.0", 5678);
        opt.id = raftid;
        opt.stm = &app;
    }

    ras.Create(opt, &app.raft_);
    std::thread th([=,&ras]{
        ras.Start();
    });

    if(!isleader){
        add_raft_node();
    }

    sleep(1);

    app.Set(std::string("abc"));
    sleep(1);

    std::cout<<"get:"<<app.Get()<<std::endl;
    sleep(200);

    return 0;
}
