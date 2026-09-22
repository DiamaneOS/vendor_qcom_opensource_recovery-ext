// Copyright 2026 The DiamaneOS Project
// SPDX-License-Identifier: Apache-2.0
// Exercise the actual implementation on temporary regular files. No device access.
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <utility>
#undef _LARGEFILE64_SOURCE
#include "../oem-recovery/gpt-utils.cpp"

extern "C" {
ssize_t __real_read(int, void*, size_t);
ssize_t __real_write(int, const void*, size_t);
off64_t __real_lseek64(int, off64_t, int);
int __real_fsync(int);
int __real_open(const char*, int, ...);
int __real_close(int);
}
namespace {
struct Outcome { ssize_t bytes; int error = 0; };
std::deque<Outcome> read_plan, write_plan;
std::deque<int> sync_plan, seek_plan;
std::string fixture_path;
int fixture_fd = -1;
int sync_calls = 0, close_calls = 0, write_calls = 0;
bool fail_open = false, fail_close = false;
std::vector<std::pair<off64_t, size_t>> writes;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void reset_faults() {
    read_plan.clear(); write_plan.clear(); sync_plan.clear(); seek_plan.clear();
    sync_calls = close_calls = write_calls = 0; writes.clear();
    fail_open = fail_close = false;
}
ssize_t planned(std::deque<Outcome>& plan, size_t requested) {
    if (plan.empty()) return static_cast<ssize_t>(requested);
    Outcome next = plan.front(); plan.pop_front();
    if (next.bytes < 0) { errno = next.error; return -1; }
    return std::min(next.bytes, static_cast<ssize_t>(requested));
}
struct Fixture {
    int fd;
    std::vector<uint8_t> original, primary, backup, entries, backup_entries;
    gpt_disk disk{};
    Fixture() : original(4096, 0x5a), primary(512, 0), backup(512, 0),
                entries(512, 0x31), backup_entries(entries) {
        char path[] = "/tmp/gpt-io-test-XXXXXX";
        fd = mkstemp(path); check(fd >= 0, "mkstemp failed");
        fixture_path = path; fixture_fd = fd;
        check(__real_write(fd, original.data(), original.size()) ==
              static_cast<ssize_t>(original.size()), "fixture write failed");
        memcpy(primary.data(), GPT_SIGNATURE, 8);
        PUT_4_BYTES(primary.data() + HEADER_SIZE_OFFSET, 92);
        PUT_4_BYTES(primary.data() + PENTRIES_OFFSET, 2);
        PUT_4_BYTES(primary.data() + PARTITION_COUNT_OFFSET, 4);
        PUT_4_BYTES(primary.data() + PENTRY_SIZE_OFFSET, 128);
        backup = primary;
        PUT_4_BYTES(backup.data() + PENTRIES_OFFSET, 6);
        disk.hdr = primary.data(); disk.hdr_bak = backup.data();
        disk.pentry_arr = entries.data(); disk.pentry_arr_bak = backup_entries.data();
        disk.pentry_arr_size = 512; disk.pentry_size = 128;
        disk.block_size = 512; disk.is_initialized = GPT_DISK_INIT_MAGIC;
        strcpy(disk.devpath, path);
        check(gpt_disk_update_crc(&disk) == 0, "CRC preparation failed");
        reset_faults();
    }
    ~Fixture() { __real_close(fd); unlink(fixture_path.c_str()); fixture_fd = -1; }
    std::vector<uint8_t> bytes() {
        std::vector<uint8_t> result(4096);
        check(pread(fd, result.data(), result.size(), 0) == 4096, "fixture read failed");
        return result;
    }
    void complete_image() {
        auto result = bytes();
        for (auto item : {std::make_pair(512, &primary), {1024, &entries},
                          {3072, &backup_entries}, {3584, &backup}})
            check(std::equal(item.second->begin(), item.second->end(),
                             result.begin() + item.first), "incorrect committed bytes");
        for (size_t i = 0; i < result.size(); ++i)
            if (i < 512 || (i >= 1536 && i < 3072))
                check(result[i] == original[i], "write escaped GPT regions");
    }
};
}
extern "C" ssize_t __wrap_read(int fd, void* data, size_t size) {
    if (fd != fixture_fd) return __real_read(fd, data, size);
    ssize_t count = planned(read_plan, size);
    return count <= 0 ? count : __real_read(fd, data, static_cast<size_t>(count));
}
extern "C" ssize_t __wrap_write(int fd, const void* data, size_t size) {
    if (fd != fixture_fd) return __real_write(fd, data, size);
    ++write_calls;
    ssize_t count = planned(write_plan, size);
    if (count <= 0) return count;
    writes.emplace_back(__real_lseek64(fd, 0, SEEK_CUR), static_cast<size_t>(count));
    return __real_write(fd, data, static_cast<size_t>(count));
}
extern "C" off64_t __wrap_lseek64(int fd, off64_t offset, int whence) {
    if (fd == fixture_fd && !seek_plan.empty()) {
        int error = seek_plan.front(); seek_plan.pop_front();
        if (error) { errno = error; return -1; }
    }
    return __real_lseek64(fd, offset, whence);
}
extern "C" int __wrap_fsync(int fd) {
    if (fd == fixture_fd) {
        ++sync_calls;
        if (!sync_plan.empty()) {
            int error = sync_plan.front(); sync_plan.pop_front();
            if (error) { errno = error; return -1; }
        }
    }
    return __real_fsync(fd);
}
extern "C" int __wrap_ioctl(int fd, unsigned long request, ...) {
    // The only emulated device operation is the sector size of our regular file.
    if (fd != fixture_fd || request != BLKSSZGET) { errno = ENOTTY; return -1; }
    va_list args; va_start(args, request);
    *va_arg(args, uint32_t*) = 512; va_end(args); return 0;
}
extern "C" int __wrap_open(const char* path, int flags, ...) {
    if (fixture_path != path) { errno = EPERM; return -1; }
    if (fail_open) { errno = EACCES; return -1; }
    int fd = __real_open(path, flags);
    fixture_fd = fd; return fd;
}
extern "C" int __wrap_close(int fd) {
    if (fd == fixture_fd) {
        ++close_calls;
        int result = __real_close(fd);
        fixture_fd = -1;
        if (fail_close) { errno = EIO; return -1; }
        return result;
    }
    return __real_close(fd);
}
int main() {
    int cases = 0, failures = 0;
    auto test = [&](const char* name, auto body) {
        try { Fixture fixture; body(fixture); ++cases; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };
    for (int rw : {0, 1}) {
        test(rw ? "short writes and EINTR" : "short reads and EINTR", [rw](Fixture& f) {
            auto& plan = rw ? write_plan : read_plan;
            plan = {{-1, EINTR}, {3}, {1}, {-1, EINTR}, {2}};
            seek_plan = {EINTR, 0};
            uint8_t data[8]; std::fill(std::begin(data), std::end(data), uint8_t(0x7b));
            check(blk_rw(f.fd, rw, 11, data, sizeof(data)) == 0, "short I/O did not complete");
            if (rw) {
                auto bytes = f.bytes();
                for (size_t i=0; i<bytes.size(); ++i)
                    check(bytes[i] == (i>=11 && i<19 ? 0x7b : 0x5a), "wrong short-write offset");
            } else for (auto byte : data) check(byte == 0x5a, "incomplete read");
        });
        for (Outcome failure : {Outcome{0}, Outcome{-1, EIO}}) {
            test(rw ? "write failure after progress" : "read failure after progress", [rw,failure](Fixture& f) {
                (rw ? write_plan : read_plan) = {{3}, failure};
                uint8_t data[8]{};
                check(blk_rw(f.fd,rw,0,data,sizeof(data)) == -1, "incomplete I/O accepted");
            });
        }
    }
    test("seek failure", [](Fixture& f) {
        seek_plan={EIO}; uint8_t data[8]{};
        check(blk_rw(f.fd,1,0,data,sizeof(data))==-1 && write_calls==0,"write after failed seek");
    });
    test("EOF is failure", [](Fixture& f) {
        uint8_t data[8]{}; check(blk_rw(f.fd,0,4092,data,sizeof(data))==-1,"EOF accepted");
    });
    test("complete commit", [](Fixture& f) {
        check(gpt_disk_commit(&f.disk)==0,"normal commit failed"); f.complete_image();
        check(sync_calls==2 && close_calls==1,"missing copy durability boundary");
    });
    test("commit handles interrupted sync", [](Fixture& f) {
        sync_plan={EINTR,0,EINTR,0};
        check(gpt_disk_commit(&f.disk)==0,"interrupted fsync failed"); f.complete_image();
        check(sync_calls==4,"fsync not retried");
    });
    for (int boundary : {0,1}) {
        test("commit fsync failure", [boundary](Fixture& f) {
            if (boundary) sync_plan.push_back(0);
            sync_plan.push_back(EIO);
            check(gpt_disk_commit(&f.disk)==-1 && close_calls==1,"sync failure accepted or leaked fd");
            if (!boundary) {
                auto bytes=f.bytes();
                check(std::equal(bytes.begin()+3072,bytes.end(),f.original.begin()+3072),"backup touched before primary sync");
            }
        });
    }
    for (int phase=0; phase<4; ++phase) {
        test("commit stops on incomplete GPT region", [phase](Fixture& f) {
            for(int i=0;i<phase;++i) write_plan.push_back({512});
            write_plan.push_back({17}); write_plan.push_back({0});
            check(gpt_disk_commit(&f.disk)==-1,"short GPT region accepted");
            check(write_calls==phase+2 && close_calls==1,"continued writes or leaked fd after failure");
        });
    }
    test("commit open failure", [](Fixture& f) {
        fail_open=true; check(gpt_disk_commit(&f.disk)==-1 && write_calls==0,"failed open accepted");
    });
    test("commit close failure", [](Fixture& f) {
        fail_close=true;check(gpt_disk_commit(&f.disk)==-1 && close_calls==1,"close failure accepted or retried");
    });
    std::cout << cases << " cases passed, " << failures
              << " failed (regular-file I/O with syscall fault injection; no hardware)\n";
    return failures ? 1 : 0;
}
