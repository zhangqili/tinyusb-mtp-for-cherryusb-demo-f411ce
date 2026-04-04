
# STM32F411CE 基于 CherryUSB 的 MTP 协议栈移植与实现

本文档详细阐述了一个基于 STM32F411CE 微控制器的 USB 媒体传输协议（MTP, Media Transfer Protocol）演示项目的工程架构与操作指南。本项目的核心工作在于将 TinyUSB 协议栈中成熟的 MTP 类驱动模块剥离，并成功移植与适配至 CherryUSB 设备协议栈，从而在不同的协议框架间实现了核心组件的有效复用。

## 系统特性

* **底层协议框架**：系统底层依托轻量级且具备高性能特性的 [CherryUSB](https://github.com/cherry-usb/CherryUSB) 设备栈构建。
* **MTP 驱动实现**：应用层 MTP 逻辑提取自开源的 [TinyUSB](https://github.com/hathach/tinyusb) 协议栈（位于 `mtp/` 目录），并针对目标框架完成了深度适配与封装。
* **硬件承载平台**：系统运行于以 STM32F411CEU6 为核心的评估板（通常称为 "Black Pill"）。
* **项目构建机制**：采用标准的 CMake 构建系统，有效解耦了工程对特定集成开发环境（IDE）的依赖，显著提升了项目的跨平台可移植性。
* **硬件抽象与驱动**：底层外设驱动代码由 STM32CubeMX 配置工具生成，并基于 ST 官方硬件抽象层（HAL）库开发。

## 项目文件组织结构

```text
├── CMakeLists.txt        # CMake 主构建脚本
├── CMakePresets.json     # CMake 预设配置文件
├── Core/                 # STM32CubeMX 生成的核心底层代码 (含 main.c 及中断处理逻辑)
├── Drivers/              # STM32 HAL 库及 CMSIS 核心头文件
├── mtp/                  # 核心移植层：TinyUSB MTP 类驱动及底层适配接口
│   ├── common/           # TinyUSB 环境依赖的通用头文件与宏定义
│   ├── mtp_device.c      # MTP 设备层逻辑实现
│   ├── mtp_fs_port.c     # MTP 文件系统硬件对接层 (用于对接实际的 Flash/SD 等存储介质)
│   ├── mtp_fs_conv.c     # MTP 协议字符编码转换模块 (负责 UTF-16 与底层文件系统 UTF-8/ASCII 间的字符串及路径转换)
│   └── usbd_mtp.c        # MTP 协议核心类驱动层 (Class Driver)，负责管理底层的端点数据流、命令状态机 (Command/Data/Response) 以及基础协议指令的解析与路由
├── usb/                  # CherryUSB 协议栈应用层配置与初始化
│   └── usbd_user.c       # CherryUSB 设备栈配置层 (涵盖 USB 描述符定义、端点分配及 MTP/HID 类接口的挂载与初始化)
├── .gitmodules           # Git 子模块配置文件 (用于管理 CherryUSB 等依赖库)
└── tinyusb-mtp-for-cherryusb-demo-f411ce.ioc # STM32CubeMX 工程配置文件
````

## 系统运行环境要求

**硬件要求：**

  * **微控制器平台**：STM32F411CEU6 核心板。
  * **调试与烧录接口**：ST-Link V2、J-Link 或其他兼容的 SWD (Serial Wire Debug) 标准调试器。
  * **数据链路**：用于提供系统供电及 USB 数据传输的标准 Type-C 线缆。
  * **存储扩展（可选）**：用于验证非易失性 MTP 文件存储逻辑的 SPI Flash 或 SD 卡模块。

## 软件依赖与编译部署规范

本项目基于 CMake 体系进行源码构建。建议开发者在 Linux、macOS 或配置有 WSL/MinGW 环境的 Windows 操作系统下执行以下编译流程。

### 1\. 构建环境配置

在进行编译前，请确认宿主机已正确安装以下构建工具链：

  * **CMake** (版本 \>= 3.16)
  * **GNU Arm Embedded Toolchain** (`arm-none-eabi-gcc`)
  * **Make** 或 **Ninja** 构建系统
  * **Git** 版本控制工具

### 2\. 源码获取与子模块初始化

由于本项目通过 Git Submodules 引入了第三方协议栈（如 CherryUSB），在克隆远程仓库时需同步拉取相关的子模块代码：

```bash
git clone --recursive [https://github.com/yourusername/tinyusb-mtp-for-cherryusb-demo-f411ce.git](https://github.com/yourusername/tinyusb-mtp-for-cherryusb-demo-f411ce.git)
cd tinyusb-mtp-for-cherryusb-demo-f411ce

# 附注：若初始克隆时未添加 --recursive 参数，可执行以下命令完成子模块初始化：
# git submodule update --init --recursive
```

### 3\. 固件编译流程

请在项目根目录下建立独立的构建目录（Out-of-source build），并执行编译指令：

```bash
mkdir build
cd build

# 指定交叉编译工具链并生成 Makefile
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/gcc-arm-none-eabi.cmake ..

# 执行多线程编译，输出 .elf, .bin 及 .hex 固件文件
make -j4
```

*说明：若您的开发环境首选 `Ninja` 作为构建后端，可在配置阶段向 CMake 传递 `-G Ninja` 参数。*

## 固件烧录与系统验证
1. **固件部署（请选择以下任一方式执行）**：
方式一：基于 SWD 接口部署

将 ST-Link 或 J-Link 调试器正确连接至目标板的 SWD 接口（对应引脚：3V3, GND, SWDIO, SWCLK）。

运用相应的烧录软件（如 OpenOCD、STM32CubeProgrammer 或 J-Flash），将构建生成的 build/tinyusb-mtp-for-cherryusb-demo-f411ce.bin 或 .hex 文件写入微控制器的内部闪存。

方式二：基于 USB DFU 模式部署

触发 DFU 引导机制：在设备建立 USB 物理连接的前提下，持续按下目标板的 BOOT0 按键，随后短按 NRST（复位）按键并释放，最后释放 BOOT0 按键（或者在按下 BOOT0 按键的同时将设备接入计算机）。此时，主机操作系统应将设备识别为 "STM32 BOOTLOADER"。

执行固件写入：启动 STM32CubeProgrammer 软件（通信端口选择为 USB），或调用跨平台开源工具 dfu-util，将编译所得的固件文件写入目标器件。

2.  **文件系统初始化（关键步骤）**：在设备首次会自动格式化，上电或执行硬件复位操作时，**请持续按住目标板载的 KEY 按键**。此操作将触发固件底层的格式化例程，以初始化系统存储介质。

3.  **建立 USB 链路**：通过 Type-C 数据线将开发板接入主机计算机。
  
4.  **MTP 挂载测试**：在计算机的操作系统设备管理器或文件资源管理器中，验证是否出现标识为 **"MTP Device"** 的媒体传输设备。验证通过后，即可进行常规的文件读写测试（具体的文件系统 I/O 行为由 `mtp_fs_port.c` 中的对接实现决定）。

5.  **HID 辅助功能验证**：在设备维持正常运行状态且未进入格式化流程的前提下，短按目标板的 **KEY 按键**。系统将通过 USB 接口模拟标准人机交互设备（HID），向主机发送字符数据 **'A'**。

## MTP 协议移植与开发技术规范

在传统的 USB 存储应用场景中，大容量存储类（MSC）在主机访问期间会呈现块设备独占特性。该机制客观上限制了微控制器（MCU）与上位机同步且安全地访问底层文件系统（例如 FatFS）的能力。作为应对技术，媒体传输协议（MTP）基于文件级传输逻辑，从根本上支持了主机与设备端对共享文件系统的安全并发访问。

为确保在 CherryUSB 框架下高效构建 MTP 功能，本项目采取了架构复用的策略：将 TinyUSB 中经过充分验证的 MTP 源码（以 `mtp/usbd_mtp.c` 为核心的协议状态机及指令路由模块）作为独立的应用程序接口层（API Layer）引入。通过构建底层通信适配器（Wrapper），将该类驱动成功对接至 CherryUSB 暴露的端点（Endpoint）底层数据收发接口，从而实现了独立于硬件的协议处理。

**定制化开发建议**：若开发人员需要更改系统的底层文件存储后端（例如从当前的基于 RAM 的存储池迁移至外置 SPI Flash 或基于 SDIO 总线的 FATFS 架构），需重点分析并重构 `mtp/mtp_fs_port.c` 源文件中的文件系统抽象层（VFS）接口。处理包含多语言字符的文件名时，相关编解码转换逻辑则需参阅 `mtp/mtp_fs_conv.c`。对 MTP 核心指令集的增删与修改，应主要在 `mtp/usbd_mtp.c` 及其关联头文件中进行。

## 鸣谢

  * [CherryUSB](https://www.google.com/url?sa=E&source=gmail&q=https://github.com/cherry-usb/CherryUSB) - 提供轻量级且结构清晰的 USB 设备及主机协议栈实现。
  * [TinyUSB](https://github.com/hathach/tinyusb) - 提供并开源了高可靠性的 MTP 类设备驱动基础源码。
  * [STMicroelectronics](https://www.st.com/) - 提供了 STM32 硬件底层驱动库（HAL）。

## 许可协议

本工程库中整合的各项源代码均遵循其原生作者或机构声明的开源协议（详情请查阅各源文件头部的 License 声明段落）。

  * 硬件抽象层（HAL）代码受 STMicroelectronics SLA0044 等相关协议约束。
  * 移植的 MTP 协议相关代码遵循 TinyUSB 项目的 MIT 开源许可协议。
