#pragma once
#include "mgscoop/buddy_pose.hpp"
namespace mgscoop::buddy_dog_commands {
using Pin=buddy_pose::Pin;
inline constexpr Pin kPins[]={
 {0xc7aea0,103,"cf325f7e97c30491e155c58c72bc8e7292ddc1d4dc3042e88ceba166d1611911"}, // final behavior factory and complete vtable binding
 {0xc7d220,1536,"8acce9eed4df70303b01479a6932256f07607dddfb7737ec0588cc4b4f201a5a"}, // full Dog Lua dispatcher including all cold bodies and LuaAiStayAndSnarl
 {0xc7da10,966,"6c9c1a1458a17e7f9e0bffea434b7fd8629528ee2389b0cad7b52863e6381e03"}, // full persistent-target behavior incl omission cleanup and optional warp
 {0xc7e650,9999,"194b84de30c58b822bbe41effbc17e04c4ef232680d9142ecdfafc261c80142c"}, // full Dog AI update reading persistent target mode
 {0xc0ef70,92,"d331a3313de61f183575442d874fbe3b00f263ed2cc61a99d5dd515618addce1"}, // generic AI record component exact final factory
 {0xc0efd0,14,"f27f9aab01ee62e6149d1740f6d3204af65fd48501ab043867b046c37840949d"}, // generic AI record indexed getter
 {0xc0efe0,41,"c1fd7d1278473cc004f34ef0198898192635e9932ad9066a19b735c5b5cc1861"}, // generic AI record bounded index map getter
 {0xc0f010,37,"138b9e1d3fc4866f2b56a75cb186c44ad292cf7cacf057e98773bd5b405c1210"}, // generic AI record exact typed cast
 {0xc0f5c0,789,"623a4963ea3d84f1e761d12e9710529ee96af88ea38c5293446b98b8384fb88b"}, // generic AI record complete layout/allocation body
 {0xc0f570,76,"7875e01b5656c1bf4732cf65a14ad2038a7e6bba790a7324a742b4874b83c309"}, // generic AI record bounded active-mask update
 {0xc56047,103,"0c1b4cc7531b24918a2f3caa7f1ad94f74eb88bac52c62929ecf37f03666500f"}, // Dog creates count-owned typed generic AI record component
 {0xa44560,311,"2730a8e95ebf6cfcc9eccf97db2d39967ce99bc281a302d0c021584a1886d6c6"}, // qualified complete CallBuddy wrapper
 {0xa44740,336,"63f12cfcb7e90355a62705e9606f00f3a9a4eaabee61e2c0b79bcc234592c827"}, // qualified complete singleton selector
 {0xa47060,90,"2e4779eeae72b10092f644c49af74357ddc837be87c6ed1271b3ba1e89b3a6a4"}, // qualified full IsBlockActive wrapper
 {0xbfa477,137,"180d7f4974a058dfc49636db1a9eff760b8aa3ad00a8a1e68b4f292b3a983ea1"}, // generic natural SendCommand dispatcher caller
};
}
