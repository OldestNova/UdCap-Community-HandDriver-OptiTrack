# 宇叠社区驱动 (OptiTrack 支持)

该 Motive Peripheral API 插件为每个已连接的 UdCap 手套创建一个手套设备。设备显示名称为 `UdCapCD_<serial>`，保留真实手套的序列号，并设置 Motive 的左右手（left/right）顺序属性。当数据帧停止传输五秒后，Motive 会移除该设备。

## 构建

需要 Windows 系统、安装了 `PeripheralAPI` 组件的 Motive 3.1 或更高版本，以及 vcpkg。配置时必须指定 `MOTIVE_ROOT` 和 `UDCAP_CORE_SOURCE_DIR`，两者没有默认路径。

```powershell
cmake -S . -B build -G Ninja `
-DCMAKE_BUILD_TYPE=Release `
-DCMAKE_TOOLCHAIN_FILE=D:/Program/vcpkg/scripts/buildsystems/vcpkg.cmake `
-DMOTIVE_ROOT='C:/Program Files/OptiTrack/Motive3.5.0F' `
-DUDCAP_CORE_SOURCE_DIR='../UdCap-Community-HandDriver-Core'
cmake --build build
ctest --test-dir build --output-on-failure
```

关闭 Motive，然后将 `build/UdCapCommunityOptiTrack.dll` 和 `build/nng.dll` 复制到 `<Motive installation>/devices` 目录下。启动 Motive，将生成的手套设备与骨架（skeleton）进行配对，并在 UI 的首选项（Preferences）中启用 **Send gloves to OptiTrack**。Motive 的局部手部坐标系采用右手坐标系；本插件针对所有 15 个关节，反映了官方 Unity 预制件（prefab）的局部 X 轴朝向。

Motive 手套属性中的 `Battery`（电量）和 `Signal Strength`（信号强度）会根据各手套的遥测数据进行更新。电量显示为固件提供的五级读数，以 20% 为增量；信号强度是指当前接收机电池数据包中带符号 RSSI 字节的数值（范围 1–128）；当遥测数据不可用时，该值变为 `-1`。Motive 实时姿态与配对仍需进行硬件检查；仅靠构建（build）和 IPC 协议测试无法确定接收机的完整绑定姿态。
