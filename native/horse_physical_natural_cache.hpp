#pragma once
// Atomic READONLY publication. No ordinary mutable payload under a seqlock.
#include <atomic>
#include <cstring>
#include <type_traits>
#include "horse_physical_natural_read.hpp"
namespace mgscoop::horse_physical_natural_read {
static_assert(std::is_trivially_copyable_v<Published>);
class Cache {
    static constexpr std::size_t Count=(sizeof(Published)+7)/8;
    std::atomic<std::uint64_t> words_[Count]{};
    std::atomic<std::uint64_t> sequence_{0};std::atomic_flag writer_=ATOMIC_FLAG_INIT;
public:
    bool Publish(const Published& pair) noexcept {
        if(writer_.test_and_set(std::memory_order_acquire))return false;
        std::uint64_t words[Count]{};std::memcpy(words,&pair,sizeof(pair));
        sequence_.fetch_add(1,std::memory_order_acq_rel);
        for(std::size_t k=0;k<Count;++k)words_[k].store(words[k],std::memory_order_relaxed);
        sequence_.fetch_add(1,std::memory_order_release);writer_.clear(std::memory_order_release);return true;
    }
    bool Clear() noexcept {return Publish(Published{});}
    bool Read(Published& pair,std::uint64_t& version) const noexcept {
        pair={};version=0;std::uint64_t words[Count]{};
        for(unsigned retry=0;retry<3;++retry){const auto before=sequence_.load(std::memory_order_acquire);if(before&1u)continue;
            for(std::size_t k=0;k<Count;++k)words[k]=words_[k].load(std::memory_order_relaxed);
            if(before!=sequence_.load(std::memory_order_acquire))continue;
            std::memcpy(&pair,words,sizeof(pair));version=before;return true;
        }
        return false;
    }
    bool Current(std::uint64_t version) const noexcept {return !(version&1u)&&version==sequence_.load(std::memory_order_acquire);}
};
using WindowRead=std::uint32_t(*)() noexcept;
using BatchRead=bool(*)() noexcept;
// Both functions read mod-owned atomics only. The real current input sequence
// is sampled again after the layout/profile read; it is never pre-filled twice
// from one earlier value. Odd/changing sequence is visible observation only.
template<class Reader> bool ObserveCached(Reader& r,const Cache& cache,const RawWindow& raw,std::uint64_t now,
    WindowRead window,BatchRead batch,Row& row) noexcept {
    Published current{};std::uint64_t version=0;row={};
    if(!window||!batch){row.reason="input_readers";return false;}
    if(!cache.Read(current,version)){row.reason="published_race";return false;}
    Windows input{};input.before=window?window():0;input.after=input.before;input.batch=batch&&batch();
    const bool ready=Observe(r,current,raw,now,input,row);
    row.input.after=window?window():0;row.input.batch=row.input.batch||(batch&&batch());
    row.inputEvenStable=!(row.input.before&1u)&&row.input.before==row.input.after&&!row.input.batch;
    if(!cache.Current(version)){row.ready=false;row.reason="published_replaced";return false;}
    return ready;
}
}
