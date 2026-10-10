# 星空 C1 独立硬件诊断

`board_main.c` 是独立诊断入口，不执行语音模型。当前语音交付版为 v9；只在排查硬件时替换为诊断固件。

Linux/WSL 下 `make diagnostic`；断电切 MCU、重新插拔 USB 上电，PowerShell 执行 `.\scripts\flash.ps1 -Mode diagnostic`。断电切 CHIP、重插 USB，确认 `C1 HARDWARE DIAGNOSTIC v3`。它上电保持停车，每次电机测试约三秒后停车。

接线为 **A=左轮、B=右轮**，见 [WIRING.md](../WIRING.md)。命令按通道解释：

- `l/L`：B（右轮）普通/全占空比前进；`h/H`：A（左轮）普通/全占空比前进。
- `r/R`：A（左轮）普通/全占空比后退。
- `t/T`：两通道普通/全占空比前进；`b/B`：两通道普通/全占空比后退。
- `s`：停车；`i`：身份；`m`：先停车、预热，再导出 24576 个 PCM16 样本。

小写指令保留原速度与配平，A 通道有 150ms 启动脉冲；语音固件默认持续全占空比。CR=0 驱动、CR=20000 停止。CNT 不支持读回，读到零不能判断计数器是否运行。

```powershell
python -m pip install -r diagnostics/requirements-host.txt
python scripts/board_diag.py --command H --label left-full --port COM5
python scripts/board_diag.py --command m --label microphone --port COM5
```

PCM 导出验证样本数与板端 FNV-1a，保存音频、日志、统计到被忽略的 tests/results/diagnostic。持续发声覆盖预热后的采样窗口；幅度变化证明声学响应，完整口令须听音复核。

实物已确认两轮前进、后退及麦克风完整人声。STBY 接 3.3V 恢复使能，重接 AIN1/2 恢复左轮方向；全占空比解决普通占空比启动不可靠的问题。诊断结束后重新写入 `c1_voice_car.bin` 才能运行语音控制。
