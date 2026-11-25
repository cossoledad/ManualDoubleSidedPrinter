# MDSPrinter Web

在连接打印机的 Ubuntu ARM64 主机上运行。手机访问 `http://主机局域网IP:30128`，上传 PDF 后可以按原文或两遍打印的顺序预览，支持模拟打印。上传上限 50 MiB、500 页。

## 一键安装或升级

从 GitHub Release 下载 Ubuntu ARM64 包，在 Ubuntu 主机上解压后运行安装脚本。

```bash
tar -xJf MDSPrinter-Web-linux-arm64-v*.tar.xz
sudo ./mdsprinter-web/install.sh
```

脚本补齐缺少的 `cups-client`、`qpdf` 和 `poppler-utils`，安装到 `/opt/mdsprinter-web`，启用并启动 `mdsprinter-web.service`。升级时用新的发布包运行同一脚本，保留 `/etc/mdsprinter-web.env` 和 `/var/lib/mdsprinter-web`。首次安装后可直接打开 `http://主机局域网IP:30128`。

发布包含 Node.js 24 ARM64。服务默认监听 `0.0.0.0:30128`。如需调整，可编辑 `/etc/mdsprinter-web.env` 中的 `MDS_BIND`、`MDS_PORT`。使用局域网主机名时设置 `MDS_HOSTNAME`，直接用 IP 地址无需设置。服务面向受信任的局域网，能访问页面的设备可以提交打印，请勿将端口映射到公网。

## 便携运行

```bash
sudo apt install cups-client qpdf poppler-utils
cd mdsprinter-web
./start.sh
```

便携方式将文档和任务状态存放在包目录下的 `data`，可用 `MDS_DATA` 指定独立目录。普通任务保留约一天，等待回放或提交结果待确认的真实任务保留到人工结束。一个状态目录只运行一个服务进程。

## 打印流程

1. 选 PDF，必要时输入页码范围并点击 **应用**。在 **打印到** 选择主机配置的 CUPS 队列，或选择模拟打印。新加打印机后点 **刷新打印机**。如已设置 `MDS_PRINTER`，它只作为首选项。
2. 提交第一遍。等所有纸张实际出完，再按示意把整叠纸保持印刷面朝向并顺时针旋转 180°，放回进纸盘。
3. 点 **已放回，打印第二遍**。结束任务会释放应用里的队列占用，已经提交的作业在 CUPS 中处理。

预览支持切换原文、第一遍和第二遍。鼠标滚轮或 `−`、`+` 缩放，按住鼠标左键或用单指自由平移，双指可捏合缩放。点击 **适合窗口** 或双击页面可复位。

网页刷新后可接续任务。两遍使用同一个队列，该队列在第二遍提交或明确结束后释放。提交结果待确认时，先到 CUPS 核对任务和纸张再结束，服务不会自动重新提交。

## 开发

在 `web` 目录执行 `node server.mjs`，开发入口默认监听 `127.0.0.1:30128`。运行 `npm test` 执行页序、预览交互、请求和状态测试。打印流水线测试用替身提交，Ubuntu 上可用 `MDS_TEST_PDF=1 npm test` 验证实际 PDF 重排和 PNG 渲染。
