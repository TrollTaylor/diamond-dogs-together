#pragma once
#include "mgscoop/equipment_stock_contract.hpp"
#include <algorithm>

namespace mgscoop::equipment_stock_writer_contract {
using Bytes14=std::array<std::uint8_t,0xE>;
inline constexpr std::uint64_t optional46=0x6700BE21BBCD5DB5ull;
// The equip one dispatch targets (docs/native-puppet.md "Generalized
// writer"). kStock46 is the qualified stock46 contract, constant for constant;
// Derive (below) builds the same shape for any stock gun id from the exact
// executable tables (tools/generate_equip_writer_pins.py --check) and the
// runtime parameter banks. Every field is re-read and compared before use.
inline constexpr unsigned kMaxPackages=8;  // the main FPK + DB6AC0's at most 7 dependency ids
// ChangeEquip 11AC4DE..11AC50B and the lower writer 120A5D2..120A611: the
// slot an equip type goes to (0 hip primary, 1 back primary, 2 secondary;
// 0xFF: not a gun type this writer serves).
constexpr std::uint8_t TypeSlot(std::uint8_t type) noexcept {
    return (type==1 || type==5 || type==7) ? 0u : (type==4 || type==6 || type==8) ? 1u : (type==2 || type==3) ? 2u : 0xFFu;
}
// A03C40: the runtime package row (parts, main FPK, flags) of a stock id below 0x400.
constexpr std::size_t PackageRowRva(std::uint16_t equip) noexcept {return 0x2C20FD0+std::size_t(equip)*24;}
struct Target {
    std::uint16_t equip=46,packed=equipment_stock_contract::descriptor,row=393;
    std::uint8_t type=4,slot=1;
    std::array<std::uint8_t,12> recipe=equipment_stock_contract::recipe;
    std::uint64_t parts=equipment_stock_contract::parts,package=equipment_stock_contract::package,package_flags=2;
    // The main FPK first, then the dependency packages in A02290's append order.
    std::array<std::uint64_t,kMaxPackages> hashes{equipment_stock_contract::hashes[0],equipment_stock_contract::hashes[1],
        equipment_stock_contract::hashes[2],equipment_stock_contract::hashes[3]};
    std::uint32_t hash_count=4;
    // 1307EF2..1307F19: ids up to 0x7C read their optional descriptor at
    // [2A6B400+8+id*8]; a zero descriptor or a larger id prepares the NULL
    // optional (ADE358: the main bank's render request with no asset).
    bool optional=true;
    std::uint64_t optional_hash=optional46;
    // stock46: the H3 parsed counts [2,1,1,1]; derived targets: 1..named count.
    bool exact_parsed=true;
    // stock46's first and only dispatch: every remote slot empty. Derived
    // targets need their own slot empty (the writer checks the others).
    bool require_remote_empty=true;
    std::uint32_t cache_index=4;
};
inline constexpr Target kStock46{};
constexpr std::uint32_t ExpectedHandle(std::uint32_t new_index,const Target& t) noexcept {
    return (new_index<<16)|(std::uint32_t(t.equip)<<5)|t.type;
}
struct OptionalLease {
    std::uintptr_t asset=0,source=0,header=0;
    std::uint64_t bytes=0;
    std::uint32_t parameters=0,global=0,render_bytes=0;
    std::array<std::uint8_t,0x20> header_bytes{};
    bool operator==(const OptionalLease& o) const noexcept {
        return asset==o.asset && source==o.source && header==o.header && bytes==o.bytes &&
            parameters==o.parameters && global==o.global && render_bytes==o.render_bytes && header_bytes==o.header_bytes;
    }
};
struct BankCacheEntry {
    std::array<std::uint8_t,0x18> reference{};
    std::uint64_t key=0;std::uint16_t refs=0;std::uintptr_t component=0;
    bool operator==(const BankCacheEntry& other) const noexcept {
        return reference==other.reference && key==other.key && refs==other.refs && component==other.component;
    }
};
struct PackageObservation {
    std::uintptr_t service=0,backing=0,allocation=0,parameters=0,table=0;
    std::uint64_t capacity=0,size=0,descriptor=0;
    std::uint32_t variant=0,reads=0,checks=0;
    std::array<std::uintptr_t,3> methods{};
};
struct BankLease {
    std::uintptr_t group=0,bank=0,allocation=0,targets=0;
    std::uint64_t bytes=0,used=0,unused=0;
    std::uint32_t count=0,target_count=0;
    std::array<std::uintptr_t,4> arrays{};
    std::array<std::uintptr_t,3> aux{};
    std::uintptr_t arena=0,policy=0,callback=0,callback_context=0,extra=0;
    std::uint32_t stride=0;
    std::array<std::uintptr_t,2> policies{};
    std::array<std::array<std::uint8_t,0x50>,2> extra_records{};
    std::array<BankCacheEntry,64> entries{};
    bool operator==(const BankLease& o) const noexcept {
        return group==o.group && bank==o.bank && allocation==o.allocation && targets==o.targets &&
            bytes==o.bytes && used==o.used && unused==o.unused && count==o.count &&
            target_count==o.target_count && arrays==o.arrays && aux==o.aux && entries==o.entries &&
            arena==o.arena && policy==o.policy && callback==o.callback && callback_context==o.callback_context &&
            extra==o.extra && stride==o.stride && policies==o.policies && extra_records==o.extra_records;
    }
};
struct Snapshot {
    std::uintptr_t player=0,iface=0,inventory=0,handles=0,metadata=0,vars=0,engine=0;
    std::array<std::uintptr_t,2> bodies{},models{},binder_records{};
    std::array<std::uint32_t,7> local_handles{};
    std::array<std::uint8_t,42> local_metadata{};
    std::array<std::uint8_t,36> local_binder{},local_recipes{};
    std::array<std::uint16_t,3> local_base_ids{};
    std::array<std::uint32_t,7> remote_handles{};
    std::array<std::uint8_t,42> remote_metadata{};
    std::array<std::uint8_t,36> remote_binder{};
    // The target slot's effect record (body628 + slot*0x50, 9C5A39) and its
    // 13 attachment items (B0CF80: 13 per slot record, 40 per body).
    std::array<std::uint8_t,0x50> remote_slot_effect{};
    std::uintptr_t info=0,cache=0,refs=0,ring=0,meta_service=0,meta_rows=0,map=0,map_rows=0,resource_owners=0;
    std::uint32_t ring_capacity=0,ring_cursor=0,new_index=0,cache_refs=0,resource_id=0;
    std::array<std::uint8_t,0x90> cache_info{};
    std::array<std::uint8_t,0x38> cache_record{};
    Bytes14 new_metadata{};
    std::array<Bytes14,3> local_engine_metadata{};
    std::array<std::uint32_t,3> local_resource_ids{};
    std::uintptr_t mission_manager=0,mission_group=0,mission_entry=0;
    std::uintptr_t global_root=0,globals=0,services=0,owner_controller=0;
    std::array<std::uintptr_t,2> attachment_managers{},attachment_arrays{};
    std::array<std::array<std::uintptr_t,40>,2> attachment_items{};
    std::array<std::array<std::uint8_t,0x20>,13> remote_slot_attachments{};
    std::uintptr_t selected_group=0,package_service=0,package_parameters=0;
    std::array<std::uintptr_t,2> selected_banks{},selected_bank_arrays{};
    std::array<std::uint32_t,2> selected_cache_counts{};
    std::array<std::array<std::uintptr_t,4>,2> selected_cache_arrays{};
    std::array<std::array<std::uintptr_t,3>,2> selected_bank_aux{};
    std::array<std::uintptr_t,2> selected_bank_allocations{};
    std::array<std::uint64_t,2> selected_bank_allocation_bytes{},selected_bank_used_bytes{};
    std::array<BankCacheEntry,64> main_cache_entries{};
    std::uint32_t main_cache_candidate=UINT32_MAX;
    std::uint32_t next_resource=0,selected_group_index=0,selected_resource_index=0;
    std::uint32_t file_count=0;
    std::uintptr_t stock_parts_asset=0,stock_parts_entity=0;
    std::uint64_t stock_parts_word30=0;
    OptionalLease optional{};
    // DB7590 chooses the resource later. These leases cover all 32 groups;
    // accepted readback must use an initially free target and an unused cache
    // entry in its actual bank. They are observations, never reservations.
    std::array<BankLease,64> banks{};
    std::array<std::uint32_t,60> initial_maps{},initial_owners{},free_queue{};
    std::array<std::uint8_t,60> initial_load_flags{};
    std::array<Bytes14,60> initial_engine_metadata{};
    std::uint64_t eligible_resources=0;
    std::uint32_t free_cursor=0;
    PackageObservation package_observation{};
};

template<class Reader> struct View;
template<class Reader> const char* AssetTree(View<Reader>& v,std::uintptr_t record,
        std::uint32_t expected_count,bool stock,Snapshot& s,const Target& t=kStock46) noexcept {
    // DDA00's tree getter is unbounded. Require the same owned header and
    // independently bounded named-asset count, with no asset-pointer read.
    // DDF60 DE381..DE4B3 filters these map values by activation class before
    // publishing parsed_count118: that filtered count is a separate contract.
    std::uintptr_t header=0,root=0;std::uint32_t count=0;std::uint8_t marker=0;
    if(!expected_count || !v.At(record,0x28,header) || !header || !v.At(record,0x30,count) ||
       !count || count>256 || !v.At(header,0x19,marker) || marker!=1 ||
       !v.At(header,8,root) || !root || root==header) return "asset_tree_bounds";
    // A derived target has no measured parsed count: at most the named count.
    if(!t.exact_parsed && expected_count>count) return "package_parsed_count";
    struct Node {std::uintptr_t pointer,parent;std::uint64_t low,high;bool has_low,has_high;};
    std::array<Node,256> stack{};unsigned pending=1,seen_count=0,parts_count=0,optional_count=0;
    std::array<std::uintptr_t,256> seen{};
    std::array<std::uint64_t,256> hashes{};
    stack[0]={root,header,0,0,false,false};
    while(pending) {
        const auto node=stack[--pending];
        if(!node.pointer || node.pointer==header || seen_count>=count) return "asset_tree_cycle";
        for(unsigned j=0;j<seen_count;++j) if(seen[j]==node.pointer) return "asset_tree_cycle";
        std::uintptr_t parent=0,left=0,right=0,asset=0;std::uint64_t hash=0;
        if(!v.At(node.pointer,0x19,marker) || marker || !v.At(node.pointer,8,parent) || parent!=node.parent ||
           !v.At(node.pointer,0,left) || !left || !v.At(node.pointer,0x10,right) || !right ||
           !v.At(node.pointer,0x20,hash) || !v.At(node.pointer,0x28,asset) || !asset ||
           (node.has_low && hash<=node.low) || (node.has_high && hash>=node.high)) return "asset_tree_owner";
        seen[seen_count]=node.pointer;hashes[seen_count++]=hash;
        if(stock && hash==t.parts) {s.stock_parts_asset=asset;++parts_count;}
        if(stock && t.optional && hash==t.optional_hash) {s.optional.asset=asset;++optional_count;}
        if(right!=header) {if(pending>=stack.size()) return "asset_tree_cycle";
            stack[pending++]={right,node.pointer,hash,node.high,true,node.has_high};}
        if(left!=header) {if(pending>=stack.size()) return "asset_tree_cycle";
            stack[pending++]={left,node.pointer,node.low,hash,node.has_low,true};}
    }
    if(seen_count!=count) return "asset_tree_count";
    unsigned lo=0,hi=0;std::uintptr_t edge=0;
    for(unsigned j=1;j<count;++j) {if(hashes[j]<hashes[lo]) lo=j;if(hashes[j]>hashes[hi]) hi=j;}
    if(!v.At(header,0,edge) || edge!=seen[lo] || !v.At(header,0x10,edge) || edge!=seen[hi]) return "asset_tree_extrema";
    if(stock && parts_count!=1) return "stock_parts_asset_missing";
    return stock && t.optional && optional_count!=1 ? "stock_optional_asset_missing" : nullptr;
}
inline std::uint32_t Word32(const std::uint8_t* p) noexcept {std::uint32_t v=0;std::memcpy(&v,p,4);return v;}
inline std::uint16_t Word16(const std::uint8_t* p) noexcept {std::uint16_t v=0;std::memcpy(&v,p,2);return v;}
template<class T> bool Zero(const T& bytes) noexcept {
    return std::all_of(bytes.begin(),bytes.end(),[](auto b){return b==0;});
}
template<class Reader> struct View {
    Reader& reader;std::uintptr_t base;std::size_t span;
    bool Bytes(std::uintptr_t object,std::size_t offset,void* value,std::size_t size) noexcept {
        constexpr auto max=std::numeric_limits<std::uintptr_t>::max();
        return object && object<=max-offset && object+offset<=max-size && reader(object+offset,value,size);
    }
    template<class T> bool At(std::uintptr_t object,std::size_t offset,T& value) noexcept {
        return Bytes(object,offset,&value,sizeof(value));
    }
    bool Image(std::size_t rva,std::size_t size=8) const noexcept {
        return base && base<=UINTPTR_MAX-span && rva<=span && size<=span-rva;
    }
    bool Method(std::uintptr_t object,std::size_t vt,std::size_t slot,std::size_t fn) noexcept {
        std::uintptr_t table=0,target=0;
        return Image(vt+slot) && Image(fn,1) && At(object,0,table) && table==base+vt &&
            At(table,slot,target) && target==base+fn;
    }
};

// The optional asset's constructor shapes: 1A95470(n, mode 0) sums, over n
// parameters, the static per-parameter records 1A953D0(0) returns (0x2B4A078:
// parameter 0, 1, then 2 for every later one; {count, kinds[3]}; kind 0 ->
// 0x40 bytes, 1..6 -> 0x60): m(n) = the summed counts (header word1) and the
// request size align16(8*m+31) + the kind bytes. Emulated from the exe and
// asserted by tools/generate_equip_writer_pins.py --check (which also pins
// 1A953D0, 1A95470, the jump table and the mode-0 table).
struct OptionalShape {std::uint16_t n,m;std::uint32_t size;};
inline constexpr std::array<OptionalShape,8> kOptionalShapes{{{1,2,224},{2,4,400},{3,5,480},{4,6,544},{5,7,624},{6,8,688},{7,9,768},{8,10,832}}};
constexpr const OptionalShape* OptionalShapeOf(std::uint16_t n,std::uint16_t m) noexcept {
    return n>=1 && n<=kOptionalShapes.size() && kOptionalShapes[n-1].n==n && kOptionalShapes[n-1].m==m ? &kOptionalShapes[n-1] : nullptr;
}
template<class Reader> const char* OptionalReady(View<Reader>& v,OptionalLease& o,const Target& t=kStock46) noexcept {
    std::uint8_t state=0;std::uint32_t result=0,flags=0;std::uint64_t hash=0;
    // The canonical loaded resource factory declares a 0xA0 object; loader
    // 1A75340 binds header98 to its owned source60 and converts count*0x10
    // relative parameter records into count*0x30 descriptors. No descriptors,
    // source-relative children, dictionary, or native method are followed here.
    if(!v.Method(o.asset,0x24C2890,0x48,0x1A75340) || !v.At(o.asset,0x30,hash) || hash!=t.optional_hash ||
       !v.At(o.asset,0x40,state) || state!=2 || !v.At(o.asset,0x44,result) || result ||
       !v.At(o.asset,0x80,flags) || (flags&0x1000000)) return "stock_optional_state";
    if(!v.At(o.asset,0x60,o.source) || !v.At(o.asset,0x68,o.bytes) ||
       !v.At(o.asset,0x98,o.header) || !o.source || o.source!=o.header ||
       o.bytes<0x20 || o.bytes>1024*1024 || o.source>UINTPTR_MAX-o.bytes ||
       !v.At(o.source,0,o.header_bytes)) return "stock_optional_source";
    if(Word32(o.header_bytes.data())!=0x0C012B72 || !v.At(o.asset,0x84,o.parameters) ||
       o.parameters!=3 || Word32(o.header_bytes.data()+4)!=o.parameters ||
       0x20ull+std::uint64_t(o.parameters)*0x10>o.bytes) return "stock_optional_header";
    // Admit only the constructor shapes of the exe's own per-parameter table:
    // word0 = n parameters (1..8), word1 = m(n) (the summed counts of
    // 1A95470 mode 0, kOptionalShapes), words 2..5 zero. Its flags1000-clear
    // fallback mode0 skips all dictionary/secondary/typed-size
    // branches. Natural1307EE2/EE6 zero the optional descriptor's count+8
    // and flags+C;1307F2D writes only its asset+0, and1307F35..F4C passes
    // that descriptor via R8+10. ADDE317/396/3A3 therefore produce request
    // flags208/count1/extra18NULL and skip AB0460. This
    // proves the ordinary arena read/write extents, not the protected binder's
    // absence of failures; that exact retail body remains pinned and SEH-bound.
    // stock46 is n4/m6 (544 bytes, as measured).
    const auto* shape=OptionalShapeOf(Word16(o.header_bytes.data()+8),Word16(o.header_bytes.data()+10));
    if(!shape) return "stock_optional_shape";
    for(unsigned i=2;i<6;++i) if(Word16(o.header_bytes.data()+8+i*2)) return "stock_optional_shape";
    if(!v.At(v.base,0x2C71680,o.global) || o.global>153) return "stock_optional_global";
    // 1A95470 mode0(n) = size(n) (kOptionalShapes; stock46 n4: 544).
    // 1AA49E0 contributes align16(8*g+88). All arithmetic is bounded here.
    o.render_bytes=512+shape->size+((8*o.global+88+15)&~15u);
    return o.render_bytes<=2048 ? nullptr : "stock_optional_render_size";
}
// The NULL optional (an id above 0x7C or a zero descriptor): 1307F31 passes
// the zeroed caller descriptor, ADE358 sees asset NULL and (main bank, policy
// nonzero) still renders: 1AB7520 req asset NULL, flags 208, count 1;
// 1AB7940 = 0x20 + 0x150 (flags 8) + 1E19200(NULL) where 1E1921F skips every
// asset branch: 0x90 + align16(1A7DA20(1,1) = 1AA49E0(1,1)+0x30 = 8g+88).
// 1E18CF0 skips its asset branch at 1E18D82 the same way.
template<class Reader> const char* OptionalAbsent(View<Reader>& v,OptionalLease& o) noexcept {
    o={};
    if(!v.At(v.base,0x2C71680,o.global) || o.global>153) return "stock_optional_global";
    o.render_bytes=512+((8*o.global+88+15)&~15u);
    return o.render_bytes<=2048 ? nullptr : "stock_optional_render_size";
}

template<class Reader> const char* MissionReady(View<Reader>& v,Snapshot& s,const Target& t=kStock46) noexcept {
    if(!v.At(v.base,0x2C1E3B0,s.mission_manager) || !v.At(s.mission_manager,0x40,s.mission_group)) return "mission_group";
    std::uint32_t count=0,state=0,phase=0;std::uintptr_t owner=0,pointers=0,records=0;
    if(!v.At(s.mission_group,0x1C,count) || count!=1 || !v.At(s.mission_group,0x48,s.mission_entry) ||
       !v.At(s.mission_entry,0x70,owner) || owner!=s.mission_group ||
       !v.At(s.mission_entry,0x80,state) || state!=3 || !v.At(s.mission_entry,0x84,phase) || phase!=10 ||
       !v.At(s.mission_entry,0x150,s.file_count) || s.file_count<4 || s.file_count>4096 ||
       !v.At(s.mission_entry,0x90,pointers) || !v.At(s.mission_entry,0x98,records) || !pointers || !records)
        return "mission_not_active";
    unsigned mask=0;
    for(std::uint32_t i=0;i<s.file_count;++i) {
        std::uintptr_t record=0;std::uint64_t hash=0;
        if(!v.At(pointers,std::size_t(i)*8,record) || record!=records+std::size_t(i)*0x120 ||
           !v.At(record,0x10,owner) || owner!=s.mission_entry || !v.At(record,8,hash) ||
           !v.Method(record,0x20C1CE0,0x38,0xDDA00)) return "noncanonical_file";
        unsigned index=0;
        for(;index<t.hash_count && index<kMaxPackages;++index) if(hash==t.hashes[index]) break;
        if(index>=t.hash_count || index>=kMaxPackages) continue;
        std::uint32_t result=0,load_phase=0,parsed_count=0;std::uint8_t ready=0;std::uintptr_t parsed=0;
        if(mask&(1u<<index) || !v.Method(record,0x20C1CE0,0x90,0xDDF60) ||
           !v.Method(record,0x20C1CE0,0x80,0xDD830) || !v.Method(record,0x20C1CE0,0x88,0xDD810) ||
           !v.At(record,0x18,result) || result || !v.At(record,0x1C,ready) || ready!=2 ||
           !v.At(record,0x20,load_phase) || load_phase!=7 || !v.At(record,0x110,parsed) || !parsed ||
           !v.At(record,0x118,parsed_count) || (t.exact_parsed ? parsed_count!=(index ? 1u : 2u) : parsed_count==0))
            return "package_not_ready";
        if(const char* why=AssetTree(v,record,parsed_count,index==0,s,t)) return why;
        mask|=1u<<index;
    }
    if(!t.hash_count || t.hash_count>kMaxPackages || mask!=(1u<<t.hash_count)-1u) return "missing_package";
    // ADDED9..ADDEEB derives its branch class from this exact native field.
    // H3 canonical stock46 asset has its full parts hash here (class7189),
    // taking the ordinary branch instead of specialized class0x1094.
    if(!v.At(s.stock_parts_asset,0x30,s.stock_parts_word30) ||
       s.stock_parts_word30!=t.parts) return "stock_parts_identity";
    static_assert(((equipment_stock_contract::parts>>0x33)&0x1FFF)==7189);
    if(((t.parts>>0x33)&0x1FFF)==0x1094) return "stock_parts_class";
    // ADC9F7 obtains the native component owner here. Do not call the
    // ordinary path with an absent owner, even if the hash/tree was ready.
    if(!v.At(s.stock_parts_asset,0x70,s.stock_parts_entity) || !s.stock_parts_entity) return "stock_parts_entity";
    return t.optional ? OptionalReady(v,s.optional,t) : OptionalAbsent(v,s.optional);
}

// Common readback deliberately does not call methods or dereference a parsed
// package array. The adapter provides fresh pp::ReadState/ResolveDm separately.
template<class Reader> const char* Capture(std::uintptr_t base,std::size_t span,Reader& reader,
        std::uintptr_t player,std::uintptr_t remote_block,Snapshot& s,const Target& t=kStock46) noexcept {
    s={};View<Reader> v{reader,base,span};s.player=player;
    if(!v.Image(0x2C25C50,0x1818) || !v.Image(PackageRowRva(t.equip),24) || !v.Image(0x2BD3FD4,4)) return "image_bounds";
    if(t.slot>2 || TypeSlot(t.type)!=t.slot || t.cache_index>=48) return "target_shape";
    std::uintptr_t current=0,ids=0,pools=0,parts=0,blocks=0,buffer=0;std::uint32_t count=0,local=0,flags=0,local_id=0,owner_id=0;
    std::array<std::uint16_t,2> id{};std::array<std::uint8_t,2> pool{},part{};
    if(!v.At(base,0x2A82C30,current) || current!=player || !v.Method(player,0x2295490,0x28,0x9CCB40) ||
       !v.At(player,0x208,count) || count!=2 || !v.At(player,0x214,local) || local ||
       !v.At(player,0x218,local_id) || local_id || !v.At(player,0x204,flags) || !(flags&1) || (flags&0x100) ||
       !v.At(player,0xA8,current) || current || !v.At(base,0x2BD3FD4,owner_id) || owner_id ||
       !v.At(player,0x70,ids) || !v.At(ids,0,id) || id!=std::array<std::uint16_t,2>{0,1} ||
       !v.At(player,0x78,pools) || !v.At(pools,0,pool) || pool!=std::array<std::uint8_t,2>{0,1} ||
       !v.At(player,0x90,parts) || !v.At(parts,0,part) || part!=std::array<std::uint8_t,2>{3,3} ||
       !v.At(player,0x50,blocks) || !v.At(player,0x378,buffer) || !buffer) return "player_lease";
    if(!v.At(player,0xC8,s.iface) || !v.Method(s.iface,0x23C1720,0x198,0x11AC2E0) ||
       !v.Method(s.iface,0x23C1720,0x18,0x11B0900) || !v.Method(s.iface,0x23C1720,0x20,0x11B2490) ||
       !v.Method(s.iface,0x23C1720,0xA0,0x11B0700) ||
       !v.At(s.iface,0x18,current) || current!=player || !v.At(s.iface,8,s.inventory) ||
       !v.Method(s.inventory,0x23895D0,8,0xEE9880) || !v.Method(s.inventory,0x23895D0,0x40,0xEE9810)) return "equipment_interface";
    std::uint32_t actors=0,stride=0,total=0,inv_local=0;
    if(!v.At(s.inventory,8,actors) || actors!=2 || !v.At(s.inventory,0xC,stride) || stride!=7 ||
       !v.At(s.inventory,0x10,total) || total!=14 || !v.At(s.inventory,0x30,inv_local) || inv_local ||
       !v.At(s.inventory,0x40,s.handles) || !v.At(s.inventory,0x50,s.metadata) || !s.handles || !s.metadata ||
       !v.At(s.inventory,0x58,s.engine) || !v.Method(s.engine,0x234B840,0,0xDC3250) ||
       !v.Method(s.engine,0x234B840,0x10,0xDC2A50)) return "inventory_bounds";
    // DC2A50 calls BFF050 -> globals98 -> controller1A0 -> virtual208.
    // That helper calls the SAME controller's virtual1F8 -> 53C990. Checking
    // only the final scalar cannot guard this actual transitive pointer walk.
    if(!v.At(base,0x2C3A8A0,s.global_root) || !v.At(s.global_root,8,s.globals) ||
       !v.At(s.globals,0x98,s.services) || !v.At(s.services,0x1A0,s.owner_controller) ||
       !v.Method(s.owner_controller,0x25564F0,0x208,0x1E02AD0) ||
       !v.Method(s.owner_controller,0x25564F0,0x1F8,0x1E02B40) ||
       !v.At(s.services,0x1E8,current) || current!=s.engine) return "native_owner_chain";
    if(!v.At(s.handles,0,s.local_handles) || !v.At(s.metadata,0,s.local_metadata) ||
       !v.At(s.handles,7*4,s.remote_handles) || !v.At(s.metadata,7*6,s.remote_metadata)) return "inventory_records";
    if(!v.At(base,0x2C16920,s.vars) || !v.At(s.vars,0xB06,s.local_base_ids) ||
       !v.At(s.vars,0xB30,s.local_recipes)) return "local_vars";
    for(unsigned i=0;i<2;++i) {
        std::uint32_t body_count=0,body_base=0;std::uintptr_t predicate=0,node=0,predicate_records=0,writer=0;
        std::uint8_t restriction=0;std::uint16_t attachment_stride=0;
        if(!v.At(blocks,i*8,s.bodies[i]) || s.bodies[i]!=buffer+i*0x6A8 ||
           (i==1 && s.bodies[i]!=remote_block) || !v.At(s.bodies[i],0,current) || current!=base+0x2295210 ||
           !v.At(s.bodies[i],8,body_count) || body_count!=1 || !v.At(s.bodies[i],0xC,body_base) || body_base!=i ||
           !v.At(s.bodies[i],0x138,current) || current!=player ||
           !v.Method(s.bodies[i],0x2295210,0x128,0x9C5980) || !v.Method(s.bodies[i],0x2295210,0x130,0x9CC6F0) ||
           !v.At(s.bodies[i],0x70,predicate) || !v.Method(predicate,0x23BC1B0,0x30,0x1156640) ||
           !v.At(predicate,0x168,node) || node!=s.bodies[i] || !v.At(predicate,0x1A8,predicate_records) ||
           !v.At(predicate_records,0,restriction) || restriction ||
           !v.At(s.bodies[i],0xF0,writer) || !v.Method(writer,0x23C95D8,0x28,0x1209EC0) ||
           !v.At(writer,8,current) || current!=s.bodies[i] ||
           !v.At(s.bodies[i],0x618,s.models[i]) || !v.Method(s.models[i],0x22EA550,0x20,0xB0C720) ||
           !v.Method(s.models[i],0x22EA550,0x18,0xB0CF80) || !v.Method(s.models[i],0x22EA550,0x28,0xB0C800) ||
           !v.At(s.models[i],0x58,body_count) || body_count!=1 || !v.At(s.models[i],0x5C,body_base) || body_base!=i ||
           !v.At(s.models[i],0x40,current) || current!=player || !v.At(s.models[i],0x50,current) || current!=s.engine ||
           !v.At(s.models[i],0x80,attachment_stride) || attachment_stride!=40 ||
           !v.At(s.models[i],0x30,s.binder_records[i]) || !s.binder_records[i]) return "body_equipment_owner";
    }
    if(!v.At(s.binder_records[0],0,s.local_binder) || !v.At(s.binder_records[1],0,s.remote_binder)) return "binder_records";
    // The actual services320/+60 factory is 786EC0 -> 7C73C0. This
    // fixed-array manager is distinct from the unused 7C75E0 vector class.
    // Its count*8 array holds individually constructed 0x20 attachment items.
    for(unsigned i=0;i<2;++i) {
        std::uint32_t attachment_count=0;
        if(!v.At(s.models[i],0x78,s.attachment_managers[i]) ||
           !v.Method(s.attachment_managers[i],0x2215C78,0,0x7C74F0) ||
           !v.At(s.attachment_managers[i],0x10,attachment_count) || attachment_count!=40 ||
           !v.At(s.attachment_managers[i],8,s.attachment_arrays[i]) ||
           !v.At(s.attachment_arrays[i],0,s.attachment_items[i])) return "attachment_manager_bounds";
        for(unsigned j=0;j<40;++j) {
            if(!s.attachment_items[i][j]) return "attachment_item_missing";
            for(unsigned k=0;k<j;++k) if(s.attachment_items[i][j]==s.attachment_items[i][k])
                return "attachment_item_alias";
            if(i) for(auto local_item:s.attachment_items[0]) if(s.attachment_items[i][j]==local_item)
                return "attachment_item_local_alias";
        }
    }
    if(s.attachment_managers[0]==s.attachment_managers[1] || s.attachment_arrays[0]==s.attachment_arrays[1])
        return "attachment_owner_alias";
    for(unsigned j=0;j<13;++j) if(!v.At(s.attachment_items[1][13*t.slot+j],0,s.remote_slot_attachments[j]))
        return "attachment_back_records";
    std::uintptr_t effects=0,context=0,context_rows=0;std::uint32_t context_count=0,context_base=0;
    if(!v.At(s.bodies[1],0x628,effects) || !v.At(effects,0x50*std::size_t(t.slot),s.remote_slot_effect) ||
       !v.At(s.bodies[1],0x10,context) || !v.Method(context,0x22E6070,0x400,0xAF0A00) ||
       !v.At(context,8,context_count) || context_count!=1 || !v.At(context,0xC,context_base) || context_base!=1 ||
       !v.At(context,0x10,context_rows) || !v.At(context_rows,0x70,current)) return "effect_clear_context";
    if(const char* reason=MissionReady(v,s,t)) return reason;
    if(!v.At(s.engine,0x1D8,s.info) || !v.At(s.engine,0x1E0,s.cache) || !v.At(s.engine,0x1E8,s.refs) ||
       !v.At(s.info,t.cache_index*0x90,s.cache_info) || !v.At(s.cache,t.cache_index*0x38,s.cache_record) ||
       !v.At(s.refs,t.cache_index*4,s.cache_refs)) return "stock_cache";
    if(Word16(s.cache_info.data()+0x5E)!=t.equip || (s.cache_info[0x78]&0x1F)!=t.type || s.cache_info[0x7A]!=t.recipe[0] ||
       s.cache_record[0x34]!=3) return "stock_cache_identity";
    // DC3755..DC376A selects the FIRST used entry of the id: none earlier.
    for(unsigned i=0;i<t.cache_index;++i) {
        std::uint8_t cf=0;std::uint16_t ci=0;
        if(!v.At(s.cache,i*0x38+0x34,cf) || !v.At(s.info,i*0x90+0x5E,ci) || (cf && ci==t.equip)) return "earlier_stock_match";
    }
    std::uintptr_t types=0,resource=0,basic=0;std::uint16_t packed=0;
    std::array<std::uint8_t,12> stock_recipe{};std::array<std::uint64_t,3> package_row{};
    if(!t.row || t.row>0x1818/12 ||
       !v.At(s.engine,8,types) || types!=base+0x2A70920 || !v.Method(types,0x22AD180,0,0xA2A3E0) ||
       !v.At(types,8+std::size_t(t.equip)*2,packed) || packed!=t.packed ||
       !v.At(s.engine,0x10,resource) || resource!=base+0x2A711F0 || !v.At(resource,8,basic) || basic!=base+0x2C25C50 ||
       !v.At(basic,(std::size_t(t.row)-1)*12,stock_recipe) || stock_recipe!=t.recipe ||
       !v.At(base,PackageRowRva(t.equip),package_row) || package_row!=std::array<std::uint64_t,3>{t.parts,t.package,t.package_flags})
        return "stock_parameter_bank";
    std::uint32_t regular=0,meta_count=0,map_count=0;std::uintptr_t meta_owner=0;
    if(!v.At(s.engine,0x100,regular) || regular!=60 ||
       !v.At(s.engine,0x120,s.meta_service) || !v.Method(s.meta_service,0x234A178,0,0xDB72A0) ||
       !v.At(s.meta_service,0x58,meta_count) || meta_count!=regular || !v.At(s.meta_service,0x28,s.meta_rows) ||
       !v.At(s.engine,0x128,s.map) || !v.Method(s.map,0x234A3E8,0,0xDB7590) ||
       !v.At(s.map,8,map_count) || map_count!=regular || !v.At(s.map,0x68,meta_owner) || meta_owner!=s.meta_service ||
       !v.At(s.map,0x18,s.map_rows) || !v.At(s.map,0x10,s.resource_owners) || !s.resource_owners) return "local_engine_bounds";
    for(unsigned i=0;i<3;++i) {
        const auto h=s.local_handles[i],index=h>>16;
        if(!h || index>=regular || !v.At(s.meta_rows,index*0xE,s.local_engine_metadata[i]) ||
           !v.At(s.map_rows,index*4,s.local_resource_ids[i]) || s.local_resource_ids[i]>=regular) return "local_handle_resources";
    }
    return nullptr;
}

template<class Reader> PackageObservation ObservePackage(View<Reader>& v,const Target& t=kStock46) noexcept {
    PackageObservation p{};p.service=v.base+0x2A6D040;
    auto read=[&](unsigned bit,std::size_t offset,auto& value) {
        if(v.At(p.service,offset,value)) p.reads|=1u<<bit;
    };
    read(0,8,p.backing);read(1,0x10,p.allocation);read(2,0x18,p.capacity);
    read(3,0x20,p.size);read(4,0x30,p.parameters);
    if(v.At(p.parameters,8,p.variant)) p.reads|=1u<<5;
    if(v.At(p.parameters,0,p.table)) p.reads|=1u<<6;
    constexpr std::array<std::size_t,3> slots{8,0x28,0x38},rvas{0xA06440,0xA05B40,0xA057C0};
    // Never follow a foreign vtable even for a diagnostic receipt.
    if(p.table==v.base+0x22A2660) for(unsigned i=0;i<3;++i)
        if(v.At(p.table,slots[i],p.methods[i])) p.reads|=1u<<(7+i);
    // 1307EF2..1307F19: only an optional-carrying target reads its descriptor.
    if(t.optional && t.equip<=0x7C && v.At(v.base+0x2A6B400,8+std::size_t(t.equip)*8,p.descriptor)) p.reads|=1u<<10;
    if((p.reads&3)==3 && p.backing && p.backing==p.allocation) p.checks|=1;
    if((p.reads&4) && p.capacity==0x90) p.checks|=2;
    if((p.reads&8) && p.size==p.capacity) p.checks|=4;
    if((p.reads&17)==17 && p.backing<=UINTPTR_MAX-0x90 && p.parameters==p.backing+0x30) p.checks|=8;
    if((p.reads&32) && !p.variant) p.checks|=16;
    if((p.reads&64) && p.table==v.base+0x22A2660) p.checks|=32;
    for(unsigned i=0;i<3;++i) if((p.reads&(1u<<(7+i))) && p.methods[i]==v.base+rvas[i]) p.checks|=1u<<(6+i);
    // 1307F19: mov rdx,[rax+rdx*8+8]. The bank has a vtable prefix.
    // Logical45 at +46*8 is 67011DD7778F7009, not native equip46.
    if((p.reads&1024) && p.descriptor==t.optional_hash) p.checks|=512;
    return p;
}
// The masks a ready package service shows for this target (no descriptor read without the optional).
constexpr std::uint32_t PackageReads(const Target& t) noexcept {return t.optional ? 0x7FFu : 0x3FFu;}
constexpr std::uint32_t PackageChecks(const Target& t) noexcept {return t.optional ? 0x3FFu : 0x1FFu;}

template<class Reader> bool RenderBankOwner(View<Reader>& v,const BankLease& b,bool optional) noexcept {
    std::uintptr_t arena=0,policy=0,callback=0,context=0,extra=0;
    std::uint32_t stride=0;
    if(!v.At(b.bank,0x68,arena) || arena!=b.arena || !v.At(b.bank,0x70,stride) || stride!=b.stride ||
       !v.At(b.bank,0x78,policy) || policy!=b.policy || !v.At(b.bank,0xD8,callback) || callback!=b.callback ||
       !v.At(b.bank,0xE8,extra) || extra!=b.extra) return false;
    if(optional) return !arena && !policy && !callback && !extra && stride==2048;
    std::uintptr_t group=0,bank=0;std::uint32_t count=0;
    if(!v.At(b.bank,0xE0,context) || context!=b.callback_context || b.group<0x20 || context!=b.group-0x20 ||
       !v.At(context,0x20,group) || group!=v.base+0x23DB5C0 ||
       !v.At(context,0x30,bank) || bank!=b.bank || !v.At(context,0x50,count) || count!=b.target_count)
        return false;
    // ADC635..ADC65D binds each constructed 0x50 extra record to this bank
    // and its target index. Contents may change in the actual bound target.
    for(unsigned i=0;i<b.target_count;++i) {
        std::uintptr_t table=0,owner=0;std::uint32_t index=0;
        if(!v.At(extra,i*0x50,table) || table!=v.base+0x22E2950 ||
           !v.At(extra,i*0x50+0x40,owner) || owner!=b.bank ||
           !v.At(extra,i*0x50+0x4C,index) || index!=i) return false;
    }
    return true;
}

template<class Reader> const char* QualifyDelayedResources(View<Reader>& v,Snapshot& s,
        std::uintptr_t groups,std::uintptr_t resource_rows,std::uintptr_t map_flags,
        std::uintptr_t free_queue,std::uint32_t free_cursor) noexcept {
    s.free_cursor=free_cursor;
    if(resource_rows!=s.resource_owners || !v.At(s.map_rows,0,s.initial_maps) || !v.At(resource_rows,0,s.initial_owners) ||
       !v.At(map_flags,0,s.initial_load_flags) || !v.At(s.meta_rows,0,s.initial_engine_metadata) ||
       !v.At(free_queue,0,s.free_queue)) return "delayed_map_unreadable";
    std::uint64_t owned=0,queued=0;
    for(unsigned handle=0;handle<60;++handle) {
        const auto resource=s.initial_maps[handle];
        // Dormant allocated-but-unmapped rows are legitimate. Pending demand
        // or release waits; allocated <-> mapped equivalence is not assumed.
        if(s.initial_load_flags[handle]&3) return "delayed_map_pending";
        if(resource==UINT32_MAX) continue;
        if(resource>=60 || Word16(s.initial_engine_metadata[handle].data())==0x7FF ||
           s.initial_owners[resource]!=handle || (owned&(1ull<<resource))) return "delayed_map_owner";
        owned|=1ull<<resource;
    }
    for(unsigned resource=0;resource<60;++resource) {
        const auto owner=s.initial_owners[resource];
        if(owner==UINT32_MAX) {s.eligible_resources|=1ull<<resource;continue;}
        if(owner>=60 || s.initial_maps[owner]!=resource) return "delayed_map_owner";
    }
    for(unsigned i=free_cursor;i<60;++i) {
        const auto resource=s.free_queue[i];
        if(resource>=60 || (queued&(1ull<<resource)) || !(s.eligible_resources&(1ull<<resource)))
            return "delayed_free_queue_owner";
        queued|=1ull<<resource;
    }
    if(queued!=s.eligible_resources || !queued) return "delayed_free_queue_owner";
    std::uint32_t base_resource=0;
    for(unsigned group_index=0;group_index<32;++group_index) {
        std::uintptr_t group=0,group_metadata=0,package=0,inline_bank=0,optional_lookup=0;
        std::uint32_t size=0,native_base=0;
        if(!v.At(groups,group_index*8,group) || !v.At(group,0x30,size) || size!=(group_index<28?2u:1u) ||
           !v.At(group,0x34,native_base) || native_base!=base_resource || !v.At(group,0x38,group_metadata) || group_metadata!=s.meta_service ||
           !v.At(group,0x68,package) || package!=v.base+0x2A6D040 ||
           !v.At(group,0x70,inline_bank) || inline_bank!=v.base+0x2A6B400 ||
           !v.At(group,0x58,optional_lookup) || optional_lookup!=v.base+0x2A71328) return "delayed_group_owner";
        for(unsigned optional=0;optional<2;++optional) {
            auto& b=s.banks[group_index*2+optional];b.group=group;b.target_count=size*(optional+1);
            std::uint32_t targets=0;std::uint16_t flags=0;
            if(!v.At(group,0x10+optional*8,b.bank) || !v.Method(b.bank,0x22E2970,0x10,0xADDE80) ||
               !v.At(b.bank,8,b.count) || !b.count || b.count>64 || !v.At(b.bank,0x50,targets) || targets!=b.target_count ||
               !v.At(b.bank,0x28,b.allocation) || !b.allocation || !v.At(b.bank,0x38,b.bytes) || !b.bytes ||
               b.bytes>1024*1024 || b.allocation>UINTPTR_MAX-b.bytes || !v.At(b.bank,0x40,b.used) || !b.used || b.used>b.bytes ||
               !v.At(b.bank,0x4A,flags) || !(flags&1) || !v.At(b.bank,0x58,b.targets) || !b.targets)
                return "delayed_bank_bounds";
            for(unsigned i=0;i<4;++i) if(!v.At(b.bank,0x88+i*8,b.arrays[i]) || !b.arrays[i]) return "delayed_bank_bounds";
            if(!v.At(b.bank,0x10,b.aux[0]) || !b.aux[0] || !v.At(b.bank,0xD0,b.aux[1]) || !b.aux[1] ||
               !v.At(b.bank,0x60,b.aux[2])) return "delayed_bank_bounds";
            if(!v.At(b.bank,0x68,b.arena) || !v.At(b.bank,0x70,b.stride) || b.stride!=2048 ||
               !v.At(b.bank,0x78,b.policy) || !v.At(b.bank,0xD8,b.callback) || !v.At(b.bank,0xE8,b.extra))
                return "delayed_render_bank_bounds";
            if(optional) {
                if(b.arena || b.policy || b.callback || b.extra) return "delayed_subbank_render_shape";
            } else if(!b.arena || !b.policy || !b.extra || b.callback!=v.base+0x1306FF0 ||
                      !v.At(b.bank,0xE0,b.callback_context) || !b.aux[2] || s.optional.render_bytes>b.stride)
                return "delayed_main_render_shape";
            struct Span {std::uintptr_t pointer;std::size_t bytes;};
            const std::array<Span,11> spans{{{b.targets,targets*8},{b.arrays[0],b.count*0x18},
                {b.arrays[1],b.count*8},{b.arrays[2],b.count*2},{b.arrays[3],b.count*8},
                {b.aux[0],targets},{b.aux[1],targets},{b.aux[2],targets*8},
                {b.arena,optional?0u:targets*b.stride},{b.policy,optional?0u:targets*8},
                {b.extra,optional?0u:targets*0x50}}};
            for(unsigned i=0;i<spans.size();++i) {
                const auto p=spans[i];if(!p.pointer && i>=7) continue;
                // Every main arena, policy and typed extra span must be
                // contained in this bank's USED backing, independently of N.
                if(p.pointer<b.allocation || p.bytes>b.used || p.pointer-b.allocation>b.used-p.bytes) return "delayed_bank_array_bounds";
                for(unsigned j=0;j<i;++j) if(spans[j].pointer && p.pointer<spans[j].pointer+spans[j].bytes &&
                    spans[j].pointer<p.pointer+p.bytes) return "delayed_bank_array_alias";
            }
            if(!RenderBankOwner(v,b,optional!=0)) return "delayed_render_owner";
            if(!optional) for(unsigned i=0;i<targets;++i) {
                if(!v.At(b.policy,i*8,b.policies[i]) || !v.At(b.extra,i*0x50,b.extra_records[i]))
                    return "delayed_render_records";
            }
            unsigned unused_count=0;
            for(unsigned i=0;i<b.count;++i) {
                auto& e=b.entries[i];std::uintptr_t asset=0;
                if(!v.At(b.arrays[0],i*0x18,e.reference) || !v.At(b.arrays[1],i*8,e.key) ||
                   !v.At(b.arrays[2],i*2,e.refs) || !v.At(b.arrays[3],i*8,e.component)) return "delayed_cache_unreadable";
                std::memcpy(&asset,e.reference.data(),sizeof(asset));
                if(!asset) {
                    if(!Zero(e.reference) || e.key || e.refs || e.component) return "delayed_cache_null_dirty";
                    b.unused|=1ull<<i;++unused_count;
                }
                if(!optional && asset==s.stock_parts_asset && e.key==s.mission_entry) return "delayed_cache_stock_match";
            }
            // One natural DB7590 pass can prepare at most N resources in a
            // group, with at most 2*N optional targets. Reserve nothing: merely
            // qualify enough initially unused cache entries for that whole pass.
            if(unused_count<b.target_count) return "delayed_cache_capacity";
            for(unsigned local=0;local<size;++local) {
                if(!(s.eligible_resources&(1ull<<(base_resource+local)))) continue;
                for(unsigned lane=0;lane<=optional;++lane) {
                    const auto target=local+lane*size;std::uintptr_t model=0,render=0;
                    std::uint8_t cache_index=0,flag=0;
                    if(!v.At(b.targets,target*8,model) || model || !v.At(b.aux[0],target,cache_index) || cache_index>b.count ||
                       !v.At(b.aux[1],target,flag) || flag>1 || (b.aux[2] && (!v.At(b.aux[2],target*8,render) || render)))
                        return "delayed_free_target_not_empty";
                    if(!optional && (b.policies[target] || Word32(b.extra_records[target].data()+0x14)!=1 ||
                                     Word32(b.extra_records[target].data()+0x48)))
                        return "delayed_free_render_not_empty";
                }
            }
        }
        base_resource+=size;
    }
    for(unsigned i=0;i<64;++i) for(unsigned j=0;j<i;++j) {
        const auto& a=s.banks[i];const auto& b=s.banks[j];
        if(a.bank==b.bank || (a.allocation<b.allocation+b.bytes && b.allocation<a.allocation+a.bytes))
            return "delayed_bank_owner_alias";
    }
    return base_resource==60 ? nullptr : "delayed_group_capacity";
}

template<class Reader> const char* Preflight(std::uintptr_t base,std::size_t span,Reader& reader,
        std::uintptr_t player,std::uintptr_t remote_block,Snapshot& s,const Target& t=kStock46) noexcept {
    if(const char* reason=Capture(base,span,reader,player,remote_block,s,t)) return reason;
    if(t.require_remote_empty) {
        if(!Zero(s.remote_handles) || !Zero(s.remote_metadata) || !Zero(s.remote_slot_effect)) return "remote_not_empty";
    } else {
        // Never replace a filled slot: the target slot's handle, its six
        // metadata bytes and its effect record are untouched (other slots may
        // hold this allocation's earlier dispatches; the writer checks those).
        bool metadata_zero=true;
        for(unsigned i=0;i<6;++i) metadata_zero=metadata_zero && s.remote_metadata[t.slot*6+i]==0;
        if(s.remote_handles[t.slot] || !metadata_zero || !Zero(s.remote_slot_effect)) return "remote_slot_not_empty";
    }
    for(const auto& attachment:s.remote_slot_attachments) {
        std::uintptr_t resource=0;std::memcpy(&resource,attachment.data(),sizeof(resource));
        if(resource) return "remote_attachment_not_empty";
    }
    // B0C720: the binder record of slot s is binder30 + s*12 (current, pending, flags).
    for(unsigned i=0;i<3;++i) {
        if(!t.require_remote_empty && i!=t.slot) continue;
        if(Word32(s.remote_binder.data()+i*12)!=0xFFFFFFE0u ||
           Word32(s.remote_binder.data()+i*12+4) || Word32(s.remote_binder.data()+i*12+8)) return "remote_binder_not_empty";
    }
    if(s.cache_refs) return "stock_cache_in_use";
    View<Reader> v{reader,base,span};std::uint32_t regular=0,meta_count=0,handle_count=0,resource_count=0;
    std::uintptr_t meta_owner=0,map_flags=0,resource_rows=0;std::uint8_t load_flags=0;
    if(!v.At(s.engine,0x100,regular) || regular!=60 || !v.At(s.engine,0x48,s.ring) ||
       !v.At(s.engine,0x50,s.ring_capacity) || s.ring_capacity!=regular ||
       !v.At(s.engine,0x54,s.ring_cursor) || s.ring_cursor>=s.ring_capacity ||
       !v.At(s.ring,s.ring_cursor*4,s.new_index) || s.new_index>=regular ||
       !v.At(s.engine,0x120,s.meta_service) || !v.Method(s.meta_service,0x234A178,0,0xDB72A0) ||
       !v.At(s.meta_service,0x58,meta_count) || meta_count!=regular || !v.At(s.meta_service,0x28,s.meta_rows) ||
       !v.At(s.meta_rows,s.new_index*0xE,s.new_metadata) ||
       !v.At(s.engine,0x128,s.map) || !v.Method(s.map,0x234A3E8,0,0xDB7590) ||
       !v.At(s.map,8,handle_count) || handle_count!=regular || !v.At(s.map,0xC,resource_count) || resource_count!=regular ||
       !v.At(s.map,0x68,meta_owner) || meta_owner!=s.meta_service ||
       !v.At(s.map,0x18,s.map_rows) || !v.At(s.map,0x20,map_flags) ||
       !v.At(s.map_rows,s.new_index*4,s.resource_id) || s.resource_id!=0xFFFFFFFFu ||
       !v.At(map_flags,s.new_index,load_flags) || load_flags ||
       !v.At(s.map,0x10,resource_rows) || !resource_rows) return "new_handle_capacity";
    if(Word16(s.new_metadata.data())!=0x7FFu) return "new_handle_not_free";
    std::uint32_t free_capacity=0,free_cursor=0,resource_owner=0;
    std::uintptr_t free_queue=0;
    if(!v.At(s.map,0x30,free_capacity) || free_capacity!=regular ||
       !v.At(s.map,0x34,free_cursor) || free_cursor>=free_capacity ||
       !v.At(s.map,0x28,free_queue) || !v.At(free_queue,free_cursor*4,s.next_resource) || s.next_resource>=regular ||
       !v.At(resource_rows,s.next_resource*4,resource_owner) || resource_owner!=0xFFFFFFFFu)
        return "new_resource_not_free";
    for(auto handle:s.local_handles) if(handle && (handle>>16)==s.new_index) return "new_handle_local_alias";
    std::uintptr_t groups=0,sizes=0,resource_service=0;std::uint32_t group_count=0,total_size=0;
    if(!v.At(s.engine,0x110,group_count) || group_count!=32 || !v.At(s.engine,0x130,groups) || !groups ||
       !v.At(s.engine,0x138,sizes) || !sizes || !v.At(s.map,0x70,resource_service) || resource_service!=s.engine+0x40 ||
       !v.Method(resource_service,0x234BB48,0,0xDBF370)) return "resource_group_bounds";
    std::array<std::uintptr_t,32> seen_groups{};
    std::array<std::uintptr_t,64> seen_banks{};
    std::array<std::uintptr_t,64> bank_allocations{};
    std::array<std::uint64_t,64> bank_allocation_bytes{};
    for(unsigned i=0;i<32;++i) {
        std::uintptr_t group=0,bank=0,group_vt=0;std::uint32_t size=0,bank_count=0,native_size=0;
        if(!v.At(groups,i*8,group) || !v.At(group,0,group_vt) || group_vt!=base+0x23DB5C0) return "resource_group_bounds";
        for(unsigned prior=0;prior<i;++prior) if(seen_groups[prior]==group) return "resource_group_alias";
        seen_groups[i]=group;
        if(!v.At(group,0x30,size) || size!=(i<28 ? 2u : 1u) || !v.At(sizes,i*4,native_size) || native_size!=size)
            return "resource_group_capacity";
        for(unsigned optional=0;optional<2;++optional) {
            if(!v.At(group,0x10+optional*8,bank) || !v.Method(bank,0x22E2970,0x240,0xADBE00) ||
               !v.Method(bank,0x22E2970,0x10,0xADDE80) || !v.Method(bank,0x22E2970,0x1A0,0xADDDB0) ||
               !v.At(bank,0x50,bank_count) || bank_count!=size*(optional+1)) return "resource_bank_capacity";
            const unsigned n=i*2+optional;
            for(unsigned prior=0;prior<n;++prior) if(seen_banks[prior]==bank) return "resource_bank_alias";
            seen_banks[n]=bank;
            if(!v.At(bank,0x28,bank_allocations[n]) || !bank_allocations[n] ||
               !v.At(bank,0x38,bank_allocation_bytes[n]) || !bank_allocation_bytes[n] ||
               bank_allocation_bytes[n]>1024*1024 || bank_allocations[n]>UINTPTR_MAX-bank_allocation_bytes[n])
                return "resource_bank_allocation";
        }
        total_size+=size;
        if(!s.selected_group && s.next_resource<total_size) {
            s.selected_group=group;s.selected_group_index=i;
            s.selected_resource_index=s.next_resource-(total_size-size);
        }
    }
    if(total_size!=regular || !s.selected_group) return "resource_total_capacity";
    std::uint32_t group_base=0,group_size=0;std::uintptr_t group_meta=0,inline_bank=0,optional_lookup=0;
    if(!v.At(s.selected_group,0x34,group_base) || group_base+s.selected_resource_index!=s.next_resource ||
       !v.At(s.selected_group,0x38,group_meta) || group_meta!=s.meta_service ||
       !v.At(s.selected_group,0x68,s.package_service) || s.package_service!=base+0x2A6D040 ||
       !v.Method(s.package_service,0x22A26E0,0,0xA047D0) ||
       !v.At(s.selected_group,0x70,inline_bank) || inline_bank!=base+0x2A6B400 ||
       !v.Method(inline_bank,0x22A0F18,0,0x9FFE10) ||
       !v.At(s.selected_group,0x58,optional_lookup) || optional_lookup!=base+0x2A71328 ||
       !v.Method(optional_lookup,0x22B89D8,0x28,0xACCC10) ||
       !v.At(s.selected_group,0x30,group_size) || s.selected_resource_index>=group_size) return "selected_resource_ownership";
    // Separate bank capacity is N and 2*N. Optional indices are local index
    // and local index+N (1308534/1308599), not 2*local index.
    for(unsigned optional=0;optional<2;++optional) {
        if(!v.At(s.selected_group,0x10+optional*8,s.selected_banks[optional]) ||
           !v.At(s.selected_banks[optional],0x58,s.selected_bank_arrays[optional]) || !s.selected_bank_arrays[optional])
            return "selected_bank_arrays";
        for(unsigned lane=0;lane<=optional;++lane) {
            std::uintptr_t resource=0;
            if(!v.At(s.selected_bank_arrays[optional],(s.selected_resource_index+lane*group_size)*8,resource) || resource)
                return "selected_bank_not_empty";
        }
        // ADC080 allocates independent ordinary cache arrays using count+8:
        // shared asset refs+88 stride0x18, match keys+90 stride8, refs+98
        // stride2, and typed components+A0 stride8. ADCCD0 prefers the first
        // NULL asset before its zero-ref eviction pass. Require that exact
        // candidate to be genuinely unused, and refuse an existing stock
        // asset/current-entry match that would bypass the NULL selection.
        const auto bank=s.selected_banks[optional];
        auto& cache_arrays=s.selected_cache_arrays[optional];
        auto& aux=s.selected_bank_aux[optional];
        std::uint8_t target_flag=0,target_entry=0;
        std::uint16_t allocation_flags=0;
        if(!v.At(bank,8,s.selected_cache_counts[optional]) || !s.selected_cache_counts[optional] ||
           s.selected_cache_counts[optional]>64 ||
           !v.At(bank,0x88,cache_arrays[0]) || !cache_arrays[0] ||
           !v.At(bank,0x90,cache_arrays[1]) || !cache_arrays[1] ||
           !v.At(bank,0x98,cache_arrays[2]) || !cache_arrays[2] ||
           !v.At(bank,0xA0,cache_arrays[3]) || !cache_arrays[3] ||
           !v.At(bank,0x10,aux[0]) || !aux[0] || !v.At(bank,0xD0,aux[1]) || !aux[1] ||
           !v.At(bank,0x60,aux[2])) return "selected_cache_bounds";
        // ADC5E8..ADC624 allocates+clears backing+28 of size+38, sets+4A
        // bit0 and reruns all array offsets with used bytes+40. Every array
        // below must belong to that one bank and occupy a separate span.
        auto& allocation=s.selected_bank_allocations[optional];
        auto& allocation_bytes=s.selected_bank_allocation_bytes[optional];
        auto& used_bytes=s.selected_bank_used_bytes[optional];
        if(!v.At(bank,0x28,allocation) || !allocation || !v.At(bank,0x38,allocation_bytes) ||
           !allocation_bytes || allocation_bytes>1024*1024 || allocation>UINTPTR_MAX-allocation_bytes ||
           !v.At(bank,0x40,used_bytes) || !used_bytes || used_bytes>allocation_bytes ||
           !v.At(bank,0x4A,allocation_flags) || !(allocation_flags&1)) return "selected_cache_allocation";
        const std::uint32_t targets=group_size*(optional+1),entries=s.selected_cache_counts[optional];
        struct ArraySpan {std::uintptr_t pointer;std::size_t bytes;};
        const std::array<ArraySpan,8> spans{{{s.selected_bank_arrays[optional],targets*8},
            {cache_arrays[0],entries*0x18},{cache_arrays[1],entries*8},{cache_arrays[2],entries*2},
            {cache_arrays[3],entries*8},{aux[0],targets},{aux[1],targets},{aux[2],targets*8}}};
        for(unsigned index=0;index<spans.size();++index) {
            const auto region=spans[index];
            if(!region.pointer && index==7) continue;
            if(region.pointer<allocation || region.bytes>allocation_bytes ||
               region.pointer-allocation>allocation_bytes-region.bytes) return "selected_cache_allocation";
            for(unsigned prior=0;prior<index;++prior) {
                const auto other=spans[prior];
                if(other.pointer && region.pointer<other.pointer+other.bytes && other.pointer<region.pointer+region.bytes)
                    return "selected_cache_array_alias";
            }
        }
        for(std::uint32_t entry_index=0;entry_index<s.selected_cache_counts[optional];++entry_index) {
            BankCacheEntry cache_entry{};std::uintptr_t asset=0;
            if(!v.At(cache_arrays[0],entry_index*0x18,cache_entry.reference) ||
               !v.At(cache_arrays[1],entry_index*8,cache_entry.key) ||
               !v.At(cache_arrays[2],entry_index*2,cache_entry.refs) ||
               !v.At(cache_arrays[3],entry_index*8,cache_entry.component)) return "selected_cache_unreadable";
            std::memcpy(&asset,cache_entry.reference.data(),sizeof(asset));
            const bool empty=Zero(cache_entry.reference) && !cache_entry.key && !cache_entry.refs && !cache_entry.component;
            if(optional) {if(!empty) return "selected_cache_not_empty";}
            else {
                s.main_cache_entries[entry_index]=cache_entry;
                if(asset==s.stock_parts_asset && cache_entry.key==s.mission_entry) return "selected_cache_stock_match";
                if(!asset && s.main_cache_candidate==UINT32_MAX) {
                    if(!empty) return "selected_cache_null_entry_dirty";
                    s.main_cache_candidate=entry_index;
                }
            }
        }
        if(!optional && s.main_cache_candidate==UINT32_MAX) return "selected_cache_no_null_entry";
        for(unsigned lane=0;lane<=optional;++lane) {
            const auto target=s.selected_resource_index+lane*group_size;
            std::uintptr_t old_instance=0;
            // H4 normal loading/release leaves index=count sentinel/flag1.
            // ADDED1 clears this flag before reading it; ADE13E overwrites
            // the selected index. Empty model/render pointers and free map
            // ownership authorize only these observed released states.
            if(!v.At(aux[0],target,target_entry) || target_entry>s.selected_cache_counts[optional] ||
               !v.At(aux[1],target,target_flag) || target_flag>1 ||
               (aux[2] && (!v.At(aux[2],target*8,old_instance) || old_instance))) return "selected_bank_aux_not_empty";
        }
    }
    if(s.selected_bank_allocations[0]<s.selected_bank_allocations[1]+s.selected_bank_allocation_bytes[1] &&
       s.selected_bank_allocations[1]<s.selected_bank_allocations[0]+s.selected_bank_allocation_bytes[0])
        return "selected_cache_bank_alias";
    // A selected bank may not share backing with any other body/resource bank,
    // including the banks carrying the local player's three loaded handles.
    for(unsigned selected=0;selected<2;++selected) for(unsigned other=0;other<64;++other) {
        if(other==s.selected_group_index*2+selected) continue;
        if(s.selected_bank_allocations[selected]<bank_allocations[other]+bank_allocation_bytes[other] &&
           bank_allocations[other]<s.selected_bank_allocations[selected]+s.selected_bank_allocation_bytes[selected])
            return "selected_cache_other_bank_alias";
    }
    // A06260 configures six 16-byte records. The sole stock request's
    // descriptor[0] >> 11 is zero, so only parameter record0 is eligible.
    s.package_observation=ObservePackage(v,t);
    s.package_parameters=s.package_observation.parameters;
    if(s.package_observation.reads!=PackageReads(t) || s.package_observation.checks!=PackageChecks(t))
        return "selected_package_bounds";
    if(const auto reason=QualifyDelayedResources(v,s,groups,resource_rows,map_flags,free_queue,free_cursor)) return reason;
    return nullptr;
}
template<class Reader> const char* DelayedCachesPreserved(View<Reader>& v,const Snapshot& before) noexcept {
    std::uintptr_t groups=0;
    if(!v.At(before.engine,0x130,groups)) return "delayed_bank_owner_changed";
    for(unsigned index=0;index<64;++index) {
        const auto& b=before.banks[index];std::uintptr_t group=0,bank=0,allocation=0,targets=0;
        std::uint64_t bytes=0,used=0;std::uint32_t count=0,target_count=0;
        if(!v.At(groups,(index/2)*8,group) || group!=b.group || !v.At(group,0x10+(index%2)*8,bank) || bank!=b.bank ||
           !v.Method(bank,0x22E2970,0x10,0xADDE80) || !v.At(bank,8,count) || count!=b.count ||
           !v.At(bank,0x50,target_count) || target_count!=b.target_count || !v.At(bank,0x28,allocation) || allocation!=b.allocation ||
           !v.At(bank,0x38,bytes) || bytes!=b.bytes || !v.At(bank,0x40,used) || used!=b.used ||
           !v.At(bank,0x58,targets) || targets!=b.targets) return "delayed_bank_owner_changed";
        for(unsigned i=0;i<4;++i) {std::uintptr_t pointer=0;
            if(!v.At(bank,0x88+i*8,pointer) || pointer!=b.arrays[i]) return "delayed_bank_owner_changed";}
        constexpr std::array<std::size_t,3> offsets{0x10,0xD0,0x60};
        for(unsigned i=0;i<3;++i) {std::uintptr_t pointer=0;
            if(!v.At(bank,offsets[i],pointer) || pointer!=b.aux[i]) return "delayed_bank_owner_changed";}
        if(!RenderBankOwner(v,b,(index%2)!=0)) return "delayed_render_owner_changed";
        for(unsigned i=0;i<b.count;++i) {
            // Any initially unused entry may serve another native request.
            // Initially occupied entries, including zero-ref cached entries,
            // remain byte-identical. Rejection cannot undo a native change.
            if(b.unused&(1ull<<i)) continue;
            BankCacheEntry e{};
            if(!v.At(b.arrays[0],i*0x18,e.reference) || !v.At(b.arrays[1],i*8,e.key) ||
               !v.At(b.arrays[2],i*2,e.refs) || !v.At(b.arrays[3],i*8,e.component)) return "delayed_cache_unreadable";
            if(!(e==b.entries[i])) return "delayed_occupied_cache_changed";
        }
    }
    return nullptr;
}
template<class Reader> const char* QualifiedBoundResource(View<Reader>& v,const Snapshot& before,
        std::uint32_t resource) noexcept {
    if(resource>=60 || !(before.eligible_resources&(1ull<<resource))) return "bound_resource_unqualified";
    std::uintptr_t owners=0;std::uint32_t owner=0;
    if(!v.At(before.map,0x10,owners) || owners!=before.resource_owners ||
       !v.At(owners,resource*4,owner) || owner!=before.new_index) return "bound_resource_owner";
    unsigned group=0;std::uint32_t local=resource;
    for(;group<32;++group) {const auto size=group<28?2u:1u;if(local<size) break;local-=size;}
    if(group>=32) return "bound_resource_unqualified";
    const auto& b=before.banks[group*2];std::uint8_t selected=0;
    std::uintptr_t model=0;BankCacheEntry e{};
    if(!v.At(b.targets,local*8,model) || !model || !v.At(b.aux[0],local,selected) || selected>=b.count ||
       !(b.unused&(1ull<<selected))) return "bound_cache_unqualified";
    if(!v.At(b.arrays[0],selected*0x18,e.reference) || !v.At(b.arrays[1],selected*8,e.key) ||
       !v.At(b.arrays[2],selected*2,e.refs) || !v.At(b.arrays[3],selected*8,e.component)) return "bound_cache_unreadable";
    std::uintptr_t asset=0;std::memcpy(&asset,e.reference.data(),sizeof(asset));
    if(asset!=before.stock_parts_asset || e.key!=before.mission_entry || !e.refs || !e.component)
        return "bound_stock_cache_identity";
    // ADE3E8..ADE3F6 chooses arena68+target*stride70; ADE40D constructs there,
    // then ADE446 publishes that same base in render60[target]. This is owned
    // allocation evidence, not visible rendering or animation completion.
    std::uintptr_t render=0;std::uint32_t bytes=0,body_offset=0;
    const auto expected=b.arena+std::size_t(local)*b.stride;
    if(!b.arena || !b.aux[2] || !v.At(b.aux[2],local*8,render) || render!=expected ||
       !v.At(render,0,bytes) || bytes!=before.optional.render_bytes ||
       !v.At(render,0xC,body_offset) || body_offset!=0x20) return "bound_render_unqualified";
    return nullptr;
}
// Retained historical predictive-bank fixture check. Runtime acceptance now
// uses DelayedCachesPreserved plus QualifiedBoundResource for the actual bank.
template<class Reader> const char* MainCachePreserved(View<Reader>& v,const Snapshot& before) noexcept {
    std::uintptr_t groups=0,group=0,bank=0,allocation=0;std::uint32_t count=0;std::uint64_t bytes=0;
    if(!v.At(before.engine,0x130,groups) || !v.At(groups,before.selected_group_index*8,group) || group!=before.selected_group ||
       !v.At(group,0x10,bank) || bank!=before.selected_banks[0] || !v.Method(bank,0x22E2970,0x10,0xADDE80) ||
       !v.At(bank,8,count) || count!=before.selected_cache_counts[0] || !count || count>64 ||
       before.main_cache_candidate>=count || !v.At(bank,0x28,allocation) || allocation!=before.selected_bank_allocations[0] ||
       !v.At(bank,0x38,bytes) || bytes!=before.selected_bank_allocation_bytes[0]) return "main_cache_owner_changed";
    std::array<std::uintptr_t,4> arrays{};
    for(unsigned lane=0;lane<4;++lane) if(!v.At(bank,0x88+lane*8,arrays[lane]) || arrays[lane]!=before.selected_cache_arrays[0][lane])
        return "main_cache_owner_changed";
    for(std::uint32_t i=0;i<count;++i) {
        if(i==before.main_cache_candidate) continue;
        BankCacheEntry after{};
        if(!v.At(arrays[0],i*0x18,after.reference) || !v.At(arrays[1],i*8,after.key) ||
           !v.At(arrays[2],i*2,after.refs) || !v.At(arrays[3],i*8,after.component) ||
           !(after==before.main_cache_entries[i])) return "other_main_cache_changed";
    }
    return nullptr;
}
// Callers must recheck body_epoch and all owner guards separately. No writer
// success code substitutes for unchanged local state or actual remote handle.
inline const char* LocalUnchanged(const Snapshot& before,const Snapshot& after) noexcept {
    if(before.player!=after.player || before.iface!=after.iface || before.inventory!=after.inventory ||
       before.handles!=after.handles || before.metadata!=after.metadata || before.vars!=after.vars ||
       before.engine!=after.engine || before.info!=after.info || before.cache!=after.cache || before.refs!=after.refs ||
       before.meta_service!=after.meta_service || before.meta_rows!=after.meta_rows ||
       before.map!=after.map || before.map_rows!=after.map_rows || before.resource_owners!=after.resource_owners ||
       before.bodies!=after.bodies || before.models!=after.models ||
       before.binder_records!=after.binder_records || before.mission_entry!=after.mission_entry ||
       before.global_root!=after.global_root || before.globals!=after.globals || before.services!=after.services ||
       before.owner_controller!=after.owner_controller) return "owner_changed";
    if(before.attachment_managers!=after.attachment_managers || before.attachment_arrays!=after.attachment_arrays ||
       before.attachment_items!=after.attachment_items) return "attachment_owner_changed";
    if(before.local_handles!=after.local_handles || before.local_metadata!=after.local_metadata ||
       before.local_binder!=after.local_binder || before.local_base_ids!=after.local_base_ids ||
       before.local_recipes!=after.local_recipes || before.local_engine_metadata!=after.local_engine_metadata ||
       before.local_resource_ids!=after.local_resource_ids) return "local_equipment_changed";
    if(before.cache_info!=after.cache_info || before.cache_record!=after.cache_record) return "stock_cache_changed";
    if(before.stock_parts_asset!=after.stock_parts_asset || before.stock_parts_word30!=after.stock_parts_word30 ||
       before.stock_parts_entity!=after.stock_parts_entity)
        return "stock_parts_asset_changed";
    if(!(before.optional==after.optional)) return "stock_optional_changed";
    return nullptr;
}

// --- Generalized targets (docs/native-puppet.md "Generalized writer") --------
// DB6AC0 for a non-alias gun (types 1..8): the recipe lanes 0/1/2/3/10 through
// the LUT getters DB6CB0/DB69D0/DB6A20/DB6CC0/DB6CD0 (one byte each), then the
// id tables 2349980 (two bytes per class, at most two while nonzero),
// 23499E0, 2349A10 (two, its own count), 2349A08 and 2349A68 (one each).
// Every table is read-only executable data pinned by the generated gates.
template<class Reader> bool DependencyIds(View<Reader>& v,const std::array<std::uint8_t,12>& recipe,
        std::array<std::uint8_t,7>& ids,unsigned& count) noexcept {
    constexpr std::array<std::size_t,5> luts{0x2349A90,0x2349B80,0x2349C00,0x2349CC0,0x2349A78};
    constexpr std::array<unsigned,5> lanes{0,1,2,3,10};
    std::array<std::uint8_t,5> k{};
    ids={};count=0;
    const auto byte=[&](std::size_t rva,std::uint8_t& out) noexcept {return v.Image(rva,1) && v.At(v.base,rva,out);};
    for(unsigned i=0;i<5;++i) if(!byte(luts[i]+recipe[lanes[i]],k[i])) return false;
    std::uint8_t id=0;
    for(unsigned j=0;j<2;++j) {if(!byte(0x2349980+std::size_t(k[0])*2+j,id)) return false;if(!id) break;ids[count++]=id;}
    if(!byte(0x23499E0+std::size_t(k[1]),id)) return false;
    if(id) ids[count++]=id;
    for(unsigned j=0;j<2;++j) {if(!byte(0x2349A10+std::size_t(k[2])*2+j,id)) return false;if(!id) break;ids[count++]=id;}
    if(!byte(0x2349A08+std::size_t(k[3]),id)) return false;
    if(id) ids[count++]=id;
    if(!byte(0x2349A68+std::size_t(k[4]),id)) return false;
    if(id) ids[count++]=id;
    return true;
}
// The stock cache (DC3700's 48 entries): info 1D8 stride 90 (+5E id, +78 type
// low 5 bits, +7A receiver), records 1E0 stride 38 (+34 flags: 1 used, 2
// protected), refs 1E8 u32.
struct CacheEntry {
    std::uint16_t equip=0;std::uint8_t type=0,receiver=0,flags=0;std::uint32_t refs=0;
};
inline constexpr unsigned kCacheEntries=48;
template<class Reader> bool Census(View<Reader>& v,std::uintptr_t engine,std::array<CacheEntry,kCacheEntries>& out) noexcept {
    out={};std::uintptr_t info_rows=0,cache_rows=0,ref_rows=0;
    if(!v.At(engine,0x1D8,info_rows) || !info_rows || !v.At(engine,0x1E0,cache_rows) || !cache_rows ||
       !v.At(engine,0x1E8,ref_rows) || !ref_rows) return false;
    for(unsigned i=0;i<kCacheEntries;++i) {
        auto& e=out[i];std::uint8_t type=0;
        if(!v.At(info_rows,i*0x90+0x5E,e.equip) || !v.At(info_rows,i*0x90+0x78,type) || !v.At(info_rows,i*0x90+0x7A,e.receiver) ||
           !v.At(cache_rows,i*0x38+0x34,e.flags) || !v.At(ref_rows,i*4,e.refs)) return false;
        e.type=type&0x1F;
    }
    return true;
}
// DC3755..DC376A: the first used entry of the id (48 when none).
inline unsigned FirstEntry(const std::array<CacheEntry,kCacheEntries>& census,std::uint16_t equip) noexcept {
    for(unsigned i=0;i<kCacheEntries;++i) if((census[i].flags&1) && census[i].equip==equip) return i;
    return kCacheEntries;
}
// Tier A1/C admission of a resident entry: used and protected (flags 3, the
// stock46 pattern), no reference (never shared with the local player).
inline bool ResidentProtected(const CacheEntry& e) noexcept {return e.flags==3 && e.refs==0;}
// --- Tier C: the substitute search (docs/native-puppet.md "Generalized writer").
// The type a slot's substitute must have: the manifest's own type of the slot
// (version 2), else the want's table type, when it is a gun type of this slot;
// 0 = only the slot class (ChangeEquip's TypeSlot: 0 {1,5,7}, 1 {4,6,8}, 2 {2,3}).
constexpr std::uint8_t SubstituteType(std::uint8_t manifest_type,std::uint8_t want_type,unsigned slot) noexcept {
    return manifest_type>=1 && manifest_type<=8 && TypeSlot(manifest_type)==slot ? manifest_type :
           want_type>=1 && want_type<=8 && TypeSlot(want_type)==slot ? want_type : std::uint8_t(0);
}
// Search order: the same type with the manifest's receiver, the same type, the
// rest of the slot class (the first two need a type).
enum class SubstitutePass : std::uint8_t {type_receiver,type,slot_class};
inline const char* SubstitutePassName(SubstitutePass pass) noexcept {
    return pass==SubstitutePass::type_receiver ? "type_receiver" : pass==SubstitutePass::type ? "type" : "slot_class";
}
struct SubstituteQuery {
    unsigned slot=0;
    std::uint8_t type=0,receiver=0;           // SubstituteType (0: slot class only), the manifest's receiver (0 unknown)
    std::uint16_t want=0;                     // the manifest's base id (never its own substitute)
    std::array<std::uint16_t,6> viewer{};     // the viewer's own vars.weapons (never shared with the local player)
    std::array<std::uint16_t,3> dispatched{}; // what this allocation already dispatched to other slots
};
// The next census entry of a pass, the lowest id first, not yet tried: used,
// protected and unreferenced (flags 3, refs 0: an n3 entry such as the
// soldiers' rifle never qualifies), the id's first used entry (DC3755), a stock
// id below 0x230 (never an alias or blueprint, never the viewer's 871..876),
// of the pass's type. kCacheEntries when none.
inline unsigned NextCandidate(const std::array<CacheEntry,kCacheEntries>& census,const SubstituteQuery& q,SubstitutePass pass,
                              const std::array<bool,kCacheEntries>& tried) noexcept {
    if(pass!=SubstitutePass::slot_class && !q.type) return kCacheEntries;
    if(pass==SubstitutePass::type_receiver && !q.receiver) return kCacheEntries;
    unsigned best=kCacheEntries;
    for(unsigned i=0;i<kCacheEntries;++i) {
        const auto& e=census[i];
        if(tried[i] || !ResidentProtected(e) || !e.equip || e.equip>=0x230 || FirstEntry(census,e.equip)!=i) continue;
        const bool type_ok=pass==SubstitutePass::slot_class ? TypeSlot(e.type)==q.slot && e.type!=q.type : e.type==q.type;
        if(!type_ok || (pass==SubstitutePass::type_receiver && e.receiver!=q.receiver)) continue;
        if(best==kCacheEntries || e.equip<census[best].equip) best=i;
    }
    return best;
}
// The explicit exclusions of an otherwise admitted entry (logged as candidate
// lines): the want itself, the viewer's own weapon, an equip this allocation
// already dispatched. nullptr: on to Derive and the full Capture.
inline const char* CandidateExcluded(const CacheEntry& e,const SubstituteQuery& q) noexcept {
    if(e.equip==q.want) return "want";
    for(auto id:q.viewer) if(id && id==e.equip) return "viewer_weapon";
    for(auto id:q.dispatched) if(id && id==e.equip) return "dispatched";
    return nullptr;
}
// A stock gun's target from the type table (A2A3E0/A2A520: packed & 0x3F /
// packed >> 6 for ids below 0x230), GunBasic, the package row, the dependency
// LUTs, the optional descriptor and the cache census. Pure reads; refuses
// blueprints/aliases (ids 0x230 and up), non-gun types and anything unread.
template<class Reader> const char* Derive(View<Reader>& v,std::uintptr_t engine,std::uint16_t equip,
        const std::array<CacheEntry,kCacheEntries>& census,Target& t) noexcept {
    t=Target{};t.exact_parsed=false;t.require_remote_empty=false;t.equip=equip;
    if(!equip || equip>=0x230) return "equip_range";
    std::uintptr_t types=0,resource=0,basic=0;
    if(!v.At(engine,8,types) || types!=v.base+0x2A70920 || !v.Method(types,0x22AD180,0,0xA2A3E0) ||
       !v.At(types,8+std::size_t(equip)*2,t.packed)) return "type_table";
    t.type=static_cast<std::uint8_t>(t.packed&0x3F);t.row=static_cast<std::uint16_t>(t.packed>>6);
    if(t.type<1 || t.type>8) return "type";
    t.slot=TypeSlot(t.type);
    if(!t.row || t.row>0x1818/12) return "row";
    if(!v.At(engine,0x10,resource) || resource!=v.base+0x2A711F0 || !v.At(resource,8,basic) || basic!=v.base+0x2C25C50 ||
       !v.At(basic,(std::size_t(t.row)-1)*12,t.recipe)) return "recipe";
    std::array<std::uint64_t,3> row{};
    if(!v.Image(PackageRowRva(equip),24) || !v.At(v.base,PackageRowRva(equip),row)) return "package_row";
    t.parts=row[0];t.package=row[1];t.package_flags=row[2];
    if(!t.parts || !t.package || t.package_flags!=2) return "package_row";
    if(((t.parts>>0x33)&0x1FFF)==0x1094) return "parts_class";
    std::array<std::uint8_t,7> ids{};unsigned count=0;
    if(!DependencyIds(v,t.recipe,ids,count)) return "dependency_tables";
    t.hashes={};t.hashes[0]=t.package;t.hash_count=1;
    for(unsigned i=0;i<count;++i) {
        std::uint64_t hash=0;
        if(!v.Image(0x2349348+std::size_t(ids[i])*16,8) || !v.At(v.base,0x2349348+std::size_t(ids[i])*16,hash) || !hash)
            return "dependency_hash";
        bool seen=false;
        for(unsigned j=0;j<t.hash_count;++j) seen=seen || t.hashes[j]==hash;
        if(!seen) {if(t.hash_count>=kMaxPackages) return "dependency_count";t.hashes[t.hash_count++]=hash;}
    }
    t.optional=false;t.optional_hash=0;
    if(equip<=0x7C) {
        if(!v.At(v.base+0x2A6B400,8+std::size_t(equip)*8,t.optional_hash)) return "optional_descriptor";
        t.optional=t.optional_hash!=0;
    }
    const unsigned first=FirstEntry(census,equip);
    if(first>=kCacheEntries) return "no_cache_entry";
    t.cache_index=first;
    // stock46 keeps its measured parsed counts.
    t.exact_parsed=equip==kStock46.equip && t.packed==kStock46.packed && t.recipe==kStock46.recipe &&
        t.parts==kStock46.parts && t.package==kStock46.package && t.hashes==kStock46.hashes &&
        t.hash_count==kStock46.hash_count && t.optional && t.optional_hash==kStock46.optional_hash;
    return nullptr;
}
}
