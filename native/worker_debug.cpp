#include "mgscoop/worker_debug.hpp"
#include <charconv>
#include <cstring>

namespace mgscoop::worker_debug {
bool Parse(const char* text, std::size_t size, const char* run, const char* role, Request& out) noexcept {
    out = {};
    if (!text || !run || !role || size == 0 || size > 384 || std::strlen(run) != 32 ||
        (std::strcmp(role,"host") && std::strcmp(role,"guest"))) return false;
    for (unsigned i=0;i<32;++i) if (!((run[i]>='0' && run[i]<='9') || (run[i]>='a' && run[i]<='f'))) return false;
    if (text[size-1]=='\n') --size;
    for (std::size_t i=0;i<size;++i) if (text[i]<32 || text[i]>126) return false;
    const char* p=text; const char* end=text+size;
    auto word=[&](const char* value) noexcept {
        const auto length=std::strlen(value);
        if (static_cast<std::size_t>(end-p)<=length || std::memcmp(p,value,length) || p[length]!=' ') return false;
        p+=length+1;return true;
    };
    if (!word("MGSWORKERDEBUG1") || !word(run) || !word(role)) return false;
    auto number=[&](std::uint64_t& value) noexcept {
        const char* start=p;
        while(p<end && *p>='0' && *p<='9') ++p;
        if(p==start || p==end || *p!=' ' || std::from_chars(start,p,value).ec!=std::errc{}) return false;
        ++p;return true;
    };
    std::uint64_t mission=0, generation=0;
    if(!number(out.sequence) || !number(out.expires) || !number(mission) || !number(generation) ||
       !out.sequence || !out.expires || !mission || mission>65535 || !generation || generation>UINT32_MAX ||
       p==end || end-p>200 || *p==' ' || end[-1]==' ') return false;
    out.mission=static_cast<std::uint32_t>(mission);out.generation=static_cast<std::uint32_t>(generation);
    std::memcpy(out.text,p,static_cast<std::size_t>(end-p)); return true;
}
bool Timely(const Request& request,std::uint64_t now,std::uint64_t previous) noexcept {
    return request.sequence>previous && request.expires>now && request.expires-now<=15000;
}
}

