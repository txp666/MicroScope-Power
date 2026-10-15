#!/usr/bin/env python3
"""Analyze one fixed-range capture without copying its raw samples into Git."""
from pathlib import Path
import argparse,hashlib,json
import numpy as np
import pandas as pd

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('csv',type=Path)
parser.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[1]/'data/analysis.json')
args=parser.parse_args()
df=pd.read_csv(args.csv)
assert list(df.columns)==['index','time_s','range_ohm','current_uA']
t=df.time_s.to_numpy();x=df.current_uA.to_numpy();r=df.range_ohm.to_numpy();dt=np.diff(t)

def stat(values):
    return {'samples':len(values),'mean_uA':float(values.mean()),'median_uA':float(np.median(values)),
            'ac_rms_uA':float(values.std(ddof=0)),'min_uA':float(values.min()),'max_uA':float(values.max())}
def window(lo,hi):
    return {'start_s':lo,'end_s':hi,**stat(x[(t>=lo)&(t<hi)])}

def current_runs(lo, hi, lower, upper=float('inf'), merge_s=0, min_s=0):
    """Threshold intervals; right endpoint is the first excluded sample."""
    a,b=np.searchsorted(t,[lo,hi]); tt=t[a:b]; xx=x[a:b]
    edges=np.diff(np.r_[False,(xx>lower)&(xx<upper),False].astype(np.int8))
    spans=[]
    for start,end in zip(np.flatnonzero(edges==1),np.flatnonzero(edges==-1)):
        if spans and tt[start]-tt[spans[-1][1]-1]<=merge_s:
            spans[-1][1]=int(end)
        else:
            spans.append([int(start),int(end)])
    return [{'start_s':float(tt[start]),'end_s':float(tt[min(end,len(tt)-1)]),
             'duration_s':float(tt[min(end,len(tt)-1)]-tt[start])}
            for start,end in spans if tt[min(end,len(tt)-1)]-tt[start]>=min_s]

def measure_timing():
    # Locate three blue plateaus by shape, independently of the ideal stage times.
    candidates=current_runs(float(t[0]),float(t[-1])+1e-9,150,1500,min_s=.075)
    candidates=[v for v in candidates if v['duration_s']<.12]
    triple=None
    for i in range(len(candidates)-2):
        group=candidates[i:i+3]
        if all(.18<d<.22 for d in np.diff([v['start_s'] for v in group])):
            triple=group
            break
    if triple is None:
        raise ValueError('Cannot identify the three SYNC plateaus.')
    ref=triple[0]['start_s']
    # Local searches use the firmware schedule only to select the neighborhood;
    # each reported boundary is a threshold crossing in the recorded samples.
    schedule=[('CPU',1,3,3000),('RGB',3,6,400),('IMU',6,9,3000),
              ('PDM',9,12,1000),('BLE',12,15,2000),('VBAT',15,16,2000),
              ('FLASH',16,17,3000)]
    checkpoints=[]
    for name,start,end,threshold in schedule:
        events=current_runs(ref+start-.01,ref+start+.01,threshold)
        if not events:
            raise ValueError(f'Cannot identify {name} start activity.')
        measured=events[0]['start_s']
        checkpoints.append({'stage':name,'program_slot_s':[start,end],
            'measured_activity_start_s':measured,'threshold_uA':threshold,
            'offset_from_reference_ms':(measured-ref-start)*1000})
    cpu=max(current_runs(ref+.8,ref+3.1,3000,min_s=.5),key=lambda v:v['duration_s'])
    flash=max(current_runs(ref+15.99,ref+16.1,3000,min_s=.001),key=lambda v:v['duration_s'])
    lights=current_runs(ref+16.99,ref+17.35,70,merge_s=.001,min_s=.06)
    if len(lights)!=2:
        raise ValueError('Cannot identify the two shutdown light responses.')
    checkpoints.append({'stage':'SHUTDOWN_LED','program_slot_s':[17,18],
        'measured_activity_start_s':lights[0]['start_s'],'threshold_uA':70,
        'offset_from_reference_ms':(lights[0]['start_s']-ref-17)*1000})
    final=current_runs(ref+17.99,ref+18.05,300,merge_s=.0001,min_s=.00005)
    if len(final)!=2:
        raise ValueError('Cannot identify the two final software activity pulses.')
    return {'sync_reference_s':ref,'sync_plateau_candidates':triple,
        'sync_reference_note':'Approximate blue plateau onset; initialization overlaps the first marker.',
        'search_half_width_ms':10,'checkpoints':checkpoints,
        'cpu_continuous_load':cpu,'flash_continuous_activity':flash,
        'shutdown_light_responses':lights,'final_software_activity':final,
        'final_activity_start_separation_ms':(final[1]['start_s']-final[0]['start_s'])*1000,
        'sleep_transition_approx_s':round(final[-1]['end_s'],3),
        'samples_over_300uA_after_final_activity':int((x[t>=final[-1]['end_s']]>300).sum()),
        'expected_sequence_s':18,'post_sequence_delay_ms':10,
        'method':'Current threshold edges in local search windows, not GPIO timestamps. '
                 'Intervals may include CPU transients and analog edge response. '
                 'CSV frame-interpolated times support sequence alignment, not clock calibration.'}

