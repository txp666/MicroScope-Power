#!/usr/bin/env python3
"""Replay OLED averaging against an unmodified exported current CSV.

Usage: python3 tests/firmware/replay_local_current.py capture.csv

This is a floating-point, timestamp-based model, not a hardware timing test.
It neither repeats the recording nor assumes a particular expected current.
A fresh getter may close a bucket whose wall-clock interval has already ended.
It never adds samples to account for the gap before that getter.
"""

import argparse
import bisect
from collections import deque
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics


BUCKET_NS = 50_000_000
WINDOW_BUCKETS = 40
REFRESH_NS = 200_000_000
DISPLAY_DEADBAND_UA = 0.05
MAX_SAMPLE_AGE_NS = 5_000_000


def rounded(value):
    return round(value, 6) if value is not None else None


def quantile(sorted_values, fraction):
    position = fraction * (len(sorted_values) - 1)
    low = int(position)
    high = min(low + 1, len(sorted_values) - 1)
    return sorted_values[low] + (sorted_values[high] - sorted_values[low]) * (
        position - low
    )


def summarize(values, reference_mean):
    if not values:
        return {"count": 0}
    mean = statistics.fmean(values)
    error = max(abs(value - reference_mean) for value in values)
    return {
        "count": len(values),
        "min_uA": rounded(min(values)),
        "max_uA": rounded(max(values)),
        "mean_uA": rounded(mean),
        "mean_error_uA": rounded(mean - reference_mean),
        "max_abs_error_uA": rounded(error),
        "all_within_0_05_uA_of_source_mean": error <= DISPLAY_DEADBAND_UA,
    }


def read_capture(path):
    times_ns, currents, ranges = [], [], set()
    previous_index = None
    with path.open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        required = {"index", "time_s", "range_ohm", "current_uA"}
        if not required.issubset(reader.fieldnames or []):
            raise ValueError("CSV requires index,time_s,range_ohm,current_uA columns")
        for line_number, row in enumerate(reader, 2):
            time_s = float(row["time_s"])
            current = float(row["current_uA"])
            range_ohm = float(row["range_ohm"])
            if not all(math.isfinite(value) for value in (time_s, current, range_ohm)):
                raise ValueError(f"non-finite value at CSV line {line_number}")
            time_ns = round(time_s * 1_000_000_000)
            index = int(row["index"])
            if times_ns and time_ns <= times_ns[-1]:
                raise ValueError(f"non-increasing timestamp at CSV line {line_number}")
            if previous_index is not None and index != previous_index + 1:
                raise ValueError(f"missing/reordered sample index at CSV line {line_number}")
            previous_index = index
            times_ns.append(time_ns)
            currents.append(current)
            ranges.add(range_ohm)
    if len(currents) < 2:
        raise ValueError("CSV needs at least two samples")
    if len(ranges) != 1:
        raise ValueError("replay requires one continuous range; split range changes first")
    origin = times_ns[0]
    return [time_ns - origin for time_ns in times_ns], currents, ranges.pop()


