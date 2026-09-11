# Teammate marker presentation

Workspace 0.10.26 adds DYNAMITE's existing-marker icon adaptation: the native converter runs first, then its exact FOB enemy result `0x200005` becomes the buddy D-Dog icon `0x1c` while this adapter's co-op session is active. All other results remain unchanged. This is the upstream icon choice, not a newly authored teammate graphic.

The change helps distinguish the partner from enemies when the game supplies that marker. It does not create a marker, guarantee partner visibility, extend marker duration, share binocular tags or send supplementary packets. Markers with additional type modifiers do not match the exact value and are not remapped, matching upstream's equality check.

The current executable's converter is RVA `0x935020`, reached through owner table `0x2273CE0`, slot `+0x2B8` at `0x2273F98`. Its arguments are RCX owner and RDX marker record, with an EAX result. The function reads system type at record+0x1E, presentation mode at +0x20 and flag words at +0x28/+0x2A. System type 14 can produce base type 5 with bit 21 set; the buddy case, system type 19, selects 0x1C for flag 1. The same converter consumes some record flags in other cases, so the original call must be preserved.

The complete declared range is 2,304 bytes: 1,840 bytes of code followed by 464 bytes of switch tables and padding. The static report separates code from data, records all three target tables and the vehicle index table, and checks target instruction boundaries. Treating the whole range as linear instructions would misdecode its tables; gating only the code would leave the dispatch data unchecked.

Installation checks the entire converter range, owner constructor (360 bytes), destructor (108 bytes), two complete interface callers (260 and 224 bytes) and the relocated converter table entry. The constructor installs the owner table; the callers pass marker-system records and consume the returned type. This is a semantic mapping supported by executable dataflow, not recovery of an original function symbol.

The hook is installed during player preparation before session creation. It forwards unchanged until both installation and the existing active owned-session scope permit remapping. Existing gameplay cleanup disables that scope on exit/loss. The hook retains no owner or marker pointers. The module and original trampoline stay available for natural callbacks; after a failed enable attempt the policy stays inactive.

Provenance: `GetUiMarkerTypeFromSystemType2Hook` and `TppMarkerType.h`, DYNAMITE revision `c90f252f8200765f66c005d6bf4357fda0c0d98e`, unknown321, GPLv3 with Commons Clause 1.0. Full terms remain in [the retained license](../third_party/dynamite/LICENSE). Current-build mapping, byte gates and lifecycle scope are project additions.

Compilation succeeded with `tools/build.ps1 -Configuration Release -BuildOnly`. No tests, Lua/source-transform execution, game/VM launches or installation changes occurred. Installed 0.10.5 remains unchanged. Neither a rendered teammate icon nor a connected guest, independent control or shared combat is verified. The existing session scope is lifecycle metadata, not an atomic engine-object lifetime guarantee.

[Static mapping](evidence/teammate-marker-contract-2026-09-10.json), [prepared build](evidence/teammate-marker-prepared-2026-09-10.json).

The continuing upstream audit also identified a climbing-state override and a path-result fallback. Their current-build identities and calling paths remain unresolved; no candidate-only hooks were installed for them. Pointer-capture hooks for equipment/emblems and supplementary marker networking remain separate work. Boss-specific and campaign synchronization continue to be deferred.
