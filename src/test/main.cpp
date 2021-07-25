#include <iostream>
#include <time.h>
#include "application.h"
#include "lotus/address.h"

int main(){
    Application app;

    RaftOptions opt;
    opt.addr_ = new address_t("0.0.0.0", 5678);
    opt.id_ = 171;
    opt.stm_ = &app;

    RaftServer ras;
    ras.Create(&opt, &app.raft_)

    app.Set("abc");
    sleep(1);
    std::cout<<app.Get()<<std::endl;

    return 0;
}
