#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>

namespace mgscoop::equipment_stock_contract {
// Exact retail data contract for PUBLIC stock46 pre-loading. Reader is supplied
// by the adapter (ReadProcessMemory only), and has signature bool(address, out,
// size). No game method is called, and all writes below target local snapshots.
inline constexpr std::uint16_t equip=46, descriptor=0x6244;
inline constexpr std::uint64_t package=0x522886805524A8D8ull;
inline constexpr std::uint64_t parts=0xE0AA7C2A9F382502ull;
inline constexpr std::array<std::uint64_t,4> hashes{
    package,0x5228A3047E1132FCull,0x522A31AC933DC74Bull,0x522B39FC57ECA880ull};
inline constexpr std::array<std::uint8_t,12> recipe{157,85,110,36,0,0,17,0,0,8,0,4};
struct Snapshot {
    std::uintptr_t manager=0,request=0,queue=0,queue_data=0,engine=0;
    std::uintptr_t info=0,cache=0,refs=0;
    std::uint32_t queue_count=0,queue_capacity=0,cache_refs=0;
    std::uint64_t prefix_hash=0;
    unsigned seen_hashes=0;
    std::array<std::uintptr_t,7> groups{};
    std::array<std::uint32_t,7> group_sizes{};
    std::array<std::uint8_t,0x90> info_bytes{};
    std::array<std::uint8_t,0x38> cache_bytes{};
};

template<class Reader>
bool Prefix(Reader& reader, std::uintptr_t data, std::uint32_t count,
            std::uint64_t& value, unsigned& found) noexcept {
    if(!data || count>4096 || data>std::numeric_limits<std::uintptr_t>::max()-std::size_t(count)*8)
        return false;
    value=14695981039346656037ull; found=0;
    for(std::uint32_t i=0;i<count;++i) {
        std::uint64_t hash=0;
        if(!reader(data+std::size_t(i)*8,&hash,sizeof(hash))) return false;
        for(unsigned b=0;b<8;++b) { value^=(hash>>(8*b))&0xffu; value*=1099511628211ull; }
        for(unsigned h=0;h<hashes.size();++h) if(hash==hashes[h]) found|=1u<<h;
    }
    return true;
}

template<class Reader>
const char* Capture(std::uintptr_t base, std::size_t span, Reader& reader,
                    Snapshot& out, bool require_room=true) noexcept {
    out={};
    const auto image=[&](std::size_t rva,std::size_t bytes=8) noexcept {
        return base && base<=std::numeric_limits<std::uintptr_t>::max()-span &&
            rva<=span && bytes<=span-rva;
    };
    const auto at=[&](std::uintptr_t object,std::size_t offset,auto& value) noexcept {
        constexpr auto max=std::numeric_limits<std::uintptr_t>::max();
        return object && object<=max-offset && object+offset<=max-sizeof(value) &&
            reader(object+offset,&value,sizeof(value));
    };
    const auto method=[&](std::uintptr_t object,std::size_t table,std::size_t slot,std::size_t fn) noexcept {
        std::uintptr_t vtable=0,target=0;
        return image(table+slot) && image(fn,1) && at(object,0,vtable) && vtable==base+table &&
            at(vtable,slot,target) && target==base+fn;
    };
    if(!image(0x2C25C50,0x1818) || !image(0x2A82C30) || !image(0x2C1E3B0) ||
       !image(0x2C3A8A0) || !image(0x2C21420,24)) return "image_bounds";
    std::uintptr_t player=0;
    if(!at(base,0x2A82C30,player)) return "player_slot_read";
    if(player) return "player_not_null";
    if(!at(base,0x2C1E3B0,out.manager) || !out.manager ||
       !at(out.manager,0xF0,out.request) || !out.request ||
       !at(out.request,0x18,out.queue) || !out.queue) return "mission_queue";
    unsigned group_index=0;
    for(auto offset : {0x40u,0x48u,0x50u,0x58u,0x60u,0x68u,0x70u}) {
        auto& group=out.groups[group_index]; auto& size=out.group_sizes[group_index++];
        if(!at(out.manager,offset,group) || !group || !at(group,0x18,size) || !size)
            return "allocated_groups";
    }
    std::uintptr_t container=0,globals=0,services=0;
    if(!at(base,0x2C3A8A0,container) || !at(container,8,globals) ||
       !at(globals,0x98,services) || !at(services,0x1E8,out.engine) ||
       !method(out.engine,0x234B840,0x280,0xDC3840)) return "engine_contract";
    std::uintptr_t types=0,resource=0,dependency=0,basic=0;
    if(!at(out.engine,8,types) || types!=base+0x2A70920 ||
       !method(types,0x22AD180,0,0xA2A3E0) || !method(types,0x22AD180,0x10,0xA2A520) ||
       !at(out.engine,0x10,resource) || resource!=base+0x2A711F0 ||
       !method(resource,0x22B85C0,0x10,0xA3C670) ||
       !at(out.engine,0x18,dependency) || !method(dependency,0x2349EE0,0x10,0xDB69E0) ||
       !method(dependency,0x2349EE0,0x18,0xDB6AC0) ||
       !method(dependency,0x2349EE0,0x20,0xDB6CB0) ||
       !method(dependency,0x2349EE0,0x28,0xDB69D0) ||
       !method(dependency,0x2349EE0,0x30,0xDB6A20) ||
       !method(dependency,0x2349EE0,0x38,0xDB6CC0) ||
       !method(dependency,0x2349EE0,0x40,0xDB6CD0)) return "parameter_services";
    std::uint16_t packed=0;
    std::array<std::uint8_t,12> actual_recipe{};
    std::array<std::uint64_t,3> row{};
    if(!at(types,8+equip*2,packed) || packed!=descriptor ||
       !at(resource,8,basic) || basic!=base+0x2C25C50 ||
       !at(basic,(393-1)*12,actual_recipe) || actual_recipe!=recipe ||
       !at(base,0x2C21420,row) || row[0]!=parts || row[1]!=package || row[2]!=2)
        return "stock46_bank";
    std::uintptr_t dep_types=0,dep_resource=0;
    if(!at(dependency,0x30,dep_types) || dep_types!=types ||
       !at(dependency,0x38,dep_resource) || dep_resource!=resource) return "dependency_owner";
    std::uint64_t dep_hash36=0,dep_hash55=0,dep_hash64=0;
    if(!at(base,0x2349348+36*16,dep_hash36) || dep_hash36!=hashes[1] ||
       !at(base,0x2349348+55*16,dep_hash55) || dep_hash55!=hashes[2] ||
       !at(base,0x2349348+64*16,dep_hash64) || dep_hash64!=hashes[3]) return "dependency_hashes";
    if(!at(out.engine,0x1D8,out.info) || !out.info || !at(out.engine,0x1E0,out.cache) || !out.cache ||
       !at(out.engine,0x1E8,out.refs) || !out.refs) return "cache_arrays";
    for(unsigned i=0;i<4;++i) {
        std::uint8_t flags=0; std::uint16_t id=0;
        if(!at(out.cache,i*0x38+0x34,flags) || !at(out.info,i*0x90+0x5E,id)) return "cache_read";
        if((flags&1) && id==equip) return "earlier_stock_match";
    }
    if(!at(out.info,4*0x90,out.info_bytes) || !at(out.cache,4*0x38,out.cache_bytes) ||
       !at(out.refs,4*4,out.cache_refs)) return "cache4_read";
    std::uint16_t cache_id=0;
    std::memcpy(&cache_id,out.info_bytes.data()+0x5E,sizeof(cache_id));
    if(cache_id!=equip || out.cache_bytes[0x34]!=3 || out.cache_refs!=0 ||
       (out.info_bytes[0x78]&0x1F)!=4 || out.info_bytes[0x7A]!=157) return "cache4_not_protected_stock46";
    if(!at(out.queue,0,out.queue_count) || !at(out.queue,4,out.queue_capacity) ||
       !at(out.queue,8,out.queue_data) || !out.queue_data || out.queue_count>out.queue_capacity ||
       out.queue_capacity>4096 || (require_room && out.queue_capacity-out.queue_count<hashes.size())) return "queue_bounds_or_room";
    if(!Prefix(reader,out.queue_data,out.queue_count,out.prefix_hash,out.seen_hashes)) return "queue_hash_read";
    return nullptr;
}

template<class Reader>
const char* Verify(std::uintptr_t base,std::size_t span,Reader& reader,const Snapshot& before,
                   Snapshot& after) noexcept {
    if(const char* reason=Capture(base,span,reader,after,false)) return reason;
    if(before.manager!=after.manager || before.request!=after.request || before.queue!=after.queue ||
       before.queue_data!=after.queue_data || before.engine!=after.engine || before.info!=after.info ||
       before.cache!=after.cache || before.refs!=after.refs || before.queue_capacity!=after.queue_capacity ||
       before.groups!=after.groups || before.group_sizes!=after.group_sizes)
        return "owner_or_queue_changed";
    if(before.info_bytes!=after.info_bytes || before.cache_bytes!=after.cache_bytes ||
       before.cache_refs!=after.cache_refs) return "cache4_changed";
    if(after.queue_count<before.queue_count || after.queue_count-before.queue_count>hashes.size() || after.seen_hashes!=15)
        return "expected_hashes_not_queued";
    std::uint64_t prefix=0; unsigned found=0;
    if(!Prefix(reader,after.queue_data,before.queue_count,prefix,found)) return "old_prefix_read";
    if(prefix!=before.prefix_hash) return "old_queue_prefix_changed";
    return nullptr;
}
}
