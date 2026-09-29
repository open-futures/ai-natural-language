// =============================================================================
//  include/tts/memory_view.hpp
//  Phase 2 — Cross-platform byte-source abstraction.
//
//  A `MemoryView` owns (or borrows) a contiguous run of bytes and exposes it
//  as a `std::span<const std::uint8_t>`. Three backends are supported:
//
//    Borrowed  — wraps an external buffer; no ownership transfer.
//    Owned     — owns a std::vector<uint8_t> internally.
//    Mmap      — owns a memory-mapped region backed by a file
//                (mmap+madvice on POSIX, CreateFileMapping on Windows).
//
//  All access is `const`; MemoryView is a read-only source. The class is
//  movable but not copyable (mmap handles are heavy and reference-counted
//  via shared_ptr to make reads thread-safe — see Phase 2 threading note).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tts {

class MemoryView {
public:
    // -------------------------------------------------------------------------
    //  Construction & lifecycle
    // -------------------------------------------------------------------------
    MemoryView() = default;
    ~MemoryView() = default;

    MemoryView(MemoryView&&) noexcept = default;
    MemoryView& operator=(MemoryView&&) noexcept = default;

    MemoryView(const MemoryView&)            = delete;
    MemoryView& operator=(const MemoryView&)  = delete;

    // -------------------------------------------------------------------------
    //  Factories
    // -------------------------------------------------------------------------

    /// Wrap an external buffer; the caller retains ownership.
    [[nodiscard]] static MemoryView
    borrow(std::span<const std::uint8_t> data) noexcept;

    /// Convenience overload for string_view-backed buffers.
    [[nodiscard]] static MemoryView
    borrow(std::string_view data) noexcept;

    /// Take ownership of a heap buffer.
    [[nodiscard]] static MemoryView
    own(std::vector<std::uint8_t> data) noexcept;

    /// Load a file. Uses mmap when TTS_HAVE_MMAP=1 and the file is large
    /// enough to justify it; otherwise falls back to a heap read.
    /// Returns an error string (not the SDK's typed enums) because this is
    /// the lowest layer — it never sees a TTF/ATTF context.
    [[nodiscard]] static std::expected<MemoryView, std::string>
    from_file(const std::filesystem::path& path, bool prefer_mmap = true);

    /// Force mmap backend (returns error on platforms without mmap support).
    [[nodiscard]] static std::expected<MemoryView, std::string>
    mmap_file(const std::filesystem::path& path);

    /// Force heap-read backend.
    [[nodiscard]] static std::expected<MemoryView, std::string>
    read_file(const std::filesystem::path& path);

    // -------------------------------------------------------------------------
    //  Observers
    // -------------------------------------------------------------------------

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return view_; }
    [[nodiscard]] const std::uint8_t* data() const noexcept            { return view_.data(); }
    [[nodiscard]] std::size_t         size() const noexcept            { return view_.size(); }
    [[nodiscard]] bool                empty() const noexcept           { return view_.empty(); }

    /// Convenience: interpret the bytes as a UTF-8 string view.
    /// No validation is performed; callers that need validation should
    /// use tts::utf8::validate() (Phase 2 types.hpp).
    [[nodiscard]] std::string_view as_string_view() const noexcept {
        return std::string_view{reinterpret_cast<const char*>(view_.data()), view_.size()};
    }

    /// Sub-view into the underlying buffer. Throws nothing — out-of-range
    /// requests are clamped to (offset, 0) by std::span semantics; use the
    /// checked overload below if you need explicit error reporting.
    [[nodiscard]] std::span<const std::uint8_t>
    subspan(std::size_t offset,
            std::size_t count = std::dynamic_extent) const noexcept {
        return view_.subspan(offset, count);
    }

    /// Checked subspan — returns nullopt-equivalent (empty optional wrapped
    /// in expected) when (offset, count) exceeds the buffer.
    [[nodiscard]] std::expected<std::span<const std::uint8_t>, std::string>
    subspan_checked(std::size_t offset, std::size_t count) const;

private:
    // Backend storage
    //   - view_         : always points at the live bytes
    //   - owned_buffer_ : populated when Backend == Owned
    //   - mmap_handle_  : shared_ptr<MmapHandle> when Backend == Mmap
    //     (MmapHandle is defined in the .cpp via PIMPL; here we just hold
    //      a shared_ptr<void> for ABI simplicity.)
    std::span<const std::uint8_t>      view_{};
    std::vector<std::uint8_t>          owned_buffer_{};
    std::shared_ptr<void>              mmap_handle_{};
};

} // namespace tts
