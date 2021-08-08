

#include <time.h>
#include <stdlib.h>

long nowtime_us(){
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_nsec/1000;
}


int randint(int low, int high) {
    static bool inited = false;
    if(!inited){
        srand(time(NULL));
        inited = true;
    }
    return rand()%(high-low) + low;
}
            
