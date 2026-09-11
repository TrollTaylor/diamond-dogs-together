# Earlier Steam startup entry: static investigation

2026-09-09. The independently inspected pointer at **game RVA `0x20A0940` is a virtual-method slot**, offset `+0x10` in the table at `0x20A0930`. It is not a CRT constructor-table entry. Its demonstrated dispatcher is a call at **`0x638B2`**, with an object pointer in `RCX` and a Boolean result consumed from `AL`. That method contains the natural Steam Init call at `0x1428`. This establishes a concrete earlier method boundary, but does not establish an installation mechanism for the current proxy before that boundary.

Only installed files, original project source, archived evidence, and primary Microsoft documentation were read. No process was accessed, launched, patched, or invoked; no hook was created. Only this document and the earlier caller document's pointer classification were updated. Executable 1.0.15.4 SHA-256 is `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`, preferred base `0x140000000`. All game addresses below are RVAs requiring the actual loaded base. The installed original input proxy's file SHA-256 is `4D6FF8F970FA8CC3485D9F502E4D85DE60EFDDB9FB6C02775A1AD1BEF3DE868C`.

## Object table and method identity

The contiguous ten-pointer region `[0x20A0930,0x20A0980)` is in the read-only `.edata` section (`0x40000040`). The following UTF-16 title string begins at `0x20A0980`. That boundary is a file-layout observation, not a complete recovered class declaration.

| Table offset | Slot RVA | Target RVA | Bounded interpretation |
| --- | --- | --- | --- |
| `+0x00` | `0x20A0930` | `0x11F0` | Resets the same vptr, calls base cleanup, tests deletion flag bit 0, returns the object pointer; consistent with a deleting destructor. |
| `+0x08` | `0x20A0938` | `0x3FAD30` | Called immediately before the Steam-containing method, with object and startup-configuration pointers. |
| `+0x10` | `0x20A0940` | `0x13A0` | Object initialization method containing `SteamAPI_Init`. |
| `+0x18` | `0x20A0948` | `0x84B020` | Later method; full meaning not established. |
| `+0x20` | `0x20A0950` | `0x15D0` | Later method; full meaning not established. |
| `+0x28` | `0x20A0958` | `0x84B020` | Later method; full meaning not established. |
| `+0x30` | `0x20A0960` | `0x16D0` | Later method, beginning with a jump to `0x3135CB0`. |
| `+0x38` | `0x20A0968` | `0x1B70` | Not interpreted here. |
| `+0x40` | `0x20A0970` | `0x1BB0` | Not interpreted here. |
| `+0x48` | `0x20A0978` | `0x1CB0` | Not interpreted here. |

The exact on-disk slot bytes at `0x20A0940` are `A0 13 00 40 01 00 00 00`, the preferred VA `0x1400013A0`. They are not an ASLR-independent runtime pointer comparison.

The factory/accessor-like routine `[0x17C0,0x1A5E)` constructs the global object at RVA `0x2B59100` under its own initialization flag. Its base-construction call is at `0x193A` to `0x3FAA70`; that target immediately jumps to a transformed code region at `0x54397F0`, whose complete semantics were not reconstructed. The explicit following stores are unambiguous:

| RVA | Bytes | Operation |
| --- | --- | --- |
| `0x1933` | `48 8D 0D C6 77 B5 02` | Set `RCX` to object `base+0x2B59100`. |
| `0x1940` | `48 8D 05 E9 EF 09 02` | Set `RAX` to table `base+0x20A0930`. |
| `0x1947` | `48 89 05 B2 77 B5 02` | Store that vptr at object `base+0x2B59100`. |
| `0x1A46` | `48 8D 05 B3 76 B5 02` | Return the same object's address in `RAX`. |

Routine `0x11F0` also loads that table at `0x120D` and writes it through incoming `RCX` at `0x1214`. Cleanup routine `0x206D0C0` restores that table into the same global object before cleanup. These uses independently distinguish a vptr from an array of independent startup callbacks.

