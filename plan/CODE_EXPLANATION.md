# RC Car 代码说明文档

> 本文档讲解固件的每个模块，帮你理解代码逻辑，方便硬件连线和问题排查。

---

## 文件结构

```
~/rc_car/
├── main.c              # 主程序：电机控制、蓝牙指令解析、心跳超时
├── Makefile            # 编译配置
├── start.s             # RISC-V 启动代码（不动）
├── sections.lds        # 链接脚本（不动）
├── CODE_EXPLANATION.md # 本文档
├── driver/
│   ├── sys_uart.c/h    # SYS_UART 调试串口（已有）
│   ├── gpio.c/h        # GPIO 电机方向控制
│   ├── pwm.c/h         # PWM 电机调速
│   ├── uart.c/h        # HP_UART 蓝牙通信
│   └── delay.c/h       # 定时器延时
└── build/
    └── retrosoc_fw.bin # 编译产物，烧录用
```

**SDK 依赖**（编译时自动链接，不需要手动复制）：

| SDK 模块 | 路径 | 作用 |
|----------|------|------|
| libc/stdio.c | `components/libc/src/stdio.c` | 提供 printf/vprintf，映射到 SYS_UART |
| TimmoLog | `components/TimmoLog/src/log.c` | 结构化日志系统，带颜色和等级 |

---

## 一、TimmoLog 日志系统

### 职责
提供带颜色、等级、文件名+行号的结构化日志输出。所有日志通过 `printf` → `hal_sys_putchar()` 输出到 SYS_UART（调试串口）。

### 日志等级

| 等级 | 颜色 | 用途 |
|------|------|------|
| `LOG_DEBUG` | 蓝色 | 调试细节，心跳等高频信息 |
| `LOG_INFO` | 绿色 | 正常运行状态 |
| `LOG_WARN` | 黄色 | 异常但可恢复的情况 |
| `LOG_ERROR` | 红色 | 错误 |
| `LOG_FATAL` | 紫色 | 致命错误，会自动死循环 |

### 输出格式

```
[INFO](main.c:123):  Motor: dir=FWD speed=3
```

- `[INFO]` — 日志等级（带颜色）
- `(main.c:123)` — 源文件名和行号
- 冒号后是实际消息

### 串口看到的效果

Tabby 终端如果支持 ANSI 颜色，会看到：
- DEBUG 信息 → 蓝色
- INFO 信息 → 绿色
- WARN 信息 → 黄色
- ERROR 信息 → 红色

如果不支持颜色，会看到原始 ANSI 转义码（不影响阅读）。

---

## 二、GPIO 驱动（driver/gpio.c）

### 职责
控制 TB6612 的 4 个方向引脚（AIN1/AIN2/BIN1/BIN2），决定电机正转/反转/停止。

### 引脚映射

| 开发板引脚 | GPIO 编号 | 连接 TB6612 | 功能 |
|-----------|----------|-------------|------|
| GPIO_0 | bit 0 | AIN1 | 左电机方向 1 |
| GPIO_1 | bit 1 | AIN2 | 左电机方向 2 |
| GPIO_2 | bit 2 | BIN1 | 右电机方向 1 |
| GPIO_3 | bit 3 | BIN2 | 右电机方向 2 |

### 核心寄存器

```c
REG_GPIO_0_DR  = 0x03000000  // 数据寄存器：写1输出高，写0输出低
REG_GPIO_0_DDR = 0x03000004  // 方向寄存器：1=输出，0=输入
```

### TB6612 真值表

| AIN1 | AIN2 | 左电机 |
|------|------|--------|
| 1 | 0 | 正转 |
| 0 | 1 | 反转 |
| 0 | 0 | 停止（制动） |

右电机 BIN1/BIN2 同理。

### API

```c
gpio_init();               // 把 GPIO_0~3 配置为输出，全部拉低（停止）
gpio_motor_left(a1, a2);   // 设左电机方向：a1=1,a2=0 正转；a1=0,a2=1 反转
gpio_motor_right(b1, b2);  // 设右电机方向
gpio_motor_stop();          // 全部拉低，两电机停止
```

