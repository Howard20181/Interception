blockkey —— 屏蔽某一个按键
=========================

用途
----

在 Interception 驱动层拦下键盘输入，只丢弃指定的那一个按键，其余按键原样透传，
不改变任何其它行为。默认目标就是笔记本键盘坏掉的**右 Alt**（AltGr）：

- 右 Alt 上报为扫描码 `0x38` **带 E0 前缀**，左 Alt 是同一个 `0x38` 但**不带 E0**，
  所以只屏蔽坏掉的右 Alt，正常工作的左 Alt 不受影响。
- 按键在程序启动前就已经卡住时（Windows 已经认为它一直按着），启动时会给每个键盘
  设备补发一次该键的抬起，把这个卡住的状态释放掉；如果它本来没被按下，这次多余的
  抬起会被系统忽略。不想要这个行为就加 `--no-release`。
- 退出：**左 Ctrl + 左 Shift + Q**（被吞掉的键不会传给系统，退出组合键也一并吞掉）。

先决条件
--------

1. 已安装 Interception 驱动（release 包里的 `install-interception.exe`，用管理员
   身份运行，装完重启）。
2. 本程序**通常需要管理员身份**运行，否则可能打不开驱动，会提示
   `cannot reach the Interception driver`。实测本机在非管理员下也能打开驱动设备，
   所以先直接运行即可，真打不开时再用管理员运行。
3. 这是进程级的拦截：程序不在运行时不会屏蔽任何按键，坏键会恢复原样。

编译
----

### 方式 A1：Visual Studio，库编进 exe（推荐，自包含 64 位）

```
samples\blockkey\build-msvc.cmd
```

脚本自己找 Visual Studio（vswhere + VsDevCmd），把 `library\interception.c` 一起编进
可执行文件，所以运行时**不需要** `interception.dll`，只依赖已安装的驱动（实测依赖只有
`ADVAPI32.dll`、`KERNEL32.dll`）。在 VS 开发者命令行里也可以手动编译：

```
cl /O2 /EHsc /DINTERCEPTION_STATIC /I ..\..\library /I .. ^
   blockkey.cpp ..\utils.c ..\..\library\interception.c /Fe:blockkey.exe ^
   /link user32.lib advapi32.lib
```

### 方式 A2：Visual Studio，链接 release 的 interception.lib（动态）

把 release 里的库解压到 `library\x64`、`library\x86`（本仓库的 `.gitignore` 里
`x64/`、`x86/` 已被忽略，不会误提交），然后：

```
samples\blockkey\build-msvc-dll.cmd          编译 x64，用 library\x64 → blockkey-dll.exe
samples\blockkey\build-msvc-dll.cmd x86      编译 x86，用 library\x86 → blockkey-dll-x86.exe
```

产物是 `blockkey-dll.exe`，脚本会把对应的 `interception.dll` 复制到旁边（运行时必须有
它）。两种用法都可以；动态链接这种形式正是 LGPL 期望的"可以替换/重新链接库"的形态，
所以要再分发给别人时它最省事，而 A1 的产物自包含、只拷一个文件。

也可以直接用现代编译器从库源码编出 dll，不需要 WDK 7.1：

```
cl /O2 /LD /DINTERCEPTION_EXPORT /I ..\..\library ..\..\library\interception.c ^
   /Fe:interception.dll /link kernel32.lib advapi32.lib
```

### 方式 B：WDK（仓库原有约定，需要 **WDK 7.1**）

`buildit.cmd` 走的是 WDK 7.1 的 `build.exe` + `sources` 机制：先在 `library` 目录用
`buildit.cmd`（或 64 位 `buildit-x64.cmd`）编出 `interception.lib`，再在本目录执行
`buildit.cmd`。它要求环境变量 `%WDK%` 指向 WDK 7.1（仓库 README 也是这个前提），并且
`%WDK%\bin\setenv.exe` 存在。

注意：**装了 WDK 10（Windows Driver Kit for Windows 10）也用不了这条**——WDK 10 已经
移除了 `build.exe`，驱动/库改用 MSBuild，`sources` 文件没有东西能读它。实测本机装了
WDK 10.1.28000，但 `%WDK%` 未设置、`Windows Kits\10` 下没有 `build.exe`，所以走不了
方式 B；用方式 A1/A2 即可。

### 方式 C：其它编译器

只需要 `interception.h` 加 `interception.lib`（或把 `library\interception.c` 一起编译，
并定义 `INTERCEPTION_STATIC`），任何 C++ 编译器都可以。

用法
----

```
blockkey                       屏蔽所有键盘上的右 Alt（默认）
blockkey --hardware-id Laptop  只屏蔽硬件 ID 里含 Laptop 的那个键盘
blockkey --probe               打印每个按键，什么都不拦（用来找坏键上报的扫描码）
blockkey --list                列出键盘设备及其硬件 ID
blockkey --help                完整选项
```

| 选项 | 说明 |
| --- | --- |
| `--key <扫描码>` | 要屏蔽的扫描码，如 `0x38`（默认 `0x38`） |
| `--e0` | 只匹配带 E0 前缀的事件（默认，即右 Alt） |
| `--no-e0` | 只匹配不带 E0 的事件，用来屏蔽同一个扫描码的左侧按键 |
| `--any-e0` | 两种都匹配 |
| `--device <编号>` | 只对第 n 个键盘设备生效（编号见 `--list`，1 到 10） |
| `--hardware-id <文本>` | 只对硬件 ID 含该文本的键盘生效（不区分大小写，别名 `--hwid`） |
| `--release` | 启动时补发一次抬起，释放已卡住的状态（默认） |
| `--no-release` | 只吞事件，绝不发送任何东西 |
| `--probe` | 打印每个按键（键盘编号、扫描码、state、是否 E0、硬件 ID），不拦截 |
| `--list` | 列出键盘设备与硬件 ID 后退出 |
| `--service` | 以 Windows 服务方式运行，由服务控制管理器启动，不要手动敲 |
| `--verbose` | 打印每个被吞掉的按键 |
| `--quiet` | 除错误外不打印 |
| `-h`, `--help` | 帮助 |

