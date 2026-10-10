# 验证工具

固件构建只需交叉编译器和随附 C 权重，无需私人音频或 Python 训练工程。

Linux/WSL 下 `bash tests/run_control.sh` 使用主机 GCC 检查当前 v9 的整数前端/网络及四种 PWM/左右映射配置，覆盖停车、拒识保持、禁用驱动、方向、静音、无效窗口及停止优先级。

`verify.py` 和 `scripts/export_voice.py` 重现原始八分段池化模型；`verify_candidate.py`、`verify_candidate_negative.py` 检查显式传入的候选模型。它们需要上一级私人工作区的 training/kws、Python/numpy/scipy、冻结检查点和复核音频，数据不随公开仓库发布。

`verify.py` 生成 tests/generated/selftest_pcm.h 等音频夹具。随后 `bash tests/run_rv32.sh` 使用当前 v9 模型执行 PicoRV32 RTL selftest 与采音仿真，需 Verilator。`run_candidate_rv32.py` 可执行单独构建的 selftest 并保存哈希；`bash tests/run_triggered.sh` 验证触发、前缀与采音中停止。原始基线测试须显式选择 voice/generated 和独立输出，重复回放不是新独立测试。

selftest.bin、capturetest.bin、triggeredtest.bin 是仿真镜像，禁止烧录。CPU RTL 保留原许可，见 vendor/README.md。公开摘要 validation/release/evidence.json 保存原报告哈希、数值检查、仿真和用户实物验收范围。原始报告、私人 PCM 与历史构建留在本地忽略目录。
