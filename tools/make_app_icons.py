#!/usr/bin/env python3
"""Generates the application icons (applications/*/data/<desktop id>.svg).

macOS Big Sur-style geometry — a continuous-corner "squircle" on the 1024 grid with a soft shadow — but
original artwork: no Apple icons are copied (see the design rules in CLAUDE.md).

    python3 tools/make_app_icons.py
"""
import math
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "applications")
ID = "io.github.egely1337.Mayonnaise."


def squircle(cx=512, cy=512, half=412, n=5.0, steps=256, ox=0.0, oy=0.0):
    """Superellipse |x|^n + |y|^n = 1, the continuous-corner shape of macOS icons."""
    pts = []
    for i in range(steps):
        t = 2 * math.pi * i / steps
        c, s = math.cos(t), math.sin(t)
        x = math.copysign(abs(c) ** (2 / n), c) * half
        y = math.copysign(abs(s) ** (2 / n), s) * half
        pts.append(f"{cx + x + ox:.1f},{cy + y + oy:.1f}")
    return "M" + " L".join(pts) + " Z"


def rrect(x, y, w, h, r):
    return (f"M{x + r},{y} H{x + w - r} A{r},{r} 0 0 1 {x + w},{y + r} V{y + h - r} "
            f"A{r},{r} 0 0 1 {x + w - r},{y + h} H{x + r} A{r},{r} 0 0 1 {x},{y + h - r} "
            f"V{y + r} A{r},{r} 0 0 1 {x + r},{y} Z")


def icon(defs, body, background):
    shape = squircle()
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
  <defs>
    <filter id="shadow" x="-20%" y="-20%" width="140%" height="140%">
      <feGaussianBlur in="SourceAlpha" stdDeviation="14"/>
      <feOffset dy="12"/>
      <feComponentTransfer><feFuncA type="linear" slope="0.35"/></feComponentTransfer>
      <feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge>
    </filter>
    <filter id="soft" x="-30%" y="-30%" width="160%" height="160%">
      <feGaussianBlur in="SourceAlpha" stdDeviation="10"/>
      <feOffset dy="10"/>
      <feComponentTransfer><feFuncA type="linear" slope="0.28"/></feComponentTransfer>
      <feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge>
    </filter>
    <clipPath id="shape"><path d="{shape}"/></clipPath>
    <linearGradient id="sheen" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#fff" stop-opacity="0.22"/>
      <stop offset="0.5" stop-color="#fff" stop-opacity="0"/>
    </linearGradient>
{defs}
  </defs>
  <g filter="url(#shadow)">
    <path d="{shape}" fill="{background}"/>
    <g clip-path="url(#shape)">
{body}
      <rect x="0" y="0" width="1024" height="1024" fill="url(#sheen)"/>
    </g>
    <path d="{shape}" fill="none" stroke="#000" stroke-opacity="0.12" stroke-width="3"/>
  </g>
