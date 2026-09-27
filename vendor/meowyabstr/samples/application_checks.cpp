#include <meowyrender/meowyrender.hpp>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
using namespace meowyrender;
int main(){
    bool initialized=false,cleaned=false;int frames=0;
    RunApplication(160,120,"Application host checks",[&]{
        ClearBackground(BLUE);DrawText("meow",10,10,20,WHITE);if(++frames==3)RequestWindowClose();
    },[&]{initialized=IsWindowReady();},[&]{cleaned=IsWindowReady();});
    bool ok=initialized&&cleaned&&frames==3&&!IsWindowReady();
    cleaned=false;bool caught=false;
    try{RunApplication(160,120,"Exception cleanup",[]{throw std::runtime_error("expected frame failure");},{},[&]{cleaned=IsWindowReady();});}
    catch(const std::runtime_error&){caught=true;}
    ok=ok&&caught&&cleaned&&!IsWindowReady();
    std::printf("%s: automatic application lifecycle and requested close\n",ok?"PASS":"FAIL");return ok?EXIT_SUCCESS:EXIT_FAILURE;
}
