#pragma once

#include "filesystem/bank.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

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

    // Extra work done on the worker with the parsed bank (the sector's heightfield decode); its result travels with the bank.
    using Prepare = std::function<std::shared_ptr<void>( const Bank & )>;
    struct Result {
        std::unique_ptr<Bank> pBank;
        std::shared_ptr<void> pExtra; // what Prepare returned (nullptr without one)
    };

    // Queues a bank (idempotent: a bank already queued, loading or ready is left alone).
    void request( const std::string &path, Prepare prepare = nullptr );
    // Drops everything queued or finished and waits for the job in flight (the world is being torn down).
    void clear();
    // Blocks until the bank is no longer queued or loading (ready, failed or unknown).
    void wait( const std::string &path );
    // True while the bank is queued or loading.
    bool isPending( const std::string &path );
    // The finished bank, moved out; an empty Result when it is not ready or failed.
    Result take( const std::string &path );

private:
    enum class State { Queued, Loading, Ready, Failed };
    struct Entry {
        State Status = State::Queued;
        std::unique_ptr<Bank> pBank;
        std::shared_ptr<void> pExtra;
        Prepare PrepareFn;
    };
    // Finished banks nobody took (the car turned away) are dropped oldest first.
    static constexpr size_t kMaxFinished = 6;

    void run();
    void trimFinished();

    std::mutex mutex;
    std::condition_variable signal;
    std::condition_variable idleSignal;
    uint32_t numBusy = 0; // workers with a bank in hand
    std::deque<std::string> queue;
    std::deque<std::string> finishedOrder;
    std::unordered_map<std::string, Entry> entries;
    // Two workers: the banks of a start-up ring parse side by side while the main thread finishes the previous sector.
    static constexpr size_t kNumWorkers = 2;
    std::vector<std::thread> threads;
    bool bStop = false;
};