</svg>
"""


def grad(id_, *stops, x2=0, y2=1):
    s = "".join(f'<stop offset="{o}" stop-color="{c}"/>' for o, c in stops)
    return f'    <linearGradient id="{id_}" x1="0" y1="0" x2="{x2}" y2="{y2}">{s}</linearGradient>\n'


def finder():
    defs = grad("bg", (0, "#7AD3FF"), (1, "#1A7FF0"))
    defs += grad("back", (0, "#D6EEFF"), (1, "#A9D6FF"))
    defs += grad("front", (0, "#FFFFFF"), (1, "#E2F1FF"))
    body = f"""      <g filter="url(#soft)">
        <path d="M232,330 Q232,290 272,290 H430 Q452,290 466,308 L494,344 H752 Q792,344 792,384 V700 Q792,740 752,740 H272 Q232,740 232,700 Z" fill="url(#back)"/>
        <path d="{rrect(214, 400, 596, 360, 44)}" fill="url(#front)"/>
        <path d="M300,560 Q512,650 724,560" fill="none" stroke="#5BB4FF" stroke-width="22" stroke-linecap="round" opacity="0.55"/>
      </g>"""
    return icon(defs, body, "url(#bg)")


def bezel(screen_defs, screen_body):
    """Light aluminium frame with a dark screen (Terminal, Task Manager)."""
    defs = grad("bg", (0, "#F2F2F4"), (1, "#B8B8BE"))
    defs += grad("screen", (0, "#2A2A2E"), (1, "#0E0E10"))
    defs += screen_defs
    body = f"""      <path d="{rrect(172, 190, 680, 610, 56)}" fill="#8A8A90" opacity="0.5"/>
      <path d="{rrect(180, 196, 664, 596, 50)}" fill="url(#screen)"/>
{screen_body}"""
    return icon(defs, body, "url(#bg)")


def terminal():
    body = """      <polyline points="262,330 352,400 262,470" fill="none" stroke="#F2F2F2" stroke-width="40" stroke-linecap="round" stroke-linejoin="round"/>
      <rect x="388" y="452" width="150" height="38" rx="12" fill="#F2F2F2"/>
      <rect x="262" y="560" width="360" height="22" rx="11" fill="#4CD964" opacity="0.55"/>
      <rect x="262" y="620" width="250" height="22" rx="11" fill="#FFFFFF" opacity="0.18"/>"""
    return bezel("", body)


def taskmanager():
    defs = grad("area", (0, "#34C759"), (1, "#34C759"))
    defs = defs.replace('stop-color="#34C759"/></linear', 'stop-color="#34C759" stop-opacity="0"/></linear')
    pts = [(200, 640), (260, 610), (320, 650), (380, 520), (430, 600), (480, 380), (530, 560), (580, 470),
           (640, 520), (700, 330), (760, 480), (824, 430)]
    line = " ".join(f"{x},{y}" for x, y in pts)
    area = f"M200,792 L{line.replace(' ', ' L')} L824,792 Z"
    grid = "".join(f'<line x1="180" y1="{y}" x2="844" y2="{y}" stroke="#fff" stroke-opacity="0.07" stroke-width="4"/>'
                   for y in range(300, 800, 100))
    grid += "".join(f'<line x1="{x}" y1="196" x2="{x}" y2="792" stroke="#fff" stroke-opacity="0.07" stroke-width="4"/>'
                    for x in range(280, 844, 100))
    body = f"""      {grid}
      <path d="{area}" fill="url(#area)" opacity="0.45"/>
      <polyline points="{line}" fill="none" stroke="#3EE06A" stroke-width="22" stroke-linejoin="round" stroke-linecap="round"/>"""
    return bezel(defs, body)


def photos():
    defs = grad("bg", (0, "#FFFFFF"), (1, "#E9E9EF"))
    defs += grad("sky", (0, "#4FB7FF"), (1, "#BFE7FF"))
    defs += grad("sky2", (0, "#FF9F6A"), (1, "#FFD9A0"))
    defs += grad("hill", (0, "#4CD964"), (1, "#1F9D45"))
    defs += grad("hill2", (0, "#2BB673"), (1, "#127A3E"))

    def card(rot, sky, sun=True):
        return f"""      <g transform="rotate({rot} 512 512)" filter="url(#soft)">
        <path d="{rrect(252, 292, 520, 440, 36)}" fill="#FFFFFF"/>
        <clipPath id="c{abs(rot)}"><path d="{rrect(280, 320, 464, 384, 22)}"/></clipPath>
        <g clip-path="url(#c{abs(rot)})">
          <rect x="280" y="320" width="464" height="384" fill="url(#{sky})"/>
          {'<circle cx="640" cy="420" r="52" fill="#FFD60A"/>' if sun else ''}
          <path d="M260,720 L420,520 L520,640 L600,560 L780,720 Z" fill="url(#hill2)"/>
          <path d="M260,720 Q420,600 560,660 T780,640 V720 Z" fill="url(#hill)"/>
        </g>
      </g>"""
    body = card(9, "sky2", False) + "\n" + card(-6, "sky")
    return icon(defs, body, "url(#bg)")


def videoplayer():
    defs = grad("bg", (0, "#3A3A40"), (1, "#0B0B0D"))
    defs += grad("disc", (0, "#FF5E7E"), (0.5, "#B45CFF"), (1, "#4B6BFF"), x2=1, y2=1)
    holes = "".join(f'<rect x="{x}" y="{y}" width="34" height="46" rx="8" fill="#fff" opacity="0.16"/>'
                    for x in (196, 794) for y in range(250, 780, 90))
    body = f"""      {holes}
      <circle cx="512" cy="512" r="250" fill="url(#disc)" filter="url(#soft)"/>
      <circle cx="512" cy="512" r="250" fill="none" stroke="#fff" stroke-opacity="0.25" stroke-width="6"/>
      <path d="M455,400 Q455,372 480,386 L640,490 Q664,505 640,520 L480,624 Q455,640 455,610 Z" fill="#FFFFFF"/>"""
    return icon(defs, body, "url(#bg)")


def gear_path(cx, cy, r_out, r_in, teeth, tooth_frac=0.42):
    pts = []
    for i in range(teeth):
        a0 = 2 * math.pi * i / teeth
        span = 2 * math.pi / teeth
        for a, r in ((a0, r_in), (a0 + span * (0.5 - tooth_frac / 2) * 0.9, r_in),
                     (a0 + span * (0.5 - tooth_frac / 2), r_out), (a0 + span * (0.5 + tooth_frac / 2), r_out),
                     (a0 + span * (0.5 + tooth_frac / 2) + span * 0.05, r_in)):
            pts.append(f"{cx + r * math.cos(a):.1f},{cy + r * math.sin(a):.1f}")
    return "M" + " L".join(pts) + " Z"


def settings():
    defs = grad("bg", (0, "#E5E5EA"), (1, "#8E8E93"))
    defs += grad("gear", (0, "#6E6E73"), (1, "#3A3A3C"))
    defs += grad("gear2", (0, "#AEAEB2"), (1, "#7C7C80"))
    body = f"""      <g filter="url(#soft)">
        <path d="{gear_path(512, 512, 330, 280, 18)}" fill="url(#gear)" fill-rule="evenodd"/>
        <circle cx="512" cy="512" r="236" fill="url(#gear2)"/>
        <path d="{gear_path(512, 512, 175, 140, 10, 0.5)}" fill="url(#gear)"/>
        <circle cx="512" cy="512" r="70" fill="url(#bg)"/>
      </g>"""
    return icon(defs, body, "url(#bg)")


ICONS = {
    "finder": ("Finder", finder),
    "terminal": ("Terminal", terminal),
    "task_manager": ("TaskManager", taskmanager),
    "photos": ("Photos", photos),
    "video_player": ("VideoPlayer", videoplayer),
    "system_settings": ("Settings", settings),
}

for folder, (name, fn) in ICONS.items():
    out = os.path.join(ROOT, folder, "data")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, ID + name + ".svg"), "w") as f:
        f.write(fn())
    print("wrote", os.path.join(folder, "data", ID + name + ".svg"))
