[Click here](../README.md) to view the README.

## Design and implementation

The design of this application is minimalistic to get started with code examples on PSOC&trade; Edge MCU devices. All PSOC&trade; Edge E84 MCU applications have a dual-CPU three-project structure to develop code for the CM33 and CM55 cores. The CM33 core has two separate projects for the secure processing environment (SPE) and non-secure processing environment (NSPE). A project folder consists of various subfolders, each denoting a specific aspect of the project. The three project folders are as follows:

**Table 1. Application projects**

Project | Description
--------|------------------------
*proj_cm33_s* | Project for CM33 secure processing environment (SPE)
*proj_cm33_ns* | Project for CM33 non-secure processing environment (NSPE)
*proj_cm55* | CM55 project

<br>

In this code example, at device reset, the secure boot process starts from the ROM boot with the secure enclave (SE) as the root of trust (RoT). From the secure enclave, the boot flow is passed on to the system CPU subsystem where the secure CM33 application starts. After all necessary secure configurations, the flow is passed on to the non-secure CM33 application. Resource initialization for this example is performed by this CM33 non-secure project. It configures the system clocks, pins, clock to peripheral connections, and other platform resources. It then enables the CM55 core using the `Cy_SysEnableCM55()` function and the CM55 core is subsequently put to DeepSleep mode.

In the CM33 non-secure application, the clocks and system resources are initialized by the BSP initialization function. The retarget-io middleware is configured to use the debug UART. The debug UART prints a message (as shown in [Terminal output on program startup](../images/boot.png)) on the terminal emulator, the onboard KitProg3 acts the USB-UART bridge to create the virtual COM port. The DFU middleware is configured for SPI transport to receive and process the user-defined commands. The User LED1 blinks every 1 second. 


### DFU middleware and user-defined commands

The DFU middleware allows applications to register a single custom handler for handling all user-defined commands. The custom handler should have the intelligence to parse the command ID and handle the respective commands. DFU MW parses each command received and calls the custom handler if the command belongs to the range of user-defined commands (`0x50 - 0xFF`).

This code example defines the user-defined commands mentioned in **Table 2** and implements their corresponding actions. 

**Table 2. User-defined commands**

Command                | Inputs (with length in Bytes) | Action
-------                | ----------------------------- | ------------------
`Toggle GPIO` - `0xFC` | 1. GPIO port number (1B) <br> 2. GPIO pin number (1B) | Configures and toggles the specified GPIO
`Read Data` - `0xFF`   | 1. Address to read from (4B) <br> 2. Number of bytes to read (1B) | Reads the requested number of bytes from the specified address 


### DFU Host Tool and sending commands

The DFU Host Tool is a standalone program included with ModusToolbox&trade; used to communicate with the device programmed with an application having DFU capability. The tool requires an *.mtbdfu* file (JSON format) as input to communicate with the device, which should include a set of commands with the associated data to be sent to the device through the DFU Host tool using the DFU command response protocol.

This example utilizes this capability of the DHU Host Tool with the *.mtbdfu* file to send user-defined commands and capture the responses sent back by the device. An *.mtbdfu* file named *Commands.mtbdfu* is provided with the code example as a reference. See the file for more information. The default file sends both the user-defined commands.

- `Toggle GPIO` command to toggle GPIO Pin 7 of Port 16 (LED2)
- `Read Data` command to read 4 bytes from address `0x60340400`

> **Note:** See [AN235935](https://www.infineon.com/AN235935) and [DFU Host Tool UG](https://www.infineon.com/ModusToolboxDFUHostTool) for more information on the *.mtbdfu* file and the DFU commands.


### Serial interface configuration

See **Table 3** for the default configuration details. Ensure the configuration of the DFU Host Tool matches as defined below.

**Table 3. Default configurations**
    
DFU transport: I2C | Default  | Description
:----------------  | :------  | :-----
Address            | 53       | 7-bit slave device address
Data rate          | 400 kbps | DFU supports standard data rates from 50 Kbps to 1 Mbps

<br>

DFU transport: UART | Default   | Description
:-----------------  | :-------  | :-----
Baud rate (bps)      | 115200    | Supports standard baud rates from 19200 to 115200
Data width          | 8 bits    | Standard frame
Parity              | None      | Standard frame
Stop bits           | 1 bit     | Standard frame
Bit order           | LSB first | Standard frame

<br>

DFU transport: SPI | Default   | Description
:------------------| :------   | :-----
Shift direction    | MSB first | Default direction set as MSB first  
Clock speed        | 1 MHz     | DFU supports 1 MHz SPI clock speed
Mode               | Mode 00   | Default mode set as Mode 00

<br>
