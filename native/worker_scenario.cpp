#include "mgscoop/worker_scenario.hpp"
#include <cstring>
#include <limits>
#if MGS_BACKGROUND_WORKER && defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <strsafe.h>
#include <atomic>
#include <cwchar>
#endif

namespace mgscoop::worker_scenario {
namespace {
// "3" (scan 0x04) stays before the mouse buttons, which are last. It is the call menu's "Get it" while CALL q is
// held (M2), and it opens the iDroid MISSIONS tab (M3, hidden run 612afff7: tabs switch with keys 1 and 3).
constexpr std::uint8_t scans[] = {0x11,0x1e,0x1f,0x20,0x1c,0x39,0x01,0x12,0x13,0x2a,0x1d,0x02,0x03,0x21,0x2e,0x10,0x2d,0x2c,0x0f,0x04,0,0};
constexpr const char* names[] = {"w","a","s","d","enter","space","escape","e","r","shift","ctrl","1","2","f","c","q","x","z","tab","3","lmb","rmb"};
static_assert(sizeof(scans) == static_cast<unsigned>(Key::count) && sizeof(names) / sizeof(names[0]) == static_cast<unsigned>(Key::count));
bool Number(const char* text, std::uint64_t& value) noexcept {
    value=0; if(!text || !*text) return false;
    for(;*text;++text) { if(*text<'0'||*text>'9') return false;
        const auto digit=static_cast<unsigned>(*text-'0');
        if(value>(std::numeric_limits<std::uint64_t>::max()-digit)/10) return false;
        value=value*10+digit; } return true;
}
bool Signed(const char* text, std::int32_t& value) noexcept {
    const bool minus=text && *text=='-'; if(minus) ++text;
    std::uint64_t number=0; if(!Number(text,number)||number>1500) return false;
    value=minus?-static_cast<std::int32_t>(number):static_cast<std::int32_t>(number); return true;
}
bool Keys(char* text, std::uint64_t& keys) noexcept {
    keys=0; if(!text||!*text) return false;
    char* start=text;
    for(char* at=text;;++at) if(*at=='+'||!*at) {
        const bool last=!*at; *at=0; bool found=false;
        for(unsigned i=0;i<static_cast<unsigned>(Key::count);++i) if(std::strcmp(start,names[i])==0) {
            const auto bit=std::uint64_t{1}<<i; if(keys&bit) return false;
            keys|=bit; found=true; break;
        }
        if(!found) return false; if(last) return true; start=at+1;
    }
}
}
bool ValidRunId(const char* id) noexcept {
    if(!id||std::strlen(id)!=32) return false;
    for(unsigned i=0;i<32;++i) if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f'))) return false;
    return true;
}
bool ValidAuthorization(const Authorization& a) noexcept { return a.offline_v1&&a.lab&&a.copied_saves&&a.exact_executable; }
bool ParseVirtualPad(const char* text,VirtualPadRequest& out) noexcept {
    if(!text)return false;
    if(!std::strcmp(text,"off")){out={};return true;}
    const auto length=std::strlen(text);if(!length || length>64)return false;
    char b[65]{};std::memcpy(b,text,length);char* token[5]{};unsigned count=1;token[0]=b;
    for(std::size_t i=0;i<length;++i) {
        if(b[i]==' ') {if(i==0 || i+1==length || b[i+1]==' ' || count==5)return false;b[i]=0;token[count++]=b+i+1;}
        else if((b[i]<'0'||b[i]>'9') && b[i]!='-')return false;
    }
    if(count!=5)return false;
    auto integer=[](const char* t,std::int32_t& n,bool signed_value,std::uint64_t maximum) noexcept {
        const bool negative=*t=='-';if(negative){if(!signed_value)return false;++t;}
        if(!*t || (t[0]=='0'&&t[1]) || (negative && t[0]=='0'))return false;
        std::uint64_t v=0;if(!Number(t,v)||v>maximum)return false;
        n=negative?-static_cast<std::int32_t>(v):static_cast<std::int32_t>(v);return true;
    };
    std::int32_t v[5]{};
    if(!integer(token[0],v[0],false,255)||!integer(token[1],v[1],false,255)||
        !integer(token[2],v[2],true,16000)||!integer(token[3],v[3],true,16000)||
        !integer(token[4],v[4],false,1500)||!v[4])return false;
    out={true,static_cast<std::uint32_t>(v[4]),static_cast<std::uint8_t>(v[0]),static_cast<std::uint8_t>(v[1]),
        static_cast<std::int16_t>(v[2]),static_cast<std::int16_t>(v[3])};return true;
}
void VirtualPad::Neutral() noexcept {
    if(state_.buttons||state_.left||state_.right||state_.lx||state_.ly||state_.rx||state_.ry) {
        const auto packet=state_.packet+1;state_={};state_.packet=packet;
    }
    expires_=0;
}
void VirtualPad::Clear() noexcept {Neutral();enabled_=false;}
void VirtualPad::Tick(std::uint64_t now) noexcept {if(expires_&&now>=expires_)Neutral();}
bool VirtualPad::Apply(const VirtualPadRequest& r,std::uint64_t now) noexcept {
    Tick(now);
    if(!r.enabled){Clear();return true;}
    if(!r.duration || r.duration>1500 || r.rx < -16000 || r.rx>16000 || r.ry < -16000 || r.ry>16000 ||
        now>std::numeric_limits<std::uint64_t>::max()-r.duration)return false;
    if(!enabled_||state_.left!=r.left||state_.right!=r.right||state_.rx!=r.rx||state_.ry!=r.ry) {
        const auto packet=state_.packet+1;state_={};state_.packet=packet;state_.left=r.left;state_.right=r.right;state_.rx=r.rx;state_.ry=r.ry;
    }
    enabled_=true;expires_=now+r.duration;return true;
}
bool ParseCommand(const char* text,std::size_t size,const char* run_id,const char* role,Command& output) noexcept {
    if(!text||!size||size>512||!ValidRunId(run_id)||!role||
       (std::strcmp(role,"host")&&std::strcmp(role,"guest"))) return false;
    char buffer[513]{}; std::memcpy(buffer,text,size);
    char* tokens[9]{}; unsigned count=0; bool in=false;
    for(std::size_t i=0;i<size;++i) {
        const unsigned char c=static_cast<unsigned char>(buffer[i]);
        if(c==' '||c=='\r'||c=='\n') {buffer[i]=0;in=false;}
        else {if(c<33||c>126) return false; if(!in) {if(count==9) return false;tokens[count++]=buffer+i;in=true;}}
    }
    if(count!=9||std::strcmp(tokens[0],"MGSWORKER1")||std::strcmp(tokens[1],run_id)||std::strcmp(tokens[2],role)) return false;
    Command c{}; if(!Number(tokens[3],c.sequence)||!c.sequence||!Number(tokens[4],c.expires)) return false;
    std::uint64_t duration=0;
    if(!std::strcmp(tokens[5],"press")||!std::strcmp(tokens[5],"hold")) {
        c.operation=!std::strcmp(tokens[5],"press")?Operation::press:Operation::hold;
        if(!Keys(tokens[6],c.keys)||!Number(tokens[7],duration)||!duration||duration>2000||std::strcmp(tokens[8],"0")) return false;
        c.duration=static_cast<std::uint32_t>(duration);
    } else if(!std::strcmp(tokens[5],"mouse")) {
        c.operation=Operation::mouse;
        if(!Signed(tokens[6],c.dx)||!Signed(tokens[7],c.dy)||(!c.dx&&!c.dy)||std::strcmp(tokens[8],"0")) return false;
    } else if(!std::strcmp(tokens[5],"resume_loading")) {
        c.operation=Operation::resume_loading;
        std::uint64_t mission=0,generation=0;
        if(!Number(tokens[6],mission)||!mission||mission>65535||!Number(tokens[7],generation)||
           !generation||generation>UINT32_MAX||std::strcmp(tokens[8],"0"))return false;
        c.mission=static_cast<std::uint32_t>(mission);c.generation=static_cast<std::uint32_t>(generation);
        c.keys=std::uint64_t{1}<<static_cast<unsigned>(Key::space);c.duration=120;
    } else if(!std::strcmp(tokens[5],"release")||!std::strcmp(tokens[5],"capture")) {
        c.operation=!std::strcmp(tokens[5],"release")?Operation::release:Operation::capture;
        if(std::strcmp(tokens[6],"0")||std::strcmp(tokens[7],"0")||std::strcmp(tokens[8],"0")) return false;
    } else return false;
    output=c; return true;
}
const char* LoadingPhaseName(LoadingPhase phase) noexcept {
    switch(phase) { case LoadingPhase::initialized:return "initialized";case LoadingPhase::running:return "running";
        case LoadingPhase::terminating:return "terminating";default:return "unavailable"; }
}
const char* Scenario::ResumeRefusal(const Command& c) const noexcept {
    if(loading_.phase!=LoadingPhase::initialized)return LoadingPhaseName(loading_.phase);
    if(!loading_.generation||c.generation!=loading_.generation||c.mission!=loading_.mission)return "loading_token_changed";
    switch(loading_.mission){case 30010:case 30020:case 30050:case 40010:case 40020:case 40050:break;
        default:return "unsupported_loading_mission";}
    if(resume_sequence_||held_[static_cast<unsigned>(Key::space)])return "loading_resume_busy";
    return nullptr;
}
bool Scenario::LoadingDeliveryAllowed(std::uint32_t mission,std::uint32_t generation,
                                    std::uint64_t expires,std::uint64_t now) const noexcept {
    return generation&&loading_.phase==LoadingPhase::initialized&&loading_.mission==mission&&
        loading_.generation==generation&&now<expires;
}
void Scenario::SetLoading(LoadingContext c,std::uint64_t now) noexcept {
    const bool revoked=c.phase!=LoadingPhase::initialized||c.mission!=loading_.mission||c.generation!=loading_.generation;
    if(revoked&&resume_sequence_) {
        const auto i=static_cast<unsigned>(Key::space);
        if(held_commands_[i]==resume_sequence_) {Set(Key::space,false,now,resume_sequence_);releases_[i]=held_commands_[i]=0;}
        resume_sequence_=0;
    }
    loading_=c;
}
void Scenario::Set(Key key,bool down,std::uint64_t now,std::uint64_t command,std::uint64_t expires) noexcept {
    const auto index=static_cast<unsigned>(key);const bool mouse=index>=static_cast<unsigned>(Key::lmb);
    auto* events=mouse?mouse_events_:keyboard_events_; auto& count=mouse?mouse_count_:keyboard_count_;
    const std::uint32_t offset=mouse?12+index-static_cast<unsigned>(Key::lmb):scans[index];
    if(held_[index]==down) {
        if(down)for(unsigned i=0;i<count;++i)if(events[i].offset==offset&&events[i].value==0x80&&events[i].command==held_commands_[index]) {
            events[i].expires=expires;events[i].command=command;
        }
        return;
    }
    held_[index]=down;
    if(!down) {
        unsigned keep=0;for(unsigned i=0;i<count;++i)
            if(events[i].offset!=offset||events[i].value!=0x80)events[keep++]=events[i];count=keep;
    }
    if(count==64) {for(unsigned i=1;i<64;++i) events[i-1]=events[i];--count;}
    events[count++]={offset,down?0x80u:0u,static_cast<std::uint32_t>(now),++event_sequence_,command,down?expires:0,
        down&&command==resume_sequence_?loading_.mission:0u,down&&command==resume_sequence_?loading_.generation:0u};
    if(mouse) mouse_command_=command; else keyboard_command_=command;
}
void Scenario::Tick(std::uint64_t now) noexcept {
    for(unsigned i=0;i<static_cast<unsigned>(Key::count);++i) if(held_[i]&&now>=releases_[i]) {
        if(held_commands_[i]==resume_sequence_)resume_sequence_=0;
        Set(static_cast<Key>(i),false,now,held_commands_[i]);releases_[i]=0;held_commands_[i]=0;
    }
    if(move_expires_&&now>=move_expires_) {dx_=dy_=0;move_expires_=0;}
    // Late press/move delivery is forbidden. Release records have no expiry:
    // a caller recovering from input loss can still clear a previously held key.
    auto prune=[now](Event* events,std::uint32_t& count) noexcept {
        unsigned keep=0;for(unsigned i=0;i<count;++i)
            if(!events[i].expires||now<events[i].expires)events[keep++]=events[i];count=keep;
    };
    prune(keyboard_events_,keyboard_count_);prune(mouse_events_,mouse_count_);
}
void Scenario::Clear() noexcept {
    for(unsigned i=0;i<static_cast<unsigned>(Key::count);++i) {held_[i]=false;releases_[i]=held_commands_[i]=0;}
    keyboard_count_=mouse_count_=0;dx_=dy_=0;move_expires_=0;capture_={};keyboard_command_=mouse_command_=0;resume_sequence_=0;
}
bool Scenario::Accept(const Command& request,std::uint64_t now) noexcept {
    Tick(now);
    Command c=request;
    if(c.operation==Operation::resume_loading) {
        if(ResumeRefusal(c))return false;
        c.operation=Operation::press;c.keys=std::uint64_t{1}<<static_cast<unsigned>(Key::space);c.duration=120;
    }
    if(!c.sequence||c.sequence>4096||c.sequence<=last_sequence_||c.expires<=now||c.expires-now>2000||last_sequence_>=4096||
       static_cast<unsigned>(c.operation)>static_cast<unsigned>(Operation::capture)) return false;
    if(c.operation==Operation::press||c.operation==Operation::hold) {
        if(!c.keys||c.keys>>static_cast<unsigned>(Key::count)||!c.duration||c.duration>2000||c.duration>c.expires-now) return false;
    } else if(c.operation==Operation::mouse) {
        if(c.dx < -1500||c.dx>1500||c.dy < -1500||c.dy>1500||(!c.dx&&!c.dy)) return false;
    } else if(c.operation==Operation::capture && (capture_.sequence||(capture_at_&&now-capture_at_<250))) return false;
    last_sequence_=c.sequence;
    if(request.operation==Operation::resume_loading)resume_sequence_=c.sequence;
    if(c.operation==Operation::press||c.operation==Operation::hold) {
        for(unsigned i=0;i<static_cast<unsigned>(Key::count);++i) if(c.keys&(std::uint64_t{1}<<i)) {
            Set(static_cast<Key>(i),true,now,c.sequence,now+c.duration);releases_[i]=now+c.duration;held_commands_[i]=c.sequence;
            if(i>=static_cast<unsigned>(Key::lmb)) mouse_command_=c.sequence; else keyboard_command_=c.sequence;
        }
    } else if(c.operation==Operation::release) {
        for(unsigned i=0;i<static_cast<unsigned>(Key::count);++i) {Set(static_cast<Key>(i),false,now,c.sequence);releases_[i]=held_commands_[i]=0;}
        dx_=dy_=0;move_expires_=0;keyboard_command_=mouse_command_=c.sequence;resume_sequence_=0;
        unsigned keep=0;for(unsigned i=0;i<mouse_count_;++i)if(mouse_events_[i].offset>=12)mouse_events_[keep++]=mouse_events_[i];mouse_count_=keep;
    } else if(c.operation==Operation::mouse) {
        // One pending bounded move; no accumulation or accidental repeat.
        dx_=c.dx;dy_=c.dy;mouse_command_=c.sequence;move_expires_=c.expires;
        unsigned keep=0;for(unsigned i=0;i<mouse_count_;++i)if(mouse_events_[i].offset>=12)mouse_events_[keep++]=mouse_events_[i];mouse_count_=keep;
        for(unsigned axis=0;axis<2;++axis) {const auto value=axis?c.dy:c.dx;if(!value) continue;
            if(mouse_count_==64) {for(unsigned i=1;i<64;++i)mouse_events_[i-1]=mouse_events_[i];--mouse_count_;}
            mouse_events_[mouse_count_++]={axis*4,static_cast<std::uint32_t>(value),static_cast<std::uint32_t>(now),++event_sequence_,c.sequence,c.expires};
        }
    } else {capture_={c.sequence,c.expires};capture_at_=now;}
    return true;
}
void Scenario::Keyboard(std::uint8_t (&bytes)[256]) const noexcept {
    std::memset(bytes,0,sizeof(bytes));
    for(unsigned i=0;i<static_cast<unsigned>(Key::lmb);++i) if(held_[i]) bytes[scans[i]]=0x80;
}
MouseState Scenario::Mouse(bool consume) noexcept {
    MouseState out{};out.x=dx_;out.y=dy_;out.buttons[0]=held_[static_cast<unsigned>(Key::lmb)]?0x80:0;
    out.buttons[1]=held_[static_cast<unsigned>(Key::rmb)]?0x80:0;
    if(consume) {dx_=dy_=0;move_expires_=0;
        unsigned keep=0;for(unsigned i=0;i<mouse_count_;++i) if(mouse_events_[i].offset>=12)mouse_events_[keep++]=mouse_events_[i];mouse_count_=keep;}
    return out;
}
std::uint32_t Scenario::Events(Device device,Event* out,std::uint32_t capacity,bool peek) noexcept {
    auto* events=device==Device::keyboard?keyboard_events_:mouse_events_;
    auto& count=device==Device::keyboard?keyboard_count_:mouse_count_;
    const auto copied=capacity<count?capacity:count;
    if(out) for(unsigned i=0;i<copied;++i) out[i]=events[i];
    if(!peek) {
        if(device==Device::mouse) for(unsigned i=0;i<copied;++i) if(events[i].offset<12) {dx_=dy_=0;move_expires_=0;}
        for(unsigned i=copied;i<count;++i) events[i-copied]=events[i];count-=copied;
    }
    return copied;
}
CaptureRequest Scenario::TakeCapture(std::uint64_t now) noexcept {
    if(!capture_.sequence) return {};
    const auto out=capture_;capture_={};return now<out.expires?out:CaptureRequest{};
}

#if MGS_BACKGROUND_WORKER && defined(_WIN32)
namespace {
INIT_ONCE initialize_once=INIT_ONCE_STATIC_INIT;
SRWLOCK state_lock=SRWLOCK_INIT;
std::atomic<bool> requested{false},configured{false},hooks_ready{false},refused{false};
wchar_t run_dir[32768]{}, desktop[80]{};char run_id[33]{},role[6]{};
DWORD process_id=0;std::uint64_t process_start=0;
VirtualPad virtual_pad{};std::uint64_t virtual_pad_sequence=0,consumed_pad=0;
Scenario scenario{};std::uint64_t last_poll=0,last_file_sequence=0,consumed_keyboard=0,consumed_mouse=0;
HANDLE receipt=INVALID_HANDLE_VALUE;unsigned diagnostic_count=0;
#ifdef MGS_WORKER_TESTING
bool mock_lease=false,mock_private=false;
#endif
bool ReadEnv(const wchar_t* name,wchar_t* text,DWORD capacity) noexcept {
    const auto count=GetEnvironmentVariableW(name,text,capacity);return count&&count<capacity;
}
bool DesktopName(HDESK handle,wchar_t (&name)[256]) noexcept {
    DWORD bytes=0;return handle&&GetUserObjectInformationW(handle,UOI_NAME,name,sizeof(name),&bytes)&&bytes<=sizeof(name)&&name[0];
}
bool PrivateThread() noexcept {
#ifdef MGS_WORKER_TESTING
    if(mock_lease)return mock_private;
#endif
    wchar_t current[256]{},input[256]{};
    if(!DesktopName(GetThreadDesktop(GetCurrentThreadId()),current)||std::wcscmp(current,desktop)) return false;
    HDESK active=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
    const bool read=DesktopName(active,input);if(active)CloseDesktop(active);
    return read&&std::wcscmp(current,input)!=0;
}
bool Path(wchar_t (&output)[32768],const wchar_t* suffix) noexcept {
    return SUCCEEDED(StringCchPrintfW(output,ARRAYSIZE(output),L"%s\\%s",run_dir,suffix));
}
void Receipt(const char* event,const char* reason,std::uint64_t sequence,std::int32_t result=0,
             std::uint32_t width=0,std::uint32_t height=0,std::uint32_t requested_flags=0,std::uint32_t forwarded_flags=0) noexcept {
    if(receipt==INVALID_HANDLE_VALUE) return;
    char text[768]{};const auto now=GetTickCount64();
    if(FAILED(StringCchPrintfA(text,ARRAYSIZE(text),"{\"run_id\":\"%s\",\"role\":\"%s\",\"pid\":%lu,\"event\":\"%s\",\"reason\":\"%s\",\"sequence\":%llu,\"tick_ms\":%llu,\"thread\":%lu,\"api_result\":%ld,\"width\":%u,\"height\":%u,\"requested_flags\":%u,\"forwarded_flags\":%u,\"gameplay_accepted\":null}\n",
        run_id,role,process_id,event,reason,sequence,now,GetCurrentThreadId(),static_cast<long>(result),width,height,requested_flags,forwarded_flags))) return;
    DWORD written=0;WriteFile(receipt,text,static_cast<DWORD>(std::strlen(text)),&written,nullptr);
}
bool ExactExecutable() noexcept {
    const auto base=reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    if(!base) return false;const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0||dos->e_lfanew>0x1000) return false;
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||
       nt->FileHeader.TimeDateStamp!=0x6a4cb898||nt->OptionalHeader.SizeOfImage!=0xa01d000||
       nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;
    wchar_t exe[32768]{};if(!GetModuleFileNameW(nullptr,exe,ARRAYSIZE(exe))) return false;
    HANDLE file=CreateFileW(exe,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;unsigned char digest[32]{};
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok)ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    unsigned char buffer[65536]{};DWORD read=0;
    while(ok) {if(!ReadFile(file,buffer,sizeof(buffer),&read,nullptr)){ok=false;break;}if(!read)break;
        ok=BCryptHashData(hash,buffer,read,0)>=0;}
    if(ok)ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);CloseHandle(file);
    constexpr unsigned char expected[]={0x08,0x5c,0x2f,0x82,0xd1,0xc9,0x63,0xc4,0x0b,0x3d,0x2d,0x55,0x78,0x66,0x61,0xdf,0xee,0x2b,0x18,0xcb,0xbf,0x38,0x8a,0x71,0x0c,0x00,0xfa,0x76,0xc5,0xe9,0xbb,0x45};
    return ok&&std::memcmp(digest,expected,sizeof(expected))==0;
}
bool ExactGameDirectoryAndMarker() noexcept {
    wchar_t declared[32768]{},exe[32768]{},canonical[32768]{},marker[32768]{};
    if(!ReadEnv(L"MGS_WORKER_GAME_DIR",declared,ARRAYSIZE(declared))||
       std::wcslen(declared)<3||declared[1]!=L':'||declared[2]!=L'\\'||
       std::wcsstr(declared,L"\\..")||std::wcsstr(declared,L"\\.\\"))return false;
    const DWORD length=GetModuleFileNameW(nullptr,exe,ARRAYSIZE(exe));
    if(!length||length>=ARRAYSIZE(exe))return false;
    wchar_t* slash=std::wcsrchr(exe,L'\\');if(!slash)return false;*slash=0;
    if(_wcsicmp(exe,declared))return false;
    HANDLE directory=CreateFileW(declared,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(directory==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};const DWORD final_length=GetFinalPathNameByHandleW(directory,canonical,ARRAYSIZE(canonical),FILE_NAME_NORMALIZED);
    const bool exact=GetFileInformationByHandle(directory,&info)&&(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&
        !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&final_length>4&&final_length<ARRAYSIZE(canonical)&&
        !std::wcsncmp(canonical,L"\\\\?\\",4)&&!_wcsicmp(canonical+4,declared);
    bool ok=exact&&SUCCEEDED(StringCchPrintfW(marker,ARRAYSIZE(marker),L"%s\\mgs-coop\\script-observe.enabled",declared));
    HANDLE file=ok?CreateFileW(marker,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr):INVALID_HANDLE_VALUE;
    char bytes[12]{};DWORD read=0;LARGE_INTEGER size{};BY_HANDLE_FILE_INFORMATION marker_info{};
    ok=file!=INVALID_HANDLE_VALUE&&GetFileInformationByHandle(file,&marker_info)&&
        !(marker_info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&
        GetFileSizeEx(file,&size)&&size.QuadPart==11&&ReadFile(file,bytes,sizeof(bytes),&read,nullptr)&&
        read==11&&!std::memcmp(bytes,"offline-v1\n",11);
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);CloseHandle(directory);return ok;
}
BOOL CALLBACK InitializeOnce(PINIT_ONCE,PVOID parameter,PVOID*) noexcept {
    const auto& authorization=*static_cast<const Authorization*>(parameter);
    wchar_t id[64]{},r[16]{},expected[80]{};
    requested.store(GetEnvironmentVariableW(L"MGS_WORKER_RUN_ID",id,ARRAYSIZE(id))!=0);
    if(!requested.load())return TRUE;
    if(!ValidAuthorization(authorization)||!ReadEnv(L"MGS_WORKER_RUN_ID",id,ARRAYSIZE(id))||
       !ReadEnv(L"MGS_WORKER_ROLE",r,ARRAYSIZE(r))||!ReadEnv(L"MGS_WORKER_RUN_DIR",run_dir,ARRAYSIZE(run_dir))||
       !ReadEnv(L"MGS_WORKER_DESKTOP",desktop,ARRAYSIZE(desktop)))return TRUE;
    for(unsigned i=0;i<32;++i){if(id[i]>127)return TRUE;run_id[i]=static_cast<char>(id[i]);}
    if(id[32]||!ValidRunId(run_id)||(std::wcscmp(r,L"host")&&std::wcscmp(r,L"guest")))return TRUE;
    for(unsigned i=0;r[i];++i)role[i]=static_cast<char>(r[i]);
    if(FAILED(StringCchPrintfW(expected,ARRAYSIZE(expected),L"MGSWorker_%s",id))||std::wcscmp(expected,desktop)||!PrivateThread())return TRUE;
    if(std::wcslen(run_dir)<3||run_dir[1]!=L':'||run_dir[2]!=L'\\'||std::wcsstr(run_dir,L"\\..")||std::wcsstr(run_dir,L"\\.\\"))return TRUE;
    HANDLE directory=CreateFileW(run_dir,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(directory==INVALID_HANDLE_VALUE)return TRUE;
    BY_HANDLE_FILE_INFORMATION information{};wchar_t canonical[32768]{};
    const bool dir_ok=GetFileInformationByHandle(directory,&information)&&(information.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&
        !(information.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&GetFinalPathNameByHandleW(directory,canonical,ARRAYSIZE(canonical),FILE_NAME_NORMALIZED);
    CloseHandle(directory);if(!dir_ok||std::wcsncmp(canonical,L"\\\\?\\",4)||_wcsicmp(canonical+4,run_dir)||
        !ExactGameDirectoryAndMarker()||!ExactExecutable())return TRUE;
    process_id=GetCurrentProcessId();FILETIME creation{},exit{},kernel{},user{};
    if(!GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user))return TRUE;
    process_start=(static_cast<std::uint64_t>(creation.dwHighDateTime)<<32)|creation.dwLowDateTime;
    wchar_t name[64]{},path[32768]{};StringCchPrintfW(name,ARRAYSIZE(name),L"%S.worker.jsonl",role);
    if(!Path(path,name))return TRUE;
    receipt=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(receipt==INVALID_HANDLE_VALUE)return TRUE;
    configured.store(true);Receipt("configured","actual_gates_private_desktop_exact_executable",0);
    return TRUE;
}
void Consumed(Device device) noexcept {
    const auto sequence=scenario.input_command(device);auto& last=device==Device::keyboard?consumed_keyboard:consumed_mouse;
    if(sequence&&sequence!=last) {last=sequence;Receipt("input_consumed",device==Device::keyboard?"keyboard_api":"mouse_api",sequence);}
}
}
bool Initialize(const Authorization& authorization) noexcept {
    const DWORD error=GetLastError();InitOnceExecuteOnce(&initialize_once,&InitializeOnce,const_cast<Authorization*>(&authorization),nullptr);
    const bool ok=configured.load();SetLastError(error);return ok;
}
bool Requested() noexcept {
    if(requested.load())return true;
    const DWORD error=GetLastError();wchar_t id[2]{};
    const bool present=GetEnvironmentVariableW(L"MGS_WORKER_RUN_ID",id,ARRAYSIZE(id))!=0;
    SetLastError(error);return present;
}
bool Active() noexcept { return configured.load()&&hooks_ready.load()&&!refused.load(); }
bool ThreadAdmitted() noexcept {const DWORD error=GetLastError();const bool ok=Active()&&PrivateThread();SetLastError(error);return ok;}
RunContext GetContext() noexcept {return configured.load()?RunContext{run_id,role,run_dir,desktop,process_id,process_start}:RunContext{};}
void Refuse(const char* reason) noexcept {
    const DWORD error=GetLastError();AcquireSRWLockExclusive(&state_lock);scenario.Clear();virtual_pad.Clear();refused.store(true);
    Receipt("refused",reason,scenario.sequence());ReleaseSRWLockExclusive(&state_lock);SetLastError(error);
}
void SetHooksReady(bool ready) noexcept {
    if(!configured.load()||refused.load())return;
    if(!ready||!PrivateThread()){Refuse("required_hooks_or_private_desktop");return;}
#ifdef MGS_WORKER_TESTING
    if(mock_lease){hooks_ready.store(true);return;}
#endif
    const DWORD error=GetLastError();AcquireSRWLockExclusive(&state_lock);
    wchar_t name[64]{},path[32768]{},temporary[32768]{};
    StringCchPrintfW(name,ARRAYSIZE(name),L"%S.context",role);
    bool ok=Path(path,name)&&SUCCEEDED(StringCchPrintfW(temporary,ARRAYSIZE(temporary),L"%s.tmp",path));
    HANDLE file=ok?CreateFileW(temporary,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr):INVALID_HANDLE_VALUE;
    char text[256]{};StringCchPrintfA(text,ARRAYSIZE(text),"MGSWORKERCTX1 %s %s %lu %llu MGSWorker_%s admitted\n",run_id,role,process_id,process_start,run_id);
    DWORD written=0;ok=file!=INVALID_HANDLE_VALUE&&WriteFile(file,text,static_cast<DWORD>(std::strlen(text)),&written,nullptr)&&written==std::strlen(text)&&FlushFileBuffers(file);
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    if(ok)ok=MoveFileExW(temporary,path,MOVEFILE_WRITE_THROUGH)!=FALSE;
    hooks_ready.store(ok);if(!ok)refused.store(true);
    Receipt(ok?"admitted":"refused",ok?"hooks_ready_private_context":"context_receipt",0);
    ReleaseSRWLockExclusive(&state_lock);SetLastError(error);
}
bool LoadingDeliveryAllowed(std::uint32_t mission,std::uint32_t generation,std::uint64_t expires,std::uint64_t now) noexcept {
    if(!ThreadAdmitted())return false;
    const DWORD error=GetLastError();AcquireSRWLockShared(&state_lock);
    const bool ok=scenario.LoadingDeliveryAllowed(mission,generation,expires,now);
    ReleaseSRWLockShared(&state_lock);SetLastError(error);return ok;
}
void SetLoadingContext(LoadingContext c) noexcept {
    if(!Active())return;
    const DWORD error=GetLastError();AcquireSRWLockExclusive(&state_lock);
    const auto previous=scenario.loading();
    if(previous.phase!=c.phase || previous.mission!=c.mission || previous.generation!=c.generation)
        virtual_pad.Clear();
    scenario.SetLoading(c,GetTickCount64());
    // Atomic owned metadata survives a loading-tip pause with no Lua Update tick.
    // Controller validates the supervisor/process lease; native consumption validates this token again.
#ifdef MGS_WORKER_TESTING
    if(mock_lease){ReleaseSRWLockExclusive(&state_lock);SetLastError(error);return;}
#endif
    wchar_t name[64]{},path[32768]{},temporary[32768]{};
    StringCchPrintfW(name,ARRAYSIZE(name),L"%S.loading.json",role);
    bool ok=Path(path,name)&&SUCCEEDED(StringCchPrintfW(temporary,ARRAYSIZE(temporary),L"%s.%u.%u.tmp",path,c.generation,unsigned(c.phase)));
    HANDLE file=ok?CreateFileW(temporary,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr):INVALID_HANDLE_VALUE;
    char text[512]{};DWORD written=0;
    const bool formatted=SUCCEEDED(StringCchPrintfA(text,ARRAYSIZE(text),
        "{\"run_id\":\"%s\",\"role\":\"%s\",\"pid\":%lu,\"process_start_filetime\":%llu,\"tick_ms\":%llu,\"phase\":\"%s\",\"mission\":%u,\"generation\":%u}\n",
        run_id,role,process_id,process_start,GetTickCount64(),LoadingPhaseName(c.phase),c.mission,c.generation));
    ok=file!=INVALID_HANDLE_VALUE&&formatted&&WriteFile(file,text,static_cast<DWORD>(std::strlen(text)),&written,nullptr)&&
        written==std::strlen(text)&&FlushFileBuffers(file);
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    if(ok)ok=MoveFileExW(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    Receipt(ok?"loading_context":"loading_context_refused",ok?LoadingPhaseName(c.phase):"metadata_write",0);
    ReleaseSRWLockExclusive(&state_lock);SetLastError(error);
}
void Poll(std::uint64_t now) noexcept {
    if(!ThreadAdmitted())return;
    const DWORD error=GetLastError();AcquireSRWLockExclusive(&state_lock);scenario.Tick(now);
#ifdef MGS_WORKER_TESTING
    if(mock_lease){ReleaseSRWLockExclusive(&state_lock);SetLastError(error);return;}
#endif
    if(last_poll&&now-last_poll<20){ReleaseSRWLockExclusive(&state_lock);SetLastError(error);return;}last_poll=now;
    wchar_t name[64]{},path[32768]{};StringCchPrintfW(name,ARRAYSIZE(name),L"%S.input",role);
    if(Path(path,name)) {
        HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file!=INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};char text[513]{};DWORD read=0;
            const bool read_ok=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&
                GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<=512&&ReadFile(file,text,512,&read,nullptr)&&static_cast<LONGLONG>(read)==size.QuadPart;
            CloseHandle(file);Command command{};
            if(read_ok&&ParseCommand(text,read,run_id,role,command)&&command.sequence>last_file_sequence) {
                last_file_sequence=command.sequence;
                const char* refusal=command.operation==Operation::resume_loading?scenario.ResumeRefusal(command):nullptr;
                const bool accepted=scenario.Accept(command,now);
                Receipt(accepted?"command_accepted":"command_refused",accepted?
                    (command.operation==Operation::resume_loading?"loading_space_120ms":"typed_bounded"):
                    refusal?refusal:"sequence_expiry_or_busy",command.sequence);
            }
        }
    }
    ReleaseSRWLockExclusive(&state_lock);SetLastError(error);
}
bool SetVirtualPad(const char* canonical,std::uint64_t sequence) noexcept {
    VirtualPadRequest request{};
    if(!sequence||sequence>4096||!ParseVirtualPad(canonical,request)||!ThreadAdmitted())return false;
    const DWORD error=GetLastError();AcquireSRWLockExclusive(&state_lock);
    const bool ok=sequence>virtual_pad_sequence && virtual_pad.Apply(request,GetTickCount64());
    if(ok) {virtual_pad_sequence=sequence;Receipt("virtual_pad_command",request.enabled?"bounded_1500ms_max":"off_neutral",sequence);}
    ReleaseSRWLockExclusive(&state_lock);SetLastError(error);return ok;
}
void ClearVirtualPad() noexcept {
    const DWORD error=GetLastError();AcquireSRWLockExclusive(&state_lock);virtual_pad.Clear();
    ReleaseSRWLockExclusive(&state_lock);SetLastError(error);
}
bool ReadVirtualPad(unsigned index,void* output,std::size_t bytes) noexcept {
    if(!output||bytes!=sizeof(VirtualPadState))return false;
    std::memset(output,0,bytes);
    if(index!=0||!ThreadAdmitted())return false;
    const DWORD error=GetLastError();AcquireSRWLockExclusive(&state_lock);
    virtual_pad.Tick(GetTickCount64());VirtualPadState state{};const bool ok=virtual_pad.Read(state);
    if(ok) {std::memcpy(output,&state,sizeof(state));
        if(consumed_pad!=virtual_pad_sequence) {consumed_pad=virtual_pad_sequence;Receipt("input_consumed","virtual_gamepad_api",virtual_pad_sequence);}}
    ReleaseSRWLockExclusive(&state_lock);SetLastError(error);return ok;
}
bool Keyboard(void* bytes,std::size_t size) noexcept {
    if(!bytes||size!=256||!ThreadAdmitted())return false;
    const DWORD error=GetLastError();Poll(GetTickCount64());AcquireSRWLockExclusive(&state_lock);
    std::uint8_t state[256]{};scenario.Keyboard(state);std::memcpy(bytes,state,256);Consumed(Device::keyboard);
    ReleaseSRWLockExclusive(&state_lock);SetLastError(error);return true;
}
bool Mouse(void* bytes,std::size_t size) noexcept {
    if(!bytes||(size!=16&&size!=20)||!ThreadAdmitted())return false;
    const DWORD error=GetLastError();Poll(GetTickCount64());AcquireSRWLockExclusive(&state_lock);
    const auto state=scenario.Mouse(true);std::memcpy(bytes,&state,size);Consumed(Device::mouse);
    ReleaseSRWLockExclusive(&state_lock);SetLastError(error);return true;
}
std::uint32_t Buffered(Device device,Event* events,std::uint32_t capacity,bool peek) noexcept {
    if(!ThreadAdmitted()||capacity>64)return 0;
    const DWORD error=GetLastError();Poll(GetTickCount64());AcquireSRWLockExclusive(&state_lock);
    const auto count=scenario.Events(device,events,capacity,peek);if(count&&!peek)Consumed(device);
    ReleaseSRWLockExclusive(&state_lock);SetLastError(error);return count;
}
CaptureRequest TakeCapture(std::uint64_t now) noexcept {
    if(!ThreadAdmitted())return {};Poll(now);AcquireSRWLockExclusive(&state_lock);
    const auto request=scenario.TakeCapture(now);ReleaseSRWLockExclusive(&state_lock);return request;
}
void FinishCapture(const CaptureRequest& request,bool ok,const char* reason,std::uint32_t width,std::uint32_t height) noexcept {
    const DWORD error=GetLastError();AcquireSRWLockExclusive(&state_lock);
    Receipt(ok?"frame_written":"frame_refused",reason,request.sequence,0,width,height);
    ReleaseSRWLockExclusive(&state_lock);SetLastError(error);
}
void InputFailure(const char* api,std::int32_t result,bool typed) noexcept {
    const DWORD error=GetLastError();AcquireSRWLockExclusive(&state_lock);
    if(diagnostic_count++<32)Receipt("input_api_refused",typed?api:"unqualified_data_format",scenario.sequence(),result);
    ReleaseSRWLockExclusive(&state_lock);SetLastError(error);
}
void DeviceConfiguration(const char* api,std::int32_t result,bool typed,std::uint32_t requested_flags,std::uint32_t forwarded) noexcept {
    const DWORD error=GetLastError();AcquireSRWLockExclusive(&state_lock);
    if(diagnostic_count++<32)Receipt("input_configuration",typed?api:"unqualified_data_format",scenario.sequence(),result,0,0,requested_flags,forwarded);
    ReleaseSRWLockExclusive(&state_lock);SetLastError(error);
}
#ifdef MGS_WORKER_TESTING
void ConfigureForTest(const Authorization& authorization,bool private_thread,bool ready) noexcept {
    AcquireSRWLockExclusive(&state_lock);mock_lease=true;mock_private=private_thread;
    std::memcpy(run_id,"0123456789abcdef0123456789abcdef",33);std::memcpy(role,"host",5);
    desktop[0]=0;run_dir[0]=0;process_id=GetCurrentProcessId();scenario={};virtual_pad={};virtual_pad_sequence=consumed_pad=0;last_poll=last_file_sequence=0;
    consumed_keyboard=consumed_mouse=0;diagnostic_count=0;refused.store(false);requested.store(true);
    configured.store(ValidAuthorization(authorization)&&private_thread);hooks_ready.store(ready&&configured.load());
    ReleaseSRWLockExclusive(&state_lock);
}
bool AcceptForTest(const Command& command,std::uint64_t now) noexcept {
    AcquireSRWLockExclusive(&state_lock);const bool ok=Active()&&mock_lease&&mock_private&&scenario.Accept(command,now);
    ReleaseSRWLockExclusive(&state_lock);return ok;
}
void SetPrivateForTest(bool value) noexcept {mock_private=value;}
TestStats StatsForTest() noexcept {return {consumed_keyboard,consumed_mouse,diagnostic_count};}
#endif
#else
bool Initialize(const Authorization&) noexcept{return false;}
bool Requested() noexcept{return false;}
bool Active() noexcept{return false;}
bool ThreadAdmitted() noexcept{return false;}
RunContext GetContext() noexcept{return {};}
void SetHooksReady(bool) noexcept{}
void Refuse(const char*) noexcept{}
void Poll(std::uint64_t) noexcept{}
void SetLoadingContext(LoadingContext) noexcept{}
bool LoadingDeliveryAllowed(std::uint32_t,std::uint32_t,std::uint64_t,std::uint64_t) noexcept{return false;}
bool SetVirtualPad(const char*,std::uint64_t) noexcept{return false;}
void ClearVirtualPad() noexcept{}
bool ReadVirtualPad(unsigned,void* out,std::size_t bytes) noexcept{if(out&&bytes==sizeof(VirtualPadState))std::memset(out,0,bytes);return false;}
bool Keyboard(void*,std::size_t) noexcept{return false;}
bool Mouse(void*,std::size_t) noexcept{return false;}
std::uint32_t Buffered(Device,Event*,std::uint32_t,bool) noexcept{return 0;}
CaptureRequest TakeCapture(std::uint64_t) noexcept{return {};}
void FinishCapture(const CaptureRequest&,bool,const char*,std::uint32_t,std::uint32_t) noexcept{}
void InputFailure(const char*,std::int32_t,bool) noexcept{}
void DeviceConfiguration(const char*,std::int32_t,bool,std::uint32_t,std::uint32_t) noexcept{}
#ifdef MGS_WORKER_TESTING
void ConfigureForTest(const Authorization&,bool,bool) noexcept{}
bool AcceptForTest(const Command&,std::uint64_t) noexcept{return false;}
void SetPrivateForTest(bool) noexcept{}
TestStats StatsForTest() noexcept{return {};}
#endif
#endif
} // namespace mgscoop::worker_scenario
