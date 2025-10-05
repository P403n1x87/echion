#include <sys/types.h>
#include <iostream>

typedef pid_t proc_ref_t;

int copy_memory(proc_ref_t proc_ref, void* addr, ssize_t len, void* buf) {
    std::cout << "copy_memory called with proc_ref: " << proc_ref << ", addr: " << addr << ", len: " << len << ", buf: " << buf << std::endl;
    return 0;    
}
