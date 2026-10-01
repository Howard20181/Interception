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

1. 已安装 Interception 驱动。把 release 里的 `install-interception.exe`（在
   "command line installer" 目录里）放到本目录旁边，然后运行：

   ```
   samples\blockkey\install-driver.cmd
   ```

   **直接双击就行**：脚本发现没有管理员权限时会自己弹 UAC 请求提权，提权后的副本在独立
   窗口里继续，而且窗口会保留（`cmd /k`）让你看清结果；拒绝 UAC 则什么都不做。脚本调用
   官方安装器的 `/install`，再核对 `keyboard.sys`、`mouse.sys` 和键盘/鼠标类的过滤器
   注册是否就位，最后提示重启——**重启后驱动才真正开始过滤**。也可以在命令行上给安装器
   路径：`install-driver.cmd "D:\path\install-interception.exe"`（这种带参数的情况需要
   管理员命令行，见下面的"关于提权"）。卸载用 `uninstall-driver.cmd`，同样走官方
   `/uninstall`，同样要重启。
2. 本程序**通常需要管理员身份**运行：它一旦拿到管理员权限（或作为服务以 SYSTEM 启动）
   就会把驱动设备收紧成"仅管理员可打开"（见下面的"驱动权限"一节），之后非管理员运行会
   提示 `cannot reach the Interception driver` 并在提示里说明设备已被收紧。实测本机驱动
   刚装好时非管理员也能打开，所以第一次直接运行即可；之后的运行用管理员，或先用
   `--no-lockdown` / `--unlock` 保持开放。
3. 这是进程级的拦截：程序不在运行时不会屏蔽任何按键，坏键会恢复原样。

编译
----

### 方式 A1：Visual Studio，库编进 exe（推荐，自包含 64 位）

```
samples\blockkey\build-msvc.cmd
```

脚本自己找 Visual Studio（vswhere + VsDevCmd），把 `library\interception.c` 一起编进
可执行文件，所以运行时**不需要** `interception.dll`，只依赖已安装的驱动（实测依赖只有
`ADVAPI32.dll`、`KERNEL32.dll`）。构建过程本身不写任何日志。

事件日志的消息表（`blockkey.mc`）**是可选的**：脚本在 `PATH` 上找得到 SDK 的
`mc.exe`/`rc.exe` 时就顺手编进 exe（这样事件查看器能显示完整描述），找不到就跳过并给出
提示，exe 照常能用。手动编译时对应下面两步：

