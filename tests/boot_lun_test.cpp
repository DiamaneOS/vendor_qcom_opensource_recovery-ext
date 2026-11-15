// Copyright 2026 The DiamaneOS Project
// SPDX-License-Identifier: Apache-2.0
// Boot LUN read and write against a fake UFS BSG device. No device access.
// Build with -D_BSG_FRAMEWORK_KERNEL_HEADERS for the BSG path, or with
// -D_GENERIC_KERNEL_HEADERS for a build that must refuse to touch the LUN.
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#include <cutils/log.h>
#include <cutils/properties.h>

// The sources below reach the device only through these calls.
DIR* fake_opendir(const char*);
struct dirent* fake_readdir(DIR*);
int fake_closedir(DIR*);
int fake_open(const char*, int, ...);
int fake_close(int);
int fake_ioctl(int, unsigned long, ...);
#define opendir fake_opendir
#define readdir fake_readdir
#define closedir fake_closedir
#define open fake_open
#define close fake_close
#define ioctl fake_ioctl
#undef _LARGEFILE64_SOURCE
#include "../oem-recovery/gpt-utils.cpp"
#undef LOG_TAG
#include "../oem-recovery/recovery-ufs-bsg.cpp"
#undef opendir
#undef readdir
#undef closedir
#undef open
#undef close
#undef ioctl

