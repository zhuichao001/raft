

#include<time.h>

long nowtime_us(){
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_nsec/1000;
}
