Here is a summary and explanation of the `ryzen_monitor` and `ryzen_smu` software projects, with a focus on the AMD Ryzen 5 5625U processor.

### Summary of `ryzen_monitor` and `ryzen_smu`

*   **`ryzen_smu`**: This is a Linux kernel driver that provides low-level access to the System Management Unit (SMU) on AMD Ryzen processors. The SMU is a microcontroller embedded within the CPU that is responsible for power management, monitoring, and control. The `ryzen_smu` driver exposes the SMU's functionality to userspace through the sysfs file system, allowing for the reading of various processor metrics.

*   **`ryzen_monitor`**: This is a command-line userspace application that reads data from the `ryzen_smu` driver and displays it in a human-readable format. It is a continuation of a demo tool originally provided with `ryzen_smu` and has been expanded to support multiple generations of Ryzen processors and provide more detailed information. It is particularly focused on providing a realistic view of the CPU's power consumption and thermal output.

In essence, `ryzen_smu` acts as a bridge between the hardware (the SMU) and the operating system's kernel, while `ryzen_monitor` is the tool that presents the data gathered by that bridge to the end-user.

### Components, Interfaces, and Data Structures

For the **AMD Ryzen 5 5625U**, which is based on the "Cezanne" architecture, here's a breakdown of how these software projects work together:

#### **Components**

*   **`ryzen_smu` (Kernel Module)**:
    *   **Core Logic (`drv.c`, `smu.c`)**: This is the heart of the driver. It handles the initialization of the driver, detects the specific Ryzen processor, and creates the necessary sysfs interface. For the Ryzen 5 5625U ("Cezanne"), the driver identifies the CPU family and model to apply the correct communication protocols with the SMU.
    *   **SMU Communication (`smu.c`)**: This component manages the direct communication with the SMU. It sends commands to the SMU to request specific data, such as power consumption, temperatures, and clock speeds for each core.
    *   **Sysfs Interface (`drv.c`)**: The driver creates a directory under `/sys/kernel/ryzen_smu_drv/` which contains several files. The most important of these is `pm_table` (Power Metrics Table), which contains an array of floating point values with a wide range of sensor data from the SMU. Other files provide information like the SMU firmware version and processor codename.

*   **`ryzen_monitor` (Userspace Application)**:
    *   **Main Application (`ryzen_monitor.c`)**: This is the entry point of the program. It handles command-line arguments, sets up a loop to periodically refresh the data, and calls other components to fetch and display the information.
    *   **SMU Data Reader (`libsmu.c`)**: This library within `ryzen_monitor` is responsible for interacting with the sysfs interface created by the `ryzen_smu` driver. It opens and reads the `/sys/kernel/ryzen_smu_drv/pm_table` file to get the raw data from the SMU.
    *   **PM Table Parser (`pm_tables.c`)**: The raw data from the `pm_table` is just a binary blob. This component contains the logic to parse this data. It uses predefined data structures (structs) that map to the layout of the `pm_table` for different processor generations. For the Ryzen 5 5625U ("Cezanne"), it would use the specific `pm_table_0x400005` definition to correctly interpret the offsets of different metrics within the binary data.
    *   **System Information (`readinfo.c`)**: This component gathers general system information, such as the CPU model name and the number of cores, by using the `cpuid` instruction. This information is then displayed at the top of the output.
    *   **Display Logic (`ryzen_monitor.c`)**: This part of the application takes the parsed data and formats it into the tables seen in the example output. It calculates derived statistics like the highest core frequency and average core voltage.

#### **Interfaces**

The primary interface between `ryzen_smu` and `ryzen_monitor` is the **sysfs file system**.

*   `ryzen_smu` exposes a set of "files" in `/sys/kernel/ryzen_smu_drv/`.
*   `ryzen_monitor` reads these files to get information. The key file is `pm_table`, which is read to get a snapshot of the processor's current state.
*   This design decouples the kernel-level hardware interaction from the user-facing application. The driver handles the complexities and potential risks of direct hardware access, while the application can safely read the data as if it were a regular file.

#### **Data Structures**

*   **`pm_table` (in `ryzen_monitor`)**: This is the most crucial data structure. It is a large C `struct` defined in `pm_tables.h`. This struct acts as a template that is overlaid onto the raw binary data read from the `/sys/kernel/ryzen_smu_drv/pm_table` file.
    *   For the Ryzen 5 5625U ("Cezanne"), `ryzen_monitor` would use a specific version of the `pm_table` struct that corresponds to the "Cezanne" architecture. This is necessary because the layout and meaning of the data in the `pm_table` can change between different CPU generations.
    *   The `pm_table` struct contains pointers to floating-point values for a vast array of metrics, including:
        *   Per-core power, voltage, temperature, and residency in different C-states (C0, C1, C6).
        *   Power limits like PPT (Package Power Tracking), TDC (Thermal Design Current), and EDC (Electrical Design Current).
        *   Temperatures for different parts of the CPU (SoC, GFX).
        *   Clock frequencies for the fabric, memory, and graphics.
        *   Power consumption of various components like the SoC, memory interface, and I/O.

