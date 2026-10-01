disable-key.cmd —— 用 Windows 自带的 Scancode Map 永久屏蔽一个键
=================================================================

适用场景：某个键坏了（比如右 Alt 一直卡住/乱触发），而你又不想为它装驱动。
这个方案**不需要驱动、不需要签名、不暴露任何设备给别人打开**——它只是往注册表写一个
Windows 自带的映射表，由 `kbdclass.sys` 在键盘启动时读取。相比之下，Interception 那类
拦截驱动会创建一个 `\\.\interceptionNN` 设备，而且实测连低完整性（沙箱）进程都能打开它
读写键盘。

用法（写 HKLM，需要管理员；**不带参数时双击即可，会自己弹 UAC**）
----------------------------------------------------------------

```
disable-key.cmd                 屏蔽右 Alt（扫描码 E0 38）
disable-key.cmd 3A              屏蔽其它键（这里是不带 E0 前缀的 Caps Lock）
disable-key.cmd E052            带 E0 前缀的键，例如 Insert
disable-key.cmd /show           只显示当前设置和将要写入的字节，不改动任何东西
disable-key.cmd /show E052      看某个键会写成什么
disable-key.cmd /restore        还原成改动前的状态（用同目录的备份）
disable-key.cmd /replace E052   机器上已经存在 Scancode Map 时，强行覆盖（会先备份）
```

- 写入位置：`HKLM\SYSTEM\CurrentControlSet\Control\Keyboard Layout\Scancode Map`
- 第一次改动前会把整个 `Keyboard Layout` 键导出到同目录的
  `scancode-map-backup.reg`，`/restore` 就是导入它。
- 已经存在 Scancode Map 时脚本**默认拒绝覆盖**（会先打印现有映射），避免把你其它映射
  冲掉；要么用 SharpKeys 合并着加，要么加 `/replace`。
- **重启后生效**（这个表是键盘设备启动时读的）。生效后从开机起、包括登录界面都有效，
  而且不依赖任何常驻程序。

字节格式（已核对）
------------------

```
8 字节全 0          头部
DWORD               映射条数（含结尾那个 0）
每条映射 DWORD      目标低字节, 目标高字节, 源低字节, 源高字节
DWORD 全 0          结束
```

“关闭某个键”就是把**目标**写成 `0000`。例如右 Alt（源 E0 38）：

```
00000000 00000000 02000000 000038E0 00000000
└ header ┘        └ 条数=2 ┘ └ 映射 ┘ └ 结束 ┘
```

这个格式我做了交叉验证：本脚本写出的字节与 SharpKeys 的实现（`DefineScancodeMap` 里
每个映射 4 字节的顺序）以及 [Emacs FAQ for Windows 里给出的三个实例]
(https://www.gnu.org/software/emacs/manual/html_node/efaq-w32/Swap-Caps-NT.html)
逐字节一致；`/show` 打印的字节也与独立实现一致。

常见扫描码
----------

右 Alt `E038`、右 Ctrl `E01D`、Insert `E052`、左 Win `E05B`、右 Win `E05C`、
Menu `E05D`、Caps Lock `3A`、左 Alt `38`、左 Ctrl `1D`、Num Lock `45`。
完整表见 [Microsoft 的扫描码文档](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input#scan-codes)。

如果你还留着 Interception 驱动，可以用 `blockkey.exe --probe` 直接看到坏键上报什么。

局限
----

- **全局**：不区分键盘。外接键盘上的同名键也会一起被屏蔽。
- **静态**：改一次要重启；不能像驱动那样动态开关。
- 不能按设备筛选、不能探测/注入按键。需要这些能力就得回到驱动方案。

不想再用驱动的话
----------------

Interception 驱动本身仍然装着（`keyboard.sys` / `mouse.sys` 作为键盘、鼠标类的上层
过滤器），也就仍然有那个 Everyone 可打开的设备。要彻底去掉这份暴露：管理员运行
`samples\blockkey\uninstall-driver.cmd`（走官方 `/uninstall`），然后重启。
