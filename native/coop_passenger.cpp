#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "coop_passenger.hpp"
#include "vehicle_mirror_hook_read.hpp"
#include "vehicle_registry_read.hpp"
#include "vehicle_resource_lifetime.hpp"
#include "vehicle_cleanup_camera_read.hpp"
#include "vehicle_lifetime_read.hpp"
#include "mgscoop/downed_player.hpp"
#include "mgscoop/link_crypto.hpp"
#include "mgscoop/native_pose_window.hpp"
#include "mgscoop/near_code.hpp"
#include "mgscoop/partner_defer.hpp"
#include "mgscoop/partner_player.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "mgscoop/worker_scenario.hpp"
#include "vehicle_controller_read.hpp"
#include "MinHook.h"

namespace mgscoop::coop_passenger {
namespace {
struct Preserve { DWORD error=GetLastError(); ~Preserve(){SetLastError(error);} };
template<class T> bool Read(std::uintptr_t p,T& out) noexcept {
 SIZE_T n=0;return detail::Range(p,sizeof(T))&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(p),&out,sizeof(T),&n)&&n==sizeof(T);
}
bool ReadBytes(std::uintptr_t p,void* out,std::size_t size) noexcept {
 SIZE_T n=0;return detail::Range(p,size)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(p),out,size,&n)&&n==size;
}
struct Pin {std::uintptr_t rva;std::size_t size;unsigned char sha[32];};
// Full declared/chained function spans, plus explicit bounded leaves. Generated
// from the exact retail PE; no MGO offset is used in this adapter.
constexpr Pin kPins[]={
 {0xa27220,57,{0x8b,0xec,0x33,0x1b,0xb6,0xef,0xae,0xa8,0xb6,0xad,0x0b,0x88,0xab,0xb1,0xac,0x95,0x3e,0xb5,0x34,0x51,0xce,0xec,0x67,0x23,0x6c,0xb9,0xea,0x62,0xd9,0xa2,0xec,0xd2}},
 {0xa27260,12,{0xca,0x44,0x02,0xe8,0x04,0xb4,0xb5,0x60,0x7e,0xd5,0x98,0x83,0x07,0x55,0x4e,0xf1,0xae,0xf2,0x1c,0x82,0xf5,0x21,0x6f,0xc4,0x74,0x59,0x4d,0x13,0xbf,0x03,0x3f,0x59}},
 {0xa272a0,154,{0x67,0x11,0xa1,0xf8,0x1a,0x6c,0xd2,0x96,0x39,0x70,0xb9,0x2a,0x34,0x6e,0xc6,0x9d,0xc1,0x06,0x56,0x3d,0xcf,0x46,0xf7,0xdc,0xde,0xb5,0x78,0x91,0x20,0xa9,0xef,0x3b}},
 {0xa27cc0,132,{0x10,0xdc,0x45,0x40,0x8d,0x28,0x8d,0xc0,0x25,0xca,0xd5,0x8c,0x5b,0x7c,0xd2,0x5f,0x32,0xea,0x84,0xe8,0x87,0x8f,0xe3,0xa6,0xba,0xc1,0x22,0x93,0xc2,0x2a,0xa4,0x4a}},
 {0xa28690,56,{0x5c,0xb4,0x99,0x5b,0xd7,0xa3,0x3a,0x1a,0x09,0xd4,0x03,0x72,0x67,0x18,0x7e,0x08,0xe5,0xda,0x0e,0x6d,0xf0,0x7b,0xc1,0x07,0x90,0x2d,0x24,0xf4,0x53,0xde,0xe6,0x89}},
 {0xa286c8,244,{0xa4,0x1c,0x82,0xce,0x97,0x7b,0x78,0x57,0x12,0xf1,0xe6,0x9f,0x80,0x6f,0x67,0xb5,0xd4,0xcf,0x51,0x1d,0xc6,0xd8,0xe8,0xdd,0x2a,0x3c,0x7e,0x9d,0x57,0x0d,0xbe,0x0c}},
 {0xa287bc,302,{0x11,0x0f,0xbd,0xea,0x85,0xd7,0xb7,0x0d,0xac,0x06,0x05,0xff,0xa9,0x08,0x31,0x91,0x7e,0x22,0x0f,0x10,0xce,0xb3,0x73,0xb0,0x98,0x58,0x06,0x8d,0x8b,0x5a,0x5c,0x82}},
 {0xa288ea,40,{0x3c,0x83,0x3c,0xda,0x3c,0xba,0xd8,0xb1,0x13,0xd8,0x60,0x5c,0x5f,0x94,0x06,0xc8,0xc7,0x61,0x93,0xe8,0x2b,0x24,0xeb,0x58,0x60,0x12,0x75,0x54,0x06,0xd7,0xa6,0x55}},
 {0xa28912,25,{0xe5,0x56,0x61,0x35,0x66,0x94,0x6e,0x8c,0x6f,0x39,0x4f,0x19,0x95,0x69,0x11,0x72,0xf6,0x47,0x18,0x56,0x47,0xc0,0x63,0xdc,0x25,0x6f,0x9a,0xb8,0xbb,0xa4,0xec,0x90}},
 {0xa29470,190,{0x4b,0xa3,0xa2,0x8a,0x06,0xa5,0xc8,0x3f,0x40,0xa0,0x7c,0xb6,0x1a,0x23,0xeb,0xb0,0x28,0x7e,0x4c,0xa5,0x20,0x3f,0x71,0x1a,0x6f,0x1f,0x8f,0x29,0x4f,0x2a,0x3d,0xc7}},
 {0xa29530,142,{0x88,0x64,0x6a,0x5d,0x2c,0xe8,0x06,0xc7,0x80,0xeb,0xdf,0x90,0x0b,0x63,0x34,0xbe,0x6d,0xba,0x5f,0xb5,0x52,0x9e,0x87,0x5e,0x2f,0x3c,0x82,0xd2,0xe4,0x14,0x35,0x74}},
 {0xa29890,30,{0xfe,0x51,0xc3,0xc2,0xe2,0x97,0x0b,0xeb,0xc4,0xed,0x28,0x3b,0x14,0xf5,0x4a,0x33,0xf3,0xcb,0x8e,0x0b,0xfb,0xea,0xed,0x6b,0xc3,0x56,0xb3,0xbd,0x3d,0xdf,0xc0,0x6d}},
 {0xa298ae,139,{0x43,0xbb,0xa6,0x5a,0xfd,0x84,0x8f,0x68,0x29,0x38,0x35,0x7c,0xab,0x31,0x4d,0x54,0x96,0x6c,0xf9,0x9c,0xc9,0xd5,0xbb,0xcf,0x9d,0xdf,0x37,0xf2,0xa7,0x81,0x48,0xb4}},
 {0xa29939,12,{0x95,0x3d,0x36,0x7b,0x64,0x96,0x83,0x06,0xd6,0x00,0xc3,0xe0,0x28,0xbe,0x2a,0xc5,0xdf,0x00,0x00,0x47,0x27,0x96,0x9b,0xe1,0x69,0x26,0xe9,0x6a,0x6c,0x4c,0x49,0xe3}},
 {0xb2a1b0,618,{0x4b,0x1e,0xf0,0xd0,0x7c,0x92,0x49,0xd6,0xe0,0xe6,0x42,0x15,0x2d,0x25,0xb6,0xf9,0xe2,0xd6,0x82,0x92,0x7e,0x4b,0x41,0xe9,0x00,0x1a,0x46,0xb8,0x4c,0x77,0x54,0x12}},
 {0xb2ac60,75,{0x43,0x87,0xc2,0xaf,0xf3,0x16,0xf4,0x44,0xb4,0xe1,0x74,0xc6,0xa9,0xe2,0xfc,0x24,0xe4,0xc8,0x9b,0xf4,0x8b,0x3e,0xe9,0x50,0x2c,0xc5,0x5f,0x0b,0x10,0x94,0x76,0x89}},
 {0xb2dd30,8,{0x99,0xfe,0x03,0x90,0xd7,0x0f,0x1d,0x98,0x07,0x6b,0x0f,0x62,0x5b,0x2b,0x53,0xda,0x7d,0x6c,0x9b,0xa1,0x61,0x80,0x3f,0x98,0x1e,0x32,0xe7,0x01,0x6e,0xed,0xd0,0x4b}},
 {0xb78d10,120,{0xfe,0xb4,0xcf,0x52,0x66,0x7f,0x2a,0xe5,0x48,0x93,0x86,0x3d,0xe3,0x31,0x67,0xfa,0x7d,0xad,0x7f,0x47,0x1a,0x5c,0x3b,0x2f,0xfa,0x2a,0x58,0xb2,0x18,0x91,0x74,0x83}},
 {0xb7af90,386,{0x1e,0x9d,0x01,0x59,0xe5,0x92,0x17,0xfe,0x00,0x55,0xd3,0x67,0x90,0xc0,0x46,0x83,0x56,0x8c,0xfc,0x3c,0xe2,0xfd,0x2c,0x12,0x38,0x10,0x33,0xfc,0xd1,0x62,0xab,0xc0}},
 {0xbfefa0,157,{0x57,0xd1,0x1c,0xb0,0x00,0x96,0xa6,0x0c,0x9c,0x3f,0xf1,0xac,0x5e,0x3c,0x8d,0xb9,0x5d,0xbe,0x5a,0x96,0xc7,0x68,0xac,0x4f,0x94,0x2b,0xd5,0xeb,0x82,0xf6,0xd9,0xf2}},
 {0xbff050,18,{0xb4,0xf4,0x8a,0x38,0xa1,0x44,0xd9,0x96,0x00,0xdd,0xb9,0xde,0xa8,0xc9,0xed,0x97,0xb0,0x97,0x58,0xe1,0x61,0x03,0x40,0x86,0x12,0x72,0x88,0x68,0x45,0xdf,0xc4,0xe6}},
 {0xcb9470,74,{0x6c,0xd4,0x42,0x4e,0x57,0x91,0xba,0xbd,0xe4,0x6f,0xd2,0x87,0x99,0x50,0xa1,0x5d,0x6f,0xa9,0x6e,0x60,0x2f,0xd7,0x40,0xcf,0xb5,0x2a,0x1b,0xff,0x73,0xec,0x69,0xfc}},
 {0x1245820,197,{0x85,0x73,0x43,0xac,0x28,0x7d,0x55,0x8f,0xa6,0x98,0x17,0xf5,0x35,0xe5,0xda,0x56,0xe7,0xce,0xb4,0x5a,0xdb,0xff,0x23,0x9b,0x18,0xd2,0xcf,0x62,0x63,0xb2,0xde,0x02}},
 {0x12458e5,18,{0xb5,0xac,0x46,0x1f,0x20,0x92,0x1b,0xf6,0xb5,0xbd,0x1a,0x95,0xa5,0x03,0xc8,0xae,0x58,0xd1,0x10,0x6d,0x93,0x9c,0x05,0xdb,0xaf,0xd7,0xc2,0xc5,0x35,0x03,0x7a,0xb9}},
 {0x12458f7,88,{0x46,0xe8,0x80,0x57,0xac,0xda,0xea,0xd5,0x34,0x49,0xb0,0x9c,0x00,0x2f,0x47,0xa8,0xf8,0x4d,0xaf,0x33,0x0c,0x83,0x2b,0x94,0x5c,0xd1,0x67,0xc7,0x79,0x5f,0xcd,0xe1}},
 {0x124594f,35,{0x4b,0x37,0x81,0x02,0x9a,0x62,0xba,0xf1,0x88,0xd2,0xb6,0x47,0xfa,0xa5,0x30,0xa4,0x3e,0x1d,0x39,0x34,0x02,0x44,0x33,0x24,0x70,0xd4,0x2c,0x89,0xf9,0xf3,0x07,0xac}},
 {0x1245972,145,{0x12,0xd9,0xd5,0xb4,0xe2,0x75,0x7c,0x78,0xcb,0xa2,0x2f,0xc0,0x1e,0x55,0x84,0xfe,0xb1,0x04,0x8d,0x51,0x4a,0x47,0x89,0x8b,0x87,0xb7,0x0f,0xc2,0xa6,0x84,0x05,0x81}},
 {0x1245a03,32,{0xd0,0x9e,0xda,0xe8,0xf8,0xde,0x6a,0xeb,0xdf,0x03,0x66,0x47,0x1e,0x3a,0x79,0x9c,0x66,0xf3,0x64,0x12,0xd6,0x02,0x15,0x74,0x07,0x5d,0xa7,0xad,0x87,0xe1,0x50,0x45}},
 {0x1246650,184,{0x68,0xd8,0xb9,0xa4,0x77,0xc9,0xdc,0x8e,0x43,0x3c,0xa7,0x8a,0xd4,0xc7,0xc5,0xeb,0xba,0x40,0x2f,0xf8,0x7c,0x19,0x41,0x06,0xd1,0x87,0xb5,0xac,0x2b,0xcb,0x22,0xf0}},
 {0x1246708,46,{0xea,0x4f,0xd1,0x26,0xb6,0xe7,0x02,0x3e,0x83,0x67,0x02,0xae,0x21,0xf9,0x35,0xed,0x37,0x3b,0xf5,0x80,0x79,0x7d,0xef,0xe9,0x0a,0x40,0x96,0xbf,0xf4,0xcb,0x31,0x8c}},
 {0x1246736,43,{0x87,0xa9,0x14,0xa2,0xdf,0xc6,0x9c,0x18,0x6a,0x91,0xa7,0x6c,0xda,0x32,0x80,0xa9,0xff,0xad,0x23,0x03,0x5a,0x4a,0xdc,0xca,0xe5,0x9d,0xcf,0x80,0x4f,0x16,0x88,0x46}},
 {0x1246761,436,{0x7e,0x22,0x4a,0x68,0x02,0x9c,0xbf,0x3f,0x8b,0x89,0x88,0x1f,0xa8,0xbc,0xc2,0x25,0xcb,0xf9,0xc9,0xd5,0x3f,0x63,0x77,0x34,0x81,0xc7,0xe3,0x14,0x60,0x0e,0x18,0x26}},
 {0x1246915,1392,{0xe5,0xd5,0x84,0xf9,0x26,0xf6,0x98,0x41,0x7f,0x58,0xd0,0xc9,0x23,0x8b,0x71,0x0a,0x89,0xaf,0x53,0x6d,0x5e,0xf7,0xcf,0x8f,0x6d,0x13,0x13,0x7f,0x7f,0x45,0xcf,0x44}},
 {0x1246e85,9,{0xe3,0xe2,0xcd,0x7e,0x0f,0x9c,0xa5,0xfe,0xd6,0xbd,0xc9,0xdf,0xbd,0x52,0x19,0xfe,0xf9,0xe5,0x92,0xbd,0x1b,0x48,0x1e,0xe7,0x88,0x33,0x0b,0x08,0x2f,0x7a,0x12,0x6f}},
 {0x1246e8e,9,{0xdd,0xa8,0x6a,0xff,0x79,0x9e,0xc6,0x9b,0x58,0x23,0xe6,0x22,0x48,0xd4,0x3e,0x21,0xe3,0xea,0x97,0xf1,0x2a,0xe0,0x37,0xee,0x96,0xaa,0x51,0x85,0x57,0xee,0x57,0x77}},
 {0x1246e97,8,{0x6e,0xd1,0x10,0xd7,0x89,0xc9,0xa8,0xfb,0xe6,0x96,0x8b,0x43,0x2a,0x8a,0xdb,0xa0,0x97,0x3f,0xc3,0xb5,0x7b,0x61,0x9a,0x78,0x34,0xe1,0x60,0x27,0x40,0xe8,0x32,0xc1}},
 {0x1246e9f,33,{0x47,0xab,0x21,0x08,0x52,0x81,0x1d,0xe4,0xbe,0x93,0x64,0x57,0x0c,0xd4,0x10,0x7b,0x07,0xa9,0xcd,0x2e,0x56,0x27,0x8f,0x0e,0xfa,0x12,0xed,0x1b,0x86,0x56,0x7e,0x21}},
 {0x1246ec0,4,{0xc8,0x4e,0x69,0x60,0x51,0x18,0x30,0x43,0x35,0x2b,0x8d,0xad,0x2e,0x5b,0x47,0x8e,0x42,0x9e,0xac,0xb8,0x3c,0x2b,0x90,0x41,0xa2,0xb8,0x43,0xc1,0x14,0xda,0xd8,0x93}},
 {0x1246ec4,4,{0xd0,0x93,0x34,0x36,0x89,0x85,0xb8,0xe8,0x86,0x0b,0xdc,0xff,0x72,0x47,0x27,0xf9,0xa1,0xd5,0xed,0xdf,0x79,0x46,0xcd,0x76,0x0f,0x61,0x95,0xe8,0x1a,0xbe,0xa3,0x07}},
 {0x1246ec8,32,{0xe2,0x44,0xbf,0x95,0xe5,0x33,0xa1,0xe9,0xc8,0xcd,0x92,0xc1,0xe4,0x98,0x9e,0x63,0x5f,0x9b,0xe8,0xd3,0x23,0x61,0x36,0x9e,0xee,0x6a,0x23,0x72,0xc2,0xdd,0x18,0x1d}},
 {0x1246ef0,389,{0xde,0x53,0xd2,0xcb,0x7d,0xad,0x2f,0x5d,0x83,0xf9,0x7f,0x43,0x9f,0x15,0xce,0x2a,0xd4,0x4d,0x12,0xa7,0x2f,0x2b,0x1b,0x2d,0x7f,0x7a,0x2a,0xb6,0xfc,0x91,0x54,0xcc}},
 {0x1247700,53,{0x6b,0x40,0xb3,0x36,0x6f,0x84,0x1e,0x55,0x8d,0xd3,0x78,0xc4,0x99,0x1e,0xe5,0x3f,0x43,0x50,0xf4,0xd5,0x76,0xf5,0xf9,0x74,0x8c,0xde,0x1c,0x46,0x0c,0x82,0x0b,0x1f}},
 {0x1247735,137,{0x9d,0xfe,0xc8,0x28,0x88,0xd1,0x8e,0x10,0xce,0xec,0xb3,0xd2,0x91,0x66,0xd2,0xe6,0x58,0x9b,0x60,0x46,0xe2,0x31,0xc0,0x88,0xfa,0xaf,0x4d,0xe8,0x54,0xa4,0x9a,0x59}},
 {0x12477be,27,{0xe7,0x48,0x90,0x94,0x94,0xaf,0xcb,0xe6,0x7a,0x83,0x78,0x2e,0xab,0x42,0x14,0xdf,0x1b,0xa7,0xed,0xbb,0xc0,0x2d,0xb9,0xba,0xe6,0xb2,0xcb,0x8a,0x8b,0xf2,0xe6,0xa9}},
 {0x1247d80,125,{0x58,0xdc,0xe6,0x6a,0x82,0x7b,0xd0,0x59,0xc9,0x94,0xb9,0x67,0x0e,0x44,0x7b,0xb6,0xec,0x3c,0xad,0xc0,0xd5,0x27,0x39,0xae,0xd5,0x1f,0xe5,0x52,0x05,0x44,0x65,0x65}},
 {0x124a4b0,103,{0x39,0x66,0xdd,0x7a,0x42,0xca,0xeb,0x5f,0xb2,0xbd,0x71,0x16,0x9d,0xe8,0xcd,0xf4,0xfd,0x57,0x94,0x8c,0x58,0x6d,0x3c,0xdb,0x83,0xa2,0x04,0xad,0x43,0xef,0x13,0x26}},
 {0x124b190,129,{0x48,0xde,0x01,0x89,0x79,0x57,0xfc,0xee,0xb6,0x8b,0xc8,0x18,0x7c,0xd2,0x9e,0x9c,0xa0,0x98,0x6e,0x0b,0x00,0x78,0x48,0x4a,0x7f,0x28,0x3c,0xc2,0xd6,0x1f,0x72,0xcb}},
 {0x124b360,64,{0x17,0x2c,0x32,0xb9,0xde,0xb5,0x4b,0x26,0x9a,0x04,0xae,0x80,0x91,0x1e,0xdf,0x30,0xc9,0xd4,0xa2,0x1f,0x3e,0xf6,0x6c,0xfb,0x52,0xe9,0x66,0xba,0x53,0x72,0xc9,0x44}},
 {0x124b3a0,706,{0xb5,0xf9,0x38,0x2e,0x5f,0x65,0xbd,0x9f,0xdc,0xb1,0x5c,0xdd,0x11,0x1b,0x86,0xa1,0xc2,0x9c,0x16,0xc5,0xe4,0x9c,0x0f,0xbe,0xf1,0x31,0x0e,0xf7,0xf8,0x79,0xb9,0xa4}},
 {0x124c760,19,{0xce,0xa0,0xb4,0x1d,0x92,0x6d,0xfa,0x78,0xd9,0x40,0x97,0x5c,0x22,0xf0,0xfb,0x31,0x23,0x56,0x1f,0x56,0x32,0x9b,0xe5,0x35,0x93,0xa9,0x0f,0x42,0xc0,0xaf,0x5c,0xb6}},
 {0x124c773,178,{0x02,0x2f,0xb7,0xb0,0xb5,0x3c,0xa8,0xe9,0xf2,0xcb,0x59,0xae,0xf8,0x0d,0x16,0x73,0xad,0xce,0x1f,0x29,0x82,0x7a,0x01,0xfb,0x52,0xb3,0xa4,0xac,0xe0,0xa8,0x73,0xfe}},
 {0x124c825,3,{0xaa,0xd6,0x89,0x62,0xe5,0xc1,0x3b,0x12,0xbd,0x8d,0x13,0x1f,0x62,0x65,0x31,0x5c,0xc3,0x7f,0x50,0x8a,0xa1,0x08,0xc4,0x92,0x5d,0x48,0x91,0xb4,0x48,0x1f,0x2c,0x10}},
 {0x124c850,1533,{0xee,0x61,0x93,0x99,0x92,0x80,0xab,0xea,0xaa,0x62,0x06,0x91,0x1c,0x0e,0xe8,0x08,0x6d,0x24,0xce,0x12,0xe9,0xfe,0x57,0x92,0x6a,0xd0,0x65,0xd6,0xfb,0x97,0x30,0xe7}},
 {0x124fd80,76,{0xe6,0x08,0x50,0x47,0x16,0x97,0x0a,0xd0,0xa8,0x63,0x8a,0x67,0xe6,0x98,0xe5,0xe8,0xf4,0x50,0x49,0x1e,0xf2,0xda,0x7d,0x14,0xfc,0xb4,0x51,0x90,0x8a,0x77,0x12,0x33}},
 {0x124fdcc,216,{0xda,0x45,0xd9,0x69,0x85,0xf3,0xf5,0x76,0xb5,0xbf,0xb3,0xb4,0x47,0x28,0x79,0x39,0xc7,0xa5,0x5c,0x8a,0x33,0xb2,0xe8,0x6f,0xd8,0x4b,0x86,0xf7,0x9e,0x8b,0x6b,0x8a}},
 {0x124fea4,887,{0x25,0x86,0xf9,0x4c,0x54,0x82,0xd3,0x8e,0x0d,0x69,0x74,0xc0,0x12,0x30,0x17,0x78,0x99,0xe6,0x3f,0x74,0x95,0x2f,0x17,0x52,0x2c,0xe5,0x75,0x26,0x2b,0x9f,0x6e,0x6c}},
 {0x125021b,926,{0x4a,0x58,0x76,0xbe,0x74,0xc6,0x45,0xfc,0xbb,0x88,0x46,0xd9,0x89,0xea,0x05,0xc9,0x5d,0x85,0x5e,0x0f,0x7b,0xaf,0xb6,0x44,0x14,0xb6,0x0a,0xa9,0x5a,0xfe,0x09,0x80}},
 {0x12505b9,15,{0x9a,0xb5,0x0d,0xb7,0x48,0xe3,0xc2,0x3f,0x54,0x2b,0x73,0x29,0x68,0x38,0xad,0xfc,0xf0,0x51,0x83,0x74,0x30,0x66,0xa6,0x20,0x9a,0x47,0x45,0x67,0x6e,0x55,0xba,0x09}},
 {0x12505c8,52,{0xfe,0x6a,0xbf,0xad,0xfd,0xb5,0xcf,0x11,0x05,0x44,0x81,0x5c,0x2a,0x24,0xd5,0x5f,0x52,0x78,0x3b,0x03,0x63,0x38,0xdb,0x81,0xa7,0x1b,0x25,0x11,0xf0,0xb6,0x21,0xd7}},
 {0x1250600,1772,{0xb5,0x60,0xeb,0x35,0x16,0x1e,0xcf,0x84,0xca,0x47,0xa5,0xa8,0x3d,0x0b,0x91,0x9a,0xbd,0x1d,0x1b,0x27,0x02,0xdc,0xf2,0x72,0x70,0x6c,0x19,0x89,0x4b,0xb7,0x67,0x10}},
 {0x1250cf0,92,{0x80,0x50,0xdb,0xeb,0x36,0x73,0x73,0x55,0x9d,0x81,0x26,0x0f,0xba,0x3e,0x81,0xf3,0xfa,0xf7,0x00,0x70,0x24,0xfb,0x64,0x90,0x6b,0x32,0xaf,0xaf,0x91,0xcb,0xd1,0x1d}},
 {0x1250d4c,1123,{0x4d,0xa8,0x3d,0xa7,0xc6,0x8a,0x07,0x15,0xb2,0xfd,0xfc,0xa3,0x3f,0x5b,0x2d,0x6a,0x79,0x09,0x87,0xf6,0xf5,0x5e,0xda,0x0b,0x46,0xb7,0x7c,0x68,0x24,0xb1,0xf0,0x9e}},
 {0x12511af,29,{0xc3,0x18,0x24,0xfa,0xfd,0x35,0xc0,0xef,0xbe,0xdd,0x49,0x92,0x71,0xba,0x7b,0x13,0xa2,0x29,0xa7,0x00,0x99,0xf2,0xca,0x0e,0x51,0x50,0x66,0x80,0x08,0x35,0x20,0xd6}},
 {0x1251200,1860,{0x43,0xd1,0x83,0xfa,0xfd,0x3f,0x4b,0x29,0x61,0xab,0xc6,0x13,0x65,0x29,0x5a,0xb6,0xce,0xe7,0xe1,0x48,0x7f,0x00,0xc8,0x0d,0x7b,0xc0,0xb5,0xc0,0xbc,0xae,0xc0,0x7e}},
 {0x1259b80,87,{0x33,0xe2,0x78,0x14,0x41,0x46,0x5c,0xeb,0xce,0xc0,0x46,0xe8,0x3e,0x84,0x74,0x99,0x4f,0xe1,0x47,0x27,0x85,0x81,0x05,0x9b,0x56,0xe9,0xc9,0x2b,0x2b,0xb3,0xd1,0xc8}},
 {0x1259bd7,214,{0xa9,0x2f,0xa1,0xbe,0xba,0xf8,0x67,0xd2,0x7d,0x4b,0x47,0x12,0xb1,0xf3,0xaf,0x01,0x84,0xae,0xb2,0x5a,0x38,0xf1,0x11,0x9c,0xab,0x49,0x9a,0xa5,0x89,0xac,0x6e,0x5c}},
 {0x1259cad,4027,{0x64,0x50,0xe2,0xcf,0x53,0x47,0x99,0xf2,0xd7,0x83,0xcb,0x2a,0xad,0xc7,0x61,0x0d,0x49,0xf2,0xf9,0x0d,0xf5,0x1d,0x59,0x8a,0xfc,0xc2,0x82,0xa6,0x87,0x58,0xc1,0xaf}},
 {0x125ac68,114,{0xd7,0x10,0x8c,0x74,0x15,0x6d,0xef,0xe4,0x69,0xfa,0x8d,0xc4,0x56,0xd3,0x11,0xd2,0x9a,0x74,0x7e,0xeb,0xa7,0xc7,0x3e,0xa2,0x3c,0x03,0x19,0xa5,0x64,0x49,0x4b,0x26}},
 {0x125acda,29,{0x02,0x9c,0x8f,0x0f,0xe2,0xe6,0x46,0x3d,0x43,0xe8,0x8b,0x1a,0xc3,0xd9,0xbb,0xdb,0x17,0xb1,0xba,0x33,0x1c,0x73,0x8a,0xa6,0xbf,0x66,0x22,0x5d,0xaa,0x30,0x8b,0x1c}},
 {0x125ad00,140,{0x31,0x2c,0x31,0xf6,0xaa,0x8e,0xfe,0xd4,0xd1,0x41,0xb2,0x42,0x4f,0xc0,0x7d,0x15,0x80,0x55,0x22,0x3d,0x4c,0xfb,0xab,0x11,0x29,0x51,0x56,0x3f,0xff,0x4f,0x9f,0xb8}},
 {0x125ca40,77,{0x33,0xab,0x29,0xcd,0xcf,0x62,0x6c,0x26,0xb1,0x4d,0x79,0x3d,0xbd,0x89,0x93,0x71,0x7d,0x23,0xbc,0x1b,0x99,0x91,0xb6,0xfd,0x86,0x4d,0x98,0xdc,0xed,0x85,0x9a,0x88}},
 {0x125ca8d,504,{0xe8,0xe7,0xe8,0x68,0x88,0xba,0x24,0xeb,0x1d,0xa8,0x87,0x4c,0xf3,0xbb,0xf2,0x92,0x9a,0x7d,0x12,0x07,0xef,0x48,0xeb,0x3c,0x02,0x60,0x75,0x98,0x36,0x1c,0x92,0x84}},
 {0x125cc85,26,{0x17,0xf0,0xec,0xe0,0x9c,0x3c,0x26,0xd4,0x3b,0x43,0x03,0x1f,0xba,0x5c,0xa1,0xe9,0xda,0x1f,0x6c,0x9d,0xc2,0x91,0x39,0x86,0x55,0x73,0x27,0x9a,0x3e,0x31,0x1f,0xd2}},
};