#if defined(_WIN32) && defined(MGS_BACKGROUND_WORKER) && MGS_BACKGROUND_WORKER
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <cstdarg>
#include <cmath>
#include "mgscoop/debug_menu.hpp"
#include "mgscoop/debug_overlay.hpp"
#include "mgscoop/worker_scenario.hpp"
namespace mgscoop::worker_debug {
namespace {
std::uint64_t last_poll{}, last_sequence{}, accepted_sequence{};
bool last_queued{};
struct Json {
    char bytes[65536]{}; std::size_t used{}; bool ok{true};
    void Add(const char* format, ...) noexcept {
        if(!ok) return;
        va_list args; va_start(args,format);
        const int n=vsnprintf_s(bytes+used,sizeof(bytes)-used,_TRUNCATE,format,args);
        va_end(args);if(n<0){ok=false;return;}used+=static_cast<std::size_t>(n);
    }
    void String(const char* s,std::size_t capacity) noexcept {
        Add("\"");
        for(std::size_t i=0;s && i<capacity && s[i];++i) {
            const auto c=static_cast<unsigned char>(s[i]);
            if(c=='"' || c=='\\') Add("\\%c",c);
            else if(c<32 || c>126) Add("\\u%04x",static_cast<unsigned>(c));
            else Add("%c",c);
        }
        Add("\"");
    }
    void Float(float value) noexcept { if(std::isfinite(value)) Add("%.5f",static_cast<double>(value));else Add("null"); }
    void Position(const float (&v)[3]) noexcept {Add("[");for(unsigned i=0;i<3;++i){if(i)Add(",");Float(v[i]);}Add("]");}
};
const char* Bool(bool value) noexcept {return value?"true":"false";}
void Publish(const worker_scenario::RunContext& c) noexcept {
    const auto s=debug_menu::GetSnapshot();Json j;
    j.Add("{\"schema\":1,\"run_id\":\"%s\",\"role\":\"%s\",\"pid\":%u,\"process_start_filetime\":%llu,\"tick_ms\":%llu,\"debug_sequence\":%llu,\"queued_sequence\":%llu,\"last_queued\":%s,",
        c.run_id,c.role,c.pid,c.process_start_filetime,GetTickCount64(),last_sequence,accepted_sequence,Bool(last_queued));
    j.Add("\"mission\":%u,\"generation\":%u,\"enabled\":%s,\"offline\":%s,\"saves_redirected\":%s,\"gameplay_ready\":%s,\"terminating\":%s,\"realized\":%s,\"body_valid\":%s,\"pose_valid\":%s,",
        s.mission,s.generation,Bool(s.enabled),Bool(s.offline),Bool(s.saves_redirected),Bool(s.gameplay_ready),Bool(s.terminating),Bool(s.realized),Bool(s.body_valid),Bool(s.pose_valid));
    j.Add("\"world_level\":%u,\"world_paired\":%s,\"world_effective\":%u,\"remote_action_flags\":%u,\"remote_equip\":%u,\"remote_act\":%u,\"mapped_clip\":%u,\"motion_count\":%u,\"remote_age_ms\":%u,",
        s.world_level,Bool(s.world_paired),s.world_effective,s.remote_action_flags,s.remote_equip,s.remote_act,s.mapped_clip,s.motion_count,s.remote_age_ms);
    j.Add("\"local_anim_valid\":%s,\"local_act\":%u,\"local_anim_stance\":%u,\"local_anim_speed\":%u,\"remote_anim_valid\":%s,\"remote_stance\":%u,\"remote_anim_stance\":%u,\"remote_anim_speed\":%u,\"dm_stance\":%u,\"dm_move\":%u,\"dm_phase\":",
        Bool(s.local_anim_valid),s.local_act,s.local_anim_stance,s.local_anim_speed,Bool(s.remote_anim_valid),s.remote_stance,s.remote_anim_stance,s.remote_anim_speed,s.dm_stance,s.dm_move);
    j.String(s.dm_phase,sizeof(s.dm_phase));j.Add(",\"local_yaw_valid\":%s,\"player_yaw\":",Bool(s.local_yaw_valid));j.Float(s.player_yaw);
    j.Add(",\"remote_yaw\":");j.Float(s.remote_yaw);j.Add(",\"body_yaw\":");j.Float(s.body_yaw);j.Add(",");
    j.Add("\"dm_speed_valid\":%s,\"dm_speed_mmps\":%u,",Bool(s.dm_speed_valid),s.dm_speed_mmps);
    j.Add("\"player_pos\":");j.Position(s.player_pos);j.Add(",\"remote_pos\":");j.Position(s.remote_pos);j.Add(",\"body_pos\":");j.Position(s.body_pos);
    j.Add(",\"heading_error_deg\":");j.Float(s.heading_error_deg);j.Add(",\"position_error_m\":");j.Float(s.position_error_m);
    j.Add(",\"queue_depth\":%u,\"last_id\":%u,\"completed\":%u,\"rejected\":%u,\"last_command\":",s.queue_depth,s.last_id,s.completed,s.rejected);
    j.String(s.last_command,sizeof(s.last_command));j.Add(",\"last_result\":");j.String(s.last_result,sizeof(s.last_result));j.Add(",\"refusal\":");j.String(s.refusal,sizeof(s.refusal));
    j.Add(",\"history\":[");for(unsigned i=0;i<8;++i){if(i)j.Add(",");j.String(s.history[i],sizeof(s.history[i]));}
    j.Add("],\"enemy_camps\":[");for(unsigned i=0;i<s.enemy_camp_count && i<32;++i){if(i)j.Add(",");j.Add("{\"name\":");j.String(s.enemy_camps[i].name,sizeof(s.enemy_camps[i].name));j.Add(",\"distance_mm\":%d,\"route_count\":%u}",s.enemy_camps[i].distance_mm,s.enemy_camps[i].route_count);}
    j.Add("],\"enemy_routes_camp\":");j.String(s.enemy_routes_camp,sizeof(s.enemy_routes_camp));j.Add(",\"enemy_routes\":[");
    for(unsigned i=0;i<s.enemy_route_count && i<64;++i){if(i)j.Add(",");j.Add("{\"name\":");j.String(s.enemy_routes[i].name,sizeof(s.enemy_routes[i].name));j.Add(",\"index\":%u}",s.enemy_routes[i].index);}
    j.Add("],\"worker_input\":{");
    const auto input = debug_overlay::GetWorkerInputStats();
    j.Add("\"raw_keyboard_posted\":%u", input.raw_keyboard_posted);
    j.Add(",\"raw_mouse_posted\":%u", input.raw_mouse_posted);
    j.Add(",\"raw_post_failed\":%u", input.raw_post_failed);
    j.Add(",\"raw_mouse_axes_posted\":%u", input.raw_mouse_axes_posted);
    j.Add(",\"raw_mouse_buttons_posted\":%u", input.raw_mouse_buttons_posted);
    j.Add(",\"raw_keyboard_reads\":%u", input.raw_keyboard_reads);
    j.Add(",\"raw_mouse_reads\":%u", input.raw_mouse_reads);
    j.Add(",\"raw_mouse_axes_reads\":%u", input.raw_mouse_axes_reads);
    j.Add(",\"raw_mouse_buttons_reads\":%u", input.raw_mouse_buttons_reads);
    j.Add(",\"raw_keyboard_packets_read\":%u", input.raw_keyboard_packets_read);
    j.Add(",\"raw_mouse_packets_read\":%u", input.raw_mouse_packets_read);
    j.Add(",\"raw_mouse_axis_packets_read\":%u", input.raw_mouse_axis_packets_read);
    j.Add(",\"raw_mouse_button_packets_read\":%u", input.raw_mouse_button_packets_read);
    j.Add(",\"raw_read_refused\":%u", input.raw_read_refused);
    j.Add(",\"cursor_success\":%u", input.cursor_success);
    j.Add(",\"cursor_null\":%u", input.cursor_null);
    j.Add(",\"cursor_thread_refused\":%u", input.cursor_thread_refused);
    j.Add(",\"cursor_window_refused\":%u", input.cursor_window_refused);
    j.Add(",\"cursor_geometry_refused\":%u", input.cursor_geometry_refused);
    j.Add(",\"cursor_map_refused\":%u", input.cursor_map_refused);
    j.Add(",\"cursor_window_match\":%u", input.cursor_window_match);
    j.Add(",\"cursor_window_mismatch\":%u", input.cursor_window_mismatch);
    j.Add(",\"cursor_present_missing\":%u", input.cursor_present_missing);
    j.Add(",\"di_mouse_state_calls\":%u", input.di_mouse_state_calls);
    j.Add(",\"di_mouse_state_supplied\":%u", input.di_mouse_state_supplied);
    j.Add(",\"di_mouse_state_axes\":%u", input.di_mouse_state_axes);
    j.Add(",\"di_mouse_state_buttons\":%u", input.di_mouse_state_buttons);
    j.Add(",\"di_mouse_buffer_calls\":%u", input.di_mouse_buffer_calls);
    j.Add(",\"di_mouse_buffer_events\":%u", input.di_mouse_buffer_events);
    j.Add(",\"di_mouse_buffer_peek_events\":%u", input.di_mouse_buffer_peek_events);
    j.Add("}}\n");if(!j.ok)return;
    wchar_t path[32768]{},temporary[32768]{};
    if(FAILED(StringCchPrintfW(path,ARRAYSIZE(path),L"%s\\%S.state.json",c.run_dir,c.role)) ||
       FAILED(StringCchPrintfW(temporary,ARRAYSIZE(temporary),L"%s.%llu.tmp",path,GetTickCount64())))return;
    HANDLE file=CreateFileW(temporary,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    BY_HANDLE_FILE_INFORMATION info{};DWORD written=0;
    const bool ok=GetFileInformationByHandle(file,&info) && !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) &&
        WriteFile(file,j.bytes,static_cast<DWORD>(j.used),&written,nullptr) && written==j.used;
    CloseHandle(file);
    // Bounded retry: sync clients/scanners can briefly hold the published file.
    for(int attempt=0;ok&&attempt<20;++attempt){if(MoveFileExW(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))break;
        const DWORD error=GetLastError();if((error!=ERROR_ACCESS_DENIED&&error!=ERROR_SHARING_VIOLATION)||attempt==19){DeleteFileW(temporary);break;}Sleep(5);}
}
}
void PollOwner() noexcept {
    if(!worker_scenario::Active() || !worker_scenario::ThreadAdmitted())return;
    const DWORD incoming=GetLastError();const auto now=GetTickCount64();
    if(last_poll && now-last_poll<100){SetLastError(incoming);return;}last_poll=now;
    const auto c=worker_scenario::GetContext();wchar_t path[32768]{};
    if(SUCCEEDED(StringCchPrintfW(path,ARRAYSIZE(path),L"%s\\%S.debug",c.run_dir,c.role))) {
        HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file!=INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};char text[385]{};DWORD read=0;
            const bool ok=GetFileInformationByHandle(file,&info) && !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) &&
                GetFileSizeEx(file,&size) && size.QuadPart>0 && size.QuadPart<=384 && ReadFile(file,text,384,&read,nullptr) && read==size.QuadPart;
            CloseHandle(file);Request request{};
            if(ok && Parse(text,read,c.run_id,c.role,request) && request.sequence>last_sequence) {
                const bool timely=Timely(request,now,last_sequence);last_sequence=request.sequence;
                last_queued=timely && debug_menu::QueueCommandForGeneration(request.text,request.mission,request.generation);
                if(last_queued)accepted_sequence=request.sequence;
            }
        }
    }
    Publish(c);SetLastError(incoming);
}
}
#else
namespace mgscoop::worker_debug {void PollOwner() noexcept {}}
#endif
