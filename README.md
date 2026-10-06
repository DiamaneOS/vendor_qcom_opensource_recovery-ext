# Qualcomm GPT and UFS recovery support

DiamaneOS maintains the Fairphone implementation used by its Qualcomm A/B
boot-control service. The `android17` branch starts from Fairphone Gerrit
`platform/vendor/qcom-opensource/recovery-ext` at
`dfa77ea20218c0337793a02bfb9071f1a6dd020d`.

## Build

- The Android 17 build uses `librecovery_updater` for GPT slot attributes and
  UFS boot-LUN selection in vendor and recovery processes.
- Leaves out the legacy Edify registration source and its unused ION/updater
  dependencies.
- Soong owns both variants; the old Make definitions are inactive.
- The explicit upstream CFI exception is removed, so normal product hardening
  applies.
- `SOONG_CONFIG_ufsbsg_ufsframework` selects the UFS interface; the FP6 product
  selects `bsg`.
- Needs kernel interface, build and device validation before it changes slot
  state.

## GPT I/O

- Completes short transfers, retries interrupted operations and rejects EOF or
  zero-progress writes.
- Committing slot metadata synchronizes the whole primary GPT before touching
  its backup, then synchronizes the backup. Sync and close failures are
  returned to the caller.
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
  synchronization and descriptor cleanup. They never access a block device.
- Android compilation and device slot behavior need separate validation.

## Upstream and licensing

Upstream history, source copyright headers and NOTICE are retained. Source
files carry their Linux Foundation redistribution or Apache-2.0 terms; these
build adaptations do not relicense upstream code.