def replay_continuous(times_ns, currents):
    completed = deque(maxlen=WINDOW_BUCKETS)
    bucket_index, bucket_sum, bucket_count = 0, 0.0, 0
    next_refresh = REFRESH_NS
    ema = displayed = None
    output = []
    refresh_count = warmup_count = 0
    last_sample_ns = None

    def close_ended_bucket(time_ns):
        nonlocal bucket_index, bucket_sum, bucket_count
        next_bucket = time_ns // BUCKET_NS
        if next_bucket == bucket_index:
            return
        if next_bucket != bucket_index + 1 or bucket_count == 0:
            raise ValueError("missing complete 50 ms bucket; recording is discontinuous")
        completed.append((bucket_sum, bucket_count))
        bucket_index = next_bucket
        bucket_sum, bucket_count = 0.0, 0

    def refresh(time_ns):
        nonlocal ema, displayed, refresh_count, warmup_count
        refresh_count += 1
        if last_sample_ns is None or time_ns - last_sample_ns > MAX_SAMPLE_AGE_NS:
            return
        # Getter wall time can end the bucket before the next raw sample arrives.
        # Closing it changes only its status; its recorded sum/count stay intact.
        close_ended_bucket(time_ns)
        if len(completed) < WINDOW_BUCKETS:
            warmup_count += 1
            return
        window_sum = math.fsum(total for total, _ in completed)
        window_count = sum(count for _, count in completed)
        mean = window_sum / window_count
        ema = mean if ema is None else ema + (mean - ema) / 4.0
        changed = displayed is None or abs(ema - displayed) >= DISPLAY_DEADBAND_UA
        if changed:
            displayed = ema
        output.append({
            "time_s": rounded(time_ns / 1_000_000_000),
            "last_sample_age_us": rounded((time_ns - last_sample_ns) / 1_000),
            "window_mean_uA": rounded(mean),
            "ema_uA": rounded(ema),
            "display_uA": rounded(displayed),
            "display_text": f"{displayed:.2f}UA",
            "display_changed": changed,
        })

    for time_ns, current in zip(times_ns, currents):
        # Refreshes see only samples acquired by their real wall-clock time.
        while next_refresh < time_ns:
            refresh(next_refresh)
            next_refresh += REFRESH_NS
        close_ended_bucket(time_ns)
        bucket_sum += current
        bucket_count += 1
        last_sample_ns = time_ns
        if next_refresh == time_ns:
            refresh(next_refresh)
            next_refresh += REFRESH_NS
    # The export may end less than one normal sampling interval before a UI
    # boundary. Permit at most two typical intervals without inventing raw data.
    # The ordinary 5 ms freshness guard still applies to every getter.
    median_interval_ns = statistics.median(
        later - earlier for earlier, later in zip(times_ns, times_ns[1:])
    )
    recorded_end_limit_ns = times_ns[-1] + round(median_interval_ns * 2)
    while next_refresh <= recorded_end_limit_ns:
        refresh(next_refresh)
        next_refresh += REFRESH_NS
    return output, refresh_count, warmup_count


def nearest_index(times_ns, target_ns):
    position = bisect.bisect_left(times_ns, target_ns)
    if position == 0:
        return 0
    if position == len(times_ns):
        return position - 1
    if target_ns - times_ns[position - 1] <= times_ns[position] - target_ns:
        return position - 1
    return position


def replay_sparse(times_ns, currents, phase_count, reference_mean):
    phase_means, readings, displays = [], [], []
    updates_per_phase = []
    for phase in range(phase_count):
        tick = phase * REFRESH_NS // phase_count
        phase_readings, phase_displays = [], []
        ema = None
        while tick <= times_ns[-1]:
            start = nearest_index(times_ns, tick)
            # Do not invent a partial 32-sample read at the file's end.
            if start + 32 > len(currents):
                break
            mean = statistics.fmean(currents[start:start + 32])
            ema = mean if ema is None else ema + (mean - ema) / 16.0
            phase_readings.append(mean)
            phase_displays.append(ema)
            tick += REFRESH_NS
        if phase_readings:
            phase_means.append(statistics.fmean(phase_readings))
            readings.extend(phase_readings)
            displays.extend(phase_displays)
            updates_per_phase.append(len(phase_readings))
    return {
        "phase_count": phase_count,
        "samples_per_read": 32,
        "period_ms": REFRESH_NS // 1_000_000,
        "ema_weight": "1/16",
        "updates_per_phase_min": min(updates_per_phase, default=0),
        "updates_per_phase_max": max(updates_per_phase, default=0),
        "burst_readings": summarize(readings, reference_mean),
        "phase_recording_means": summarize(phase_means, reference_mean),
        "ema_outputs_all_phases": summarize(displays, reference_mean),
    }