### 排查要点
- 如果电机不转：先用万用表量 GPIO_0~3 的电平，确认方向信号有输出
- 如果方向反了：交换对应电机的 AO1/AO2 线，或者在代码里把 a1/a2 对调

---

## 二、PWM 驱动（driver/pwm.c）

### 职责
输出 10kHz PWM 波形到 TB6612 的 PWMA/PWMB，控制电机转速。

### 引脚映射

| 开发板引脚 | PWM 通道 | 连接 TB6612 | 功能 |
|-----------|---------|-------------|------|
| PWM_CH0 | CR0 | PWMA | 左电机调速 |
| PWM_CH1 | CR1 | PWMB | 右电机调速 |

### 核心寄存器

```c
REG_PWM_0_CTRL = 0x03004000  // 控制寄存器：写3使能PWM
REG_PWM_0_PSCR = 0x03004004  // 预分频：写71，让定时器1MHz
REG_PWM_0_CMP  = 0x0300400c  // 周期：写999，1MHz/1000=10kHz
REG_PWM_0_CR0  = 0x03004010  // 通道0占空比：0~999
REG_PWM_0_CR1  = 0x03004014  // 通道1占空比：0~999
```

### 频率计算

```
系统时钟 = 72MHz
预分频 = PSCR + 1 = 72 → 定时器频率 = 72MHz / 72 = 1MHz
周期 = CMP + 1 = 1000 → PWM频率 = 1MHz / 1000 = 10kHz
```

### 速度档位

| 档位 | 占空比 | CR0/CR1 值 | 速度 |
|------|--------|-----------|------|
| 1 | 20% | 200 | 慢 |
| 2 | 40% | 400 | |
| 3 | 60% | 600 | 默认 |
| 4 | 80% | 800 | |
| 5 | 100% | 999 | 最快 |

### API

```c
pwm_init();                  // 初始化 PWM，10kHz，初始占空比=0
pwm_set_left(duty);          // 设左电机占空比（0~999）
pwm_set_right(duty);         // 设右电机占空比（0~999）
pwm_set_speed(level);        // 设速度档位（1~5），自动设左右电机
```

### 排查要点
- 如果电机嗡嗡响但不转：PWM 信号可能没到 TB6612，检查 PWMA/PWMB 接线
- 如果速度没变化：确认 PWMA/PWMB 接的是 PWM_CH0/PWM_CH1，不是其他引脚

---

## 三、UART 驱动（driver/uart.c）

### 职责
通过 HP_UART 与 HC-05 蓝牙模块通信。手机发的指令通过 HC-05 的 TX → 开发板 HP_UART_RX 收到。

### 引脚映射

| 开发板引脚 | 功能 | 连接 HC-05 |
|-----------|------|-----------|
| CUST_UART_TX | HP_UART 发送 | HC-05 RX |
| CUST_UART_RX | HP_UART 接收 | HC-05 TX |

> ⚠️ TX/RX 是**交叉连接**的！

### 核心寄存器

```c
REG_UART_1_LCR = 0x03003000  // 线路控制：写0x1F = 8N1
REG_UART_1_DIV = 0x03003004  // 波特率分频：72MHz/115200-1 = 624
REG_UART_1_TRX = 0x03003008  // 收发寄存器：读=收，写=发
REG_UART_1_FCR = 0x0300300c  // FIFO控制：先写0x0F清空，再写0x0C使能
REG_UART_1_LSR = 0x03003010  // 线路状态：bit7=RX有数据，bit8=TX忙
```

### 初始化序列

```c
REG_UART_1_LCR = 0x00;     // 先解锁
REG_UART_1_DIV = 624;      // 设波特率 115200
REG_UART_1_FCR = 0x0F;     // 复位FIFO
REG_UART_1_FCR = 0x0C;     // 使能FIFO
REG_UART_1_LCR = 0x1F;     // 8数据位，无校验，1停止位
```

