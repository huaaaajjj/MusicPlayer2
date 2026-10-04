# 剥离音频内嵌封面和歌词

在播放列表或媒体库中选择一首或多首歌曲，右键选择 **剥离内嵌封面和歌词**。
也可以通过 **工具 → 剥离当前歌曲的内嵌封面和歌词** 处理当前歌曲。

- 支持 MP3、FLAC、M4A/MP4、WAV、WMA/ASF。
- 歌词以 UTF-8 保存到音频所在目录，文件名为 `歌曲文件名.lrc`。
- 封面保留原始图片字节，保存为 `歌曲文件名.jpg/png/gif/bmp/webp`，不重新压缩图片。
- 同名文件直接覆盖；同一歌曲文件名的其他常见图片扩展名（包括 `.jpeg`）会被清理，不生成编号副本。不同歌曲各保存一份，不共用 `cover.jpg`。
- 多个内嵌封面或歌词版本仅导出读取顺序中的首个非空版本，然后删除该文件中支持的封面、歌词标签。MP3 中重复的 APE 标签、FLAC 的旧式封面字段、M4A 的标准及 iTunes 自定义歌词字段也会清理。
- 没有对应内嵌内容时，不修改已有的外部歌词或图片；重复执行不会生成额外文件。
- 确认覆盖后才执行。导出使用同目录临时文件，写入完成并刷新到磁盘后替换目标；两个导出及重复封面清理均成功后才修改音频标签。音频不转码，其他标签保留。
- 无法写入、无法覆盖或清理失败时，不开始删除内嵌标签。已经导出的文件会保留，结果窗口列出失败文件；音频保存失败时仍保留导出文件供恢复。
- CUE 分轨、osu!、网络地址、不支持的音频/图片格式、只有空字符串歌词标签，以及 ID3 SYLT / ASF 同步歌词二进制标签会跳过，避免丢失无法导出的内容。普通包含时间轴的 LRC 文本支持导出。
- 当前正在播放的文件会临时关闭并恢复播放位置；成功后关联外部歌词和封面。

## 嵌入封面大小限制

所有通过属性编辑、批量嵌入、下载后嵌入及格式转换写入的封面，统一限制图片数据不超过 **4 MiB（4 × 1024 × 1024 字节）**。
小于或等于该限制时保留原始图片字节；超过限制时自动转为 JPEG，依次尝试 90、80、70、60 的质量，仍然过大则按比例缩小尺寸后重试，直到满足限制。
仅使用临时压缩副本嵌入，原始图片不变；临时文件在写入完成或失败后清理。压缩失败时不调用音频标签写入，已有内嵌封面不受影响。删除封面的操作不受限制。
超限图片的 EXIF 方向会应用到像素，透明区域铺白色背景，动画封面转为静态 JPEG。

## 自动验证

使用 Visual Studio 2022 C++/MFC 开发环境、Python、Mutagen 和 FFmpeg。测试只生成并处理合成音频，不操作真实音乐文件。
测试驱动独立于旧的 v141 测试项目，不需要把旧项目加入主解决方案。

在仓库根目录的 Developer PowerShell 中运行：

```powershell
msbuild UnitTest/EmbeddedMediaTests.vcxproj /p:Configuration=Release /p:Platform=x64
Copy-Item -LiteralPath x64/Release/tag.dll -Destination UnitTest/x64/EmbeddedMediaTests/tag.dll
python -m pip install --target UnitTest/x64/python-deps mutagen
$env:PYTHONPATH = (Resolve-Path UnitTest/x64/python-deps).Path
python -X utf8 UnitTest/test_embedded_media.py --driver UnitTest/x64/EmbeddedMediaTests/EmbeddedMediaTests.exe --ffmpeg ffmpeg -v
python -X utf8 UnitTest/test_album_cover_compression.py --driver UnitTest/x64/EmbeddedMediaTests/EmbeddedMediaTests.exe --ffmpeg ffmpeg --ffprobe ffprobe -v
```

需要与应用相匹配的 x64 `tag.dll` 和 Visual C++/MFC 运行库；FFmpeg 不在 PATH 时传入其完整路径。
测试覆盖五种音频格式、中文路径/歌词、多份标签清理、同名覆盖、重复执行、只读和写入失败保护、ID3v1/ID3v2 版本保留，并比较音频包 SHA-256 及其他标签。
测试样本保留在被 Git 忽略的 `UnitTest/x64/embedded-media-*` 中，便于复查。
