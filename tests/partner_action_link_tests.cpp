// Real production link handshake/MAC/replay/body codecs over an in-memory
// datagram pipe, including the exact action mailbox used by partner_net.
// No synthetic accepted-delivery flag, Windows sockets or game processes.
#include "mgscoop/partner_action_mailbox.hpp"
#include "mgscoop/partner_net.hpp"
#include "mgscoop/player_sync.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace a = mgscoop::partner_actions;
namespace l = mgscoop::link;
namespace s = mgscoop::sync;
unsigned checks = 0;
void Check(bool ok,const char* why) { ++checks; if (!ok) { std::fprintf(stderr,"FAIL %u: %s\n",checks,why); std::exit(1); } }
struct Datagram { mgscoop::lab::Endpoint from{}, to{}; std::vector<std::uint8_t> bytes; };
struct Node {
    l::Session session; mgscoop::lab::Endpoint self{}; std::vector<Datagram> outbox; std::uint64_t rng = 1;
    a::Mailbox inbox{}; unsigned accepted = 0, poses = 0, resets = 0;
    void On(const Datagram& d,std::uint64_t now) {
        const auto delivery = session.OnDatagram(d.bytes.data(),d.bytes.size(),d.from,now);
        if (delivery.new_session) { a::Reset(inbox); ++resets; }
        std::uint64_t peer = 0;
        if (!a::TrustedPeer(true,delivery.data,d.from,{},peer)) return;
        unsigned count = 0; Check(l::ValidateBody(delivery.body,delivery.size,count),"authenticated body structurally valid");
        l::MessageReader reader(delivery.body,delivery.size); l::Message m{};
        while (reader.Next(m)) {
            if (m.type != static_cast<unsigned>(l::MsgType::frame)) continue;
            a::Frame f{};
            if (a::Decode(m.data,m.size,f)) {
                if (a::Store(inbox,f,peer,mgscoop::partner_net::kSession,30050,now)) ++accepted;
            } else {
                std::uint64_t sid = 0; std::uint32_t epoch = 0; s::PlayerTransformPacket pose{}; s::AnimState anim{}; bool has = false;
                if (s::decode_partner_transform_frame(m.data,m.size,sid,epoch,pose,anim,has)) ++poses;
            }
        }
    }
};
bool Send(void* context,const mgscoop::lab::Endpoint& to,const std::uint8_t* data,std::size_t size) noexcept {
    auto& n = *static_cast<Node*>(context); n.outbox.push_back({n.self,to,{data,data+size}}); return true;
}
bool Random(void* context,std::uint8_t* out,std::size_t size) noexcept {
    auto& n = *static_cast<Node*>(context);
    for (std::size_t i=0;i<size;++i) { n.rng = n.rng*6364136223846793005ull+1442695040888963407ull; out[i]=std::uint8_t(n.rng>>56); } return true;
}
void Start(Node& n,l::Role role,mgscoop::lab::Endpoint peer,std::uint64_t now) {
    l::SessionConfig config{}; config.role=role; config.exact_filter=false; config.peer=peer;
    std::memcpy(config.build,"actions-fixture",15); char secret[l::kSecretChars];
    std::memcpy(secret,"7K3QZ9M2XWD4HT8RVN6B",sizeof(secret)); l::DerivePsk(secret,config.psk);
    n.session.Start(config,{&n,&Send,&Random},now);
}
void Pump(Node& host,Node& guest,std::uint64_t now) {
    for (unsigned pass=0;pass<50;++pass) {
        std::vector<Datagram> packets; packets.swap(host.outbox);
        for (auto& d:guest.outbox) packets.push_back(d); guest.outbox.clear();
        if (packets.empty()) return;
        for (const auto& d:packets) {
            if (mgscoop::lab::SameEndpoint(d.to,host.self)) host.On(d,now);
            else if (mgscoop::lab::SameEndpoint(d.to,guest.self)) guest.On(d,now);
        }
    }
    Check(false,"bounded handshake pump");
}
void Connect(Node& host,Node& guest,std::uint64_t& now) {
    Start(host,l::Role::host,{},now); Start(guest,l::Role::guest,host.self,now);
    for (unsigned i=0;i<40 && guest.session.state()!=l::LinkState::connected;++i) {
        host.session.OnTimer(now); guest.session.OnTimer(now); Pump(host,guest,now); now+=20;
    }
    Check(host.session.state()==l::LinkState::connected && guest.session.state()==l::LinkState::connected,"actual authenticated two-way handshake connected");
}
void SendFrame(Node& node,const a::Frame& f,std::uint64_t now) {
    std::uint8_t frame[a::kFrameSize],pose[s::transform_anim_frame_size],body[l::kLinkMaxBody];
    Check(a::Encode(f,frame)==sizeof(frame),"fixed companion encodes");
    s::PlayerTransformPacket transform{}; transform.sequence=f.sequence; s::AnimState anim{14,1,2,s::AnimFlag::Valid};
    Check(s::encode_transform_anim_frame(f.session,f.epoch,transform,anim,pose)==sizeof(pose),"unchanged64-byte pose encodes");
    l::BodyWriter writer(body,sizeof(body));
    Check(writer.Add(l::MsgType::frame,frame,sizeof(frame)) && writer.Add(l::MsgType::frame,pose,sizeof(pose)),"companion and movement fit production frame writer");
    Check(node.session.SendData(body,writer.size(),now),"production session signs action payload");
}
int main() {
    Node host,guest; host.self={0x0a000001,47830}; guest.self={0x0a000002,50000}; guest.rng=81;
    std::uint64_t now=1000; Connect(host,guest,now);
    a::Producer producer{}; a::Sample raw{}; raw.enabled=raw.eligible=true; raw.mission=30050; raw.equip=873; raw.tick_ms=100;
    a::Observe(producer,raw); a::Frame f{mgscoop::partner_net::kSession,30050,1,producer.action};
    SendFrame(guest,f,now); Pump(host,guest,now);
    a::Receipt receipt{}; std::uint32_t age=0;
    Check(a::SampleMailbox(host.inbox,f.session,30050,30050,now,true,receipt,age) && receipt.status==a::ReceiptStatus::baseline,"authenticated owner baseline");
    Check(host.inbox.peer==mgscoop::lab::PackEndpoint(guest.self) && host.accepted==1 && host.poses==1,"source identity comes from actual callback and legacy pose coexists");
    raw.tick_ms=110; raw.raw_fire_active=true; a::Observe(producer,raw); f.action=producer.action; ++f.sequence; now+=20;
    SendFrame(guest,f,now); Check(guest.outbox.size()==1,"one signed action datagram ready");
    auto genuine=guest.outbox[0],tampered=genuine; guest.outbox.clear(); tampered.bytes.back()^=1;
    host.On(tampered,now); Check(host.accepted==1,"invalid MAC cannot deliver action data");
    auto foreign=genuine; foreign.from.port+=1; host.On(foreign,now);
    Check(host.accepted==1,"valid packet from unaccepted callback endpoint cannot deliver action data");
    host.On(genuine,now); Check(host.accepted==2,"genuine authenticated fire delivery accepted");
    host.On(genuine,now+1); Check(host.accepted==2,"authenticated packet replay cannot replay action history");
    for (unsigned i=0;i<4;++i) {
        raw.tick_ms+=10; raw.raw_fire_active=i==3; raw.raw_reload_pressed=i==1;
        a::Observe(producer,raw); f.action=producer.action; ++f.sequence; now+=20;
        SendFrame(guest,f,now); Pump(host,guest,now);
    }
    Check(a::SampleMailbox(host.inbox,f.session,30050,30050,now,true,receipt,age) && receipt.count==3
        && receipt.events[0].kind==a::EventKind::fire && receipt.events[1].kind==a::EventKind::reload && receipt.events[2].kind==a::EventKind::fire,
        "authenticated repeated callbacks preserve unconsumed fire/reload/fire order");
    const auto accepted=host.accepted; auto wrong_epoch=f; ++wrong_epoch.sequence; ++wrong_epoch.epoch;
    now+=20; SendFrame(guest,wrong_epoch,now); Pump(host,guest,now);
    Check(host.accepted==accepted,"authenticated wrong epoch still rejected by action envelope");
    Check(!a::SampleMailbox(host.inbox,f.session,30050,30020,now,true,receipt,age),"verified owner mission does not trust remote payload mission");
    const auto resets=host.resets; now+=2000; Connect(host,guest,now);
    ++f.sequence; SendFrame(guest,f,now); Pump(host,guest,now);
    Check(host.resets>resets && a::SampleMailbox(host.inbox,f.session,30050,30050,now,true,receipt,age)
        && receipt.status==a::ReceiptStatus::baseline && receipt.count==0,"new actual authenticated session cancels old context and never replays retained events");
    std::printf("PASS %u authenticated action-link checks (in-memory transport, no game)\n",checks);
}
