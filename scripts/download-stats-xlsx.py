"""Build downloads.xlsx from the daily snapshots that download-stats.ps1 appends to downloads.csv.

One Overview sheet (every app on one chart) plus one sheet per app (table + chart). Each chart is tagged with the
GitHub release dates that fall inside the recorded range, as dashed vertical lines named by release tag.
Usage: python download-stats-xlsx.py <stats-folder> [owner/repo]
"""
import csv
import datetime as dt
import json
import subprocess
import sys
from pathlib import Path

from openpyxl import Workbook
from openpyxl.chart import ScatterChart, Reference, Series
from openpyxl.chart.label import DataLabel, DataLabelList
from openpyxl.chart.shapes import GraphicalProperties
from openpyxl.styles import Alignment, Font, PatternFill
from openpyxl.utils import get_column_letter

out_dir = Path(sys.argv[1])
repo = sys.argv[2] if len(sys.argv) > 2 else "AndyTuke/mu-family"
csv_path = out_dir / "downloads.csv"
xlsx_path = out_dir / "downloads.xlsx"

# Phase 1: snapshots -> {app: {date: downloads}}, keeping the LAST row of each day (a day can have several runs).
history = {}
with open(csv_path, newline="", encoding="utf-8-sig") as f:
    for row in csv.DictReader(f):
        day = dt.date.fromisoformat(row["date"])
        history.setdefault(row["product"], {})[day] = int(row["downloads"])
if not history:
    sys.exit("downloads.csv has no rows")
all_days = sorted({d for series in history.values() for d in series})
first_day, last_day = all_days[0], all_days[-1]

# Phase 2: release dates (family releases ship every app at once, so one list serves every chart).
raw = subprocess.run(["gh", "api", "--paginate", f"repos/{repo}/releases", "--jq",
                      ".[] | [.tag_name, .published_at] | @tsv"], capture_output=True, text=True, check=True).stdout
releases = []
for line in raw.splitlines():
    tag, published = line.split("\t")
    releases.append((tag, dt.date.fromisoformat(published[:10])))
releases.sort(key=lambda r: r[1])
in_range = [(t, d) for t, d in releases if first_day <= d <= last_day]

# Phase 3: workbook.
wb = Workbook()
head_fill = PatternFill("solid", fgColor="2B2B2B")
head_font = Font(bold=True, color="FFFFFF")
palette = ["4E79A7", "F28E2B", "59A14F", "E15759", "76B7B2", "EDC948", "B07AA1", "FF9DA7", "9C755F", "BAB0AC"]


def style_header(ws, ncols):
    for c in range(1, ncols + 1):
        cell = ws.cell(row=1, column=c)
        cell.fill, cell.font = head_fill, head_font
        cell.alignment = Alignment(horizontal="center")
        ws.column_dimensions[get_column_letter(c)].width = 14


def add_release_lines(ws, chart, top, first_free_row, col):
    """Dashed vertical line per in-range release, drawn from a 2-point helper table placed out of the way."""
    r = first_free_row
    for tag, day in in_range:
        ws.cell(row=r, column=col, value=day).number_format = "yyyy-mm-dd"
        ws.cell(row=r, column=col + 1, value=0)
        ws.cell(row=r + 1, column=col, value=day).number_format = "yyyy-mm-dd"
        ws.cell(row=r + 1, column=col + 1, value=top)
        ws.cell(row=r, column=col + 2, value=tag)
        xs = Reference(ws, min_col=col, min_row=r, max_row=r + 1)
        ys = Reference(ws, min_col=col + 1, min_row=r, max_row=r + 1)
        s = Series(ys, xs, title=f"Release {tag}")
        s.marker.symbol = "none"
        s.graphicalProperties = GraphicalProperties()
        s.graphicalProperties.line.solidFill = "888888"
        s.graphicalProperties.line.dashStyle = "dash"
        s.graphicalProperties.line.width = 12700
        label = DataLabel(idx=1, showSerName=True, showVal=False, showCatName=False, showLegendKey=False, dLblPos="t")
        s.dLbls = DataLabelList(dLbl=[label], showVal=False, showSerName=False, showCatName=False, showLegendKey=False)
        chart.series.append(s)
        r += 3


def new_chart(title, width, height):
    ch = ScatterChart()
    ch.title, ch.style, ch.width, ch.height = title, 2, width, height
    ch.x_axis.number_format, ch.x_axis.majorTimeUnit = "d mmm", "days"
    ch.x_axis.delete = ch.y_axis.delete = False
    ch.x_axis.tickLblPos = "low"
    ch.x_axis.majorGridlines = None   # horizontal gridlines only: the date lines would crowd the release markers
    ch.y_axis.majorGridlines.spPr = GraphicalProperties()
    ch.y_axis.majorGridlines.spPr.line.solidFill = "D9D9D9"
    ch.legend.position = "r"
    return ch


