#!/usr/bin/env python3
"""Assert chrome menus stay small: a ⋯ / hamburger / menu level holds at most 5 items,
and the Android bottom navigation bar holds at most 4 destinations.
Separators do not count. Dynamic content (poll options, @mentions) is ignored.
usage: ui_menu_limits.py [--root DIR] [--self-test]
"""
from __future__ import annotations
import argparse, re, sys, xml.etree.ElementTree as ET
from pathlib import Path

MAX_MENU = 5
MAX_NAV = 4

def repo_root(start: Path | None = None) -> Path:
    p = (start or Path(__file__).resolve()).parent
    for d in [p, *p.parents]:
        if (d / "ui-winui").is_dir() and (d / "editions" / "portable").is_dir():
            return d
    raise SystemExit("cannot locate repo root from " + str(p))

def fail(rows, name, msg):
    rows.append((name, msg))

# ----- Android -----
def count_dropdown_items(block: str) -> int:
    return len(re.findall(r"DropdownMenuItem\s*\(", block))

def android_checks(root: Path, rows):
    home = root / "editions/portable/android/app/src/main/java/io/github/frec86/lanmessenger/Home.kt"
    text = home.read_text(encoding="utf-8")
    m = re.search(r"val items = listOf\((.*?)\)\s*\n\s*items\.forEachIndexed", text, re.S)
    if not m:
        fail(rows, "android-nav", "NavigationBar items listOf not found in Home.kt")
    else:
        n = len(re.findall(r'Triple\(', m.group(1)))
        if n > MAX_NAV:
            fail(rows, "android-nav", f"bottom nav has {n} destinations (max {MAX_NAV})")
        elif n == 0:
            fail(rows, "android-nav", "no Triple destinations in NavigationBar")
    # header overflow: first DropdownMenu in the file (top bar ⋯)
    m = re.search(r"DropdownMenu\([^)]*\)\s*\{(.*?)\n                \}", text, re.S)
    if not m:
        fail(rows, "android-overflow", "header DropdownMenu not found")
    else:
        n = count_dropdown_items(m.group(1))
        if n > MAX_MENU:
            fail(rows, "android-overflow", f"header ⋯ has {n} items (max {MAX_MENU})")
    chat = root / "editions/portable/android/app/src/main/java/io/github/frec86/lanmessenger/Chat.kt"
    if chat.exists():
        ct = chat.read_text(encoding="utf-8")
        i = ct.find("when (menuSub)")
        if i < 0:
            fail(rows, "android-chat-menu", "when (menuSub) not found in Chat.kt")
        else:
            # slice the when-block by braces
            j = ct.find("{", i)
            depth = 0
            k = j
            while k < len(ct):
                if ct[k] == "{": depth += 1
                elif ct[k] == "}":
                    depth -= 1
                    if depth == 0:
                        k += 1
                        break
                k += 1
            block = ct[j+1:k-1]
            branches = re.findall(r'(?:""|"([^"]+)")\s*->\s*(?:if\s*\([^)]+\)\s*)?\{', block)
            # extract bodies with brace scan from each arrow
            pos = 0
            found = 0
            for m in re.finditer(r'(?:""|"([^"]+)")\s*->\s*(?:if\s*\([^)]+\)\s*)?\{', block):
                name = m.group(1) or "⋯"
                start = m.end() - 1  # '{'
                depth = 0
                k = start
                while k < len(block):
                    if block[k] == "{": depth += 1
                    elif block[k] == "}":
                        depth -= 1
                        if depth == 0:
                            break
                    k += 1
                body = block[start+1:k]
                n = count_dropdown_items(body)
                found += 1
                if n > MAX_MENU:
                    fail(rows, "android-chat-menu", f"Chat.kt 「{name}」 has {n} items (max {MAX_MENU})")
            if found == 0:
                fail(rows, "android-chat-menu", "no menuSub branches parsed")

