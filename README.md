## 音量锁定

使用 Windows 7 新增的 Core Audio API 来锁定指定进程的音量，可同时锁定多个进程。

（我知道这个 API 从 vista 就有了，但 win7 才算能用）

### 下载

手动下载：<https://github.com/wzv5/VolumeLock/releases/latest>

或者使用 [Scoop](https://scoop.sh)：

```
scoop bucket add wzv5 https://github.com/wzv5/ScoopBucket
scoop install wzv5/volumelock
```

### 配置文件

创建 `config.yaml` 文件，与 exe 文件放在一起。

有 3 种匹配模式：完整路径（`FullPath`）、文件名（`FileName`）、正则表达式（`Regex`）。

`type` 和 `path` 都不区分大小写。

使用数组形式指定多个项目。

``` yaml
-
    type: regex
    path: "D:\\\\scoop\\\\home\\\\apps\\\\bh3\\\\.+"
    volume: 20
-
    type: fullpath
    path: "C:\\Program Files (x86)\\K-Lite Codec Pack\\MPC-HC64\\mpc-hc64.exe"
    volume: 80
-
    type: filename
    path: "QQMusic.exe"
    volume: 80
```

### 使用 VS2019 编译

通过 vcpkg 安装 yaml-cpp 依赖：`vcpkg install yaml-cpp:x64-windows-static`。

然后开始编译。

### 一些说明

- 疫情期间为了转移关注点而瞎写的，免得整天刷新闻看到令自己不愉快的东西
- 代码会尽量基于 C++ 标准库，尽量不调用平台 API，为了以后能更方便的重用代码
- Core Audio API 的坑好多。。微软文档也不说，还要靠自己猜错误原因
