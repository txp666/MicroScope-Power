#!/usr/bin/env python3
"""Draw report figures from a combined-test CSV without copying its raw data.

Example:
  python3 tests/xiao_nrf52840_sense_plus/tools/plot_capture.py \
    /path/to/pico-power-scope-capture.csv

Requires numpy and matplotlib. Stage labels are approximate firmware alignment;
they are not peripheral IDs recorded by the CSV. The exported time is based on
frame-time interpolation, not independently timestamped ADC samples.
"""
from pathlib import Path
import argparse
import json
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager


def runs(mask):
    changes = np.diff(np.r_[False,mask,False].astype(np.int8))
    return zip(np.flatnonzero(changes==1),np.flatnonzero(changes==-1))


def find_sync(t,x):
    # The blue LED plateaus are about 0.33 mA, 100 ms on / 100 ms off.
    # Reject short spikes and long loads; require the three-pulse cadence.
    candidates = [(t[s],t[e-1]) for s,e in runs((x>150)&(x<1500))
                  if .075<t[e-1]-t[s]<.12]
    for i in range(len(candidates)-2):
        triple = candidates[i:i+3]
        if all(.18<value<.22 for value in np.diff([v[0] for v in triple])):
            return float(triple[0][0])
    raise ValueError('Cannot locate three SYNC plateaus; supply --sync-start explicitly.')


def aggregate(t,x,lo,hi,width):
    s,e = np.searchsorted(t,[lo,hi])
    tt,xx = t[s:e],x[s:e]
    key = np.floor((tt-lo)/width).astype(int)
    n = np.bincount(key)
    lower,upper = np.full(len(n),np.inf),np.full(len(n),-np.inf)
    np.minimum.at(lower,key,xx)
    np.maximum.at(upper,key,xx)
    mean = np.divide(np.bincount(key,weights=xx),n,
                     out=np.full(len(n),np.nan),where=n>0)
    lower[n==0]=upper[n==0]=np.nan
    return lo+(np.arange(len(n))+.5)*width,lower,upper,mean


def plot_envelope(ax,t,x,lo,hi,width,unit=1000,label=None,mean_label=None):
    tt,low,high,mean=aggregate(t,x,lo,hi,width)
    ax.fill_between(tt,low/unit,high/unit,color=BLUE,alpha=.34,lw=.45,label=label)
    ax.plot(tt,mean/unit,color=BLUE,lw=.7,label=mean_label)


def set_font():
    font=Path('/Library/Fonts/Arial Unicode.ttf')
    if font.exists():
        font_manager.fontManager.addfont(str(font))
        plt.rcParams['font.family']='Arial Unicode MS'
    plt.rcParams.update({
        'font.size':10,'axes.titlesize':11,'axes.labelsize':10,
        'xtick.labelsize':9,'ytick.labelsize':9,'legend.fontsize':9,
        'axes.spines.top':False,'axes.spines.right':False,
        'axes.axisbelow':True,'axes.unicode_minus':False,
        'grid.color':'#DFE5ED','grid.linewidth':.6,
        'figure.facecolor':'white','savefig.facecolor':'white',
    })


BLUE,ORANGE,GREEN,RED,GRAY='#1768AA','#B96C1C','#27826B','#B84141','#586779'


