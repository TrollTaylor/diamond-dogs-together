#include "mgscoop/worker_debug.hpp"
#include "mgscoop/debug_menu.hpp"
#include <cstdio>
#include <cstring>
#include <string>
int main() {
    using namespace mgscoop::worker_debug;
    constexpr char run[]="0123456789abcdef0123456789abcdef";
    const std::string prefix=std::string("MGSWORKERDEBUG1 ")+run+" host ";
    Request request{};unsigned checks=0;
    auto check=[&](bool value){++checks;if(!value){std::printf("failed %u\n",checks);return false;}return true;};
    const auto valid=prefix+"1 1001 30010 2 weather rain\n";
    if(!check(Parse(valid.data(),valid.size(),run,"host",request) && request.sequence==1 && !std::strcmp(request.text,"weather rain")))return 1;
    if(!check(Timely(request,1000,0)) || !check(!Timely(request,1001,0)) || !check(!Timely(request,1000,1)))return 1;
    request.expires=16001;if(!check(!Timely(request,1000,0)))return 1;
    for(const auto& text:{prefix+"0 1001 30010 2 time noon",prefix+"1 0 30010 2 time noon",prefix+"1 1001 30010 2 ",prefix+"1 1001 30010 2  time noon",prefix+"1 1001 30010 2 time noon ",prefix+"1 1001 30010 2 time\nnoon",prefix+"1 1001 30010 2 time noon\r\n",prefix+"18446744073709551616 1001 30010 2 time noon",prefix+"1 1001 30010 2 "+std::string(201,'x')}) {
        if(!check(!Parse(text.data(),text.size(),run,"host",request)))return 1;
    }
    if(!check(!Parse(valid.data(),valid.size(),run,"guest",request)))return 1;
    std::string nul=prefix+"1 1001 30010 2 time noon";nul[20]='\0';if(!check(!Parse(nul.data(),nul.size(),run,"host",request)))return 1;
    for(const auto& text:{prefix+"1 1001 0 2 position",prefix+"1 1001 30010 0 position",prefix+"1 1001 65536 2 position",prefix+"1 1001 30010 4294967296 position",prefix+"1 1001 30010 2 "+std::string(201,'x')}) {
        if(!check(!Parse(text.data(),text.size(),run,"host",request)))return 1;
    }
    namespace d=mgscoop::debug_menu;
    d::Configure(true,true,true);d::BeginMission(30010);const auto before=d::GetSnapshot();
    if(!check(d::QueueCommandForGeneration("position",before.mission,before.generation)))return 1;
    d::Command popped{};if(!check(d::Pop(popped)))return 1;
    d::BeginMission(30020);
    if(!check(!d::QueueCommandForGeneration("position",before.mission,before.generation)))return 1;
    const auto after=d::GetSnapshot();
    if(!check(!d::QueueCommandForGeneration("position",before.mission,after.generation)) || !check(!d::QueueCommandForGeneration("position",after.mission,0)))return 1;
    if(!check(d::QueueCommandForGeneration("position",after.mission,after.generation)))return 1;
    if(!check(d::Pop(popped) && popped.generation==after.generation))return 1;
    std::printf("worker debug policy %u PASS; gameplay command validation remains the existing debug queue\n",checks);return 0;
}
