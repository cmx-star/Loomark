#!/usr/bin/env python3
"""Generate deterministic Markdown samples for local P0 large-file checks."""

from __future__ import annotations

import argparse
from pathlib import Path


CHUNK_TEMPLATE = """# Section {section}

This is a deterministic paragraph for large-file editor testing. It contains
plain Markdown text, a link-like token [example](https://example.invalid), and
enough repeated words to exercise scanning without using private data.

```text
fenced heading should not become outline: # Section {section}
```

"""


def parse_size(value: str) -> int:
    normalized = value.strip().lower()
    units = {"b": 1, "kb": 1024, "mb": 1024 * 1024}
    for suffix, multiplier in sorted(units.items(), key=lambda item: len(item[0]), reverse=True):
        if normalized.endswith(suffix):
            return int(float(normalized[: -len(suffix)]) * multiplier)
    return int(normalized)


def generate(path: Path, target_bytes: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    written = 0
    section = 1
    with path.open("wb") as output:
        while written < target_bytes:
            block = CHUNK_TEMPLATE.format(section=section).encode("utf-8")
            remaining = target_bytes - written
            data = block[:remaining]
            output.write(data)
            written += len(data)
            section += 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--size", default="10mb", help="Target size, for example 10mb, 300mb, 512mb")
    args = parser.parse_args()

    target_bytes = parse_size(args.size)
    generate(args.output, target_bytes)
    print(f"generated {args.output} ({target_bytes} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
