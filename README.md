# ron-osdi2023

# user space benchmark
1. download LiTL (https://github.com/multicore-locks/litl)
2. place ron.c, tickron.c plockron.c in (dir of LiTL)/src
3. Compile and execute according to LiTL's instructions
 
# kernel space
1. Download Linux kernel 5.12.1 from (https://mirrors.edge.kernel.org/pub/linux/kernel/v5.x/linux-5.12.1.tar.xz).
2. Use qspinlock.patch to patch (kernel source)/kernel/locking/qspinlock.c.
3. Compile and install the kernel using the instructions provided at (https://linuxhint.com/compile-and-install-kernel-ubuntu/).

# costomize the TSP_ORDER for your own
1. Obtain the cost of communication between cores from the following source: (https://github.com/nviennot/core-to-core-latency).
2. Generate the shortest path using Google OR Tools. Refer to the documentation available at (https://developers.google.com/optimization).
3. For these four files (ron.c, plockron.c, tickron.c, qspinlock.c), write the shortest path into an array. Taking AMD 2990WX as an example, the array format should be:
   int idCov[64] = { 17, 48, 22, 6,  29, 50, 58, 55, 35, 4,  26, 10, 28,
           40, 12, 15, 57, 46, 54, 63, 25, 61, 37, 0,  14, 44,
           30, 49, 52, 1,  9,  38, 7,  34, 60, 33, 24, 20, 59,
           41, 5,  8,  45, 13, 43, 31, 36, 21, 19, 23, 51, 3,
           11, 32, 27, 39, 62, 18, 42, 47, 53, 16, 56, 2 };


# Artifact Description
The Artifact includes an implementation of the RON algorithm using C11. To compare it with other algorithms, these C files need to be compiled together with LiTL. Use the methods provided by LiTL to run the applications, which can be simple multithreaded programs or regular applications like Google LevelDB. Since the RON algorithm is highly optimized for CPUs, users must understand how the cores are interconnected to construct the TSP_ORDER. If using an AMD 2990WX, it is possible to reproduce the results without modifying the code.

# Testing Environment
To access the testing environment, use the following command to log in:


**For accurate performance measurement of each application, we strongly recommend that reviewers do not log in to numa1 simultaneously. Otherwise, the experiment results may be inaccurate due to competing processors.**

If the reviewer has a 2990WX machine, they can create an account named 'osdi2023' on the machine and use sftp to copy the files from numa1 to their computer.

# File and Directory Structures
The artifact contains 5 directories,
* litl: LiTL is a library that allows executing a program based on Pthread mutex locks with another locking algorithm.
    * Author : Hugo Guiroux <hugo.guiroux at gmail dot com>
    * Related Publication: Multicore Locks: the Case is Not Closed Yet, Hugo Guiroux, Renaud Lachaize, Vivien Quéma, USENIX ATC'16.
    * source: https://github.com/multicore-locks/litl
    * The algorithm implemented in this paper is located in the litl/src directory in the home directory.
        * RON: ```ron.c```
        * RON-plock: ```plockron.c```
        * RON-Ticket: ```tickron.c``` 
* app_benchmark: Application-level Benchmarks
* linux-ron: The Linux kernel with RON
* others: It is a directory that related to implement the LiTL library
* Test: Store the testing program for Figure 5 to Figure 9 in our paper.
    * Figure: It is a directory store the figure generate from the test
    * raw_data: It is a directory that store the raw data execute from the figure.sh
    * Figure.py: generate figure and store it into the Figure directory
    * Figure.sh: Our testing shell script file, users can execute this file to get the final result. 
    * testing_code: This directory stores all the testing code by figure name.

# Microbenchmarks for Quantitative Analysis

* Step 1: Execute Figure.sh
```bash=
cd ~/test/
./Figure.sh
```

* Users can add parameters with Figure.sh
    * ``` ./Figure.sh ```: If users execute this file directory, it will execute all experiment from figure 5 to figure 9.
    * ```./Figrue.sh 5```: It will only implement the figure 5's test, in the same way, users can add 6, 7, 8, or 9 to get different experimental results
    * ```./Figure.sh 5 6 ```: Users can also add two to more parameters with Figure.sh, it will implement the corresponding figure experiment.
    * All of our test have the mechanism to wait the cpu temperature cool down to 60 degree celsius.

* Step 2: After execute the Figure.sh, users will have raw data in the raw_data directory and the figure.png files in Figure directory
* Step 3: Users can download the Figure directory in your local system
    * ``` $ scp -r osdi2023@numa1.cs.ccu.edu.tw:~/test/Figure/ ./ ```


# Application-level benchmarks
In this section, we use LD_PRELOAD to make the application use the modified lock-unlock functions. We have placed all scripts in the ~/app_benchmark directory. The corresponding lock-unlock algorithms and scripts are as follows:

| Algorithm | Script | 
| -------- | -------- | 
| MCS     | libmcs_spinlock.sh     | 
| C-BO-MCS     | libcbomcs_spinlock.sh     | 
| ticket     | libticket_original.sh     | 
| pthread     | libpthreadspin_original.sh     | 
| RON-ticket     | libtickron_original.sh     | 

## Figure 10
Google LevelDB provides a performance measurement tool called "db_bench". Therefore, we can measure the performance of different spinlock algorithms on LevelDB by using LD_PRELOAD. The following is the method of execution.
```bash=
cd ~/app_benchmark/
~/app_benchmark/libcbomcs_spinlock.sh ./benchmarks/leveldb-1.20/out-static/db_bench
sleep 60
~/app_benchmark/libticket_original.sh ./benchmarks/leveldb-1.20/out-static/db_bench
sleep 60
~/app_benchmark/libpthreadspin_original.sh ./benchmarks/leveldb-1.20/out-static/db_bench
sleep 60
~/app_benchmark/libtickron_original.sh ./benchmarks/leveldb-1.20/out-static/db_bench
```
Example of Results
![](https://i.imgur.com/9BzHj2B.png)



## Figure 11
Due to the need to provide the correct input file, we have created Shell scripts for two Splash2X applications. Please refer to the Splash2X user manual (https://github.com/darchr/parsec-benchmark) for instructions on how to use it.


```bash=
cd ~/app_benchmark/benchmarks/splash2x
./test_ocean.sh
./test_ray.sh
```

Example of Results
![](https://i.imgur.com/L4CSCX2.png)


## Figure 12
1. Download Linux kernel 5.12.1
2. Replace the file kernel/locking/qspinlock.h with the one provided by the authors. (~/linux-ron/kernel/locking/qspinlock.h)
3. Compile and install Linux kernel
4. Switch between Linux kernels with and without RON using GRUB, and measure the time required for system calls on each kernel.

The spinlock algorithm used by the Linux currently running on numa1.cs.ccu.edu.tw is RON. Validating the performance of the algorithm requires switching Linux kernels. If the reviewer needs to verify this part of the performance, please let us know. We can switch the Linux kernel to the regular version at the time specified by reviewers.

# Evaluation and Expected Result

The aforementioned method can be used to compare the performance of RON with other algorithms. The reviewer was able to replicate almost all experimental results. As far as we know, there is a higher degree of variability in the results of the experiment shown in Figure 5.a, possibly due to the fact that all algorithms have similar performance under low load conditions.

Please refer to the following figure. The AMD 2990WX will automatically increase the execution frequency (AMD Turbo Core) when the temperature is lower to achieve better performance. Therefore, during each experiment, waitTemp is used to wait for the CPU temperature to drop to the specified temperature before starting a new round of experiments.

The experiments in the paper were conducted during winter, so the waitTemp parameter was set to 35 degrees. However, since the current temperature is higher, the CPU temperature during idle is 57 degrees, so the waitTemp parameter for the new experiment is set to 60 degrees. As the difference between experiments (a) and (b) is only waitTemp, we believe that the temperature has a greater impact on RON under low loads.


![](https://i.imgur.com/ntfngEq.png)