windows=[('CPU',10,11),('RGB red',12,12.7),('RGB green',13,13.7),('RGB blue',14,14.7),
         ('IMU',15.1,17.5),('PDM',18.1,20.5),('BLE',21.1,23.5),('VBAT',24.05,24.75),
         ('Flash wait',25.05,25.75),('Shutdown wait',26.3,26.75)]
# Current threshold event intervals on the exported time axis, not GPIO widths.
mask=(t>=27)&(x>100);edges=np.diff(np.r_[False,mask,False].astype(np.int8));runs=[]
for a,b in zip(np.flatnonzero(edges==1),np.flatnonzero(edges==-1)):
    if runs and t[a]-t[runs[-1][1]-1] <= .00015:runs[-1][1]=int(b)
    else:runs.append([int(a),int(b)])
starts=np.array([t[a] for a,b in runs])
widths=np.array([t[b]-t[a] for a,b in runs])
peaks=np.array([x[a:b].max() for a,b in runs])
end=float(t[-1]);tail=window(27,end+1e-9);tail['end_s']=end;tail['duration_s']=end-27
idx=int(np.argmax(x))
result={'source_csv':args.csv.name,'source_sha256':hashlib.sha256(args.csv.read_bytes()).hexdigest(),
 'board':'Seeed Studio XIAO nRF52840 Sense Plus','firmware_version':'XIAO-COMBINED-3',
 'scope':'Fixed 1-ohm capture named above.',
 'quality':{'columns':list(df.columns),'rows':len(df),'duration_s':end-float(t[0]),
    'missing_values':int(df.isna().sum().sum()),'nonfinite_values':int((~np.isfinite(df.to_numpy())).sum()),
    'index_discontinuities':int((np.diff(df['index'])!=1).sum()),'nonincreasing_timestamps':int((dt<=0).sum()),
    'range_ohm':np.unique(r).tolist(),'range_switches':int((np.diff(r)!=0).sum()),
    'gaps_over_1ms':int((dt>.001).sum()),'median_dt_us':float(np.median(dt)*1e6),
    'min_dt_us':float(dt.min()*1e6),'max_dt_us':float(dt.max()*1e6)},
 'full_window':stat(x),'full_window_time_weighted_mean_uA':float(np.trapezoid(x,t)/(t[-1]-t[0])),
 'peak_time_s':float(t[idx]),'peak_range_ohm':int(r[idx]),'sync_reference_s':8.898,
 'stages':{name:window(lo,hi) for name,lo,hi in windows},'sleep':tail,
 'sleep_subwindows':[window(27,30),window(30,33),window(33,36),window(36,end+1e-9)],
 'sleep_pulses':{'threshold_uA':100,'merged_gap_us':150,'events':len(runs),
    'median_width_us':float(np.median(widths)*1e6),'min_width_us':float(widths.min()*1e6),
    'max_width_us':float(widths.max()*1e6),'median_interval_ms':float(np.median(np.diff(starts))*1000),
    'median_peak_uA':float(np.median(peaks))},
 'quantization_step_uA':float(np.median(np.diff(np.unique(x)))),
 'timing_alignment':measure_timing(),
 'display':{'voltage_input_V':3.782,'nominal_sampling_option_sps':100000,'window_setting_s':60,
    'manual_y_max_uA':14507.201,'samples_above_y_max':int((x>14507.201).sum()),
    'screenshot_current_uA':3.815,'screenshot_energy_mJ':144.155,
    'voltage_is_independent_measurement':False},
 'methods':{'mean':'All samples in each stated interval, unfiltered and without excluding current spikes.',
    'ac_rms':'Population standard deviation after subtracting the interval mean.',
    'timing':'Exported frame-interpolated time values; no independent sample-level timestamps.',
    'stage_identity':'Aligned to firmware sequence and current markers; not explicit CSV phase labels.'}}
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({'source_sha256':result['source_sha256'],'quality':result['quality'],'full':result['full_window'],'sleep':result['sleep'],'sleep_pulses':result['sleep_pulses'],'peak_time_s':result['peak_time_s'],'samples_above_y_max':result['display']['samples_above_y_max']},ensure_ascii=False,indent=2))
