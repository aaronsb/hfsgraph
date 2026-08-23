#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate a synthetic directory tree for driver benchmarks.

    tests/driver/make_synth.py OUT_DIR [--depth 5] [--seed 7]

Deterministic for a given seed: 3–9 subdirs per dir, 3–25 empty files per dir with
a spread of extensions, to the given depth (depth 5 ≈ 6.5k dirs). Files are empty,
so the tree is cheap to create and the Bytes metric degenerates — use Count.
"""
import argparse
import os
import random

EXTS = ["cpp", "h", "py", "md", "json", "png", "txt", "rs", "o", "toml"]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--depth", type=int, default=5)
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()
    random.seed(args.seed)
    count = 0

    def mk(d: str, depth: int) -> None:
        nonlocal count
        os.makedirs(d, exist_ok=True)
        for i in range(random.randint(3, 25)):
            open(os.path.join(d, f"f{i}.{random.choice(EXTS)}"), "w").close()
        if depth < args.depth:
            for j in range(random.randint(3, 9)):
                count += 1
                mk(os.path.join(d, f"d{depth}_{j}"), depth + 1)

    mk(args.out, 0)
    print(f"{count} dirs under {args.out}")


if __name__ == "__main__":
    main()