def overview(t,x,sync,out):
    fig,ax=plt.subplots(figsize=(10,4.65))
    fig.subplots_adjust(left=.075,right=.985,bottom=.19,top=.73)
    fig.text(.075,.965,'XIAO Sense Plus · 固定 1 Ω 完整电流波形',fontsize=15,va='top')
    fig.text(.075,.90,f'时长 {t[-1]:.3f} s；SYNC 近似起点 {sync:.6f} s；固定 1 Ω 量程。',
             fontsize=10,color=GRAY)
    plot_envelope(ax,t,x,0,t[-1]+1e-8,.002,
                  label='2 ms 最小–最大包络（保留峰值）',mean_label='2 ms 均值')
    phases=[(0,1,'SYNC'),(1,3,'CPU'),(3,6,'RGB'),(6,9,'IMU'),
            (9,12,'PDM'),(12,15,'BLE'),(15,16,'VBAT'),
            (16,17,'FLASH'),(17,18,'SHUTDOWN')]
    for i,(lo,hi,label) in enumerate(phases):
        if i%2:
            ax.axvspan(sync+lo,sync+hi,color='#EEF2F7',zorder=-2)
        ax.text(sync+(lo+hi)/2,1.025,label,transform=ax.get_xaxis_transform(),
                 ha='left',va='bottom',rotation=58,fontsize=9)
        ax.axvline(sync+lo,color='#BCC9D8',lw=.45,zorder=-1)
    ax.axvspan(27,t[-1],color='#F3F6F0',zorder=-2)
    ax.text(4.3,28.7,'启动',ha='center',fontsize=9,color=GRAY)
    ax.text(32.8,28.7,'休眠阶段：27 s 至末',ha='center',fontsize=9,color=GRAY)
    peak=int(np.argmax(x))
    ax.annotate(f'原始峰值 {x[peak]/1000:.3f} mA',
                 (t[peak],x[peak]/1000),xytext=(18.3,24.5),fontsize=10,color=RED,
                 arrowprops=dict(arrowstyle='->',lw=.8,color=RED))
    ax.legend(frameon=False,loc='upper right',bbox_to_anchor=(1,.78),handlelength=1.6)
    ax.set(xlim=(0,t[-1]),ylim=(-.6,31.2),xticks=np.arange(0,40,5),
           yticks=[0,5,10,15,20,25,30],xlabel='CSV 相对时间 / s',ylabel='整板电流 / mA')
    ax.grid(axis='y')
    fig.text(.075,.055,'按固件 18 秒负载时序标注；各阶段读数为整板电流。',
             fontsize=9,color=GRAY)
    fig.text(.075,.018,'曲线为每 2 ms 的最小–最大包络与平均值，保留原始采样峰值。',
             fontsize=9,color=GRAY)
    fig.savefig(out/'waveform-overview.png',dpi=160)
    plt.close(fig)


