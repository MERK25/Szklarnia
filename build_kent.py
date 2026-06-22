#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
build_kent.py
=============
Pobiera CAŁE Repertorium Kenta z http://www.homeoint.org/books/kentrep/
i zapisuje je do pliku `kent-repertory.json`, który ładuje aplikacja
(index.html). Po jednorazowym uruchomieniu „Repertorium” w aplikacji
obejmuje wszystkie rozdziały i rubryki tego repozytorium.

WYMAGANIA:  Python 3.8+  oraz biblioteka requests:
    pip install requests

URUCHOMIENIE (w folderze z plikiem index.html):
    python build_kent.py

Skrypt zapisuje pobrane strony do podfolderu .cache/, więc kolejne
uruchomienia są szybkie i uprzejme dla serwera.

JAK ROZPOZNAWANE SĄ STOPNIE LEKÓW (konwencja Kenta):
    pogrubienie  -> stopień 3   (najsilniejszy)
    kursywa      -> stopień 2
    pismo zwykłe -> stopień 1
"""

import os, re, json, time, html, sys, urllib.parse, datetime
import requests

BASE   = "http://www.homeoint.org/books/kentrep/"
INDEX  = BASE + "index.htm"
REMEDS = BASE + "kentreme.htm"
CACHE  = ".cache"
OUT    = "kent-repertory.json"
DELAY  = 0.30                      # uprzejma przerwa między pobraniami (s)

# strony „meta”, które nie są rozdziałami objawów.
# Uwaga: wyrażenia regularne wyłapują fragment PO słowie „kent”,
# np. z „kentreme.htm” powstaje „reme”, dlatego tu trzymamy te fragmenty.
SKIP_CHAPTERS = {"pref","cont","reme","abbr","userep","rep",
                 "rept","repert","home","index"}

session = requests.Session()
session.headers.update({"User-Agent": "kent-repertory-builder/1.0 (personal use)"})

os.makedirs(CACHE, exist_ok=True)

def fetch(url):
    """Pobiera URL z pamięcią podręczną na dysku."""
    key = re.sub(r"[^A-Za-z0-9]+", "_", url) + ".html"
    path = os.path.join(CACHE, key)
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    for attempt in range(3):
        try:
            r = session.get(url, timeout=30)
            r.encoding = r.apparent_encoding or "latin-1"
            txt = r.text
            with open(path, "w", encoding="utf-8") as f:
                f.write(txt)
            time.sleep(DELAY)
            return txt
        except Exception as e:
            print(f"  ! błąd ({attempt+1}/3) {url}: {e}", file=sys.stderr)
            time.sleep(1.5)
    return ""

def absolutize(href, base):
    return urllib.parse.urljoin(base, href)

# ---------------------------------------------------------------------------
# 1) Lista rozdziałów (nazwa + URL) z index.htm
# ---------------------------------------------------------------------------
def get_chapters():
    htmltxt = fetch(INDEX)
    chapters = []   # (filename, display_name, url)
    seen = set()
    for m in re.finditer(r'<a\s+href="([^"]+?kent([a-z]+)\.htm)"[^>]*>(.*?)</a>',
                          htmltxt, re.I | re.S):
        url, stem, label = m.group(1), m.group(2).lower(), m.group(3)
        if stem in SKIP_CHAPTERS or stem in seen:
            continue
        name = re.sub(r"<[^>]+>", "", label)
        name = html.unescape(name).strip()
        if not name:
            continue
        seen.add(stem)
        chapters.append((stem, name, absolutize(url, INDEX)))
    return chapters

# ---------------------------------------------------------------------------
# 2) Z każdej strony-spisu rozdziału zbieramy linki do stron z treścią
#    (kentrepN/kentNNNN.htm) oraz mapę: plik treści -> rozdział.
# ---------------------------------------------------------------------------
CONTENT_RE = re.compile(r'href="([^"]*?kentrep\d*/kent\d+\.htm)', re.I)
CHAPLINK_RE = re.compile(r'href="[^"]*?/kentrep/kent([a-z]+)\.htm', re.I)

def collect_content_pages(chapters):
    content_urls = []           # zachowuje kolejność
    seen = set()
    for stem, name, url in chapters:
        page = fetch(url)
        for m in CONTENT_RE.finditer(page):
            cu = absolutize(m.group(1), url).split("#")[0]
            if cu not in seen:
                seen.add(cu)
                content_urls.append(cu)
    return content_urls

# ---------------------------------------------------------------------------
# 3) Parser pojedynczej strony z treścią
# ---------------------------------------------------------------------------
B_OPEN  = re.compile(r'<\s*(b|strong)\b[^>]*>', re.I)
B_CLOSE = re.compile(r'<\s*/\s*(b|strong)\s*>', re.I)
I_OPEN  = re.compile(r'<\s*(i|em)\b[^>]*>', re.I)
I_CLOSE = re.compile(r'<\s*/\s*(i|em)\s*>', re.I)

NAV_LINE = re.compile(r'^(KENT|<+|>+|-+|p\.\s*\d+|\[|copyright)', re.I)
PAGE_MARK = re.compile(r'p\.\s*\d+', re.I)

def chapter_of_page(raw, chap_by_stem):
    """Rozdział strony rozpoznajemy po linku powrotnym do /kentrep/kentXXX.htm."""
    m = CHAPLINK_RE.search(raw)
    if m:
        return chap_by_stem.get(m.group(1).lower())
    return None

def to_marked_text(raw):
    """Zamienia <b>/<i> na znaczniki \x01/\x02 i usuwa pozostałe tagi."""
    # wytnij nagłówek/stopkę nawigacji w miarę możliwości – zostawiamy całość,
    # filtrowanie linii zrobimy dalej.
    t = raw
    t = B_OPEN.sub("\x01", t); t = B_CLOSE.sub("\x01", t)
    t = I_OPEN.sub("\x02", t); t = I_CLOSE.sub("\x02", t)
    t = re.sub(r'<br[^>]*>', "\n", t, flags=re.I)
    t = re.sub(r'</p>', "\n", t, flags=re.I)
    t = re.sub(r'</tr>', "\n", t, flags=re.I)
    t = re.sub(r'<[^>]+>', "", t)          # usuń resztę tagów (w tym <a ...>)
    t = html.unescape(t)
    return t

REM_TOKEN = re.compile(r'^[a-zäöüæœ][a-zäöüæœ0-9\-]*$', re.I)

def parse_remedies(s):
    """Z fragmentu po dwukropku zwraca {skrót: stopień}."""
    out = {}
    b3 = b2 = False
    buf = []
    def flush():
        name = "".join(buf).strip()
        buf.clear()
        if not name:
            return
        if "(" in name or "see" in name.lower():
            return
        grade = 3 if b3 else (2 if b2 else 1)
        key = name.strip().strip(".").strip().lower()
        key = key.replace("\xa0", "").strip()
        if key and REM_TOKEN.match(key):
            # najwyższy spotkany stopień wygrywa
            if out.get(key, 0) < grade:
                out[key] = grade
    for ch in s:
        if ch == "\x01":
            flush(); b3 = not b3
        elif ch == "\x02":
            flush(); b2 = not b2
        elif ch == ",":
            flush()
        elif ch == "\n":
            flush()
        else:
            buf.append(ch)
    flush()
    return out

def first_alpha_word(s):
    m = re.search(r"[A-Za-zÀ-ÿ]+", s)
    return m.group(0) if m else ""

def parse_page(raw, chapter):
    rubrics = []
    text = to_marked_text(raw)
    current_main = None
    for line in text.split("\n"):
        line = line.strip()
        if not line:
            continue
        clean = line.replace("\x01", "").replace("\x02", "").strip()
        if not clean or NAV_LINE.match(clean):
            continue
        if " : " in line or re.search(r":\s*$", line):
            label, _, rem = line.partition(" : ")
            label_clean = label.replace("\x01","").replace("\x02","").strip()
            label_clean = re.sub(r"\s+", " ", label_clean)
            if not label_clean:
                continue
            has_bold = "\x01" in label
            fw = first_alpha_word(label_clean)
            is_main = has_bold or (fw.isupper() and len(fw) > 1)
            if is_main:
                current_main = label_clean
                path = label_clean
            else:
                path = (current_main + ", " + label_clean) if current_main else label_clean
            rems = parse_remedies(rem)
            if rems and chapter:
                rubrics.append({"c": chapter, "p": path, "r": rems})
        else:
            # być może nagłówek rubryki (pogrubiony, bez leków w tej samej linii)
            if "\x01" in line and len(clean) > 1 and not PAGE_MARK.search(clean):
                fw = first_alpha_word(clean)
                if fw and (fw.isupper() or "\x01" in line):
                    current_main = re.sub(r"\s+", " ", clean)
    return rubrics

# ---------------------------------------------------------------------------
# 4) Słownik leków (skrót -> pełna nazwa) z kentreme.htm
# ---------------------------------------------------------------------------
def get_remedy_names():
    raw = fetch(REMEDS)
    text = to_marked_text(raw)
    names = {}
    for line in text.split("\n"):
        m = re.match(r"\s*(.+?\.?)-{3,}\s*(.+)$", line)
        if m:
            key = m.group(1).strip().strip(".").strip().lower()
            name = re.sub(r"\s+", " ", m.group(2)).strip()
            if key and name and REM_TOKEN.match(key):
                names[key] = name
    return names

# ---------------------------------------------------------------------------
# MAIN
# ---------------------------------------------------------------------------
def main():
    print("» Pobieram spis rozdziałów…")
    chapters = get_chapters()
    chap_by_stem = {stem: name for stem, name, _ in chapters}
    chapter_order = [name for _, name, _ in chapters]
    print(f"  znaleziono {len(chapters)} rozdziałów.")

    print("» Zbieram adresy stron z treścią…")
    content = collect_content_pages(chapters)
    print(f"  do pobrania: {len(content)} stron.")

    print("» Słownik nazw leków…")
    remedy_names = get_remedy_names()
    print(f"  leków: {len(remedy_names)}.")

    all_rubrics = []
    for i, url in enumerate(content, 1):
        raw = fetch(url)
        ch = chapter_of_page(raw, chap_by_stem)
        rubs = parse_page(raw, ch)
        all_rubrics.extend(rubs)
        if i % 20 == 0 or i == len(content):
            print(f"  [{i}/{len(content)}] rubryk łącznie: {len(all_rubrics)}")

    # uzupełnij słownik o skróty napotkane w treści (gdyby czegoś brakowało)
    used = set()
    for r in all_rubrics:
        used.update(r["r"].keys())
    for k in used:
        remedy_names.setdefault(k, k.capitalize())

    data = {
        "meta": {
            "source": BASE,
            "title": "Repertory of the Homœopathic Materia Medica — J.T. Kent (1897)",
            "generated": datetime.datetime.now().isoformat(timespec="seconds"),
            "rubrics": len(all_rubrics),
            "remedies": len(remedy_names),
        },
        "chapters": chapter_order,
        "remedies": {k: remedy_names[k] for k in sorted(remedy_names)},
        "rubrics": all_rubrics,
    }
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, separators=(",", ":"))
    size = os.path.getsize(OUT) / (1024*1024)
    print(f"\n✓ Zapisano {OUT}  ({size:.1f} MB)")
    print(f"  rozdziały: {len(chapter_order)} · rubryki: {len(all_rubrics)} · leki: {len(remedy_names)}")
    print("  Wgraj ten plik obok index.html (to samo repozytorium) i gotowe.")

if __name__ == "__main__":
    main()