# ----- WinUI XAML -----
def xaml_menu_counts(path: Path, rows, label: str):
    raw = path.read_text(encoding="utf-8")
    # lightweight tag walk: ignore everything except MenuFlyout / Item / SubItem / Separator
    tokens = re.findall(r"</?MenuFlyout(?:SubItem|Item|Separator)?(?:\s[^>]*>|/>|>)", raw)
    stack = []  # list of [tag, count, title]
    def title_of(tok):
        m = re.search(r'Text="([^"]*)"', tok)
        return m.group(1) if m else tag_of(tok)
    def tag_of(tok):
        m = re.search(r"</?(MenuFlyout(?:SubItem|Item|Separator)?)(?:\s|/|>)", tok)
        return m.group(1) if m else ""
    for tok in tokens:
        tag = tag_of(tok)
        closing = tok.startswith("</")
        selfclose = tok.endswith("/>")
        if tag == "MenuFlyoutSeparator":
            continue
        if closing:
            if stack and stack[-1][0] == tag:
                _t, n, title = stack.pop()
                if tag in ("MenuFlyout", "MenuFlyoutSubItem") and n > MAX_MENU:
                    fail(rows, label, f"{title} has {n} items (max {MAX_MENU})")
                if stack and tag in ("MenuFlyoutItem", "MenuFlyoutSubItem"):
                    stack[-1][1] += 1
            continue
        if tag in ("MenuFlyout", "MenuFlyoutSubItem"):
            stack.append([tag, 0, title_of(tok)])
            if selfclose and stack:
                # empty
                stack.pop()
                if stack:
                    stack[-1][1] += 1
            continue
        if tag == "MenuFlyoutItem":
            if stack:
                stack[-1][1] += 1
    # unclosed
    while stack:
        _t, n, title = stack.pop()
        if _t in ("MenuFlyout", "MenuFlyoutSubItem") and n > MAX_MENU:
            fail(rows, label, f"{title} has {n} items (max {MAX_MENU})")

def winui_checks(root: Path, rows):
    chat = root / "ui-winui/LanMessenger/Views/ChatPage.xaml"
    if chat.exists():
        xaml_menu_counts(chat, rows, "winui-chat-flyout")

# ----- C / desktop menus: count helper-built top levels -----
def gtk_checks(root: Path, rows):
    p = root / "editions/portable/linux/gtk_gui.c"
    t = p.read_text(encoding="utf-8")
    # top-level: v072_msub(menu, ...) plus mi(menu, ...)
    start = t.find("menu=gtk_menu_new();")
    if start < 0:
        fail(rows, "gtk3-hamburger", "menu=gtk_menu_new() not found")
        return
    block = t[start:t.find("gtk_widget_show_all(menu);", start)]
    tops = len(re.findall(r"v072_msub\(menu,", block)) + len(re.findall(r"\bmi\(menu,", block))
    if tops > MAX_MENU:
        fail(rows, "gtk3-hamburger", f"top-level hamburger has {tops} items (max {MAX_MENU})")
    for m in re.finditer(r"v072_msub\((\w+),\"([^\"]+)\"\)", block):
        var, name = m.group(1), m.group(2)
        # items added to this submenu: mi(var, or v072_msub(var,
        # use the assignment target: { GtkWidget *s=v072_msub(menu,"添加");
        pass
    # count mi(s, after each msub by scanning each brace group roughly via name
    # Re-parse per submenu variable declared as GtkWidget *X=v072_msub
    for m in re.finditer(r"GtkWidget \*(\w+)=v072_msub\((\w+),\"([^\"]+)\"\)", block):
        var, parent, name = m.group(1), m.group(2), m.group(3)
        # from this point until the next sibling at same brace... count mi(var, and v072_msub(var,
        rest = block[m.end():]
        n = len(re.findall(rf"\bmi\({var},", rest.split("}", 1)[0] if "}" in rest else rest))
        n += len(re.findall(rf"v072_msub\({var},", rest.split("}", 1)[0] if "}" in rest else rest))
        if n > MAX_MENU:
            fail(rows, "gtk3-hamburger", f"submenu 「{name}」 has {n} items (max {MAX_MENU})")

def adw_checks(root: Path, rows):
    p = root / "editions/portable/linux/adw_gui.c"
    t = p.read_text(encoding="utf-8")
    for label, needle, parent in (
        ("adw-hamburger", "menu=g_menu_new();", "menu"),
        ("adw-chat-more", "GMenu *cm=g_menu_new();", "cm"),
    ):
        i = t.find(needle)
        if i < 0:
            fail(rows, label, f"{needle} not found")
            continue
        # take a 2500-char window
        block = t[i:i+3500]
        tops = len(re.findall(rf"v072_append_sub\({parent},", block)) + len(re.findall(rf"g_menu_append\({parent},", block))
        if tops > MAX_MENU:
            fail(rows, label, f"top level has {tops} items (max {MAX_MENU})")
        for m in re.finditer(r"GMenu \*(\w+)=g_menu_new\(\);([^\{]*?|.*?v072_append_sub\((\w+),\"([^\"]+)\",\1\))", block, re.S):
            pass
        # each `{ GMenu *s=g_menu_new(); ... v072_append_sub(parent,"Name",s)`
        for m in re.finditer(r'\{ GMenu \*(\w+)=g_menu_new\(\);(.*?)v072_append_sub\((\w+),"([^"]+)",\1\);', block, re.S):
            var, body, par, name = m.group(1), m.group(2), m.group(3), m.group(4)
            n = len(re.findall(rf"g_menu_append\({var},", body)) + len(re.findall(rf"v072_append_sub\({var},", body))
            if n > MAX_MENU:
                fail(rows, label, f"submenu 「{name}」 has {n} items (max {MAX_MENU})")