### API

```c
hp_uart_init();           // 初始化 HP_UART 115200 8N1
hp_uart_send(c);          // 发一个字符（阻塞等待TX空闲）
hp_uart_putstr(str);      // 发字符串
hp_uart_recv();           // 非阻塞接收：有数据返回字符(0~255)，无数据返回-1
hp_uart_has_data();       // 检查是否有数据可读
```

### 排查要点
- 如果收不到蓝牙数据：先确认 HC-05 的波特率是 115200（出厂可能是 9600，需要用 AT 指令改）
- 如果乱码：TX/RX 可能接反了，或者波特率不匹配
- HC-05 红灯快闪 = 等待配对，红灯双闪 = 已连接

---

## 四、定时器延时（driver/delay.c）

### 职责
提供微秒/毫秒延时，以及一个毫秒级 tick 计数器（用于心跳超时判断）。

### 核心寄存器

```c
// Timer0：用作精确延时
REG_TIM_0_CONFIG = 0x0300005c  // 配置寄存器
REG_TIM_0_DATA   = 0x03000064  // 计数值寄存器（递减到0停止）

// Timer1：用作系统 tick（递减计数器）
REG_TIM_1_CONFIG = 0x03000068
REG_TIM_1_DATA   = 0x03000070
```

### Timer0 工作原理（延时用）

```
1. CONFIG = 0x0100  → 停止定时器，配置模式
2. DATA = CPU_FREQ × 微秒数 - 1  → 设定计数值
3. CONFIG = 0x0101  → 启动定时器（递减模式）
4. 等待 DATA == 0  → 延时完成
```

### Timer1 工作原理（tick 用）

```
1. 初始化时设为 0xFFFFFFFF（最大值），递减模式
2. get_tick_ms() 返回 0xFFFFFFFF - DATA，即递减的毫秒数
3. 每 ~49天 溢出一次，对于本项目足够
```

---

## 五、主程序（main.c）

### 整体流程

```
启动
  → 初始化 SYS_UART（调试串口）
  → 初始化 HP_UART（蓝牙串口）
  → 初始化定时器
  → 初始化 GPIO（4路输出）
  → 初始化 PWM（10kHz）
  → 打印启动信息
  → 进入主循环：
      → 尝试从蓝牙接收一个字节
      → 如果收到指令 → 解析并执行
      → 检查心跳超时（1秒无指令自动停车）
```

### 指令协议

| 指令 | 动作 | 串口日志 |
|------|------|---------|
| F | 前进 | `[F] DIR=FWD` |
| B | 后退 | `[B] DIR=BWD` |
| L | 左转 | `[L] DIR=LFT` |
| R | 右转 | `[R] DIR=RGT` |
| S | 停车 | `[S] DIR=STP` |
| 1~5 | 调速 | `[3] SPD=3` |
| H | 心跳 | （静默，重置超时计时器） |

### 心跳超时保护

- 每收到一条指令，记录当前 tick
- 主循环不断检查：如果超过 1000ms 没收到新指令
  → 自动停车（DIR_STOP + PWM=0）
  → 串口打印 `[TIMEOUT] Motor stopped - no heartbeat`
  → 手机 APP 需要持续发 `H`（心跳）保持连接

### 电机控制状态

| 方向 | 左电机 | 右电机 |
|------|--------|--------|
| 前进 | AIN1=1, AIN2=0 (正转) | BIN1=1, BIN2=0 (正转) |
| 后退 | AIN1=0, AIN2=1 (反转) | BIN1=0, BIN2=1 (反转) |
| 左转 | AIN1=0, AIN2=1 (反转) | BIN1=1, BIN2=0 (正转) |
| 右转 | AIN1=1, AIN2=0 (正转) | BIN1=0, BIN2=1 (反转) |
| 停车 | 全部 0 | 全部 0 |

---

