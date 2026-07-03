#!/usr/bin/env python3
"""Per-process totals and top-N self-weight shares from `xperf -a profile -detail` output.

Usage: python profile_shares.py <profile.txt> [--process NAME]... [--top N]
Without --process, all processes except Idle/System/Unknown are shown, sorted by total weight.
Weight units are microseconds of sampled CPU; percentages are shares of that process's own total.
"""
import argparse
import re


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument('profile_txt')
	ap.add_argument('--process', action='append', help='process-name substring filter (repeatable)')
	ap.add_argument('--top', type=int, default=40, help='rows per process (default 40)')
	args = ap.parse_args()

	rows = {}
	for line in open(args.profile_txt, errors='ignore'):
		m = re.match(r'\s*(.+?) \(\s*(-?\d+)\),\s*(\d+),\s*[\d.]+,\s*(.+)$', line)
		if not m:
			continue
		name, pid, weight, func = m.group(1), m.group(2), int(m.group(3)), m.group(4).strip()
		rows.setdefault(f'{name} ({pid})', []).append((weight, func))

	def selected(key):
		if args.process:
			return any(p.lower() in key.lower() for p in args.process)
		# xperf quotes the Unknown pseudo-process name; lstrip makes the filter robust either way.
		return not key.lstrip('"').startswith(('Idle', 'System', 'Unknown'))

	processes = [(sum(w for w, _ in entries), key, entries) for key, entries in rows.items() if selected(key)]
	processes.sort(reverse=True)
	for total, key, entries in processes:
		if total == 0:
			continue
		entries.sort(reverse=True)
		print(f'=== {key}: total weight {total / 1e6:.1f}M ===')
		for weight, func in entries[:args.top]:
			print(f'{100 * weight / total:6.2f}%  {weight:>10}  {func[:150]}')
		print()


if __name__ == '__main__':
	main()
