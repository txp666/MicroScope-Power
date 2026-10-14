import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
const here=path.dirname(fileURLToPath(import.meta.url));
const root=path.resolve(here,'../../..');
const out=path.resolve(here,'../media');
const require=createRequire(process.env.MICROSCOPE_MEDIA_MODULE_ROOT?path.join(process.env.MICROSCOPE_MEDIA_MODULE_ROOT,'package.json'):import.meta.url);
const sharp=require('sharp');
fs.mkdirSync(out,{recursive:true});
const C={ink:'#17232b',muted:'#596c78',teal:'#147d77',navy:'#315e83',orange:'#b87528',line:'#ced9df',paper:'#f5f8f9',white:'#ffffff'};
const esc=s=>String(s).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;');
const text=(x,y,s,size=34,fill=C.ink,weight=400,anchor='start')=>`<text x="${x}" y="${y}" fill="${fill}" font-size="${size}" font-weight="${weight}" text-anchor="${anchor}">${esc(s)}</text>`;
const lines=(x,y,ss,size=34,fill=C.ink,weight=400,step=size*1.45)=>ss.map((s,i)=>text(x,y+i*step,s,size,fill,weight)).join('');
const rect=(x,y,w,h,fill=C.paper,stroke=C.line,r=20)=>`<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${r}" fill="${fill}" stroke="${stroke}" stroke-width="2"/>`;
const line=(points,color=C.navy,width=5,arrow=false,dash=false)=>`<path d="${points}" fill="none" stroke="${color}" stroke-width="${width}" stroke-linejoin="round" stroke-linecap="round" ${arrow?`marker-end="url(#a-${color.slice(1)})"`:''} ${dash?'stroke-dasharray="12 10"':''}/>`;
const dot=(x,y,color=C.navy)=>`<circle cx="${x}" cy="${y}" r="7" fill="${color}"/>`;
const picture=(file,x,y,w,h,r=18)=>{const src=fs.readFileSync(path.join(root,file)).toString('base64');return `<defs><clipPath id="p-${x}-${y}"><rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${r}"/></clipPath></defs><image href="data:image/jpeg;base64,${src}" x="${x}" y="${y}" width="${w}" height="${h}" preserveAspectRatio="xMidYMid meet" clip-path="url(#p-${x}-${y})"/>`;};
const box=(x,y,w,h,title,sub=[],color=C.navy)=>rect(x,y,w,h,C.white,C.line,18)+`<rect x="${x}" y="${y}" width="7" height="${h}" rx="3" fill="${color}"/>`+text(x+w/2,y+58,title,38,color,650,'middle')+sub.map((s,i)=>text(x+w/2,y+112+43*i,s,29,C.muted,400,'middle')).join('');
const header=(n,title,subtitle,w=2400)=>text(100,78,`MICROSCOPE POWER  /  ${n}`,28,C.teal,600)+text(100,179,title,72,C.ink,700)+text(100,244,subtitle,34,C.muted)+text(w-100,78,'txp666 · 原创',28,C.muted,400,'end')+line(`M100 280H${w-100}`,C.line,2);
const svg=(w,h,body)=>`<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}"><defs>${[C.navy,C.teal,C.orange].map(c=>`<marker id="a-${c.slice(1)}" markerWidth="10" markerHeight="10" refX="8" refY="5" orient="auto" markerUnits="strokeWidth"><path d="M0 0L10 5L0 10Z" fill="${c}"/></marker>`).join('')}</defs><style>text{font-family:Arial,'PingFang SC','Microsoft YaHei','Noto Sans CJK SC',sans-serif}</style><rect width="${w}" height="${h}" fill="white"/>${body}</svg>`;
const entries=[];
async function save(name,w,h,body,label){const s=svg(w,h,body);fs.writeFileSync(path.join(out,name+'.svg'),s);await sharp(Buffer.from(s)).png({compressionLevel:9}).toFile(path.join(out,name+'.png'));await sharp(Buffer.from(s)).jpeg({quality:88,mozjpeg:true}).toFile(path.join(out,name+'.jpg'));if(name==='01-cover'){await sharp(Buffer.from(s)).resize(1150,800).jpeg({quality:90,mozjpeg:true}).toFile(path.join(out,'01-cover-upload.jpg'));}entries.push({name,title:label,width:w,height:h,jpegBytes:fs.statSync(path.join(out,name+'.jpg')).size});}
// Exact real photo and deterministic typography; no generated texture in the final cover.
await save('01-cover',2300,1600,
text(100,96,'OPEN HARDWARE  /  POWER & CURRENT',30,C.teal,600)+text(2200,96,'txp666 · 原创',30,C.muted,400,'end')+
`<rect x="100" y="145" width="140" height="8" fill="${C.teal}"/>`+
text(100,306,'MicroScope Power',144,C.ink,700)+rect(1570,202,630,245,'#eef6f5','none',18)+text(1605,345,'100',158,C.teal,700)+text(1910,343,'kSPS',66,C.teal,600)+text(1610,405,'最高采样配置',36,C.muted,500)+text(100,411,'便携式可调电源与微电流波形分析仪',62,C.ink,600)+text(100,492,'观察单片机的休眠、唤醒与活动电流',42,C.muted)+
rect(100,540,455,66,C.paper,'none',10)+text(125,585,'约 0.8–5 V 可调供电',33,C.teal,600)+rect(575,540,350,66,C.paper,'none',10)+text(600,585,'1 Ω / 10 Ω 双量程',33,C.teal,600)+rect(945,540,280,66,C.paper,'none',10)+text(972,585,'16-bit ADC',33,C.teal,600)+rect(1245,540,285,66,C.paper,'none',10)+text(1273,585,'WebUSB',33,C.teal,600)+rect(1550,540,245,66,C.paper,'none',10)+text(1578,585,'OLED',33,C.teal,600)+
picture('docs/open-source/source-assets/device-highres.jpg',225,640,1850,881,20)+line('M100 1545H2200',C.line,2)+text(100,1582,'供电为标称设计范围，实际 VOUT 受输入与负载压降影响。',27,C.muted)+text(2200,1582,'RP2040 · 固件 MIT',27,C.muted,400,'end'), '开源封面');
await save('02-hardware',2400,1550,
header('01','硬件组成','供电、采样、本地显示与网页分析，放在一块板上')+
text(100,340,'整机实物',33,C.ink,600)+picture('images/device.jpg',100,390,1190,568)+text(1390,340,'PCB、OLED 与外壳',33,C.ink,600)+picture('images/hardware.jpg',1390,370,880,660)+
[['01','可调供电','TPS7A90 + DAC60501','DAC 设定，LDO 稳压'],['02','双量程前端','INA296A3 ×2 + TMUX1119','1 Ω / 10 Ω 两路采样'],['03','本地显示','OLED + 旋转编码器','设置电压与查看空闲电流'],['04','浏览器分析','RP2040 + WebUSB','波形、游标、低通与导出']].map((a,i)=>{let x=100+i*560;return line(`M${x} 1100H${x+500}`,C.line,2)+text(x,1165,a[0],30,C.teal,600)+text(x,1230,a[1],44,C.ink,600)+lines(x,1300,a.slice(2),30,C.muted,400,48)}).join('')+
text(100,1494,'照片保留真实器件、外壳与显示内容；采集期间本地调压暂停，请在开始采集前完成设定。',29,C.muted),'硬件实物与功能');
await save('03-system',2400,1530,
header('02','系统框图','电源支路和测量支路分开：USB 总输入电流不等于输出支路电流')+
box(100,350,330,180,'USB 输入',['VBUS / D+ / D−'],C.teal)+box(780,350,390,180,'TPS7A90',['可调输出 LDO'],C.orange)+box(1300,350,470,180,'1 Ω + 10 Ω',['两只采样电阻串联'],C.orange)+box(1960,350,330,180,'被测负载',['VOUT → DUT'],C.orange)+
line('M430 440H780',C.orange,5,true)+text(580,409,'VBUS',28,C.muted)+line('M1170 440H1300',C.orange,5,true)+text(1213,409,'VREG',26,C.muted)+line('M1770 440H1960',C.orange,5,true)+text(1824,409,'VOUT',28,C.muted)+
box(100,690,380,170,'HE9073 3.3 V',['整机 V_SYS 供电'],C.teal)+line('M260 530V690',C.teal,5,true)+
box(600,740,360,180,'INA296A3 ×2',['各自检测分流压降','增益 50 V/V'])+box(1080,740,280,180,'TMUX1119',['选择一路信号','GP20 / SEL'])+box(1480,740,300,180,'OPA320',['单位增益缓冲'])+box(1930,740,360,180,'ADS8866',['16-bit SAR ADC','REF3025：2.5 V'])+
line('M1535 530V630H780V740',C.navy,5,true)+text(905,617,'两路分流压降',29,C.muted)+line('M960 830H1080',C.navy,5,true)+line('M1360 830H1480',C.navy,5,true)+line('M1780 830H1930',C.navy,5,true)+
box(100,1170,390,190,'DAC60501',['12-bit 电压设定','DAC 自身内部参考'],C.teal)+box(740,1170,540,190,'RP2040',['SPI 采样 / 双核服务','VOUT 分压读回 / 本地 UI'])+box(1500,1170,790,190,'ModuSignal 浏览器上位机',['电流曲线 · 低通 · A/B 游标 · CSV'])+
line('M2110 920V1050H1010V1170',C.navy,5,true)+text(1500,1032,'SPI 采样数据',29,C.muted)+line('M1280 1270H1500',C.navy,5,true)+text(1350,1234,'WebUSB',29,C.muted)+
line('M740 1260H490',C.teal,4,true,true)+text(584,1226,'I²C',29,C.teal)+line('M295 1170V1070H540V490H780',C.teal,4,true,true)+text(550,688,'FB 注入',28,C.teal)+
text(100,1450,'橙色：主输出电流路径    深蓝：采样与数据    蓝绿：供电与控制',30,C.muted)+text(100,1500,'框图按当前原理图整理；ADC 参考与 DAC 内部参考是两套独立来源。',28,C.muted),'电源与采样系统框图');
await save('04-current',2400,1650,
header('03','双量程电流采样','1 Ω 与 10 Ω 始终串联；SEL 选择放大器输出，不旁路采样电阻')+
text(110,342,'主电流路径',32,C.orange,600)+text(715,342,'两路电流检测',32,C.navy,600)+text(1370,342,'信号选择与 ADC',32,C.navy,600)+
box(130,390,270,110,'VREG',[],C.orange)+box(130,585,270,130,'R23 · 1 Ω',[],C.orange)+box(130,985,270,130,'R24 · 10 Ω',[],C.orange)+
line('M265 500V585',C.orange,5,true)+line('M265 715V985',C.orange,5,true)+line('M265 1115V1210',C.orange,5,true)+dot(265,555,C.orange)+dot(265,850,C.orange)+dot(265,1145,C.orange)+
box(130,1210,270,110,'VOUT',[],C.orange)+box(690,1195,500,145,'DUT → GND',['被测负载'],C.orange)+line('M400 1265H690',C.orange,5,true)+
box(710,550,400,200,'U14 · INA296A3',['1 Ω 通道','增益 50 V/V'])+box(710,950,400,200,'U15 · INA296A3',['10 Ω 通道','增益 50 V/V'])+
line('M265 555H425V610H710',C.teal,4,true)+line('M265 850H515V690H710',C.teal,4,true)+line('M515 850V1010H710',C.teal,4,true)+dot(515,850,C.teal)+line('M265 1145H425V1090H710',C.teal,4,true)+
text(660,584,'IN+',28,C.teal,600)+text(660,664,'IN−',28,C.teal,600)+text(660,984,'IN+',28,C.teal,600)+text(660,1064,'IN−',28,C.teal,600)+
rect(1320,675,360,350,C.white,C.line,18)+`<rect x="1320" y="675" width="7" height="350" rx="3" fill="${C.navy}"/>`+text(1500,737,'TMUX1119',38,C.navy,650,'middle')+text(1500,805,'S1 · 1 Ω 通道',31,C.muted,400,'middle')+text(1500,925,'S2 · 10 Ω 通道',31,C.muted,400,'middle')+
line('M1110 650H1215V790H1320',C.navy,5,true)+line('M1110 1050H1215V910H1320',C.navy,5,true)+
line('M1500 1120V1025',C.teal,4,true,true)+text(1500,1170,'RP2040 · GP20 / SEL',30,C.teal,500,'middle')+
box(1850,765,420,170,'OPA320',['单位增益缓冲'])+box(1850,1020,420,120,'ADC 输入 RC',['R25 33 Ω / C41 1 nF'])+box(1850,1210,420,150,'ADS8866',['16-bit · REF3025 2.5 V'])+
line('M1680 850H1850',C.navy,5,true)+line('M2060 935V1020',C.navy,5,true)+line('M2060 1140V1210',C.navy,5,true)+
rect(100,1400,1130,155,C.paper,C.line,16)+text(135,1445,'R 为当前通道 1 Ω / 10 Ω，VADC 已扣除零点',29,C.teal,600)+text(135,1513,'I(A) = VADC(V) / (50 × R(Ω))',43,C.ink,600)+
rect(1290,1400,440,155,C.paper,C.line,16)+text(1320,1445,'1 Ω：理论满量程',29,C.muted)+text(1320,1520,'≈ 50 mA',59,C.ink,600)+rect(1790,1400,510,155,C.paper,C.line,16)+text(1820,1445,'10 Ω：理论满量程',29,C.muted)+text(1820,1520,'≈ 5 mA',59,C.ink,600)+
text(100,1620,'原理示意，省略供电与部分前端 RC。理论满量程不是整机精度；主电流路径的总采样电阻为 11 Ω。',28,C.muted),'双量程采样原理');
await save('05-power',2400,1650,
header('04','DAC 如何调节电源','DAC 通过 R14 改变反馈节点电流，TPS7A90 自身的模拟闭环负责稳压')+
box(120,350,360,190,'USB · VBUS',['供电输入'],C.orange)+box(660,350,470,190,'TPS7A90',['FB 基准约 0.8 V'],C.orange)+line('M480 450H660',C.orange,5,true)+line('M1130 450H1900',C.orange,5,true)+text(1495,414,'VREG',34,C.orange,600)+box(1900,350,340,190,'1 Ω + 10 Ω',['到 VOUT / 负载'],C.orange)+
box(120,660,440,160,'RP2040',['I²C 写 DAC'],C.teal)+box(120,950,440,180,'DAC60501',['VDAC：标称 0…2.5 V'],C.teal)+line('M340 820V950',C.teal,5,true,true)+
dot(1510,450,C.orange)+line('M1510 450V620',C.orange,4)+rect(1460,620,100,190,C.white,C.orange,2)+text(1620,710,'R13 · 33 kΩ',35,C.ink,600)+line('M1510 810V1040',C.orange,4)+
line('M560 1040H740',C.teal,5)+rect(740,990,350,100,C.white,C.teal,2)+text(915,1054,'R14 · 20 kΩ',34,C.ink,600,'middle')+line('M1090 1040H1510',C.teal,5)+text(595,994,'VDAC',30,C.teal,600)+
dot(1510,1040,C.navy)+text(1570,1030,'FB ≈ 0.8 V',38,C.navy,600)+line('M1510 1040H1260V600H895V540',C.navy,4,true)+
line('M1510 1040V1130',C.navy,4)+rect(1460,1130,100,150,C.white,C.navy,2)+text(1620,1210,'R12 · 9.31 kΩ',35,C.ink,600)+line('M1510 1280V1320',C.navy,4)+line('M1465 1320H1555M1480 1337H1540M1495 1354H1525',C.navy,4)+
rect(100,1410,2200,160,C.paper,C.line,18)+text(135,1460,'标称理论关系（在 LDO 正常稳压条件下）',30,C.teal,600)+text(135,1530,'VREG ≈ 4.956 − 1.65 × VDAC',55,C.ink,600)+text(1330,1520,'VDAC 增加 → VREG 降低',38,C.ink,600)+
text(100,1620,'VOUT ≈ VREG − 11 Ω × I；当前电压读回用于显示，固件未实现 VOUT 的软件 PID 补偿。',30,C.muted),'DAC反馈注入与电压设定');
await save('06-host',2400,1560,
header('05','浏览器里查看电流过程','µA / mA 双轴，A/B 游标，可选截止频率低通，原始数据保留')+
picture('images/webusb-waveform.jpg',100,330,2200,1065,16)+
text(100,1460,'左键框选放大  ·  右键拖动平移  ·  双击恢复  ·  原始 / 低通 CSV 导出',36,C.ink,600)+text(100,1520,'用户提供的采集示例；图中的读数对应当次负载与滤波条件，不作为仪器精度或底噪指标。',29,C.muted),'网页波形与操作');
const paramRows=(x,y,rows)=>rows.map((r,i)=>line(`M${x} ${y+i*122+85}H${x+1040}`,C.line,2)+text(x,y+i*122+28,r[0],30,C.muted)+text(x+310,y+i*122+28,r[1],33,C.ink,600)).join('');
await save('07-parameters',2400,1810,
header('06','关键参数','100 kSPS 最高配置 · 约 0.8–5 V 标称可调供电')+
rect(100,340,1040,235,'#eef6f5','none',18)+text(140,473,'100',138,C.teal,700)+text(430,470,'kSPS',63,C.teal,600)+text(140,540,'10 / 20 / 50 / 100 kSPS 可选',33,C.muted)+
rect(1260,340,1040,235,C.paper,'none',18)+text(1300,475,'0.8–5 V',130,C.ink,700)+text(1300,540,'LDO 标称设计范围，实际 VOUT 随负载变化',32,C.muted)+
paramRows(100,685,[['外部 ADC','ADS8866 · 16 bit'],['ADC 参考','REF3025 · 2.5 V'],['前端增益','INA296A3 · 50 V/V'],['采样电阻','1 Ω / 10 Ω'],['理论电流上限','50 mA / 5 mA'],['理论电流码宽','0.763 / 0.0763 µA/码'],['总串联电阻','11 Ω（两只始终串联）']])+
paramRows(1260,685,[['主控','RP2040 · 双核'],['设定 DAC','DAC60501 · 12 bit'],['显示','1.3 英寸 OLED · 128 × 64'],['输入供电','USB 5 V · Type-C'],['通信','WebUSB · Chrome / Edge'],['低通预设','10 Hz–20 kHz · 一阶'],['自定义截止频率','1–45000 Hz（受采样率限制）']])+
text(100,1640,'供电标称理论值：VREG ≈ 0.831–4.956 V；VOUT ≈ VREG − 11 Ω × I。',30,C.muted)+text(100,1692,'100 kSPS 为最高采样配置；理论量程与码宽不等于整机精度。',30,C.muted)+text(100,1744,'数字低通实际截止频率不超过估算采样率的 45%；原始数据保留。',30,C.muted),'采样与供电关键参数');
fs.writeFileSync(path.join(out,'manifest.json'),JSON.stringify({date:'2026-10-14',style:'clean white engineering; exact real photographs and vector typography; generated background not used',entries},null,2)+'\n');console.log(JSON.stringify(entries,null,2));
