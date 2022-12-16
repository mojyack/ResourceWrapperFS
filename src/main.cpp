#include <filesystem>
#include <iostream>
#include <variant>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include "drivers/flac/driver.hpp"
#include "drivers/jxl/driver.hpp"
#include "fuse.hpp"
#include "util/string-map.hpp"
#include "util/thread.hpp"

using Stat    = struct stat;
using Statvfs = struct statvfs;

class WeakString {
  private:
    std::variant<std::string, std::string_view> data;

  public:
    auto view() const -> const std::string_view {
        if(data.index() == 0) {
            return std::get<0>(data);
        } else {
            return std::get<1>(data);
        }
    }

    auto cstr() const -> const char* {
        return view().data();
    }

    WeakString(std::string data) : data(std::move(data)) {}

    WeakString(const std::string_view view) : data(view) {}
};

using Drivers = std::tuple<drivers::jxl::Driver, drivers::flac::Driver>;

namespace {
auto root    = std::string();
auto drivers = Drivers();

using DecodedCache = StringMap<FileDescriptor>;

auto critical_decoded_cache = Critical<DecodedCache>();

template <size_t N>
auto to_real_path(const std::string_view path) -> WeakString {
    if constexpr(N < std::tuple_size_v<Drivers>) {
        auto& driver = std::get<N>(drivers);
        auto  result = driver.get_real_path(path);
        if(result) {
            return result.value();
        }
        return to_real_path<N + 1>(path);
    } else {
        return path;
    }
}

auto to_real_path(const std::string_view path) -> WeakString {
    if(std::filesystem::exists(path)) {
        return path;
    }
    return to_real_path<0>(path);
}

template <size_t N = 0>
auto to_phantom_paths(const std::string_view path) -> std::vector<WeakString> {
    if constexpr(N < std::tuple_size<Drivers>::value) {
        auto& driver = std::get<N>(drivers);
        auto  result = driver.get_phantom_paths(path);
        if(result) {
            auto r = std::vector<WeakString>();
            r.reserve(result.value().size());
            for(auto& s : result.value()) {
                r.emplace_back(std::move(s));
            }
            return r;
        }
        return to_phantom_paths<N + 1>(path);
    } else {
        return {path};
    }
}

template <size_t N>
auto open_phantom_file_by_driver(const char* const path, const int mode) -> std::optional<int> {
    if constexpr(N < std::tuple_size<Drivers>::value) {
        auto& driver = std::get<N>(drivers);
        auto  result = driver.open_phantom_file(path);
        if(result) {
            return result.value();
        }
        return open_phantom_file_by_driver<N + 1>(path, mode);
    } else {
        return std::nullopt;
    }
}

auto open_phantom_file(const std::string_view path, const char* const abs, const int mode) -> int {
    if(std::filesystem::exists(abs)) {
        return ::open(abs, mode);
    }

    {
        auto [lock, decoded_cache] = critical_decoded_cache.access();
        if(const auto p = decoded_cache.find(path); p != decoded_cache.end()) {
            return p->second.as_handle();
        }
    }

    const auto phantom_file = open_phantom_file_by_driver<0>(abs, mode);
    if(!phantom_file) {
        return -1;
    }

    const auto new_file = phantom_file.value();

    {
        auto [lock, decoded_cache] = critical_decoded_cache.access();
        decoded_cache.emplace(path, new_file);
    }

    return new_file;
}

auto close_phantom_file(const std::string_view path) -> bool {
    auto [lock, decoded_cache] = critical_decoded_cache.access();
    if(const auto p = decoded_cache.find(path); p != decoded_cache.end()) {
        decoded_cache.erase(p);
        return true;
    } else {
        return false;
    }
}

auto is_cached_phantom_file(const std::string_view path) -> bool {
    auto [lock, decoded_cache] = critical_decoded_cache.access();
    return decoded_cache.find(path) != decoded_cache.end();
}

class FileHandle {
  private:
    int  fd;
    bool opened;

