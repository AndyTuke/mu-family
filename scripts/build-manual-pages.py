#!/usr/bin/env python3
"""Build the HTML user-manual pages for the website from the manual scripts.

Source of truth: docs/<product>/create_manual.ps1 (the H1/H2/H3/P/Bullet/Pic calls).
Output: site/<product>-manual.html, plus resized screenshots in site/manual-images/.
The .docx and PDF files are not touched. Re-run after any manual text change:

    python scripts/build-manual-pages.py

The script only rewrites the generated pages and images. Links from the product
pages, the sitemap and guides.html are added once by hand (or by the one-off
step in the web steward's notes) and are not changed here.
"""
import html
import json
import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    Image = None

ROOT = Path(__file__).resolve().parent.parent
SITE = ROOT / "site"
DOCS = ROOT / "docs"
BASE = "https://transwarp.me/"
IMG_DIR = SITE / "manual-images"
MAX_IMG_WIDTH = 1100

# name, folder, title, description, PDF file, product page, hero image on site
PRODUCTS = [
    ("mu-Clid", "mu-clid", "mu-Clid Manual: Euclidean Sequencer Plugin User Guide",
     "User guide for mu-Clid, a free Euclidean rhythm sequencer and sample trigger plugin (VST3, CLAP, Standalone): Euclidean layers, logic, modulators, mixer, presets and MIDI.",
     "mu-Clid-User-Manual.pdf", "mu-clid", "euclidean-rhythm-sequencer-vst3-full-window.png"),
    ("mu-Clid MIDI", "mu-clid-midi", "mu-Clid MIDI Manual: Euclidean MIDI Effect User Guide",
     "User guide for mu-Clid MIDI, a free Euclidean rhythm MIDI effect plugin (VST3, CLAP): setting it up in your DAW, the Euclidean panel, logic and MIDI output.",
     "mu-Clid-MIDI-User-Manual.pdf", "mu-clid-midi", "euclidean-midi-effect-vst3-editor.png"),
    ("mu-Tant", "mu-tant", "mu-Tant Manual: Wavetable Drone Synth User Guide",
     "User guide for mu-Tant, a free wavetable drone synth plugin (VST3, CLAP, Standalone): oscillators, FM and ring modulation, the gate sequencer, modulators and mixer.",
     "mu-Tant-User-Manual.pdf", "mu-tant", "wavetable-drone-synth-vst3-full-window.png"),
    ("mu-Toni", "mu-toni", "mu-Toni Manual: Generative Arpeggiator Synth User Guide",
     "User guide for mu-Toni, a free generative arpeggiator synth plugin (VST3, CLAP, Standalone): scale and chord pool, the Appergater, the synth, modulators and mixer.",
     "mu-Toni-User-Manual.pdf", "mu-toni", "generative-arpeggiator-synth-vst3-full-window.png"),
    ("mu-On", "mu-on", "mu-On Manual: 909-Style Groove Sequencer User Guide",
     "User guide for mu-On, a free 909-style groove sequencer plugin (VST3, CLAP, Standalone): the step grid, kick and bass engines, drums, rumble, sidechain and mixer.",
     "mu-On-User-Manual.pdf", "mu-on", "909-groove-sequencer-vst3-full-window.png"),
    ("mu-Tilate", "mu-tilate", "mu-Tilate Manual: Loop Slicer Plugin User Guide",
     "User guide for mu-Tilate, a free loop slicer plugin (VST3, CLAP, Standalone): loading loops, slicing on transients, the timeline, tempo matching, slice controls and mixer.",
     "mu-Tilate-User-Manual.pdf", "mu-tilate", "loop-slicer-plugin-vst3-full-window.png"),
    ("mu-Tronic", "mu-tronic", "mu-Tronic Manual: Standalone Live Performance Rig User Guide",
     "User guide for mu-Tronic, a free standalone live performance rig that hosts up to 16 sequencer and synth layers: slots, buses, scenes, presets and mixer.",
     "mu-Tronic-User-Manual.pdf", "mu-tronic", "standalone-performance-rig-full-window.png"),
    ("mu-Link", "mu-link", "mu-Link Manual: Free Live Mixer and Clock Sync Hub User Guide",
     "User guide for mu-Link, a free live audio mixer and clock sync hub for Windows: connecting apps, the master clock, mixer strips, scenes and MIDI clock.",
     "mu-Link-User-Manual.pdf", "mu-link", "audio-sync-hub-live-mixer-window.png"),
]

