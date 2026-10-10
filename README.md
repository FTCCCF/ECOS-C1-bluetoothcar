# 星空 C1 语音控制小车

将 FTCCCF/ECOS-C1-bluetoothcar 的蓝牙入口替换为板上语音识别，支持“前进、停止、后退”。INMP441 采音、重采样、高通、MFCC12、INT8 时间卷积网络和 TB6612 电机控制全部在 C1 的 RV32IM 上运行，运行时无需电脑或蓝牙。上游基线为 `c12c7cab8c33b65c28a8ab5370155fa6c56c9911`。

当前交付 **v9** 使用 `mfcc-cmn-global-20261010-11`：两层时间卷积、逐系数均值减除（CMN）、全局时间池化、五类输出（forward / stop / backward / unknown / noise），3733 个参数。指令概率至少 0.75、第一与第二类概率差至少 0.15 时接受动作，门限没有调整。模型 SHA256：

```text
a04434631d6eadee122c5c6d0c0fed2eb7b32609ffa127e3e13fb8002f30063d
```

2026-10-10 用户在板卡自主测试并确认“前进、停车、后退、停车都正常”，随后接受本版可用。这四步发生在电脑串口观察开始之前，验收依据为用户对车轮动作的观察；后续日志没有覆盖这四步。板端身份、模型哈希、实际推理和下载状态另有记录，见 [validation/summary.json](validation/summary.json)。

## 接线与控制

完整接线见 [WIRING.md](WIRING.md)。验收配置为 A=左轮、B=右轮：

- AIN1/2 → H55.9/.7（GPIO6/7），PWMA → U58.2（PWM1）；左紫/黄 → AO1/AO2。
- BIN1/2 → H55.6/.8（GPIO0/1），PWMB → U58.5（PWM2）；右红/黄 → BO1/BO2。
- TB6612 VCC、STBY → 3.3V，VM → 已测试降压 OUT+；降压 OUT−、板卡、驱动、麦克风共地。
- INMP441 VDD → H55.30（3.3V），GND → H55.28，L/R → H55.29（GND）；SCK → H55.3（GPIO9），WS → H55.5（GPIO8），SD → H55.4（GPIO5）。

当前使用全占空比：此板 PWM 为反相比较，CR=0 驱动、CR=20000 停止。普通占空比曾无法可靠启动。原速度 4000、换算上限 6000、前进配平 952/1000 保留为可选基线。换方向前撤掉占空比；拒识保持上一次动作，“停止”清除方向和占空比。生产固件没有行驶超时。

## 编译与烧录

需要 `riscv64-unknown-elf-gcc`（验证版本 13.2.0），无需 ECOS SDK、浮点 libc 或 PSRAM。全部 DSP/网络计算使用整数，链接脚本保留至少 8KiB SRAM 栈。默认启用 ST7735；显示通道超时后语音流程继续。在 Linux/WSL 工程目录运行：

```bash
make model car
```

- `build/voice/c1_voice_model.bin`：相同 v9 模型，PWM 禁用、方向脚低电平。
- `build/voice/c1_voice_car.bin`：启用电机，使用验收接线与全占空比。

小车镜像 48848 字节，SHA256 为 `4e8dd37982d90bfec8036863bd7e50b8815a43ce1c6b33113211e2ced9cc7bed`；默认构建与实板验收镜像逐字节一致。更改选项时用 `make -B` 或独立 `BUILD_DIR`。

先断电切 MCU，重新插拔 USB 并上电，在 Windows PowerShell 工程目录执行：

```powershell
.\scripts\flash.ps1 -Mode car
```

脚本识别 HFP-LINK 下载盘、核对文件哈希，并非缓存读取确认 `write successful !!!`。下载器保留上次成功状态时需在 MCU 模式重插 USB。断电切 CHIP，再重插 USB 并上电即可运行；首次测试车轮架空。上电自动循环识别。

SYS_UART 为 115200，端口号以本机为准：

```powershell
.\scripts\read-board.ps1 -PortName COM5 -Seconds 30
```

串口 `i` 查询版本，`s` 停车并暂停，`a` 单次采音，`r` 恢复循环，`d` 导出刚才的输入，`q` 关闭显示。命令在当前采音/推理块结束后处理。安装 `diagnostics/requirements-host.txt` 后可用 `python scripts/voice_board_test.py --command prepare --port COM5` 检查 v9 身份，`--command resume` 恢复；`observe_voice.py` 只读串口。

## 节拍和验证范围

采音先预热 65536 帧、学习安静基线，连续六个 128 帧块超过门限才确认语音起始；保留 2048 帧前缀，补齐 24576 帧，按实际捕获周期重采样，识别第一段完整一秒。板端推理记录为 402812283 周期，按标称 72MHz 约 **5.6 秒**。推理期间麦克风时钟暂停，存在监听空档；本版不提供连续采音或低延迟停车。

- 当前 v9 完整 C 路径 63/63 判定与冻结参考一致，功能标签 63/63：30 个验证、28 个新增训练和 5 个重复现场诊断窗口，不是新的独立测试成绩。
- 同一组 C 量化输入的网络最大概率误差约 0.00000496；跨浮点/定点前端最大概率差约 0.00636，超过早期 0.005 检查界限。分层验证保留该差异，单独检查整数网络，未修改动作门限。
- 五段完整负样本的 100 个相关重叠窗口（原声音约 14.39 秒）无动作输出；跨前端最大概率差约 0.0230。
- 原始模型 41 个窗口的 C 输出逐字节不变；PicoRV32 RTL 执行当前 v9 权重，前进窗口推理 388533650 周期。采音仿真覆盖等待 0/2 周期、PCM 符号、时钟数、电机/LCD GPIO 保持和采音中停止。
- 下载成功，串口确认 v9/当前模型及推理结果；实物四步动作由用户自主测试确认。

训练按原始录音分组：178 个训练、30 个验证、20 个原测试窗口；本候选未评估那 20 个原测试窗口。新增手机录音接受前进 9、停止 14、后退 11 段，截断样本排除。私人音频、训练检查点、逐录音报告不发布；构建只需随附 C 权重与 DSP 常数。验证工具说明见 [tests/README.md](tests/README.md)。

原始八分段池化模型保留在 `voice/generated`，可单独构建基线：

```bash
make model BUILD_DIR=build/baseline VOICE_MODEL_DIR=voice/generated VOICE_FIRMWARE_VERSION=v6
```

## 打包

构建后在 Windows Python 运行：

```powershell
python scripts/summarize_validation.py
python scripts/package_voice.py --output ..\output\c1-voicecar-v9-20261010.zip
```

文件清单明确列出当前源码、模型和两个生产镜像；打包核对模型/验证源码/固件哈希与 ZIP CRC，排除旧固件、仿真镜像、私人音频和中间实验。旧蓝牙资料在 `plan/`，当前入口与接线以本文件和 WIRING.md 为准。