namespace {
const int kBsgFd = 1000;
struct Device {
    bool listed = true;       // /dev has ufs-bsg0
    int ioctl_result = 0;     // what the SG_IO ioctl returns
    int reply_result = 0;     // ufs_bsg_reply.result set by the kernel
    uint8_t response = 0;     // query response code in the reply UPIU
    uint32_t lun = 1;         // bBootLunEn held by the device
    int ioctls = 0, opens = 0, closes = 0;
    uint32_t msgcode = 0, dword_0 = 0, dword_1 = 0, value = 0;
    uint8_t opcode = 0xff, idn = 0xff;
} dev;
int dir_token, dir_pos;
struct dirent entry;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

DIR* fake_opendir(const char* path) {
    if (strcmp(path, "/dev")) { errno = ENOENT; return nullptr; }
    dir_pos = 0;
    return reinterpret_cast<DIR*>(&dir_token);
}
struct dirent* fake_readdir(DIR*) {
    const char* names[] = {".", "..", dev.listed ? "ufs-bsg0" : "null"};
    if (dir_pos >= 3) return nullptr;
    memset(&entry, 0, sizeof(entry));
    strcpy(entry.d_name, names[dir_pos++]);
    return &entry;
}
int fake_closedir(DIR*) { return 0; }
int fake_open(const char* path, int, ...) {
    if (strcmp(path, "/dev/ufs-bsg0")) { errno = ENOENT; return -1; }
    ++dev.opens;
    return kBsgFd;
}
int fake_close(int fd) {
    if (fd == kBsgFd) ++dev.closes;
    return 0;
}
int fake_ioctl(int fd, unsigned long request, ...) {
    ++dev.ioctls;
    if (fd != kBsgFd || request != 0x2285) { errno = ENOTTY; return -1; }
    va_list args; va_start(args, request);
    auto* io = va_arg(args, struct sg_io_v4*); va_end(args);
    auto* req = reinterpret_cast<struct ufs_bsg_request*>(static_cast<uintptr_t>(io->request));
    auto* rsp = reinterpret_cast<struct ufs_bsg_reply*>(static_cast<uintptr_t>(io->response));
    check(io->guard == 'Q' && io->request_len == sizeof(*req) &&
          io->max_response_len >= sizeof(*rsp), "malformed sg_io_v4");
    dev.msgcode = req->msgcode;
    dev.dword_0 = be32toh(req->upiu_req.header.dword_0);
    dev.dword_1 = be32toh(req->upiu_req.header.dword_1);
    dev.opcode = req->upiu_req.qr.opcode;
    dev.idn = req->upiu_req.qr.idn;
    dev.value = be32toh(req->upiu_req.qr.value);
    if (dev.ioctl_result) { errno = EIO; return dev.ioctl_result; }
    // The kernel copies the response UPIU as it is: big endian fields.
    memset(rsp, 0, sizeof(*rsp));
    rsp->result = dev.reply_result;
    rsp->upiu_rsp.header.dword_0 = htobe32(0x36u << 24);
    rsp->upiu_rsp.header.dword_1 = htobe32(uint32_t(dev.response) << 8);
    rsp->upiu_rsp.qr.opcode = dev.opcode;
    rsp->upiu_rsp.qr.idn = dev.idn;
    if (!dev.response && !dev.reply_result) {
        if (dev.opcode == 0x4) dev.lun = dev.value;
        if (dev.opcode == 0x3) rsp->upiu_rsp.qr.value = htobe32(dev.lun);
    }
    return 0;
}

int main() {
    int cases = 0, failures = 0;
    auto test = [&](const char* name, auto body) {
        try { dev = Device(); body(); ++cases; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };
    test("LUN values map to boot chains", [] {
        enum boot_chain chain = BACKUP_BOOT;
        check(boot_chain_from_lun_id(1, &chain) == 0 && chain == NORMAL_BOOT, "1 is not xbl_a");
        check(boot_chain_from_lun_id(2, &chain) == 0 && chain == BACKUP_BOOT, "2 is not xbl_b");
        for (uint32_t bad : {0u, 3u, 0x100u, 0x101u, 0x01000000u, 0xffffffffu})
            check(boot_chain_from_lun_id(bad, &chain) == -1, "unknown LUN value accepted");
        check(gpt_utils_get_xbl_boot_partition(nullptr) == -1, "null chain accepted");
    });
#ifdef _BSG_FRAMEWORK_KERNEL_HEADERS
    for (uint32_t lun : {1u, 2u}) {
        test(lun == 1 ? "read LUN 1" : "read LUN 2", [lun] {
            dev.lun = lun;
            enum boot_chain chain = lun == 1 ? BACKUP_BOOT : NORMAL_BOOT;
            check(gpt_utils_get_xbl_boot_partition(&chain) == 0, "read failed");
            check(chain == (lun == 1 ? NORMAL_BOOT : BACKUP_BOOT), "wrong chain");
            check(dev.msgcode == 0x16 && dev.dword_0 >> 24 == 0x16, "not a query request");
            check(((dev.dword_1 >> 16) & 0xff) == 0x01, "not a standard read");
            check(dev.opcode == 0x3 && dev.idn == 0x0, "not a read of bBootLunEn");
            check(dev.ioctls == 1 && dev.opens == 1 && dev.closes == 1, "descriptor leaked");
        });
    }
    for (uint32_t lun : {0u, 3u, 0x0100u}) {
        test("read of an unknown LUN value fails", [lun] {
            dev.lun = lun; enum boot_chain chain;
            check(gpt_utils_get_xbl_boot_partition(&chain) == -1, "unknown value accepted");
        });
    }
    test("ioctl failure fails the read", [] {
        dev.ioctl_result = -1; enum boot_chain chain;
        check(gpt_utils_get_xbl_boot_partition(&chain) == -1 && dev.closes == 1, "accepted");
    });
    test("kernel error fails the read", [] {
        dev.reply_result = -EIO; enum boot_chain chain;
        check(gpt_utils_get_xbl_boot_partition(&chain) == -1, "accepted");
    });
    test("query response error fails the read", [] {
        dev.response = 0xfe; enum boot_chain chain;
        check(gpt_utils_get_xbl_boot_partition(&chain) == -1, "accepted");
    });
    test("missing BSG node fails the read", [] {
        dev.listed = false; enum boot_chain chain;
        check(gpt_utils_get_xbl_boot_partition(&chain) == -1 && dev.ioctls == 0, "accepted");
    });
    test("write sends the value big endian and reads back", [] {
        char unused[] = "";
        check(set_boot_lun(unused, 2) == 0, "write failed");
        check(((dev.dword_1 >> 16) & 0xff) == 0x81 && dev.opcode == 0x4 && dev.idn == 0x0,
              "not a standard write of bBootLunEn");
        check(dev.value == 2 && dev.lun == 2 && dev.closes == 1, "wrong value written");
        enum boot_chain chain = NORMAL_BOOT;
        check(gpt_utils_get_xbl_boot_partition(&chain) == 0 && chain == BACKUP_BOOT,
              "read back differs");
    });
#else
    test("switch without BSG fails and writes nothing", [] {
        char unused[] = "";
        check(set_boot_lun(unused, 2) == -1 && dev.ioctls == 0, "switch reported success");
    });
    test("read without BSG fails", [] {
        enum boot_chain chain;
        check(gpt_utils_get_xbl_boot_partition(&chain) == -1 && dev.ioctls == 0, "accepted");
    });
#endif
    std::cout << cases << " cases passed, " << failures << " failed (boot LUN, "
#ifdef _BSG_FRAMEWORK_KERNEL_HEADERS
              << "BSG build"
#else
              << "build without BSG"
#endif
              << ", fake device)\n";
    return failures ? 1 : 0;
}