def line_series(ws, ref_x, ref_y, name, colour):
    s = Series(ref_y, ref_x, title=name)
    s.marker.symbol = "circle"
    s.marker.size = 6
    s.graphicalProperties = GraphicalProperties()
    s.graphicalProperties.line.solidFill = colour
    s.graphicalProperties.line.width = 22000
    s.marker.graphicalProperties = GraphicalProperties(solidFill=colour)
    s.marker.graphicalProperties.line.solidFill = colour
    s.smooth = False
    return s


apps = sorted(history, key=lambda a: -history[a][last_day] if last_day in history[a] else 0)

# Overview: one row per day, one column per app, plus the chart.
ov = wb.active
ov.title = "Overview"
ov.append(["Date"] + apps + ["All apps"])
style_header(ov, len(apps) + 2)
carry = {}
for day in all_days:
    row = [day]
    total = 0
    for a in apps:
        carry[a] = history[a].get(day, carry.get(a, 0))   # a missing snapshot repeats the last known total
        row.append(carry[a])
        total += carry[a]
    row.append(total)
    ov.append(row)
for r in range(2, ov.max_row + 1):
    ov.cell(row=r, column=1).number_format = "yyyy-mm-dd"
last_row = ov.max_row
chart = new_chart("Downloads per app (cumulative)", 26, 13)
xs = Reference(ov, min_col=1, min_row=2, max_row=last_row)
for i, a in enumerate(apps):
    ys = Reference(ov, min_col=2 + i, min_row=2, max_row=last_row)
    chart.series.append(line_series(ov, xs, ys, a, palette[i % len(palette)]))
helper_col = len(apps) + 5
top = max(carry.values()) * 1.1
add_release_lines(ov, chart, int(top) + 1, 2, helper_col)
ov.cell(row=1, column=helper_col, value="Release markers (helper)").font = Font(italic=True, color="888888")
ov.add_chart(chart, f"A{last_row + 3}")

# One sheet per app: Date, Total, Change since the previous day, Release published that day.
release_on = {}
for tag, day in releases:
    release_on.setdefault(day, []).append(tag)
for a in apps:
    ws = wb.create_sheet(a)
    ws.append(["Date", "Total downloads", "Change", "Release"])
    style_header(ws, 4)
    ws.column_dimensions["D"].width = 22
    prev = None
    run = 0
    for day in all_days:
        run = history[a].get(day, run)
        ws.append([day, run, (run - prev) if prev is not None else None, ", ".join(release_on.get(day, []))])
        ws.cell(row=ws.max_row, column=1).number_format = "yyyy-mm-dd"
        prev = run
    n = ws.max_row
    ch = new_chart(f"{a} downloads (cumulative)", 20, 10)
    ch.series.append(line_series(ws, Reference(ws, min_col=1, min_row=2, max_row=n),
                                 Reference(ws, min_col=2, min_row=2, max_row=n), a, palette[apps.index(a) % len(palette)]))
    add_release_lines(ws, ch, int(max(run, 1) * 1.1) + 1, 2, 7)
    ch.legend = None
    ws.cell(row=1, column=7, value="Release markers (helper)").font = Font(italic=True, color="888888")
    ws.add_chart(ch, f"A{n + 3}")

# Releases list for reference, including those before the first snapshot (not drawn: there is no data to put them against).
rs = wb.create_sheet("Releases")
rs.append(["Release", "Published", "On chart"])
style_header(rs, 3)
for tag, day in reversed(releases):
    rs.append([tag, day, "yes" if first_day <= day <= last_day else "before first snapshot" if day < first_day else "after last snapshot"])
    rs.cell(row=rs.max_row, column=2).number_format = "yyyy-mm-dd"
rs.column_dimensions["C"].width = 24

notes = wb.create_sheet("Notes")
notes.append(["Counts are GitHub release-file download counters, summed over every version of each app (Windows zips). "
              "They include repeat downloads, bots and scanners, and they fall if a release file is deleted or re-uploaded."])
notes.append([f"History starts {first_day:%d %b %Y} (the first snapshot); nothing is back-filled. Last updated {dt.datetime.now():%d %b %Y %H:%M}."])
notes.column_dimensions["A"].width = 120

try:
    wb.save(xlsx_path)
except PermissionError:
    sys.exit(f"{xlsx_path} is open in Excel: close it and run again (the CSV history is safe)")
print(f"wrote {xlsx_path} ({len(apps)} apps, {len(all_days)} days, {len(in_range)} releases on the chart)")
