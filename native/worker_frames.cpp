#include "mgscoop/worker_frames.hpp"
#if MGS_BACKGROUND_WORKER && defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <strsafe.h>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace mgscoop::worker_frames {
namespace {
template<class T> void Release(T*& value) noexcept {if(value)value->Release();value=nullptr;}
bool Write(HANDLE file,const void* bytes,DWORD size) noexcept {
    DWORD written=0;return WriteFile(file,bytes,size,&written,nullptr)&&written==size;
}
struct Pending {
    IDXGISwapChain* chain{}; // identity only, never dereferenced outside Present
    ID3D11Device* device{}; ID3D11DeviceContext* context{}; ID3D11Texture2D* staging{};
    worker_scenario::CaptureRequest request{}; D3D11_TEXTURE2D_DESC desc{};
};
Pending pending{};
}
void Reset() noexcept {
    if(pending.request.sequence)worker_scenario::FinishCapture(pending.request,false,"resize_or_capture_reset",pending.desc.Width,pending.desc.Height);
    Release(pending.staging);Release(pending.context);Release(pending.device);pending={};
}
void Capture(IDXGISwapChain* chain,const worker_scenario::CaptureRequest& request) noexcept {
    if(!chain||(!request.sequence&&!pending.request.sequence))return;
    if(!worker_scenario::ThreadAdmitted()){Reset();return;}
    if(pending.request.sequence&&pending.chain!=chain){Reset();}
    if(pending.request.sequence&&request.sequence)worker_scenario::FinishCapture(request,false,"capture_busy",0,0);
    const auto actual=pending.request.sequence?pending.request:request;
    if(!actual.sequence)return;
    const DWORD error=GetLastError();
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    ID3D11Texture2D* source=nullptr;ID3D11Texture2D* staging=nullptr;
    unsigned char* pixels=nullptr;HANDLE file=INVALID_HANDLE_VALUE;
    bool mapped=false,ok=false,deferred=false;const char* reason="backbuffer_qualification";
    D3D11_TEXTURE2D_DESC desc{};DXGI_SWAP_CHAIN_DESC chain_desc{};
    wchar_t output[32768]{},temporary[32768]{};D3D11_MAPPED_SUBRESOURCE memory{};
    const bool already_copied=pending.request.sequence!=0;
    if(already_copied) {device=pending.device;context=pending.context;staging=pending.staging;desc=pending.desc;pending={};}
    do {
        if(GetTickCount64()>=actual.expires||FAILED(chain->GetDesc(&chain_desc)))break;
        DWORD pid=0;const DWORD window_thread=GetWindowThreadProcessId(chain_desc.OutputWindow,&pid);
        wchar_t desktop_name[256]{};DWORD needed=0;
        const auto run=worker_scenario::GetContext();
        if(!window_thread||pid!=run.pid||!GetUserObjectInformationW(GetThreadDesktop(window_thread),UOI_NAME,desktop_name,sizeof(desktop_name),&needed)||
           needed>sizeof(desktop_name)||std::wcscmp(desktop_name,run.desktop))break;
        if(!already_copied) {
        if(FAILED(chain->GetDevice(__uuidof(ID3D11Device),reinterpret_cast<void**>(&device)))||FAILED(device->GetDeviceRemovedReason())||
           FAILED(chain->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&source))))break;
        source->GetDesc(&desc);
        if(!desc.Width||!desc.Height||desc.Width>2560||desc.Height>1440||desc.MipLevels!=1||desc.ArraySize!=1||
           desc.SampleDesc.Count!=1||(desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB&&
           desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM&&desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB))break;
        device->GetImmediateContext(&context);if(!context)break;
        D3D11_TEXTURE2D_DESC copy=desc;copy.Usage=D3D11_USAGE_STAGING;copy.BindFlags=0;copy.CPUAccessFlags=D3D11_CPU_ACCESS_READ;copy.MiscFlags=0;
        if(FAILED(device->CreateTexture2D(&copy,nullptr,&staging))) {reason="staging_creation";break;}
        context->CopyResource(staging,source);
        Release(source);
        }
        if(FAILED(device->GetDeviceRemovedReason())) {reason="device_removed";break;}
        // Never wait for GPU completion. Busy staging is an explicit refusal.
        const HRESULT map=context->Map(staging,0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&memory);
        if(map==DXGI_ERROR_WAS_STILL_DRAWING) {
            pending={chain,device,context,staging,actual,desc};device=nullptr;context=nullptr;staging=nullptr;deferred=true;break;
        }
        if(FAILED(map)) {reason="staging_map_failed";break;}
        mapped=true;const auto stride=desc.Width*4u;const auto size=stride*desc.Height;
        if(!memory.pData||memory.RowPitch<stride||memory.RowPitch>65536) {reason="row_pitch";break;}
        pixels=static_cast<unsigned char*>(HeapAlloc(GetProcessHeap(),0,size));if(!pixels){reason="bounded_allocation";break;}
        const bool rgba=desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM||desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        for(unsigned y=0;y<desc.Height;++y) {
            const auto* row=static_cast<const unsigned char*>(memory.pData)+static_cast<std::size_t>(y)*memory.RowPitch;
            auto* out=pixels+static_cast<std::size_t>(y)*stride;
            for(unsigned x=0;x<desc.Width;++x) {out[4*x]=row[4*x+(rgba?2:0)];out[4*x+1]=row[4*x+1];out[4*x+2]=row[4*x+(rgba?0:2)];out[4*x+3]=255;}
        }
        context->Unmap(staging,0);mapped=false;
        if(!worker_scenario::ThreadAdmitted()||GetTickCount64()>=actual.expires){reason="expired_or_desktop_changed";break;}
        if(FAILED(StringCchPrintfW(output,ARRAYSIZE(output),L"%s\\%S-frame-%llu.bmp",run.run_dir,run.role,actual.sequence))||
           FAILED(StringCchPrintfW(temporary,ARRAYSIZE(temporary),L"%s.tmp",output))){reason="output_path";break;}
        file=CreateFileW(temporary,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file==INVALID_HANDLE_VALUE){reason="output_create";break;}
        BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+size;
        BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=static_cast<LONG>(desc.Width);info.biHeight=-static_cast<LONG>(desc.Height);
        info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;info.biSizeImage=size;
        if(!Write(file,&header,sizeof(header))||!Write(file,&info,sizeof(info))||!Write(file,pixels,size)||!FlushFileBuffers(file)){reason="output_write";break;}
        CloseHandle(file);file=INVALID_HANDLE_VALUE;
        ok=MoveFileExW(temporary,output,MOVEFILE_WRITE_THROUGH)!=FALSE;reason=ok?"dx11_backbuffer_bmp":"output_publish";
    } while(false);
    if(mapped&&context)context->Unmap(staging,0);
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    if(pixels)HeapFree(GetProcessHeap(),0,pixels);
    Release(staging);Release(source);Release(context);Release(device);
    if(!deferred)worker_scenario::FinishCapture(actual,ok,reason,desc.Width,desc.Height);
    SetLastError(error);
}
}
#else
namespace mgscoop::worker_frames {
void Capture(IDXGISwapChain*,const worker_scenario::CaptureRequest&) noexcept{}
void Reset() noexcept{}
}
#endif