def sleep_detail(t,x,start,out):
    selected=t>=start
    tt,xx=t[selected],x[selected]
    mean=float(xx.mean())
    fig=plt.figure(figsize=(10,7.2))
    gs=fig.add_gridspec(3,1,left=.085,right=.985,bottom=.16,top=.87,
                        height_ratios=[1.2,.8,1.0],hspace=.55)
    ax0,ax1,ax2=[fig.add_subplot(gs[i]) for i in range(3)]
    fig.text(.085,.968,'休眠阶段电流 · 固定 1 Ω',fontsize=15,va='top')
    fig.text(.085,.922,
             f'{start:.0f}–{t[-1]:.6f} s；平均电流 {mean:.3f} μA；窄脉冲见局部放大。',
             fontsize=10,color=GRAY)
    plot_envelope(ax0,t,x,start,t[-1]+1e-8,.001,unit=1,
                  label='1 ms 最小–最大包络',mean_label='1 ms 均值')
    ax0.axhline(mean,color=ORANGE,ls='--',lw=.8,label=f'全样本均值 {mean:.3f} μA')
    ax0.set(title='A  休眠电流：峰值包络与均值',xlim=(start,t[-1]),ylim=(-8,255),
            yticks=[0,50,100,150,200,250],ylabel='电流 / μA')
    ax0.legend(frameon=False,loc='upper right',ncol=3,columnspacing=1,
                handlelength=1.4,fontsize=8)

    starts=np.arange(start,t[-1],2)
    ends=np.minimum(starts+2,t[-1]+1e-8)
    means=np.array([x[(t>=lo)&(t<hi)].mean() for lo,hi in zip(starts,ends)])
    centers=(starts+ends)/2
    ax1.plot(centers,means,'o-',color=BLUE,lw=1,ms=4)
    ax1.axhline(mean,color=ORANGE,ls='--',lw=.8)
    for lo,hi,value in zip(starts,ends,means):
        ax1.hlines(value,lo,hi,color=BLUE,lw=.7,alpha=.4)
    ax1.text(.995,.92,f'末窗 {ends[-1]-starts[-1]:.3f} s',transform=ax1.transAxes,
             ha='right',va='top',fontsize=9,color=GRAY)
    ax1.set(title='B  分窗均值：每窗 2 s，末窗保留实际长度',
            xlim=(start,t[-1]),ylim=(min(means.min(),mean)-.035,max(means.max(),mean)+.035),
            ylabel='均值 / μA',xlabel='CSV 相对时间 / s')
    ax1.ticklabel_format(axis='y',style='plain',useOffset=False)

    peak_index=int(np.flatnonzero(selected)[np.argmax(xx)])
    peak_time=t[peak_index]
    lo,hi=peak_time-.00065,peak_time+.001
    s,e=np.searchsorted(t,[lo,hi])
    ax2.plot((t[s:e]-lo)*1000,x[s:e],color=BLUE,lw=.8,marker='.',ms=3)
    ax2.annotate(f'本段最大样本 {x[peak_index]:.2f} μA',
                 ((peak_time-lo)*1000,x[peak_index]),xytext=(.88,185),
                 fontsize=9,color=GRAY,arrowprops=dict(arrowstyle='->',lw=.8,color=GRAY))
    ax2.set(title='C  窄脉冲局部放大：原始采样点',
            xlim=(0,(hi-lo)*1000),ylim=(-8,255),yticks=[0,50,100,150,200,250],
            xlabel=f'距 {lo:.6f} s 的时间 / ms',ylabel='电流 / μA')
    for ax in [ax0,ax1,ax2]:
        ax.grid(axis='y')
        ax.title.set_ha('left'); ax.title.set_position((0,1.02))
    fig.text(.085,.056,f'{mean:.3f} μA 为固定 1 Ω 量程下的统计均值，包含零点、噪声与量程分辨率影响。',
             fontsize=9,color=GRAY)
    fig.text(.085,.025,f'统计时长约 {t[-1]-start:.2f} s；时间轴由帧时间戳插值。',
             fontsize=9,color=GRAY)
    fig.savefig(out/'sleep-detail.png',dpi=160)
    plt.close(fig)


