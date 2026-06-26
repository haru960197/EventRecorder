#!/usr/bin/env python3
"""plot_event_rate.py – Event Rate Time Series Analysis

Reads a CSV event file in the format produced by metavision_evt3_raw_file_decoder:

    %geometry:width,height
    x,y,polarity,timestamp
    ...

Calculates the number of events per time window (default: 100 ms) and plots
the event rate over time as a line graph.

By default, if the recording is ~15 seconds, ~40 seconds or ~90 seconds long
(matching the event_recorder duration), the script automatically highlights
the three dynamic masking phases:
  - For ~15s recording:
    - Phase 1 (0–5s): Mask OFF (Normal state)
    - Phase 2 (5–10s): Mask ON (Hot pixel mask applied)
    - Phase 3 (10–15s): Mask OFF (Mask cleared)
  - For ~40s recording:
    - Phase 1 (0–5s): Mask OFF (Normal state)
    - Phase 2 (5–35s): Mask ON (Hot pixel mask applied)
    - Phase 3 (35–40s): Mask OFF (Mask cleared)
  - For ~90s recording:
    - Phase 1 (0–30s): Mask OFF (Normal state)
    - Phase 2 (30–60s): Mask ON (Hot pixel mask applied)
    - Phase 3 (60–90s): Mask OFF (Mask cleared)

Usage:
    python3 scripts/plot_event_rate.py events.csv -o event_rate.png
    python3 scripts/plot_event_rate.py events.csv -d 50 -o event_rate.png
    python3 scripts/plot_event_rate.py events.csv --draw-phases true -o event_rate.png
"""

from __future__ import annotations
import argparse
import sys
from pathlib import Path

# Use headless backend to avoid Tkinter/X11 issues when running over SSH
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np


# ---------------------------------------------------------------------------
# Style Settings
# ---------------------------------------------------------------------------
def _apply_style() -> None:
    """Apply a clean, publication-ready style matching visualize_sync.py."""
    plt.rcParams.update({
        "figure.figsize": (8, 6),
        "figure.dpi": 150,
        "axes.grid": True,
        "axes.linewidth": 0.8,
        "grid.alpha": 0.5,
        "grid.linewidth": 0.5,
        "grid.linestyle": "--",
        "lines.linewidth": 1.5,
        "savefig.bbox": "tight",
        "savefig.pad_inches": 0.05,
    })


# ---------------------------------------------------------------------------
# CSV Parsing
# ---------------------------------------------------------------------------
def _parse_geometry(line: str) -> tuple[int, int] | None:
    """Parse a geometry header line such as '%geometry:1280,720'.

    Returns (width, height) or None if the line does not match.
    """
    line = line.strip()
    if not line.startswith("%geometry:"):
        return None
    try:
        parts = line[len("%geometry:"):].split(",")
        return int(parts[0]), int(parts[1])
    except (ValueError, IndexError):
        return None


