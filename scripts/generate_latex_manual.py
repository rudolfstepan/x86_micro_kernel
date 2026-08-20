#!/usr/bin/env python3
"""Create one LaTeX book from all Markdown files in the repository.

This script scans the repository recursively for Markdown files, orders them by
relative path, and renders a single LaTeX document that can be compiled to PDF
with pdflatex.

Example:
    python scripts/generate_latex_manual.py
    python scripts/generate_latex_manual.py --root . --output build/manual/reist-handbuch.tex --compile
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path


def rel_posix(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def normalize_unicode_for_latex(text: str) -> str:
    replacements = {
        "✅": "[OK]",
        "❌": "[ERROR]",
        "⚠️": "[WARN]",
        "🚧": "[WIP]",
        "📌": "[NOTE]",
        "•": "-",
        "–": "-",
        "—": "--",
        "…": "...",
        "“": '"',
        "”": '"',
        "‘": "'",
        "’": "'",
        "«": "<<",
        "»": ">>",
        "ä": "\\\"a",
        "ö": "\\\"o",
        "ü": "\\\"u",
        "Ä": "\\\"A",
        "Ö": "\\\"O",
        "Ü": "\\\"U",
        "ß": "\\ss{}",
        "é": "\\'e",
        "è": "\\'e",
        "à": "\\`a",
        "ù": "\\`u",
        "ç": "\\c{c}",
        "ñ": "\\~n",
    }
    for old, new in replacements.items():
        text = text.replace(old, new)
    return text


def latex_escape(text: str) -> str:
    text = normalize_unicode_for_latex(text)
    replacements = {
        "\\": r"\textbackslash{}",
        "&": r"\&",
        "%": r"\%",
        "$": r"\$",
        "#": r"\#",
        "_": r"\_",
        "{": r"\{",
        "}": r"\}",
        "~": r"\textasciitilde{}",
        "^": r"\textasciicircum{}",
    }
    for old, new in replacements.items():
        text = text.replace(old, new)
    return text


def sanitize_code_for_latex(text: str) -> str:
    text = normalize_unicode_for_latex(text)
    replacements = {
        "→": "->",
        "←": "<-",
        "⇒": "=>",
        "≤": "<=",
        "≥": ">=",
        "•": "*",
        "–": "-",
        "—": "--",
        "“": '"',
        "”": '"',
        "‘": "'",
        "’": "'",
    }
    for old, new in replacements.items():
        text = text.replace(old, new)
    return text


def first_heading(markdown_text: str) -> str:
    for line in markdown_text.splitlines():
        match = re.match(r"^(#{1,6})\s+(.*?)\s*#*\s*$", line)
        if match:
            heading = re.sub(r"\s+", " ", match.group(2)).strip()
            return heading or "Dokument"
    return "Dokument"


def strip_leading_heading(markdown_text: str) -> str:
    lines = markdown_text.splitlines()
    index = 0
    while index < len(lines) and not lines[index].strip():
        index += 1
    if index < len(lines):
        match = re.match(r"^(#{1,6})\s+(.*?)\s*#*\s*$", lines[index])
        if match:
            lines = lines[index + 1:]
    return "\n".join(lines).lstrip("\n")


def split_markdown_table_row(line: str) -> list[str]:
    row = line.strip()
    if row.startswith("|"):
        row = row[1:]
    if row.endswith("|"):
        row = row[:-1]
    return [cell.strip() for cell in row.split("|")]


def markdown_table_to_latex(lines: list[str], start_index: int) -> tuple[str, int] | tuple[None, int]:
    if start_index + 1 >= len(lines):
        return None, start_index

    first = lines[start_index].strip()
    second = lines[start_index + 1].strip()
    if "|" not in first or "|" not in second:
        return None, start_index

    first_cells = split_markdown_table_row(first)
    second_cells = split_markdown_table_row(second)
    if len(first_cells) < 2 or len(second_cells) < 2:
        return None, start_index

    delimiter_ok = True
    for cell in second_cells:
        cleaned = cell.strip()
        if not re.fullmatch(r":?-{3,}:?", cleaned):
            delimiter_ok = False
            break
    if not delimiter_ok:
        return None, start_index

    end_index = start_index + 2
    while end_index < len(lines):
        candidate = lines[end_index].strip()
        if not candidate or "|" not in candidate:
            break
        if re.match(r"^[-|: ]+$", candidate):
            end_index += 1
            continue
        if candidate.startswith("-") or candidate.startswith("*") or candidate.startswith("#"):
            break
        end_index += 1

    col_count = max(len(first_cells), len(second_cells))
    align = []
    for cell in second_cells[:col_count]:
        text = cell.strip()
        if text.startswith(":") and text.endswith(":"):
            align.append("c")
        elif text.startswith(":"):
            align.append("l")
        elif text.endswith(":"):
            align.append("r")
        else:
            align.append("l")
    while len(align) < col_count:
        align.append("l")

    header = [latex_inline(cell) for cell in first_cells[:col_count]]
    rows: list[list[str]] = []
    all_cells: list[str] = []
    for row_index in range(start_index + 2, end_index):
        cells = split_markdown_table_row(lines[row_index])
        if len(cells) < col_count:
            cells = cells + [""] * (col_count - len(cells))
        elif len(cells) > col_count:
            cells = cells[:col_count]
        cleaned = [latex_inline(cell) for cell in cells]
        rows.append(cleaned)
        all_cells.extend(cleaned)

    max_cell_len = max((len(cell) for cell in all_cells), default=0)
    needs_landscape = col_count > 4 or max_cell_len > 30

    table_lines = [
        "\\toprule",
        " & ".join(header) + " \\\\",
        "\\midrule",
    ]
    for row in rows:
        table_lines.append(" & ".join(row) + " \\\\ ")
    table_lines.append("\\bottomrule")

    body = "\n".join(table_lines)
    latex = ""
    if needs_landscape:
        latex += "\\begin{landscape}\n"
    latex += "\\begin{adjustbox}{max width=\\textwidth, center}\n"
    latex += "\\begin{tabular}{" + " ".join(align) + "}\n"
    latex += body + "\n"
    latex += "\\end{tabular}\n"
    latex += "\\end{adjustbox}\n"
    if needs_landscape:
        latex += "\\end{landscape}\n"
    return latex, end_index


def checkbox_to_latex(line: str) -> str | None:
    match = re.match(r"^\s*(?:[-*])\s*\[(?P<state>[ xX])\]\s*(?P<text>.*)$", line)
    if not match:
        return None

    state = match.group("state").strip().lower()
    content = match.group("text").strip()
    symbol = r"$\checkmark$" if state in {"x"} else r"$\square$"
    return r"\item[" + symbol + r"] " + latex_inline(content)


def valid_latex_listing_language(lang: str) -> str:
    if not lang:
        return ""
    aliases = {
        "bash": "bash",
        "sh": "bash",
        "shell": "bash",
        "zsh": "bash",
        "python": "Python",
        "py": "Python",
        "c": "C",
        "cpp": "C++",
        "c++": "C++",
        "java": "Java",
        "json": "json",
        "yaml": "yaml",
        "yml": "yaml",
        "toml": "toml",
        "xml": "XML",
        "html": "HTML",
        "css": "CSS",
        "sql": "SQL",
        "make": "make",
        "makefile": "make",
        "tex": "TeX",
        "latex": "TeX",
        "markdown": "markdown",
        "md": "markdown",
        "diff": "diff",
    }
    normalized = lang.strip().lower().replace(" ", "")
    return aliases.get(normalized, "")


def markdown_to_latex(text: str) -> str:
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    lines = text.split("\n")
    out: list[str] = []
    in_code = False
    code_lines: list[str] = []
    in_itemize = False
    in_enumerate = False
    in_quote = False

    def close_lists() -> None:
        nonlocal in_itemize, in_enumerate
        if in_itemize:
            out.append("\\end{itemize}")
            in_itemize = False
        if in_enumerate:
            out.append("\\end{enumerate}")
            in_enumerate = False

    def close_quote() -> None:
        nonlocal in_quote
        if in_quote:
            out.append("\\end{quote}")
            in_quote = False

    i = 0
    while i < len(lines):
        line = lines[i].rstrip()

        if line.startswith("```"):
            close_lists()
            close_quote()
            if in_code:
                lang = ""
                if code_lines and code_lines[0].startswith("LANG:"):
                    lang = code_lines[0].split(":", 1)[1].strip()
                    code_lines = code_lines[1:]
                listing_lang = valid_latex_listing_language(lang)
                attrs = "basicstyle=\\ttfamily\\small, breaklines=true"
                if listing_lang:
                    attrs += f", language={listing_lang}"
                out.append("\\begin{lstlisting}[" + attrs + "]")
                out.extend(sanitize_code_for_latex(line) for line in code_lines)
                out.append("\\end{lstlisting}")
                code_lines = []
                in_code = False
            else:
                fence = line.strip()[3:].strip()
                lang = fence.strip().lower() if fence else ""
                code_lines = [f"LANG:{lang}"] if lang else []
                in_code = True
            i += 1
            continue

        if in_code:
            code_lines.append(line)
            i += 1
            continue

        table_render, next_index = markdown_table_to_latex(lines, i)
        if table_render is not None:
            close_lists()
            close_quote()
            out.append(table_render)
            i = next_index
            continue

        if re.match(r"^#{1,6}\s+", line):
            close_lists()
            close_quote()
            m = re.match(r"^(#{1,6})\s+(.*?)\s*#*\s*$", line)
            if not m:
                i += 1
                continue
            level = len(m.group(1))
            heading = m.group(2).strip()
            heading_ltx = latex_escape(heading)
            if level == 1:
                out.append(f"\\chapter{{{heading_ltx}}}")
            elif level == 2:
                out.append(f"\\section{{{heading_ltx}}}")
            elif level == 3:
                out.append(f"\\subsection{{{heading_ltx}}}")
            elif level == 4:
                out.append(f"\\subsubsection{{{heading_ltx}}}")
            else:
                out.append(f"\\paragraph{{{heading_ltx}}}")
            i += 1
            continue

        if re.match(r"^\s*$", line):
            close_lists()
            close_quote()
            out.append("")
            i += 1
            continue

        if line.startswith(">"):
            if not in_quote:
                out.append("\\begin{quote}")
                in_quote = True
            out.append(latex_escape(line[1:].strip()))
            i += 1
            continue

        checkbox_item = checkbox_to_latex(line)
        if checkbox_item is not None:
            if not in_itemize:
                close_quote()
                out.append("\\begin{itemize}")
                in_itemize = True
            out.append(checkbox_item)
            i += 1
            continue

        if re.match(r"^\s*-\s+", line) or re.match(r"^\s*\*\s+", line):
            if not in_itemize:
                close_quote()
                out.append("\\begin{itemize}")
                in_itemize = True
            content = re.sub(r"^\s*[-*]\s+", "", line)
            out.append("\\item " + latex_inline(content))
            i += 1
            continue

        if re.match(r"^\s*\d+\.\s+", line):
            if not in_enumerate:
                close_quote()
                out.append("\\begin{enumerate}")
                in_enumerate = True
            content = re.sub(r"^\s*\d+\.\s+", "", line)
            out.append("\\item " + latex_inline(content))
            i += 1
            continue

        close_lists()
        out.append(latex_inline(line))
        i += 1

    close_lists()
    close_quote()
    if in_code:
        out.append("\\end{verbatim}")

    body = "\n".join(out).strip()
    return body + "\n" if body else ""


def latex_inline(text: str) -> str:
    text = latex_escape(text)
    text = re.sub(r"\*\*(.+?)\*\*", r"\\textbf{\1}", text)
    text = re.sub(r"(?<!\*)\*(?!\*)(.+?)(?<!\*)\*(?!\*)", r"\\emph{\1}", text)
    text = re.sub(r"`([^`]+)`", r"\\texttt{\1}", text)
    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r"\\href{\2}{\1}", text)
    return text


def should_skip(path: Path) -> bool:
    ignore_dirs = {
        ".git",
        ".hg",
        ".svn",
        "__pycache__",
        ".venv",
        "venv",
        "node_modules",
        "build",
        "dist",
        "out",
        ".idea",
        ".vscode",
    }
    return any(part in ignore_dirs for part in path.parts)


def find_markdown_files(root: Path) -> list[Path]:
    files = []
    for pattern in ("**/*.md", "**/*.markdown"):
        files.extend(root.glob(pattern))

    unique = []
    seen: set[Path] = set()
    for p in files:
        if not p.is_file() or should_skip(p):
            continue
        norm = p.resolve()
        if norm not in seen:
            seen.add(norm)
            unique.append(p)
    unique.sort(key=lambda p: p.relative_to(root).as_posix().lower())

    if not unique:
        raise FileNotFoundError(f"Keine Markdown-Dateien gefunden unter: {root}")
    return unique


def extract_code_examples(markdown_text: str) -> list[tuple[str, str]]:
    examples: list[tuple[str, str]] = []
    pattern = re.compile(r"```(?:\s*([A-Za-z0-9_+\-./]+))?\s*\n(.*?)```", re.DOTALL)
    for match in pattern.finditer(markdown_text):
        lang = (match.group(1) or "").strip()
        code = match.group(2).strip()
        if not code:
            continue
        examples.append((lang, code))
    return examples


def build_latex_book(title: str, files: list[Path], root: Path) -> str:
    chapters: list[str] = []
    grouped: dict[str, list[Path]] = {}

    for path in files:
        rel = rel_posix(path, root)
        key = rel.split("/", 1)[0] if "/" in rel else "ROOT"
        grouped.setdefault(key, []).append(path)

    for group_name in sorted(grouped.keys(), key=lambda value: (value == "ROOT", value.lower())):
        group_paths = grouped[group_name]
        if group_name != "ROOT":
            chapters.append(f"\\part{{{latex_escape(group_name)}}}")

        for path in sorted(group_paths, key=lambda p: rel_posix(p, root).lower()):
            text = path.read_text(encoding="utf-8")
            heading = first_heading(text)
            rel = rel_posix(path, root)
            chapters.append(f"\\chapter{{{latex_escape(heading)}}}")
            chapters.append(f"\\label{{sec:{rel.replace('/', '-')}}}")
            chapters.append(f"\\textit{{Quelle: {latex_escape(rel)}}}\\\\")
            chapters.append("")
            chapters.append(markdown_to_latex(strip_leading_heading(text)))

    latex = (
        "% Generated by scripts/generate_latex_manual.py\n"
        "\\documentclass[11pt,oneside,openany]{book}\n"
        "\\usepackage[utf8]{inputenc}\n"
        "\\usepackage[T1]{fontenc}\n"
        "\\usepackage{lmodern}\n"
        "\\usepackage{geometry}\n"
        "\\usepackage{hyperref}\n"
        "\\usepackage{amsmath,amssymb}\n"
        "\\usepackage{enumitem}\n"
        "\\usepackage{graphicx}\n"
        "\\usepackage{fancyhdr}\n"
        "\\usepackage{listings}\n"
        "\\usepackage{booktabs}\n"
        "\\usepackage{adjustbox}\n"
        "\\usepackage{pdflscape}\n"
        "\\usepackage{longtable}\n"
        "\\usepackage{array}\n"
        "\\usepackage{titlesec}\n"
        "\\usepackage[table]{xcolor}\n"
        "\\geometry{a4paper,margin=2.5cm}\n"
        "\\pagestyle{fancy}\n"
        "\\fancyhf{}\n"
        "\\fancyhead[LE,RO]{\\thepage}\n"
        "\\fancyhead[LO]{\\nouppercase{\\rightmark}}\n"
        "\\fancyhead[RE]{\\nouppercase{\\leftmark}}\n"
        "\\renewcommand{\\headrulewidth}{0.4pt}\n"
        "\\setlength{\\headheight}{14pt}\n"
        "\\setlength{\\footskip}{18pt}\n"
        "\\setlength{\\parskip}{0.5em}\n"
        "\\setlength{\\parindent}{0pt}\n"
        "\\setlist[itemize]{leftmargin=*, itemsep=0.35em, topsep=0.35em}\n"
        "\\setlist[enumerate]{leftmargin=*, itemsep=0.35em, topsep=0.35em}\n"
        "\\titleformat{\\chapter}[display]{\\normalfont\\huge\\bfseries}{\\chaptertitlename\\ \thechapter}{20pt}{\\Huge}\n"
        "\\titlespacing*{\\chapter}{0pt}{0pt}{18pt}\n"
        "\\titleformat{\\section}{\\normalfont\\Large\\bfseries}{\\thesection}{1em}{}\n"
        "\\titleformat{\\subsection}{\\normalfont\\large\\bfseries}{\\thesubsection}{1em}{}\n"
        "\\renewcommand{\\chaptermark}[1]{\\markboth{\\chaptername\\space\\thechapter.\\ #1}{}}\n"
        "\\renewcommand{\\sectionmark}[1]{\\markright{\\thesection.\\ #1}}\n"
        "\\hypersetup{colorlinks=true,linkcolor=blue,urlcolor=blue,citecolor=blue}\n"
        "\\lstset{basicstyle=\\ttfamily\\small, breaklines=true, showstringspaces=false, frame=single, rulecolor=\\color{gray!60}}\n"
        "\\setcounter{tocdepth}{2}\n"
        f"\\title{{{latex_escape(title)}}}\n"
        "\\author{REIST OS}\n"
        "\\date{\\today}\n"
        "\n"
        "\\begin{document}\n"
        "\\frontmatter\n"
        "\\maketitle\n"
        "\\tableofcontents\n"
        "\\mainmatter\n"
        "\n"
    )
    latex += "\n".join(chapters)
    code_sections: list[str] = ["\\chapter{Code Examples}"]
    code_count = 0
    for path in files:
        rel = rel_posix(path, root)
        text = path.read_text(encoding="utf-8")
        examples = extract_code_examples(text)
        if not examples:
            continue
        code_sections.append(f"\\section{{{latex_escape(rel)}}}")
        for lang, code in examples:
            listing_lang = valid_latex_listing_language(lang)
            attrs = "basicstyle=\\ttfamily\\small, breaklines=true"
            if listing_lang:
                attrs += f", language={listing_lang}"
            code_sections.append("\\begin{lstlisting}[" + attrs + "]")
            code_sections.append(sanitize_code_for_latex(code))
            code_sections.append("\\end{lstlisting}")
            code_count += 1

    if code_count > 0:
        latex += "\n" + "\n".join(code_sections) + "\n"

    latex += "\n\\backmatter\n"
    latex += "\\chapter*{Documentation Index}\n"
    latex += "\\addcontentsline{toc}{chapter}{Documentation Index}\n"
    latex += "\\begin{thebibliography}{99}\n"
    for index, path in enumerate(files, start=1):
        rel = rel_posix(path, root)
        title = first_heading(path.read_text(encoding="utf-8"))
        latex += f"\\bibitem{{doc{index}}} {latex_escape(title)} -- {latex_escape(rel)}\n"
    latex += "\\end{thebibliography}\n"
    latex += "\\end{document}\n"
    return latex


def compile_pdf(tex_path: Path, work_dir: Path) -> None:
    pdflatex = shutil.which("pdflatex")
    if pdflatex is None:
        raise FileNotFoundError("pdflatex not found; install TeX Live and retry.")

    cmd = [
        pdflatex,
        "-interaction=nonstopmode",
        "-halt-on-error",
        "-output-directory",
        str(work_dir),
        str(tex_path),
    ]
    completed = subprocess.run(cmd, capture_output=True, text=False)
    if completed.returncode != 0:
        output_text = (completed.stdout or b"").decode("utf-8", errors="replace")
        error_text = (completed.stderr or b"").decode("utf-8", errors="replace")
        raise RuntimeError(output_text + error_text)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a single LaTeX book from all Markdown files in the repository.")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1], help="Repository root to scan")
    parser.add_argument("--output", type=Path, default=Path("docs/manual/reist-handbuch.tex"), help="Output .tex path")
    parser.add_argument("--title", default="REIST OS Architecture and Engineering Handbook", help="Document title")
    parser.add_argument("--compile", action="store_true", help="Compile to PDF with pdflatex if installed")
    args = parser.parse_args()

    root = args.root.resolve()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    files = find_markdown_files(root)
    tex = build_latex_book(args.title, files, root)
    output.write_text(tex, encoding="utf-8")
    print(f"LaTeX document written to: {output}")
    print(f"Markdown files included: {len(files)}")

    if args.compile:
        try:
            compile_pdf(output, output.parent)
            print(f"PDF created: {output.with_suffix('.pdf')}")
        except Exception as exc:  # pragma: no cover
            print(f"Warning: LaTeX compilation failed: {exc}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
