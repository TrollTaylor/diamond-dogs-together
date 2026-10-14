// Detached manifest/context fixtures. No file, Win32, hooks or game operations.
#include "worker_protocol.hpp"
#include <iostream>
#include <cstdlib>
#include <cstring>
namespace {
namespace ur=mgscoop::mgo_unique_rest;
unsigned checks=0;void Check(bool ok,const char* why){++checks;if(!ok){std::cerr<<"FAIL "<<why<<'\n';std::exit(1);}}
std::string Base(){
 return "schema=1\nmode=mgsv\nrun_id=0123456789abcdef0123456789abcdef\nrun_dir=D:\\runs\\0123456789abcdef0123456789abcdef\ndesktop=MGSWorker_0123456789abcdef0123456789abcdef\ntimeout_ms=60000\nready_timeout_ms=30000\nexe_sha256="+
  std::string(mgsworker::kRetailExeSha)+"\nsteam_sha256="+mgsworker::kRetailSteamSha+
  "\nloader_sha256="+std::string(64,'a')+"\nruntime_archive_sha256="+ur::archive_sha+
  "\nruntime_motion_count=21\nruntime_profile=mgo_unique_rest_v1\nruntime_buddy_capacity=0\nruntime_archive_bytes=16360448\nruntime_catalog_sha256="+ur::catalog_sha+
  "\nruntime_build_profile=mgo-unique-rest\nruntime_native_stage=gun\nlab_mode=lab\nprobe_receipt=D:\\runs\\qualification.txt\nprobe_receipt_sha256="+std::string(64,'b')+
  "\noffline_master_port=4242\nhost_exe=D:\\host\\mgsvtpp.exe\nhost_save_dir=D:\\runs\\0123456789abcdef0123456789abcdef\\host-save\nguest_exe=D:\\guest\\mgsvtpp.exe\nguest_save_dir=D:\\runs\\0123456789abcdef0123456789abcdef\\guest-save\n"+
  "host_lab_sha256="+std::string(64,'c')+"\nhost_coop_sha256="+std::string(64,'c')+"\nhost_graphics_sha256="+std::string(64,'c')+"\nguest_lab_sha256="+std::string(64,'c')+"\nguest_coop_sha256="+std::string(64,'c')+"\nguest_graphics_sha256="+std::string(64,'c')+"\n";
}
std::string Replaced(std::string text,const char* from,const char* to){const auto at=text.find(from);Check(at!=std::string::npos,"fixture_anchor");text.replace(at,std::strlen(from),to);return text;}
}
int main(){
 mgsworker::Manifest m{};std::string why;const auto valid=Base();
 Check(mgsworker::ParseManifest(valid,m,why)==ur::compiled,"only_explicit_private_compilation_accepts_profile");
 Check(!mgsworker::ParseManifest(Replaced(valid,"lab_mode=lab","lab_mode=play"),m,why),"play_refused");
 Check(!mgsworker::ParseManifest(Replaced(valid,"runtime_build_profile=mgo-unique-rest","runtime_build_profile=research"),m,why),"generic_research_profile_refused");
 Check(!mgsworker::ParseManifest(Replaced(valid,"runtime_native_stage=gun","runtime_native_stage=loco"),m,why),"wrong_native_stage_refused");
 Check(!mgsworker::ParseManifest(Replaced(valid,"runtime_archive_bytes=16360448","runtime_archive_bytes=26949632"),m,why),"cached_resident_archive_size_refused");
 Check(!mgsworker::ParseManifest(Replaced(valid,"runtime_buddy_capacity=0","runtime_buddy_capacity=2"),m,why),"mixed_buddy_catalog_refused");
 Check(!mgsworker::ParseManifest(Replaced(valid,"runtime_profile=mgo_unique_rest_v1","runtime_profile=mgo_sleep_rest_v1"),m,why),"old_profile_new_hash_refused");
 Check(!mgsworker::ParseManifest(Replaced(valid,"runtime_motion_count=21","runtime_motion_count=2"),m,why),"direct_motion_count_not_native_bank_count");
 Check(!mgsworker::ParseManifest(Replaced(valid,ur::archive_sha,mgsworker::kBaselineArchiveSha),m,why),"mixed_baseline_archive_refused");
 Check(!mgsworker::ParseManifest(Replaced(valid,"guest_exe=D:\\guest\\mgsvtpp.exe","guest_exe=D:\\host\\mgsvtpp.exe"),m,why),"role_roots_must_remain_distinct");
 ur::ContextPins p{L"0123456789abcdef0123456789abcdef",L"host",L"MGSWorker_0123456789abcdef0123456789abcdef",L"D:\\host",L"D:\\host",
  L"mgo_unique_rest_v1",L"92e935bced30dcd7a1eda498462a375a8594241a1cfa7228026e3e346e886c25",L"cd23aa3146823af8577079009ed54cfa6f1508f2ae2e7d55e8a0b55dab46ad51",L"16360448",L"21",L"0",L"lab",L"mgo-unique-rest",L"gun"};
 Check(ur::ContextMatches(p)==ur::compiled,"exact_native_context");
 auto bad=p;bad.role=L"other";Check(!ur::ContextMatches(bad),"unknown_role");
 bad=p;bad.game=L"D:\\guest";Check(!ur::ContextMatches(bad),"role_game_pair");
 bad=p;bad.desktop=L"Default";Check(!ur::ContextMatches(bad),"visible_desktop");
 bad=p;bad.build_profile=L"normal";Check(!ur::ContextMatches(bad),"normal_build");
 bad=p;bad.profile=L"mgo_sleep_rest_v1";Check(!ur::ContextMatches(bad),"old_native_profile");
 bad=p;bad.lab_mode=L"play";Check(!ur::ContextMatches(bad),"native_play_context");
 std::cout<<"mgo_unique_rest_profile_tests: "<<checks<<" detached checks passed; no native operations\n";
}
