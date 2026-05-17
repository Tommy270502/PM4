from __future__ import annotations

import datetime as dt
import os
import struct
import zipfile
from pathlib import Path
from xml.sax.saxutils import escape


ROOT = Path(__file__).resolve().parent
OUT = ROOT / "Radar_Heart_Rate_Video_Presentation.pptx"
EMU_PER_INCH = 914400
SLIDE_W = 12192000
SLIDE_H = 6858000

NS_A = "http://schemas.openxmlformats.org/drawingml/2006/main"
NS_R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
NS_P = "http://schemas.openxmlformats.org/presentationml/2006/main"
NS_REL = "http://schemas.openxmlformats.org/package/2006/relationships"


def emu(inches: float) -> int:
    return int(round(inches * EMU_PER_INCH))


def xml(text: str) -> str:
    return escape(text, {"'": "&apos;", '"': "&quot;"})


def rels(items: list[tuple[str, str, str]]) -> str:
    body = "".join(
        f'<Relationship Id="{rid}" Type="{typ}" Target="{xml(target)}"/>'
        for rid, typ, target in items
    )
    return f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="{NS_REL}">{body}</Relationships>'


def png_dimensions(path: Path) -> tuple[int, int]:
    with path.open("rb") as f:
        sig = f.read(8)
        if sig != b"\x89PNG\r\n\x1a\n":
            raise ValueError(f"{path} is not a PNG")
        _length = struct.unpack(">I", f.read(4))[0]
        chunk = f.read(4)
        if chunk != b"IHDR":
            raise ValueError(f"{path} does not contain IHDR")
        width, height = struct.unpack(">II", f.read(8))
        return width, height


class ShapeBuilder:
    def __init__(self) -> None:
        self.parts: list[str] = []
        self.next_id = 2

    def _id(self) -> int:
        sid = self.next_id
        self.next_id += 1
        return sid

    def rect(
        self,
        x: int,
        y: int,
        w: int,
        h: int,
        fill: str = "FFFFFF",
        line: str | None = None,
        alpha: int = 100000,
        line_width: int = 12700,
        name: str = "Rectangle",
        preset: str = "rect",
    ) -> None:
        sid = self._id()
        line_xml = '<a:ln><a:noFill/></a:ln>'
        if line:
            line_xml = (
                f'<a:ln w="{line_width}"><a:solidFill><a:srgbClr val="{line}"/></a:solidFill></a:ln>'
            )
        self.parts.append(
            f"""
            <p:sp>
              <p:nvSpPr><p:cNvPr id="{sid}" name="{xml(name)}"/><p:cNvSpPr/><p:nvPr/></p:nvSpPr>
              <p:spPr>
                <a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{w}" cy="{h}"/></a:xfrm>
                <a:prstGeom prst="{preset}"><a:avLst/></a:prstGeom>
                <a:solidFill><a:srgbClr val="{fill}"><a:alpha val="{alpha}"/></a:srgbClr></a:solidFill>
                {line_xml}
              </p:spPr>
            </p:sp>
            """
        )

    def text(
        self,
        x: int,
        y: int,
        w: int,
        h: int,
        text: str | list[str],
        size: int = 24,
        color: str = "111827",
        bold: bool = False,
        align: str = "l",
        font: str = "Aptos",
        name: str = "Text",
        fill: str | None = None,
        margin_l: int = 0,
        margin_r: int = 0,
        margin_t: int = 0,
        margin_b: int = 0,
        alpha: int = 100000,
        line_spacing_pct: int = 105000,
    ) -> None:
        sid = self._id()
        paras = text if isinstance(text, list) else text.split("\n")
        run_bold = ' b="1"' if bold else ""
        fill_xml = "<a:noFill/>" if fill is None else (
            f'<a:solidFill><a:srgbClr val="{fill}"><a:alpha val="{alpha}"/></a:srgbClr></a:solidFill>'
        )
        p_xml = []
        for p in paras:
            p_xml.append(
                f"""
                <a:p>
                  <a:pPr algn="{align}"><a:lnSpc><a:spcPct val="{line_spacing_pct}"/></a:lnSpc></a:pPr>
                  <a:r>
                    <a:rPr lang="en-US" sz="{size * 100}"{run_bold} dirty="0">
                      <a:solidFill><a:srgbClr val="{color}"/></a:solidFill>
                      <a:latin typeface="{xml(font)}"/><a:ea typeface="{xml(font)}"/><a:cs typeface="{xml(font)}"/>
                    </a:rPr>
                    <a:t>{xml(p)}</a:t>
                  </a:r>
                  <a:endParaRPr lang="en-US" sz="{size * 100}">
                    <a:solidFill><a:srgbClr val="{color}"/></a:solidFill>
                    <a:latin typeface="{xml(font)}"/>
                  </a:endParaRPr>
                </a:p>
                """
            )
        self.parts.append(
            f"""
            <p:sp>
              <p:nvSpPr><p:cNvPr id="{sid}" name="{xml(name)}"/><p:cNvSpPr txBox="1"/><p:nvPr/></p:nvSpPr>
              <p:spPr>
                <a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{w}" cy="{h}"/></a:xfrm>
                <a:prstGeom prst="rect"><a:avLst/></a:prstGeom>
                {fill_xml}
                <a:ln><a:noFill/></a:ln>
              </p:spPr>
              <p:txBody>
                <a:bodyPr wrap="square" lIns="{margin_l}" rIns="{margin_r}" tIns="{margin_t}" bIns="{margin_b}"/>
                <a:lstStyle/>
                {''.join(p_xml)}
              </p:txBody>
            </p:sp>
            """
        )

    def picture(
        self,
        x: int,
        y: int,
        w: int,
        h: int,
        rid: str,
        path: Path,
        mode: str = "cover",
        name: str = "Picture",
    ) -> None:
        sid = self._id()
        crop = ""
        if mode == "contain":
            iw, ih = png_dimensions(path)
            area_ratio = w / h
            img_ratio = iw / ih
            if area_ratio > img_ratio:
                new_w = int(h * img_ratio)
                x = x + (w - new_w) // 2
                w = new_w
            else:
                new_h = int(w / img_ratio)
                y = y + (h - new_h) // 2
                h = new_h
        if mode == "cover":
            iw, ih = png_dimensions(path)
            area_ratio = w / h
            img_ratio = iw / ih
            if area_ratio > img_ratio:
                scale = w / iw
                scaled_h = ih * scale
                crop_amt = max(0, (scaled_h - h) / (2 * scaled_h))
                crop = f'<a:srcRect t="{int(crop_amt * 100000)}" b="{int(crop_amt * 100000)}"/>'
            else:
                scale = h / ih
                scaled_w = iw * scale
                crop_amt = max(0, (scaled_w - w) / (2 * scaled_w))
                crop = f'<a:srcRect l="{int(crop_amt * 100000)}" r="{int(crop_amt * 100000)}"/>'
        self.parts.append(
            f"""
            <p:pic>
              <p:nvPicPr><p:cNvPr id="{sid}" name="{xml(name)}"/><p:cNvPicPr><a:picLocks noChangeAspect="1"/></p:cNvPicPr><p:nvPr/></p:nvPicPr>
              <p:blipFill><a:blip r:embed="{rid}"/>{crop}<a:stretch><a:fillRect/></a:stretch></p:blipFill>
              <p:spPr>
                <a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{w}" cy="{h}"/></a:xfrm>
                <a:prstGeom prst="rect"><a:avLst/></a:prstGeom>
                <a:ln><a:noFill/></a:ln>
              </p:spPr>
            </p:pic>
            """
        )

    def line(
        self,
        x1: int,
        y1: int,
        x2: int,
        y2: int,
        color: str = "111827",
        width: int = 19050,
        arrow: bool = False,
        name: str = "Line",
    ) -> None:
        sid = self._id()
        x = min(x1, x2)
        y = min(y1, y2)
        w = abs(x2 - x1)
        h = abs(y2 - y1)
        flip_h = ' flipH="1"' if x2 < x1 else ""
        flip_v = ' flipV="1"' if y2 < y1 else ""
        arrow_xml = '<a:tailEnd type="triangle"/>' if arrow else ""
        self.parts.append(
            f"""
            <p:cxnSp>
              <p:nvCxnSpPr><p:cNvPr id="{sid}" name="{xml(name)}"/><p:cNvCxnSpPr/><p:nvPr/></p:nvCxnSpPr>
              <p:spPr>
                <a:xfrm{flip_h}{flip_v}><a:off x="{x}" y="{y}"/><a:ext cx="{w}" cy="{h}"/></a:xfrm>
                <a:prstGeom prst="line"><a:avLst/></a:prstGeom>
                <a:ln w="{width}" cap="round"><a:solidFill><a:srgbClr val="{color}"/></a:solidFill>{arrow_xml}</a:ln>
              </p:spPr>
            </p:cxnSp>
            """
        )

    def triangle(
        self,
        x: int,
        y: int,
        w: int,
        h: int,
        fill: str = "FFFFFF",
        alpha: int = 100000,
        rotation: int = 0,
        name: str = "Triangle",
    ) -> None:
        sid = self._id()
        rot_xml = f' rot="{rotation}"' if rotation else ""
        self.parts.append(
            f"""
            <p:sp>
              <p:nvSpPr><p:cNvPr id="{sid}" name="{xml(name)}"/><p:cNvSpPr/><p:nvPr/></p:nvSpPr>
              <p:spPr>
                <a:xfrm{rot_xml}><a:off x="{x}" y="{y}"/><a:ext cx="{w}" cy="{h}"/></a:xfrm>
                <a:prstGeom prst="triangle"><a:avLst/></a:prstGeom>
                <a:solidFill><a:srgbClr val="{fill}"><a:alpha val="{alpha}"/></a:srgbClr></a:solidFill>
                <a:ln><a:noFill/></a:ln>
              </p:spPr>
            </p:sp>
            """
        )


