#pragma once
#include "worker_protocol.hpp"
#include "mgscoop/lab_config.hpp"

namespace mgsworker {
// Pure admission subset. OS path/handle/actual redirect/mount identity remains
// mandatory in supervisor and in-process bridge, separately from declarations.
inline bool OfflineLab(const mgscoop::LabConfig& lab, const std::string& role, const std::string& save) {
    const auto expected = role == "host" ? mgscoop::LabRole::host : mgscoop::LabRole::guest;
    return (role == "host" || role == "guest") && lab.present && lab.role == expected && lab.mode == mgscoop::LabMode::lab &&
        lab.loopback && lab.xinput == mgscoop::kXInputNone && lab.invalid_values == 0 && lab.mutex_suffix[0] &&
        AbsolutePath(lab.save_dir) && EqualFold(lab.save_dir, save) && lab.partner && lab.net && lab.net_bind_loopback &&
        lab.net_bind_address == 0x7f000001 && lab.net_peer_address == 0x7f000001 &&
        lab.partner_source == mgscoop::PartnerSource::net && lab.partner_apply == mgscoop::PartnerApply::input &&
        lab.partner_input == mgscoop::PartnerInput::net && lab.partner_input_send;
}
} // namespace mgsworker
