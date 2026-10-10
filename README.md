# Qualcomm GPT and UFS recovery support

DiamaneOS's fork of Fairphone's implementation (Fairphone Gerrit
`platform/vendor/qcom-opensource/recovery-ext`), used by the Qualcomm A/B
boot-control service. Upstream history is kept; DiamaneOS changes are the
commits on top.

## Build

- `librecovery_updater` gives vendor and recovery processes GPT slot
  attributes and UFS boot-LUN selection.
- It leaves out the legacy Edify registration source and its ION/updater
  dependencies.
- Soong builds both variants; the old Make definitions are inactive.
- It builds with the product's normal hardening, including CFI.
- `SOONG_CONFIG_ufsbsg_ufsframework` selects the UFS interface; the FP6 product
  selects `bsg`.

## GPT I/O

- Completes short transfers, retries interrupted operations and rejects EOF or
  zero-progress writes.
- Committing slot metadata synchronizes the whole primary GPT before touching
  its backup, then synchronizes the backup.
- Sync and close failures are returned to the caller.
- This keeps the existing copy order. It does not make updates across multiple
  LUNs atomic or establish physical power-loss recovery.

## Tests

Run the host regressions on Linux with a C++17 compiler, a GNU-compatible
linker and zlib development headers/library:

```sh
tests/run-gpt-io-tests.sh
```

- The runner takes the usual `CXX`, `CPPFLAGS`, `CXXFLAGS`, `LDFLAGS` and
  `LDLIBS` overrides.
- Tests compile the actual implementation and inject syscall failures against
  temporary regular files: incomplete transfers, interruption, failed
  synchronization and descriptor cleanup.
- They never access a block device.

## Upstream and licensing

Upstream source copyright headers and `NOTICE` are kept.
