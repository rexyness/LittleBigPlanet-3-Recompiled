# shadNet client: origin and changes

These files are shadPS4's shadNet client (rpcs3 / rpcsn lineage), GPL-2.0-or-later, with their
original copyright and SPDX notices:

- `client.cpp`, `client.h`, `server_probe.cpp`, `server_probe.h`, `shadnet.proto`.

They were copied from LBPPC (emurphy04, https://github.com/emurphy04/LBPPC at `aad6a91`,
`gpu/bbnet/vendor/shadps4/shadnet/`), which vendors shadPS4 v0.20.0's client
(https://github.com/shadps4-emu/shadPS4/tree/v.0.20.0) and adds, in place, marked `bbport` / `lbppc`:

- the TLS client for a server behind a TLS proxy;
- TCP keepalive and a login-reply timeout;
- the server's newer notifications (service unavailable, client rejected, signaling helper).

LBPPC's copy was taken rather than our submodule's own `src/shadnet` because it matches the server
`play.lbppc.com` runs. Each file's header says GPL-2.0-or-later. Nothing from LBPPC's untagged
(GPL-3.0) files is here.

## Our changes (marked `lbp3:`)

- **Title:** the login's title id and name come from `SetTitle()`, set by our service, instead of
  shadPS4's `Common::ElfInfo`. The default is LBP3's: "CUSA00473", "LittleBigPlanet™ 3".
- **TLS:** turned on by `SetTlsWanted()`, which our service sets from `LBP3_SHADNET_SERVER`
  (`tls://...`). LBPPC's `BB_SHADNET_TLS=1` is still honoured, as is `BB_SHADNET_CA` (an extra CA
  file for a test server).
- **Client version:** this port reports itself as "LBP3-Recompiled (lbp3-port)", not as shadPS4 or
  LBPPC. `LBP3_SHADNET_CLIENT_VERSION` overrides it.
- **`common/elf_info.h` and `common/scm_rev.h`:** no longer included.
- **Sign-in codes (command 42, `GetAuthorizationCode`):** `RequestAuthorizationCode` and its reply
  handler, and the two proto messages, are adapted from LBPPC 0.1.3 (`951fb8d`,
  `gpu/bbnet/vendor/shadps4/shadnet/client.cpp:788-845` and `shadnet.proto:577-585`, GPL-2.0-or-later;
  the messages come from shadNet's own server proto). Ours also returns the code's lifetime.
- **Bundled roots:** Let's Encrypt's ISRG Root X1 and X2 (public certificates, from
  letsencrypt.org/certificates, fingerprints in the code) are added to the trust store, as LBPPC 0.1.3 does,
  for PCs whose Windows store hasn't fetched them yet.
- **`FailureTooSoon`:** a login refused with TooSoon (LBPPC's wrong-password pause) gets its own state, after
  the existing ones.

The service that drives the client (`../shadnet_service.*`) and the NP glue (`hle/np_shadnet.cpp`,
`hle/np_offline.cpp`) are this project's own code.
