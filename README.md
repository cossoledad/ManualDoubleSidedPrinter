# ManualDoubleSidedPrinter

面向 HP LaserJet Pro MFP M126a 的 Windows 手动双面打印工具。使用 C++20、Win32 原生控件、Conan 2 和 CMake。应用通过 Windows 自带的 PDF 引擎渲染文档，发行版为单个 `.exe`。

## 使用

1. 选择 PDF 和打印机。页码范围留空表示全部，也可输入 `1-3,5,8-10`。自定义页码会按文档顺序排列并去重。
2. 点击 **打印第一遍**，等打印机全部出纸。
3. 将整叠纸顺时针旋转 180°，保持印刷面朝向，放回进纸盘。点击 **已放回，打印第二遍**。

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

在 Windows 的 VS Code 中打开仓库目录，安装 Microsoft **C/C++** 扩展（`ms-vscode.cpptools`），按 **F5** 并选择 **ManualDoubleSidedPrinter (Debug)**。构建任务会自动加载 MSVC x64 环境，优先使用 PATH 上的 Ninja，也支持 Visual Studio 附带的 Ninja。可在 `src/main.cpp` 的 `wWinMain` 设置启动断点，或用 **Ctrl+Shift+B** 单独构建。

Debug 版和 PDB 位于 `build/vscode-debug`，这条本地调试路径可离线运行。

## 后续 PDF 预览

`src/pdf_document.*` 提供页数、页尺寸和指定尺寸的页面位图，可用于后续预览。预览可在后台按窗口所需的像素尺寸渲染当前页，请求过期时丢弃结果，并按页码和显示尺寸缓存少量位图。若要预览纸面效果，可按两遍打印计划映射原文页码，并直接显示空白占位。

当前打印逐页渲染，按可打印区域等比缩小并居中，位图最高 300 DPI，单边最多 4096 像素。提交成功表示作业已进入打印队列，纸张出完后再放回进纸盘。
