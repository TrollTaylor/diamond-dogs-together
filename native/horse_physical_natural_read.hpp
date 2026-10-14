#pragma once
// Separate READONLY native Entity-dispatch observer. No legacy Lua Scope
// changes, engine calls, profile mutation, cargo binding or writer authority.
#include "horse_physical_callback_probe.hpp"
#include "horse_physical_natural_contract.hpp"
namespace mgscoop::horse_physical_natural_read {
namespace cr=carry_plugin_read;
namespace hp=horse_player_physical_read;
namespace pr=horse_physical_profile_read;
using RawWindow=horse_physical_callback_probe::RawWindow;
struct Published {
    std::uintptr_t image=0,player=0,data=0,state=0;
    std::uintptr_t blocks[2]{},ps[2]{};hp::Body binding[2]{};
    std::uint32_t mission=0,generation=0,luaOwnerThread=0;std::uint64_t birth[2]{};
    std::uint16_t epoch=0;std::uint64_t at=0,serial=0;
    bool admitted=false,exactPins=false;
};
struct Windows {std::uint32_t before=0,after=0;bool batch=false;};
struct Row {
    std::uint16_t body=0xffff,mission=0;std::uint32_t actualThread=0,luaOwnerThread=0,life[2]{},age=UINT32_MAX;
    std::uint8_t parts[2]{},pools[2]{};std::uint16_t phase=0,action[2]{};Windows input{};
    hp::Body binding[2]{};pr::Profile profile{};
    bool ready=false,contextMatches=false,inputEvenStable=false;
    const char* reason="not_sampled";
};
template<class Reader> bool CodeGate(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch) noexcept {
    if(!hp::CodeGate(r,image,span,mismatch)||!pr::CodeGate(r,image,span,mismatch))return false;
    unsigned char bytes[256]{};
    for(const auto& site:horse_physical_natural_contract::sites){
        if(site.rva>span||site.size>span-site.rva||site.size>sizeof(bytes)||!r(image+site.rva,bytes,site.size)){mismatch=site.rva;return false;}
        for(std::size_t k=0;k<site.size;++k)if(bytes[k]!=site.bytes[k]){mismatch=site.rva;return false;}}
    return true;
}
inline bool BindingIdentity(const hp::Body& a,const hp::Body& b) noexcept {
    return a.cs==b.cs&&a.entry==b.entry&&a.entity==b.entity&&a.components==b.components&&a.component==b.component&&
      a.physicalInterface==b.physicalInterface&&a.transform==b.transform&&a.ownerTransform==b.ownerTransform&&
      a.physicalArray==b.physicalArray&&a.componentCount==b.componentCount&&a.tag==b.tag&&a.componentType==b.componentType;
}
// This is a current-layout read keyed by a trusted, fresh published pair.
// It intentionally has no Lua/native thread equality predicate: it observes
// the exact engine callback thread and reports it separately, without treating
// that thread as the Lua owner or borrowing any carried/sleep write window.
template<class Reader> bool ReadCurrentLayout(Reader& r,const Published& p,Row& out) noexcept {
    const auto fail=[&](const char* why) noexcept {out.reason=why;return false;};
    std::uintptr_t current=0,vt=0,q=0,s=0,sync=0,ids=0,pool=0,parts=0,blocks=0,table=0,actions=0;
    std::uint32_t count=0,flags=0,local=0;
    if(!cr::Get(r,p.image+0x2a82c30,current)||current!=p.player||!cr::Get(r,p.image+0x2c01372,out.mission)||out.mission!=p.mission||
       !cr::Get(r,p.player,vt)||vt!=p.image+0x2295490||!cr::Get(r,p.player+0x60,q)||q!=p.data||!cr::Get(r,q,vt)||vt!=p.image+0x23ad970||
       !cr::Get(r,q+0x1e8,s)||s!=p.state||!cr::Get(r,s+0x40,count)||count!=2||!cr::Get(r,p.player+0xa8,sync)||sync||
       !cr::Get(r,p.player+0x204,flags)||!(flags&1u)||(flags&0x100u)||!cr::Get(r,p.player+0x208,count)||count!=2||
       !cr::Get(r,p.player+0x20c,count)||count!=2||!cr::Get(r,p.player+0x214,local)||local)return fail("native_pair");
    if(!cr::Get(r,q+0x18,actions)||!cr::Aligned(actions,2)||!cr::Range(actions,4)||
       !cr::Get(r,p.player+0x70,ids)||!cr::Aligned(ids,2)||!cr::Get(r,p.player+0x78,pool)||!cr::Range(pool,2)||
       !cr::Get(r,p.player+0x90,parts)||!cr::Range(parts,2)||!cr::Get(r,p.player+0x378,blocks)||!cr::Aligned(blocks,8)||
       !cr::Range(blocks,16*0x6a8)||!cr::Get(r,p.player+0x50,table)||!cr::Aligned(table,8)||!cr::Range(table,16*8))return fail("native_arrays");
    for(unsigned id=0;id<2;++id){auto& b=out.binding[id];std::uint16_t actual=0;std::uint32_t one=0,base=0;std::uintptr_t ptr=0,block=0,ps=0,life=0;
        if(!cr::Get(r,actions+id*2,out.action[id])||!cr::Get(r,ids+id*2,actual)||actual!=id||!cr::Get(r,pool+id,out.pools[id])||out.pools[id]>=16||
           !cr::Get(r,parts+id,out.parts[id])||out.parts[id]!=3)return fail("native_instances");
        block=blocks+std::size_t(out.pools[id])*0x6a8;
        if(block!=p.blocks[id]||!cr::Get(r,table+out.pools[id]*8,ptr)||ptr!=block||!cr::Get(r,block,vt)||vt!=p.image+0x2295210||
           !cr::Get(r,block+0x138,ptr)||ptr!=p.player||!cr::Get(r,block+8,one)||one!=1||!cr::Get(r,block+0xc,base)||base!=id||
           !cr::Get(r,block+0x78,ps)||ps!=p.ps[id]||!cr::Aligned(ps,8)||!cr::Get(r,ps,vt)||(vt!=p.image+0x23b09f0&&vt!=p.image+0x23b0c70)||
           !cr::Get(r,ps+0x408,ptr)||ptr!=block||!cr::Get(r,ps+8,base)||base!=id||!cr::Get(r,ps+0x280,life)||!cr::Aligned(life,4)||
           !cr::Get(r,life,out.life[id]))return fail("native_body_life");
        if(!cr::Get(r,ps+0x238,b.cs)||!cr::Aligned(b.cs,8)||!cr::Get(r,b.cs,vt)||vt!=p.image+0x22e6070||
           !cr::Get(r,block+0x10,ptr)||ptr!=b.cs||!cr::Get(r,b.cs+8,one)||one!=1||!cr::Get(r,b.cs+0xc,base)||base!=id||
           !cr::Get(r,b.cs+0x10,b.entry)||!cr::Aligned(b.entry,16)||!cr::Range(b.entry,0xc0)||
           !cr::Get(r,b.entry+0x60,b.transform)||!cr::Aligned(b.transform,16)||!cr::Get(r,b.entry+0x70,b.entity)||
           !cr::Aligned(b.entity,8)||!cr::Range(b.entity,0x38))return fail("native_model");
        if(!cr::Get(r,b.entity+0x28,b.componentCount)||!b.componentCount||b.componentCount>128||!cr::Get(r,b.entity+0x30,b.components)||
           !cr::Aligned(b.components,8)||!cr::Range(b.components,std::size_t(b.componentCount)*8))return fail("native_membership");
        for(unsigned k=0;k<b.componentCount;++k){std::uintptr_t component=0;std::uint32_t tag=0;
            if(!cr::Get(r,b.components+k*8,component)||!cr::Aligned(component,8)||!cr::Get(r,component+0x10,tag))return fail("native_membership");
            if(tag==0x7f1402f4){if(b.component)return fail("native_tag_alias");b.component=component;b.tag=tag;}}
        if(!b.component||!cr::Range(b.component,0xf0)||!cr::Get(r,b.component,vt)||vt!=p.image+0x24c97f8)return fail("native_physical_type");
        b.componentType=0x24c97f8;b.physicalInterface=b.component+0x80;
        if(!cr::Get(r,b.physicalInterface,vt)||vt!=p.image+0x24c9830||!cr::Get(r,b.component+0x68,b.ownerTransform)||b.ownerTransform!=b.transform||
           !cr::Get(r,ps+0x270,b.physicalArray)||!cr::Aligned(b.physicalArray,8)||!cr::Get(r,b.physicalArray,ptr)||ptr!=b.physicalInterface||
           !cr::Get(r,b.component+0xd0,b.flags)||!r(b.physicalInterface+0x10,b.matrix,sizeof(b.matrix)))return fail("native_physical_owner");
        for(float value:b.matrix)if(!std::isfinite(value)||std::fabs(value)>200000.0f)return fail("native_matrix");
        if(!BindingIdentity(b,p.binding[id]))return fail("published_binding_changed");
    }
    const auto& a=out.binding[0];const auto& b=out.binding[1];
    if(out.pools[0]==out.pools[1]||p.blocks[0]==p.blocks[1]||p.ps[0]==p.ps[1]||a.cs==b.cs||a.entry==b.entry||a.entity==b.entity||
       a.component==b.component||a.transform==b.transform||a.physicalArray==b.physicalArray||a.components==b.components)return fail("native_pair_alias");
    return true;
}
template<class Reader> bool Observe(Reader& r,const Published& p,const RawWindow& raw,std::uint64_t now,const Windows& input,Row& out) noexcept {
    out={};out.actualThread=raw.thread;out.luaOwnerThread=p.luaOwnerThread;out.input=input;
    out.inputEvenStable=!(input.before&1u)&&input.before==input.after&&!input.batch;
    const auto fail=[&](const char* why) noexcept {out.reason=why;return false;};
    if(!p.admitted||!p.exactPins||!p.image||!p.player||!p.data||!p.state||!p.serial||!p.luaOwnerThread||!p.birth[0]||!p.birth[1]||
       !p.epoch||!p.generation||(p.mission!=30010&&p.mission!=30020&&p.mission!=30050))return fail("published_scope");
    if(!p.at||now<p.at||now-p.at>750)return fail("published_stale");out.age=static_cast<std::uint32_t>(now-p.at);
    if(raw.caller!=p.image+horse_physical_natural_contract::callerReturn)return fail("native_caller");
    if(!raw.thread||!raw.self||!raw.poseContext)return fail("native_arguments");
    if(raw.self==p.binding[0].component)out.body=0;
    else if(raw.self==p.binding[1].component)out.body=1;
    else return fail("native_foreign_self");
    out.contextMatches=raw.poseContext==p.binding[out.body].entity;
    if(!out.contextMatches)return fail("native_entity_context");
    Row first=out,second=out;
    if(!ReadCurrentLayout(r,p,first)){out=first;return false;}
    if(!ReadCurrentLayout(r,p,second)){out=second;return false;}
    if(!hp::SameBody(first.binding[0],second.binding[0])||!hp::SameBody(first.binding[1],second.binding[1])||
       first.life[0]!=second.life[0]||first.life[1]!=second.life[1]||first.action[0]!=second.action[0]||first.action[1]!=second.action[1]||
       first.pools[0]!=second.pools[0]||first.pools[1]!=second.pools[1])return fail("native_pair_changed");
    out=second;if(!pr::Observe(r,p.image,out.binding[out.body],out.profile)||!cr::Get(r,raw.self+0x3c,out.phase))return fail("native_profile");
    out.ready=true;out.reason="native_entity_observed";return true;
}
inline bool Line(const Row& row,char* out,std::size_t capacity) noexcept {
    if(!out||!capacity)return false;
    const int n=std::snprintf(out,capacity,"physNative;r=%u;b=%u;t=%u,%u;age=%u;ctx=%u;w=%u,%u,%u;stable=%u;why=%s",
      row.ready?1u:0u,unsigned(row.body),row.actualThread,row.luaOwnerThread,row.age,row.contextMatches?1u:0u,
      row.input.before,row.input.after,row.input.batch?1u:0u,row.inputEvenStable?1u:0u,row.reason);
    return n>=0&&static_cast<std::size_t>(n)<capacity;
}
inline bool LifeLine(const Row& row,char* out,std::size_t capacity) noexcept {
    if(!out||!capacity)return false;
    const int n=std::snprintf(out,capacity,"physNativeLife;r=%u;b=%u;pool=%u,%u;p=%u,%u;q=%x,%x;life=%x,%x;prof=%u,%u,%x;phase=%u",
      row.ready?1u:0u,unsigned(row.body),unsigned(row.pools[0]),unsigned(row.pools[1]),unsigned(row.parts[0]),unsigned(row.parts[1]),
      unsigned(row.action[0]),unsigned(row.action[1]),row.life[0],row.life[1],row.profile.profileCount,row.profile.mode,row.profile.flags,unsigned(row.phase));
    return n>=0&&static_cast<std::size_t>(n)<capacity;
}
}