典型用法
--------

**1. 先确认坏键上报的是什么**

```
blockkey --probe
```

按一下坏键，记下打印出来的 `code` 和有没有 `E0`，然后 Ctrl + Shift + Q 退出。
注意 `--probe` 会把按下的内容打印到控制台，别在输入密码时开着。

**2. 只屏蔽笔记本自带的键盘（外接键盘的右 Alt 仍然可用）**

```
blockkey --list
```

输出形如 `2: ACPI\PNP0303\4&1A2B3C&0&0`，取其中能唯一标识内置键盘的一段：

```
blockkey --hardware-id PNP0303
```

硬件 ID 里几乎都带 `&`，在 cmd 里必须加引号，否则会被当成命令分隔符：

```
blockkey --hardware-id "VID_2717&PID_5011&REV_0100&MI_00"
```

硬件 ID 比设备编号稳定（编号会随插拔、重启变化），长期使用建议用 `--hardware-id`。

**3. 坏键不是右 Alt**

用 `--probe` 查到扫描码后按扫描码指定，例如屏蔽 Insert（`0x52` + E0）：

```
blockkey --key 0x52 --e0
```

常见扫描码：右 Alt `0x38`+E0、左 Alt `0x38`、Insert `0x52`+E0、左 Win `0x5B`+E0、
右 Win `0x5C`+E0、Menu `0x5D`+E0、Caps Lock `0x3A`、Num Lock `0x45`。少数笔记本上报
坏键时不带 E0，那就加 `--no-e0`。

**4. 开机自启：让它从开机起就一直屏蔽**

坏键是开机就存在的，所以最终要让它开机就生效。三种做法：

| 做法 | 何时生效 | 需要管理员 | 运行日志 |
| --- | --- | --- | --- |
| 装成 Windows 服务（推荐） | 开机时，登录前 | 仅安装/卸载时 | `%ProgramData%\blockkey\blockkey.log` |
| 计划任务（开机触发） | 开机时，登录前 | 仅安装/卸载时 | 无 |
| 启动文件夹 / 登录触发 | 登录之后 | 不需要 | 无（会留一个黑窗口） |

安装成服务（管理员命令提示符，参数会原样存进服务）：

```
samples\blockkey\install-service.cmd
samples\blockkey\install-service.cmd --hardware-id "VID_2717&PID_5011&REV_0100&MI_00"
```

脚本会执行 `sc create`（`start= auto`、以 LocalSystem 运行、命令行里带 `--service`
和你给的参数）、写描述、设置崩溃后自动重启，然后立即启动服务。之后：

```
sc query blockkey                                  查看状态
sc stop blockkey                                   停止，右 Alt 立刻恢复
sc start blockkey                                  再次启动
samples\blockkey\uninstall-service.cmd             停止并删除
```

- 服务没有控制台，所以运行情况写进日志文件：启动参数、补发抬起的键盘数、停止时吞掉的
  按键总数。服务模式下 `--verbose` 不逐条记录，免得一个卡住的键把日志写爆。
- 退出组合键 Ctrl + Shift + Q 只对控制台运行有效；服务用 `sc stop blockkey` 停。
- 服务与手动运行互斥：服务在跑时再手动运行会提示 `another instance is already running`，
  这是有意的（两个实例会分散键盘事件）。

不想装服务的话，用计划任务达到同样效果（管理员命令提示符）：

```
samples\blockkey\install-task.cmd
samples\blockkey\install-task.cmd --hardware-id "VID_2717&PID_5011&REV_0100&MI_00"
```

它等价于 `schtasks /create /tn blockkey /sc onstart /ru SYSTEM /rl HIGHEST /f
/tr "\"<完整路径>\blockkey.exe\" --quiet"`，卸载用 `uninstall-task.cmd`。缺点是运行
日志只能自己加输出重定向。

自测（不需要驱动、不需要管理员）
--------------------------------

```
samples\blockkey\tests\run-tests.cmd
```

测试用桩替换 Interception API，把脚本化的按键序列喂给程序本体，检查哪些被吞掉、哪些
被透传、参数解析和退出组合键是否正确，共 60 项断言，MSVC、clang++、g++ 都能跑。

常见问题
--------

- `cannot reach the Interception driver`：驱动没装，或没用管理员身份运行。
- `another instance is already running`：已经有一个 blockkey 在运行（单实例互斥）。
- `--service must be started by the service control manager`：`--service` 只给服务用，
  手动运行不要加它（这条错误也会记进服务日志文件）。
- 服务起不来：先看 `%ProgramData%\blockkey\blockkey.log`；再确认驱动已装、并且
  `blockkey.exe --list` 能正常列出键盘。
- 屏蔽之后 Alt 似乎还是“按着”的：说明按键在启动前已被系统认定为按下，而补发的抬起没
  生效。用 `--verbose` 确认事件确实被吞掉了，必要时重启后再启动本程序。
- 想确认它真的在工作：加 `--verbose`，按一下坏键会打印 `swallowed ...`。
- 本程序只设置键盘过滤器，鼠标完全不受影响。
