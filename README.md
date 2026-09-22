# Qualcomm GPT and UFS recovery support

DiamaneOS maintains the Fairphone implementation used by its Qualcomm A/B
boot-control service. The `android17` branch starts from Fairphone Gerrit
`platform/vendor/qcom-opensource/recovery-ext` at
`dfa77ea20218c0337793a02bfb9071f1a6dd020d`.

The Android 17 build uses `librecovery_updater` for GPT slot attributes and UFS
boot-LUN selection in vendor and recovery processes. It excludes the legacy
Edify registration source and its unused ION/updater dependencies. Soong owns
both variants; the old Make definitions are inactive. The explicit upstream
CFI exception is removed so normal product hardening applies.

The UFS interface is selected through `SOONG_CONFIG_ufsbsg_ufsframework`.
The FP6 product selects `bsg`. Kernel interface, build and device validation
remain necessary before using this code to change slot state.

Upstream history, source copyright headers and NOTICE are retained. These build
adaptations do not relicense upstream code. Source files carry their applicable
Linux Foundation redistribution or Apache-2.0 terms.

GPT I/O completes short transfers, retries interrupted operations and rejects
EOF or zero-progress writes. Committing slot metadata synchronizes the complete
primary GPT before touching its backup, then synchronizes the backup; sync and
close failures are returned to the caller. This preserves the existing copy
order. It does not make updates across multiple LUNs atomic or establish physical
power-loss recovery.

Run the host regressions on Linux with a C++17 compiler, GNU-compatible linker
and zlib development headers/library:

```sh
tests/run-gpt-io-tests.sh
```

The runner accepts the conventional `CXX`, `CPPFLAGS`, `CXXFLAGS`, `LDFLAGS` and
`LDLIBS` overrides. Tests compile the actual implementation and inject syscall
failures against temporary regular files, including incomplete transfers,
interruption, failed synchronization and descriptor cleanup. They never access
a block device. Android compilation and device slot behavior remain separate
validation requirements.
