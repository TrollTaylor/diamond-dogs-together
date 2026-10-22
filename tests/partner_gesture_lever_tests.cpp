// The gesture lab lever (docs/native-puppet.md "Gestures", "Lab lever: knock and
// hand signs"), pure part: the command grammar, the stock slot table, the lab
// admission order, the stock preconditions and the result / log formats.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "mgscoop/partner_gesture_lever.hpp"

namespace {
namespace gl = mgscoop::partner_gesture_lever;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) {
        std::fprintf(stderr, "failed check %u: %s\n", checks, what);
        std::exit(1);
    }
}
bool Same(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }

void TestGrammar() {
    Check(gl::IsCommand("gesture_lever") && gl::IsCommand("gesture_lever knock") && gl::IsCommand("gesture_lever bogus"),
          "the command is ours with any argument");
    Check(!gl::IsCommand("gesture_levers knock") && !gl::IsCommand("gesture") && !gl::IsCommand(nullptr) && !gl::IsCommand("") &&
              !gl::IsCommand(" gesture_lever knock"),
          "other commands are not");
    Check(gl::Parse("gesture_lever knock") == gl::Request::knock, "knock");
    Check(gl::Parse("gesture_lever sign_go") == gl::Request::sign_go, "sign_go");
    Check(gl::Parse("gesture_lever sign_stop") == gl::Request::sign_stop, "sign_stop");
    const char* bad[] = {"gesture_lever", "gesture_lever ", "gesture_lever  knock", "gesture_lever knock ", "gesture_lever KNOCK",
                         "gesture_lever sign", "gesture_lever pickup", "gesture_lever knock sign_go", "gesture_leverknock", nullptr};
    for (const char* text : bad) Check(gl::Parse(text) == gl::Request::none, "malformed text parses to none");
    Check(Same(gl::RequestName(gl::Request::knock), "knock") && Same(gl::RequestName(gl::Request::sign_go), "sign_go") &&
              Same(gl::RequestName(gl::Request::sign_stop), "sign_stop") && Same(gl::RequestName(gl::Request::none), "none"),
          "names match the gesture kinds of partner_gesture_policy");
}

void TestSlots() {
    // KnockActionPluginImpl vtable 0x23C5A20: +0x28 RequestKnock, +0x30 Go, +0x38 Stop; the row kind and the upper byte
    // the state then writes (StateKnock 0x17, StateHandSignGo 0x18, StateHandSignStop 0x19).
    const gl::Slot k = gl::SlotFor(gl::Request::knock), g = gl::SlotFor(gl::Request::sign_go), s = gl::SlotFor(gl::Request::sign_stop);
    Check(k.offset == 0x28 && k.target_rva == 0x11E5100 && k.row_kind == 1 && k.upper == 0x17, "knock slot");
    Check(g.offset == 0x30 && g.target_rva == 0x11E5060 && g.row_kind == 2 && g.upper == 0x18, "sign_go slot");
    Check(s.offset == 0x38 && s.target_rva == 0x11E50B0 && s.row_kind == 3 && s.upper == 0x19, "sign_stop slot");
    Check(gl::SlotFor(gl::Request::none).offset == 0, "no slot for none");
    Check(gl::kPluginVtableRva == 0x23C5A20 && gl::kPollingRva == 0x11E5160 && gl::kBlockPlugin == 0xF8 && gl::kRowStride == 0xE0,
          "the plugin layout constants");
}

void TestAdmit() {
    const gl::Gate open{true, true, true, true, true, true, true, true, false, false};
    Check(gl::Admit(open) == nullptr, "everything lab: admitted");
    struct Case { gl::Gate gate; const char* reason; };
    const Case cases[] = {
        {{false, false, false, false, false, false, false, false, true, true}, "key_off"},  // the key wins
        {{true, false, false, false, false, false, false, false, true, true}, "mode"},      // play
        {{true, true, false, true, true, true, true, true, false, false}, "lab_inactive"},
        {{true, true, true, false, true, true, true, true, false, false}, "saves_not_redirected"},
        {{true, true, true, true, false, true, true, true, false, false}, "offline_branch"},
        {{true, true, true, true, true, false, true, true, false, false}, "partner_off"},
        {{true, true, true, true, true, true, false, true, false, false}, "code_gate"},
        {{true, true, true, true, true, true, true, false, false, false}, "gameplay"},
        {{true, true, true, true, true, true, true, true, true, false}, "shutting_down"},
        {{true, true, true, true, true, true, true, true, false, true}, "latched"},
    };
    for (const Case& c : cases) Check(Same(gl::Admit(c.gate), c.reason), c.reason);
}

