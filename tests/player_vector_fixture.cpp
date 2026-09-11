#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstring>

namespace {
alignas(16) std::array<float,4> source{1,2,3,4}, destination{};
HANDLE returned_event=nullptr, release_event=nullptr;
const wchar_t* gate=nullptr;
bool indexed=false, no_hit=false, skip_copy=false, copy_limit=false;
volatile LONG calls=0, correct=0;
}
extern "C" {
__declspec(dllexport) volatile float FixtureWrite0 = 0;
__declspec(dllexport) volatile float FixtureWrite1 = 0;
}
extern "C" __declspec(dllexport) __declspec(noinline)
void FixtureBaseline() {
    FixtureWrite0=0;
    FixtureWrite1=0;
}
extern "C" __declspec(dllexport) __declspec(noinline)
void FixtureIndexed(void* receiver, unsigned index, const float* value) {
    InterlockedIncrement(&calls);
    if(receiver==reinterpret_cast<void*>(0x12345678) && index==3 && value==source.data())
        InterlockedIncrement(&correct);
    memcpy(destination.data(),value,16);
    FixtureWrite0=value[0];
}
extern "C" __declspec(dllexport) __declspec(noinline)
void FixtureLocal(void* receiver, const float* value) {
    InterlockedIncrement(&calls);
    if(receiver==reinterpret_cast<void*>(0x12345678) && value==source.data())
        InterlockedIncrement(&correct);
    memcpy(destination.data(),value,16);
    FixtureWrite1=value[0];
}
DWORD WINAPI worker(void*) {
    const auto deadline=GetTickCount64()+45000;
    while(GetFileAttributesW(gate)==INVALID_FILE_ATTRIBUTES && GetTickCount64()<deadline) Sleep(10);
    if(GetFileAttributesW(gate)==INVALID_FILE_ATTRIBUTES) return 2;
    while(no_hit && IsDebuggerPresent() && GetTickCount64()<deadline) Sleep(10);
    if(skip_copy) for(int i=0;i<(copy_limit?61:1);++i) FixtureBaseline();
    if(indexed) FixtureIndexed(reinterpret_cast<void*>(0x12345678),3,source.data());
    else FixtureLocal(reinterpret_cast<void*>(0x12345678),source.data());
    SetEvent(returned_event);
    WaitForSingleObject(release_event,10000);
    return 0;
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=6) return 2;
    gate=argv[1]; indexed=wcscmp(argv[3],L"Indexed")==0;
    no_hit=wcscmp(argv[4],L"NoHit")==0;
    skip_copy=wcscmp(argv[5],L"SkipCopy")==0;
    copy_limit=wcscmp(argv[3],L"CopyLimit")==0;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    std::array<unsigned char,16> before_local{},before_indexed{};
    std::array<unsigned char,20> before_baseline{};
    memcpy(before_baseline.data(),reinterpret_cast<void*>(&FixtureBaseline),20);
    memcpy(before_local.data(),reinterpret_cast<void*>(&FixtureLocal),16);
    memcpy(before_indexed.data(),reinterpret_cast<void*>(&FixtureIndexed),16);
    returned_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    release_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);
    if(!returned_event||!release_event||!thread) return 3;
    const auto waited=WaitForSingleObject(returned_event,50000);
    CONTEXT context{}; context.ContextFlags=CONTEXT_DEBUG_REGISTERS;
    auto suspended=SuspendThread(thread);
    bool context_ok=suspended!=static_cast<DWORD>(-1) && GetThreadContext(thread,&context);
    if(suspended!=static_cast<DWORD>(-1)) ResumeThread(thread);
    bool detached=!IsDebuggerPresent();
    bool registers_clear=context_ok && (context.Dr7&0xffu)==0;
    bool bytes_ok=memcmp(before_local.data(),reinterpret_cast<void*>(&FixtureLocal),16)==0 &&
        memcmp(before_indexed.data(),reinterpret_cast<void*>(&FixtureIndexed),16)==0 &&
        memcmp(before_baseline.data(),reinterpret_cast<void*>(&FixtureBaseline),20)==0;
    bool data_ok=destination==std::array<float,4>{1,2,3,4} && source==destination &&
        FixtureWrite0==(indexed?1.0f:0.0f) && FixtureWrite1==(indexed?0.0f:1.0f);
    bool ok=waited==WAIT_OBJECT_0 && calls==1 && correct==1 && detached && registers_clear && bytes_ok && data_ok;
    FILE* out=nullptr;
    if(_wfopen_s(&out,argv[2],L"wb")!=0 || !out) ok=false;
    if(out) {
        fprintf(out,"{\"passed\":%s,\"calls\":%ld,\"arguments_preserved\":%s,\"data_preserved\":%s,\"debugger_detached\":%s,\"breakpoints_disabled\":%s,\"bytes_preserved\":%s}\n",
            ok?"true":"false",calls,correct==1?"true":"false",data_ok?"true":"false",detached?"true":"false",registers_clear?"true":"false",bytes_ok?"true":"false");
        fclose(out);
    }
    SetEvent(release_event); WaitForSingleObject(thread,10000);
    CloseHandle(thread); CloseHandle(returned_event); CloseHandle(release_event);
    return ok?0:1;
}
