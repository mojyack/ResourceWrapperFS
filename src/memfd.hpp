#pragma once
#include <memory>
#include <mutex>
#include <string_view>

#include <sys/mman.h>

#include "util/fd.hpp"

enum class OpenMode {
    Read,
    Write,
};

struct FileDeleter {
    auto operator()(FILE* const file) -> void {
        fclose(file);
    }
};

using File = std::unique_ptr<FILE, FileDeleter>;

template <OpenMode mode>
auto open_memory_file(const char* const name) -> File {
    const int fd = memfd_create(name, 0);
    if(fd == -1) {
        return NULL;
    }
    return File(fdopen(fd, mode == OpenMode::Read ? "rb" : "wb"));
}

inline auto extract_fd_from_file(const File& file) -> int {
    return dup(fileno(file.get()));
}

inline auto open_memory_fd(const char* const name) -> FileDescriptor {
    return FileDescriptor(memfd_create(name, 0));
}

inline auto reset_fd_cursor(const int fd) -> int {
    if(lseek(fd, 0, SEEK_SET) == -1) {
        return -1;
    }
    return fd;
}

inline auto get_fd_size(const int fd) -> ssize_t {
    static auto mutex = std::mutex();
    const auto  lock  = std::lock_guard(mutex); // block lseek for same fd at same time
    const auto  size  = lseek(fd, 0, SEEK_END);
    if(size == -1) {
        goto error;
    }
    if(reset_fd_cursor(fd) == -1) {
        goto error;
    }
    return size;
error:
    return -1;
}
