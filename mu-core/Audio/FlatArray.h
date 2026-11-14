#pragma once

#include <cassert>
#include <cstddef>
#include <memory>

namespace mu_core
{

// A fixed-size array whose length is decided once at run time (a layer or bus count), held in one contiguous block.
// It replaces `std::array<T, kMaxLayers>` where the count is no longer a compile-time constant. The size is set at
// construction or by reset(), both off the audio thread; element access is a plain base-pointer index, so a loop that
// hoists data() and size() costs the same as a fixed array. Not copyable (elements such as std::atomic are not).
template <typename T>
class FlatArray
{
public:
    FlatArray() = default;
    explicit FlatArray(int count) { reset(count); }

    // Allocate `count` default-constructed elements (count 0 leaves it empty). Message thread or constructor only.
    void reset(int count)
    {
        assert(count >= 0);
        count_ = count;
        items_ = count > 0 ? std::make_unique<T[]>((size_t) count) : nullptr;
    }

    FlatArray(FlatArray&&) noexcept = default;
    FlatArray& operator=(FlatArray&&) noexcept = default;
    FlatArray(const FlatArray&) = delete;
    FlatArray& operator=(const FlatArray&) = delete;

    int  size()  const noexcept { return count_; }
    bool empty() const noexcept { return count_ == 0; }

    T&       operator[](int i)       noexcept { assert(i >= 0 && i < count_); return items_[(size_t) i]; }
    const T& operator[](int i) const noexcept { assert(i >= 0 && i < count_); return items_[(size_t) i]; }

    // The same for code that holds an index as size_t (kept so a std::array call site moves over unchanged).
    T&       operator[](size_t i)       noexcept { assert(i < (size_t) count_); return items_[i]; }
    const T& operator[](size_t i) const noexcept { assert(i < (size_t) count_); return items_[i]; }

    void fill(const T& v) { for (int i = 0; i < count_; ++i) items_[(size_t) i] = v; }

    T*       data()       noexcept { return items_.get(); }
    const T* data() const noexcept { return items_.get(); }
    T*       begin()       noexcept { return items_.get(); }
    T*       end()         noexcept { return items_.get() + count_; }
    const T* begin() const noexcept { return items_.get(); }
    const T* end()   const noexcept { return items_.get() + count_; }

private:
    std::unique_ptr<T[]> items_;
    int count_ = 0;
};

} // namespace mu_core
