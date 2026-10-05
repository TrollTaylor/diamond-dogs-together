# Two-PC link: one-PC live tests (2026-10-05, coop-lab 794d24f)

Both lab copies on one PC (host `MGS_TPP`, guest `MGS_TPP_guest`), offline,
link v2 (docs/link-protocol.md). Real saves were UNCHANGED after every run.

| # | Test | Command | Result |
|---|---|---|---|
| 1 | Lab default (regression) | `Play-OfflineCoop.ps1 -ExitAfterReadySec 150` | **PASS.** `link=connected; session=1; peer=127.0.0.1:4782x`, rtt 0-1 ms, partner visible on both copies with the right looks (Quiet / Snake), dm=map clips, no reconnects. |
| 2 | LAN address, authenticated | `Play-OfflineCoop.ps1 -Lan -WanSim awful` | **PASS.** The guest reached the host through 192.168.8.128 with the full handshake (`hello_rx` -> `welcome_rx` -> `confirm`), partner visible on both copies. No firewall prompt appeared. |
| 3 | WAN simulation `awful` | (same run, about 7 min in Mother Base) | **PASS.** rtt 343-359 ms, jitter about 22 ms, loss 3-16 % in bursts, adaptive `interp_ms` 159-171, partner live throughout. During the three loading screens (title -> 40050 -> 30050) the guest logged 3 `link=reconnecting` and the host 1 `link=lost`; each was back within about 1 s. In Mother Base: 0. |
| 4 | Wrong join code | Prepare `-Lan`, a different guest secret, `Start-Lab -Offline -Lan` | **PASS.** Host: `link=drop; reason=bad_mac`, never connected, about 43 B/s sent against about 600 B/s received (no amplification). Guest: `link=rejected; reason=bad_code; verified=0`, `link-status.json` `state=connecting, reason=bad_code`. |
| 5 | Guest quits and comes back | `Start-Lab -Offline -Lan`, close the guest window, restart its exe | **PASS.** Host `link=lost; after_ms=3018` (partner frozen), then `link=reconnected; session=2; gap_ms=14620` once the new guest started. The partner reloaded on both copies (`partner_look=loaded`, `partner_dmmap=playing`), and the host status was `connected / partner live / shown 1`. |

Notes:

- The lab status line's `(N ms)` is the age of the newest partner frame, not
  the RTT; the RTT is `rtt_ms` in `link=stats` and `link-status.json`.
- The quitting guest was seen as `lost` after 3 s, not as `link=close;
  reason=peer` (closing the window ended the process before a close was sent).
- Still needs two real PCs: the Play-Coop Host/Join flow in `mode=play`,
  real NAT, port forwarding, UPnP and VPNs (Tailscale, ZeroTier, Radmin),
  play-mode build/proto REJECTs, NAT rebinding, and firewall prompts on a
  fresh PC.

Friend package: `pwsh -File tools/package.ps1 -Friend` built
`dist/mgs-coop-friend-0.11.0-20261005T174528Z-b73bc6e8.zip` (2.4 MB). It does
not contain the runtime archive, a link secret or any save.