*   **`system_info` (in `ryzen_monitor`)**: This struct, defined in `readinfo.h`, is used to store general information about the processor that is not directly obtained from the SMU `pm_table`. This includes the CPU name, codename, number of cores, and CCDs (Core Complex Dies). This information is primarily gathered using the `cpuid` instruction.



#### The Structure of the Cezanne PM Table

The PM table is a binary block of data containing an array of 32-bit floating-point values. Its structure is not fixed; it varies with the BIOS (AGESA) version. To correctly parse this data, it's essential to first identify the table's version number. For Cezanne, a common version is **`0x400005`**. `ryzen_monitor` uses this version to select the appropriate data structure and function (`pm_table_0x400005`) to map the binary data to human-readable metrics.

#### Key Metrics Found in the Cezanne PM Table

The following is a breakdown of the critical information contained within the Cezanne PM table, as deciphered by the community:

##### 1.  CPU Core Metrics (Per-Core)
*   **Core Power**: The instantaneous power consumption of each individual CPU core in Watts.
*   **Core Voltage**: The voltage supplied to each core.
*   **Core Temperature**: The temperature of each core in degrees Celsius.
*   **Effective Frequency**: The average clock speed of a core after accounting for time spent in sleep states (C-states). This is a more accurate measure of performance than the instantaneous clock speed.
*   **C-State Residency**: The percentage of time a core spends in various sleep states (e.g., CC1, CC6). High residency in deep sleep states like CC6 is indicative of efficient power saving during idle periods.

##### 2.  Electrical & Thermal Constraints
*   **PPT (Package Power Tracking)**: The total power being consumed by the entire APU package and the configured limit in Watts.
*   **TDC (Thermal Design Current)**: The maximum sustained current the motherboard's voltage regulators can supply to the APU, limited by thermal constraints.
*   **EDC (Electrical Design Current)**: The maximum peak current the voltage regulators can supply for short durations.

##### 3.  Integrated Graphics (iGPU) Metrics
*   **GFX Power & Voltage**: The power consumption and voltage of the integrated Radeon graphics.
*   **GFX Temperature**: The temperature of the iGPU.
*   **GFX Clock**: The operating frequency of the iGPU.
*   **GFX Busy**: The utilization percentage of the iGPU.

##### 4.  Memory and Infinity Fabric
*   **FCLK (Fabric Clock)**: The frequency of the Infinity Fabric, which is the communication backbone of the APU.
*   **MCLK (Memory Clock)**: The clock speed of the system's RAM.
*   **UCLK (Uncore Clock)**: The clock speed of the memory controller. For optimal performance, UCLK and MCLK should be synchronized.

##### 5.  Granular Power Consumption
*   **SoC Power**: The power consumed by the System-on-a-Chip components, excluding the CPU cores and iGPU.
*   **L3 Cache Power**: Power usage of the L3 cache, broken down into logic and memory components.
*   **I/O Power**: Power consumed by various input/output interfaces, such as USB and display controllers.


### How the Kernel Driver Reads the PM Table

The process is not a direct read from the SMU's internal memory. Instead, it's a clever two-step mechanism that uses the system's main RAM as a staging area.

1.  **Command the SMU to Copy Data to DRAM**:
    *   When a read is requested, the kernel driver first sends a specific command to the SMU via its mailbox interface. This command is typically named `TransferTableSmu2Dram`.
    *   This command instructs the SMU to perform a DMA (Direct Memory Access) transfer, copying the entire, up-to-date PM table from its internal memory to a pre-negotiated, contiguous block of physical system RAM (DRAM).
    *   The physical memory address of this block is determined once during the driver's initialization by querying the SMU.

2.  **Read the Data from DRAM**:
    *   After sending the command, the driver reads the data *from that specific location in the system's RAM*, not from the SMU itself.
    *   In kernel space, this is done using functions like `ioremap_cache` to create a virtual mapping to the physical DRAM address, and then `memcpy_fromio` to safely copy the data from this memory-mapped I/O region into a buffer that can be passed to the userspace application.

This indirect method is efficient and safer than trying to directly access the memory of an embedded controller like the SMU.

### Reading All at Once vs. Partial Reads

The driver **always reads the entire PM table at once**. It is an "all-or-nothing" operation.

*   **Why?** The SMU presents the PM table as a single, contiguous data structure. The `TransferTableSmu2Dram` command is designed to dump this entire block. There is no known mechanism or command to request only a specific offset or a partial chunk of the table.
*   **Interface Design**: The sysfs interface exposes this data as a single "file" (`/sys/kernel/ryzen_smu_drv/pm_table`). When a program reads this file, the kernel's read handler is triggered, which executes the two-step process described above and returns the entire buffer. The size of this buffer is known to the driver based on the processor's family and the PM table version.
*   **Is it slower?** Not really. The overhead is in sending the command to the SMU and waiting for the DMA transfer to complete. The actual `memcpy_fromio` operation is incredibly fast. Reading a 4KB block from RAM is trivial for a modern CPU. Therefore, even if partial reads were possible, the latency of sending a command to the SMU for each small piece of data would be far, far slower than getting the whole block at once.

