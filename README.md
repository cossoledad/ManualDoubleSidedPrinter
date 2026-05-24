# ManualDoubleSidedPrinter

基于 Avalonia 的 HPM126a 手动双面辅助工具（仅支持 PDF）。

当前版本：1.0.4

## 功能

- 读取 PDF 页数并校验格式。
- 支持页码范围选择（类似 Chrome 打印）：
  - 全部页面
  - 自定义页面与区间，例如 `1-3,5,8-10`
- 计算 M126a 双面手动打印两次任务（基于已选页面顺序）：
  - 第一次打印：选中序列中的第 1/3/5... 张
  - 第二次打印：选中序列中的第 2/4/6... 张并倒序
  - 选中页数为奇数时，第二次会自动插入空白占位页
- 图形化步骤引导与旋转提示（顺时针旋转 180°）。
- 每次打印前自动抽取并重排 PDF 为临时文件，再一次性发送到打印队列（非逐页慢速发送）。
- 启动失败时会弹出错误提示，并指向 `%TEMP%/ManualDoubleSidedPrinter/startup.log` 方便排查。

## VS Code 快捷任务

- `dotnet: run`：一键运行
- `dotnet: build`：构建
- `dotnet: publish win-x64`：发布 Windows 自包含目录（兼容性优先）
- `package: installer`：一键生成安装程序（需本机已安装 Inno Setup 6）
- 目标框架：`net8.0-windows`

## 真实打印

- 选择 PDF 后，选择目标打印机。
- 选择“全部”或输入自定义页码区间并应用。
- 计划会自动生成（或在应用自定义范围后刷新）。
- 点击“打印第一遍”。
- 整叠纸顺时针旋转 180° 回纸，点击“已旋转”。
- 点击“打印第二遍”。

## 调试

使用 `.vscode/launch.json` 的 `.NET Launch Avalonia App` 配置即可启动调试。

## 打包安装程序

- 使用 `.vscode/launch.json` 的 `Package Installer` 或任务 `package: installer`。
- 输出目录：`artifacts/installer`。
