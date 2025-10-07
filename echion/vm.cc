#include <echion/vm.h>

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <algorithm>

void* VmReader::init(size_t new_sz)
{
    // Makes a temporary file and ftruncates it to the specified size
    std::array<std::string, 3> tmp_dirs = {"/dev/shm", "/tmp", "/var/tmp"};
    std::string tmp_suffix = "/echion-XXXXXX";
    void* ret = nullptr;

    for (auto& tmp_dir : tmp_dirs)
    {
        // Reset the file descriptor, just in case
        close(fd);
        fd = -1;

        // Create the temporary file
        std::string tmpfile = tmp_dir + tmp_suffix;
        fd = mkstemp(tmpfile.data());
        if (fd == -1)
            continue;

        // Unlink might fail if delete is blocked on the VFS, but currently no action is taken
        unlink(tmpfile.data());

        // Make sure we have enough size
        if (ftruncate(fd, new_sz) == -1)
        {
            continue;
        }

        // Map the file
        ret = mmap(NULL, new_sz, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
        if (ret == MAP_FAILED)
        {
            ret = nullptr;
            continue;
        }

        // Successful.  Break.
        sz = new_sz;
        break;
    }

    return ret;
}

VmReader::VmReader(size_t _sz) : sz{_sz}
{
    buffer = init(sz);
    if (!buffer)
    {
        throw std::runtime_error("Failed to initialize buffer with size " + std::to_string(sz));
    }
    instance = this;
}

VmReader* VmReader::get_instance()
{
    if (instance == nullptr)
    {
        try
        {
            instance = new VmReader(1024 * 1024);  // A megabyte?
        }
        catch (std::exception& e)
        {
            std::cerr << "Failed to initialize VmReader: " << e.what() << std::endl;
        }
    }
    return instance;
}

ssize_t VmReader::safe_copy(pid_t pid, const struct iovec* local_iov, unsigned long liovcnt,
                            const struct iovec* remote_iov, unsigned long riovcnt,
                            unsigned long flags)
{
    (void)pid;
    (void)flags;
    if (liovcnt != 1 || riovcnt != 1)
    {
        // Unsupported
        return 0;
    }

    // Check to see if we need to resize the buffer
    if (remote_iov[0].iov_len > sz)
    {
        if (ftruncate(fd, remote_iov[0].iov_len) == -1)
        {
            return 0;
        }
        else
        {
            void* tmp = mremap(buffer, sz, remote_iov[0].iov_len, MREMAP_MAYMOVE);
            if (tmp == MAP_FAILED)
            {
                return 0;
            }
            buffer = tmp;  // no need to munmap
            sz = remote_iov[0].iov_len;
        }
    }

    ssize_t ret = pwritev(fd, remote_iov, riovcnt, 0);
    if (ret == -1)
    {
        return ret;
    }

    // Copy the data from the buffer to the remote process
    std::memcpy(local_iov[0].iov_base, buffer, local_iov[0].iov_len);
    return ret;
}

VmReader::~VmReader()
{
    if (buffer)
    {
        munmap(buffer, sz);
    }
    if (fd != -1)
    {
        close(fd);
    }
    instance = nullptr;
}

bool read_process_vm_init()
{
    VmReader* _ = VmReader::get_instance();
    return !!_;
}

ssize_t vmreader_safe_copy(pid_t pid, const struct iovec* local_iov, unsigned long liovcnt,
                           const struct iovec* remote_iov, unsigned long riovcnt,
                           unsigned long flags)
{
    auto reader = VmReader::get_instance();
    if (!reader)
        return 0;
    return reader->safe_copy(pid, local_iov, liovcnt, remote_iov, riovcnt, flags);
}
__attribute__((constructor)) void init_safe_copy()
{
    char src[128];
    char dst[128];
    for (size_t i = 0; i < 128; i++)
    {
        src[i] = 0x41;
        dst[i] = ~0x42;
    }

    // Check to see that process_vm_readv works, unless it's overridden
    const char force_override_str[] = "ECHION_ALT_VM_READ_FORCE";
    const std::array<std::string, 6> truthy_values = {"1",  "true",   "yes",
                                                      "on", "enable", "enabled"};
    const char* force_override = std::getenv(force_override_str);
    if (!force_override || std::find(truthy_values.begin(), truthy_values.end(), force_override) ==
                               truthy_values.end())
    {
        struct iovec iov_dst = {dst, sizeof(dst)};
        struct iovec iov_src = {src, sizeof(src)};
        ssize_t result = process_vm_readv(getpid(), &iov_dst, 1, &iov_src, 1, 0);

        // If we succeed, then use process_vm_readv
        if (result == sizeof(src))
        {
            safe_copy = process_vm_readv;
            return;
        }
    }

    // Else, we have to setup the writev method
    if (!read_process_vm_init())
    {
        // std::cerr might not have been fully initialized at this point, so use
        // fprintf instead.
        fprintf(stderr, "Failed to initialize all safe copy interfaces\n");
        failed_safe_copy = true;
        return;
    }

    safe_copy = vmreader_safe_copy;
}

int copy_memory(proc_ref_t proc_ref, void* addr, ssize_t len, void* buf)
{
    ssize_t result = -1;

    // Early exit on zero page
    if (reinterpret_cast<uintptr_t>(addr) < 4096)
    {
        return result;
    }

#if defined PL_LINUX
    struct iovec local[1];
    struct iovec remote[1];

    local[0].iov_base = buf;
    local[0].iov_len = len;
    remote[0].iov_base = addr;
    remote[0].iov_len = len;

    result = safe_copy(proc_ref, local, 1, remote, 1, 0);

#elif defined PL_DARWIN
    kern_return_t kr = mach_vm_read_overwrite(proc_ref, (mach_vm_address_t)addr, len,
                                              (mach_vm_address_t)buf, (mach_vm_size_t*)&result);

    if (kr != KERN_SUCCESS)
        return -1;

#endif

    return len != result;
}

void _set_pid(pid_t _pid)
{
    pid = _pid;
}
