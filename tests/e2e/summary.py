#!/usr/bin/env python3
"""Prints the end-to-end results as a Markdown table (for $GITHUB_STEP_SUMMARY)."""
import json
import sys

d = json.load(open(sys.argv[1], encoding="utf-8"))
print(f"### {d['label']} (plugin {d['pluginVersion']})\n")
print("| check | result | detail |\n|---|---|---|")
for name, r in d["results"].items():
    print(f"| {name} | {'PASS' if r['ok'] else 'FAIL'} | {r['detail']} |")