def flash_shutdown(t,x,sync,out):
    flash,shutdown,finish=sync+16,sync+17,sync+18
    lo,hi=round(flash,1),round(finish+.2,1)
    fig=plt.figure(figsize=(10,5.7))
    gs=fig.add_gridspec(2,1,left=.085,right=.985,bottom=.15,top=.85,
                        height_ratios=[1,1.1],hspace=.52)
    ax0,ax1=[fig.add_subplot(gs[i]) for i in range(2)]
    fig.text(.085,.968,f'Flash 检查与收尾 · {lo:.1f}–{hi:.1f} s',fontsize=15,va='top')
    fig.text(.085,.91,'Flash 检查、LED 标记与测试结束后的电流变化。',
             fontsize=10,color=GRAY)
    plot_envelope(ax0,t,x,lo,hi,.00025,
                  label='0.25 ms 最小–最大包络',mean_label='0.25 ms 均值')
    ax0.axvspan(flash,shutdown,color='#EEF2F7',zorder=-2)
    ax0.axvspan(shutdown,finish,color='#EEF6EF',zorder=-2)
    ax0.text((flash+shutdown)/2,14.3,'FLASH：1 s 时段',ha='center',color=GRAY,fontsize=9)
    ax0.text((shutdown+finish)/2,14.3,'SHUTDOWN：1 s 时段',ha='center',color=GRAY,fontsize=9)
    flash_window=(t>=flash)&(t<flash+.08)
    flash_index=int(np.flatnonzero(flash_window)[np.argmax(x[flash_window])])
    ax0.annotate('Flash 检查的短时活动', (t[flash_index],x[flash_index]/1000),xytext=(flash+.18,9),
                 color=GRAY,fontsize=9,arrowprops=dict(arrowstyle='->',lw=.8,color=GRAY))
    ax0.set(title='A  Flash 与 LED 活动：完整纵轴',xlim=(lo,hi),ylim=(-.3,16),
            yticks=[0,5,10,15],ylabel='电流 / mA')
    ax0.legend(frameon=False,loc='center right',fontsize=8,handlelength=1.5)

    plot_envelope(ax1,t,x,lo,hi,.0005,unit=1)
    for green_start in [shutdown,shutdown+.16]:
        ax1.axvspan(green_start,green_start+.08,color='#D9EEE4',zorder=-2)
    ax1.axvspan(shutdown+.32,finish,color='#EEF1F4',zorder=-2)
    ax1.text(shutdown+.16,235,'两次约 80 ms 绿灯窗口',ha='center',color=GREEN,fontsize=9)
    ax1.text((shutdown+.32+finish)/2,195,'测试收尾：时槽等待',
             ha='center',color=GRAY,fontsize=9)
    ax1.axvline(finish,color=RED,ls='--',lw=.8)
    ax1.annotate('测试结束\n进入休眠阶段',(finish,8),xytext=(finish-.04,100),
                  ha='right',va='center',color=RED,fontsize=8,
                  arrowprops=dict(arrowstyle='->',lw=.8,color=RED))
    ax1.set(title='B  LED 标记与休眠电流：纵轴放大（瞬态峰值见 A）',
            xlim=(lo,hi),ylim=(-12,280),yticks=[0,100,200],
            xlabel='CSV 相对时间 / s',ylabel='电流 / μA')
    for ax in [ax0,ax1]:
        ax.grid(axis='y')
        ax.title.set_ha('left'); ax.title.set_position((0,1.02))
    fig.text(.085,.06,'Flash 与测试收尾各占 1 s 时槽；LED 标记为两次约 80 ms 绿灯。',
             fontsize=9,color=GRAY)
    fig.text(.085,.025,'各阶段均为整板电流，包含 MCU、板载外设和供电电路。',
             fontsize=9,color=GRAY)
    fig.savefig(out/'flash-shutdown.png',dpi=160)
    plt.close(fig)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv',type=Path)
    parser.add_argument('--output-dir',type=Path,default=Path(__file__).resolve().parents[1]/'images')
    parser.add_argument('--sync-start',type=float,default=None)
    parser.add_argument('--sleep-start',type=float,default=27.0)
    args=parser.parse_args()
    data=np.loadtxt(args.csv,delimiter=',',skiprows=1)
    if data.ndim!=2 or data.shape[1]!=4:
        parser.error('Expected CSV columns index,time_s,range_ohm,current_uA')
    t,x=data[:,1],data[:,3]
    if not np.all(np.isfinite(data)) or not np.all(np.diff(t)>0):
        parser.error('The capture must have finite values and strictly increasing times')
    if not np.all(data[:,2]==1):
        parser.error('These figures are for a fixed 1-ohm capture')
    sync=find_sync(t,x) if args.sync_start is None else args.sync_start
    set_font(); args.output_dir.mkdir(parents=True,exist_ok=True)
    overview(t,x,sync,args.output_dir)
    sleep_detail(t,x,args.sleep_start,args.output_dir)
    flash_shutdown(t,x,sync,args.output_dir)
    tail=x[t>=args.sleep_start]
    print(json.dumps({'source':str(args.csv),'rows':len(t),'duration_s':float(t[-1]),
                      'sync_alignment_s':sync,'peak_uA':float(x.max()),
                      'sleep_start_s':args.sleep_start,'sleep_n':len(tail),
                      'sleep_mean_uA':float(tail.mean()),
                      'figures':[str(args.output_dir/name) for name in
                                 ['waveform-overview.png','sleep-detail.png','flash-shutdown.png']]},
                     ensure_ascii=False,indent=2))


if __name__=='__main__':
    main()