## Actual dispatch chain and calling convention

The CRT-like routine `[0x1A05CB0,0x1A05E93)` calls `0x3AA0` at `0x1A05DEF` after its initializer tables. At that call it supplies the image base in `RCX`, zero in `EDX`, a command-line pointer in `R8`, and a show-state value in `R9D`, consistent with a WinMain-style entry. The chain within that ordinary application path is:

```text
CRT startup 0x1A05CB0
  call 0x3AA0 at 0x1A05DEF
    call global-object factory/accessor 0x17C0 at 0x3DE0
    RCX = returned object; RDX = startup configuration
    call 0x64560 at 0x3DEC
      call startup dispatcher 0x63840 at 0x64584
        call vtable +0x08 at 0x638A9
        call vtable +0x10 at 0x638B2 -> 0x13A0
          natural SteamAPI_Init call at 0x1428
```

The dispatcher saves incoming object `RCX` in `RBX`, configuration `RDX` in `RDI`, and stores the configuration at `[object+0x380]` (`0x63874`). Immediately around the relevant call:

| RVA | Bytes | Operation |
| --- | --- | --- |
| `0x638A9` | `FF 50 08` | Call earlier virtual slot `+0x08`. |
| `0x638AC` | `48 8B 03` | Reload `RAX=[RBX]`, the current object vptr. |
| `0x638AF` | `48 8B CB` | Supply object `RCX=RBX`. |
| `0x638B2` | `FF 50 10` | Call current table slot `+0x10`. |
| `0x638B5` | `84 C0` | Test the method's `AL` result. |
| `0x638B7` | `75 20` | Continue initialization at `0x638D9` on true. |

At method entry `0x13A0`, `40 53 48 81 EC 80 00 00 00 48 8B D9` saves `RBX`, reserves stack space, and retains incoming `RCX` as the object. Its later read of `[RBX+0x380]` at `0x1476` matches the dispatcher's configuration store. This method's return paths explicitly write `AL=1` at `0x158A` or `AL=0` at `0x15BD`. A minimal observed machine-level signature is therefore Boolean-like `method(object_pointer)` on Windows x64; no extra incoming argument is established. A `void(*)()` initializer replacement would discard a required object argument and a consumed return value. Microsoft documents the x64 register arguments, stack shadow space, and undefined unused return-register bits. [Microsoft x64 ABI](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170)

The wrapper boundary would precede **this method's** Init call when reached through the verified table. It does not guarantee that Init occurs: earlier mutex failure/existing-instance paths return false, and failed Steam initialization takes an `exit(1)` path. The method's eventual Boolean result covers additional initialization after Steam; it must not be labeled the Steam Init result. Only the `0x1428/0x142E` pair captures that specific API result. See [natural Init caller investigation](steam-init-caller-investigation.md).

## Separate CRT initializer ranges

The executable imports `_initterm_e` at IAT `0x208D6C8` and `_initterm` at IAT `0x208D6D0`, through thunks `0x1A0656C` and `0x1A06572`. The caller's actual RIP-relative LEA operands establish these half-open table ranges:

| Call RVA | Function | RCX start RVA | RDX end RVA | File entries |
| --- | --- | --- | --- | --- |
| `0x1A05D2E` | `_initterm_e` | `0x209FEE0` | `0x209FF08` | 5 slots, 4 nonzero. |
| `0x1A05D64` | `_initterm` | `0x208E148` | `0x209FED8` | 9,138 slots, 9,137 nonzero. |

The corresponding argument instructions are `0x1A05D20: 48 8D 15 E1 A1 69 00`, `0x1A05D27: 48 8D 0D B2 A1 69 00`, `0x1A05D56: 48 8D 15 7B A1 69 00`, and `0x1A05D5D: 48 8D 0D E4 83 68 00`. Neither table contains the preferred pointer to `0x13A0`; slot `0x20A0940` lies outside both. Microsoft identifies these functions as internal initializer-table walkers that skip null entries. [Microsoft initializer reference](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/initterm-initterm-e?view=msvc-170)

