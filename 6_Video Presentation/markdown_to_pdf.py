from __future__ import annotations

import argparse
import html
import re
import shutil
import subprocess
from pathlib import Path


HEADING_RE = re.compile(r"^(#{1,6})\s+(.*)$")
LIST_RE = re.compile(r"^-\s+(.*)$")
TABLE_SEPARATOR_RE = re.compile(r"^\s*\|(?:\s*:?-+:?\s*\|)+\s*$")

EDGE_CANDIDATES = [
    shutil.which("msedge"),
    Path(r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"),
    Path(r"C:\Program Files\Microsoft\Edge\Application\msedge.exe"),
]


def find_edge() -> Path:
    for candidate in EDGE_CANDIDATES:
        if not candidate:
            continue
        path = Path(candidate)
        if path.exists():
            return path
    raise SystemExit("Microsoft Edge was not found. Install Edge or update EDGE_CANDIDATES in markdown_to_pdf.py.")


def format_inline(text: str) -> str:
    parts = text.split("`")
    rendered: list[str] = []
    for index, part in enumerate(parts):
        escaped = html.escape(part)
        if index % 2 == 1:
            rendered.append(f"<code>{escaped}</code>")
        else:
            rendered.append(escaped)
    return "".join(rendered)


def split_table_row(row: str) -> list[str]:
    return [cell.strip() for cell in row.strip().strip("|").split("|")]


def render_blocks(markdown_text: str) -> str:
    lines = markdown_text.splitlines()
    blocks: list[str] = []
    i = 0

    while i < len(lines):
        line = lines[i].rstrip()
        stripped = line.strip()

        if not stripped:
            i += 1
            continue

        heading = HEADING_RE.match(stripped)
        if heading:
            level = len(heading.group(1))
            text = format_inline(heading.group(2).strip())
            blocks.append(f"<h{level}>{text}</h{level}>")
            i += 1
            continue

        if stripped.startswith("|") and i + 1 < len(lines) and TABLE_SEPARATOR_RE.match(lines[i + 1].strip()):
            header_cells = split_table_row(stripped)
            rows: list[list[str]] = []
            i += 2
            while i < len(lines) and lines[i].strip().startswith("|"):
                rows.append(split_table_row(lines[i]))
                i += 1

            thead = "".join(f"<th>{format_inline(cell)}</th>" for cell in header_cells)
            tbody_parts: list[str] = []
            for row in rows:
                row_cells = "".join(f"<td>{format_inline(cell)}</td>" for cell in row)
                tbody_parts.append(f"<tr>{row_cells}</tr>")
            blocks.append(f"<table><thead><tr>{thead}</tr></thead><tbody>{''.join(tbody_parts)}</tbody></table>")
            continue

        list_item = LIST_RE.match(stripped)
        if list_item:
            items: list[str] = []
            while i < len(lines):
                current = lines[i].strip()
                match = LIST_RE.match(current)
                if not match:
                    break
                items.append(f"<li>{format_inline(match.group(1).strip())}</li>")
                i += 1
            blocks.append(f"<ul>{''.join(items)}</ul>")
            continue

        paragraph_lines = [stripped]
        i += 1
        while i < len(lines):
            lookahead = lines[i].strip()
            if not lookahead:
                break
            if HEADING_RE.match(lookahead) or LIST_RE.match(lookahead):
                break
            if lookahead.startswith("|") and i + 1 < len(lines) and TABLE_SEPARATOR_RE.match(lines[i + 1].strip()):
                break
            paragraph_lines.append(lookahead)
            i += 1
        blocks.append(f"<p>{format_inline(' '.join(paragraph_lines))}</p>")

    return "\n".join(blocks)


def render_document(markdown_path: Path, body_html: str) -> str:
    base_href = markdown_path.parent.resolve().as_uri().rstrip("/") + "/"
    title = html.escape(markdown_path.stem)
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{title}</title>
  <base href="{html.escape(base_href)}">
  <style>
    @page {{
      size: A4;
      margin: 16mm 14mm 16mm 14mm;
    }}

    :root {{
      color-scheme: light;
      --ink: #18222d;
      --muted: #4a5a6a;
      --border: #b8c8d8;
      --panel: #eef4fa;
      --panel-alt: #f8fbfe;
      --accent: #0d5e86;
    }}

    * {{
      box-sizing: border-box;
    }}

    body {{
      margin: 0;
      color: var(--ink);
      font-family: "Segoe UI", Calibri, Arial, sans-serif;
      font-size: 11pt;
      line-height: 1.45;
    }}

    h1, h2, h3, h4, h5, h6 {{
      margin: 0 0 0.45em;
      color: var(--accent);
      line-height: 1.2;
      page-break-after: avoid;
    }}

    h1 {{
      font-size: 22pt;
      margin-top: 0;
      padding-bottom: 0.2em;
      border-bottom: 2px solid var(--accent);
    }}

    h2 {{
      font-size: 15pt;
      margin-top: 1.35em;
    }}

    p {{
      margin: 0.3em 0 0.85em;
    }}

    ul {{
      margin: 0.35em 0 0.95em 1.2em;
      padding: 0;
    }}

    li {{
      margin: 0.2em 0;
    }}

    code {{
      font-family: Consolas, "Courier New", monospace;
      font-size: 0.95em;
      background: #edf1f5;
      padding: 0.08em 0.32em;
      border-radius: 3px;
    }}

    table {{
      width: 100%;
      border-collapse: collapse;
      margin: 0.8em 0 1.15em;
      table-layout: fixed;
      page-break-inside: auto;
    }}

    thead {{
      display: table-header-group;
    }}

    tr {{
      page-break-inside: avoid;
      break-inside: avoid;
    }}

    th, td {{
      border: 1px solid var(--border);
      padding: 8px 10px;
      text-align: left;
      vertical-align: top;
      overflow-wrap: anywhere;
    }}

    th {{
      background: var(--panel);
      font-weight: 700;
    }}

    tbody tr:nth-child(even) td {{
      background: var(--panel-alt);
    }}

    th:nth-child(1), td:nth-child(1) {{
      width: 50%;
    }}

    th:nth-child(2), td:nth-child(2) {{
      width: 38%;
    }}

    th:nth-child(3), td:nth-child(3) {{
      width: 12%;
      white-space: nowrap;
    }}
  </style>
</head>
<body>
{body_html}
</body>
</html>
"""


def print_to_pdf(markdown_path: Path, output_pdf: Path) -> None:
    body_html = render_blocks(markdown_path.read_text(encoding="utf-8"))
    document_html = render_document(markdown_path, body_html)
    edge_path = find_edge()

    build_dir = output_pdf.parent / ".pdf_build"
    build_dir.mkdir(exist_ok=True)
    html_path = build_dir / f"{markdown_path.stem}.html"
    profile_dir = build_dir / "edge-profile"
    profile_dir.mkdir(exist_ok=True)
    html_path.write_text(document_html, encoding="utf-8")

    command = [
        str(edge_path),
        "--headless=new",
        "--disable-gpu",
        "--no-pdf-header-footer",
        f"--user-data-dir={profile_dir}",
        f"--print-to-pdf={output_pdf.resolve()}",
        html_path.resolve().as_uri(),
    ]
    subprocess.run(command, check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="Render a Markdown file to PDF using a minimal built-in HTML renderer and Microsoft Edge.")
    parser.add_argument("markdown_file", help="Path to the Markdown file to render.")
    parser.add_argument(
        "output_pdf",
        nargs="?",
        help="Optional PDF output path. Defaults to the Markdown filename with a .pdf extension.",
    )
    args = parser.parse_args()

    markdown_path = Path(args.markdown_file).resolve()
    if not markdown_path.exists():
        raise SystemExit(f"Markdown file not found: {markdown_path}")

    output_pdf = Path(args.output_pdf).resolve() if args.output_pdf else markdown_path.with_suffix(".pdf")
    print_to_pdf(markdown_path, output_pdf)
    print(f"Generated PDF: {output_pdf}")


if __name__ == "__main__":
    main()