## 六、上电后串口应该看到什么

### SYS_UART（Tabby，115200）

正常启动后应该看到（带颜色）：

```
[INFO](main.c:XX):  ========================================
[INFO](main.c:XX):    RC Car Firmware v1.0
[INFO](main.c:XX):  ========================================
[INFO](main.c:XX):  Board:    StarrySkyC1 (retroSoC)
[INFO](main.c:XX):  CPU:      72MHz RISC-V
[INFO](main.c:XX):  HP_UART:  115200 8N1 (HC-05 BT)
[INFO](main.c:XX):  Timer:    init OK
[INFO](main.c:XX):  GPIO:     4 pins output (AIN1/2, BIN1/2)
[INFO](main.c:XX):  PWM:      10kHz (CH0+CH1)
[INFO](main.c:XX):  Speed:    default level 3 (60%)
[INFO](main.c:XX):  Timeout:  1000ms heartbeat
[INFO](main.c:XX):  ----------------------------------------
[INFO](main.c:XX):  Ready. Waiting for BT commands...
```

收到手机指令后：

```
[INFO](main.c:XX):  CMD: Forward
[INFO](main.c:XX):  Motor: dir=FWD speed=3
[INFO](main.c:XX):  CMD: Speed -> level 5 (100%)
[INFO](main.c:XX):  CMD: Stop
[INFO](main.c:XX):  Motor: dir=STP speed=5
```

超时会打印：

```
[WARN](main.c:XX):  Heartbeat timeout! Motor stopped.
```

### HC-05 蓝牙状态灯

| 灯状态 | 含义 |
|--------|------|
| 快闪（约2次/秒） | 等待配对 |
| 双闪 | 已连接手机 |
| 常亮 | 正在通信 |

---

## 七、常见问题排查

### 问题1：烧录后串口没输出

| 检查项 | 方法 |
|--------|------|
| FLASH_SEL 开关 | 必须拨到 **CHIP** 才能运行 |
| 拨码开关 | 从左到右 **0100** |
| COM口选择 | Tabby 要选对 COM5 |
| 波特率 | 115200 8N1 |
| 按复位键 | 上电后要按一下复位 |

### 问题2：电机不转

| 检查项 | 方法 |
|--------|------|
| TB6612 STBY 脚 | 必须接 3.3V，不接不工作 |
| TB6612 VM 脚 | 必须接电池 7.4V |
| TB6612 VCC 脚 | 接 3.3V（逻辑电源） |
| LM2596 输出 | 用万用表确认 5.0V |
| PWM 信号 | 用示波器或万用表量 PWMA/PWMB 有无波形 |

### 问题3：蓝牙连不上

| 检查项 | 方法 |
|--------|------|
| HC-05 供电 | 必须接 5V，不是 3.3V |
| HC-05 波特率 | 出厂可能是 9600，需要用 AT 指令改成 115200 |
| TX/RX 交叉 | HC-05 TX → 开发板 RX，HC-05 RX → 开发板 TX |

### 问题4：方向反了

不需要改代码，直接交换对应电机的 AO1/AO2 线（或 BO1/BO2 线）即可。

---

## 八、HC-05 改波特率方法（如果需要）

1. 断开 HC-05 与其他设备的连接
2. 按住 HC-05 底板上的小按键（或接通 KEY 引脚到 VCC）
3. 给 HC-05 上电，灯慢闪（约1次/秒）= 进入 AT 命令模式
4. 用 USB-TTL 模块连接 HC-05（TX→RX，RX→TX，波特率 38400）
5. 发送 AT 指令：

```
AT              → 返回 OK
AT+UART=115200,0,0  → 返回 OK（改成115200 8N1）
AT+NAME=RC_CAR  → 返回 OK（可选，改蓝牙名称）
AT+PSWD=1234    → 返回 OK（可选，改配对密码）
```

6. 断电重启 HC-05，恢复正常模式

---

*文档版本: 1.0 | 2026-07-11*