def xp_checks(root: Path, rows):
    p = root / "editions/portable/src/gui.c"
    t = p.read_text(encoding="utf-8")
    i = t.find("static HMENU build_menu(void)")
    if i < 0:
        fail(rows, "xp-menus", "build_menu not found")
        return
    block = t[i:t.find("return m;", i)+10]
    # top of each bar: v072_mpopup(X, or AM(X, where X in f,c,g,s,h — not nested vars
    for bar, title in (("f", "文件"), ("c", "联系人"), ("g", "群组"), ("s", "设置"), ("h", "帮助")):
        n = len(re.findall(rf"v072_mpopup\({bar},", block)) + len(re.findall(rf"\bAM\({bar},", block))
        if n > MAX_MENU:
            fail(rows, "xp-menus", f"「{title}」 top level has {n} items (max {MAX_MENU})")
    for m in re.finditer(r'(\w+)=v072_mpopup\((\w+),L"([^"]+)"\)', block):
        var, parent, name = m.group(1), m.group(2), m.group(3)
        # until semicolon groups on following AM(var and v072_mpopup(var — use all later AM(var before next var reused
        # simple: count AM(var, and v072_mpopup(var, in the rest of the same statement-run (until a blank or next bar build)
        rest = block[m.end():]
        # stop at next assignment to the same var or end
        stop = re.search(rf"\n\s*{var}=", rest)
        chunk = rest[:stop.start()] if stop else rest
        # only the immediately following AM/mpopup on this popup, until a different construction at column of next top call
        n = len(re.findall(rf"\bAM\({var},", chunk)) + len(re.findall(rf"v072_mpopup\({var},", chunk))
        if n > MAX_MENU:
            fail(rows, "xp-menus", f"submenu 「{name}」 has {n} items (max {MAX_MENU})")

def self_test() -> int:
    # a tiny XAML over the limit should fail, a 2-item one should pass
    import tempfile, os
    rows = []
    with tempfile.TemporaryDirectory() as d:
        x = Path(d) / "t.xaml"
        x.write_text("""<Page><MenuFlyout>
            <MenuFlyoutItem Text="1"/><MenuFlyoutItem Text="2"/><MenuFlyoutItem Text="3"/>
            <MenuFlyoutItem Text="4"/><MenuFlyoutItem Text="5"/><MenuFlyoutItem Text="6"/>
        </MenuFlyout></Page>""", encoding="utf-8")
        xaml_menu_counts(x, rows, "self")
        if not rows:
            print("SELFTEST FAIL: expected overflow on 6-item flyout")
            return 1
        rows.clear()
        x.write_text("""<Page><MenuFlyout>
            <MenuFlyoutSubItem Text="A"><MenuFlyoutItem Text="1"/><MenuFlyoutItem Text="2"/></MenuFlyoutSubItem>
            <MenuFlyoutItem Text="B"/>
        </MenuFlyout></Page>""", encoding="utf-8")
        xaml_menu_counts(x, rows, "self")
        if rows:
            print("SELFTEST FAIL: 2-item flyout flagged:", rows)
            return 1
    print("SELFTEST OK")
    return 0

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        sys.exit(self_test())
    root = Path(args.root) if args.root else repo_root()
    rows = []
    android_checks(root, rows)
    winui_checks(root, rows)
    gtk_checks(root, rows)
    adw_checks(root, rows)
    xp_checks(root, rows)
    if rows:
        print(f"ui_menu_limits: {len(rows)} violation(s)")
        for n, m in rows:
            print(f"  FAIL {n}: {m}")
        sys.exit(1)
    print("ui_menu_limits: OK (⋯ ≤5, Android bottom nav ≤4)")
    sys.exit(0)

if __name__ == "__main__":
    main()