def load_csv(input_path: Path) -> tuple[np.ndarray, tuple[int, int]]:
    """Load a CSV event file and return a structured numpy array plus sensor size.

    The CSV format is:
        %geometry:width,height   ← optional header
        x,y,polarity,timestamp   ← one event per line (timestamp in µs)

    Lines beginning with '%' are treated as comments/metadata.

    Parameters
    ----------
    input_path : Path
        Path to the CSV file.

    Returns
    -------
    events : np.ndarray
        Structured array with fields 'x', 'y', 'p', 't'.
    sensor_size : tuple[int, int]
        (width, height) parsed from %geometry header, or (0, 0) if absent.
    """
    print(f"[INFO] Opening file: {input_path}")

    sensor_size = (0, 0)
    data_rows: list[str] = []

    with open(input_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("%"):
                geom = _parse_geometry(line)
                if geom is not None:
                    sensor_size = geom
                    print(f"[INFO] Sensor resolution: {sensor_size[0]} x {sensor_size[1]}")
                continue
            data_rows.append(line)

    if not data_rows:
        return np.array([]), sensor_size

    # Parse all event rows at once with numpy for speed
    # Each row: x, y, polarity, timestamp
    import io
    raw = np.loadtxt(io.StringIO("\n".join(data_rows)), delimiter=",", dtype=np.int64)

    # Handle edge case of a single event (1-D array)
    if raw.ndim == 1:
        raw = raw.reshape(1, -1)

    if raw.shape[1] != 4:
        raise ValueError(
            f"Expected 4 columns (x, y, polarity, timestamp) but got {raw.shape[1]}."
        )

    dtype = np.dtype([("x", np.int32), ("y", np.int32), ("p", np.int8), ("t", np.int64)])
    events = np.empty(len(raw), dtype=dtype)
    events["x"] = raw[:, 0]
    events["y"] = raw[:, 1]
    events["p"] = raw[:, 2]
    events["t"] = raw[:, 3]

    print(f"[INFO] Loaded {len(events):,} events.")
    print(f"[INFO] Time range: {events['t'][0]} µs → {events['t'][-1]} µs "
          f"({(events['t'][-1] - events['t'][0]) / 1_000_000:.3f} s)")

    return events, sensor_size


# ---------------------------------------------------------------------------
# Event Rate Calculation
# ---------------------------------------------------------------------------
def analyze_event_rate(
    events: np.ndarray,
    delta_t_ms: float,
) -> tuple[np.ndarray, np.ndarray]:
    """Bin events into fixed time windows and count events per bin.

    Parameters
    ----------
    events : np.ndarray
        Structured array with field 't' (timestamps in µs).
    delta_t_ms : float
        Bin size in milliseconds.

    Returns
    -------
    times : np.ndarray
        Bin center times in seconds.
    counts : np.ndarray
        Number of events in each bin.
    """
    delta_t_us = delta_t_ms * 1000.0

    t = events["t"]
    t_min = float(t[0])
    t_max = float(t[-1])
    duration_us = t_max - t_min

    # Build bin edges starting from the first event timestamp
    n_bins = max(1, int(np.ceil(duration_us / delta_t_us)))
    bin_edges = t_min + np.arange(n_bins + 1) * delta_t_us

    counts, _ = np.histogram(t, bins=bin_edges)

    # Bin centers in seconds (offset from t=0 for a clean x-axis)
    bin_centers_sec = (bin_edges[:-1] + delta_t_us / 2.0 - t_min) / 1_000_000.0

    print(f"[INFO] Binned into {n_bins} × {delta_t_ms:.0f} ms windows.")
    print(f"[INFO] Total events: {counts.sum():,}")
    print(f"[INFO] Peak events/bin: {counts.max():,}")
    print(f"[INFO] Average events/bin: {counts.mean():.1f}")

    return bin_centers_sec, counts


# ---------------------------------------------------------------------------
# Plotting
# ---------------------------------------------------------------------------
def plot_event_rate(
    times: np.ndarray,
    counts: np.ndarray,
    output_path: Path,
    delta_t_ms: float,
    sensor_size: tuple[int, int],
    input_name: str,
    draw_phases: str = "auto",
    title: str | None = None,
    dpi: int = 300,
) -> None:
    """Generate and save the event rate line plot.

    Parameters
    ----------
    times : np.ndarray
        Bin center times in seconds.
    counts : np.ndarray
        Number of events per bin.
    output_path : Path
        Output PNG path.
    delta_t_ms : float
        Bin size in milliseconds (used for axis label).
    sensor_size : tuple[int, int]
        (width, height) for the subtitle.
    input_name : str
        Source filename for the subtitle.
    draw_phases : str
        'auto', 'true', or 'false'.
    title : str, optional
        Main plot title.
    dpi : int
        Output image resolution.
    """
    _apply_style()

    fig, ax = plt.subplots(figsize=(8, 6))

    # Main line + shaded area under curve
    ax.plot(times, counts, color="C0", linewidth=1.5, label="Event Rate", zorder=3)
    ax.fill_between(times, counts, color="C0", alpha=0.10, zorder=2)

    ax.set_xlabel("Time [s]", fontsize=12)
    ax.set_ylabel(f"Event Count (per {delta_t_ms:.0f} ms)", fontsize=12)

    # Y-axis: compact unit suffixes
    def _y_fmt(x, pos):
        if x >= 1e6:
            return f"{x * 1e-6:.1f}M"
        if x >= 1e3:
            return f"{x * 1e-3:.0f}k"
        return str(int(x))

    ax.yaxis.set_major_formatter(ticker.FuncFormatter(_y_fmt))

    # --- Phase shading ---
    max_time = times[-1] if len(times) > 0 else 0.0
    should_draw = False
    p1, p2, p3 = None, None, None

    if draw_phases == "auto":
        if 12.0 <= max_time <= 18.0:
            should_draw = True
            p1, p2, p3 = 5.0, 10.0, 15.0
        elif 35.0 <= max_time <= 45.0:
            should_draw = True
            p1, p2, p3 = 5.0, 35.0, 40.0
        elif 80.0 <= max_time <= 100.0:
            should_draw = True
            p1, p2, p3 = 30.0, 60.0, 90.0
    elif draw_phases == "true":
        should_draw = True
        if max_time > 80.0:
            p1, p2, p3 = 30.0, 60.0, 90.0
        elif max_time > 35.0:
            p1, p2, p3 = 5.0, 35.0, 40.0
        else:
            p1, p2, p3 = 5.0, 10.0, 15.0

    if should_draw and p1 is not None and p2 is not None and p3 is not None:
        print(f"[INFO] Adding shaded regions for 3-phase masking (0-{p1:.0f}s / {p1:.0f}-{p2:.0f}s / {p2:.0f}-{p3:.0f}s).")
        ax.axvspan(0,  p1,              color="#EE6677", alpha=0.08,
                   label="Phase 1: Normal (Mask OFF)")
        ax.axvspan(p1,  p2,             color="#228833", alpha=0.08,
                   label="Phase 2: Active (Mask ON)")
        ax.axvspan(p2, min(p3, max_time), color="#CCBB44", alpha=0.08,
                   label="Phase 3: Normal (Mask OFF)")

        ax.axvline(x=p1,  color="#666666", linestyle="--", linewidth=1.0, alpha=0.6, zorder=4)
        ax.axvline(x=p2, color="#666666", linestyle="--", linewidth=1.0, alpha=0.6, zorder=4)

        y_top = ax.get_ylim()[1]
        text_y = y_top * 0.90
        for xc, label in [(p1 / 2.0, "Phase 1\nMask OFF"),
                          ((p1 + p2) / 2.0, "Phase 2\nMask ON"),
                          ((p2 + p3) / 2.0, "Phase 3\nMask OFF")]:
            ax.text(xc, text_y, label, ha="center", va="center",
                    fontsize=10, color="#555555", weight="bold")

        ax.set_xlim(0, max(p3, max_time))

    # --- Titles ---
    if not title:
        title = "Event Rate Time Series"

    w, h = sensor_size
    res_str = f"{w}x{h}" if w and h else "unknown"
    ax.set_title(f"{title}", fontsize=13)

    ax.grid(True, linestyle="--", alpha=0.5, linewidth=0.5, zorder=1)
    ax.legend(fontsize=10, framealpha=0.8)

    # --- Save ---
    output_path.parent.mkdir(parents=True, exist_ok=True)
    plt.tight_layout()
    fig.savefig(output_path, dpi=dpi, bbox_inches="tight")
    plt.close(fig)
    print(f"[OK] Plot saved to: {output_path}")


# ---------------------------------------------------------------------------
# CLI Entry Point
# ---------------------------------------------------------------------------
def main() -> None:
    parser = argparse.ArgumentParser(
        description="Plot event rate time series from a CSV event file.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
        epilog="""\
CSV format (produced by metavision_evt3_raw_file_decoder):
  %%geometry:width,height
  x,y,polarity,timestamp_us
  ...
""",
    )
    parser.add_argument(
        "input",
        nargs="?",
        default="events.csv",
        help="Path to the input CSV event file.",
    )
    parser.add_argument(
        "-o", "--output",
        default="event_rate.png",
        help="Path to save the output PNG plot.",
    )
    parser.add_argument(
        "-d", "--delta-t",
        type=float,
        default=100.0,
        metavar="MS",
        help="Bin size in milliseconds.",
    )
    parser.add_argument(
        "--draw-phases",
        choices=["auto", "true", "false"],
        default="auto",
        help=(
            "Overlay 3-phase masking regions. "
            "'auto' draws them when file duration is ~15 s, ~40 s, or ~90 s."
        ),
    )
    parser.add_argument(
        "--title",
        help="Custom main title for the plot.",
    )
    parser.add_argument(
        "--dpi",
        type=int,
        default=300,
        help="Output image DPI.",
    )

    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)

    if not input_path.exists():
        print(f"[ERROR] File not found: {input_path}", file=sys.stderr)
        sys.exit(1)

    # Load CSV
    events, sensor_size = load_csv(input_path)

    if len(events) == 0:
        print("[ERROR] No events parsed from file. Exiting.", file=sys.stderr)
        sys.exit(1)

    # Bin into time windows
    times, counts = analyze_event_rate(events, args.delta_t)

    # Plot and save
    plot_event_rate(
        times=times,
        counts=counts,
        output_path=output_path,
        delta_t_ms=args.delta_t,
        sensor_size=sensor_size,
        input_name=input_path.name,
        draw_phases=args.draw_phases,
        title=args.title,
        dpi=args.dpi,
    )


if __name__ == "__main__":
    main()