CALL = re.compile(r'^(H1|H2|H3|P|Bullet|Pic)\s+"')


def read_ps_string(text, pos):
    """Read a PowerShell double-quoted string starting after the opening quote.
    Returns (value, index after the closing quote)."""
    out = []
    i = pos
    while i < len(text):
        c = text[i]
        if c == "`" and i + 1 < len(text):
            n = text[i + 1]
            out.append({"n": "\n", "t": "\t"}.get(n, n))
            i += 2
        elif c == '"':
            if text[i + 1:i + 2] == '"':
                out.append('"')
                i += 2
            else:
                return "".join(out), i + 1
        else:
            out.append(c)
            i += 1
    # A line with no closing quote (a typo in a manual script): keep the text read so far.
    print("warning: unterminated string:", text[:60], file=sys.stderr)
    return "".join(out), len(text)


def parse_manual(path):
    """Return (title_info, blocks). blocks are (kind, text, extra) tuples."""
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    blocks = []
    title = {"title": "", "subtitle": "", "version": ""}
    for ln in lines:
        s = ln.strip()
        m = CALL.match(s)
        if m:
            kind = m.group(1)
            text, end = read_ps_string(s, m.end())
            extra = ""
            rest = s[end:].strip()
            if kind == "Pic" and rest.startswith('"'):
                extra, _ = read_ps_string(rest, 1)
            blocks.append((kind, text, extra))
            continue
        # Title block: $sel.TypeText("...") lines before the first page break
        t = re.match(r'^\$sel\.TypeText\("(.*)"\)\s*$', s)
        if t and not blocks and "$" not in t.group(1):
            val = t.group(1)
            if not title["title"]:
                title["title"] = val
            elif not title["subtitle"]:
                title["subtitle"] = val
            elif val.startswith("Version"):
                title["version"] = val
    return title, blocks


def slugify(s):
    s = re.sub(r"^\d+\.\s*", "", s.lower())
    s = re.sub(r"[^a-z0-9]+", "-", s).strip("-")
    return s or "section"


def esc(s):
    return html.escape(s, quote=True)


def inline(s):
    """Escape text, then bold the label before ' — ' in short bullet lead-ins."""
    return esc(s)


def bullet_html(s):
    m = re.match(r"^(.{1,60}?)\s+[—–]\s+(.*)$", s, re.S)
    if m:
        return "<strong>%s</strong> &mdash; %s" % (esc(m.group(1).strip()), esc(m.group(2)))
    return esc(s)


def prepare_image(folder, image, prefix):
    """Resize a screenshot into site/manual-images and return its site-relative name."""
    src = DOCS / folder / "images" / image
    if not src.exists():
        return None
    name = "%s-manual-%s.webp" % (prefix, re.sub(r"\.png$", "", image))
    dst = IMG_DIR / name
    if Image is None:
        print("Pillow not installed: cannot make", name, file=sys.stderr)
        return None
    with Image.open(src) as im:
        im = im.convert("RGBA") if im.mode in ("P", "LA") else im
        if im.width > MAX_IMG_WIDTH:
            h = round(im.height * MAX_IMG_WIDTH / im.width)
            im = im.resize((MAX_IMG_WIDTH, h), Image.LANCZOS)
        w, h = im.size
        IMG_DIR.mkdir(exist_ok=True)
        im.save(dst, "WEBP", quality=82, method=6)
    return name, w, h


