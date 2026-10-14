// Detached ABI/fallback tests: fake original methods only, no game or hooks.
#include "sleep_unique_bind_adapter.hpp"
#include <iostream>
#include <cstdlib>
namespace {
namespace an=mgscoop::sleep_unique_bind_native;
unsigned checks=0,commonCalls=0,appendCalls=0,reports=0;
std::uintptr_t recordSeen=0,engineSeen=0,bankSeen=0;std::uint32_t actorSeen=0;
void Check(bool ok,const char* why){++checks;if(!ok){std::cerr<<"FAIL "<<why<<'\n';std::exit(1);}}
bool Append(std::uintptr_t engine,std::uintptr_t bank){++appendCalls;engineSeen=engine;bankSeen=bank;return bank==222;}
void Common(std::uintptr_t record,std::uint32_t actor){++commonCalls;recordSeen=record;actorSeen=actor;an::AddMtarHook(111,222);}
bool Read(void*,std::uintptr_t,void*,std::size_t)noexcept{return false;}
bool Hash(void*,const void*,std::size_t,char*)noexcept{return false;}
bool Scope(void*,const mgscoop::sleep_unique_bind::Event&,mgscoop::sleep_unique_bind::Scope&)noexcept{return false;}
void Report(void*,const mgscoop::sleep_unique_bind::Event&,const mgscoop::sleep_unique_bind::Reply&)noexcept{++reports;}
}
int main(){
 an::Configuration c{};c.image=0x140000000;c.originalCommon=Common;c.originalAppend=Append;
 c.read=Read;c.hash=Hash;c.scope=Scope;c.report=Report;
 Check(!an::Configure(nullptr),"missing_configuration_refused");
 Check(!an::Configure(&c),"unadmitted_installation_refused");
 c.privateInstallationAdmitted=true;
#if !defined(MGS_BACKGROUND_WORKER)
 Check(!an::Configure(&c),"release_no_private_adapter");
#else
 c.observeOnly=false;Check(!an::Configure(&c),"append_compiled_off");c.observeOnly=true;
 Check(an::Configure(&c),"exact_readonly_configuration");
 Check(!an::Configure(&c),"second_configuration_refused");
 an::CommonBindHook(333,1);Check(commonCalls==1&&recordSeen==333&&actorSeen==1,"original_common_ABI_forwarding");
 Check(appendCalls==1&&engineSeen==111&&bankSeen==222&&!reports,"foreign_caller_preserves_normal_append_without_observation");
 Check(!an::AddMtarHook(444,555)&&appendCalls==2,"original_append_bool_result_preserved");
 an::Unconfigure();an::CommonBindHook(666,0);
 Check(commonCalls==2&&recordSeen==666&&actorSeen==0&&appendCalls==3,"quiescent_unconfigure_retains_original_fallback");
 Check(!an::AddMtarHook(777,888)&&appendCalls==4&&!reports,"fallback_append_preserves_arguments_and_result");
#endif
 std::cout<<"sleep_unique_bind_wrapper_tests: "<<checks<<" synthetic checks passed; no game operations\n";
}
