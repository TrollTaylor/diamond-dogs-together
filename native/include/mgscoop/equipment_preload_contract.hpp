#pragma once
// The generalized preload (W2; docs/native-puppet.md "Generalized preload"):
// the exact read-only preflight and readback around ONE public
// TppEquip.RequestLoadToEquipMissionBlock({ids}) call, which Lua makes at the
// natural allocation owner before StartLoadingToEquipMissionBlock. No game
// method is called here; the reader is ReadProcessMemory in the adapter.
//
// A03C40 per id: the main FPK hash of the id's package row (2C20FD0 + id*24,
// +8; skipped when zero or already queued), A02290 the hashes of the DB6AC0
// dependency ids (2349348 + id*16, skipped when queued), then engine vt+0x280
// = DC3840 -> DC3700(id, protect 1): the first used entry of the id gains the
// protect bit (DC37AF..DC37C5); without one the first unused entry is built
// (DC377E..DC378B, DC3809 -> DC3470); with none unused a zero-ref unprotected
// entry is EVICTED and rebuilt (DC37D1..DC3800). The preflight refuses unless
// every id takes the first two paths, the queue has room for every new hash
// (no growth at A03D2D) and no requested id's existing entry is referenced.
#include "mgscoop/equipment_stock_writer_contract.hpp"

