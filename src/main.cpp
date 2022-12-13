#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fuse.hpp"

using Stat    = struct stat;
using Statvfs = struct statvfs;

class FileHandle {
  private:
    int  fd;
    bool opened;

  public:
    operator int() const {
        return fd;
    }

    FileHandle(const char* const path, const int mode, fuse_file_info* const fi) {
        if(fi != NULL) {
            fd     = fi->fh;
            opened = false;
        } else {
            fd     = ::open(path, mode);
            opened = true;
        }
    }

    ~FileHandle() {
        if(!opened) {
            return;
        }
        ::close(fd);
    }
};

namespace {
auto init(fuse_conn_info* const /*conn*/, fuse_config* const cfg) -> void* {
    cfg->entry_timeout    = 0;
    cfg->entry_timeout    = 0;
    cfg->attr_timeout     = 0;
    cfg->negative_timeout = 0;
    return NULL;
}

auto getattr(const char* const path, Stat* const stbuf, fuse_file_info* /*fi*/) -> int {
    const auto res = ::lstat(path, stbuf);
    return res == -1 ? -errno : 0;
}

auto access(const char* const path, const int mask) -> int {
    const auto res = ::access(path, mask);
    return res == -1 ? -errno : 0;
}

auto readlink(const char* const path, char* const buf, const size_t size) -> int {
    const auto res = ::readlink(path, buf, size - 1);
    if(res == -1) {
        return -errno;
    }
    buf[res] = '\0';
    return 0;
}

auto readdir(const char* const path, void* const buf, const fuse_fill_dir_t filler, const off_t /*offset*/, fuse_file_info* const /*fi*/, const fuse_readdir_flags /*flags*/) -> int {
    const auto dir = opendir(path);
    if(dir == NULL) {
        return errno;
    }

    auto de = (dirent*)(nullptr);
    while((de = ::readdir(dir)) != NULL) {
        auto st = Stat();
        memset(&st, 0, sizeof(st));
        st.st_ino  = de->d_ino;
        st.st_mode = de->d_type << 12;
        if(filler(buf, de->d_name, &st, 0, FUSE_FILL_DIR_PLUS)) {
            break;
        }
    }
    closedir(dir);
    return 0;
}

auto mkdir(const char* const path, const mode_t mode) -> int {
    const auto res = ::mkdir(path, mode);
    return res == -1 ? -errno : 0;
}

auto unlink(const char* const path) -> int {
    const auto res = ::unlink(path);
    return res == -1 ? -errno : 0;
}

auto rmdir(const char* const path) -> int {
    const auto res = ::rmdir(path);
    return res == -1 ? -errno : 0;
}

auto symlink(const char* const from, const char* const to) -> int {
    const auto res = ::symlink(from, to);
    return res == -1 ? -errno : 0;
}

auto rename(const char* const from, const char* const to, const unsigned int flag) -> int {
    if(flag) {
        return -EINVAL;
    }

    const auto res = ::rename(from, to);
    return res == -1 ? -errno : 0;
}

auto link(const char* const from, const char* const to) -> int {
    const auto res = ::link(from, to);
    return res == -1 ? -errno : 0;
}

auto chmod(const char* const path, const mode_t mode, fuse_file_info* const /*fi*/) -> int {
    const auto res = ::chmod(path, mode);
    return res == -1 ? -errno : 0;
}

auto chown(const char* const path, const uid_t uid, const gid_t gid, fuse_file_info* const /*fi*/) -> int {
    const auto res = ::lchown(path, uid, gid);
    return res == -1 ? -errno : 0;
}

auto truncate(const char* const path, const off_t size, fuse_file_info* const fi) -> int {
    const auto res = fi != NULL ? ::ftruncate(fi->fh, size) : ::truncate(path, size);
    return res == -1 ? -errno : 0;
}

auto create(const char* path, const mode_t mode, fuse_file_info* const fi) -> int {
    const auto res = ::open(path, fi->flags, mode);
    fi->fh         = res;
    return 0;
}

auto open(const char* const path, fuse_file_info* const fi) -> int {
    const auto res = ::open(path, fi->flags);
    fi->fh         = res;
    return 0;
}

auto read(const char* const path, char* const buf, const size_t size, const off_t offset, fuse_file_info* const fi) -> int {
    const auto file = FileHandle(path, O_RDONLY, fi);
    auto       res  = ::pread(file, buf, size, offset);
    return res == -1 ? -errno : 0;
}

auto write(const char* const path, const char* const buf, const size_t size, const off_t offset, fuse_file_info* const fi) -> int {
    const auto file = FileHandle(path, O_WRONLY, fi);
    auto       res  = ::pwrite(file, buf, size, offset);
    return res == -1 ? -errno : 0;
}

auto statfs(const char* const path, Statvfs* const stbuf) -> int {
    const auto res = ::statvfs(path, stbuf);
    return res == -1 ? -errno : 0;
}

auto release(const char* const /*path*/, fuse_file_info* const fi) -> int {
    ::close(fi->fh);
    return 0;
}

auto fallocate(const char* const path, const int mode, const off_t offset, const off_t length, fuse_file_info* const fi) -> int {
    if(mode) {
        return -EOPNOTSUPP;
    }

    const auto file = FileHandle(path, O_WRONLY, fi);
    if(file == -1) {
        return -errno;
    }
    return -::posix_fallocate(file, offset, length);
}

auto copy_file_range(const char* const path_in, fuse_file_info* const fi_in, off_t offset_in,
                     const char* const path_out, fuse_file_info* const fi_out, off_t offset_out,
                     const size_t len, const int flags) -> ssize_t {
    const auto file_in  = FileHandle(path_in, O_RDONLY, fi_in);
    const auto file_out = FileHandle(path_out, O_WRONLY, fi_out);
    if(file_in == -1 || file_out == -1) {
        return -errno;
    }
    const auto res = ::copy_file_range(file_in, &offset_in, file_out, &offset_out, len, flags);
    return res == -1 ? -errno : 0;
}

auto lseek(const char* const path, const off_t off, const int whence, fuse_file_info* const fi) -> off_t {
    const auto file = FileHandle(path, O_RDONLY, fi);
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
    .release         = release,
    .readdir         = readdir,
    .init            = init,
    .access          = access,
    .create          = create,
    .fallocate       = fallocate,
    .copy_file_range = copy_file_range,
    .lseek           = lseek,
};
} // namespace

auto main(const int argc, char* argv[]) -> int {
    return fuse_main(argc, argv, &operations, NULL);
}
