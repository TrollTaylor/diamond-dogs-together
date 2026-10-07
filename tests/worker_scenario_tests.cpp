#include "mgscoop/worker_scenario.hpp"
#include <cstdio>
#include <cstring>
#include <limits>
#include <initializer_list>
namespace w=mgscoop::worker_scenario;
namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
constexpr const char* id="0123456789abcdef0123456789abcdef";
w::Command Parse(const char* operation){char text[512]{};std::snprintf(text,sizeof(text),"MGSWORKER1 %s host 1 3000 %s\n",id,operation);
    w::Command c{};Check(w::ParseCommand(text,std::strlen(text),id,"host",c),operation);return c;}
}
int main(){
    Check(w::ValidRunId(id),"exact32lowerhexid");Check(!w::ValidRunId("a"),"shortid");Check(!w::ValidRunId("0123456789abcdef0123456789abcdeG"),"uppercaseid");
    for(unsigned bits=0;bits<16;++bits)Check(w::ValidAuthorization({(bits&1)!=0,(bits&2)!=0,(bits&4)!=0,(bits&8)!=0})==(bits==15),"allactualgatesrequired");
    w::Command rejected{};
    const char* malformed[]={"", "MGSWORKER1 foreign host 1 3000 release 0 0 0", "MGSWORKER1 0123456789abcdef0123456789abcdef guest 1 3000 release 0 0 0",
      "MGSWORKER1 0123456789abcdef0123456789abcdef host 1 3000 exec calc.exe 0 0", "MGSWORKER1 0123456789abcdef0123456789abcdef host 1 3000 hold f9 1 0",
      "MGSWORKER1 0123456789abcdef0123456789abcdef host 1 3000 hold w+w 1 0", "MGSWORKER1 0123456789abcdef0123456789abcdef host 1 3000 hold w 2001 0",
      "MGSWORKER1 0123456789abcdef0123456789abcdef host 1 3000 mouse 1501 0 0", "MGSWORKER1 0123456789abcdef0123456789abcdef host 1 3000 mouse 0 0 0",
      "MGSWORKER1 0123456789abcdef0123456789abcdef host 18446744073709551616 3000 release 0 0 0", "MGSWORKER1 0123456789abcdef0123456789abcdef host 1 3000 release 0 0 0 extra"};
    for(const auto* text:malformed)Check(!w::ParseCommand(text,std::strlen(text),id,"host",rejected),"boundedstricttypedparser");
    char oversized[514]{};std::memset(oversized,'a',sizeof(oversized));Check(!w::ParseCommand(oversized,sizeof(oversized),id,"host",rejected),"oversizedparser");
    w::Scenario engine;auto command=Parse("hold w+rmb+lmb 500 0");Check(engine.Accept(command,1000),"acceptheldcombo");
    std::uint8_t keys[256]{};engine.Keyboard(keys);Check(keys[0x11]==0x80&&keys[0x1c]==0,"walkonlyvirtualkeyboard");
    auto mouse=engine.Mouse(true);Check(mouse.buttons[0]==0x80&&mouse.buttons[1]==0x80&&mouse.x==0,"aimfireonlyvirtualmouse");
    Check(!engine.Accept(command,1000),"duplicatenotresubmitted");Check(engine.input_command(w::Device::keyboard)==1&&engine.input_command(w::Device::mouse)==1,"inputcommandcorrelation");
    engine.Tick(1499);engine.Keyboard(keys);Check(keys[0x11]==0x80,"holdbeforedeadline");engine.Tick(1500);engine.Keyboard(keys);mouse=engine.Mouse(true);
    Check(keys[0x11]==0&&mouse.buttons[0]==0&&mouse.buttons[1]==0,"automaticboundedrelease");
    auto movement=Parse("mouse -1500 1500 0");movement.sequence=2;Check(engine.Accept(movement,1600),"boundedrelativemove");
    mouse=engine.Mouse(false);Check(mouse.x==-1500&&mouse.y==1500,"mousepeek");mouse=engine.Mouse(true);Check(mouse.x==-1500,"mouseconsume");
    Check(engine.Mouse(true).x==0,"movementneverrepeats");
    w::Event events[64]{};const auto before=engine.Events(w::Device::keyboard,events,64,true);Check(before==1,"staledownprunedreleasepreserved");
    Check(events[0].value==0&&events[0].offset==0x11&&events[0].command==1&&!events[0].expires,"typedreleaseoriginalcommandcorrelation");
    Check(engine.Events(w::Device::keyboard,events,64,true)==before,"bufferedpeekdoesnotconsume");
    Check(engine.Events(w::Device::keyboard,events,1,false)==1&&engine.Events(w::Device::keyboard,events,64,false)==0,"boundedbufferedbatches");
    Check(engine.Events(w::Device::keyboard,events,64,false)==0,"eventsneverreplay");
    auto release=Parse("release 0 0 0");release.sequence=3;Check(engine.Accept(release,1700),"explicitrelease");
    auto capture=Parse("capture 0 0 0");capture.sequence=4;Check(engine.Accept(capture,1700),"captureaccepted");
    auto busy=capture;busy.sequence=5;Check(!engine.Accept(busy,1700),"onependingcapture");Check(engine.TakeCapture(1800).sequence==4,"capturetakenonce");
    Check(engine.TakeCapture(1800).sequence==0,"captureneverreplayed");Check(!engine.Accept(busy,1900),"capturecadence");Check(engine.Accept(busy,1950),"capturecadenceboundary");
    Check(engine.TakeCapture(3000).sequence==0,"expiredcapturerefused");
    w::Scenario admission;command.sequence=1;command.expires=1000;Check(!admission.Accept(command,1000),"expiredcommand");
    command.expires=3001;Check(!admission.Accept(command,1000),"maxfuturettl");command.expires=3000;command.duration=2001;Check(!admission.Accept(command,1000),"maxhold");
    command.duration=2000;Check(admission.Accept(command,1000),"inclusiveholdboundary");command.sequence=4097;Check(!admission.Accept(command,1000),"runcommandbudget");
    admission.Clear();admission.Keyboard(keys);Check(keys[0x11]==0&&admission.Mouse(true).buttons[0]==0,"clearrevokesheldstate");
    w::Scenario late;auto delayed=Parse("mouse 50 -70 0");delayed.expires=1200;
    Check(late.Accept(delayed,1000),"unconsumedmoveaccepted");late.Tick(1199);Check(late.Mouse(false).x==50,"pendingmovebeforeexpiry");
    late.Tick(1200);Check(late.Mouse(true).x==0&&late.Mouse(true).y==0,"pendingmousestateexpireswithoutmouseconsumer");
    Check(late.Events(w::Device::mouse,events,64,false)==0,"bufferedmovedoesnotdeliverafterexpiry");
    delayed=Parse("hold w+rmb 100 0");delayed.sequence=2;delayed.expires=1200;
    Check(late.Accept(delayed,1100),"boundeddownaccepted");Check(late.Events(w::Device::keyboard,events,64,true)==1&&events[0].expires==1200,"pressarriesboundeddeadline");
    late.Tick(1200);late.Keyboard(keys);Check(keys[0x11]==0&&!late.Mouse(false).buttons[1],"holdexpirydoesnotrequireactualdeviceconsumption");
    Check(late.Events(w::Device::keyboard,events,64,true)==1&&events[0].value==0&&events[0].command==2&&!events[0].expires,"latekeyboardrecoverswithreleaseonly");
    late.Tick(10000);Check(late.Events(w::Device::mouse,events,64,false)==1&&events[0].offset==13&&events[0].value==0,"latebuttonreleaseremainsavailablenoolddown");
    w::Scenario renewed;delayed.sequence=1;delayed.expires=3000;Check(renewed.Accept(delayed,1000),"initialbufferedrenewalhold");
    delayed.sequence=2;Check(renewed.Accept(delayed,1050),"explicitrenewalhold");renewed.Tick(1100);
    Check(renewed.Events(w::Device::keyboard,events,64,true)==1&&events[0].value==0x80&&events[0].expires==1150&&events[0].command==2,"pendingrenewedpressdeadlineupdatedwithoutduplicate");
    renewed.Tick(1150);Check(renewed.Events(w::Device::keyboard,events,64,false)==1&&events[0].value==0&&events[0].command==2,"renewedholdreleasesatnewboundeddeadline");
    w::Scenario moving;delayed=Parse("mouse 50 70 0");Check(moving.Accept(delayed,1000),"firstpendingmove");
    delayed.sequence=2;delayed.dx=-10;delayed.dy=0;Check(moving.Accept(delayed,1001),"replacependingmove");
    Check(moving.Events(w::Device::mouse,events,64,true)==1&&events[0].command==2&&events[0].offset==0,"pendingmovesneveraccumulateinstaleeventbuffer");
    release.sequence=3;Check(moving.Accept(release,1002),"explicitreleasependingdelta");Check(!moving.Mouse(false).x&&moving.Events(w::Device::mouse,events,64,false)==0,"explicitreleaseclearsstateandbuffereddelta");
    w::Scenario released;delayed=Parse("hold w 500 0");Check(released.Accept(delayed,1000),"earlyreleaseinitialhold");
    release.sequence=2;Check(released.Accept(release,1050),"earlyexplicitrelease");
    Check(released.Events(w::Device::keyboard,events,64,true)==1&&events[0].value==0&&events[0].command==2,"explicitreleaseimmediatelyremovesundelivereddown");
    delayed.sequence=3;Check(released.Accept(delayed,1060),"newholdafterrelease");delayed.sequence=4;Check(released.Accept(delayed,1070),"renewnewholdonly");
    Check(released.Events(w::Device::keyboard,events,64,true)==2&&events[0].value==0&&events[0].command==2&&!events[0].expires&&events[1].value==0x80&&events[1].command==4&&events[1].expires==1570,"renewaldoesnotreviveortagpriorreleasedpress");
    capture.sequence=5;Check(released.Accept(capture,1100),"unrelatedcaptureafterholdrenewal");released.Tick(1570);
    Check(released.Events(w::Device::keyboard,events,64,false)==2&&events[0].value==0&&events[0].command==2&&events[1].value==0&&events[1].command==4,"internalreleasecorrelatesholdnotlatestcapturecommand");
    for(int result:{0,1,-1,static_cast<int>(0x8007001eu),static_cast<int>(0x8007000cu)})
        for(unsigned gate=0;gate<4;++gate)Check(w::MayReplaceState(result,(gate&1)!=0,(gate&2)!=0)==(result>=0&&gate==3),"originalfailurecannotbecomesuccess");
    std::printf("worker_scenario checks=%u failures=%u compiled=%u\n",checks,failures,w::Compiled()?1u:0u);return failures?1:0;
}
