# 静读

静读是武哒哒 WDD 2026V1 的 ESP32 阅读固件，面向 240 × 416 的黑白触摸屏和单侧键。支持 TXT、可重排 EPUB、字形缓存、书签、离线英语练习与词典、局域网文件管理和无线墨屏。

从源码构建目标板应用，在仓库根目录运行：

```sh
./scripts/build.sh
```

桌面回归使用 `./tests/run.sh`。板端构建完成后，运行 `python3 scripts/package.py` 会在 `artifacts/` 生成应用镜像、随卡 ZIP 和 SHA256SUMS。ZIP 中的目录直接对应 TF 卡根目录。[环境与验证命令](docs/development/构建与验证.md) · [机型与分区](docs/hardware/WDD-2026V1.md)

设备使用者可从[阅读和导入指南](docs/guides/使用与导入.md)开始。二次开发入口见[工程规范](docs/development/工程目录与编码规范.md)、[智能体协作说明](AGENTS.md)和[文档索引](docs/README.md)。

第三方组件、随卡资料和字体各自保留来源与许可。[FreeInk 及第三方声明](licenses/NOTICE) · [随卡资料来源](tf/资料来源/来源.md)
