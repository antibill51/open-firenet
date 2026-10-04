#!/usr/bin/env python3
import sys
import re
import os
from datetime import datetime

log_path = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/of.log')

if not os.path.exists(log_path):
    print(f"Log file {log_path} not found.")
    sys.exit(1)

sys_records = []
hb_records = []
wd_events = []
restarts = []

with open(log_path, 'r', errors='ignore') as f:
    for line_num, line in enumerate(f):
        if '[wd]' in line:
            wd_events.append((line_num, line.strip()))
        if '=== Open-Firenet' in line or '=== Open Firenet' in line or 'rst:0x' in line:
            restarts.append((line_num, line.strip()))
        
        m_sys = re.search(r'\[sys\] uptime=(\d+)s heap=(\d+) maxblock=(\d+) rxAge=(\d+)ms', line)
        if m_sys:
            uptime = int(m_sys.group(1))
            heap = int(m_sys.group(2))
            maxblock = int(m_sys.group(3))
            rx_age = int(m_sys.group(4))
            sys_records.append((uptime, heap, maxblock, rx_age))
            
        m_hb = re.search(r'\[hb\] ack=(\d+) gen=(\d+) in=(\d+) out=(\d+) rev=(\d+) sensors=(\d+) controls=(\d+) rssi=(-?\d+)', line)
        if m_hb:
            hb_records.append({
                'ack': int(m_hb.group(1)),
                'in': int(m_hb.group(3)),
                'out': int(m_hb.group(4)),
                'sensors': int(m_hb.group(6)),
                'controls': int(m_hb.group(7)),
                'rssi': int(m_hb.group(8))
            })

file_size_mb = os.path.getsize(log_path) / (1024 * 1024)
print(f"=== Open Firenet Log Analysis: {log_path} ({file_size_mb:.2f} MB) ===")

if not sys_records:
    print("No [sys] records found in log yet.")
    sys.exit(0)

start_uptime, start_heap, start_maxblock, _ = sys_records[0]
end_uptime, end_heap, end_maxblock, last_rx_age = sys_records[-1]
duration_min = (end_uptime - start_uptime) / 60.0
duration_h = duration_min / 60.0

heaps = [r[1] for r in sys_records]
maxblocks = [r[2] for r in sys_records]
rx_ages = [r[3] for r in sys_records]

# Baseline heap over last 10 samples to filter out transient HTTP/TLS socket buffers
recent_baseline_heap = sum(heaps[-10:]) / min(10, len(heaps))
start_baseline_heap = sum(heaps[:10]) / min(10, len(heaps))
delta_heap = recent_baseline_heap - start_baseline_heap
leak_rate_per_hour = (delta_heap / duration_h) if duration_h > 0.05 else 0

print(f"• Uptime: {start_uptime}s -> {end_uptime}s ({duration_min:.1f} min / {duration_h:.2f} h)")
print(f"• Samples: {len(sys_records)} health checks")
print(f"• Heap: start_baseline={start_baseline_heap:.0f} B | current_instant={end_heap} B | recent_baseline={recent_baseline_heap:.0f} B | min={min(heaps)} B | max={max(heaps)} B")
print(f"  Baseline Delta: {delta_heap:+.0f} B ({leak_rate_per_hour:+.1f} B/h)")
print(f"• Max Contiguous Block: start={start_maxblock} B | now={end_maxblock} B | min={min(maxblocks)} B")

stalls_5s = [r for r in sys_records if r[3] > 5000]
stalls_15s = [r for r in sys_records if r[3] > 15000]

print(f"• RX Responsiveness:")
print(f"  Current rxAge: {last_rx_age} ms")
print(f"  Max rxAge: {max(rx_ages)} ms ({max(rx_ages)/1000.0:.2f} s)")
print(f"  Stalls > 5s: {len(stalls_5s)} | Stalls > 15s: {len(stalls_15s)}")

if hb_records:
    last_hb = hb_records[-1]
    print(f"• CDC Traffic: in={last_hb['in']} packets | out={last_hb['out']} packets | RSSI={last_hb['rssi']} dBm")

print(f"• Watchdog triggers: {len(wd_events)}")
for w in wd_events:
    print(f"  {w[1]}")

print(f"• Reboots detected: {len(restarts)}")

# Diagnosis
print("\n--- DIAGNOSTIC ---")
status = "EXCELLENT"
alerts = []

if leak_rate_per_hour < -2000:
    status = "WARNING"
    alerts.append(f"Possible memory leak detected: {leak_rate_per_hour:.0f} B/h")

if min(heaps) < 40000:
    status = "CRITICAL"
    alerts.append(f"Low memory reached: {min(heaps)} B")

if len(wd_events) > 0:
    status = "WARNING"
    alerts.append(f"Watchdog triggered {len(wd_events)} time(s)")

if stalls_15s:
    status = "WARNING"
    alerts.append(f"{len(stalls_15s)} CDC communication stall(s) > 15s")

if alerts:
    print(f"Status: {status}")
    for a in alerts:
        print(f"  ⚠️  {a}")
else:
    print(f"Status: ✅ {status} — Flat heap (no leak), stable memory, healthy CDC link.")
