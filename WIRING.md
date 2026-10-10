# C1 语音小车接线

对应当前固件、TB6612FNG 模块和 INMP441 模块。接线时先断电；针号从排针的 1 脚标记开始按奇偶列数，不按板卡随意摆放的方向猜。

H55.4=GPIO5，H55.2=GND，H55.30=3.3V，H55.28/29=GND。GPIO 编号和物理针号不同。

## 供电

- 电池正极经原开关接已测试降压模块 IN+，电池负极接 IN-。
- 降压模块 OUT+ 接 TB6612 VM；OUT- 接公共 GND。
- C1 在调试阶段通过其 USB/Type-C 供电。降压模块的电机电源不接 H55 的 3.3V 排针。
- H55.30 的 3.3V 通过面包板或分线接 TB6612 VCC、STBY 和麦克风 VDD。
- C1 H55.28、TB6612 GND、麦克风 GND、降压模块 OUT- 共地。多个 GND 可通过面包板地线连接。
- 当前小车测试时接通 VM 电机供电，车轮架空。仅测试不驱动电机的 model 版本时可断开 VM。
- 面包板电源总线可能在中间断开；VCC、STBY、麦克风 VDD 必须接在确实连通的 3.3V 段，各模块 GND 接在确实共地的段。

## INMP441

```text
模块 VDD / VCC → H55.30  3.3V
模块 GND       → H55.28  GND
模块 L/R       → H55.29  GND，选择左声道
模块 SCK/BCLK  → H55.3   GPIO9
模块 WS/LRCLK  → H55.5   GPIO8
模块 SD/DOUT   → H55.4   GPIO5
```

若模块额外引出 EN/CHIPEN，接同一 3.3V；六针模块按上面六根线接。VDD 使用 3.3V。

## TB6612：左轮使用 A 通道

```text
AIN1 → H55.9   GPIO6
AIN2 → H55.7   GPIO7
PWMA → PWM 排针 U58.2，PWM1
AO1  → 左电机紫色延长线（原红线）
AO2  → 左电机黄色延长线（原黑线）
```

## TB6612：右轮使用 B 通道

```text
BIN1 → H55.6   GPIO0
BIN2 → H55.8   GPIO1
PWMB → PWM 排针 U58.5，PWM2
BO1  → 右电机红色延长线（原红线）
BO2  → 右电机黄色延长线（原黑线）
```

方向输入、PWM、输出必须属于同一通道。上面接线已实测两轮均能前进、后退，与当前默认固件 left_A,right_B 配置一致。STBY 必须接 3.3V；PWMA/PWMB 接上述板卡 PWM 引脚。

Type-C 朝上、元件面朝自己时，H55 是底部长双排针：靠板中心一排从左到右为偶数 2～30，靠板边一排为奇数 1～29。H55.4 是靠中心一排左数第 2 针；H55.3/.5 是靠边一排第 2/3 针。U58 是右下方双排针：靠中心一列从上到下为 1/3/5/7/9，靠边一列为 2/4/6/8/10，PWM1 为右列最上第 2 针，PWM2 为左列从上第 3 排第 5 针。

## ST7735（如使用）

```text
VCC、RES/RST、BLK → 3.3V
GND              → 公共 GND
SCL/SCK          → QSPI 排针 U59.10
SDA/MOSI         → QSPI 排针 U59.1
CS               → QSPI 排针 U59.5
DC               → H55.18，GPIO2
```

蓝牙模块不参与当前语音控制。先验证麦克风和模型，再接通 VM 运行小车版本。

## 依据

- [ECOS 官方 C1 接口原理图](https://embedded.openecos.com/zh-cn/latest/res/img/brd/starry-sky-c/v1.0/brd_sch_interface.webp)：H55、U58、U59 的针号。
- 当前 voice/motor.h、voice/motor.c 和 driver/voice_board.c：左右轮方向、PWM 通道和电平控制。
- [Toshiba TB6612FNG 数据手册](https://toshiba.semicon-storage.com/info/TB6612FNG_datasheet_en_20141001.pdf?did=10660&prodName=TB6612FNG)：A/B 输入、PWM 和输出对应关系。
- [TDK INMP441 数据手册](https://product.tdk.com/system/files/dam/doc/product/sw_piezo/mic/mems-mic/data_sheet/inmp441.pdf)：供电、左声道和 CHIPEN。