constexpr std::uintptr_t kCanExec=0x1246650,kNearest=0x124b3a0,kPhase1=0x1251200,kPose=0x1259b80,kExit=0x1247700;
constexpr std::uintptr_t kJcc=0x1246a02,kJccNext=0x1246a08,kJccFail=0x1246e73;
constexpr std::uint32_t kOriginalDisp=0x46b;
constexpr std::uintptr_t kPrequeryReturn=0x12458b1,kPhase1Return=0x125147e;
using CanExecFn=bool(*)(void*,std::uint32_t,std::uint8_t,void*);
using NearestFn=std::uint8_t(*)(void*,const void*,std::uint8_t,std::uint8_t);
using CallbackFn=void(*)(void*,std::uint32_t,std::uint32_t,void*);
using PoseFn=void(*)(void*,std::uint32_t,void*);
using ExitFn=bool(*)(void*,std::uint32_t,void*);
CanExecFn g_canExec=nullptr;NearestFn g_nearest=nullptr;CallbackFn g_phase1=nullptr;PoseFn g_pose=nullptr;ExitFn g_exit=nullptr;
std::uintptr_t g_base=0;std::size_t g_span=0;Log g_log=nullptr;
partner_player::Addresses g_addresses{};
std::atomic<bool> g_installed{false},g_enabled{false},g_stopped{false},g_lost{false};
std::atomic<std::uint32_t> g_owner{0},g_mission{0},g_epoch{0};
std::atomic<std::uint64_t> g_tickAt{0},g_serial{0},g_predicates{0},g_overrides{0},g_prequeries{0},g_commits{0},g_attachments{0},g_exits{0},g_refused{0};
std::atomic<const char*> g_scopeReason{"never"},g_tickReason{"never"};
std::atomic<std::uint32_t> g_stockChecks{0},g_stockAccepted{0},g_stockTarget{0xffff},g_stockQuery{0xff};
std::atomic<std::uint32_t> g_stockNearest{0},g_stockFirst{4},g_stockLast{4},g_stockResult{4};
SRWLOCK g_lock=SRWLOCK_INIT;detail::Lease g_lease{};
namespace hooks=vehicle_mirror_hook_read;
hooks::Hook g_hooks[5]{};unsigned char g_originalHeads[5][16]{};
NativePoseObserver g_poseObserver=nullptr;

