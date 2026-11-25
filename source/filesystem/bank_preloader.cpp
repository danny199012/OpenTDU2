#include "shared.h"
#include "bank_preloader.h"

#include <algorithm>

namespace {
std::string Key( const std::string &path ) {
    return ToLower( path );
}
} // namespace

BankPreloader::~BankPreloader() {
    {
        std::lock_guard<std::mutex> lock( mutex );
        bStop = true;
    }
    signal.notify_all();
    if ( thread.joinable() ) {
        thread.join();
    }
}

void BankPreloader::request( const std::string &path ) {
    const std::string key = Key( path );
    std::lock_guard<std::mutex> lock( mutex );
    if ( entries.count( key ) != 0 ) {
        return;
    }
    entries[key].Status = State::Queued;
    queue.push_back( key );
    if ( !thread.joinable() ) {
        bStop = false;
        thread = std::thread( [this]() { run(); } );
    }
    signal.notify_one();
}

bool BankPreloader::isPending( const std::string &path ) {
    std::lock_guard<std::mutex> lock( mutex );
    auto it = entries.find( Key( path ) );
    return it != entries.end() && ( it->second.Status == State::Queued || it->second.Status == State::Loading );
}

std::unique_ptr<Bank> BankPreloader::take( const std::string &path ) {
    const std::string key = Key( path );
    std::lock_guard<std::mutex> lock( mutex );
    auto it = entries.find( key );
    if ( it == entries.end() ) {
        return nullptr;
    }
    if ( it->second.Status == State::Ready ) {
        std::unique_ptr<Bank> pBank = std::move( it->second.pBank );
        entries.erase( it );
        finishedOrder.erase( std::remove( finishedOrder.begin(), finishedOrder.end(), key ), finishedOrder.end() );
        return pBank;
    }
    if ( it->second.Status == State::Failed ) {
        // The caller loads it the old way (and logs the failure); a later request may try again.
        entries.erase( it );
        finishedOrder.erase( std::remove( finishedOrder.begin(), finishedOrder.end(), key ), finishedOrder.end() );
    }
    return nullptr;
}

void BankPreloader::trimFinished() {
    while ( finishedOrder.size() > kMaxFinished ) {
        entries.erase( finishedOrder.front() );
        finishedOrder.pop_front();
    }
}

void BankPreloader::run() {
    for ( ;; ) {
        std::string key;
        {
            std::unique_lock<std::mutex> lock( mutex );
            signal.wait( lock, [this]() { return bStop || !queue.empty(); } );
            if ( bStop ) {
                return;
            }
            key = std::move( queue.front() );
            queue.pop_front();
            auto it = entries.find( key );
            if ( it == entries.end() ) {
                continue;
            }
            it->second.Status = State::Loading;
        }
        auto pBank = std::make_unique<Bank>();
        const bool bOk = pBank->load( key.c_str() );
        std::lock_guard<std::mutex> lock( mutex );
        auto it = entries.find( key );
        if ( it == entries.end() ) {
            continue;
        }
        it->second.Status = bOk ? State::Ready : State::Failed;
        if ( bOk ) {
            it->second.pBank = std::move( pBank );
        }
        finishedOrder.push_back( key );
        trimFinished();
    }
}
