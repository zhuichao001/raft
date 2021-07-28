#include <iostream>
#include <string>
#include <time.h>
#include <unistd.h>
#include "application.h"
#include "lotus/address.h"
#include "raft/raft_server.h"

int main(){
    Application app;

    RaftOptions opt;
    {
        opt.addr = new address_t("0.0.0.0", 5678);
        opt.id = 171;
        opt.stm = &app;
    }

    RaftServer ras;
    ras.Create(opt, &app.raft_);

    app.Set(std::string("abc"));
    sleep(1);
    std::cout<<"get:"<<app.Get()<<std::endl;

    return 0;
}