std::uint32_t g_gatewayDisp=0;void* g_gateway=nullptr;
std::size_t g_gatewayLength=0;unsigned char g_gatewayBytes[512]{};

thread_local unsigned t_canDepth=0,t_phaseDepth=0;
thread_local std::uintptr_t t_primary=0,t_descriptor=0,t_phasePrimary=0;
thread_local std::uint32_t t_body=0xffffffffu,t_phaseBody=0xffffffffu;
thread_local std::uint8_t t_query=0xff;
thread_local detail::Lease t_candidate{};

void Emit(const char* message) noexcept {if(g_log)g_log(message);}
bool Gate() noexcept {
 unsigned char raw[8192]{};
 for(const auto& pin:kPins){
  if(pin.rva>g_span||pin.size>g_span-pin.rva)return false;
  link::Sha256 hash{};link::Sha256Init(hash);std::size_t offset=0;
  while(offset<pin.size){const auto left=pin.size-offset;const auto size=left<sizeof(raw)?left:sizeof(raw);
   if(!partner_input_hook::ReadNativePoseOriginalCode(g_base+pin.rva+offset,raw,size))return false;
   link::Sha256Update(hash,raw,size);offset+=size;
  }
  unsigned char digest[32]{};link::Sha256Final(hash,digest);if(std::memcmp(digest,pin.sha,32))return false;
 }
 const struct {std::uintptr_t slot,target;} slots[]={{0x23cbea8+8,kCanExec},{0x22ac8e0+0x18,0xa272a0},
  {0x22ac8e0+0x168,0xa27260},{0x22ac8e0+0x170,0xa27220},{0x22ed9f0,0xb2dd30},
  {0x22ac8e0+0x158,0xa28690},{0x22ed9f0+0x10,0xb2ac60}};
 for(const auto& pin:slots){std::uintptr_t value=0;if(!Read(g_base+pin.slot,value)||value!=g_base+pin.target)return false;}
 std::uintptr_t mismatch=0;auto reader=partner_input_hook::ReadNativePoseOriginalCode;
 return vehicle_controller_read::CodeGate(reader,g_base,g_span,mismatch);
}
bool PatchesLive() noexcept {
 if(!g_installed.load()||!g_gateway||!g_gatewayLength||g_gatewayLength>sizeof(g_gatewayBytes))return false;
 auto read=ReadBytes;for(const auto& hook:g_hooks)if(!hooks::Live(read,g_base,hook))return false;
 unsigned char jcc[6]{},gateway[512]{};std::uint32_t disp=0;
 if(!ReadBytes(g_base+kJcc,jcc,sizeof(jcc))||jcc[0]!=0x0f||jcc[1]!=0x85||
  !ReadBytes(reinterpret_cast<std::uintptr_t>(g_gateway),gateway,g_gatewayLength)||
  std::memcmp(gateway,g_gatewayBytes,g_gatewayLength))return false;
 std::memcpy(&disp,jcc+2,4);std::int32_t signedDisp=0;std::memcpy(&signedDisp,&disp,4);
 return disp==g_gatewayDisp&&g_base+kJccNext+static_cast<std::intptr_t>(signedDisp)==reinterpret_cast<std::uintptr_t>(g_gateway);
}
struct Current {
 downed_player::Snapshot owner{};partner_player::State player{};
 vehicle_controller_read::Observation controller{};
 std::uintptr_t service=0;std::uint32_t sequence=0;
 const char* reason="never";
};
bool CurrentScope(Current& out,bool requireEnabled=true) noexcept {
 out={};
 const auto fail=[&](const char* why) noexcept {out.reason=why;g_scopeReason.store(why);return false;};
 if(!g_installed.load())return fail("not_installed");
 if(g_stopped.load())return fail("stopped");
 if(g_lost.load())return fail("lifecycle_lost");
 if(requireEnabled&&!g_enabled.load())return fail("disabled");
 if(!worker_scenario::ThreadAdmitted())return fail("worker_thread");
 if(!PatchesLive())return fail("owned_patch");
 if(partner_input_hook::InDrainJob())return fail("partner_drain");
 if(partner_defer::InBatch())return fail("batch");
 const auto now=GetTickCount64();const auto thread=GetCurrentThreadId();
 if(!thread||thread!=g_owner.load())return fail("owner_thread");
 if(!detail::Fresh(now,g_tickAt.load(),750))return fail("tick_expired");
 const auto window=partner_input_hook::NativePoseWindowStatus();
 if(window.held)return fail("pose_held");
 if(window.faulted)return fail("pose_faulted");
 if(window.owned)return fail("pose_owned");
 out.owner=downed_player::GetSnapshot();
 if(!out.owner.ready)return fail("owner_not_ready");
 if(!out.owner.armed)return fail("owner_unarmed");
 if(out.owner.down)return fail("owner_downed");
 if(!out.owner.generation)return fail("owner_generation");
 if(out.owner.mission!=g_mission.load()||out.owner.epoch!=g_epoch.load())return fail("owner_scene");
 if(!detail::Fresh(now,out.owner.lastPoll,750))return fail("owner_poll_expired");
 out.sequence=partner_input_hook::WindowSeq();if(out.sequence&1u)return fail("input_window");
 const auto state=partner_player::ReadState(g_addresses,out.player);
 if(state!=partner_player::Refusal::none)return fail(partner_player::RefusalName(state));
 if(out.player.count!=2)return fail("body_count");
 if(out.player.realized_count!=2)return fail("body_realized_count");
 if(out.player.local||out.player.ids[0]!=0||out.player.ids[1]!=1)return fail("body_ids");
 if(!out.player.arrays_ok||!out.player.bits_ok||!out.player.slots_ok)return fail("body_arrays");
 if(out.player.parts[0]!=3||out.player.parts[1]!=3)return fail("body_parts");
 vehicle_controller_read::Scope scope{};scope.exactGate=true;scope.admitted=true;scope.ready=true;scope.armed=true;
 scope.image=g_base;scope.player=out.player.P;scope.data=out.player.Q;scope.state=out.player.S;
 scope.mission=scope.ownedMission=out.owner.mission;scope.epoch=scope.ownedEpoch=out.owner.epoch;
 scope.generation=scope.ownedGeneration=out.owner.generation;scope.thread=scope.ownerThread=thread;
 scope.now=now;scope.poll=out.owner.lastPoll;scope.sequenceBefore=scope.sequenceAfter=out.sequence;
 auto reader=ReadBytes;const char* reason=nullptr;
 if(!vehicle_controller_read::ReadOnce(reader,scope,out.controller,&reason))return fail(reason?reason:"vehicle_controller");
 std::uintptr_t root=0,world=0,manager=0,registered=0,vt=0;
 if(!Read(g_base+0x2c3a8a0,root)||!root||!Read(root+8,world)||!world||
 !Read(world+0x98,manager)||!manager||!Read(manager+0x2a8,out.service))return fail("vehicle_world_service");
 if(!Read(g_base+0x2c20730,registered)||out.service!=registered)return fail("vehicle_registered");
 if(!Read(out.service,vt)||vt!=g_base+0x22ac8e0)return fail("vehicle_service_type");
 const auto after=downed_player::GetSnapshot();
 if(!after.ready||!after.armed||after.down||after.mission!=out.owner.mission||after.epoch!=out.owner.epoch||
 after.generation!=out.owner.generation)return fail("owner_changed");
 if(partner_input_hook::WindowSeq()!=out.sequence)return fail("input_window_changed");
 out.reason="ok";g_scopeReason.store(out.reason);return true;
}
bool CurrentUnit(const Current& scope,std::uint16_t vehicle,std::uintptr_t unit,detail::Unit& out) noexcept {
 auto reader=ReadBytes;detail::Unit a{},b{};
 return detail::ReadUnit(reader,g_base,scope.service,vehicle,unit,a)&&
 detail::ReadUnit(reader,g_base,scope.service,vehicle,unit,b)&&detail::Same(a,b)&&
 partner_input_hook::WindowSeq()==scope.sequence&&(out=b,true);
}
bool CurrentLease(const detail::Lease& l,const Current& s) noexcept {
 const auto& b=s.controller.body[0];return l.valid&&l.serial&&l.thread==GetCurrentThreadId()&&l.player==s.player.P&&
 l.mission==s.owner.mission&&l.epoch==s.owner.epoch&&l.generation==s.owner.generation&&
 l.primary==b.primary&&l.record==b.record;
}
bool CopyLease(detail::Lease& out) noexcept {
 if(!TryAcquireSRWLockShared(&g_lock))return false;out=g_lease;ReleaseSRWLockShared(&g_lock);return true;
}
bool LoadLease(detail::Lease& out) noexcept {return CopyLease(out)&&out.valid;}
// Invoked only by the taken occupied-driver branch in original CanExec.
__declspec(noinline) bool OccupiedGateway(void* unit,void* primary,std::uint32_t body,void* descriptor) noexcept {
 const Preserve preserve;g_predicates.fetch_add(1);Current current{};
 if(t_canDepth!=1||t_query>1||body||t_body!=body||t_primary!=reinterpret_cast<std::uintptr_t>(primary)||
 t_descriptor!=reinterpret_cast<std::uintptr_t>(descriptor)||!CurrentScope(current))return false;
 const auto& b=current.controller.body[0];
 if(b.primary!=t_primary||b.raw[0x14]||b.raw[0x15]||!descriptor)return false;
 std::uint16_t vehicle=0xffff;detail::Unit a{};
 if(!Read(t_descriptor+0x20,vehicle)||!CurrentUnit(current,vehicle,reinterpret_cast<std::uintptr_t>(unit),a))return false;
 const auto seat=detail::EmptyPassenger(a);if(seat==4)return false;
 detail::Lease candidate{};candidate.valid=true;candidate.issued=GetTickCount64();candidate.serial=g_serial.fetch_add(1)+1;
 candidate.mission=current.owner.mission;candidate.epoch=current.owner.epoch;candidate.generation=current.owner.generation;
 candidate.thread=GetCurrentThreadId();candidate.player=current.player.P;candidate.primary=b.primary;candidate.record=b.record;
 candidate.seat=seat;candidate.vehicle=a;t_candidate=candidate;g_overrides.fetch_add(1);return true;
}
__declspec(noinline) bool HookCanExec(void* self,std::uint32_t body,std::uint8_t query,void* descriptor) noexcept {
 const DWORD entry=GetLastError();const auto oldPrimary=t_primary,oldDescriptor=t_descriptor;const auto oldBody=t_body;
 const auto oldQuery=t_query;t_query=query;
 const auto oldCandidate=t_candidate;t_candidate={};++t_canDepth;t_primary=reinterpret_cast<std::uintptr_t>(self);
 t_descriptor=reinterpret_cast<std::uintptr_t>(descriptor);t_body=body;
 SetLastError(entry);const bool result=g_canExec(self,body,query,descriptor);const DWORD after=GetLastError();
 if(!body&&worker_scenario::ThreadAdmitted()){
  g_stockChecks.fetch_add(1);if(result)g_stockAccepted.fetch_add(1);g_stockQuery.store(query);
  std::uint16_t target=0xffff;
  if(descriptor&&Read(reinterpret_cast<std::uintptr_t>(descriptor)+0x20,target)&&(target&0xfe00u)==0x6400u)g_stockTarget.store(target);
  else g_stockTarget.store(0xffff);
 }
 const auto candidate=t_candidate;--t_canDepth;t_primary=oldPrimary;t_descriptor=oldDescriptor;t_body=oldBody;t_candidate=oldCandidate;t_query=oldQuery;
 if(result&&candidate.valid&&query<=1&&TryAcquireSRWLockExclusive(&g_lock)){
  // A query may refresh an uncommitted candidate. It cannot replace an active
  // passenger allocation or issue anything on behalf of body1.
  if(!g_lease.committed)g_lease=candidate;
  ReleaseSRWLockExclusive(&g_lock);
 }
 SetLastError(after);return result;
}
__declspec(noinline) std::uint8_t HookNearest(void* unit,const void* position,std::uint8_t first,std::uint8_t last) noexcept {
 const DWORD entry=GetLastError();const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 detail::Lease lease{};const bool pre=caller==g_base+kPrequeryReturn&&t_canDepth==1;
 const bool phase=caller==g_base+kPhase1Return&&t_phaseDepth==1;
 if(pre)lease=t_candidate;else if(phase)LoadLease(lease);
 Current current{};detail::Unit found{};bool changed=false;
 if(lease.valid&&(pre||phase)&&CurrentScope(current)&&CurrentLease(lease,current)&&
 CurrentUnit(current,lease.vehicle.vehicle,reinterpret_cast<std::uintptr_t>(unit),found)){
  const auto& b=current.controller.body[0];
  const bool phaseOwned=phase&&t_phasePrimary==b.primary&&!t_phaseBody&&b.raw[0x14]==1&&
   vehicle_controller_read::Word(b.raw+0x44)==lease.vehicle.vehicle;
  changed=detail::OverrideRange(lease,found,b.primary,0,GetTickCount64(),pre,phaseOwned,first,last);
 }
 if(changed)(pre?g_prequeries:g_commits).fetch_add(1);
 SetLastError(entry);const auto result=g_nearest(unit,position,first,last);const DWORD after=GetLastError();
 // Never manufacture a successful nearest result; sentinel4 and every native
 // geometry/door/raycast refusal stay unchanged.
 if((pre||phase)&&worker_scenario::ThreadAdmitted()){
  g_stockNearest.fetch_add(1);g_stockFirst.store(first);g_stockLast.store(last);g_stockResult.store(result);
 }
 SetLastError(after);return result;
}
__declspec(noinline) void HookPhase1(void* self,std::uint32_t body,std::uint32_t event,void* payload) noexcept {
 const DWORD entry=GetLastError();const auto old=t_phasePrimary;const auto oldBody=t_phaseBody;
 ++t_phaseDepth;t_phasePrimary=reinterpret_cast<std::uintptr_t>(self);t_phaseBody=body;
 SetLastError(entry);g_phase1(self,body,event,payload);const DWORD after=GetLastError();
 --t_phaseDepth;t_phasePrimary=old;t_phaseBody=oldBody;
 if(!body&&event==0){Current current{};detail::Lease lease{};
  if(CurrentScope(current)&&LoadLease(lease)&&CurrentLease(lease,current)){
   const auto& b=current.controller.body[0];detail::Unit u{};
   const auto seat=std::uint8_t(b.raw[0x17]&15u);
   if(b.primary==reinterpret_cast<std::uintptr_t>(self)&&b.raw[0x14]==1&&b.raw[0x15]==2&&seat==lease.seat&&
    vehicle_controller_read::Word(b.raw+0x44)==lease.vehicle.vehicle&&CurrentUnit(current,lease.vehicle.vehicle,lease.vehicle.unit,u)&&u.rider[seat]==0&&
    TryAcquireSRWLockExclusive(&g_lock)){
    if(detail::SameIdentity(lease,g_lease))g_lease.committed=true;ReleaseSRWLockExclusive(&g_lock);
    Emit("coop_passenger=physical_admission; body=0; next=2\r\n");
   }
  }
 }
 SetLastError(after);
}
__declspec(noinline) void HookPose(void* self,std::uint32_t body,void* record) noexcept {
 const DWORD entry=GetLastError();const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 SetLastError(entry);g_pose(self,body,record);const DWORD after=GetLastError();
 if(g_poseObserver)g_poseObserver(reinterpret_cast<std::uintptr_t>(self),body,reinterpret_cast<std::uintptr_t>(record),caller);

 if(!body){Current current{};detail::Lease lease{};
  if(CurrentScope(current)&&LoadLease(lease)&&lease.committed&&CurrentLease(lease,current)){
   const auto& b=current.controller.body[0];detail::Unit unit{};std::uintptr_t map=0,attached=0;
   std::uint16_t qVehicle=0xffff;std::uintptr_t qArray=0;
   if(b.primary==reinterpret_cast<std::uintptr_t>(self)&&b.record==reinterpret_cast<std::uintptr_t>(record)&&
    b.raw[0x14]==2&&(b.raw[0x17]&15u)==lease.seat&&
    Read(current.player.P+0x120,map)&&map&&Read(map,attached)&&attached==lease.vehicle.unit&&
    Read(current.player.Q+0xe0,qArray)&&qArray&&Read(qArray,qVehicle)&&qVehicle==lease.vehicle.vehicle&&
    CurrentUnit(current,lease.vehicle.vehicle,attached,unit)&&unit.rider[lease.seat]==0)g_attachments.fetch_add(1);
  }
 }
 SetLastError(after);
}
__declspec(noinline) bool HookExit(void* self,std::uint32_t body,void* record) noexcept {
 const DWORD entry=GetLastError();SetLastError(entry);const bool result=g_exit(self,body,record);const DWORD after=GetLastError();
 if(!body){detail::Lease lease{};Current current{};
  if(LoadLease(lease)&&lease.committed&&CurrentScope(current)&&CurrentLease(lease,current)&&
   lease.primary==reinterpret_cast<std::uintptr_t>(self)&&lease.record==reinterpret_cast<std::uintptr_t>(record)){
   detail::Unit unit{};std::uintptr_t map=0,attached=1;
   if(CurrentUnit(current,lease.vehicle.vehicle,lease.vehicle.unit,unit)&&unit.rider[lease.seat]==0xffff&&
    Read(current.player.P+0x120,map)&&map&&Read(map,attached)&&!attached)g_exits.fetch_add(1);
  }
 }
 // 1247700's true return alone does not prove removal: it returns true even
 // when P120 was null. Status additionally requires the matching empty slot.
 SetLastError(after);return result;
}
bool InstallGateway() noexcept {
 unsigned char raw[6]{};if(!ReadBytes(g_base+kJcc,raw,sizeof(raw)))return false;
 const unsigned char expected[]={0x0f,0x85,0x6b,0x04,0,0};if(std::memcmp(raw,expected,sizeof(raw)))return false;
 void* buffer=near_code::AllocateNear(g_base+kJccNext,4096);if(!buffer)return false;
 const auto length=detail::BuildGateway(static_cast<unsigned char*>(buffer),4096,reinterpret_cast<std::uintptr_t>(&OccupiedGateway),g_base+kJccNext,g_base+kJccFail);
 if(!length||length>sizeof(g_gatewayBytes)){VirtualFree(buffer,0,MEM_RELEASE);return false;}
 std::memcpy(g_gatewayBytes,buffer,length);g_gatewayLength=length;
 DWORD prior=0;if(!VirtualProtect(buffer,4096,PAGE_EXECUTE_READ,&prior)){VirtualFree(buffer,0,MEM_RELEASE);return false;}
 FlushInstructionCache(GetCurrentProcess(),buffer,length);
 std::int32_t displacement=0;if(!near_code::Rel32(g_base+kJccNext,reinterpret_cast<std::uintptr_t>(buffer),displacement)){VirtualFree(buffer,0,MEM_RELEASE);return false;}
 const auto value=static_cast<std::uint32_t>(displacement);
 const char* failure=near_code::PatchDword(reinterpret_cast<void*>(g_base+kJcc+2),kOriginalDisp,value);
 std::uint32_t actual=0;if(failure){
  if(Read(g_base+kJcc+2,actual)&&actual==value){g_gateway=buffer;g_gatewayDisp=value;return false;}
  VirtualFree(buffer,0,MEM_RELEASE);return false;
 }
 // Process-lifetime allocation: a game thread may still be inside the gateway.
 g_gateway=buffer;g_gatewayDisp=value;return true;
}
} // namespace