void TestPreconditions() {
    const gl::Local stand{1, 0, 0, 1, 0};
    for (gl::Request r : {gl::Request::knock, gl::Request::sign_go, gl::Request::sign_stop})
        Check(gl::Precondition(r, stand) == nullptr, "standing idle: every request is admitted");
    // Knock: the call menu's enable test (UpdateCommandSetKnock 0x110F8D0).
    Check(gl::Precondition(gl::Request::knock, {20, 0, 0, 1, 1}) == nullptr && gl::Precondition(gl::Request::knock, {31, 0, 0, 2, 0}) == nullptr &&
              gl::Precondition(gl::Request::knock, {37, 0, 0, 3, 0}) == nullptr,
          "knock: acts 1..20 and 31..37, walking");
    Check(Same(gl::Precondition(gl::Request::knock, {0, 0, 0, 1, 0}), "act") && Same(gl::Precondition(gl::Request::knock, {21, 0, 0, 1, 0}), "act") &&
              Same(gl::Precondition(gl::Request::knock, {30, 0, 0, 1, 0}), "act") && Same(gl::Precondition(gl::Request::knock, {38, 0, 0, 1, 0}), "act") &&
              Same(gl::Precondition(gl::Request::knock, {0x4D, 0, 0, 1, 0}), "act"),
          "knock: other acts (none, ladder, box) refused");
    Check(Same(gl::Precondition(gl::Request::knock, {1, 5, 0, 1, 0}), "upper"), "knock: an upper action (reload) refused");
    Check(Same(gl::Precondition(gl::Request::knock, {1, 0, 1, 1, 0}), "q28"), "knock: Q+0x28 set refused");
    Check(Same(gl::Precondition(gl::Request::knock, {1, 0, 0, 1, 2}), "speed") && Same(gl::Precondition(gl::Request::knock, {1, 0, 0, 1, 3}), "speed"),
          "knock: running refused");
    // Signs: FUN_11E3A20 kinds 2 / 3 and the polling's stance test.
    for (gl::Request r : {gl::Request::sign_go, gl::Request::sign_stop}) {
        Check(gl::Precondition(r, {1, 0, 0, 1, 1}) == nullptr && gl::Precondition(r, {3, 0, 0, 2, 0}) == nullptr &&
                  gl::Precondition(r, {5, 0, 0, 3, 0}) == nullptr,
              "sign: walking, squat and crawl admitted");
        Check(Same(gl::Precondition(r, {1, 0, 0, 1, 2}), "speed"), "sign: speed 2 refused");
        Check(Same(gl::Precondition(r, {0x79, 0, 0, 1, 0}), "act") && Same(gl::Precondition(r, {0x7A, 0, 0, 1, 0}), "act"),
              "sign: acts 0x79 / 0x7A refused");
        Check(Same(gl::Precondition(r, {1, 0, 0, 4, 0}), "stance"), "sign: stance 4 refused");
        Check(Same(gl::Precondition(r, {1, 0x18, 0, 1, 0}), "upper"), "sign: a running sign refused (no rising edge)");
    }
    Check(Same(gl::Precondition(gl::Request::none, stand), "args"), "none: args");
}

void TestFormats() {
    gl::Outcome o{};
    o.request = gl::Request::sign_stop;
    o.local = {65535, 255, 255, 255, 255};
    o.row_kind = 255;
    o.row_d0 = 255;
    o.row_life = 255;
    o.id = 4294967295u;
    char out[161]{};
    const std::size_t n = gl::FormatResult(o, out, sizeof(out));
    Check(n > 0 && n <= 160 && std::strncmp(out, "gesture_lever=requested;kind=sign_stop;", 39) == 0, "the widest result fits 160 characters");
    for (std::size_t i = 0; i < n; ++i) {
        const char c = out[i];
        Check((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '=' || c == ';' || c == ',' ||
                  c == '.',
              "result charset");
    }
    o.refusal = "saves_not_redirected";
    Check(gl::FormatResult(o, out, sizeof(out)) > 0 && std::strncmp(out, "gesture_lever=refused;reason=saves_not_redirected;kind=sign_stop;", 65) == 0,
          "refused result");
    char small[16]{};
    Check(gl::FormatResult(o, small, sizeof(small)) == 0 && small[0] == 0, "a short buffer gets nothing");
    gl::Outcome k{};
    k.request = gl::Request::knock;
    k.local = {1, 0, 0, 1, 0};
    k.called = true;
    k.other_same = true;
    k.row_kind = 1;
    k.row_d0 = 1;
    k.row_life = 2;
    k.row_frame = 1234;
    k.recc = 0x10000000;
    char line[400]{};
    Check(gl::FormatLog(k, line, sizeof(line)) > 0 &&
              std::strcmp(line, "partner_gesture=lever; result=requested; reason=none; kind=knock; id=0; act=1; upper=0x00; q28=0; stance=1; "
                                "speed=0; called=1; row_kind=1; row_d0=0x01; row_life=2; frame=1234; rec4=0x00000000; rec8=0x00000000; "
                                "recc=0x10000000; other_same=1\r\n") == 0,
          "the log line");
    gl::FormatResult(k, out, sizeof(out));
    Check(std::strcmp(out, "gesture_lever=requested;kind=knock;id=0;act=1;stance=1;speed=0;row=1,0x01,2;other=same") == 0, "the requested result");
}
} // namespace

int main() {
    TestGrammar();
    TestSlots();
    TestAdmit();
    TestPreconditions();
    TestFormats();
    std::printf("partner_gesture_lever_tests: %u checks passed\n", checks);
    return 0;
}