def slide_xml(builder: ShapeBuilder, bg: str = "FFFFFF") -> str:
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:sld xmlns:a="{NS_A}" xmlns:r="{NS_R}" xmlns:p="{NS_P}">
  <p:cSld>
    <p:bg><p:bgPr><a:solidFill><a:srgbClr val="{bg}"/></a:solidFill><a:effectLst/></p:bgPr></p:bg>
    <p:spTree>
      <p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>
      <p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr>
      {''.join(builder.parts)}
    </p:spTree>
  </p:cSld>
  <p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>
</p:sld>"""


def notes_xml(notes: str, slide_no: int) -> str:
    paras = []
    for line in notes.split("\n"):
        paras.append(
            f"""
            <a:p>
              <a:pPr algn="l"/>
              <a:r><a:rPr lang="en-US" sz="1400"><a:solidFill><a:srgbClr val="111827"/></a:solidFill><a:latin typeface="Aptos"/></a:rPr><a:t>{xml(line)}</a:t></a:r>
              <a:endParaRPr lang="en-US" sz="1400"/>
            </a:p>
            """
        )
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:notes xmlns:a="{NS_A}" xmlns:r="{NS_R}" xmlns:p="{NS_P}">
  <p:cSld>
    <p:spTree>
      <p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>
      <p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr>
      <p:sp>
        <p:nvSpPr><p:cNvPr id="2" name="Slide Image Placeholder"/><p:cNvSpPr><a:spLocks noGrp="1" noRot="1" noChangeAspect="1"/></p:cNvSpPr><p:nvPr><p:ph type="sldImg"/></p:nvPr></p:nvSpPr>
        <p:spPr/>
      </p:sp>
      <p:sp>
        <p:nvSpPr><p:cNvPr id="3" name="Notes Placeholder"/><p:cNvSpPr><a:spLocks noGrp="1"/></p:cNvSpPr><p:nvPr><p:ph type="body" idx="1"/></p:nvPr></p:nvSpPr>
        <p:spPr/>
        <p:txBody><a:bodyPr/><a:lstStyle/>{''.join(paras)}</p:txBody>
      </p:sp>
      <p:sp>
        <p:nvSpPr><p:cNvPr id="4" name="Slide Number Placeholder"/><p:cNvSpPr><a:spLocks noGrp="1"/></p:cNvSpPr><p:nvPr><p:ph type="sldNum" sz="quarter" idx="5"/></p:nvPr></p:nvSpPr>
        <p:spPr/>
        <p:txBody><a:bodyPr/><a:lstStyle/><a:p><a:r><a:rPr lang="en-US" sz="1000"/><a:t>{slide_no}</a:t></a:r></a:p></p:txBody>
      </p:sp>
    </p:spTree>
  </p:cSld>
  <p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>
</p:notes>"""


