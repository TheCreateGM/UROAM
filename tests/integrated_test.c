#include <stdio.h>
#include <stdlib.h>
#include <uroam.h>

int main() {
    printf("=== MemOpt Integrated Test ===\n");
    
    int ret = memopt_init();
    printf("memopt_init returned: %d\n", ret);
    
    void* ptr1 = memopt_alloc(1024);
    printf("Allocated 1024 bytes at: %p\n", ptr1);
    
    void* ptr2 = memopt_alloc(4096);
    printf("Allocated 4096 bytes at: %p\n", ptr2);
    
    void* ptr3 = memopt_alloc(16384);
    printf("Allocated 16384 bytes at: %p\n", ptr3);
    
    memopt_free(ptr1);
    printf("Freed ptr1\n");
    
    void* ptr4 = memopt_alloc(2048);
    printf("Allocated 2048 bytes at: %p (should reuse free list)\n", ptr4);
    
    memopt_free(ptr2);
    memopt_free(ptr3);
    memopt_free(ptr4);
    
    memopt_print_stats();
    
    memopt_shutdown();
    printf("\nTest completed successfully!\n");
    
    return 0;
}
