// Original out-of-process field sampler. No game calls, writes, threads or saves.
// Static field derivation: archive/research-2026-10-23:docs/player-observation-candidates.md.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <bcrypt.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <iomanip>
#include <limits>

namespace {
constexpr std::array<unsigned char,32> accepted_hash{
    0x08,0x5c,0x2f,0x82,0xd1,0xc9,0x63,0xc4,0x0b,0x3d,0x2d,0x55,0x78,0x66,0x61,0xdf,
    0xee,0x2b,0x18,0xcb,0xbf,0x38,0x8a,0x71,0x0c,0x00,0xfa,0x76,0xc5,0xe9,0xbb,0x45};
constexpr DWORD image_size = 167890944;
constexpr std::uintptr_t vars_pointer_rva = 0x2C16920;
constexpr std::uintptr_t vars_backing_rva = 0x2C01360;
constexpr std::uintptr_t player_index_rva = 0x2BD3FD4;
constexpr std::uintptr_t player_bank_rva = 0x2BD3FD0;
constexpr std::uintptr_t player_records_rva = 0x2BD3FF0;
constexpr std::size_t player_record_bytes = 0xE0;
constexpr std::size_t player_reference_position_offset = 0x10;
constexpr std::size_t player_status_offset = 0xC0;
struct NamedStatus { const char* name; unsigned bit; };
constexpr std::array<NamedStatus,10> named_statuses{{
    {"STAND",4},{"SQUAT",5},{"CRAWL",6},{"NORMAL_ACTION",7},{"IDLE",9},
    {"WALK",21},{"RUN",22},{"DASH",23},{"DEAD",62},{"PARTS_ACTIVE",145}}};
static_assert(player_status_offset+5*sizeof(std::uint32_t)<=player_record_bytes);
static_assert(sizeof(float)==4);
static_assert(player_reference_position_offset+3*sizeof(float)<=player_record_bytes);
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE v) : value(v) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
bool digest(HANDLE file, std::array<unsigned char,32>& result) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0) >= 0;
    if(ok) ok = BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0) >= 0;
    std::array<unsigned char,65536> buffer{};
    while(ok) {
        DWORD count = 0;
        if(!ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr)) { ok=false; break; }
        if(!count) break;
        ok = BCryptHashData(hash,buffer.data(),count,0) >= 0;
    }
    if(ok) ok = BCryptFinishHash(hash,result.data(),static_cast<ULONG>(result.size()),0) >= 0;
    if(hash) BCryptDestroyHash(hash);
    if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    return ok;
}
bool read_at(HANDLE process, std::uintptr_t base, std::uintptr_t rva, void* out, std::size_t count) {
    if(rva > image_size || count > image_size-rva || base > std::numeric_limits<std::uintptr_t>::max()-image_size) return false;
    SIZE_T read = 0;
    return ReadProcessMemory(process,reinterpret_cast<void*>(base+rva),out,count,&read) && read==count;
}
template<std::size_t N> bool matches(HANDLE process,std::uintptr_t base,std::uintptr_t rva,const std::array<unsigned char,N>& expected) {
    std::array<unsigned char,N> actual{};
    return read_at(process,base,rva,actual.data(),actual.size()) && actual==expected;
}
bool code_matches(HANDLE process,std::uintptr_t base) {
    // Compare the complete index leaf and two known adapter push-call sites.
    // Field offsets/load widths rely on the separately documented static
    // analysis and whole-file hash, not on those two call instructions alone.
    // None of these addresses is evaluated as a callable native function.
    return matches(process,base,0x53C990,std::array<unsigned char,15>{0x8b,0x05,0x3e,0x76,0x69,0x02,0x83,0xf8,0x10,0x89,0x01,0x0f,0x92,0xc0,0xc3}) &&
        matches(process,base,0x9AAAF1,std::array<unsigned char,5>{0xe8,0x5a,0x6e,0x06,0x01}) &&
        matches(process,base,0x9AAC50,std::array<unsigned char,5>{0xe8,0xfb,0x6c,0x06,0x01}) &&
        // Named PlayerInfo status getter, valid-index address computation,
        // and bit extraction. Independently decoded from this exact file;
        // archive/research-2026-10-23:docs/player-update-investigation.md gives the name/bit derivation.
        matches(process,base,0x53CA40,std::array<unsigned char,12>{0x48,0x63,0x05,0x89,0x75,0x69,0x02,0x83,0xf9,0x10,0x72,0x12}) &&
        matches(process,base,0x53CA5E,std::array<unsigned char,27>{0x48,0xc1,0xe0,0x04,0x8b,0xc9,0x48,0x03,0xc1,0x48,0x8d,0x0d,0x82,0x75,0x69,0x02,0x48,0x69,0xc0,0xe0,0x00,0x00,0x00,0x48,0x03,0xc1,0xc3}) &&
        matches(process,base,0x5545D7,std::array<unsigned char,24>{0x8b,0xc8,0x8b,0xc0,0x48,0xc1,0xe8,0x05,0x83,0xe1,0x1f,0x8b,0x84,0x86,0xc0,0x00,0x00,0x00,0x0f,0xa3,0xc8,0x48,0x8b,0xcb}) &&
        // Complete local published-record selector and vector-copy getter.
        // The named near-player-position consumer uses the first three floats;
        // archive/research-2026-10-23:docs/player-vector-producer-investigation.md records the derivation.
        matches(process,base,0x53C9D0,std::array<unsigned char,61>{
            0x8b,0x0d,0xfe,0x75,0x69,0x02,0x48,0x63,0x05,0xf3,0x75,0x69,0x02,0x83,0xf9,0x10,
            0x72,0x12,0x48,0x69,0xc0,0x00,0x0e,0x00,0x00,0x48,0x8d,0x0d,0x00,0x76,0x69,0x02,
            0x48,0x03,0xc1,0xc3,0x48,0xc1,0xe0,0x04,0x48,0x03,0xc1,0x48,0x8d,0x0d,0xee,0x75,
            0x69,0x02,0x48,0x69,0xc0,0xe0,0x00,0x00,0x00,0x48,0x03,0xc1,0xc3}) &&
        matches(process,base,0x1E02E00,std::array<unsigned char,31>{
            0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xda,0xe8,0xc2,0x9b,0x73,0xfe,0x0f,0x28,
            0x40,0x10,0x48,0x8b,0xc3,0x66,0x0f,0x7f,0x03,0x48,0x83,0xc4,0x20,0x5b,0xc3});
}
bool network_code_matches(HANDLE process,std::uintptr_t base) {
    // Complete wrapper and accessor: B+0x2BEFC00 -> +0x30 -> +0x28.
    // These are byte comparisons, never callable function pointers.
    return matches(process,base,0x1E0AE40,std::array<unsigned char,37>{
        0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x0d,0xb3,0x4d,0xde,0x00,
        0x48,0x8b,0xda,0xe8,0x1b,0xb6,0x9c,0xfe,0x48,0x8b,0xd3,0x48,0x8b,
        0xc8,0x48,0x83,0xc4,0x20,0x5b,0xe9,0x1b,0x35,0x9e,0xfe}) &&
        matches(process,base,0x7D6470,std::array<unsigned char,9>{
            0x48,0x8b,0x41,0x30,0x48,0x8b,0x40,0x28,0xc3}) &&
        matches(process,base,0x7EE4A7,std::array<unsigned char,7>{0xc7,0x47,0x10,0x01,0x00,0x00,0x00}) &&
        matches(process,base,0x7EE54D,std::array<unsigned char,4>{0xc6,0x47,0x48,0x01}) &&
        matches(process,base,0x7EE55A,std::array<unsigned char,7>{0xc7,0x47,0x10,0x02,0x00,0x00,0x00}) &&
        matches(process,base,0x7ED711,std::array<unsigned char,10>{0x48,0x8d,0x05,0xf0,0x5d,0xa3,0x01,0x48,0x89,0x03});
}
bool read_object(HANDLE process,std::uintptr_t object,std::uintptr_t offset,void* out,std::size_t size) {
    // Only bounded reads in canonical user address space. ReadProcessMemory
    // contains invalid/unmapped-pointer faults; no remote address is dereferenced locally.
    constexpr std::uintptr_t highest=0x00007FFFFFFFFFFF;
    if(object<0x10000 || object>highest || offset>highest-object || size==0 || size>256 ||
       size-1>highest-(object+offset)) return false;
    SIZE_T read=0;
    return ReadProcessMemory(process,reinterpret_cast<const void*>(object+offset),out,size,&read) && read==size;
}
struct NetworkFields {
    const char* status="unreadable";
    std::array<std::uintptr_t,3> chain{};
    std::uintptr_t vtable=0;
    std::uint32_t state=0;
    unsigned char flag=0;
};
NetworkFields network_fields(HANDLE process,std::uintptr_t base) {
    NetworkFields result;
    if(!read_at(process,base,0x2BEFC00,&result.chain[0],sizeof(std::uintptr_t))) return result;
    if(!result.chain[0]) { result.status="root_absent"; return result; }
    if(!read_object(process,result.chain[0],0x30,&result.chain[1],sizeof(std::uintptr_t))) return result;
    if(!result.chain[1]) { result.status="branch_absent"; return result; }
    if(!read_object(process,result.chain[1],0x28,&result.chain[2],sizeof(std::uintptr_t))) return result;
    if(!result.chain[2]) { result.status="service_absent"; return result; }
    if(!read_object(process,result.chain[2],0,&result.vtable,sizeof(result.vtable))) return result;
    if(result.vtable!=base+0x2223508) {
        result.status="service_type_unverified"; return result;
    }
    if(!read_object(process,result.chain[2],0x10,&result.state,sizeof(result.state)) ||
       !read_object(process,result.chain[2],0x48,&result.flag,sizeof(result.flag))) return result;
    result.status="observed_raw_fields";
    return result;
}
void print_network_fields(HANDLE process,std::uintptr_t base,DWORD sample) {
    const auto before=network_fields(process,base);
    const auto after=network_fields(process,base);
    // Repeated reads detect some transitions. Equal pointers cannot exclude
    // ABA reuse or concurrent writes and confer no object lifetime lease.
    const bool same=before.chain==after.chain && before.vtable==after.vtable &&
        before.state==after.state && before.flag==after.flag && std::strcmp(before.status,after.status)==0;
    const bool observed=same && std::strcmp(before.status,"observed_raw_fields")==0;
    std::cout << "{\"sample\":" << sample << ",\"tick_ms\":" << GetTickCount64()
        << ",\"kind\":\"network_service_candidate\",\"status\":\"" << (same ? before.status : "changed_during_sample")
        << "\",\"snapshot_atomic\":false,\"lifetime_verified\":false,\"state_raw\":";
    if(observed) std::cout << before.state; else std::cout << "null";
    std::cout << ",\"flag_48_raw\":";
    if(observed) std::cout << static_cast<unsigned>(before.flag); else std::cout << "null";
    std::cout << ",\"vtable_rva_raw\":";
    if(observed) std::cout << before.vtable-base; else std::cout << "null";
    std::cout << ",\"transport_presence\":\"unverified\",\"connection_status\":\"unverified\"}" << std::endl;
}
int fail(const char* reason) {
    std::cout << "{\"status\":\"refused\",\"reason\":\"" << reason << "\"}\n";
    return 1;
}
bool number(const wchar_t* text,DWORD low,DWORD high,DWORD& out) {
    if(!text || !*text) return false;
    std::uint64_t value=0;
    for(auto* p=text;*p;++p) {
        if(*p<L'0'||*p>L'9') return false;
        value=value*10+(*p-L'0');
        if(value>high) return false;
    }
    out=static_cast<DWORD>(value);
    return value>=low;
}
}
int wmain(int argc,wchar_t** argv) {
    // One finite invocation, JSON lines on stdout. No output file chosen from
    // game data. A caller may redirect stdout into its own evidence directory.
    DWORD pid=0,samples=1,interval_ms=100;
    const bool network=argc>1 && std::wcscmp(argv[argc-1],L"--network")==0;
    if(network) --argc;
    if(argc!=2 && argc!=4) return fail("usage_world_probe_PID_optional_samples_interval_ms_optional_network_flag");
    if(!number(argv[1],1,MAXDWORD,pid) || (argc==4 && (!number(argv[2],1,300,samples)||!number(argv[3],50,10000,interval_ms))))
        return fail("invalid_bounded_arguments");
    Handle process(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid));
    if(!process.value) return fail("process_not_readable");
    std::array<wchar_t,32768> exe{};
    DWORD path_size=static_cast<DWORD>(exe.size());
    if(!QueryFullProcessImageNameW(process.value,0,exe.data(),&path_size)) return fail("process_path_unavailable");
    const auto* name=wcsrchr(exe.data(),L'\\');
    if(!name || _wcsicmp(name+1,L"mgsvtpp.exe")!=0) return fail("unexpected_process_name");
    Handle file(CreateFileW(exe.data(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    std::array<unsigned char,32> actual_hash{};
    if(file.value==INVALID_HANDLE_VALUE || !digest(file.value,actual_hash) || actual_hash!=accepted_hash) return fail("unsupported_executable_fingerprint");
    std::array<HMODULE,2048> modules{};
    DWORD needed=0;
    if(!K32EnumProcessModules(process.value,modules.data(),static_cast<DWORD>(sizeof(modules)),&needed)||needed>sizeof(modules))
        return fail("module_inventory_unavailable");
    std::uintptr_t base=0;
    unsigned found=0;
    for(std::size_t i=0;i<needed/sizeof(HMODULE);++i) {
        std::array<wchar_t,32768> path{};
        const auto length=K32GetModuleFileNameExW(process.value,modules[i],path.data(),static_cast<DWORD>(path.size()));
        if(!length||length>=path.size()||_wcsicmp(path.data(),exe.data())!=0) continue;
        MODULEINFO info{};
        if(!K32GetModuleInformation(process.value,modules[i],&info,sizeof(info))||info.SizeOfImage!=image_size)
            return fail("mapped_image_size_mismatch");
        base=reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
        ++found;
    }
    if(found!=1 || !code_matches(process.value,base)) return fail("loaded_identity_or_code_mismatch");
    if(network && !network_code_matches(process.value,base)) return fail("network_access_code_mismatch");
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    std::cout << "{\"status\":\"verified_target\",\"pid\":" << pid
        << ",\"sha256\":\"085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45\","
        << "\"utc\":\"" << std::setfill('0') << std::setw(4) << utc.wYear << '-' << std::setw(2) << utc.wMonth << '-' << std::setw(2) << utc.wDay
        << 'T' << std::setw(2) << utc.wHour << ':' << std::setw(2) << utc.wMinute << ':' << std::setw(2) << utc.wSecond << "Z\","
        << "\"scope\":\"external_field_reads_only\",\"calls_or_writes\":false,\"snapshot_atomic\":false,"
        << "\"freshness\":\"unverified\",\"actor_presence\":\"unverified\"}" << std::endl;
    for(DWORD sample=0;sample<samples;++sample) {
        if(network) print_network_fields(process.value,base,sample);
        std::uintptr_t pointer=0,after=0;
        std::uint16_t mission=0;
        std::array<float,3> position{};
        std::array<float,3> player_reference_position{};
        std::uint32_t index=0,index_after=0;
        std::int32_t bank=0,bank_after=0;
        std::array<std::uint32_t,5> status_words{};
        if(!read_at(process.value,base,vars_pointer_rva,&pointer,sizeof(pointer))) return fail("vars_pointer_unreadable");
        if(pointer==0) {
            std::cout << "{\"sample\":" << sample << ",\"status\":\"vars_not_initialized\",\"player_reference_position_candidate\":null}" << std::endl;
        } else {
            // Keep this first reader strictly on the statically initialized
            // image allocation. A different pointer needs separate evidence.
            if(pointer!=base+vars_backing_rva) return fail("unexpected_vars_backing_pointer");
            if(!read_at(process.value,base,vars_backing_rva+0x12,&mission,sizeof(mission)) ||
               !read_at(process.value,base,vars_backing_rva+0xec,position.data(),sizeof(position)) ||
               !read_at(process.value,base,player_index_rva,&index,sizeof(index)) ||
               !read_at(process.value,base,player_bank_rva,&bank,sizeof(bank))) return fail("field_read_failed");
            // Never index the two fixed image-backed banks until both
            // selectors are bounded. A valid index is still not actor proof.
            const bool status_selectors_valid=(bank==0||bank==1)&&index<16;
            if(status_selectors_valid) {
                const auto record_rva=player_records_rva+
                    (static_cast<std::uintptr_t>(bank)*16+index)*player_record_bytes;
                if(!read_at(process.value,base,record_rva+player_status_offset,status_words.data(),sizeof(status_words)))
                    return fail("player_status_read_failed");
                // Fixed image-backed XYZ only. No actor pointer dereference or
                // readiness inference; the fourth vector component is unused.
                if(!read_at(process.value,base,record_rva+player_reference_position_offset,
                            player_reference_position.data(),sizeof(player_reference_position)))
                    return fail("player_reference_position_read_failed");
            }
            if(!read_at(process.value,base,player_bank_rva,&bank_after,sizeof(bank_after)) ||
               !read_at(process.value,base,player_index_rva,&index_after,sizeof(index_after)) ||
               !read_at(process.value,base,vars_pointer_rva,&after,sizeof(after))) return fail("field_read_failed");
            if(after!=pointer) return fail("vars_pointer_changed_during_sample");
            if(bank_after!=bank||index_after!=index) {
                // A normal publication flip invalidates this observation,
                // not the next scheduled attempt. Never emit mixed fields or
                // retry indefinitely: this attempt consumes one sample slot.
                std::cout << "{\"sample\":" << sample << ",\"tick_ms\":" << GetTickCount64()
                    << ",\"status\":\"sample_skipped\",\"reason\":\"player_selectors_changed_during_sample\"}" << std::endl;
                if(sample+1<samples) Sleep(interval_ms);
                continue;
            }
            // Equal selectors cannot exclude an ABA flip or concurrent writes.
            // These remain non-atomic observations, even when every gate passes.
            if(!std::isfinite(position[0])||!std::isfinite(position[1])||!std::isfinite(position[2])) return fail("nonfinite_position");
            if(status_selectors_valid && (!std::isfinite(player_reference_position[0]) ||
               !std::isfinite(player_reference_position[1]) || !std::isfinite(player_reference_position[2])))
                return fail("nonfinite_player_reference_position_candidate");
            std::cout << std::setprecision(9) << "{\"sample\":" << sample << ",\"tick_ms\":" << GetTickCount64()
                << ",\"status\":\"observed_unverified_gameplay\",\"mission_code_raw\":" << mission
                << ",\"mission_initialization_sentinel\":" << (mission==0xffff ? "true":"false")
                << ",\"position_fields\":[" << position[0] << ',' << position[1] << ',' << position[2]
                << "],\"local_player_index_raw\":" << index
                << ",\"player_bank_raw\":" << bank
                << ",\"player_status_selectors_valid\":" << (status_selectors_valid ? "true":"false")
                << ",\"player_status_words_raw\":";
            if(!status_selectors_valid) std::cout << "null,\"named_status_bits\":null";
            else {
                std::cout << '[';
                for(std::size_t word=0;word<status_words.size();++word) {
                    if(word) std::cout << ',';
                    std::cout << status_words[word];
                }
                std::cout << "],\"named_status_bits\":{";
                for(std::size_t named=0;named<named_statuses.size();++named) {
                    if(named) std::cout << ',';
                    const auto bit=named_statuses[named].bit;
                    const bool set=(status_words[bit>>5]&(std::uint32_t{1}<<(bit&31)))!=0;
                    std::cout << '\"' << named_statuses[named].name << "\":" << (set ? "true":"false");
                }
                std::cout << '}';
            }
            std::cout << ",\"player_reference_position_candidate\":";
            if(!status_selectors_valid) std::cout << "null";
            else std::cout << '[' << player_reference_position[0] << ',' << player_reference_position[1]
                           << ',' << player_reference_position[2] << ']';
            std::cout << '}' << std::endl;
        }
        if(sample+1<samples) Sleep(interval_ms);
    }
    return 0;
}
