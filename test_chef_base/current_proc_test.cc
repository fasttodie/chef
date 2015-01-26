#include "chef_current_proc.h"
#include <assert.h>

int main()
{
    printf(">current_proc_test.\n");
    printf("    %d\n", chef::current_proc::getpid());
    printf("    %s\n", chef::current_proc::obtain_bin_path().c_str());
    printf("    %s\n", chef::current_proc::obtain_bin_name().c_str());
    assert(chef::current_proc::obtain_bin_name() == "current_proc_test");
    printf("    %s\n", chef::current_proc::obtain_bin_dir().c_str());
    printf("    %d\n", chef::current_proc::get_cpu_num());
    printf("<current_proc_test.\n");
    return 0;
}