def pulse_summary(times_ns, currents):
    median = statistics.median(currents)
    mad = statistics.median(abs(value - median) for value in currents)
    threshold = median + 6.0 * 1.4826 * mad
    above = [index for index, value in enumerate(currents) if value > threshold]
    groups = []
    for index in above:
        if not groups or times_ns[index] - times_ns[groups[-1][-1]] > 1_000_000:
            groups.append([])
        groups[-1].append(index)
    # Isolated points are counted, while the event list shows sustained pulses.
    sustained = [group for group in groups if len(group) >= 5]
    return {
        "threshold_rule": "median + 6 * 1.4826 * MAD",
        "threshold_uA": rounded(threshold),
        "samples_above_threshold": len(above),
        "sustained_cluster_count": len(sustained),
        "cluster_join_gap_ms": 1,
        "sustained_cluster_min_samples": 5,
        "sustained_cluster_start_s": [
            rounded(times_ns[group[0]] / 1_000_000_000) for group in sustained
        ],
        "all_samples_including_pulses_are_kept": True,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--phases", type=int, default=400,
                        help="old sparse-sampling initial phases (at least 16)")
    args = parser.parse_args()
    if args.phases < 16:
        parser.error("--phases must be at least 16")
    try:
        times_ns, currents, range_ohm = read_capture(args.csv)
        reference_mean = statistics.fmean(currents)
        ordered = sorted(currents)
        intervals = [later - earlier for earlier, later in zip(times_ns, times_ns[1:])]
        integral = math.fsum(
            (first + second) * interval / 2.0
            for first, second, interval in zip(currents, currents[1:], intervals)
        )
        outputs, refresh_count, warmup_count = replay_continuous(times_ns, currents)
        result = {
            "source": {
                "path": str(args.csv.resolve()),
                "sha256": hashlib.sha256(args.csv.read_bytes()).hexdigest(),
                "samples": len(currents),
                "duration_s": rounded(times_ns[-1] / 1_000_000_000),
                "range_ohm": range_ohm,
                "median_interval_us": rounded(statistics.median(intervals) / 1_000),
                "min_interval_us": rounded(min(intervals) / 1_000),
                "max_interval_us": rounded(max(intervals) / 1_000),
                "mean_uA": rounded(reference_mean),
                "time_integral_mean_uA": rounded(integral / times_ns[-1]),
                "median_uA": rounded(statistics.median(currents)),
                "min_uA": rounded(ordered[0]),
                "max_uA": rounded(ordered[-1]),
                "p99_uA": rounded(quantile(ordered, 0.99)),
            },
            "pulses": pulse_summary(times_ns, currents),
            "continuous": {
                "bucket_ms": BUCKET_NS // 1_000_000,
                "completed_buckets": WINDOW_BUCKETS,
                "current_bucket_excluded": True,
                "refresh_ms": REFRESH_NS // 1_000_000,
                "refresh_phase_ms": 0,
                "max_sample_age_ms": MAX_SAMPLE_AGE_NS // 1_000_000,
                "getter_closes_ended_bucket": True,
                "terminal_getter_allowance_us": rounded(
                    statistics.median(intervals) * 2 / 1_000
                ),
                "last_getter_after_recorded_end_us": rounded(max(
                    0.0,
                    (outputs[-1]["time_s"] * 1_000_000_000 - times_ns[-1]) / 1_000,
                )) if outputs else None,
                "ema_weight": "1/4",
                "display_deadband_uA": DISPLAY_DEADBAND_UA,
                "refresh_count": refresh_count,
                "warmup_refresh_count": warmup_count,
                "display_updates_including_initial": sum(
                    entry["display_changed"] for entry in outputs
                ),
                "display_outputs": summarize(
                    [entry["display_uA"] for entry in outputs], reference_mean
                ),
                "outputs": outputs,
            },
            "old_sparse": replay_sparse(times_ns, currents, args.phases, reference_mean),
            "limitations": [
                "One finite recording; no looping or periodic extension is performed.",
                "Full-window output count is limited by two-second warmup and file end.",
                "Terminal getter may run at most two median sample intervals after file end; no samples are added.",
                "One full-window output cannot establish long-term display stability.",
                "Uses CSV sample times and floats, not firmware integer math or real CPU timing.",
                "Refresh phase is zero relative to the first sample; actual UI phase may differ.",
                "Does not establish pulse origin, long-term stability, or hardware accuracy.",
            ],
        }
        print(json.dumps(result, separators=(",", ":"), allow_nan=False))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"replay error: {error}\n")


if __name__ == "__main__":
    main()
