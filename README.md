# MDSPrinter

面向 HP LaserJet Pro MFP M126a 的手动双面打印工具。Windows 桌面版使用 C++20、Win32、Conan 2 和 CMake。Web 版部署在连接打印机的 Ubuntu ARM64 主机，手机和电脑可通过局域网在浏览器上传、预览和打印 PDF。

GitHub Release 提供 Windows 安装包、便携 ZIP 和 Ubuntu ARM64 Web 包。Web 版默认端口为 `30128`，见 [部署说明](web/README.md)。

## 使用

1. 选择 PDF 和打印目标。页码范围留空打印全部，也可输入 `1-3,5,8-10`。自定义页码按文档顺序排列并去重。
2. 在右侧预览中切换 **原文**、**第一遍** 或 **第二遍**。滚轮以指针为中心缩放，按住鼠标左键拖动查看放大的页面。双击页面或点击 **适合窗口** 恢复整页显示。
3. 点击 **打印第一遍**，等打印机全部出纸。双遍任务会自动切到第二遍预览并播放回放示意。
4. 将整叠纸顺时针旋转 180°，保持印刷面朝向，放回进纸盘。点击 **已放回，打印第二遍**。

回放示意会循环播放，直到继续或结束当前任务。关闭 Windows 的动画效果后，界面显示旋转前后的静态示意。

在 **打印到** 下拉框选择 **模拟打印（不出纸）**，可以演练第一遍、放回纸张引导和第二遍，无需连接打印机。

选中单页时只提交一次任务。选中五页时，第一遍顺序为 `1, 3, 5`，第二遍为 `空白, 4, 2`。空白页让第二遍先送过最后一张纸。打印机驱动若开启了跳过空白页或反序打印，请在 Windows 打印首选项中关闭这些选项。

## 构建和发布

使用 Windows 10 22H2 或 Windows 11、带桌面 C++ 工作负载和 Windows SDK 的 Visual Studio 2022 或 2026、CMake 3.24 以上（VS 2026 的 Visual Studio 生成器需要 CMake 4.2 以上）、Python 与 Conan 2.28 以上。

```powershell
python -m pip install 'conan>=2.28,<3'
conan profile detect
conan build . -of build/conan -s:h build_type=Release -s:h arch=x86_64 -s:h compiler.cppstd=20 -s:h compiler.runtime=static
ctest --test-dir build/conan/build -C Release --output-on-failure
./build/conan/build/Release/ManualDoubleSidedPrinter.exe
```

Conan 提供 CMake 工具链和应用打包配方。当前依赖由 Windows SDK 提供。安装 Inno Setup 6 后，用 PowerShell 7 运行 `./scripts/package-installer.ps1`，会在 `artifacts/installer` 生成安装包和便携 ZIP。推送到 `main` 时，GitHub Actions 使用同一脚本构建、测试并发布。

离线时，可在配置好 MSVC 和 Ninja 的开发者终端中直接构建。

```powershell
cmake -S . -B build/ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/ninja
ctest --test-dir build/ninja --output-on-failure
```

## VS Code 调试

在 Windows 的 VS Code 中打开仓库目录，安装 Microsoft **C/C++** 扩展（`ms-vscode.cpptools`），按 **F5** 并选择 **MDSPrinter (Debug)**。构建任务会自动加载 MSVC x64 环境，优先使用 PATH 上的 Ninja，也支持 Visual Studio 附带的 Ninja。可在 `src/main.cpp` 的 `wWinMain` 设置启动断点，或用 **Ctrl+Shift+B** 单独构建。

Debug 版和 PDB 位于 `build/vscode-debug`，这条本地调试路径可离线运行。

## PDF 渲染

预览复用系统 PDF 渲染接口，在后台逐页渲染。打印按可打印区域等比缩小并居中，位图最高 300 DPI，单边最多 4096 像素。