```
mc -h . -r . blockkey.mc          :: 可选，需要 Windows SDK
rc /nologo /fo blockkey.res blockkey.rc
cl /O2 /EHsc /DINTERCEPTION_STATIC /I ..\..\library /I .. ^
   blockkey.cpp ..\utils.c ..\..\library\interception.c blockkey.res /Fe:blockkey.exe ^
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
| `--release-only` | 只补发一次抬起就退出（急救用：坏键把 Alt/Ctrl 卡住时） |
| `--probe` | 打印每个按键（键盘编号、扫描码、state、是否 E0、硬件 ID），不拦截 |
| `--list` | 列出键盘设备与硬件 ID 后退出 |
| `--service` | 以 Windows 服务方式运行，由服务控制管理器启动，不要手动敲 |
| `--verbose` | 打印每个被吞掉的按键 |
| `--quiet` | 除错误外不打印 |
| `--no-lockdown` | 不动驱动的设备访问权限（见下节） |
| `--unlock` | 把驱动设备重新对所有用户开放，然后退出（维护用） |
| `-h`, `--help` | 帮助 |

驱动权限：有权限时自动收紧
--------------------------

Interception 驱动创建的 20 个控制设备（`\\.\interception00`…`19`）默认对 **Everyone**
开放读写，所以任何**非管理员**进程——包括跑在低完整性下的沙箱进程——都能读取全部键盘
输入、也能注入按键。设备对象的 DACL 里 Everyone 的掩码是 `0x001201BF`（泛读/泛写/执行，
但不含 `WRITE_DAC`），所以只有 SYSTEM 和管理员能改它。

于是本程序**只要已经拿到足够的权限，就顺手把它收紧**：

- **以管理员身份运行**，或**作为服务启动**（服务控制管理器以 SYSTEM 启动它）时，程序会
  静默地把这 20 个设备的 DACL 改成只允许 **SYSTEM 和 Administrators**：
  `D:P(A;;GA;;;SY)(A;;GA;;;BA)`；
- **不需要重启、不需要改驱动**：设备对象原地收紧；
- **自己不受影响**：改动前程序已经打开了这 20 个句柄，已打开的句柄不会因 DACL 变化失效
  （这一点用等价实验验证过：同一个 SDDL、同一个 `SE_KERNEL_OBJECT` 对象类型，设置成功 →
  新的打开被拒 `error 5` → 旧句柄照旧可用）；
- **持续到驱动重新加载**（重启）或有人执行 `--unlock` 为止。装成服务后，服务每次开机都会
  重新收紧一次，所以是长期生效的。

代价要知道：

- 收紧之后，**非管理员**的运行会失败并提示
  `its devices are restricted to administrators, so run this program as administrator`。
  包括 `--list`、`--probe`、普通屏蔽运行，以及急救脚本——急救脚本因此改成了自己弹 UAC；
- 其它以非管理员身份使用该驱动的工具（例如 AutoHotInterception）也会一起被挡住，
  需要时用管理员执行一次 `blockkey --unlock` 放开；
- 不想让它动权限，就加 `--no-lockdown`（保持 Everyone 可用的原状）。

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
| 装成 Windows 服务（推荐） | 开机时，登录前 | 仅安装/卸载时 | Windows 事件日志（应用程序日志，source `blockkey`） |
| 计划任务（开机触发） | 开机时，登录前 | 仅安装/卸载时 | 无 |
| 启动文件夹 / 登录触发 | 登录之后 | 不需要 | 无（会留一个黑窗口） |

安装成服务：**不带参数时直接双击**（脚本自己弹 UAC 提权）；要带参数就先用管理员命令
提示符打开，例如：

```
samples\blockkey\install-service.cmd
samples\blockkey\install-service.cmd --hardware-id "VID_2717&PID_5011&REV_0100&MI_00"
```

脚本做三件事：把**运行文件复制到标准位置** `%ProgramFiles%\blockkey`（只会复制
`blockkey.exe`、`interception.dll` 和 `README.txt`，脚本本身留在原处）、`sc create` 指向
那份副本（`start= auto`、LocalSystem、命令行带 `--service` 和你给的参数，并设置崩溃后
自动重启）、注册事件日志源，然后立即启动。之后原文件夹挪走也不影响已装好的服务。

想装到别处就设环境变量（不要在末尾加反斜杠）：

```
set BLOCKKEY_INSTALL_DIR=D:\tools\blockkey
samples\blockkey\install-service.cmd
```

管理：

```
sc query blockkey                                  查看状态
sc stop blockkey                                   停止，右 Alt 立刻恢复
sc start blockkey                                  再次启动
samples\blockkey\uninstall-service.cmd             停止、删除服务，并清掉安装目录里的程序文件
```

卸载只删它自己复制过去的那三个文件；如果那个目录里还有别的东西，目录会保留；如果检测到
程序是"原地安装"（安装目录就是脚本所在目录，例如旧版本装的），它会保留文件不动。升级同理：
`sc stop blockkey` → 覆盖安装目录里的 `blockkey.exe`/`interception.dll` → `sc start blockkey`，
路径没变就不用重装服务。

安装目录的要求（默认值已经满足）：必须是**本地固定盘**、放在只有管理员能写的目录里——网络
共享、映射盘、U 盘都不行（服务在会话 0、开机时启动，那时没有网络和盘映射），非系统盘的
BitLocker 卷也可能在开机时尚未解锁；默认的 `%ProgramFiles%\blockkey` 正好避开这些坑。

- 服务没有控制台，所以运行情况写进 **Windows 事件日志**：启动参数、补发抬起的键盘数、
  停止时吞掉的按键总数。安装脚本会把这个源注册到
  `HKLM\SYSTEM\CurrentControlSet\Services\EventLog\Application\blockkey` 并指向
  `blockkey.exe`（消息表就编在 exe 里，由 `blockkey.mc` 经 `mc.exe`/`rc.exe` 生成）。
  查看方式：

  ```
  eventvwr.msc                                  事件查看器 -> Windows 日志 -> 应用程序
  Get-WinEvent -ProviderName blockkey           或 PowerShell
  ```

  日志大小由系统管：应用程序日志默认上限 20 MB、写满自动覆盖最旧的（可在事件查看器里
  改）。程序自己不写任何日志文件；只有在**打不开事件日志**这种异常情况下，才会退回写
  `%ProgramData%\blockkey\blockkey.log`。
- **只有由服务控制管理器启动的服务实例会写事件日志**。命令行的各种模式（`--list`、
  `--probe`、普通屏蔽运行，以及手动敲 `blockkey.exe --service`）一律只打印到控制台 /
  stderr，不碰事件日志；构建过程同样不写日志。
- 日志因此不会无限增长：正常一次开关机只有 2–3 条事件，卡住的键也**不会**逐条记录
  （服务模式下 `--verbose` 不生效），只在停止时记一个总数。
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
被透传、参数解析、退出组合键，以及驱动权限策略的判定和两个 DACL 的合法性，共 114 项
断言，MSVC、clang++、g++ 都能跑。测试里会强制带上 `--no-lockdown`，所以**即使从管理员
命令行跑测试也不会改动机器上驱动的实际权限**。

常见问题
--------

- `cannot reach the Interception driver`：驱动没装，或权限不够。程序会区分这两种情况：
  提示里出现 `its devices are restricted to administrators` 就说明驱动装好了、只是已被
  收紧成"仅管理员"，用管理员身份运行即可（要放开就执行一次管理员权限的
  `blockkey --unlock`）；提示 `install it and run this program as administrator` 才是驱动
  没装。
- `another instance is already running`：已经有一个 blockkey 在运行（单实例互斥）。
- **Alt/Ctrl 卡住不放，命令行里按 Enter 变成切换全屏、命令不执行**：这是 Windows 认为
  那个修饰键还按着，命令行就把 Enter 当成了 Alt+Enter。原因是坏键在**没有任何过滤**的
  时候被系统记成了"按下"（程序/服务没在跑，或 `--hardware-id` 没匹配到任何键盘）。
  三种解法：
  1. 敲一下**另一侧**的同名键（左 Alt）通常就能释放；
  2. 按 Ctrl+Alt+Del 进安全注意序列，也会重置修饰键状态；
  3. 用本程序的急救脚本（**双击即可，不需要键盘操作**，服务在跑时也能用；它会自己弹
     UAC 请求管理员权限，因为驱动设备通常已被收紧成仅管理员可打开）：

     ```
     samples\blockkey\release-stuck-key.cmd
     samples\blockkey\release-stuck-key.cmd 10      :: probe 窗口改成 10 秒
     ```

     它会先跑几秒 `--probe`，把键盘此刻在上报什么原样打印出来（方便确认坏键到底在发
     哪个扫描码），然后向所有键盘补发配置键两种 E0 变体的抬起事件。只想补发、不看
     probe 的话可以直接用 `blockkey.exe --release-only`。
  根治办法是让它一直跑（装成服务，见上一节）；服务启动时的补发抬起也会清掉开机过程中
  已经卡住的状态。另外程序现在会在启动时自检：`--hardware-id`/`--device` 没有匹配到
  任何键盘时会明确提示 `no keyboard matches ... nothing will be swallowed`，以免"看起来
  在跑其实什么都没拦"。
- `--service must be started by the service control manager`：`--service` 只给服务用，
  手动运行不要加它（这条错误也会作为 source `blockkey` 的事件记进应用程序日志）。
- **关于提权**：安装/卸载类脚本（`install-driver.cmd`、`install-service.cmd` 等）在没有
  管理员权限时会自己弹 UAC；但**带参数**时它们不通过 UAC 边界转发参数，而是提示你用管理员
  命令行运行——因为 cmd 在展开 `%*` 时会重新解析元字符，硬件 ID 里的 `&` 一定会被拆坏
  （`set BLOCKKEY_INSTALL_DIR` 时同理，避免静默装到默认目录）。所以：
  - 双击 = 立即提权，使用默认设置；
  - 要传 `--hardware-id "VID_xxxx&PID_xxxx..."` 这类参数，先开一个管理员命令提示符再运行。
- 服务起不来：在事件查看器的应用程序日志里按 source `blockkey` 筛（或
  `Get-WinEvent -ProviderName blockkey`）；再确认驱动已装、并且 `blockkey.exe --list`
  能正常列出键盘。
- 屏蔽之后 Alt 似乎还是“按着”的：说明按键在启动前已被系统认定为按下，而补发的抬起没
  生效。用 `--verbose` 确认事件确实被吞掉了，必要时重启后再启动本程序。
- 想确认它真的在工作：加 `--verbose`，按一下坏键会打印 `swallowed ...`。
- 本程序只设置键盘过滤器，鼠标完全不受影响。
