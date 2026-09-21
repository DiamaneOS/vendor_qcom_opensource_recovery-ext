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
