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
BOOL WorkerClipForTest(const RECT*) noexcept;
SHORT WorkerAsyncForTest(int) noexcept;
SHORT WorkerKeyStateForTest(int) noexcept;
UINT WorkerRawBufferForTest(RAWINPUT*,UINT*,UINT) noexcept;
UINT WorkerRawInputForTest(HRAWINPUT,UINT,void*,UINT*,UINT) noexcept;
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
}
int main(){
    Check(MH_Initialize()==MH_OK,"fixtureMinHookonly");
    std::array<void*,16> methods{};methods[9]=reinterpret_cast<void*>(&State);methods[10]=reinterpret_cast<void*>(&Data);methods[11]=reinterpret_cast<void*>(&Format);methods[13]=reinterpret_cast<void*>(&Coop);methods[15]=reinterpret_cast<void*>(&Info);
    Fake fake{methods.data()};auto* object=reinterpret_cast<IDirectInputDevice8W*>(&fake);
    Check(ui::HookWorkerDeviceFixtureForTest(methods[9],methods[10],methods[11],methods[13]),"realwrappersfakeorigins");
    Check(ui::HookWorkerRawBufferFixtureForTest(reinterpret_cast<void*>(&RawBuffer)),"typedrawbufferfakeorigin");
    Check(ui::HookWorkerRawInputFixtureForTest(reinterpret_cast<void*>(&RawData)),"typedrawinputfakeorigin");
    w::ConfigureForTest({true,true,true,true},true,true);Check(w::Active()&&w::ThreadAdmitted(),"fixturemockprivateadmission");
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
    const HWND sentinel=reinterpret_cast<HWND>(0x1234);Check(ui::WorkerCoopForTest(object,sentinel,DISCL_EXCLUSIVE|DISCL_FOREGROUND)==DI_OK&&fake.flags==(DISCL_NONEXCLUSIVE|DISCL_BACKGROUND)&&fake.hwnd==sentinel,"workercoopforwardedboundedbackgroundmode");
    fake.result=DIERR_INPUTLOST;Check(ui::WorkerCoopForTest(object,sentinel,DISCL_EXCLUSIVE|DISCL_FOREGROUND)==DIERR_INPUTLOST,"cooperativefailureunchanged");Check(GetLastError()==0x12345678,"cooperativeLastError");fake.result=DI_OK;
    fake.type=DI8DEVTYPE_JOYSTICK;const auto prior_coop=fake.coop_calls;
    Check(ui::WorkerCoopForTest(object,sentinel,DISCL_EXCLUSIVE|DISCL_FOREGROUND)==DI_OK&&fake.coop_calls==prior_coop+1&&fake.flags==(DISCL_EXCLUSIVE|DISCL_FOREGROUND),"unknowncontrolleroriginaloncewithunchangedflags");
    Check(w::Active()&&GetLastError()==0x12345678,"unknowncontrollerdoesnotfabricatesuccessorrevokekeyboard");fake.type=DI8DEVTYPE_MOUSE;
    SetLastError(0x22222222);Check(ui::WorkerCursorForTest(1500,1500)&&ui::WorkerClipForTest(nullptr)&&GetLastError()==0x22222222,"cursorcallsneverreachglobalAPI");
    Check(ui::WorkerAsyncForTest('W')==0&&ui::WorkerKeyStateForTest('W')==0,"neverqueriesglobalkeyboard");XINPUT_STATE pad{};pad.Gamepad.wButtons=0xffff;
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
    ui::ShutdownForTest();MH_Uninitialize();std::printf("worker_input_fixture checks=%u failures=%u no_windows_no_input_no_game\n",checks,failures);return failures?1:0;
}
