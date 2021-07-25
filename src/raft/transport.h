#include <stdio.h>
#include "lotus/dialer.h"
#include "lotus/engine.h"
#include "lotus/server.h"

class Transport {
public:
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    int Start(address_t *addr, server_t *svr){
        if (addr == nullptr){
            return -1;
        }
        servers[addr] = svr;
        eng.start(addr, svr);
        return 0;
    }

    void Stop() {
        eng.stop();
    }

    void Run(){
        eng.run();
    }

    void Send(const address_t *addr, const RaftMessage &msg){
        auto it = clients.find(addr);
        if (it==clients.end()){
            dialer_t *cli = eng.open(addr);
            clients[addr] = cli;
        }
        //TODO:serialize msg
        reqeust_t req;
        clients[addr]->call(&req, nullptr);
    }

private:
    std::map<address_t *, dialer_t *> clients;
    std::map<address_t *, server_t *> servers;
    engine_t eng;
};
