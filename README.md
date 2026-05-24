# ManualDoubleSidedPrinter

基于 Avalonia 的 HPM126a 手动双面辅助工具（仅支持 PDF）。

当前版本：1.0.1

## 功能

- 读取 PDF 页数并校验格式。
- 计算 M126a 双面手动打印两次任务：
  - 偶数总页：第一次奇数页，第二次偶数倒序页
  - 奇数总页：第一次打印全部奇数页，第二次先打印空白占位页，再打印偶数倒序页
  - 例如 5 页：第一次 1,3,5；第二次 空白,4,2
- 图形化步骤引导与旋转提示（顺时针旋转 180°）。
- 每次打印前自动抽取并重排 PDF 为临时文件，再一次性发送到打印队列（非逐页慢速发送）。

## VS Code 快捷任务

- `dotnet: run`：一键运行
- `dotnet: build`：构建
- `dotnet: publish win-x64`：发布 Windows 单文件
- `package: installer`：一键生成安装程序（需本机已安装 Inno Setup 6）
- 目标框架：`net10.0-windows`

## 真实打印

- 选择 PDF 后，选择目标打印机。
- 计划会自动生成。
- 点击“打印第一遍”。
- 整叠纸顺时针旋转 180° 回纸，点击“已旋转”。
- 点击“打印第二遍”。

## 调试

使用 `.vscode/launch.json` 的 `.NET Launch Avalonia App` 配置即可启动调试。

## 打包安装程序

- 使用 `.vscode/launch.json` 的 `Package Installer` 或任务 `package: installer`。
- 输出目录：`artifacts/installer`。