  public:
    operator int() const {
        return fd;
    }

    FileHandle(const std::string_view path, const char* const abs, const int mode, fuse_file_info* const fi) {
        if(fi != NULL) {
            fd     = fi->fh;
            opened = false;
        } else {
            fd     = open_phantom_file(path, abs, mode);
            opened = true;
        }
    }

    ~FileHandle() {
        if(opened) {
            ::close(fd);
        }
    }
};

auto init(fuse_conn_info* const /*conn*/, fuse_config* const cfg) -> void* {
    cfg->entry_timeout    = 0;
    cfg->entry_timeout    = 0;
    cfg->attr_timeout     = 0;
    cfg->negative_timeout = 0;
    return NULL;
}

auto getattr(const char* const path, Stat* const stbuf, fuse_file_info* /*fi*/) -> int {
    const auto abs      = root + path;
    const auto new_path = to_real_path(abs);
    const auto res      = ::lstat(new_path.cstr(), stbuf);

    if(new_path.view() != abs) {
        // this is phantom file
        // we have to set proper file size
        const auto file = open_phantom_file(path, abs.data(), 0);
        const auto size = get_fd_size(file);
        if(size != -1) {
            stbuf->st_size = size;
        } else {
            return -errno;
        }
    }

    return res == -1 ? -errno : 0;
}

auto access(const char* const path, const int mask) -> int {
    const auto abs      = root + path;
    const auto new_path = to_real_path(abs);
    const auto res      = ::access(new_path.cstr(), mask);
    return res == -1 ? -errno : 0;
}

auto readlink(const char* const path, char* const buf, const size_t size) -> int {
    const auto abs      = root + path;
    const auto new_path = to_real_path(abs);
    const auto res      = ::readlink(new_path.cstr(), buf, size - 1);
    if(res == -1) {
        return -errno;
    }
    buf[res] = '\0';
    return 0;
}

auto readdir(const char* const path, void* const buf, const fuse_fill_dir_t filler, const off_t /*offset*/, fuse_file_info* const /*fi*/, const fuse_readdir_flags /*flags*/) -> int {
    const auto abs = root + path;
    const auto dir = opendir(abs.data());
    if(dir == NULL) {
        return errno;
    }

    auto de = (dirent*)(nullptr);
    while((de = ::readdir(dir)) != NULL) {
        auto st = Stat();
        memset(&st, 0, sizeof(st));
        st.st_ino  = de->d_ino;
        st.st_mode = de->d_type << 12;

        for(const auto& file : to_phantom_paths(abs + "/" + de->d_name)) {
            const auto new_filename = std::filesystem::path(file.view()).filename().string();
            if(filler(buf, new_filename.data(), &st, 0, FUSE_FILL_DIR_PLUS)) {
                goto finish;
            }
        }
    }
finish:
    closedir(dir);
    return 0;
}

auto mkdir(const char* const path, const mode_t mode) -> int {
    const auto abs = root + path;
    const auto res = ::mkdir(abs.data(), mode);
    return res == -1 ? -errno : 0;
}

auto unlink(const char* const path) -> int {
    const auto abs = root + path;
    const auto res = ::unlink(abs.data());
    return res == -1 ? -errno : 0;
}

auto rmdir(const char* const path) -> int {
    const auto abs = root + path;
    const auto res = ::rmdir(abs.data());
    return res == -1 ? -errno : 0;
}

auto symlink(const char* const from, const char* const to) -> int {
    const auto abs_from = root + from;
    const auto abs_to   = root + to;
    const auto res      = ::symlink(abs_from.data(), abs_to.data());
    return res == -1 ? -errno : 0;
}

auto rename(const char* const from, const char* const to, const unsigned int flag) -> int {
    if(flag) {
        return -EINVAL;
    }

    const auto abs_from = root + from;
    const auto abs_to   = root + to;
    const auto res      = ::rename(abs_from.data(), abs_to.data());
    return res == -1 ? -errno : 0;
}

auto link(const char* const from, const char* const to) -> int {
    const auto abs_from = root + from;
    const auto abs_to   = root + to;
    const auto res      = ::link(abs_from.data(), abs_to.data());
    return res == -1 ? -errno : 0;
}

auto chmod(const char* const path, const mode_t mode, fuse_file_info* const /*fi*/) -> int {
    const auto abs      = root + path;
    const auto new_path = to_real_path(abs);
    const auto res      = ::chmod(new_path.cstr(), mode);
    return res == -1 ? -errno : 0;
}

auto chown(const char* const path, const uid_t uid, const gid_t gid, fuse_file_info* const /*fi*/) -> int {
    const auto abs      = root + path;
    const auto new_path = to_real_path(abs);
    const auto res      = ::lchown(new_path.cstr(), uid, gid);
    return res == -1 ? -errno : 0;
}

auto truncate(const char* const path, const off_t size, fuse_file_info* const fi) -> int {
    const auto abs      = root + path;
    const auto new_path = to_real_path(abs);
    const auto res      = fi != NULL ? ::ftruncate(fi->fh, size) : ::truncate(new_path.cstr(), size);
    return res == -1 ? -errno : 0;
}

auto create(const char* path, const mode_t mode, fuse_file_info* const fi) -> int {
    const auto abs = root + path;
    const auto res = ::open(abs.data(), fi->flags, mode);
    fi->fh         = res;
    return 0;
}

auto utimens(const char* const path, const timespec tv[2], fuse_file_info* const fi) -> int {
    const auto abs  = root + path;
    const auto file = FileHandle(path, abs.data(), O_WRONLY, fi);
    auto       res  = ::futimens(file, tv);
    return res == -1 ? -errno : 0;
}

auto open(const char* const path, fuse_file_info* const fi) -> int {
    const auto abs = root + path;
    const auto res = open_phantom_file(path, abs.data(), fi->flags);
    fi->fh         = res;
    return 0;
}

auto read(const char* const path, char* const buf, const size_t size, const off_t offset, fuse_file_info* const fi) -> int {
    const auto abs  = root + path;
    const auto file = FileHandle(path, abs.data(), O_RDONLY, fi);
    auto       res  = ::pread(file, buf, size, offset);
    return res == -1 ? -errno : res;
}

auto write(const char* const path, const char* const buf, const size_t size, const off_t offset, fuse_file_info* const fi) -> int {
    const auto abs  = root + path;
    const auto file = FileHandle(path, abs.data(), O_WRONLY, fi);
    auto       res  = ::pwrite(file, buf, size, offset);
    return res == -1 ? -errno : res;
}

auto statfs(const char* const path, Statvfs* const stbuf) -> int {
    const auto abs      = root + path;
    const auto new_path = to_real_path(abs);
    const auto res      = ::statvfs(new_path.cstr(), stbuf);
    return res == -1 ? -errno : 0;
}

auto release(const char* const path, fuse_file_info* const fi) -> int {
    constexpr auto do_not_delete_cache = true;

    if(do_not_delete_cache) {
        if(!is_cached_phantom_file(path)) {
            ::close(fi->fh);
        }
    } else {
        if(!close_phantom_file(path)) {
            ::close(fi->fh);
        }
    }

    return 0;
}

auto fallocate(const char* const path, const int mode, const off_t offset, const off_t length, fuse_file_info* const fi) -> int {
    if(mode) {
        return -EOPNOTSUPP;
    }

    const auto abs  = root + path;
    const auto file = FileHandle(path, abs.data(), O_WRONLY, fi);
    if(file == -1) {
        return -errno;
    }
    return -::posix_fallocate(file, offset, length);
}

auto setxattr(const char* const path, const char* const name, const char* const value, const size_t size, const int flags) -> int {
    const auto abs      = root + path;
    const auto new_path = to_real_path(abs);
    const auto res      = ::lsetxattr(path, name, value, size, flags);
    return res == -1 ? -errno : 0;
}

auto getxattr(const char* const path, const char* const name, char* const value, const size_t size) -> int {
    const auto abs      = root + path;
    const auto new_path = to_real_path(abs);
    const auto res      = ::lgetxattr(path, name, value, size);
    return res == -1 ? -errno : res;
}

auto listxattr(const char* const path, char* const list, const size_t size) -> int {
    const auto abs      = root + path;
    const auto new_path = to_real_path(abs);
    const auto res      = ::llistxattr(path, list, size);
    return res == -1 ? -errno : res;
}

auto removexattr(const char* const path, const char* const name) -> int {
    const auto abs      = root + path;
    const auto new_path = to_real_path(abs);
    const auto res      = ::lremovexattr(path, name);
    return res == -1 ? -errno : 0;
}

auto copy_file_range(const char* const path_in, fuse_file_info* const fi_in, off_t offset_in,
                     const char* const path_out, fuse_file_info* const fi_out, off_t offset_out,
                     const size_t len, const int flags) -> ssize_t {
    const auto abs_in   = root + path_in;
    const auto abs_out  = root + path_out;
    const auto file_in  = FileHandle(path_in, abs_in.data(), O_RDONLY, fi_in);
    const auto file_out = FileHandle(path_out, abs_out.data(), O_WRONLY, fi_out);
    if(file_in == -1 || file_out == -1) {
        return -errno;
    }
    const auto res = ::copy_file_range(file_in, &offset_in, file_out, &offset_out, len, flags);
    return res == -1 ? -errno : 0;
}

auto lseek(const char* const path, const off_t off, const int whence, fuse_file_info* const fi) -> off_t {
    const auto abs  = root + path;
    const auto file = FileHandle(path, abs.data(), O_RDONLY, fi);
    if(file == -1) {
        return -errno;
    }

    const auto res = ::lseek(file, off, whence);
    return res == -1 ? -errno : 0;
}

const auto operations = fuse_operations{
    .getattr         = getattr,
    .readlink        = readlink,
    .mknod           = mknod,
    .mkdir           = mkdir,
    .unlink          = unlink,
    .rmdir           = rmdir,
    .symlink         = symlink,
    .rename          = rename,
    .link            = link,
    .chmod           = chmod,
    .chown           = chown,
    .truncate        = truncate,
    .open            = open,
    .read            = read,
    .write           = write,
    .statfs          = statfs,
    .flush           = NULL,
    .release         = release,
    .fsync           = NULL,
    .setxattr        = setxattr,
    .getxattr        = getxattr,
    .listxattr       = listxattr,
    .removexattr     = removexattr,
    .opendir         = NULL,
    .readdir         = readdir,
    .releasedir      = NULL,
    .fsyncdir        = NULL,
    .init            = init,
    .destroy         = NULL,
    .access          = access,
    .create          = create,
    .lock            = NULL,
    .utimens         = utimens,
    .bmap            = NULL,
    .ioctl           = NULL,
    .poll            = NULL,
    .write_buf       = NULL,
    .read_buf        = NULL,
    .flock           = NULL,
    .fallocate       = fallocate,
    .copy_file_range = copy_file_range,
    .lseek           = lseek,
};
} // namespace

auto main(const int argc, char* argv[]) -> int {
    for(auto i = 1; i < argc; i += 1) {
        if(argv[i][0] != '-') {
            root = std::filesystem::absolute(argv[i]).string() + ".dev";
            if(!std::filesystem::is_directory(root)) {
                std::cerr << "device dir \"" << root << "\" is not a directory";
            }
        }
    }

    return fuse_main(argc, argv, &operations, NULL);
}