Those tables run before the WinMain-style call in this decoded CRT path. This investigation did not establish the complete transformed control-flow route from the executable's PE entry `0x69B5F80` into the CRT routine, nor prove that no initializer or TLS callback uses Steam independently. The known executable TLS callback RVAs remain `0x6CFC4CE`, `0x5AAB920`, and `0xE200`.

## Input proxy timing and remaining installation gap

`DINPUT8.dll` is an ordinary load-time dependency, descriptor index 7 at RVA `0x9E793CC`, importing `DirectInput8Create` at IAT `0x208D048`. `steam_api64.dll` is descriptor index 1 at `0x9E79354`; the delay-import directory is zero. Under normal Windows load-time linking, dependencies are mapped and receive process initialization before the application's ordinary startup path executes. Descriptor order alone is not a general DLL initialization-order contract. [Microsoft load-time linking](https://learn.microsoft.com/en-us/windows/win32/dlls/load-time-dynamic-linking)

Existing archived evidence independently shows the actual game-directory proxy mapped before the PE-entry observation: [registration return log](evidence/game-registration-return-2026-09-09.log), line 77, records `D:\Games\steamapps\common\MGS_TPP\DINPUT8.dll`; the PE bootstrap and hardware arming markers follow at lines 114 and 124. That is historical module-load evidence, not a new live observation or proof that the proxy's normal exported function has already been called.

In the original [proxy source](../native/probe.cpp), `DllMain` only requests disabled thread notifications. The real System32 input library is loaded lazily on a forwarded input export. The Steam observer installs after a successful real `DirectInput8Create` return. The known game call to that export is `0x34B1E: E8 03 3E 2F 00`, through thunk `0x328926: FF 25 1C 47 D6 01`. Loading the proxy early therefore does not itself execute its existing observer setup early.

A future wrapper reached through the demonstrated virtual dispatch would run on the ordinary application startup path, outside the proxy's `DllMain`. **The current proxy has no demonstrated earlier outside-DllMain callback that can install that wrapper in time.** A new thread launched during DLL loading would not establish deterministic ordering before the main thread reaches the slot. Installing the pointer during DLL initialization would be a separate loader-lock design decision, not justified by the future wrapper body running later. No such installation is proposed or implemented here. DLL entry functions have restricted initialization behavior, and invoking loader-dependent work there can introduce dependency-order failures. [Microsoft DLL entry guidance](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-entry-point-function)

Before any implementation, the specific unresolved ownership facts are the loaded object/vptr/slot identity, page protection, whether another component changes the slot, and a lifetime guarantee that the wrapper module cannot unload while the slot refers to it. The slot is aligned but resides on a read-only image page; a write would be a real process mutation affecting every object that shares this table. Original failure, termination, and exception behavior must survive exactly once, and the object pointer must not be retained past a proven lifetime. None of these facts authorizes calling the method or initializing Steam manually.

## One concrete next observation

After the separate natural Steam Init trial, a bounded passive dispatcher capture at `0x638B2` can validate this route without altering the vtable. Check surrounding bytes `48 8B 03 48 8B CB FF 50 10 84 C0` at `0x638AC`, require the natural call's `RCX==RBX`, require `[RCX]==base+0x20A0930`, and require `[RAX+0x10]==base+0x13A0`. Record the object address only as opaque identity, thread, stack pointer, and already-loaded proxy module path. A matched return at `0x638B5` uses the same thread and **the same pre-call RSP**, while recording only `AL` as the outer method result. The prior direct-call fixture validates the equal-RSP pairing primitive; this virtual-dispatch composition would still need its own focused fixture before use.

This capture would establish the observed object's path and the proxy's availability at this earlier boundary. It would not solve wrapper installation timing, observe all Steam shutdown paths, prove TLS ordering, or permit the currently closed runtime SDK query gate to open.
