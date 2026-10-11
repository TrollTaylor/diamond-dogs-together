#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <xinput.h>
#include <MinHook.h>
#include <array>
#include <cstdio>
#include <cstring>
#include "mgscoop/worker_scenario.hpp"
#include "mgscoop/debug_overlay.hpp"
namespace mgscoop::debug_overlay {
bool HookWorkerDeviceFixtureForTest(void*,void*,void*,void*) noexcept;
bool HookWorkerRawBufferFixtureForTest(void*) noexcept;
bool HookWorkerRawInputFixtureForTest(void*) noexcept;
HRESULT WorkerStateForTest(IDirectInputDevice8W*,DWORD,void*) noexcept;
HRESULT WorkerDataForTest(IDirectInputDevice8W*,DWORD,DIDEVICEOBJECTDATA*,DWORD*,DWORD) noexcept;
HRESULT WorkerFormatForTest(IDirectInputDevice8W*,const DIDATAFORMAT*) noexcept;
HRESULT WorkerCoopForTest(IDirectInputDevice8W*,HWND,DWORD) noexcept;
BOOL WorkerCursorForTest(int,int) noexcept;
void ConfigureWorkerCursorFixtureForTest(HWND,BOOL (WINAPI*)(HWND,LPRECT),BOOL (WINAPI*)(HWND,LPPOINT),BOOL (WINAPI*)(LPPOINT)) noexcept;
BOOL WorkerGetCursorForTest(LPPOINT) noexcept;
void SetWorkerPresentWindowForTest(HWND) noexcept;
BOOL WorkerClipForTest(const RECT*) noexcept;
SHORT WorkerAsyncForTest(int) noexcept;
SHORT WorkerKeyStateForTest(int) noexcept;
UINT WorkerRawBufferForTest(RAWINPUT*,UINT*,UINT) noexcept;
UINT WorkerRawInputForTest(HRAWINPUT,UINT,void*,UINT*,UINT) noexcept;
HRAWINPUT WorkerSyntheticKeyboardForTest(unsigned,bool) noexcept;
HRAWINPUT WorkerSyntheticLoadingForTest(std::uint32_t,std::uint32_t,std::uint64_t) noexcept;
DWORD WorkerPadForTest(DWORD,XINPUT_STATE*) noexcept;
}
namespace w=mgscoop::worker_scenario;namespace ui=mgscoop::debug_overlay;
namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* text){++checks;if(!value){++failures;std::printf("FAIL %s\n",text);}}
struct Fake {void** methods;DWORD type=DI8DEVTYPE_KEYBOARD;HRESULT result=DI_OK;unsigned state_calls{},data_calls{},format_calls{},coop_calls{};DWORD flags{};HWND hwnd{};};
__declspec(noinline) HRESULT STDMETHODCALLTYPE State(IDirectInputDevice8W* object,DWORD size,void* output){auto& f=*reinterpret_cast<Fake*>(object);++f.state_calls;if(output)std::memset(output,0xaa,size);SetLastError(0x12345678);return f.result;}
__declspec(noinline) HRESULT STDMETHODCALLTYPE Data(IDirectInputDevice8W* object,DWORD,DIDEVICEOBJECTDATA* output,DWORD* count,DWORD flags){auto& f=*reinterpret_cast<Fake*>(object);++f.data_calls;f.flags=flags;if(count){if(output&&*count){output[0]={1,0xaa,2,3,0};*count=1;}else *count=0;}SetLastError(0x12345678);return f.result;}
__declspec(noinline) HRESULT STDMETHODCALLTYPE Format(IDirectInputDevice8W* object,LPCDIDATAFORMAT){auto& f=*reinterpret_cast<Fake*>(object);++f.format_calls;SetLastError(0x12345678);return f.result;}
__declspec(noinline) HRESULT STDMETHODCALLTYPE Coop(IDirectInputDevice8W* object,HWND hwnd,DWORD flags){auto& f=*reinterpret_cast<Fake*>(object);++f.coop_calls;f.hwnd=hwnd;f.flags=flags;SetLastError(0x12345678);return f.result;}
__declspec(noinline) HRESULT STDMETHODCALLTYPE Info(IDirectInputDevice8W* object,DIDEVICEINSTANCEW* info){if(!info||info->dwSize!=sizeof(*info))return DIERR_INVALIDPARAM;info->dwDevType=reinterpret_cast<Fake*>(object)->type;SetLastError(0x98765432);return DI_OK;}
unsigned raw_calls{};UINT raw_result=1;RAWINPUT* raw_output{};UINT* raw_size{};UINT raw_header{};
__declspec(noinline) UINT WINAPI RawBuffer(RAWINPUT* output,UINT* size,UINT header){++raw_calls;raw_output=output;raw_size=size;raw_header=header;if(output&&size)std::memset(output,0xab,*size);if(size)*size=7;SetLastError(0x11223344);return raw_result;}
unsigned raw_data_calls{};UINT raw_data_result=sizeof(RAWINPUT),raw_data_size=sizeof(RAWINPUT),raw_packet_size=sizeof(RAWINPUT),raw_kind=RIM_TYPEKEYBOARD;
HRAWINPUT raw_handle{};UINT raw_command{},raw_data_header{};void* raw_data_output{};UINT* raw_data_size_pointer{};
__declspec(noinline) UINT WINAPI RawData(HRAWINPUT input,UINT command,void* output,UINT* size,UINT header){
    ++raw_data_calls;raw_handle=input;raw_command=command;raw_data_output=output;raw_data_size_pointer=size;raw_data_header=header;
    if(output&&size){std::memset(output,0xab,*size);if(*size>=sizeof(RAWINPUT)){
        auto* raw=static_cast<RAWINPUT*>(output);raw->header.dwType=raw_kind;raw->header.dwSize=raw_packet_size;raw->data.keyboard.VKey='W';raw->data.keyboard.Flags=0;
    }}
    if(size)*size=raw_data_size;SetLastError(0x10203040);return raw_data_result;
}
const HWND cursor_window=reinterpret_cast<HWND>(0x789a);
RECT cursor_rect{0,0,1280,720};
unsigned cursor_original_calls{},cursor_rect_calls{},cursor_map_calls{};
BOOL cursor_original_result=TRUE,cursor_rect_result=TRUE,cursor_map_result=TRUE;
BOOL WINAPI CursorOriginal(LPPOINT point){++cursor_original_calls;if(point)*point={11,-22};SetLastError(0x33445566);return cursor_original_result;}
BOOL WINAPI CursorRect(HWND own,LPRECT rect){++cursor_rect_calls;Check(own==cursor_window,"cursorgeometryqualifiedwindow");*rect=cursor_rect;SetLastError(0x55667788);return cursor_rect_result;}
BOOL WINAPI CursorToScreen(HWND own,LPPOINT point){++cursor_map_calls;Check(own==cursor_window,"cursormappingqualifiedwindow");point->x-=1920;point->y+=37;SetLastError(0x778899aa);return cursor_map_result;}
void ConfigureCursor(HWND own=cursor_window){ui::ConfigureWorkerCursorFixtureForTest(own,CursorRect,CursorToScreen,CursorOriginal);}
void CursorPassthrough(){
    ConfigureCursor();POINT point{31,41};
    for(const BOOL result:{TRUE,FALSE}){
        cursor_original_result=result;const auto before=cursor_original_calls;SetLastError(0x11223344);
        Check(ui::WorkerGetCursorForTest(&point)==result&&cursor_original_calls==before+1&&point.x==11&&point.y==-22&&
              GetLastError()==0x33445566,"normalcursororiginalonceexactBOOLoutputandLastError");
    }
    Check(cursor_rect_calls==0&&cursor_map_calls==0,"normalcursorneverusesvirtualgeometry");
}
void CursorWorker(){
    ConfigureCursor();POINT point{31,41};const auto physical=cursor_original_calls;
    SetLastError(0x11223344);
    Check(ui::WorkerGetCursorForTest(&point)&&point.x==-1280&&point.y==397&&GetLastError()==0x11223344,
          "privatecursorclientcentremappedtoscreenwithoutglobalcursor");
    Check(point.x+1920>cursor_rect.left&&point.x+1920<cursor_rect.right&&point.y-37>cursor_rect.top&&point.y-37<cursor_rect.bottom,
          "virtualcursorstrictlyinsideactualretailclientbounds");
    for(unsigned refused=0;refused<8;++refused){
        point={31,41};cursor_rect={0,0,1280,720};cursor_rect_result=cursor_map_result=TRUE;ConfigureCursor();
        if(refused==1)w::SetPrivateForTest(false);
        if(refused==2)ConfigureCursor(nullptr);
        if(refused==3)cursor_rect_result=FALSE;
        if(refused==4)cursor_map_result=FALSE;
        if(refused==5)cursor_rect.right=1;
        if(refused==6)cursor_rect.bottom=0;
        if(refused==7)cursor_rect.right=-10;
        SetLastError(0x11223344);
        Check(!ui::WorkerGetCursorForTest(refused==0?nullptr:&point)&&point.x==31&&point.y==41&&GetLastError()==0x11223344,
              "nullforeignmissingwindowfailedorunusablegeometryrefuseswithoutoutputorerrorchanges");
        w::SetPrivateForTest(true);
    }
    Check(cursor_original_calls==physical,"requestedworkernevercallsphysicalGetCursorPos");
    cursor_rect={0,0,1280,720};cursor_rect_result=cursor_map_result=TRUE;ConfigureCursor();
    auto stats=ui::GetWorkerInputStats();
    Check(stats.cursor_success==1&&stats.cursor_null==1&&stats.cursor_thread_refused==1&&stats.cursor_window_refused==1&&
          stats.cursor_geometry_refused==4&&stats.cursor_map_refused==1,
          "copiedcursorcountersdistinguishsuccessfulqueryandeveryrefusalreason");
    ui::SetWorkerPresentWindowForTest(cursor_window);Check(ui::WorkerGetCursorForTest(&point),"cursorwindowmatchfixture");
    ui::SetWorkerPresentWindowForTest(reinterpret_cast<HWND>(0x3456));Check(ui::WorkerGetCursorForTest(&point),"cursorwindowmismatchfixture");
    ui::SetWorkerPresentWindowForTest(nullptr);stats=ui::GetWorkerInputStats();
    Check(stats.cursor_window_match==1&&stats.cursor_window_mismatch==1&&stats.cursor_present_missing>0&&cursor_original_calls==physical,
          "cursorwindowdiagnosticscompareselectionwithPresentwithoutchangingselection");
}
}
int main(){
    Check(MH_Initialize()==MH_OK,"fixtureMinHookonly");
    std::array<void*,16> methods{};methods[9]=reinterpret_cast<void*>(&State);methods[10]=reinterpret_cast<void*>(&Data);methods[11]=reinterpret_cast<void*>(&Format);methods[13]=reinterpret_cast<void*>(&Coop);methods[15]=reinterpret_cast<void*>(&Info);
    Fake fake{methods.data()};auto* object=reinterpret_cast<IDirectInputDevice8W*>(&fake);
    Check(ui::HookWorkerDeviceFixtureForTest(methods[9],methods[10],methods[11],methods[13]),"realwrappersfakeorigins");
    Check(ui::HookWorkerRawBufferFixtureForTest(reinterpret_cast<void*>(&RawBuffer)),"typedrawbufferfakeorigin");
    Check(ui::HookWorkerRawInputFixtureForTest(reinterpret_cast<void*>(&RawData)),"typedrawinputfakeorigin");
    CursorPassthrough();
    w::ConfigureForTest({true,true,true,true},true,true);Check(w::Active()&&w::ThreadAdmitted(),"fixturemockprivateadmission");
    CursorWorker();
    w::SetLoadingContext({w::LoadingPhase::running,30010,1});
    XINPUT_STATE vp{};SetLastError(0x77665544);
    Check(ui::WorkerPadForTest(0,&vp)==ERROR_DEVICE_NOT_CONNECTED&&vp.Gamepad.wButtons==0&&GetLastError()==0x77665544,"virtualpaddisconnectedbeforecommand");
    Check(w::SetVirtualPad("255 0 12000 0 1500",1),"privatevirtualpadcommandadmitted");
    Check(ui::WorkerPadForTest(0,&vp)==ERROR_SUCCESS&&vp.dwPacketNumber==1&&vp.Gamepad.bLeftTrigger==255&&vp.Gamepad.sThumbRX==12000&&vp.Gamepad.wButtons==0&&GetLastError()==0x77665544,"exportwrapperdeliversboundedvirtualstateandpreservesLastError");
    Check(ui::WorkerPadForTest(1,&vp)==ERROR_DEVICE_NOT_CONNECTED&&vp.dwPacketNumber==0&&vp.Gamepad.bLeftTrigger==0,"allothercontrollersremainhidden");
    w::SetPrivateForTest(false);Check(ui::WorkerPadForTest(0,&vp)==ERROR_DEVICE_NOT_CONNECTED&&vp.Gamepad.bLeftTrigger==0,"foreigndesktopcannotconsumevirtualpad");
    Check(!w::SetVirtualPad("255 0 12000 0 1500",2),"foreigndesktopcannotconfigurevirtualpad");w::SetPrivateForTest(true);
    Check(!w::SetVirtualPad("255 0 12000 0 1501",2)&&ui::WorkerPadForTest(0,&vp)==ERROR_SUCCESS&&vp.Gamepad.bLeftTrigger==255,"invalidcommandcannotreplacevalidlease");
    Check(!w::SetVirtualPad("255 0 0 0 1500",1),"replayedpadcommandcannotrenewlease");
    ui::SetOpenForTest(true);Check(ui::WorkerPadForTest(0,&vp)==ERROR_DEVICE_NOT_CONNECTED&&vp.Gamepad.bLeftTrigger==0,"F9capturesvirtualpadincludingearlyprovider");
    Check(!w::ReadVirtualPad(0,&vp,sizeof(vp)),"F9disarmsoldvirtualhold");
    ui::SetOpenForTest(false);Check(ui::WorkerPadForTest(0,&vp)==ERROR_DEVICE_NOT_CONNECTED,"F9closecannotreviveoldvirtualhold");
    Check(w::SetVirtualPad("off",3)&&ui::WorkerPadForTest(0,&vp)==ERROR_DEVICE_NOT_CONNECTED&&vp.Gamepad.bLeftTrigger==0,"disarmclearsvirtualholds");
    Check(w::SetVirtualPad("255 0 0 0 1500",4),"rearmforloadingcontextcheck");
    w::SetLoadingContext({w::LoadingPhase::running,30010,1});
    Check(ui::WorkerPadForTest(0,&vp)==ERROR_SUCCESS&&vp.Gamepad.bLeftTrigger==255,"sameloadingcontextretainsvirtuallease");
    Check(w::SetVirtualPad("255 0 0 0 1500",5),"sameloadingcontextallowsleaserenewal");
    w::SetLoadingContext({w::LoadingPhase::running,30010,1});
    Check(ui::WorkerPadForTest(0,&vp)==ERROR_SUCCESS&&vp.Gamepad.bLeftTrigger==255,"sameloadingcontextrenewalremainsconnected");
    w::SetLoadingContext({w::LoadingPhase::running,30010,2});
    Check(ui::WorkerPadForTest(0,&vp)==ERROR_DEVICE_NOT_CONNECTED&&vp.Gamepad.bLeftTrigger==0,"generationchangerevokesvirtualholds");
    Check(w::SetVirtualPad("255 0 0 0 1500",6),"rearmforphasecheck");
    w::SetLoadingContext({w::LoadingPhase::terminating,30010,2});
    Check(ui::WorkerPadForTest(0,&vp)==ERROR_DEVICE_NOT_CONNECTED&&vp.Gamepad.bLeftTrigger==0,"phasechangerevokesvirtualholds");
    Check(w::SetVirtualPad("255 0 0 0 1500",7),"rearmformissioncheck");
    w::SetLoadingContext({w::LoadingPhase::terminating,30020,2});
    Check(ui::WorkerPadForTest(0,&vp)==ERROR_DEVICE_NOT_CONNECTED&&vp.Gamepad.bLeftTrigger==0,"missionchangerevokesvirtualholds");
    Check(w::SetVirtualPad("255 0 0 0 1500",8),"rearmforrefusalcheck");w::Refuse("padfixture");
    Check(ui::WorkerPadForTest(0,&vp)==ERROR_DEVICE_NOT_CONNECTED&&vp.Gamepad.bLeftTrigger==0,"workerrefusalrevokesvirtualholds");
    w::ConfigureForTest({true,true,true,true},true,true);
    Check(ui::WorkerFormatForTest(object,&c_dfDIKeyboard)==DI_OK&&fake.format_calls==1,"typedkeyboardcanonicalformat");Check(GetLastError()==0x12345678,"formatLastErrorunchanged");
    const auto now=GetTickCount64();w::Command c{1,now+2000,std::uint64_t{1}<<static_cast<unsigned>(w::Key::w),w::Operation::hold,500,0,0};
    Check(w::AcceptForTest(c,now),"scriptcommandaccepted");std::array<unsigned char,256> keys{};
    Check(ui::WorkerStateForTest(object,256,keys.data())==DI_OK&&keys[0x11]==0x80&&keys[0]==0&&keys[255]==0,"virtualkeyboardreplacesphysical");
    Check(fake.state_calls==1&&GetLastError()==0x12345678,"originalonceLastErrorpreserved");
    Check(w::StatsForTest().keyboard_consumed==1,"actualAPIreceiptcorrelation");
    fake.result=DIERR_INPUTLOST;keys.fill(0x42);
    Check(ui::WorkerStateForTest(object,256,keys.data())==DIERR_INPUTLOST&&keys[0]==0xaa&&keys[255]==0xaa,"inputlostresultandoriginaloutputunchanged");Check(GetLastError()==0x12345678,"inputlostLastErrorpreserved");fake.result=DI_OK;
    w::SetPrivateForTest(false);Check(ui::WorkerStateForTest(object,256,keys.data())==DIERR_UNSUPPORTED&&keys[0x11]==0,"foreignthreadnovirtualinput");w::SetPrivateForTest(true);
    DIDEVICEOBJECTDATA event{};DWORD count=1;
    Check(ui::WorkerDataForTest(object,sizeof(event),&event,&count,DIGDD_PEEK)==DI_OK&&count==1&&event.dwOfs==0x11&&event.dwData==0x80&&fake.flags==DIGDD_PEEK,"bufferedpeekvirtualunchangedflags");
    count=1;Check(ui::WorkerDataForTest(object,sizeof(event),&event,&count,0)==DI_OK&&count==1,"bufferedactualconsume");
    count=1;Check(ui::WorkerDataForTest(object,sizeof(event),&event,&count,0)==DI_OK&&count==0,"bufferednoreplay");
    fake.result=DIERR_NOTACQUIRED;count=1;Check(ui::WorkerDataForTest(object,sizeof(event),&event,&count,0)==DIERR_NOTACQUIRED&&count==1&&event.dwOfs==1&&event.dwData==0xaa,"notacquiredoutputcountandresultunchanged");Check(GetLastError()==0x12345678,"bufferedfailureLastError");fake.result=DI_OK;
    DIDATAFORMAT unknown=c_dfDIKeyboard;unknown.dwDataSize=255;
    Check(ui::WorkerFormatForTest(object,&unknown)==DI_OK,"unknownformatpreservesoriginalsetresult");Check(ui::WorkerStateForTest(object,256,keys.data())==DIERR_UNSUPPORTED,"unknownformatcannotreceivevirtual");
    fake.type=DI8DEVTYPE_MOUSE;Check(ui::WorkerFormatForTest(object,&c_dfDIMouse2)==DI_OK,"typedmousecanonicalformat");
    c.sequence=2;c.keys=(std::uint64_t{1}<<static_cast<unsigned>(w::Key::rmb))|(std::uint64_t{1}<<static_cast<unsigned>(w::Key::lmb));Check(w::AcceptForTest(c,GetTickCount64()),"scriptlocalaimfireaccepted");
    DIMOUSESTATE2 mouse{};Check(ui::WorkerStateForTest(object,sizeof(mouse),&mouse)==DI_OK&&mouse.rgbButtons[0]==0x80&&mouse.rgbButtons[1]==0x80&&mouse.rgbButtons[7]==0&&mouse.lX==0,"virtualaimfiresuppressesphysicalmouse");
    c.sequence=3;c.operation=w::Operation::mouse;c.keys=0;c.dx=90;c.dy=-12;c.duration=0;Check(w::AcceptForTest(c,GetTickCount64()),"scriptmousemoveaccepted");
    Check(ui::WorkerStateForTest(object,sizeof(mouse),&mouse)==DI_OK&&mouse.lX==90&&mouse.lY==-12,"onemoveconsumed");Check(ui::WorkerStateForTest(object,sizeof(mouse),&mouse)==DI_OK&&mouse.lX==0,"movenotrepeated");
    Check(ui::GetWorkerInputStats().di_mouse_state_supplied==3&&ui::GetWorkerInputStats().di_mouse_state_axes==1&&
          ui::GetWorkerInputStats().di_mouse_state_buttons==3,"DImousediagnosticsdistinguishheldstatefromsingleaxisconsumption");
    count=64;std::array<DIDEVICEOBJECTDATA,64> mouse_events{};
    const auto buffered_before=ui::GetWorkerInputStats();
    Check(ui::WorkerDataForTest(object,sizeof(event),mouse_events.data(),&count,DIGDD_PEEK)==DI_OK&&count==2,"DImousebuttonedgesremainafterstateaxisconsumption");
    count=64;Check(ui::WorkerDataForTest(object,sizeof(event),mouse_events.data(),&count,0)==DI_OK&&count==2,"DImousebufferconsumestwoheldbuttonedges");
    const auto buffered_after=ui::GetWorkerInputStats();
    Check(buffered_after.di_mouse_buffer_calls==buffered_before.di_mouse_buffer_calls+2&&
          buffered_after.di_mouse_buffer_peek_events==buffered_before.di_mouse_buffer_peek_events+2&&
          buffered_after.di_mouse_buffer_events==buffered_before.di_mouse_buffer_events+2,"DIbufferpeeksandactualconsumptioncountseparately");
    const HWND sentinel=reinterpret_cast<HWND>(0x1234);Check(ui::WorkerCoopForTest(object,sentinel,DISCL_EXCLUSIVE|DISCL_FOREGROUND)==DI_OK&&fake.flags==(DISCL_NONEXCLUSIVE|DISCL_BACKGROUND)&&fake.hwnd==sentinel,"workercoopforwardedboundedbackgroundmode");
    fake.result=DIERR_INPUTLOST;Check(ui::WorkerCoopForTest(object,sentinel,DISCL_EXCLUSIVE|DISCL_FOREGROUND)==DIERR_INPUTLOST,"cooperativefailureunchanged");Check(GetLastError()==0x12345678,"cooperativeLastError");fake.result=DI_OK;
    fake.type=DI8DEVTYPE_JOYSTICK;const auto prior_coop=fake.coop_calls;
    Check(ui::WorkerCoopForTest(object,sentinel,DISCL_EXCLUSIVE|DISCL_FOREGROUND)==DI_OK&&fake.coop_calls==prior_coop+1&&fake.flags==(DISCL_EXCLUSIVE|DISCL_FOREGROUND),"unknowncontrolleroriginaloncewithunchangedflags");
    Check(w::Active()&&GetLastError()==0x12345678,"unknowncontrollerdoesnotfabricatesuccessorrevokekeyboard");fake.type=DI8DEVTYPE_MOUSE;
    SetLastError(0x22222222);Check(ui::WorkerCursorForTest(1500,1500)&&ui::WorkerClipForTest(nullptr)&&GetLastError()==0x22222222,"cursorcallsneverreachglobalAPI");
    Check(ui::WorkerAsyncForTest('W')==0&&ui::WorkerKeyStateForTest('W')==0,"neverqueriesglobalkeyboard");XINPUT_STATE pad{};pad.Gamepad.wButtons=0xffff;
    struct RawKeyCase { unsigned scan; USHORT vk, flags; bool down; };
    const RawKeyCase modifier_cases[] = {
        {0x2a,VK_SHIFT,RI_KEY_MAKE,true}, {0x2a,VK_SHIFT,RI_KEY_BREAK,false},
        {0x36,VK_SHIFT,RI_KEY_MAKE,true}, {0x1d,VK_CONTROL,RI_KEY_MAKE,true},
        {0x9d,VK_CONTROL,RI_KEY_E0,true}, {0x9d,VK_CONTROL,RI_KEY_E0|RI_KEY_BREAK,false}
    };
    for(const auto& key:modifier_cases) {
        const auto token=ui::WorkerSyntheticKeyboardForTest(key.scan,key.down);
        RAWINPUT packet{};UINT size=sizeof(packet);const auto calls=raw_data_calls;
        Check(ui::WorkerRawInputForTest(token,RID_INPUT,&packet,&size,sizeof(RAWINPUTHEADER))==sizeof(RAWINPUTHEADER)+sizeof(RAWKEYBOARD),"scriptmodifierpacketreadwithoutphysicalinput");
        Check(packet.data.keyboard.VKey==key.vk&&packet.data.keyboard.MakeCode==(key.scan&0x7f)&&packet.data.keyboard.Flags==key.flags&&
              packet.data.keyboard.Message==static_cast<UINT>(key.down?WM_KEYDOWN:WM_KEYUP),"rawmodifiergenericVKeywithsideandreleasepreserved");
        Check(raw_data_calls==calls,"syntheticmodifierneverqueriesoriginalrawinput");
    }
    const auto before_loading_raw=ui::GetWorkerInputStats();
    w::SetLoadingContext({w::LoadingPhase::initialized,30020,8});
    const auto loading_token=ui::WorkerSyntheticLoadingForTest(30020,8,GetTickCount64()+2000);
    RAWINPUT loading_packet{};UINT loading_size=sizeof(loading_packet);
    Check(ui::WorkerRawInputForTest(loading_token,RID_INPUT,&loading_packet,&loading_size,sizeof(RAWINPUTHEADER))==sizeof(RAWINPUTHEADER)+sizeof(RAWKEYBOARD)&&
          loading_packet.data.keyboard.VKey==VK_SPACE&&loading_packet.data.keyboard.Flags==RI_KEY_MAKE,
          "loadingrawcurrentacceptedallocationdeliversSpace");
    w::SetLoadingContext({w::LoadingPhase::running,30020,8});loading_size=sizeof(loading_packet);
    Check(ui::WorkerRawInputForTest(loading_token,RID_INPUT,&loading_packet,&loading_size,sizeof(RAWINPUTHEADER))==sizeof(RAWINPUTHEADER)+sizeof(RAWKEYBOARD)&&
          loading_packet.data.keyboard.VKey==VK_SPACE&&loading_packet.data.keyboard.Flags==RI_KEY_BREAK&&loading_packet.data.keyboard.Message==WM_KEYUP,
          "postedloadingtokencannotjumpafterruntimeentry");
    w::SetLoadingContext({w::LoadingPhase::initialized,30020,9});loading_size=sizeof(loading_packet);
    ui::WorkerRawInputForTest(loading_token,RID_INPUT,&loading_packet,&loading_size,sizeof(RAWINPUTHEADER));
    Check(loading_packet.data.keyboard.Flags==RI_KEY_BREAK,"oldrawtokenrefusedafterallocationchange");
    const auto expired_loading_token=ui::WorkerSyntheticLoadingForTest(30020,9,GetTickCount64());loading_size=sizeof(loading_packet);
    ui::WorkerRawInputForTest(expired_loading_token,RID_INPUT,&loading_packet,&loading_size,sizeof(RAWINPUTHEADER));
    Check(loading_packet.data.keyboard.Flags==RI_KEY_BREAK,"raw120msloadingdeadlineenforcedatdelivery");
    w::SetLoadingContext({w::LoadingPhase::terminating,30020,9});
    const auto terminating_token=ui::WorkerSyntheticLoadingForTest(30020,9,GetTickCount64()+2000);loading_size=sizeof(loading_packet);
    ui::WorkerRawInputForTest(terminating_token,RID_INPUT,&loading_packet,&loading_size,sizeof(RAWINPUTHEADER));
    Check(loading_packet.data.keyboard.Flags==RI_KEY_BREAK,"terminatingrawloadingdeliveryfailsclosed");
    const auto after_loading_raw=ui::GetWorkerInputStats();
    Check(after_loading_raw.raw_keyboard_reads==before_loading_raw.raw_keyboard_reads+5&&
          after_loading_raw.raw_keyboard_packets_read==before_loading_raw.raw_keyboard_packets_read+3,
          "loadingrawreadsdistinctfromuniquepostedtokens");
    const auto packet_stats=ui::GetWorkerInputStats();
    Check(packet_stats.raw_keyboard_reads==11&&packet_stats.raw_keyboard_packets_read==9,"syntheticRIDINPUTreadcountersexcludephysicaldata");
    const auto repeated_token=ui::WorkerSyntheticKeyboardForTest(0x11,true);
    RAWINPUT repeated{};UINT repeated_size=sizeof(repeated);
    ui::WorkerRawInputForTest(repeated_token,RID_INPUT,&repeated,&repeated_size,sizeof(RAWINPUTHEADER));
    repeated_size=sizeof(repeated);ui::WorkerRawInputForTest(repeated_token,RID_INPUT,&repeated,&repeated_size,sizeof(RAWINPUTHEADER));
    repeated_size=sizeof(repeated);ui::WorkerRawInputForTest(repeated_token,RID_HEADER,&repeated,&repeated_size,sizeof(RAWINPUTHEADER));
    repeated_size=0;ui::WorkerRawInputForTest(repeated_token,RID_INPUT,nullptr,&repeated_size,sizeof(RAWINPUTHEADER));
    const auto duplicate_stats=ui::GetWorkerInputStats();
    Check(duplicate_stats.raw_keyboard_reads==13&&duplicate_stats.raw_keyboard_packets_read==10,
          "multipleAPIreadsareseparatefromuniqueinputtokensheaderandsizequeriesexcluded");
    Check(ui::WorkerPadForTest(0,&pad)==ERROR_DEVICE_NOT_CONNECTED&&!pad.Gamepad.wButtons,"physicalpadneverforwarded");
    RAWINPUT raw{};UINT raw_bytes=sizeof(raw);
    Check(ui::WorkerRawBufferForTest(&raw,&raw_bytes,sizeof(RAWINPUTHEADER))==0&&raw_calls==1&&raw_output==&raw&&raw_size==&raw_bytes&&raw_header==sizeof(RAWINPUTHEADER),"rawbufferoriginalonceunchangedargumentsneutralcount");
    Check(raw.header.dwType==0&&raw.header.dwSize==0&&raw_bytes==7&&GetLastError()==0x11223344,"rawsuccessboundedpayloadneutralandsizeerrorretained");
    raw_result=static_cast<UINT>(-1);raw_bytes=sizeof(raw);
    Check(ui::WorkerRawBufferForTest(&raw,&raw_bytes,sizeof(RAWINPUTHEADER))==static_cast<UINT>(-1)&&raw_calls==2&&raw.header.dwType==0xababababu&&raw_bytes==7&&GetLastError()==0x11223344,"rawfailureexactoutputsizeuintsentinelerror");
    const auto input_handle=reinterpret_cast<HRAWINPUT>(0x6789);raw_bytes=sizeof(raw);
    Check(ui::WorkerRawInputForTest(input_handle,RID_INPUT,&raw,&raw_bytes,sizeof(RAWINPUTHEADER))==sizeof(raw)&&raw_data_calls==1&&raw_handle==input_handle&&raw_command==RID_INPUT&&raw_data_output==&raw&&raw_data_size_pointer==&raw_bytes&&raw_data_header==sizeof(RAWINPUTHEADER),"rawdataoriginalonceallargumentsexact");
    Check(raw.data.keyboard.VKey==0&&raw.data.keyboard.Flags==RI_KEY_BREAK&&raw_bytes==sizeof(raw)&&GetLastError()==0x10203040,"knownrawkeyboardpayloadneutralpreservingbytesanderror");
    raw_data_result=static_cast<UINT>(-1);raw_data_size=73;raw_bytes=sizeof(raw);
    Check(ui::WorkerRawInputForTest(input_handle,RID_INPUT,&raw,&raw_bytes,sizeof(RAWINPUTHEADER))==static_cast<UINT>(-1)&&raw_data_calls==2&&raw.data.keyboard.VKey=='W'&&raw.data.keyboard.Flags==0&&raw_bytes==73&&GetLastError()==0x10203040,"originalrawfailureoutputsizeanderroruntouched");
    raw_data_result=65537;raw_data_size=sizeof(raw);raw_bytes=sizeof(raw);auto prior_diagnostic=w::StatsForTest().diagnostic_count;
    Check(ui::WorkerRawInputForTest(input_handle,RID_INPUT,&raw,&raw_bytes,sizeof(RAWINPUTHEADER))==static_cast<UINT>(-1)&&raw_data_calls==3&&raw_bytes==sizeof(raw)&&GetLastError()==0x10203040,"oversizedsuccessfulrawresultisexplicitworkerfailure");
    Check(w::StatsForTest().diagnostic_count==prior_diagnostic+1,"oversizedrawrefusalreceiptseparatefromoriginalsuccess");
    raw_data_result=sizeof(raw);raw_data_size=sizeof(raw)+1;raw_bytes=sizeof(raw);
    Check(ui::WorkerRawInputForTest(input_handle,RID_INPUT,&raw,&raw_bytes,sizeof(RAWINPUTHEADER))==static_cast<UINT>(-1)&&raw_bytes==sizeof(raw)+1&&GetLastError()==0x10203040,"inconsistentsuccesssizecannotexceedoriginalcapacity");
    raw_data_result=sizeof(RAWINPUTHEADER)+1;raw_data_size=raw_packet_size=raw_data_result;raw_bytes=sizeof(raw);
    Check(ui::WorkerRawInputForTest(input_handle,RID_INPUT,&raw,&raw_bytes,sizeof(RAWINPUTHEADER))==static_cast<UINT>(-1),"truncatedknownkeyboardpayloadrefused");
    raw_data_result=raw_data_size=raw_packet_size=sizeof(raw);raw_kind=99;raw_bytes=sizeof(raw);
    Check(ui::WorkerRawInputForTest(input_handle,RID_INPUT,&raw,&raw_bytes,sizeof(RAWINPUTHEADER))==static_cast<UINT>(-1),"unknownrawrecordkindrefused");
    raw_kind=RIM_TYPEHID;raw_bytes=sizeof(raw);
    Check(ui::WorkerRawInputForTest(input_handle,RID_INPUT,&raw,&raw_bytes,sizeof(RAWINPUTHEADER))==sizeof(raw)&&raw.data.hid.dwSizeHid==0&&raw.data.hid.dwCount==0,"boundedhidpayloadneutralwithoutvariablefollow");
    raw_kind=RIM_TYPEKEYBOARD;raw_bytes=sizeof(raw);
    Check(ui::WorkerRawInputForTest(input_handle,RID_INPUT,&raw,&raw_bytes,1)==static_cast<UINT>(-1)&&GetLastError()==0x10203040,"unsupportedheadersizeworkerrefusaloriginalerrorretained");
    raw_data_result=0;raw_bytes=0;
    Check(ui::WorkerRawInputForTest(input_handle,RID_INPUT,nullptr,&raw_bytes,sizeof(RAWINPUTHEADER))==0&&raw_bytes==sizeof(raw)&&GetLastError()==0x10203040,"rawrequiredsizequeryoriginalresultsizeanderror");
    w::SetPrivateForTest(false);const auto before_refused=fake.coop_calls;
    Check(ui::WorkerCoopForTest(object,sentinel,DISCL_EXCLUSIVE|DISCL_FOREGROUND)==DI_OK&&fake.coop_calls==before_refused+1&&fake.flags==(DISCL_EXCLUSIVE|DISCL_FOREGROUND),"refusedcoopforwardsoriginalonceunchanged");
    Check(!w::Active()&&GetLastError()==0x12345678,"refusedprivatecooprevokesworkerpreserveserror");w::SetPrivateForTest(true);
    w::Refuse("fixture_end");Check(!w::Active(),"revocation");Check(ui::WorkerStateForTest(object,sizeof(mouse),&mouse)==DIERR_UNSUPPORTED&&!mouse.rgbButtons[0],"revocationcannotleakphysicalorheldvirtual");
    ui::ShutdownForTest();Check(ui::GetWorkerInputStats().cursor_success==0&&ui::GetWorkerInputStats().raw_keyboard_reads==0,
                              "fixturecleanupclearsdiagnosticsandboundedtokenstore");
    MH_Uninitialize();std::printf("worker_input_fixture checks=%u failures=%u no_windows_no_input_no_game\n",checks,failures);return failures?1:0;
}
