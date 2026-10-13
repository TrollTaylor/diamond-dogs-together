#pragma once
#include "player_tree_closure_contract.hpp"
#include "player_op2_tree_read.hpp"
#include "player_op2_null_read.hpp"
#include "player_row4_inactive_read.hpp"
namespace mgscoop::player_tree_closure {
namespace tree=player_op2_tree;namespace nullRecord=player_op2_null;namespace row4=player_row4_inactive;namespace pins=player_tree_closure_contract;
enum class Operation:std::uint8_t{carrierReply=1,recipientOp2=2};
enum class Effect:std::uint8_t{unqualified,noOp,hashOrOpReject,cookieCheckedReject,row4MinusOne,row12NullReject,retainedCarrierReplyRequired};
struct Pair{std::uint32_t phase,handler;};
inline constexpr Pair pairs[]={
 {0x23b0e50,0xff4180},
 {0x23b1590,0x645940},
 {0x23b3fa0,0x645940},
 {0x23b5090,0x10a1420},
 {0x23b5aa0,0x645940},
 {0x23b60f0,0x645940},
 {0x23b6e80,0x10e0440},
 {0x23b8dd0,0x11268b0},
 {0x23b9b50,0x11301b0},
 {0x23ba180,0x645940},
 {0x23ba820,0x645940},
 {0x23bb2c0,0x645940},
 {0x23bb9b0,0x645940},
 {0x23bc170,0x645940},
 {0x23bcb20,0x1160c20},
 {0x23bdae0,0x117a820},
 {0x23be070,0x645940},
 {0x23bee10,0x118d080},
 {0x23bf950,0x11965d0},
 {0x23bff00,0x645940},
 {0x23c04e0,0x645940},
 {0x23c0bf0,0x645940},
 {0x23c20c0,0x645940},
 {0x23c2690,0x645940},
 {0x23c2ca0,0x645940},
 {0x23c3890,0x11c7360},
 {0x23c4430,0x645940},
 {0x23c5ac0,0x645940},
 {0x23c7c30,0x645940},
 {0x23c9640,0x120e4c0},
 {0x23c9d20,0x645940},
 {0x23cadc0,0x121f160},
 {0x23cb6b0,0x12300f0},
 {0x23cbf10,0x124ae80},
 {0x23cc840,0x125f7e0},
 {0x23cd1d0,0x645940},
 {0x23cd7f0,0x1279e90},
 {0x23cfa70,0x645940},
 {0x23d0000,0x1288cc0},
 {0x23d0d70,0x128e4a0},
 {0x23d25f0,0x645940},
 {0x23d3f60,0x645940},
 {0x23d55f0,0x12bf120},
 {0x23d5c70,0x645940},
 {0x23d7540,0x645940},
 {0x2454ff0,0x645940},
 {0x2455650,0x645940},
 {0x2455d50,0x645940},
 {0x2456340,0x17979d0},
 {0x2457910,0x645940}
};
struct ClassifiedNode{std::uint32_t row=0,phaseVtRva=0,handlerRva=0;Effect effect=Effect::unqualified;};
struct Observation{
 tree::Observation tree{};row4::Observation inactive{};nullRecord::Observation null{};
 ClassifiedNode nodes[tree::capacity]{};unsigned count=0;
 Operation operation=Operation::carrierReply;
 bool needsRow4Lifetime=false,needsNullWitness=false,needsRetainedCarrierReply=false;
};
inline bool Supported(Operation op,unsigned actualId)noexcept{
 return (op==Operation::carrierReply&&actualId==0)||(op==Operation::recipientOp2&&actualId==1);
}
inline Effect ClassifyNode(const tree::Node& n,Operation op)noexcept{
 if((op!=Operation::carrierReply&&op!=Operation::recipientOp2)||n.row>=tree::capacity)return Effect::unqualified;
 bool exact=false;for(const auto& p:pairs)if(n.phaseVtRva==p.phase&&n.handlerRva==p.handler){exact=true;break;}
 if(!exact)return Effect::unqualified;
 if(n.handlerRva==0x1279e90)return n.row==4?Effect::row4MinusOne:Effect::unqualified;
 if(n.handlerRva==0x1288cc0)return n.row!=0x12?Effect::unqualified:(op==Operation::recipientOp2?Effect::row12NullReject:Effect::hashOrOpReject);
 if(n.handlerRva==0x11301b0)return n.row!=0x32?Effect::unqualified:(op==Operation::carrierReply?Effect::retainedCarrierReplyRequired:Effect::hashOrOpReject);
 if(n.handlerRva==0x645940)return Effect::noOp;
 if(n.handlerRva==0xff4180)return n.row==1?Effect::cookieCheckedReject:Effect::unqualified;
 if(n.handlerRva==0x1160c20||n.handlerRva==0x118d080)return Effect::cookieCheckedReject;
 switch(n.handlerRva){
 case 0x10a1420:case 0x10e0440:case 0x11268b0:case 0x117a820:case 0x11965d0:case 0x11c7360:case 0x120e4c0:
 case 0x121f160:case 0x12300f0:case 0x124ae80:case 0x125f7e0:case 0x128e4a0:case 0x12bf120:case 0x17979d0:return Effect::hashOrOpReject;
 default:return Effect::unqualified;
 }
}
inline bool Same(const Observation& a,const Observation& b)noexcept{
 if(a.count>tree::capacity||b.count>tree::capacity||a.tree.count>tree::capacity||b.tree.count>tree::capacity||a.count!=a.tree.count||b.count!=b.tree.count||a.operation!=b.operation||a.count!=b.count||a.needsRow4Lifetime!=b.needsRow4Lifetime||a.needsNullWitness!=b.needsNullWitness||
 a.needsRetainedCarrierReply!=b.needsRetainedCarrierReply||!tree::Same(a.tree,b.tree)||!row4::Same(a.inactive,b.inactive)||!nullRecord::Same(a.null,b.null))return false;
 for(unsigned i=0;i<a.count;++i){const auto& x=a.nodes[i];const auto& y=b.nodes[i];
  if(x.row!=y.row||x.phaseVtRva!=y.phaseVtRva||x.handlerRva!=y.handlerRva||x.effect!=y.effect)return false;}return true;
}
// Read-only path/effect facts. Success is not native dispatch permission:
// the genuine scoped origin provider, retained carrier reply wrapper and the
// external row4 client/plain-cell lifecycle capability remain separate duties.
// No caller-supplied "scope ready" booleans or native callbacks are accepted.
template<class R>bool ReadOnce(R& reader,std::uintptr_t image,std::uintptr_t player,std::uintptr_t block,Operation op,Observation& out,const char*& why)noexcept{
 out={};const auto fail=[&](const char* text)noexcept{why=text;return false;};const unsigned actualId=op==Operation::carrierReply?0u:1u;
 if(!Supported(op,actualId))return fail("operation");
 Observation q{};q.operation=op;tree::Observation late{};
 if(!tree::ReadOnce(reader,image,player,block,actualId,q.tree,why))return false;
 for(unsigned i=0;i<q.tree.count;++i){const auto& n=q.tree.nodes[i];const auto effect=ClassifyNode(n,op);if(effect==Effect::unqualified)return fail("unknown_registered_pair");
  q.nodes[q.count++]={n.row,n.phaseVtRva,n.handlerRva,effect};
  if(effect==Effect::row4MinusOne){
   if(q.needsRow4Lifetime||!row4::ReadOnce(reader,image,player,block,actualId,q.inactive,why))return false;
   if(q.inactive.state!=q.tree.state||q.inactive.node!=n.node||q.inactive.primary+0x30!=n.phase)return fail("row4_tree_mismatch");q.needsRow4Lifetime=true;
  }
  if(effect==Effect::row12NullReject){
   if(q.needsNullWitness||!nullRecord::ReadOnce(reader,image,player,block,actualId,q.null,why))return false;
   if(q.null.state!=q.tree.state||q.null.node!=n.node||q.null.primary+0x30!=n.phase)return fail("null_tree_mismatch");q.needsNullWitness=true;
  }
  if(effect==Effect::retainedCarrierReplyRequired)q.needsRetainedCarrierReply=true;
 }
 if(op==Operation::carrierReply&&!q.needsRetainedCarrierReply)return fail("missing_carrier_reply");
 if(!tree::ReadOnce(reader,image,player,block,actualId,late,why)||!tree::Same(q.tree,late))return fail("tree_changed_after_witness");
 out=q;why="readonly_effect_facts";return true;
}
template<class DataR,class CodeR>bool ReadPair(DataR& data,CodeR& code,std::uintptr_t image,std::size_t span,std::uintptr_t player,std::uintptr_t block,Operation op,Observation& out,const char*& why,std::uintptr_t& mismatch)noexcept{
 out={};if(!pins::CodeGate(code,data,image,span,mismatch)){why="exact_code_or_slot_changed";return false;}
 Observation a{},b{};if(!ReadOnce(data,image,player,block,op,a,why)||!ReadOnce(data,image,player,block,op,b,why))return false;
 if(!Same(a,b)){why="composed_pair_changed";return false;}
 if(!pins::CodeGate(code,data,image,span,mismatch)){why="late_code_or_slot_changed";return false;}
 out=a;return true;
}
} // namespace mgscoop::player_tree_closure
