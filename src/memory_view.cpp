// =============================================================================
//  src/memory_view.cpp — Phase 2 implementation
// =============================================================================
#include "tts/memory_view.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <new>
#include <system_error>

#if defined(TTS_PLATFORM_POSIX) && defined(TTS_HAVE_MMAP)
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#  define TTS_HAS_POSIX_MMAP 1
#else
#  define TTS_HAS_POSIX_MMAP 0
#endif

#if defined(TTS_PLATFORM_WINDOWS)
#  define WIN32_LEAN_AND_MEAN 1
#  include <windows.h>
#  define TTS_HAS_WIN_MMAP 1
#else
#  define TTS_HAS_WIN_MMAP 0
#endif

namespace tts {

namespace {

// ---------------------------------------------------------------------------
//  POSIX mmap handle
// ---------------------------------------------------------------------------
#if TTS_HAS_POSIX_MMAP
struct PosixMmapHandle {
    void*       region = nullptr;
    std::size_t length = 0;
    int         fd     = -1;

    PosixMmapHandle() = default;
    ~PosixMmapHandle() {
        if (region && region != MAP_FAILED) ::munmap(region, length);
        if (fd >= 0) ::close(fd);
    }
    PosixMmapHandle(const PosixMmapHandle&)            = delete;
    PosixMmapHandle& operator=(const PosixMmapHandle&) = delete;
};
#endif

#if TTS_HAS_WIN_MMAP
struct WinMmapHandle {
    HANDLE file_mapping = nullptr;
    HANDLE file_handle   = INVALID_HANDLE_VALUE;
    void*  base          = nullptr;
    std::size_t length   = 0;

    WinMmapHandle() = default;
    ~WinMmapHandle() {
        if (base)   ::UnmapViewOfFile(base);
        if (file_mapping) ::CloseHandle(file_mapping);
        if (file_handle != INVALID_HANDLE_VALUE) ::CloseHandle(file_handle);
    }
};
#endif

} // namespace

// ---------------------------------------------------------------------------
//  Factories
// ---------------------------------------------------------------------------
MemoryView MemoryView::borrow(std::span<const std::uint8_t> data) noexcept {
    MemoryView v;
    v.view_ = data;
    return v;
}

MemoryView MemoryView::borrow(std::string_view data) noexcept {
    return borrow(std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(data.data()), data.size()});
}

MemoryView MemoryView::own(std::vector<std::uint8_t> data) noexcept {
    MemoryView v;
    if (!data.empty()) {
        v.view_ = std::span<const std::uint8_t>{data.data(), data.size()};
    }
    v.owned_buffer_ = std::move(data);
    return v;
}

std::expected<MemoryView, std::string>
MemoryView::mmap_file(const std::filesystem::path& path) {
#if TTS_HAS_POSIX_MMAP
    auto h = std::make_shared<PosixMmapHandle>();
    h->fd = ::open(path.c_str(), O_RDONLY);
    if (h->fd < 0) {
        return std::unexpected(std::string{"open() failed: "} +
                               std::strerror(errno));
    }
    struct stat st{};
    if (::fstat(h->fd, &st) != 0) {
        return std::unexpected(std::string{"fstat() failed: "} +
                               std::strerror(errno));
    }
    if (st.st_size <= 0) {
        // Empty file: return an empty view (still a valid state).
        MemoryView v;
        v.mmap_handle_ = std::static_pointer_cast<void>(
            std::shared_ptr<PosixMmapHandle>{std::move(h)});
        return v;
    }
    h->length = static_cast<std::size_t>(st.st_size);
    h->region = ::mmap(nullptr, h->length, PROT_READ,
                        MAP_PRIVATE, h->fd, 0);
    if (h->region == MAP_FAILED) {
        return std::unexpected(std::string{"mmap() failed: "} +
                               std::strerror(errno));
    }
    // Best-effort: hint the kernel we'll read sequentially.
    (void)::madvise(h->region, h->length, MADV_SEQUENTIAL);

    MemoryView v;
    v.view_ = std::span<const std::uint8_t>{
        static_cast<const std::uint8_t*>(h->region), h->length};
    v.mmap_handle_ = std::static_pointer_cast<void>(
        std::shared_ptr<PosixMmapHandle>{std::move(h)});
    return v;

#elif TTS_HAS_WIN_MMAP
    auto h = std::make_shared<WinMmapHandle>();
    h->file_handle = ::CreateFileW(
        path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h->file_handle == INVALID_HANDLE_VALUE) {
        return std::unexpected("CreateFileW failed");
    }
    LARGE_INTEGER sz{};
    if (!::GetFileSizeEx(h->file_handle, &sz)) {
        return std::unexpected("GetFileSizeEx failed");
    }
    if (sz.QuadPart == 0) {
        MemoryView v;
        v.mmap_handle_ = std::static_pointer_cast<void>(
            std::shared_ptr<WinMmapHandle>{std::move(h)});
        return v;
    }
    h->length = static_cast<std::size_t>(sz.QuadPart);
    h->file_mapping = ::CreateFileMappingW(
        h->file_handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!h->file_mapping) {
        return std::unexpected("CreateFileMappingW failed");
    }
    h->base = ::MapViewOfFile(h->file_mapping, FILE_MAP_READ, 0, 0, 0);
    if (!h->base) {
        return std::unexpected("MapViewOfFile failed");
    }
    MemoryView v;
    v.view_ = std::span<const std::uint8_t>{
        static_cast<const std::uint8_t*>(h->base), h->length};
    v.mmap_handle_ = std::static_pointer_cast<void>(
        std::shared_ptr<WinMmapHandle>{std::move(h)});
    return v;
#else
    return std::unexpected("mmap not available on this platform");
#endif
}

std::expected<MemoryView, std::string>
MemoryView::read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return std::unexpected(std::string{"cannot open: "} + path.string());
    }
    const std::streamsize sz = in.tellg();
    if (sz < 0) return std::unexpected("tellg() failed");
    in.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
    if (sz > 0) {
        if (!in.read(reinterpret_cast<char*>(buf.data()), sz)) {
            return std::unexpected("read() failed");
        }
    }
    return own(std::move(buf));
}

std::expected<MemoryView, std::string>
MemoryView::from_file(const std::filesystem::path& path, bool prefer_mmap) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(std::string{"file does not exist: "} + path.string());
    }
    const auto sz = std::filesystem::file_size(path, ec);
    if (ec) {
        return std::unexpected(std::string{"file_size() failed: "} + ec.message());
    }
    // Below 4 KiB the syscall/page-fault cost of mmap dominates — just read it.
    if (prefer_mmap && sz >= 4096) {
        if (auto r = mmap_file(path)) return r;
        // fall through to heap read on mmap failure
    }
    return read_file(path);
}

std::expected<std::span<const std::uint8_t>, std::string>
MemoryView::subspan_checked(std::size_t offset, std::size_t count) const {
    if (offset > view_.size()) {
        return std::unexpected("offset > size");
    }
    const std::size_t avail = view_.size() - offset;
    if (count > avail) {
        return std::unexpected("count exceeds available bytes");
    }
    return view_.subspan(offset, count);
}

} // namespace tts
