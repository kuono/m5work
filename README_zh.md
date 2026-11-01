# M5-MicroHs 远程振动监控系统 (v1.0.0)

[日本語](README.md) | [English](README_en.md) | [简体中文](README_zh.md)

这是一款高可靠性的远程状态监控系统，通过 ESP-NOW 将 M5StickC Plus2（振动检测传感器）与 M5Core2（集中监控主机）连接在一起。  
系统的核心判断逻辑采用针对嵌入式环境的 Haskell 编译器 **MicroHs**，将其转译为 C 语言，实现了纯函数式的状态机架构。

---

## 主要特性

- **函数式响应核心**: 传感器和接收端的状态转换逻辑均使用 Haskell 编写。2.0秒延迟保持和4级灵敏度预设等功能均作为纯函数实现。
- **低延迟高可靠通信**: 使用 ESP-NOW（支持 AES 加密）进行直接的端到端通信。无需 Wi-Fi 路由器。
- **动态 UI 布局**: UI 坐标和颜色可从接收端 LittleFS（支持 SD 卡回退）上的 `layout.txt` 动态加载。
- **可复现的构建流水线**: 通过 `Makefile` 自动化管理从 Haskell 转译、PlatformIO 固件构建、设备烧录到文档生成的全流程。

---

## 系统架构

```text
[ 振动检测子机 ]
  M5StickC Plus2 (IMU: MPU6886)
      │
      ▼ (代码转译)
  Haskell Core (Logic.hs -> Logic.c)
      │
      ▼ [ ESP-NOW (加密通信) ]
      │
[ 集中监控主机 ]
  M5Core2 (TFT LCD / Touch / LittleFS)
      │
      ▼ (代码转译)
  Haskell Monitor (MonitorLogic.hs -> MonitorLogic.c)
```

---

## 目录结构

```text
.
├── Makefile                # 自动化构建脚本
├── build_docs.sh           # 规格说明书与 API 文档生成脚本
├── common/                 # 公共头文件及运行时
│   ├── espnow/             # ESP-NOW 通信配置（密钥模板）
│   └── microhs_runtime/    # 经过 ESP32 适配修改的 MicroHs 运行时 (eval.c)
├── core2_receiver/         # 接收端 (M5Core2) PlatformIO 项目
├── m5stick_sensor/         # 传感器 (M5StickC Plus2) PlatformIO 项目
├── haskell/                # 核心逻辑的 Haskell 源码
│   ├── Logic.hs            # 传感器端逻辑
│   └── MonitorLogic.hs     # 接收端逻辑
└── docs/                   # 系统规格说明及生成的文档
```

---

## 前置要求

- **主机环境**: macOS / Linux (zsh 或 bash)
- **构建工具**:
  - [PlatformIO Core (CLI)](https://platformio.org/)
  - GNU Make
  - (可选) MicroHs (`mhs`) — 仅在修改 Haskell 逻辑后重新生成 C 代码时需要
  - (可选) Pandoc, Haddock — 用于生成文档

---

## 快速入门

### 1. 配置通信设置
出于安全考虑，ESP-NOW 的 PMK/LMK 及 MAC 地址配置已从代码库中排除。请从模板创建实际的配置文件：

```bash
cp common/espnow/espnow_config.h.example common/espnow/espnow_config.h
```
打开 `common/espnow/espnow_config.h`，根据需要调整加密密钥和目标 MAC 地址。

### 2. 构建固件
在项目根目录下运行 `make all`，即可自动构建两台设备的固件。

```bash
make all
```

### 3. 烧录到设备

使用 USB 数据线连接各设备，并分别执行烧录：

```bash
# 烧录子机 (M5StickC Plus2)
make upload-sensor

# 烧录主机 (M5Core2) 固件
make upload-receiver

# 烧录主机 UI 配置文件 (LittleFS)
make upload-fs
```

---

## 主要 Makefile 命令

| 命令 | 说明 |
|---|---|
| `make all` | 构建子机和主机固件 |
| `make haskell` | 将 `haskell/*.hs` 转译为 C 代码 |
| `make sensor` | 仅构建子机固件 |
| `make receiver` | 仅构建主机固件 |
| `make upload-sensor` | 将固件烧录至子机 |
| `make upload-receiver` | 将固件烧录至主机 |
| `make upload-fs` | 将 `data/layout.txt` 烧录至主机的 LittleFS |
| `make monitor-sensor` | 打开子机串口监视器 |
| `make monitor-receiver` | 打开主机串口监视器 |
| `make doc` | 重新生成文档 (HTML/PDF 及 API) |
| `make clean` | 清理 PlatformIO 缓存及转译的中间文件 |

---

## 许可证

本项目基于 MIT 许可证开源。详情请参阅 [LICENSE](LICENSE) 文件。