### Is the Interface Always Polling?

This is a key point that requires distinguishing between the userspace application and the kernel driver.

*   **Userspace (e.g., `ryzen_monitor`) is Polling**: An application like `ryzen_monitor` operates by repeatedly reading the `/sys/kernel/ryzen_smu_drv/pm_table` file in a loop, usually with a `sleep()` interval (e.g., once per second). This is a classic polling model.

*   **Kernel Driver is On-Demand, Not Polling**: The `ryzen_smu` driver itself **does not poll the SMU**. It sits idle until a userspace program requests to read the sysfs file. The entire two-step process of commanding the SMU and reading from DRAM only happens *when a read operation is initiated on the file*.

To prevent a misbehaving userspace application from spamming the SMU with requests (e.g., polling thousands of times per second), the `ryzen_smu` driver includes a throttling mechanism. As seen in `smu.c`:

```c
// Check if we should tell the SMU to refresh the table via jiffies.
// Use a minimum interval of 1 ms.
if (!g_smu.pm_jiffies || time_after(jiffies, g_smu.pm_jiffies + msecs_to_jiffies(1))) {
    g_smu.pm_jiffies = jiffies;

    ret = smu_transfer_table_to_dram(dev);
    if (ret != SMU_Return_OK)
        return ret;
}
```

This code ensures that the `TransferTableSmu2Dram` command is sent to the SMU at most once every millisecond, regardless of how fast userspace is polling the file.

### Are the Maximum Data Rates Specified Somewhere?

There are **no official public specifications from AMD** on this. The maximum data rates are practical limitations derived from the hardware and the driver's implementation.

1.  **SMU Internal Update Rate**: The SMU itself is sampling its internal sensors at a very high frequency, likely in the kilohertz (kHz) range, to make its own power management decisions. This is the ultimate physical limit, and it is not publicly documented.

2.  **Driver-Imposed Rate Limit**: As shown in the code snippet above, the `ryzen_smu` driver intentionally limits the refresh command rate to **approximately 1000 Hz (1ms)**. This is the practical maximum refresh rate you could get from the driver.

3.  **Application-Determined Rate**: In reality, the rate is determined by the monitoring application. `ryzen_monitor` polls at 1 Hz by default. For human monitoring, a rate higher than 2-4 Hz is generally unnecessary and just consumes extra CPU cycles. The high-frequency data is primarily for the SMU's internal algorithms, not for external logging.

### Is the Measurement in the Kernel Module Triggered by open() or read()?

The measurement of the `pm_table` is initiated when the `/sys/kernel/ryzen_smu_drv/pm_table` file is **read**, not when it is opened.

A closer look at the provided source code reveals the specific execution flow:

1.  **Sysfs Attribute Creation (`drv.c`):** The driver creates a read-only sysfs attribute named `pm_table`. The function `pm_table_show` is registered as the handler for read operations on this file.

    ```c
    // in drv.c
    __RO_ATTR (pm_table); 
    
    static ssize_t pm_table_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
        if (smu_read_pm_table(g_driver.device, g_driver.pm_table, &g_driver.pm_table_read_size) != SMU_Return_OK)
            return 0;
    
        memcpy(buff, g_driver.pm_table, g_driver.pm_table_read_size);
        return g_driver.pm_table_read_size;
    }
    ```

2.  **Read Operation Handler (`smu.c`):** When a user-space program (like `cat` or the provided `monitor_cpu.c`) reads from `/sys/kernel/ryzen_smu_drv/pm_table`, the kernel invokes `pm_table_show`. This function, in turn, calls `smu_read_pm_table`.

3.  **Measurement Initiation (`smu.c`):** The `smu_read_pm_table` function contains the logic to trigger a new measurement. It checks if at least one millisecond has passed since the last measurement. If it has, it calls `smu_transfer_table_to_dram()`, which sends a command to the SMU to update the power metrics table in DRAM.

    ```c
    // in smu.c
    enum smu_return_val smu_read_pm_table(struct pci_dev *dev, unsigned char *dst,
                                          size_t *len) {
      // ... (other code) ...
    
      // Check if we should tell the SMU to refresh the table via jiffies.
      // Use a minimum interval of 1 ms.
      if (!g_smu.pm_jiffies ||
          time_after(jiffies, g_smu.pm_jiffies + msecs_to_jiffies(1))) {
        g_smu.pm_jiffies = jiffies;
    
        ret = smu_transfer_table_to_dram(dev); // This initiates the measurement
        if (ret != SMU_Return_OK)
          return ret;
    
        // ... (code for 2nd table) ...
      }
    
      // ... (code to copy data from DRAM to the user's buffer) ...
    
      return SMU_Return_OK;
    }
    ```

In summary, the `open()` system call on the file does not trigger a hardware update. The actual command to the SMU to perform a measurement and update the data table is sent during the `read()` system call, but it is throttled to occur at most once per millisecond to avoid excessive polling.

# References

1. Latest version of the kernel module https://github.com/amkillam/ryzen_smu
2. https://github.com/mann1x/ryzen_monitor_ng