namespace mgscoop::equipment_preload_contract {
namespace w=equipment_stock_writer_contract;
inline constexpr unsigned kIds=3;
inline constexpr unsigned kHashes=kIds*w::kMaxPackages;
enum class Action : std::uint8_t { none, protect, build };
inline const char* ActionName(Action a) noexcept {
    return a==Action::protect ? "protect" : a==Action::build ? "build" : "none";
}
struct Item {
    std::uint16_t equip=0;
    std::uint8_t type=0,entry=0xFF;
    Action action=Action::none;
    std::uint32_t hash_count=0;
    std::array<std::uint64_t,w::kMaxPackages> hashes{};
};
struct Snapshot {
    std::uintptr_t manager=0,request=0,queue=0,queue_data=0,engine=0,info=0,cache=0,refs=0;
    std::uint32_t queue_count=0,queue_capacity=0;
    std::uint64_t prefix_hash=0;
    std::array<std::uintptr_t,7> groups{};
    std::array<std::uint32_t,7> group_sizes{};
    std::array<std::array<std::uint8_t,0x90>,w::kCacheEntries> info_bytes{};
    std::array<std::array<std::uint8_t,0x38>,w::kCacheEntries> cache_bytes{};
    std::array<std::uint32_t,w::kCacheEntries> refs_values{};
    unsigned item_count=0;
    std::array<Item,kIds> items{};
    // The distinct hashes the call appends, in A03C40/A02290 order, and how
    // many of them are not queued yet.
    std::array<std::uint64_t,kHashes> expected{};
    unsigned expected_count=0,new_hashes=0;
};

template<class Reader> bool Queued(Reader& reader,std::uintptr_t data,std::uint32_t count,std::uint64_t hash,bool& found) noexcept {
    found=false;
    for(std::uint32_t i=0;i<count;++i) {
        std::uint64_t value=0;
        if(!reader(data+std::size_t(i)*8,&value,sizeof(value))) return false;
        if(value==hash) {found=true;return true;}
    }
    return true;
}
// One id's hashes (main FPK then dependencies, deduplicated) without any cache
// requirement: the derivation of Derive minus the census.
template<class Reader> const char* Hashes(w::View<Reader>& v,std::uintptr_t engine,std::uint16_t equip,Item& item) noexcept {
    item=Item{};item.equip=equip;
    if(!equip || equip>=0x230) return "equip_range";
    std::uintptr_t types=0,resource=0,basic=0;std::uint16_t packed=0;
    if(!v.At(engine,8,types) || types!=v.base+0x2A70920 || !v.Method(types,0x22AD180,0,0xA2A3E0) ||
       !v.At(types,8+std::size_t(equip)*2,packed)) return "type_table";
    item.type=static_cast<std::uint8_t>(packed&0x3F);
    const std::size_t row=packed>>6;
    if(item.type<1 || item.type>8) return "type";
    std::array<std::uint8_t,12> gun_recipe{};
    if(!row || row>0x1818/12 || !v.At(engine,0x10,resource) || resource!=v.base+0x2A711F0 || !v.At(resource,8,basic) ||
       basic!=v.base+0x2C25C50 || !v.At(basic,(row-1)*12,gun_recipe)) return "recipe";
    std::array<std::uint64_t,3> package_row{};
    if(!v.Image(w::PackageRowRva(equip),24) || !v.At(v.base,w::PackageRowRva(equip),package_row) ||
       !package_row[0] || !package_row[1] || package_row[2]!=2) return "package_row";
    std::array<std::uint8_t,7> ids{};unsigned count=0;
    if(!w::DependencyIds(v,gun_recipe,ids,count)) return "dependency_tables";
    item.hashes[item.hash_count++]=package_row[1];
    for(unsigned i=0;i<count;++i) {
        std::uint64_t hash=0;
        if(!v.Image(0x2349348+std::size_t(ids[i])*16,8) || !v.At(v.base,0x2349348+std::size_t(ids[i])*16,hash) || !hash)
            return "dependency_hash";
        bool seen=false;
        for(unsigned j=0;j<item.hash_count;++j) seen=seen || item.hashes[j]==hash;
        if(!seen) {if(item.hash_count>=w::kMaxPackages) return "dependency_count";item.hashes[item.hash_count++]=hash;}
    }
    return nullptr;
}
template<class Reader> const char* Owners(std::uintptr_t base,std::size_t span,Reader& reader,Snapshot& out) noexcept {
    w::View<Reader> v{reader,base,span};
    if(!v.Image(0x2C25C50,0x1818) || !v.Image(0x2A82C30) || !v.Image(0x2C1E3B0) || !v.Image(0x2C3A8A0)) return "image_bounds";
    std::uintptr_t player=0;
    if(!v.At(base,0x2A82C30,player)) return "player_slot_read";
    if(player) return "player_not_null";
    if(!v.At(base,0x2C1E3B0,out.manager) || !out.manager || !v.At(out.manager,0xF0,out.request) || !out.request ||
       !v.At(out.request,0x18,out.queue) || !out.queue) return "mission_queue";
    unsigned group_index=0;
    for(auto offset:{0x40u,0x48u,0x50u,0x58u,0x60u,0x68u,0x70u}) {
        auto& group=out.groups[group_index];auto& size=out.group_sizes[group_index++];
        if(!v.At(out.manager,offset,group) || !group || !v.At(group,0x18,size) || !size) return "allocated_groups";
    }
    std::uintptr_t container=0,globals=0,services=0;
    if(!v.At(base,0x2C3A8A0,container) || !v.At(container,8,globals) || !v.At(globals,0x98,services) ||
       !v.At(services,0x1E8,out.engine) || !v.Method(out.engine,0x234B840,0x280,0xDC3840)) return "engine_contract";
    std::uintptr_t types=0,resource=0,dependency=0,dep_types=0,dep_resource=0;
    if(!v.At(out.engine,8,types) || types!=base+0x2A70920 || !v.Method(types,0x22AD180,0,0xA2A3E0) ||
       !v.Method(types,0x22AD180,0x10,0xA2A520) || !v.At(out.engine,0x10,resource) || resource!=base+0x2A711F0 ||
       !v.Method(resource,0x22B85C0,0x10,0xA3C670) || !v.At(out.engine,0x18,dependency) ||
       !v.Method(dependency,0x2349EE0,0x10,0xDB69E0) || !v.Method(dependency,0x2349EE0,0x18,0xDB6AC0) ||
       !v.Method(dependency,0x2349EE0,0x20,0xDB6CB0) || !v.Method(dependency,0x2349EE0,0x28,0xDB69D0) ||
       !v.Method(dependency,0x2349EE0,0x30,0xDB6A20) || !v.Method(dependency,0x2349EE0,0x38,0xDB6CC0) ||
       !v.Method(dependency,0x2349EE0,0x40,0xDB6CD0) ||
       !v.At(dependency,0x30,dep_types) || dep_types!=types || !v.At(dependency,0x38,dep_resource) || dep_resource!=resource)
        return "parameter_services";
    if(!v.At(out.engine,0x1D8,out.info) || !out.info || !v.At(out.engine,0x1E0,out.cache) || !out.cache ||
       !v.At(out.engine,0x1E8,out.refs) || !out.refs) return "cache_arrays";
    for(unsigned i=0;i<w::kCacheEntries;++i)
        if(!v.At(out.info,i*0x90,out.info_bytes[i]) || !v.At(out.cache,i*0x38,out.cache_bytes[i]) ||
           !v.At(out.refs,i*4,out.refs_values[i])) return "cache_read";
    if(!v.At(out.queue,0,out.queue_count) || !v.At(out.queue,4,out.queue_capacity) || !v.At(out.queue,8,out.queue_data) ||
       !out.queue_data || out.queue_count>out.queue_capacity || out.queue_capacity>4096) return "queue_bounds";
    unsigned ignored=0;
    if(!equipment_stock_contract::Prefix(reader,out.queue_data,out.queue_count,out.prefix_hash,ignored)) return "queue_hash_read";
    return nullptr;
}
inline std::uint16_t EntryId(const Snapshot& s,unsigned i) noexcept {return w::Word16(s.info_bytes[i].data()+0x5E);}
inline std::uint8_t EntryFlags(const Snapshot& s,unsigned i) noexcept {return s.cache_bytes[i][0x34];}

// The preflight: the ids' hashes, DC3700's predicted path for each and the
// queue room. `count` ids (1..3, distinct, already filtered by the caller).
template<class Reader> const char* Capture(std::uintptr_t base,std::size_t span,Reader& reader,
        const std::uint16_t* ids,unsigned count,Snapshot& out) noexcept {
    out=Snapshot{};
    if(!ids || !count || count>kIds) return "ids";
    if(const char* why=Owners(base,span,reader,out)) return why;
    w::View<Reader> v{reader,base,span};
    std::array<bool,w::kCacheEntries> used{};
    for(unsigned i=0;i<w::kCacheEntries;++i) used[i]=(EntryFlags(out,i)&1)!=0;
    for(unsigned n=0;n<count;++n) {
        for(unsigned m=0;m<n;++m) if(ids[m]==ids[n]) return "duplicate_id";
        Item& item=out.items[out.item_count++];
        if(const char* why=Hashes(v,out.engine,ids[n],item)) return why;
        unsigned first=w::kCacheEntries;
        for(unsigned i=0;i<w::kCacheEntries;++i) if(used[i] && EntryId(out,i)==item.equip) {first=i;break;}
        if(first<w::kCacheEntries) {
            // Never touch an entry the local player references.
            if(out.refs_values[first]) return "cache_shared";
            item.entry=static_cast<std::uint8_t>(first);
            item.action=(EntryFlags(out,first)&2) ? Action::none : Action::protect;
        } else {
            unsigned unused=w::kCacheEntries;
            for(unsigned i=0;i<w::kCacheEntries;++i) if(!used[i]) {unused=i;break;}
            // DC37D1: no unused entry left means an eviction. Refuse.
            if(unused==w::kCacheEntries) return "cache_evict";
            used[unused]=true;
            item.entry=static_cast<std::uint8_t>(unused);item.action=Action::build;
        }
        for(unsigned h=0;h<item.hash_count;++h) {
            bool seen=false;
            for(unsigned j=0;j<out.expected_count;++j) seen=seen || out.expected[j]==item.hashes[h];
            if(seen) continue;
            if(out.expected_count>=kHashes) return "hash_count";
            out.expected[out.expected_count++]=item.hashes[h];
            bool queued=false;
            if(!Queued(reader,out.queue_data,out.queue_count,item.hashes[h],queued)) return "queue_hash_read";
            if(!queued) ++out.new_hashes;
        }
    }
    // A03D2D: a full queue reallocates; require room for every new hash.
    if(out.queue_capacity-out.queue_count<out.new_hashes) return "queue_room";
    return nullptr;
}
// The readback after the public call returned: the same owners and queue
// storage, the old prefix unchanged, exactly the new hashes appended, every
// used entry byte-identical except the protect bit of the predicted ids and
// the predicted unused entries now built for their ids. Nothing was evicted.
template<class Reader> const char* Verify(std::uintptr_t base,std::size_t span,Reader& reader,const Snapshot& before,
        Snapshot& after) noexcept {
    after=Snapshot{};
    if(const char* why=Owners(base,span,reader,after)) return why;
    if(before.manager!=after.manager || before.request!=after.request || before.queue!=after.queue ||
       before.queue_data!=after.queue_data || before.engine!=after.engine || before.info!=after.info ||
       before.cache!=after.cache || before.refs!=after.refs || before.queue_capacity!=after.queue_capacity ||
       before.groups!=after.groups || before.group_sizes!=after.group_sizes) return "owner_or_queue_changed";
    if(after.queue_count!=before.queue_count+before.new_hashes) return "queue_growth";
    std::uint64_t prefix=0;unsigned ignored=0;
    if(!equipment_stock_contract::Prefix(reader,after.queue_data,before.queue_count,prefix,ignored)) return "old_prefix_read";
    if(prefix!=before.prefix_hash) return "old_queue_prefix_changed";
    for(unsigned j=0;j<before.expected_count;++j) {
        bool queued=false;
        if(!Queued(reader,after.queue_data,after.queue_count,before.expected[j],queued)) return "queue_hash_read";
        if(!queued) return "expected_hash_not_queued";
    }
    for(unsigned i=0;i<w::kCacheEntries;++i) {
        const Item* item=nullptr;
        for(unsigned n=0;n<before.item_count;++n) if(before.items[n].entry==i) item=&before.items[n];
        if(item && item->action==Action::build) {
            if((EntryFlags(after,i)&3)!=3 || EntryId(after,i)!=item->equip) return "built_entry_identity";
            continue;
        }
        if(after.refs_values[i]!=before.refs_values[i] || after.info_bytes[i]!=before.info_bytes[i]) return "cache_entry_changed";
        auto expected=before.cache_bytes[i];
        if(item && item->action==Action::protect) expected[0x34]=static_cast<std::uint8_t>(expected[0x34]|2);
        if(after.cache_bytes[i]!=expected) return "cache_entry_changed";
    }
    return nullptr;
}
}
