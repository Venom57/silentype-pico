#!/usr/bin/env python3
"""Fail the build on any DRC error or unconnected item; summarise warnings."""
import collections, json, sys
d = json.load(open(sys.argv[1]))
errs = [v for v in d["violations"] if v["severity"] == "error"]
unc = d.get("unconnected_items", [])
warn = collections.Counter(v["type"] for v in d["violations"] if v["severity"] == "warning")
print(f"DRC: {len(errs)} errors, {len(unc)} unconnected, warnings: {dict(warn) or 'none'}")
for v in errs:
    print("  ERROR", v["type"], "|", " | ".join(i["description"] for i in v["items"]))
sys.exit(1 if errs or unc else 0)
