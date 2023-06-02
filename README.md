# Compile and install

## user space benchmark
1. download LiTL (https://github.com/multicore-locks/litl)
2. place ron.c, tickron.c plockron.c in (dir of LiTL)/src
3. Compile and execute according to LiTL's instructions
 
## kernel space
1. Download Linux kernel 5.12.1 from (https://mirrors.edge.kernel.org/pub/linux/kernel/v5.x/linux-5.12.1.tar.xz).
2. Use qspinlock.patch to patch (kernel source)/kernel/locking/qspinlock.c. (https://www.howtogeek.com/415442/how-to-apply-a-patch-to-a-file-and-create-patches-in-linux/)
3. Compile and install the kernel using the instructions provided at (https://linuxhint.com/compile-and-install-kernel-ubuntu/).

## Customize TSP_ORDER based on your own processor
1. Obtain the cost of communication between cores from the following source: (https://github.com/nviennot/core-to-core-latency).
2. Generate the shortest path using Google OR Tools. Refer to the documentation available at (https://developers.google.com/optimization).
3. For these four files (ron.c, plockron.c, tickron.c, qspinlock.c), write the shortest path into an array. Taking AMD 2990WX as an example, the array format should be:

   int idCov[64] = { 17, 48, 22, 6,  29, 50, 58, 55, 35, 4,  26, 10, 28,
           40, 12, 15, 57, 46, 54, 63, 25, 61, 37, 0,  14, 44,
           30, 49, 52, 1,  9,  38, 7,  34, 60, 33, 24, 20, 59,
           41, 5,  8,  45, 13, 43, 31, 36, 21, 19, 23, 51, 3,
           11, 32, 27, 39, 62, 18, 42, 47, 53, 16, 56, 2 };

