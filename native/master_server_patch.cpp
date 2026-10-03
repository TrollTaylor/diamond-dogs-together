// Master-server URL redirect (interface stub; implementation pending).
#include "mgscoop/master_server_patch.hpp"
namespace mgscoop::master_server {
bool Install(void*, std::size_t, const char* url, void (*log)(const char*) noexcept) noexcept {
    if (!url || !*url) return true;
    if (log) log("master_server=refused; reason=not_implemented\r\n");
    return false;
}
}
