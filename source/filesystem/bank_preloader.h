#pragma once

#include "filesystem/bank.h"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

// Reads and parses banks on a worker thread (2026-11-25): the sector load on
// the main thread spent 22 % of its time in Bank::load (file copy + directory
// parse, -logsectortimes "bank"). The worker does the same Bank::load (the
// file cache and resolvePath are locked) and hands the finished Bank over;
// the main thread takes it without waiting. A bank that was never requested
// (or failed) is simply loaded the old way by the caller.
class BankPreloader {
public:
    BankPreloader() = default;
    ~BankPreloader();
    BankPreloader( const BankPreloader & ) = delete;
    BankPreloader &operator=( const BankPreloader & ) = delete;

    // Queues a bank (idempotent: a bank already queued, loading or ready is left alone).
    void request( const std::string &path );
    // True while the bank is queued or loading.
    bool isPending( const std::string &path );
    // The finished bank, moved out; nullptr when it is not ready or failed.
    std::unique_ptr<Bank> take( const std::string &path );

private:
    enum class State { Queued, Loading, Ready, Failed };
    struct Entry {
        State Status = State::Queued;
        std::unique_ptr<Bank> pBank;
    };
    // Finished banks nobody took (the car turned away) are dropped oldest first.
    static constexpr size_t kMaxFinished = 6;

    void run();
    void trimFinished();

    std::mutex mutex;
    std::condition_variable signal;
    std::deque<std::string> queue;
    std::deque<std::string> finishedOrder;
    std::unordered_map<std::string, Entry> entries;
    std::thread thread;
    bool bStop = false;
};
