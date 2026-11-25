# MDSPrinter Web

在连接打印机的 Ubuntu ARM64 主机上运行。手机访问 `http://主机局域网IP:30128`，上传 PDF 后可以按原文或两遍打印的顺序预览，支持模拟打印。上传上限 50 MiB、500 页。

## 直接运行

先安装系统工具并用 `lpstat -p` 查看已验证能打印 PDF 的 CUPS 队列名。

```bash
sudo apt install cups-client qpdf poppler-utils
lpstat -p
tar -xJf MDSPrinter-Web-linux-arm64-v*.tar.xz
cd mdsprinter-web
MDS_PRINTER='你的CUPS队列名' ./start.sh
```

发布包已带 Node.js 24 ARM64。省略 `MDS_PRINTER` 时可预览和模拟。启动脚本默认监听 `0.0.0.0:30128`，可通过 `MDS_BIND` 和 `MDS_PORT` 调整。使用自定义局域网主机名时将其设为 `MDS_HOSTNAME`，直接用 IP 地址无需设置。服务面向受信任的局域网，能访问页面的设备可以提交打印，请勿将端口映射到公网。

## 后台运行

将解压后的 `mdsprinter-web` 目录放到 `/opt/mdsprinter-web`。创建 `/etc/mdsprinter-web.env`，写入 `MDS_PRINTER=实际队列名`。如需自定义监听地址，在同一文件添加 `MDS_BIND` 或 `MDS_HOSTNAME`。

```bash
sudo install -m 644 /opt/mdsprinter-web/mdsprinter-web.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now mdsprinter-web
journalctl -u mdsprinter-web -n 50 --no-pager
```

直接运行时文档和任务状态存放在包目录下的 `data`，可用 `MDS_DATA` 指定独立目录。systemd 方式默认使用 `/var/lib/mdsprinter-web`。升级时保留状态目录。普通任务保留约一天，等待回放或提交结果待确认的真实任务保留到人工结束。一个状态目录只运行一个服务进程。

## 打印流程

1. 选 PDF，必要时输入页码范围并点击 **应用**。右侧可以切换原文、第一遍和第二遍。
2. 提交第一遍。等所有纸张实际出完，再按示意把整叠纸保持印刷面朝向并顺时针旋转 180°，放回进纸盘。
3. 点 **已放回，打印第二遍**。结束任务会释放应用里的队列占用，已经提交的作业在 CUPS 中处理。

网页刷新后可接续任务。真实打印占用队列直到第二遍提交或明确结束，模拟与实物队列独立。提交结果待确认时，先到 CUPS 核对任务和纸张再结束，服务不会自动重新提交。

## 开发

在 `web` 目录执行 `node server.mjs`，开发入口默认监听 `127.0.0.1:30128`。安装 `qpdf`、`pdfinfo` 和 `pdftoppm` 后可验证真实 PDF 转换。运行 `node --test` 执行页序、请求和状态测试。打印流水线测试用替身提交，Ubuntu 上可用 `MDS_TEST_PDF=1 node --test` 验证实际 PDF 重排和 PNG 渲染。