bool RegisterNativePoseObserver(NativePoseObserver observer) noexcept {
 const Preserve preserve;if(!observer||g_base||g_installed.load())return false;
 if(g_poseObserver)return g_poseObserver==observer;g_poseObserver=observer;return true;
}
bool OverlayOriginal(std::uintptr_t at,void* output,std::size_t size) noexcept {
 const Preserve preserve;if(!output||!detail::Range(at,size))return false;
 if(!g_installed.load())return true;if(!PatchesLive())return false;auto read=ReadBytes;
 if(!hooks::Overlay(read,g_base,g_hooks,g_originalHeads,at,output,size))return false;
 const auto begin=g_base+kJcc+2,end=begin+4,low=at>begin?at:begin,high=at+size<end?at+size:end;
 if(low<high){unsigned char original[4]{};const auto displacement=kOriginalDisp;std::memcpy(original,&displacement,4);
  std::memcpy(static_cast<unsigned char*>(output)+low-at,original+low-begin,high-low);}
 return PatchesLive();
}
bool Install(const void* image,std::size_t span,bool accepted,Log log) noexcept {
 const Preserve preserve;if(g_installed.load())return true;
 if(g_base||!accepted||!image||span<0xa01d000||!worker_scenario::Compiled())return false;
 g_base=reinterpret_cast<std::uintptr_t>(image);g_span=span;g_log=log;g_addresses=partner_player::AddressesFor(image);
 if(!Gate()){Emit("coop_passenger=install_refused; reason=exact_code\r\n");return false;}
 const auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)return false;
 void* wrappers[]={reinterpret_cast<void*>(&HookCanExec),reinterpret_cast<void*>(&HookNearest),reinterpret_cast<void*>(&HookPhase1),reinterpret_cast<void*>(&HookPose),reinterpret_cast<void*>(&HookExit)};
 void** originals[]={reinterpret_cast<void**>(&g_canExec),reinterpret_cast<void**>(&g_nearest),reinterpret_cast<void**>(&g_phase1),reinterpret_cast<void**>(&g_pose),reinterpret_cast<void**>(&g_exit)};
 const std::uintptr_t rvas[]={kCanExec,kNearest,kPhase1,kPose,kExit};
 for(unsigned i=0;i<5;++i){g_hooks[i].rva=rvas[i];g_hooks[i].detour=reinterpret_cast<std::uintptr_t>(wrappers[i]);
  if(!partner_input_hook::ReadNativePoseOriginalCode(g_base+rvas[i],g_originalHeads[i],16))return false;}
 unsigned made=0;for(;made<5;++made)if(MH_CreateHook(reinterpret_cast<void*>(g_base+g_hooks[made].rva),wrappers[made],originals[made])!=MH_OK)break;
 if(made!=5){while(made)MH_RemoveHook(reinterpret_cast<void*>(g_base+g_hooks[--made].rva));return false;}
 // The gate is installed first while its callback is strictly disabled. It
 // cannot bypass the original occupied-driver rejection until all hooks own
 // their patches and a private fresh gameplay tick enables the prototype.
 if(!InstallGateway()){for(auto& hook:g_hooks)MH_RemoveHook(reinterpret_cast<void*>(g_base+hook.rva));return false;}
 unsigned enabled=0;for(;enabled<5;++enabled)if(MH_EnableHook(reinterpret_cast<void*>(g_base+g_hooks[enabled].rva))!=MH_OK)break;
 if(enabled!=5){for(unsigned i=0;i<enabled;++i)MH_DisableHook(reinterpret_cast<void*>(g_base+g_hooks[i].rva));
  for(auto& hook:g_hooks)MH_RemoveHook(reinterpret_cast<void*>(g_base+hook.rva));return false;}
 auto read=ReadBytes;for(unsigned i=0;i<5;++i){g_hooks[i].trampoline=reinterpret_cast<std::uintptr_t>(*originals[i]);
  if(!hooks::Seal(read,g_base,g_hooks[i]))return false;}

 g_installed.store(true);Emit("coop_passenger=installed; enabled=0; body=0\r\n");return true;
}
void Tick(std::uint32_t mission,std::uint16_t epoch,bool admission) noexcept {
 const Preserve preserve;if(!g_installed.load()||g_stopped.load())return;
 const auto owner=GetCurrentThreadId();const auto previousOwner=g_owner.load();
 const bool worker=worker_scenario::ThreadAdmitted();
 const bool scene=epoch&&(mission==30010||mission==30020||mission==30050);
 const bool thread=!previousOwner||previousOwner==owner;
 const bool valid=admission&&worker&&scene&&thread;
 g_tickReason.store(!admission?"admission":!worker?"worker":!scene?"scene":!thread?"thread":"ok");
 if(!valid){g_tickAt.store(0);detail::Lease lease{};if(!CopyLease(lease)||lease.valid)g_lost.store(true);return;}
 if(g_mission.load()&& (g_mission.load()!=mission||g_epoch.load()!=epoch)){
  detail::Lease lease{};if(!CopyLease(lease)||lease.valid){g_lost.store(true);g_tickAt.store(0);return;}
 }
 g_owner.store(owner);g_mission.store(mission);g_epoch.store(epoch);g_tickAt.store(GetTickCount64());
 detail::Lease lease{};Current current{};
 if(LoadLease(lease)&&CurrentScope(current,false)&&CurrentLease(lease,current)){
  const auto& body=current.controller.body[0];std::uintptr_t map=0,attached=1;detail::Unit unit{};
  const bool idle=!body.raw[0x14]&&!body.raw[0x15]&&!(body.bitmapValue&1u)&&
   Read(current.player.P+0x120,map)&&map&&Read(map,attached)&&!attached;
  const bool expired=!lease.committed&&!detail::Fresh(GetTickCount64(),lease.issued,1500)&&idle;
  const bool retired=lease.committed&&!body.raw[0x14]&&!body.raw[0x15]&&!(body.bitmapValue&1u)&&
   Read(current.player.P+0x120,map)&&map&&Read(map,attached)&&!attached&&
   CurrentUnit(current,lease.vehicle.vehicle,lease.vehicle.unit,unit)&&unit.rider[lease.seat]==0xffff;
  if((expired||retired)&&TryAcquireSRWLockExclusive(&g_lock)){
   if(detail::SameIdentity(lease,g_lease))g_lease={};ReleaseSRWLockExclusive(&g_lock);
   if(retired)Emit("coop_passenger=retired; body=0; slot_empty=1\r\n");
  }
 }
}
bool SetEnabled(bool on) noexcept {
 const Preserve preserve;Current current{};
 if(on){if(!CurrentScope(current,false))return false;g_enabled.store(true);return true;}
 detail::Lease lease{};if(!CopyLease(lease)||lease.valid)return false;
 g_enabled.store(false);return true;
}
bool Enabled() noexcept {return g_enabled.load();}
bool LifetimeCensusLine(char* out,std::size_t cap) noexcept {
 const Preserve preserve;if(!out||!cap)return false;Current before{},after{};
 detail::EngineRead e{},late{};vehicle_lifetime_read::UI a{},b{};
 native_pose_carry_participants::Pair models{},again{};vehicle_lifetime_read::CallbackCensus callbacks{},last{};
 vehicle_lifetime_read::Allocation allocation{},current{};auto reader=ReadBytes;
 const char* why="life_read";bool coherent=false,typed=false;
 if(!CurrentScope(before,false))why=before.reason;
 else if(!detail::ReadEngine(reader,g_base,g_span,before.player.P,e))why="engine_read";
 else if(!vehicle_lifetime_read::ReadUI(reader,g_base,g_span,before.player.P,e,a))why="ui_read";
 else if(!native_pose_carry_participants::ReadPair(reader,g_base,before.player.P,models)||
  !vehicle_lifetime_read::Callbacks(g_base,models,before.controller,callbacks))why="callback_read";
 else{
  const bool first=vehicle_lifetime_read::ReadAllocation(reader,g_base,g_span,before.service,allocation);
  if(!CurrentScope(after,false))why=after.reason;
  else if(before.sequence!=after.sequence||before.service!=after.service||before.player.P!=after.player.P||
   before.player.Q!=after.player.Q||before.player.S!=after.player.S||before.owner.generation!=after.owner.generation||
   before.owner.mission!=after.owner.mission||before.owner.epoch!=after.owner.epoch||
   !vehicle_controller_read::Same(before.controller,after.controller))why="scope_changed";
  else if(!detail::ReadEngine(reader,g_base,g_span,after.player.P,late)||!detail::Same(e,late)||
   !vehicle_lifetime_read::ReadUI(reader,g_base,g_span,after.player.P,late,b)||!vehicle_lifetime_read::Same(a,b)||
   !native_pose_carry_participants::ReadPair(reader,g_base,after.player.P,again)||
   !native_pose_carry_participants::Same(models,again)||
   !native_pose_carry_participants::CoherentCallbacks(models.body[0],again.body[0])||
   !native_pose_carry_participants::CoherentCallbacks(models.body[1],again.body[1])||
   !vehicle_lifetime_read::Callbacks(g_base,again,after.controller,last))why="life_changed";
  else{
   const bool second=vehicle_lifetime_read::ReadAllocation(reader,g_base,g_span,after.service,current);
   coherent=partner_input_hook::WindowSeq()==after.sequence&&first==second;
   typed=coherent&&first&&vehicle_lifetime_read::Same(allocation,current);why=coherent?"ok":"life_changed";
  }
 }
 const auto rva=[](std::uintptr_t p) noexcept {return p>=g_base&&p-g_base<g_span?p-g_base:0;};
 const int n=_snprintf_s(out,cap,_TRUNCATE,
  "plife;read=%u;why=%s;cb=%u,%u;mask=%u,%u;free=%u,%u;dv=%llx;iv=%llx;get=%llx;alloc=%u;n=%u;head=%u;rec=%u",
  coherent?1u:0u,why,callbacks.present[0],callbacks.present[1],callbacks.eventMask[0],callbacks.eventMask[1],
  callbacks.free[0],callbacks.free[1],static_cast<unsigned long long>(rva(a.dispatcherVT)),
  static_cast<unsigned long long>(rva(a.providerVT)),static_cast<unsigned long long>(rva(a.getter)),typed?1u:0u,
  unsigned(allocation.count),allocation.descriptors,allocation.recordMatches);return n>=0;
}
bool ResourceCensusLine(char* out,std::size_t cap) noexcept {
 const Preserve preserve;if(!out||!cap)return false;Current before{},after{};
 detail::EngineRead e{},late{};detail::Unit unit{},current{};vehicle_resource_lifetime::Owner a{},b{};auto reader=ReadBytes;
 const char* why="resource_read";bool observed=false,typed=false;
 if(!CurrentScope(before,false))why=before.reason;
 else{
  const auto id=vehicle_controller_read::Word(before.controller.body[0].raw+0x44);
  if(!detail::ReadEngine(reader,g_base,g_span,before.player.P,e)||!e.bound[0]||!CurrentUnit(before,id,e.bound[0],unit))why="local_not_riding";
  else{
   const bool first=vehicle_resource_lifetime::Read(reader,g_base,unit,a);
   if(!CurrentScope(after,false))why=after.reason;
   else if(before.sequence!=after.sequence||before.service!=after.service||before.player.P!=after.player.P||
    before.player.Q!=after.player.Q||before.player.S!=after.player.S||before.owner.generation!=after.owner.generation||
    before.owner.mission!=after.owner.mission||before.owner.epoch!=after.owner.epoch||
    !vehicle_controller_read::Same(before.controller,after.controller))why="scope_changed";
   else if(!detail::ReadEngine(reader,g_base,g_span,after.player.P,late)||!detail::Same(e,late)||
    !CurrentUnit(after,id,late.bound[0],current)||!detail::Same(unit,current))why="unit_changed";
   else{
    const bool second=vehicle_resource_lifetime::Read(reader,g_base,current,b);
    observed=first==second&&vehicle_resource_lifetime::Same(a,b)&&partner_input_hook::WindowSeq()==after.sequence;
    typed=observed&&second;why=observed?"ok":"resource_changed";
   }
  }
 }
 const auto rva=[](std::uintptr_t p) noexcept {return p>=g_base&&p-g_base<g_span?p-g_base:0;};
 const int n=_snprintf_s(out,cap,_TRUNCATE,
  "pres;read=%u;type=%u;why=%s;vt=%llx;sv=%llx;n=%u;owner=%u;rec=%u;bits=%u;meta=%u;tag=%x;bytes=%llu",
  observed?1u:0u,typed?1u:0u,why,static_cast<unsigned long long>(rva(a.vtable)),
  static_cast<unsigned long long>(rva(a.secondaryVT)),unsigned(a.count),a.vehicleObject==unit.object&&a.vehicleObject?1u:0u,
  a.records==unit.records&&a.records?1u:0u,a.bitmap==unit.bitmap&&a.bitmap?1u:0u,
  a.metadata==unit.metadata&&a.metadata?1u:0u,a.tag,static_cast<unsigned long long>(a.bytes));return n>=0;
}
bool CameraCensusLine(char* out,std::size_t cap) noexcept {
 const Preserve preserve;if(!out||!cap)return false;Current before{},after{};
 vehicle_cleanup_camera_read::Owner a{},b{};auto read=ReadBytes;bool ready=false;const char* why="camera_read";
 if(!CurrentScope(before,false))why=before.reason;
 else if(!vehicle_cleanup_camera_read::Read(read,g_base,before.player.P,before.player.Q,a))why="camera_owner";
 else if(!CurrentScope(after,false)||before.sequence!=after.sequence||before.player.P!=after.player.P||
  before.player.Q!=after.player.Q||before.player.S!=after.player.S||before.owner.mission!=after.owner.mission||
  before.owner.epoch!=after.owner.epoch||before.owner.generation!=after.owner.generation||
  !vehicle_controller_read::Same(before.controller,after.controller))why="camera_scope_changed";
 else if(!vehicle_cleanup_camera_read::Read(read,g_base,after.player.P,after.player.Q,b)||
  !vehicle_cleanup_camera_read::Same(a,b)||partner_input_hook::WindowSeq()!=after.sequence)why="camera_changed";
 else{ready=true;why="ok";}
 const int n=_snprintf_s(out,cap,_TRUNCATE,"pcamera;read=%u;why=%s;component=%u;resource=%u;interface=%u;local=%u,%u;flags=%x",
  ready?1u:0u,why,a.component?1u:0u,a.resource?1u:0u,a.interface?1u:0u,a.realLocal,a.resourceLocal,a.flags);return n>=0;
}
bool LookupCensusLine(char* out,std::size_t cap) noexcept {
 const Preserve preserve;if(!out||!cap)return false;Current first{},after{};detail::Unit u{},again{};
 detail::EngineRead e{},final{};vehicle_registry_read::Lookup a{},b{};auto reader=ReadBytes;
 const char* why="lookup_read";bool observed=false,callable=false;
 if(!CurrentScope(first,false))why=first.reason;
 else{
  const auto id=vehicle_controller_read::Word(first.controller.body[0].raw+0x44);
  if(!detail::ReadEngine(reader,g_base,g_span,first.player.P,e)||!e.bound[0]||!CurrentUnit(first,id,e.bound[0],u))why="local_not_riding";
  else if(vehicle_registry_read::Observe(reader,g_base,g_span,e,u,a)){
   const bool ca=vehicle_registry_read::Callable(reader,g_base,u,a);
   if(!CurrentScope(after,false))why=after.reason;
   else if(first.sequence!=after.sequence||first.player.P!=after.player.P||first.player.Q!=after.player.Q||first.player.S!=after.player.S||
    first.service!=after.service||first.owner.mission!=after.owner.mission||first.owner.epoch!=after.owner.epoch||
    first.owner.generation!=after.owner.generation||!vehicle_controller_read::Same(first.controller,after.controller))why="scope_changed";
   else if(!detail::ReadEngine(reader,g_base,g_span,after.player.P,final)||!detail::Same(e,final)||
    !CurrentUnit(after,id,final.bound[0],again)||!detail::Same(u,again)||
    !vehicle_registry_read::Observe(reader,g_base,g_span,final,again,b))why="lookup_changed";
   else{
    const bool cb=vehicle_registry_read::Callable(reader,g_base,again,b);
    observed=vehicle_registry_read::Same(a,b)&&ca==cb&&partner_input_hook::WindowSeq()==after.sequence;
    callable=observed&&cb;why=observed?"ok":"lookup_changed";
   }
  }
 }
 const auto rva=[](std::uintptr_t p) noexcept {return p>=g_base&&p-g_base<g_span?p-g_base:0;};
 const int n=_snprintf_s(out,cap,_TRUNCATE,
  "plook;read=%u;type=%u;why=%s;qv=%llx;qf=%llx;owner=%u;mode=%u;inner=%llx;n=%u,%u;records=%u;bits=%u",
  observed?1u:0u,callable?1u:0u,why,
  static_cast<unsigned long long>(rva(a.queryVT)),static_cast<unsigned long long>(rva(a.querySlot)),
  a.queryOwner==a.primary&&a.queryOwner?1u:0u,unsigned(a.queryMode),
  static_cast<unsigned long long>(rva(a.innerQuerySlot)),unsigned(a.count),unsigned(a.queryCount),
  a.queryRecords==u.records&&a.queryRecords?1u:0u,a.queryBits==u.bitmap&&a.queryBits?1u:0u);return n>=0;
}
bool EngineCensusLine(char* out,std::size_t cap) noexcept {
 const Preserve preserve;if(!out||!cap)return false;
 Current before{},after{};detail::EngineRead a{},b{};auto reader=ReadBytes;
 const char* reason="engine_read";bool coherent=false;bool typed[2]{};
 detail::Unit unit[2]{};
 if(!CurrentScope(before,false))reason=before.reason;
 else if(detail::ReadEngine(reader,g_base,g_span,before.player.P,a)){
  for(unsigned id=0;id<2;++id){const auto vehicle=vehicle_controller_read::Word(before.controller.body[id].raw+0x44);
   if(a.bound[id])typed[id]=CurrentUnit(before,vehicle,a.bound[id],unit[id]);
  }
  if(!CurrentScope(after,false))reason=after.reason;
  else if(before.sequence!=after.sequence||before.service!=after.service||before.player.P!=after.player.P||
   before.player.Q!=after.player.Q||before.player.S!=after.player.S||before.owner.mission!=after.owner.mission||
   before.owner.epoch!=after.owner.epoch||before.owner.generation!=after.owner.generation||
   !vehicle_controller_read::Same(before.controller,after.controller))reason="scope_changed";
  else if(!detail::ReadEngine(reader,g_base,g_span,after.player.P,b)||!detail::Same(a,b))reason="engine_changed";
  else{
   coherent=true;reason="ok";
   // The typed unit is re-read within the same owner closure after both registry
   // samples. No native query, binder, seat or state transition is invoked.
   for(unsigned id=0;id<2;++id)if(typed[id]){
    detail::Unit current{};typed[id]=CurrentUnit(after,unit[id].vehicle,b.bound[id],current)&&detail::Same(unit[id],current);
   }
   detail::EngineRead finalEngine{};const auto final=downed_player::GetSnapshot();
   if(partner_input_hook::WindowSeq()!=after.sequence||!final.ready||!final.armed||final.down||
    final.mission!=after.owner.mission||final.epoch!=after.owner.epoch||final.generation!=after.owner.generation||
    !detail::ReadEngine(reader,g_base,g_span,after.player.P,finalEngine)||!detail::Same(b,finalEngine)){
    coherent=false;reason="owner_changed";
   }
  }
 }
 if(!coherent){const int n=_snprintf_s(out,cap,_TRUNCATE,"pengine;read=0;why=%s",reason);return n>=0;}
 const int n=_snprintf_s(out,cap,_TRUNCATE,
  "pengine;read=1;vt=%llx;query=%llx;p120=%u,%u;typed=%u,%u;b0=%u,%u;b1=%u,%u;r=%u,%u",
  static_cast<unsigned long long>(b.vtable-g_base),static_cast<unsigned long long>(b.query-g_base),
  a.bound[0]?1u:0u,a.bound[1]?1u:0u,typed[0]?1u:0u,typed[1]?1u:0u,
  unsigned(after.controller.body[0].raw[0x14]),unsigned(vehicle_controller_read::Word(after.controller.body[0].raw+0x44)),
  unsigned(after.controller.body[1].raw[0x14]),unsigned(vehicle_controller_read::Word(after.controller.body[1].raw+0x44)),
  unsigned(unit[0].rider[0]),unsigned(unit[1].rider[0]));return n>=0;
}
bool SeatCensusLine(char* out,std::size_t cap) noexcept {
 const Preserve preserve;if(!out||!cap)return false;Current current{};detail::Unit unit{};
 bool valid=false,scope=false;unsigned phase=0,seat=4;std::uint16_t vehicle=0xffff;
 std::uintptr_t observedVT=0,observedGetter=0;std::uint16_t observedCount=0;
 if(CurrentScope(current,false)){
  scope=true;
  const auto& b=current.controller.body[0];phase=b.raw[0x14];seat=b.raw[0x17]&15u;
  vehicle=vehicle_controller_read::Word(b.raw+0x44);std::uintptr_t map=0,bound=0;
  if(Read(current.player.P+0x120,map)&&map&&Read(map,bound)&&bound){
   valid=CurrentUnit(current,vehicle,bound,unit);
   std::uintptr_t object=0;if(Read(current.service+0xb0,object)&&detail::Range(object,0x1c8)){
    Read(object,observedVT);Read(object+0x1c0,observedCount);
    if(observedVT>=g_base&&observedVT-g_base<g_span)Read(observedVT,observedGetter);
   }
  }
 }
 if(!scope){
  const int n=_snprintf_s(out,cap,_TRUNCATE,
   "pseats;scope=0;ready=0;why=%s;tick=%s;stock=%u,%u;target=%u;near=%u,%u,%u,%u",
   current.reason,g_tickReason.load(),g_stockChecks.load(),g_stockAccepted.load(),g_stockTarget.load(),
   g_stockNearest.load(),g_stockFirst.load(),g_stockLast.load(),g_stockResult.load());return n>=0;
 }
 const auto vtRva=observedVT>=g_base&&observedVT-g_base<g_span?observedVT-g_base:0;
 const auto getterRva=observedGetter>=g_base&&observedGetter-g_base<g_span?observedGetter-g_base:0;
 const int n=_snprintf_s(out,cap,_TRUNCATE,
  "pseats;scope=%u;ready=%u;phase=%u;seat=%u;veh=%u;count=%u;alloc=%u;type=%u;r=%u,%u,%u,%u;spare=%u;vt=%llx;fn=%llx;n=%u",
  scope?1u:0u,valid?1u:0u,phase,seat,unsigned(vehicle),unsigned(unit.count),unsigned(unit.allocated),unsigned(unit.type),
  unsigned(unit.rider[0]),unsigned(unit.rider[1]),unsigned(unit.rider[2]),unsigned(unit.rider[3]),unsigned(detail::EmptyPassenger(unit)),
  static_cast<unsigned long long>(vtRva),static_cast<unsigned long long>(getterRva),unsigned(observedCount));
 return n>=0;
}
bool StatusLine(char* out,std::size_t cap) noexcept {
 const Preserve preserve;if(!out||!cap)return false;detail::Lease lease{};LoadLease(lease);
 Current current{};if(!CurrentScope(current,false)){
  const int n=_snprintf_s(out,cap,_TRUNCATE,
   "passenger;on=%u;en=%u;lost=%u;lease=%u;commit=%u;scope=0;why=%s;tick=%s;stock=%u,%u;allow=%llu",
   g_installed.load()?1u:0u,g_enabled.load()?1u:0u,g_lost.load()?1u:0u,lease.valid?1u:0u,lease.committed?1u:0u,
   current.reason,g_tickReason.load(),g_stockChecks.load(),g_stockAccepted.load(),g_overrides.load());return n>=0;
 }
 const int n=_snprintf_s(out,cap,_TRUNCATE,
  "passenger;on=%u;en=%u;lost=%u;lease=%u;commit=%u;seat=%u;veh=%u;pred=%llu;allow=%llu;pre=%llu;entry=%llu;attach=%llu;exit=%llu;ref=%llu",
  g_installed.load()?1u:0u,g_enabled.load()?1u:0u,g_lost.load()?1u:0u,lease.valid?1u:0u,lease.committed?1u:0u,
  unsigned(lease.seat),unsigned(lease.vehicle.vehicle),g_predicates.load(),g_overrides.load(),g_prequeries.load(),g_commits.load(),g_attachments.load(),g_exits.load(),g_refused.load());
 return n>=0;
}
bool Shutdown() noexcept {
 const Preserve preserve;detail::Lease lease{};if(!CopyLease(lease)||lease.valid)return false;
 g_enabled.store(false);g_stopped.store(true);return true;
}
} // namespace mgscoop::coop_passenger
