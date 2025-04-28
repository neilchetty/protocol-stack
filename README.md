# C Protocol Stack Simulator

## Overview

This project simulates a basic network protocol stack implemented in C. It demonstrates the flow of data through different layers, from Application down to a simulated Physical layer and back up. Communication between two instances of the simulator is achieved using POSIX shared memory and semaphores to mimic a physical link.

The implemented layers include:

* **Application Layer:** Simple string message passing.
* **Transport Layer:** Basic UDP implementation (header addition, no checksum verification).
* **Network Layer:** Simplified IP-like layer with header addition, header checksum calculation/verification, and basic fragmentation support (reassembly logic is currently limited to non-fragmented packets).
* **Data Link Layer:** Implements framing (start/end flags), byte stuffing/destuffing, and a simple 1-byte checksum for frame integrity.
* **Physical Layer:** Simulated using POSIX shared memory and semaphores for inter-process communication between two running instances.
* **Concurrency:** Uses a thread pool (`thpool`) to handle asynchronous processing for packets moving up the stack (Physical -> Data Link, Data Link -> Network, etc.) and optionally for packets moving down (Transport -> Network).

## Requirements

* **C Compiler:** A standard C compiler like GCC or Clang.
* **Make:** The `make` build utility.
* **POSIX System:** A POSIX-compliant operating system (like Linux or macOS) that supports:
    * Pthreads (for the thread pool and receiver thread).
    * POSIX Shared Memory (`shm_open`, `mmap`, etc.).
    * POSIX Semaphores (`sem_open`, `sem_post`, `sem_trywait`, etc.).

## Cloning the Source Code

Make sure that `git` is installed. This project uses `git submodule` for thread-pool.

1. Open your terminal in the directory where you want to clone the project.
    ```bash
    git clone https://github.com/neilchetty/protocol-stack.git
    cd protocol-stack
    ```
2. To clone the remaining submodule-
    ```bash
    git submodule init
    git submodule update
    ```
3. Now you are read to build the project.

## Building the Simulator

The project includes a `Makefile` to simplify compilation.

1.  Open your terminal in the project's root directory (where the `Makefile` is located).
2.  Run the `make` command:
    ```bash
    make
    ```
    This will compile all source files and create an executable named `protocol_stack` in the root directory. Object files (`.o`) will be placed in the `build/` subdirectory.

3.  To clean up build files (object files and the executable), run:
    ```bash
    make clean
    ```

## Running the Simulation

To simulate communication between two nodes (e.g., `nic1` and `nic2`):

1.  **Open two separate terminal windows.**
2.  **Terminal 1 (Start `nic1` sending to `nic2`):**
    ```bash
    ./build/protocol_stack nic1 nic2
    ```
3.  **Terminal 2 (Start `nic2` sending to `nic1`):**
    ```bash
    ./build/protocol_stack nic2 nic1
    ```

* The first argument (`nic1` or `nic2`) is the identifier this instance uses for its own shared memory (its "listening address").
* The second argument (`nic2` or `nic1`) is the identifier of the instance it will attempt to send messages to.

**Observing Output:**

* The program will print detailed debug messages (prefixed by the layer, e.g., `PHYSICAL:`, `DATALINK:`, `NETWORK:`, `TRANSPORT:`, `APP:`) showing the flow of data down the stack on sending and up the stack on receiving.
* ANSI color codes are used to differentiate the output from different layers.
* By default, each instance sends a message every 10 seconds. You should see messages sent by `nic1` being received and processed by `nic2`, and vice-versa.

**Stopping the Simulation:**

* Press `Ctrl+C` in each terminal window to gracefully shut down the corresponding instance. The program will attempt to clean up shared memory and semaphore resources.

## Notes
* The network layer's reassembly logic is currently very basic and only reliably handles non-fragmented packets.
* Error handling is basic.
