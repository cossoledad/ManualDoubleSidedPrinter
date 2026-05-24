# ManualDoubleSidedPrinter

Windows 下的 HPM126a 手动双面打印辅助工具（仅支持 PDF）。

当前版本：1.0.5

## 核心功能

- 自动生成第一遍/第二遍打印顺序（适配手动双面流程）
- 支持页码范围输入：`1-3,5,8-10`
- 可视化步骤引导：打印第一遍 -> 旋转 -> 打印第二遍
- 启动失败自动记录日志：`%TEMP%/ManualDoubleSidedPrinter/startup.log`

## 快速使用

1. 选择 PDF
2. 选择打印机
3. 选择页码模式（全部/自定义）
4. 点击“打印第一遍”
5. 纸张顺时针旋转 180° 后点击“已旋转”
6. 点击“打印第二遍”

## 本地开发

- 目标框架：`net8.0-windows`
- 运行：`dotnet run --project ManualDoubleSidedPrinter.csproj -f net8.0-windows`
- 构建：`dotnet build -f net8.0-windows`

## 打包

- 发布：`dotnet publish ManualDoubleSidedPrinter.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=false -o artifacts/publish/win-x64`
- 安装包：`powershell -ExecutionPolicy Bypass -File scripts/package-installer.ps1`
- 输出目录：`artifacts/installer`

## CI/CD

- 推送到 `main` 后，GitHub Actions 自动：
- 构建项目
- 生成安装包（Inno Setup）
- 发布 GitHub Release（附 `.exe` 与 `.zip`）
