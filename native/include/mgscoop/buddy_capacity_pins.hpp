#pragma once
#include "mgscoop/buddy_pose.hpp"
namespace mgscoop::buddy_capacity_routes {
using Pin=buddy_pose::Pin;
inline constexpr Pin kPins[]={
 {0xbf99e0,34,"76eadda6f658a477094e637c28f73db48a3b809cac9bd09e310af883f76c0a13"}, // pure-horse-census-v2 exact census body
 {0xbf9a10,8,"0eb5b43389c356e1e8636cc907f5864472bb505c38fe604a3c8681722dfadf2f"}, // pure-horse-census-v2 exact census body
 {0xbf8a20,102,"01f5e3dcecc8b7351c2e1958b2f28a5aa3839f59dceb86bcf6a43a0b97a0f5e7"}, // pure-horse-census-v2 exact census body
 {0x9e4b90,188,"b57816d5dd3f337dc231a12d59ab22ac0dde1ab7d09a26360a209fe7a81bb150"}, // pure-horse-census-v2 exact census body
 {0x9e5470,279,"28190465380f73f484b50fdaa8a1258c5367dbcdb6aee72d7862ba1829c81cfa"}, // pure-horse-census-v2 exact census body
 {0x9e6060,107,"c62ef3483d7ecea21b938bf5d2e3293da6c79a31901d7b483179fc054687f3ec"}, // pure-horse-census-v2 exact census body
 {0xe4c450,37,"efbd95908ff2eaedd5254095f15c93dc5e89e04507ae5ddb2ea6eef2552957f0"}, // pure-horse-census-v2 exact census body
 {0xe7c680,156,"7c7332c1f782af774725b2c8d52bf9b68b66542c53a42a34399a813a7b951ca8"}, // pure-horse-census-v2 exact census body
 {0xe7c720,37,"9586fe3239ca0adcf23183f6e7ade1dae35b53ccf030589454398a86b620a4fe"}, // pure-horse-census-v2 exact census body
 {0xe7c750,123,"b41dabc7c3ceb54236ad13db68cc9abd16539cb6758143d012bae4a0a2fed262"}, // pure-horse-census-v2 exact census body
 {0xe4b1d0,527,"c370b2d03484d1379e8680b91e638effdaf4681b2f668d3bd1f0748e9aec25b4"}, // pure-horse-census-v2 exact census body
 {0xe4b9b0,92,"e5bfd385931ac93351ad631ddc6b771369935c939a0787fd50e380172fb0c39c"}, // pure-horse-census-v2 exact census body
 {0xa46030,87,"a34b19b8aed39371ac1e5565bde9275af0bdab9d21fb9beb89dfab25489eb025"}, // pure-horse-census-v2 exact census body
 {0xa81980,280,"225b1b0cef0e8d7d11be8dcaab2e7b40a21648f0e52a98ec3a5670a128b0d1eb"}, // pure-walker-census-v1 exact census body
 {0xa82350,1203,"f2ac07e4a914f05917154f488f594ee9f5c40fb9d87b25c64c90772a6234b0aa"}, // pure-walker-census-v1 exact census body
 {0xa83670,110,"987cc412fdf25787606825540449723293003ae825c05522b36412f2ce0fc1a4"}, // pure-walker-census-v1 exact census body
 {0x1701a60,531,"423bd6b083dc0c6826d1b3d6173bd0096e14d683e5590378095313e26c7c8d7e"}, // pure-walker-census-v1 exact census body
 {0x1704670,37,"26ba6faf759023e8cce04573d4a345ff26a7ceafb00349146a1fe5fb2ccb5dd4"}, // pure-walker-census-v1 exact census body
 {0x1704900,150,"2c688934864303c3e2e9519d02cfca1b31b75e411eec93ec6182a1161feb75b0"}, // pure-walker-census-v1 exact census body
 {0x1704d53,25,"5a63214577a282e825775659579eec63d8724e46e5d1121491cbacece8cbcf0f"}, // pure-walker-census-v1 exact census body
 {0x1705014,82,"4bcd8414ba1e76c43c874571ee033c891e7d2b81ec914a1178aa0a8e4cb7ed02"}, // pure-walker-census-v1 exact census body
 {0x1704250,18,"1a52e6723f65c73ab7e6fc5c3c43b775c04984bc5dd79547d4e61cd7d557d72a"}, // pure-walker-census-v1 exact census body
 {0x17016b0,905,"4e890cb4cf9db14ec1eb35de77072ad6f2c62aae694b7b87210529185bf5b366"}, // pure-walker-census-v1 exact census body
 {0x1706190,600,"8bcb246e91034dcc507593187f345ab7688908587746dee202b3692fc66dfbf5"}, // pure-walker-census-v1 exact census body
 {0xa45be0,839,"a5052216ef561a55e68c3ed4c68bc47496478195050975fc2764ce064a3bb439"}, // buddy registered Lua table
 {0xa44560,311,"2730a8e95ebf6cfcc9eccf97db2d39967ce99bc281a302d0c021584a1886d6c6"}, // complete CallBuddy wrapper including cold body
 {0xa44740,336,"63f12cfcb7e90355a62705e9606f00f3a9a4eaabee61e2c0b79bcc234592c827"}, // singleton buddy selector
 {0x1706190,2890,"e79ce6e32235a3d7c69c37534666423f2080a5ed94688b4843ebbcc45040bafa"}, // Walker full Lua dispatcher including cold body
 {0xe4d250,1806,"ebc78c5b7f8feeda50123b42aaa141b5817261da598cc06ba0f19f2968bc5b84"}, // Horse Lua dispatcher
 {0x170d410,790,"9d83c98473852f55b63d3ed94f18aa842d5f0ce811b7dad0e802b92654640a03"}, // walker SetPosition transform helper
 {0xbfa477,137,"180d7f4974a058dfc49636db1a9eff760b8aa3ad00a8a1e68b4f292b3a983ea1"}, // full generic SendCommand natural caller
 {0xa47060,90,"2e4779eeae72b10092f644c49af74357ddc837be87c6ed1271b3ba1e89b3a6a4"}, // full IsBlockActive wrapper
 {0xa46dc0,540,"5b5fb620192a34acc9eb2ad4a297952c50b00cf2f5fd1f09b7f024145f70eed5"}, // exact registered BuddyType enum
 {0x17033b0,88,"ad29e19158e804b0a0de865cf7f4c07d64bb346aec7d323e54ad4651bebba475"}, // exact final derived Walker factory; its stored type is 0x2442920/0x2442960, not the construction-only pair
};
}