def add_header(b: ShapeBuilder, title: str, timecode: str, dark: bool = False) -> None:
    color = "FFFFFF" if dark else "111827"
    sub = "D1D5DB" if dark else "6B7280"
    b.text(emu(0.55), emu(0.34), emu(9.7), emu(0.45), title, 19, color, True)
    b.text(emu(11.1), emu(0.37), emu(1.7), emu(0.32), timecode, 11, sub, False, "r")
    b.rect(emu(0.55), emu(0.86), emu(12.25), emu(0.015), "00A6D6" if dark else "005EB8", alpha=100000)


def add_footer(b: ShapeBuilder, slide_no: int, dark: bool = False) -> None:
    b.text(
        emu(11.95),
        emu(7.07),
        emu(0.75),
        emu(0.2),
        f"{slide_no:02d}/14",
        8,
        "CBD5E1" if dark else "9CA3AF",
        False,
        "r",
    )


def video_placeholder(
    b: ShapeBuilder,
    x: int,
    y: int,
    w: int,
    h: int,
    title: str,
    detail: str,
    label: str = "VIDEO PLACEHOLDER",
) -> None:
    b.rect(x, y, w, h, "111827", line="00A6D6", alpha=100000, line_width=25400)
    b.rect(x + emu(0.14), y + emu(0.14), w - emu(0.28), h - emu(0.28), "1F2937", alpha=100000)
    b.triangle(x + w // 2 - emu(0.22), y + h // 2 - emu(0.3), emu(0.5), emu(0.6), "FFFFFF", 90000, rotation=5400000)
    b.text(x + emu(0.35), y + emu(0.28), w - emu(0.7), emu(0.28), label, 11, "93C5FD", True, "c")
    b.text(x + emu(0.45), y + h - emu(0.9), w - emu(0.9), emu(0.32), title, 16, "FFFFFF", True, "c")
    b.text(x + emu(0.45), y + h - emu(0.53), w - emu(0.9), emu(0.24), detail, 9, "CBD5E1", False, "c")


def label_box(b: ShapeBuilder, x: float, y: float, text: str, fill: str, color: str = "FFFFFF") -> None:
    b.rect(emu(x), emu(y), emu(2.15), emu(0.48), fill, alpha=100000)
    b.text(emu(x + 0.12), emu(y + 0.13), emu(1.91), emu(0.2), text, 12, color, True, "c")


def build_slides() -> tuple[list[str], list[str], dict[str, Path], list[list[tuple[str, str]]]]:
    img = ROOT / "img"
    media_paths = {
        "holter": img / "holter_extended.png",
        "sleeping": img / "sleeping_extended.png",
        "monitoring": img / "monitoring_extended.png",
        "pcb": img / "pcb with stand.png",
        "radar": img / "radar beam 1.png",
    }

    slides: list[str] = []
    notes: list[str] = []
    slide_rels: list[list[tuple[str, str]]] = []

    def finish(b: ShapeBuilder, note: str, rel_items: list[tuple[str, str]] | None = None, bg: str = "FFFFFF") -> None:
        slides.append(slide_xml(b, bg))
        notes.append(note)
        slide_rels.append(rel_items or [])

    # 1
    b = ShapeBuilder()
    b.rect(0, 0, SLIDE_W, SLIDE_H, "0B1220")
    b.rect(emu(0.55), emu(0.45), emu(1.45), emu(0.08), "00A6D6")
    b.text(emu(0.55), emu(1.85), emu(9.7), emu(1.45), "Contactless Radar-Based\nHeart Rate Monitoring Device", 34, "FFFFFF", True)
    b.text(emu(0.58), emu(3.55), emu(5.3), emu(0.35), "ZHAW PM4 Electronics Project", 18, "CBD5E1")
    b.text(emu(0.58), emu(6.65), emu(5.5), emu(0.25), "Thomas Perri | Bogdans Grebnevs", 10, "94A3B8")
    b.rect(emu(9.55), emu(1.1), emu(2.7), emu(4.95), "111827", line="1F9ACF", alpha=100000, line_width=19050)
    for i in range(9):
        y = emu(1.65 + i * 0.46)
        x1 = emu(9.85)
        x2 = emu(11.95)
        b.line(x1, y, x2, y + (emu(0.18) if i % 2 else -emu(0.18)), "00A6D6", 9525)
    add_footer(b, 1, True)
    finish(b, "No voiceover. Static title slide with minimal project title and ZHAW PM4 label.", bg="0B1220")

    # 2
    b = ShapeBuilder()
    b.picture(0, 0, SLIDE_W, SLIDE_H, "rId2", media_paths["holter"])
    b.rect(0, 0, SLIDE_W, SLIDE_H, "000000", alpha=47000)
    add_header(b, "Problem", "0:07-0:18", True)
    b.text(emu(0.75), emu(1.65), emu(6.1), emu(1.0), "Heart rate.\nStill contact-based.", 36, "FFFFFF", True)
    label_box(b, 0.78, 4.45, "Electrodes", "005EB8")
    label_box(b, 3.2, 4.45, "Straps", "00A6D6")
    label_box(b, 5.62, 4.45, "Wearables", "F59E0B", "111827")
    add_footer(b, 2, True)
    finish(
        b,
        "Heart rate tells a lot. But measuring it often still means contact: electrodes, straps, watches, or wearable sensors.",
        [("rId2", "media/image1.png")],
        bg="111827",
    )

    # 3
    b = ShapeBuilder()
    b.picture(0, 0, SLIDE_W, SLIDE_H, "rId2", media_paths["sleeping"])
    b.rect(0, 0, SLIDE_W, SLIDE_H, "000000", alpha=42000)
    add_header(b, "Need", "0:19-0:32", True)
    b.text(emu(0.75), emu(1.55), emu(6.8), emu(0.85), "The ideal measurement is...", 30, "FFFFFF", True)
    for idx, (word, col) in enumerate([("comfortable", "00A6D6"), ("continuous", "FFFFFF"), ("contact-free", "F59E0B")]):
        b.rect(emu(0.78 + idx * 2.7), emu(4.85), emu(2.3), emu(0.6), col, alpha=90000)
        b.text(emu(0.86 + idx * 2.7), emu(5.04), emu(2.14), emu(0.22), word, 13, "111827" if col != "FFFFFF" else "111827", True, "c")
    add_footer(b, 3, True)
    finish(
        b,
        "For sleep, long-term monitoring, or sensitive users, contact can become the limitation. The ideal measurement is comfortable, continuous, and almost invisible.",
        [("rId2", "media/image2.png")],
        bg="111827",
    )

    # 4
    b = ShapeBuilder()
    b.rect(0, 0, SLIDE_W, SLIDE_H, "F8FAFC")
    add_header(b, "Device Concept", "0:33-0:48")
    b.picture(emu(6.55), emu(1.15), emu(6.25), emu(5.35), "rId2", media_paths["pcb"])
    b.rect(emu(6.55), emu(1.15), emu(6.25), emu(5.35), "000000", alpha=8000)
    b.text(emu(0.75), emu(1.55), emu(5.2), emu(1.05), "Detects chest movement\nwithout touching the body.", 29, "111827", True)
    for i, (item, col) in enumerate([("Contactless", "005EB8"), ("Real-time", "00A6D6"), ("LCD Display", "F59E0B")]):
        y = 3.35 + i * 0.7
        b.rect(emu(0.8), emu(y), emu(0.16), emu(0.16), col)
        b.text(emu(1.08), emu(y - 0.06), emu(3.6), emu(0.28), item, 18, "111827", True)
    add_footer(b, 4)
    finish(
        b,
        "This device detects heart-rate-related chest movement without touching the body. It processes the radar signal in real time on an STM32 microcontroller and shows the result directly on an integrated display.",
        [("rId2", "media/image3.png")],
    )

    # 5
    b = ShapeBuilder()
    b.rect(0, 0, SLIDE_W, SLIDE_H, "0B1220")
    add_header(b, "Doppler Principle", "0:49-1:05", True)
    b.picture(emu(0.65), emu(1.2), emu(5.65), emu(4.85), "rId2", media_paths["radar"], mode="contain")
    b.rect(emu(6.75), emu(1.5), emu(0.7), emu(3.2), "94A3B8", alpha=70000)
    b.text(emu(6.48), emu(4.95), emu(1.25), emu(0.25), "Chest", 12, "CBD5E1", True, "c")
    for i in range(4):
        b.line(emu(3.5 + i * 0.25), emu(2.15 + i * 0.22), emu(6.65), emu(2.65), "00A6D6", 25400, True)
        b.line(emu(6.65), emu(3.05), emu(3.65 + i * 0.25), emu(3.7 + i * 0.18), "F59E0B", 19050, True)
    b.text(emu(8.0), emu(1.55), emu(4.3), emu(0.65), "Small cardiac movements\nshift the reflected phase.", 24, "FFFFFF", True)
    b.rect(emu(8.1), emu(3.55), emu(3.45), emu(0.015), "CBD5E1", alpha=65000)
    points = [(8.1, 3.55), (8.45, 3.25), (8.8, 3.85), (9.15, 3.25), (9.5, 3.82), (9.85, 3.34), (10.2, 3.62), (10.55, 3.47), (10.9, 3.52), (11.25, 3.43)]
    for (x1, y1), (x2, y2) in zip(points, points[1:]):
        b.line(emu(x1), emu(y1), emu(x2), emu(y2), "00A6D6", 19050)
    b.text(emu(8.1), emu(4.15), emu(3.8), emu(0.28), "Measurable heartbeat pattern", 13, "CBD5E1")
    add_footer(b, 5, True)
    finish(
        b,
        "The principle is Doppler sensing. A microwave radar module sends a signal toward the chest. Small movements caused by cardiac activity change the phase of the reflected signal, creating a measurable heartbeat pattern.",
        [("rId2", "media/image4.png")],
        bg="0B1220",
    )

    # 6
    b = ShapeBuilder()
    b.rect(0, 0, SLIDE_W, SLIDE_H, "FFFFFF")
    add_header(b, "System Architecture", "1:06-1:22")
    b.text(emu(0.75), emu(1.25), emu(8.4), emu(0.42), "Radar front end to embedded visualization", 23, "111827", True)
    boxes = [
        (0.85, "K-LC5\nRadar"),
        (3.45, "Analog\nFront End"),
        (6.05, "STM32F429\nDiscovery"),
        (8.65, "LCD\nTouchscreen"),
        (11.0, "OpenLog\nSD Card"),
    ]
    for x, txt in boxes:
        fill = "EFF6FF" if x != 11.0 else "FEF3C7"
        line = "005EB8" if x != 11.0 else "F59E0B"
        b.rect(emu(x), emu(2.45), emu(1.65), emu(1.05), fill, line=line, alpha=100000, line_width=19050)
        b.text(emu(x + 0.12), emu(2.73), emu(1.41), emu(0.4), txt, 14, "111827", True, "c")
    for x in [2.52, 5.12, 7.72, 10.28]:
        b.line(emu(x), emu(2.97), emu(x + 0.72), emu(2.97), "005EB8", 25400, True)
    b.text(emu(0.85), emu(4.55), emu(4.3), emu(0.34), "I/Q acquisition", 16, "005EB8", True)
    b.text(emu(4.25), emu(4.55), emu(4.2), emu(0.34), "Real-time processing", 16, "00A6D6", True)
    b.text(emu(7.8), emu(4.55), emu(4.3), emu(0.34), "Touch UI + logging", 16, "F59E0B", True)
    add_footer(b, 6)
    finish(
        b,
        "Inside the system, the K-LC5 radar module is connected to an analog front end and an STM32 Discovery board. The microcontroller acquires the I and Q radar channels, processes them, and drives the LCD touchscreen.",
    )

    # 7
    b = ShapeBuilder()
    b.rect(0, 0, SLIDE_W, SLIDE_H, "F8FAFC")
    add_header(b, "Standalone Demonstration", "1:23-1:39")
    video_placeholder(
        b,
        emu(0.75),
        emu(1.35),
        emu(8.25),
        emu(4.85),
        "Live board operation",
        "Insert clip: STM32 + LCD running without external PC",
    )
    b.text(emu(9.45), emu(1.55), emu(2.9), emu(0.52), "Show in this clip", 19, "111827", True)
    for i, item in enumerate(["Standalone operation", "Heart-rate display", "OpenLog close-up", "SD card logging"]):
        y = 2.35 + i * 0.62
        b.rect(emu(9.52), emu(y), emu(0.18), emu(0.18), "00A6D6" if i < 2 else "F59E0B")
        b.text(emu(9.85), emu(y - 0.07), emu(2.55), emu(0.26), item, 13, "111827", True)
    add_footer(b, 7)
    finish(
        b,
        "The complete processing chain runs directly on the embedded platform. No external PC is required during operation, while heart-rate data can also be logged to an SD card with the integrated OpenLog module for later analysis.",
    )

    # 8
    b = ShapeBuilder()
    b.rect(0, 0, SLIDE_W, SLIDE_H, "FFFFFF")
    add_header(b, "Processing Window", "1:40-1:55")
    b.text(emu(0.75), emu(1.3), emu(6.8), emu(0.45), "Sampling and analysis timing", 24, "111827", True)
    stats = [("100 Hz", "sampling"), ("512", "samples/channel"), ("5.12 s", "window length"), ("2.56 s", "update interval")]
    for i, (big, small) in enumerate(stats):
        x = 0.8 + i * 3.05
        b.rect(emu(x), emu(2.05), emu(2.35), emu(1.25), "EFF6FF", line="005EB8", line_width=19050)
        b.text(emu(x + 0.12), emu(2.33), emu(2.1), emu(0.38), big, 25, "005EB8", True, "c")
        b.text(emu(x + 0.12), emu(2.86), emu(2.1), emu(0.23), small, 11, "111827", False, "c")
    b.rect(emu(1.05), emu(4.55), emu(5.25), emu(0.42), "E0F2FE", line="00A6D6")
    b.rect(emu(3.7), emu(5.15), emu(5.25), emu(0.42), "E0F2FE", line="00A6D6")
    b.line(emu(1.05), emu(5.9), emu(9.35), emu(5.9), "64748B", 12700, True)
    b.text(emu(1.05), emu(4.18), emu(3.2), emu(0.24), "50% overlap", 12, "005EB8", True)
    b.text(emu(0.95), emu(6.1), emu(8.6), emu(0.25), "New result every 2.56 seconds while using 5.12 seconds of signal data", 12, "475569")
    add_footer(b, 8)
    finish(
        b,
        "The radar signals are sampled at 100 hertz. Each analysis window contains 512 samples per channel, equal to 5.12 seconds of data, with updates every 2.56 seconds using 50 percent overlap.",
    )

    # 9
    b = ShapeBuilder()
    b.rect(0, 0, SLIDE_W, SLIDE_H, "F8FAFC")
    add_header(b, "Signal Analysis UI", "1:56-2:12")
    video_placeholder(
        b,
        emu(0.75),
        emu(1.25),
        emu(8.35),
        emu(5.0),
        "Time signal, FFT spectrum, peak detection",
        "Insert UI recording; show filter/effect menu for 1-2 seconds",
    )
    b.rect(emu(9.55), emu(1.58), emu(2.55), emu(0.55), "005EB8")
    b.text(emu(9.72), emu(1.77), emu(2.2), emu(0.18), "I/Q signals", 12, "FFFFFF", True, "c")
    b.line(emu(10.82), emu(2.18), emu(10.82), emu(2.78), "005EB8", 19050, True)
    b.rect(emu(9.55), emu(2.85), emu(2.55), emu(0.55), "00A6D6")
    b.text(emu(9.72), emu(3.04), emu(2.2), emu(0.18), "FFT spectrum", 12, "111827", True, "c")
    b.line(emu(10.82), emu(3.45), emu(10.82), emu(4.05), "005EB8", 19050, True)
    b.rect(emu(9.55), emu(4.12), emu(2.55), emu(0.55), "F59E0B")
    b.text(emu(9.72), emu(4.31), emu(2.2), emu(0.18), "Dominant peak", 12, "111827", True, "c")
    add_footer(b, 9)
    finish(
        b,
        "From the sampled I and Q signals, the system computes the frequency spectrum and detects the dominant heart-rate peak. The interface also provides analysis tools such as selectable filter modes for testing and development.",
    )

    # 10
    b = ShapeBuilder()
    b.rect(0, 0, SLIDE_W, SLIDE_H, "FFFFFF")
    add_header(b, "Interface Walkthrough", "2:13-2:30")
    video_placeholder(
        b,
        emu(3.95),
        emu(1.25),
        emu(8.25),
        emu(5.0),
        "Touchscreen page walkthrough",
        "Insert UI recording: Time Signal, FFT Spectr, Peak Detect, EKG BPM",
    )
    pages = ["Time Signal", "FFT Spectr", "Peak Detect", "EKG BPM", "Log file insert"]
    for i, page in enumerate(pages):
        y = 1.5 + i * 0.76
        b.rect(emu(0.85), emu(y), emu(2.45), emu(0.5), "EFF6FF" if i < 4 else "FEF3C7", line="005EB8" if i < 4 else "F59E0B")
        b.text(emu(1.02), emu(y + 0.16), emu(2.1), emu(0.18), page, 12, "111827", True, "c")
    add_footer(b, 10)
    finish(
        b,
        "The interface makes the signal processing visible: time signal, FFT spectrum, peak-frequency readout, ECG monitor page for reference comparison, and logged heart-rate data for offline evaluation.",
    )

    # 11
    b = ShapeBuilder()
    b.rect(0, 0, SLIDE_W, SLIDE_H, "F8FAFC")
    add_header(b, "Debugging and Validation", "2:31-2:43")
    video_placeholder(
        b,
        emu(0.8),
        emu(1.3),
        emu(7.15),
        emu(4.9),
        "Zoomed UI details",
        "Insert clip: spectrum peak plus ECG BPM reference page",
    )
    b.text(emu(8.5), emu(1.55), emu(3.7), emu(0.75), "User sees the result\nand the intermediate data.", 23, "111827", True)
    for i, item in enumerate(["BPM estimate", "Time signal", "FFT spectrum", "Peak position", "ECG comparison"]):
        y = 3.0 + i * 0.52
        b.rect(emu(8.58), emu(y), emu(0.16), emu(0.16), "005EB8" if i < 3 else "F59E0B")
        b.text(emu(8.9), emu(y - 0.06), emu(2.65), emu(0.22), item, 12, "111827", True)
    add_footer(b, 11)
    finish(
        b,
        "This allows the user to see both the final heart-rate estimate and the intermediate signal and spectrum, supporting debugging, validation, and further development.",
    )

    # 12
    b = ShapeBuilder()
    b.rect(0, 0, SLIDE_W, SLIDE_H, "0B1220")
    add_header(b, "Technical Summary", "2:44-2:55", True)
    items = [("Range", "up to about 1 m"), ("Spectrum", "+/- 4 Hz display"), ("Validation", "ECG comparison"), ("Logging", "SD card via OpenLog")]
    for i, (title, value) in enumerate(items):
        x = 0.85 + (i % 2) * 6.0
        y = 1.65 + (i // 2) * 2.05
        b.rect(emu(x), emu(y), emu(5.2), emu(1.35), "111827", line="00A6D6" if i != 2 else "F59E0B", line_width=19050)
        b.text(emu(x + 0.25), emu(y + 0.22), emu(4.7), emu(0.24), title, 13, "94A3B8", True)
        b.text(emu(x + 0.25), emu(y + 0.68), emu(4.7), emu(0.38), value, 22, "FFFFFF", True)
    add_footer(b, 12, True)
    finish(
        b,
        "The device targets short-range operation up to about one meter. The displayed spectrum covers approximately plus or minus four hertz, with ECG comparison used for validation.",
        bg="0B1220",
    )

    # 13
    b = ShapeBuilder()
    b.rect(0, 0, SLIDE_W, SLIDE_H, "FFFFFF")
    add_header(b, "FFT Resolution and Interpolation", "2:56-3:16")
    b.text(emu(0.85), emu(1.25), emu(5.2), emu(0.52), "Peak is refined between raw FFT bins", 22, "111827", True)
    base_x = 1.0
    base_y = 5.55
    bin_w = 0.55
    heights = [0.35, 0.55, 0.9, 1.55, 2.35, 1.7, 1.0, 0.62, 0.4]
    for i, h in enumerate(heights):
        fill = "F59E0B" if i in [4, 5] else "93C5FD"
        b.rect(emu(base_x + i * 0.62), emu(base_y - h), emu(bin_w), emu(h), fill, alpha=100000)
    b.line(emu(base_x - 0.2), emu(base_y), emu(base_x + 5.7), emu(base_y), "334155", 12700, True)
    b.line(emu(base_x - 0.2), emu(base_y), emu(base_x - 0.2), emu(2.65), "334155", 12700, True)
    curve = [(3.0, 4.15), (3.28, 3.62), (3.56, 3.32), (3.84, 3.25), (4.12, 3.42), (4.4, 3.82)]
    for (x1, y1), (x2, y2) in zip(curve, curve[1:]):
        b.line(emu(x1), emu(y1), emu(x2), emu(y2), "DC2626", 25400)
    b.text(emu(1.15), emu(5.9), emu(5.0), emu(0.22), "Frequency bins", 11, "475569", False, "c")
    b.rect(emu(7.3), emu(1.55), emu(4.4), emu(0.72), "EFF6FF", line="005EB8")
    b.text(emu(7.55), emu(1.82), emu(3.9), emu(0.2), "0.195 Hz bin spacing", 15, "005EB8", True, "c")
    b.rect(emu(7.3), emu(2.65), emu(4.4), emu(0.72), "EFF6FF", line="005EB8")
    b.text(emu(7.55), emu(2.92), emu(3.9), emu(0.2), "11.7 bpm per bin", 15, "005EB8", True, "c")
    b.rect(emu(7.3), emu(3.75), emu(4.4), emu(0.72), "FEF3C7", line="F59E0B")
    b.text(emu(7.55), emu(4.02), emu(3.9), emu(0.2), "sub-bin interpolation", 15, "111827", True, "c")
    b.text(emu(7.35), emu(5.0), emu(4.3), emu(0.62), "Accuracy still depends on signal quality, leakage, motion artifacts, and temporal stability.", 13, "475569")
    add_footer(b, 13)
    finish(
        b,
        "The FFT bin spacing is approximately 0.195 hertz, or 11.7 beats per minute. However, parabolic peak interpolation refines the peak position, so the BPM output is not limited to raw FFT-bin steps. Practical accuracy still depends on signal quality, leakage, motion artifacts, and temporal stability.",
    )

    # 14
    b = ShapeBuilder()
    b.rect(0, 0, SLIDE_W, SLIDE_H, "0B1220")
    add_header(b, "Contactless Radar-Based Heart Rate Monitoring Device", "3:17-3:27", True)
    b.text(emu(0.75), emu(1.35), emu(7.6), emu(0.55), "Compact platform for comfortable vital-sign monitoring", 24, "FFFFFF", True)
    summary = [
        "Contactless heart-rate monitoring",
        "Short-range operation: up to about 1 m",
        "Real-time STM32F429 processing",
        "Touch UI: time, FFT, peak, ECG pages",
        "FFT bin spacing: about 0.195 Hz",
        "Accuracy: currently under validation against reference ECG",
    ]
    for i, item in enumerate(summary):
        y = 2.15 + i * 0.45
        b.rect(emu(0.85), emu(y), emu(0.14), emu(0.14), "00A6D6" if i < 5 else "F59E0B")
        b.text(emu(1.12), emu(y - 0.07), emu(7.0), emu(0.23), item, 12, "CBD5E1" if i < 5 else "FDE68A", True)
    b.rect(emu(8.75), emu(1.65), emu(3.2), emu(3.2), "111827", line="00A6D6", line_width=19050)
    b.text(emu(9.05), emu(2.55), emu(2.6), emu(0.72), "QR / repo\nplaceholder", 23, "FFFFFF", True, "c")
    b.text(emu(8.75), emu(5.22), emu(3.2), emu(0.25), "Replace with repository or demo link", 9, "94A3B8", False, "c")
    b.text(emu(0.85), emu(6.35), emu(6.5), emu(0.26), "Authors: Thomas Perri, Bogdans Grebnevs", 11, "CBD5E1", True)
    b.text(emu(0.85), emu(6.68), emu(6.5), emu(0.22), "Date: add recording or submission date", 9, "94A3B8")
    add_footer(b, 14, True)
    finish(
        b,
        "Contactless sensing, real-time embedded processing, touchscreen visualization, ECG-supported validation, and SD-card logging come together in one compact platform for comfortable vital-sign monitoring.",
        bg="0B1220",
    )

    return slides, notes, media_paths, slide_rels


def presentation_xml(num_slides: int) -> str:
    slide_ids = "".join(f'<p:sldId id="{255 + i}" r:id="rId{1 + i}"/>' for i in range(1, num_slides + 1))
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:presentation xmlns:a="{NS_A}" xmlns:r="{NS_R}" xmlns:p="{NS_P}" saveSubsetFonts="1">
  <p:sldMasterIdLst><p:sldMasterId id="2147483648" r:id="rId1"/></p:sldMasterIdLst>
  <p:notesMasterIdLst><p:notesMasterId r:id="rId{num_slides + 2}"/></p:notesMasterIdLst>
  <p:sldIdLst>{slide_ids}</p:sldIdLst>
  <p:sldSz cx="{SLIDE_W}" cy="{SLIDE_H}" type="wide"/>
  <p:notesSz cx="6858000" cy="9144000"/>
  <p:defaultTextStyle>
    <a:defPPr><a:defRPr lang="en-US"/></a:defPPr>
    <a:lvl1pPr marL="0" algn="l" defTabSz="914400"><a:defRPr sz="1800" kern="1200"><a:solidFill><a:schemeClr val="tx1"/></a:solidFill><a:latin typeface="+mn-lt"/></a:defRPr></a:lvl1pPr>
  </p:defaultTextStyle>
</p:presentation>"""


def slide_master_xml() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:sldMaster xmlns:a="{NS_A}" xmlns:r="{NS_R}" xmlns:p="{NS_P}">
  <p:cSld><p:spTree>
    <p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>
    <p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr>
  </p:spTree></p:cSld>
  <p:clrMap bg1="lt1" tx1="dk1" bg2="lt2" tx2="dk2" accent1="accent1" accent2="accent2" accent3="accent3" accent4="accent4" accent5="accent5" accent6="accent6" hlink="hlink" folHlink="folHlink"/>
  <p:sldLayoutIdLst><p:sldLayoutId id="2147483649" r:id="rId1"/></p:sldLayoutIdLst>
  <p:txStyles><p:titleStyle/><p:bodyStyle/><p:otherStyle/></p:txStyles>
</p:sldMaster>"""


def slide_layout_xml() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:sldLayout xmlns:a="{NS_A}" xmlns:r="{NS_R}" xmlns:p="{NS_P}" type="blank" preserve="1">
  <p:cSld name="Blank"><p:spTree>
    <p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>
    <p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr>
  </p:spTree></p:cSld>
  <p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>
</p:sldLayout>"""


def notes_master_xml() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:notesMaster xmlns:a="{NS_A}" xmlns:r="{NS_R}" xmlns:p="{NS_P}">
  <p:cSld><p:spTree>
    <p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>
    <p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr>
  </p:spTree></p:cSld>
  <p:clrMap bg1="lt1" tx1="dk1" bg2="lt2" tx2="dk2" accent1="accent1" accent2="accent2" accent3="accent3" accent4="accent4" accent5="accent5" accent6="accent6" hlink="hlink" folHlink="folHlink"/>
</p:notesMaster>"""


def theme_xml() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<a:theme xmlns:a="{NS_A}" name="Radar Presentation Theme">
  <a:themeElements>
    <a:clrScheme name="Radar">
      <a:dk1><a:srgbClr val="111827"/></a:dk1>
      <a:lt1><a:srgbClr val="FFFFFF"/></a:lt1>
      <a:dk2><a:srgbClr val="0B1220"/></a:dk2>
      <a:lt2><a:srgbClr val="F8FAFC"/></a:lt2>
      <a:accent1><a:srgbClr val="005EB8"/></a:accent1>
      <a:accent2><a:srgbClr val="00A6D6"/></a:accent2>
      <a:accent3><a:srgbClr val="F59E0B"/></a:accent3>
      <a:accent4><a:srgbClr val="10B981"/></a:accent4>
      <a:accent5><a:srgbClr val="DC2626"/></a:accent5>
      <a:accent6><a:srgbClr val="64748B"/></a:accent6>
      <a:hlink><a:srgbClr val="005EB8"/></a:hlink>
      <a:folHlink><a:srgbClr val="7C3AED"/></a:folHlink>
    </a:clrScheme>
    <a:fontScheme name="Aptos">
      <a:majorFont><a:latin typeface="Aptos Display"/><a:ea typeface=""/><a:cs typeface=""/></a:majorFont>
      <a:minorFont><a:latin typeface="Aptos"/><a:ea typeface=""/><a:cs typeface=""/></a:minorFont>
    </a:fontScheme>
    <a:fmtScheme name="Radar">
      <a:fillStyleLst><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:gradFill rotWithShape="1"><a:gsLst><a:gs pos="0"><a:schemeClr val="phClr"/></a:gs><a:gs pos="100000"><a:schemeClr val="phClr"/></a:gs></a:gsLst><a:lin ang="5400000" scaled="0"/></a:gradFill><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:fillStyleLst>
      <a:lnStyleLst><a:ln w="9525" cap="flat" cmpd="sng" algn="ctr"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:prstDash val="solid"/></a:ln><a:ln w="25400" cap="flat" cmpd="sng" algn="ctr"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:prstDash val="solid"/></a:ln><a:ln w="38100" cap="flat" cmpd="sng" algn="ctr"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:prstDash val="solid"/></a:ln></a:lnStyleLst>
      <a:effectStyleLst><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle></a:effectStyleLst>
      <a:bgFillStyleLst><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:bgFillStyleLst>
    </a:fmtScheme>
  </a:themeElements>
  <a:objectDefaults/>
  <a:extraClrSchemeLst/>
</a:theme>"""


def content_types(num_slides: int, media_count: int) -> str:
    overrides = [
        ("/docProps/app.xml", "application/vnd.openxmlformats-officedocument.extended-properties+xml"),
        ("/docProps/core.xml", "application/vnd.openxmlformats-package.core-properties+xml"),
        ("/ppt/presentation.xml", "application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml"),
        ("/ppt/presProps.xml", "application/vnd.openxmlformats-officedocument.presentationml.presProps+xml"),
        ("/ppt/viewProps.xml", "application/vnd.openxmlformats-officedocument.presentationml.viewProps+xml"),
        ("/ppt/tableStyles.xml", "application/vnd.openxmlformats-officedocument.presentationml.tableStyles+xml"),
        ("/ppt/theme/theme1.xml", "application/vnd.openxmlformats-officedocument.theme+xml"),
        ("/ppt/slideMasters/slideMaster1.xml", "application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml"),
        ("/ppt/slideLayouts/slideLayout1.xml", "application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml"),
        ("/ppt/notesMasters/notesMaster1.xml", "application/vnd.openxmlformats-officedocument.presentationml.notesMaster+xml"),
    ]
    for i in range(1, num_slides + 1):
        overrides.append((f"/ppt/slides/slide{i}.xml", "application/vnd.openxmlformats-officedocument.presentationml.slide+xml"))
        overrides.append((f"/ppt/notesSlides/notesSlide{i}.xml", "application/vnd.openxmlformats-officedocument.presentationml.notesSlide+xml"))
    body = "".join(f'<Override PartName="{part}" ContentType="{typ}"/>' for part, typ in overrides)
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
  <Default Extension="xml" ContentType="application/xml"/>
  <Default Extension="png" ContentType="image/png"/>
  {body}
</Types>"""


def app_xml(num_slides: int) -> str:
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties" xmlns:vt="http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes">
  <Application>Codex Open XML generator</Application>
  <PresentationFormat>On-screen Show (16:9)</PresentationFormat>
  <Slides>{num_slides}</Slides>
  <Notes>{num_slides}</Notes>
  <Company>ZHAW</Company>
</Properties>"""


def core_xml() -> str:
    now = dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:dcmitype="http://purl.org/dc/dcmitype/" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <dc:title>Contactless Radar-Based Heart Rate Monitoring Device</dc:title>
  <dc:subject>PM4 video presentation deck</dc:subject>
  <dc:creator>Thomas Perri, Bogdans Grebnevs</dc:creator>
  <cp:lastModifiedBy>Codex</cp:lastModifiedBy>
  <dcterms:created xsi:type="dcterms:W3CDTF">{now}</dcterms:created>
  <dcterms:modified xsi:type="dcterms:W3CDTF">{now}</dcterms:modified>
</cp:coreProperties>"""


def pres_props_xml() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:presentationPr xmlns:a="{NS_A}" xmlns:r="{NS_R}" xmlns:p="{NS_P}"/>"""


def view_props_xml() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:viewPr xmlns:a="{NS_A}" xmlns:r="{NS_R}" xmlns:p="{NS_P}"><p:normalViewPr><p:restoredLeft sz="15620"/><p:restoredTop sz="94660"/></p:normalViewPr><p:slideViewPr><p:cSldViewPr><p:cViewPr varScale="1"><p:scale><a:sx n="100" d="100"/><a:sy n="100" d="100"/></p:scale><p:origin x="0" y="0"/></p:cViewPr><p:guideLst/></p:cSldViewPr></p:slideViewPr></p:viewPr>"""


def table_styles_xml() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<a:tblStyleLst xmlns:a="{NS_A}" def="{{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}}"/>"""


def build() -> None:
    slides, notes, media_paths, slide_rels = build_slides()
    media_files = {
        "media/image1.png": media_paths["holter"],
        "media/image2.png": media_paths["sleeping"],
        "media/image3.png": media_paths["pcb"],
        "media/image4.png": media_paths["radar"],
    }

    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", content_types(len(slides), len(media_files)))
        z.writestr("_rels/.rels", rels([
            ("rId1", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument", "ppt/presentation.xml"),
            ("rId2", "http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties", "docProps/core.xml"),
            ("rId3", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties", "docProps/app.xml"),
        ]))
        z.writestr("docProps/app.xml", app_xml(len(slides)))
        z.writestr("docProps/core.xml", core_xml())
        z.writestr("ppt/presentation.xml", presentation_xml(len(slides)))
        pres_rels = [("rId1", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster", "slideMasters/slideMaster1.xml")]
        for i in range(1, len(slides) + 1):
            pres_rels.append((f"rId{1 + i}", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide", f"slides/slide{i}.xml"))
        pres_rels.extend([
            (f"rId{len(slides) + 2}", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/notesMaster", "notesMasters/notesMaster1.xml"),
            (f"rId{len(slides) + 3}", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/presProps", "presProps.xml"),
            (f"rId{len(slides) + 4}", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/viewProps", "viewProps.xml"),
            (f"rId{len(slides) + 5}", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/tableStyles", "tableStyles.xml"),
            (f"rId{len(slides) + 6}", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme", "theme/theme1.xml"),
        ])
        z.writestr("ppt/_rels/presentation.xml.rels", rels(pres_rels))
        z.writestr("ppt/presProps.xml", pres_props_xml())
        z.writestr("ppt/viewProps.xml", view_props_xml())
        z.writestr("ppt/tableStyles.xml", table_styles_xml())
        z.writestr("ppt/theme/theme1.xml", theme_xml())
        z.writestr("ppt/slideMasters/slideMaster1.xml", slide_master_xml())
        z.writestr("ppt/slideMasters/_rels/slideMaster1.xml.rels", rels([
            ("rId1", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout", "../slideLayouts/slideLayout1.xml"),
            ("rId2", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme", "../theme/theme1.xml"),
        ]))
        z.writestr("ppt/slideLayouts/slideLayout1.xml", slide_layout_xml())
        z.writestr("ppt/slideLayouts/_rels/slideLayout1.xml.rels", rels([
            ("rId1", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster", "../slideMasters/slideMaster1.xml")
        ]))
        z.writestr("ppt/notesMasters/notesMaster1.xml", notes_master_xml())
        z.writestr("ppt/notesMasters/_rels/notesMaster1.xml.rels", rels([
            ("rId1", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme", "../theme/theme1.xml")
        ]))
        for i, slide in enumerate(slides, start=1):
            z.writestr(f"ppt/slides/slide{i}.xml", slide)
            sr = [("rId1", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout", "../slideLayouts/slideLayout1.xml")]
            sr.append(("rIdNotes", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/notesSlide", f"../notesSlides/notesSlide{i}.xml"))
            for rid, target in slide_rels[i - 1]:
                sr.append((rid, "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image", f"../{target}"))
            z.writestr(f"ppt/slides/_rels/slide{i}.xml.rels", rels(sr))
            z.writestr(f"ppt/notesSlides/notesSlide{i}.xml", notes_xml(notes[i - 1], i))
            z.writestr(f"ppt/notesSlides/_rels/notesSlide{i}.xml.rels", rels([
                ("rId1", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/notesMaster", "../notesMasters/notesMaster1.xml"),
                ("rId2", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide", f"../slides/slide{i}.xml"),
            ]))
        for target, source in media_files.items():
            z.write(source, f"ppt/{target}")

    print(f"Created {OUT}")
    print(f"Slides: {len(slides)}")
    print(f"Size: {OUT.stat().st_size} bytes")


if __name__ == "__main__":
    build()