def build_page(prod, template):
    name, folder, title, desc, pdf, page, hero = prod
    ps = DOCS / folder / "create_manual.ps1"
    info, blocks = parse_manual(ps)
    slug = "%s-manual" % page
    ids = {}
    toc = []
    body = []
    in_list = False
    first_h1_seen = False
    pics = 0

    def close_list():
        nonlocal in_list
        if in_list:
            body.append("</ul>")
            in_list = False

    for kind, text, extra in blocks:
        if kind == "Bullet":
            if not in_list:
                body.append("<ul>")
                in_list = True
            body.append("<li>%s</li>" % bullet_html(text))
            continue
        close_list()
        if kind == "P":
            body.append("<p>%s</p>" % esc(text))
        elif kind in ("H1", "H2", "H3"):
            sid = slugify(text)
            n = ids.get(sid, 0)
            ids[sid] = n + 1
            if n:
                sid = "%s-%d" % (sid, n + 1)
            tag = {"H1": "h2", "H2": "h3", "H3": "h4"}[kind]
            if kind == "H1":
                toc.append((sid, text))
            body.append('<%s id="%s">%s</%s>' % (tag, sid, esc(text), tag))
        elif kind == "Pic":
            got = prepare_image(folder, extra, page) if extra else None
            if got:
                fname, w, h = got
                pics += 1
                body.append('<figure><img src="manual-images/%s" alt="%s" width="%d" height="%d" loading="lazy"><figcaption>%s</figcaption></figure>'
                            % (fname, esc(text), w, h, esc(text)))
    close_list()

    toc_html = "<nav class=\"toc\" aria-label=\"Contents\"><h2>Contents</h2><ol>%s</ol></nav>" % "".join(
        '<li><a href="#%s">%s</a></li>' % (i, esc(re.sub(r"^\d+\.\s*", "", t))) for i, t in toc)
    pdf_note = ('<p>Prefer a file? <a href="manuals/%s">Download the manual as a PDF</a>. '
                'Back to <a href="%s.html">%s</a> &middot; <a href="%s-releases.html">release notes</a> &middot; '
                '<a href="guides.html">all guides</a>.</p>') % (pdf, page, esc(name), page)

    head_end = template.index("</head>")
    main_s = template.index("  <main")
    main_e = template.index("</main>") + len("</main>")
    h = template[:head_end]
    h = re.sub(r"<title>.*?</title>", lambda m: "<title>%s | Transwarp</title>" % esc(title), h, 1)
    h = re.sub(r'(<meta name="description" content=")[^"]*"', lambda m: m.group(1) + esc(desc) + '"', h, 1)
    h = re.sub(r'(<link rel="canonical" href=")[^"]*"', lambda m: m.group(1) + BASE + slug + '"', h, 1)
    h = re.sub(r'(<meta property="og:url" content=")[^"]*"', lambda m: m.group(1) + BASE + slug + '"', h, 1)
    h = re.sub(r'(<meta property="og:title" content=")[^"]*"', lambda m: m.group(1) + esc(title) + '"', h, 1)
    ld = {
        "@context": "https://schema.org",
        "@type": "TechArticle",
        "headline": title,
        "description": desc,
        "mainEntityOfPage": BASE + slug,
        "image": BASE + hero,
        "inLanguage": "en-GB",
        "about": {"@type": "SoftwareApplication", "name": name, "url": BASE + page,
                  "applicationCategory": "MusicApplication", "operatingSystem": "Windows"},
        "author": {"@type": "Organization", "name": "Transwarp Development Project"},
        "publisher": {"@type": "Organization", "name": "Transwarp Development Project", "url": BASE},
        "encodingFormat": "text/html",
        "associatedMedia": {"@type": "MediaObject", "contentUrl": BASE + "manuals/" + pdf, "encodingFormat": "application/pdf"},
    }
    extra_head = ('  <meta property="og:description" content="%s">\n  <meta property="og:image" content="%s%s">\n'
                  '  <script type="application/ld+json">\n%s\n  </script>\n') % (esc(desc), BASE, hero, json.dumps(ld, indent=2, ensure_ascii=False))
    h = h.replace('  <link rel="preconnect" href="https://fonts.googleapis.com">',
                  extra_head + '  <link rel="preconnect" href="https://fonts.googleapis.com">', 1)
    sub = info["subtitle"] or ""
    main = ('  <main style="padding-top: 120px; padding-bottom: 100px;">\n    <article class="art manual">\n'
            '      <div class="kicker">User manual%s</div>\n      <h1>%s</h1>\n'
            '      <p class="lede">%s: %s.</p>\n%s\n%s\n%s\n%s\n    </article>\n  </main>') % (
        (" &middot; " + esc(info["version"])) if info["version"] else "",
        esc(title.split(" | ")[0]), esc(name), esc(sub) if sub else "User guide",
        pdf_note, toc_html, "\n".join(body), pdf_note)
    out = h + template[head_end:main_s] + main + template[main_e:]
    open(SITE / (slug + ".html"), "w", encoding="utf-8", newline="").write(out)
    return slug, len(blocks), pics


def main():
    template = open(SITE / "contact.html", encoding="utf-8", newline="").read()
    # contact.html is the page shell; strip any manual links injected into it by mistake
    for prod in PRODUCTS:
        slug, n, pics = build_page(prod, template)
        print("%-28s %4d blocks %3d images" % (slug + ".html", n, pics))


if __name__ == "__main__":
    main()
