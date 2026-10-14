# MicroScope Power 开源图文资料

这套资料采用白底、实物照片、蓝绿点缀和精确排版。封面与框图没有重绘产品外观，图片中的器件、屏幕与外壳来自实际照片；参数以当前图纸和源码为依据。

- [完整图文预览](index.html)：正文、图片及下载入口。
- [立创开源平台正文](article.md)：可复制到项目描述。
- [平台标题、简介与标签](platform-fields.json)。
- [七张图片](media/)：每张提供 JPG、PNG、可编辑 SVG；上传推荐 JPG。
- [原理图、PCB 导出、器件清单与外壳模型](hardware/)。
- [电路证据](circuit-evidence.md)与[软件参数核对](software-evidence.md)：供技术复核使用。

## 图片清单

| 文件 | 用途 | 尺寸 |
| --- | --- | --- |
| `media/01-cover.jpg` | 工程封面与正文首图 | 2300 × 1600，23:16 |
| `media/01-cover-upload.jpg` | 较小封面备用 | 1150 × 800，23:16 |
| `media/02-hardware.jpg` | 整机、PCB、OLED 与外壳 | 2400 × 1550 |
| `media/03-system.jpg` | 电源、采样与通信系统框图 | 2400 × 1530 |
| `media/04-current.jpg` | 双量程电流采样原理 | 2400 × 1650 |
| `media/05-power.jpg` | DAC 调节 LDO 的反馈网络 | 2400 × 1650 |
| `media/06-host.jpg` | 网页波形、双轴、低通与游标 | 2400 × 1560 |
| `media/07-parameters.jpg` | 采样率、供电范围、量程与接口参数 | 2400 × 1810 |

封面与所有正文 JPG 均小于 1 MB。PNG 保留清晰文字；SVG 中嵌入照片，可继续编辑文字、位置与颜色。

## 在立创开源平台使用

1. 用 `01-cover.jpg` 替换工程封面。标题和简介可从 `platform-fields.json` 复制；复刻成本保留原值，未在这套资料里核算或宣传价格。
2. 将 `article.md` 的文字粘贴到描述编辑器。逐一上传七张 JPG，再用平台生成的图片链接替换正文中的 `media/*.jpg`；平台提示不支持直接使用外部图片链接。
3. 选定 Markdown 或富文本方式后保持在同一种编辑方式中完成内容。当前页面提示两种内容相互独立，提交只保留选中方式的内容。
4. 在“3D 模型”中使用 `hardware/上盖.step`、`底壳.step`、`挡板.step`。设计图优先使用平台原生工程；本包的 PADS 导出及原理图 PNG 用于备份和核对。
5. 附件可使用资料 ZIP、已公开的固件 UF2 与 ModuSignal 离线包。配套下载链接在正文末尾；固件与上位机许可分别说明。

## 硬件文件说明

硬件导出来自 2026-10-13 提供的资料，图纸标识 V1.0。PADS 和 PNG ZIP 保留原始字节；单独的 `.asc` / `.txt` 是 ZIP 内的原始文件，`.utf8` 仅作文字编码转换方便查看，未改变设计连接。

`hardware/components.csv` 从 PCB PART 数据汇总，含 108 个器件位号、50 行归并项目。未确证的厂家料号保持空白；这份表不包含采购价格，也不替代装配前对原生工程和实装料号的核对。

外壳 STEP 来源于用户同日提供的上盖、底壳与挡板。包内没有新生成的 Gerber、钢网或贴片生产文件。

## 重新生成图片

进入 `tooling/`，安装 Node.js 与依赖后运行：

```sh
npm install
npm run build
```

使用 `sharp` 渲染 SVG；为了保持中文外观，安装 PingFang SC、Microsoft YaHei 或 Noto Sans CJK SC 字体。脚本读取仓库 `images/` 和本目录 `source-assets/device-highres.jpg`；独立资料包中已提供可直接使用的图片，不要求使用者运行脚本。

最终采用真实照片和程序生成的精确排版。曾用 ImageGen 探索一张纹理背景，该背景未用于任何最终海报或资料包。无需图片生成服务即可从现有素材重新导出图片。

## 开源入口

- 固件源码：[MicroScope-Power](https://github.com/txp666/MicroScope-Power)，自有材料采用 MIT。
- 固件发布：[v0.2.1](https://github.com/txp666/MicroScope-Power/releases/tag/v0.2.1)。
- 上位机：[ModuSignal](https://github.com/txp666/modusignal)，仓库采用 Apache-2.0。
- 上位机发布：[v1.0.4](https://github.com/txp666/modusignal/releases/tag/v1.0.4)。
- 在线设备页：[MicroScope Power](https://modusignal.cn/pages/devices/microscope-power/)。

参考其他项目的章节组织与实物展示方式：[HARTLink](https://oshwhub.com/txp666/project_dhswhikf)、[AOMaster](https://oshwhub.com/txp666/project_dvokplei)。本文与图片文字为本项目重新整理。
