#!/usr/bin/env python3
"""A deliberately complicated PPTX to test OpenPresentation's reader and drawing.
Team 5, 25-26 October 2026 (Dale: "generate a really complicated pptx with
animations etc to test presenter"). Every feature here is one PowerPoint
writes; none is from a private document.

    python3 make_stress_pptx.py OUT.pptx
"""
import io, math, sys, copy
from lxml import etree
from PIL import Image, ImageDraw
from pptx import Presentation
from pptx.util import Emu, Pt, Inches
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE, MSO_CONNECTOR
from pptx.enum.text import PP_ALIGN, MSO_ANCHOR, MSO_AUTO_SIZE
from pptx.chart.data import CategoryChartData, XyChartData
from pptx.enum.chart import XL_CHART_TYPE, XL_LEGEND_POSITION, XL_LABEL_POSITION, XL_MARKER_STYLE
from pptx.oxml.ns import qn

NS = {"a": "http://schemas.openxmlformats.org/drawingml/2006/main",
      "p": "http://schemas.openxmlformats.org/presentationml/2006/main",
      "r": "http://schemas.openxmlformats.org/officeDocument/2006/relationships"}
P = "{%s}" % NS["p"]
A = "{%s}" % NS["a"]

prs = Presentation()
prs.slide_width, prs.slide_height = Inches(13.333), Inches(7.5)
W, H = prs.slide_width, prs.slide_height
L_TITLE, L_TC, L_SECTION, L_TWO, L_CMP, L_TONLY, L_BLANK = (prs.slide_layouts[i] for i in (0, 1, 2, 3, 4, 5, 6))


# ---------- helpers ----------------------------------------------------------
def png(w, h, kind):
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    if kind == "gradient":
        for y in range(h):
            for x in range(0, w, 4):
                d.rectangle([x, y, x + 3, y], fill=(int(255 * x / w), int(255 * y / h), 160, 255))
    elif kind == "alpha":
        for r in range(min(w, h) // 2, 0, -2):
            a = int(255 * r / (min(w, h) / 2))
            d.ellipse([w / 2 - r, h / 2 - r, w / 2 + r, h / 2 + r], fill=(220, 40, 60, 255 - a))
    elif kind == "checker":
        s = 16
        for y in range(0, h, s):
            for x in range(0, w, s):
                d.rectangle([x, y, x + s - 1, y + s - 1], fill=(30, 30, 30, 255) if (x // s + y // s) % 2 else (240, 240, 240, 255))
        d.text((10, 10), "AmigaChrome", fill=(255, 0, 0, 255))
    b = io.BytesIO()
    if kind == "jpeg":
        Image.new("RGB", (w, h), (20, 90, 160)).save(b, "JPEG", quality=85)
    else:
        im.save(b, "PNG")
    b.seek(0)
    return b


def add_transition(slide, xml):
    """Inserts <p:transition> after clrMapOvr (or cSld), as PowerPoint orders them."""
    sld = slide._element
    el = etree.fromstring(xml)
    anchor = sld.find(P + "clrMapOvr")
    if anchor is None:
        anchor = sld.find(P + "cSld")
    anchor.addnext(el)
    return el


def add_timing(slide, effects):
    """effects: list of (shape_id, preset_class, preset_id, preset_subtype, node_type, dur_ms, extra_xml).
    Builds one click sequence; each effect starts on click (clickEffect) or after previous (afterEffect)."""
    sld = slide._element
    nid = [2]

    def cid():
        nid[0] += 1
        return nid[0]

    pars = []
    for (spid, pclass, pid, psub, ntype, dur, behaviour) in effects:
        b = behaviour.format(spid=spid, dur=dur, cid=cid(), cid2=cid(), cid3=cid())
        pars.append(f"""
<p:par><p:cTn id="{cid()}" fill="hold"><p:stCondLst><p:cond delay="{'indefinite' if ntype == 'clickEffect' else '0'}"/></p:stCondLst>
 <p:childTnLst><p:par><p:cTn id="{cid()}" fill="hold"><p:stCondLst><p:cond delay="0"/></p:stCondLst>
  <p:childTnLst><p:par><p:cTn id="{cid()}" presetID="{pid}" presetClass="{pclass}" presetSubtype="{psub}" fill="hold" nodeType="{ntype}">
   <p:stCondLst><p:cond delay="0"/></p:stCondLst><p:childTnLst>{b}</p:childTnLst></p:cTn></p:par></p:childTnLst>
 </p:cTn></p:par></p:childTnLst></p:cTn></p:par>""")
    xml = f"""<p:timing xmlns:p="{NS['p']}" xmlns:a="{NS['a']}"><p:tnLst><p:par><p:cTn id="1" dur="indefinite" restart="never" nodeType="tmRoot"><p:childTnLst>
<p:seq concurrent="1" nextAc="seek"><p:cTn id="2" dur="indefinite" nodeType="mainSeq"><p:childTnLst>{''.join(pars)}</p:childTnLst></p:cTn>
<p:prevCondLst><p:cond evt="onPrev" delay="0"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:prevCondLst>
<p:nextCondLst><p:cond evt="onNext" delay="0"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:nextCondLst></p:seq>
</p:childTnLst></p:cTn></p:par></p:tnLst></p:timing>"""
    el = etree.fromstring(xml)
    anchor = sld.find(P + "transition")
    if anchor is None:
        anchor = sld.find(P + "clrMapOvr")
    if anchor is None:
        anchor = sld.find(P + "cSld")
    anchor.addnext(el)


FADE_IN = ('<p:set><p:cBhvr><p:cTn id="{cid}" dur="1" fill="hold"><p:stCondLst><p:cond delay="0"/></p:stCondLst></p:cTn>'
           '<p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl><p:attrNameLst><p:attrName>style.visibility</p:attrName></p:attrNameLst></p:cBhvr>'
           '<p:to><p:strVal val="visible"/></p:to></p:set>'
           '<p:animEffect transition="in" filter="fade"><p:cBhvr><p:cTn id="{cid2}" dur="{dur}"/><p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl></p:cBhvr></p:animEffect>')
FLY_IN = ('<p:set><p:cBhvr><p:cTn id="{cid}" dur="1" fill="hold"><p:stCondLst><p:cond delay="0"/></p:stCondLst></p:cTn>'
          '<p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl><p:attrNameLst><p:attrName>style.visibility</p:attrName></p:attrNameLst></p:cBhvr>'
          '<p:to><p:strVal val="visible"/></p:to></p:set>'
          '<p:anim calcmode="lin" valueType="num"><p:cBhvr additive="base"><p:cTn id="{cid2}" dur="{dur}" fill="hold"/>'
          '<p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl><p:attrNameLst><p:attrName>ppt_x</p:attrName></p:attrNameLst></p:cBhvr>'
          '<p:tavLst><p:tav tm="0"><p:val><p:strVal val="0-#ppt_w/2"/></p:val></p:tav><p:tav tm="100000"><p:val><p:strVal val="#ppt_x"/></p:val></p:tav></p:tavLst></p:anim>')
SPIN = ('<p:animRot by="21600000"><p:cBhvr><p:cTn id="{cid}" dur="{dur}" fill="hold"/><p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl>'
        '<p:attrNameLst><p:attrName>r</p:attrName></p:attrNameLst></p:cBhvr></p:animRot>')
PULSE = ('<p:animScale><p:cBhvr><p:cTn id="{cid}" dur="{dur}" autoRev="1" fill="hold"/><p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl></p:cBhvr>'
         '<p:by x="110000" y="110000"/></p:animScale>')
COLOR = ('<p:animClr clrSpc="rgb" dir="cw"><p:cBhvr><p:cTn id="{cid}" dur="{dur}" fill="hold"/><p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl>'
         '<p:attrNameLst><p:attrName>fillcolor</p:attrName></p:attrNameLst></p:cBhvr><p:to><a:srgbClr val="FF8800"/></p:to></p:animClr>')
MOTION = ('<p:animMotion origin="layout" path="M 0 0 C 0.1 -0.2 0.3 -0.2 0.4 0 C 0.5 0.2 0.7 0.2 0.8 0 E" pathEditMode="relative" ptsTypes="">'
          '<p:cBhvr><p:cTn id="{cid}" dur="{dur}" fill="hold"/><p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl>'
          '<p:attrNameLst><p:attrName>ppt_x</p:attrName><p:attrName>ppt_y</p:attrName></p:attrNameLst></p:cBhvr></p:animMotion>')
FADE_OUT = ('<p:animEffect transition="out" filter="fade"><p:cBhvr><p:cTn id="{cid}" dur="{dur}"/><p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl></p:cBhvr></p:animEffect>'
            '<p:set><p:cBhvr><p:cTn id="{cid2}" dur="1" fill="hold"><p:stCondLst><p:cond delay="{dur}"/></p:stCondLst></p:cTn>'
            '<p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl><p:attrNameLst><p:attrName>style.visibility</p:attrName></p:attrNameLst></p:cBhvr>'
            '<p:to><p:strVal val="hidden"/></p:to></p:set>')
WIPE = ('<p:set><p:cBhvr><p:cTn id="{cid}" dur="1" fill="hold"><p:stCondLst><p:cond delay="0"/></p:stCondLst></p:cTn>'
        '<p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl><p:attrNameLst><p:attrName>style.visibility</p:attrName></p:attrNameLst></p:cBhvr>'
        '<p:to><p:strVal val="visible"/></p:to></p:set>'
        '<p:animEffect transition="in" filter="wipe(down)"><p:cBhvr><p:cTn id="{cid2}" dur="{dur}"/><p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl></p:cBhvr></p:animEffect>')


def run(p, text, size=None, bold=None, italic=None, color=None, font=None):
    r = p.add_run()
    r.text = text
    f = r.font
    if size: f.size = Pt(size)
    if bold is not None: f.bold = bold
    if italic is not None: f.italic = italic
    if color: f.color.rgb = RGBColor.from_string(color)
    if font: f.name = font
    return r


def rpr(r):
    return r._r.get_or_add_rPr()


def notes(slide, text):
    slide.notes_slide.notes_text_frame.text = text


# ---------- 1. title slide: fade transition, title fades in, subtitle flies in ----
s = prs.slides.add_slide(L_TITLE)
s.shapes.title.text = "OpenPresentation stress deck"
s.placeholders[1].text = "Every feature PowerPoint writes — transitions, animations, charts, tables, groups → all in one file"
add_transition(s, f'<p:transition xmlns:p="{NS["p"]}" spd="slow" advClick="1" advTm="8000"><p:fade thruBlk="1"/></p:transition>')
add_timing(s, [(s.shapes.title.shape_id, "entr", 10, 0, "clickEffect", 700, FADE_IN),
               (s.placeholders[1].shape_id, "entr", 2, 8, "afterEffect", 500, FLY_IN)])
notes(s, "Speaker notes on the title slide. A second line with → arrows and “smart quotes”.")

# ---------- 2. text stress -------------------------------------------------------
s = prs.slides.add_slide(L_TC)
s.shapes.title.text = "Text: levels, bullets, numbering, caps, spacing, autofit"
tf = s.placeholders[1].text_frame
tf.clear()
items = [(0, "Level 1 bullet with a long line that has to wrap across the width of the placeholder more than once to test line breaking"),
         (1, "Level 2: bold, italic, underline, strike, super and sub"),
         (2, "Level 3: SMALL CAPS label and ALL CAPS letter-spaced"),
         (3, "Level 4 at 9 pt with a hyperlink and a jump to slide 5"),
         (4, "Level 5 at 7 pt: tiny text should stay readable or at least keep its spaces"),
         (0, "Numbered 1."), (0, "Numbered 2."), (0, "Custom bullets → ✓ • ■")]
for i, (lvl, text) in enumerate(items):
    p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
    p.level = lvl
    if lvl == 1:
        for t, k in (("bold ", "b"), ("italic ", "i"), ("underline ", "u"), ("strike ", "s"), ("x", None), ("2", "sup"), (" H", None), ("2", "sub"), ("O", None)):
            r = run(p, t)
            e = rpr(r)
            if k == "b": e.set("b", "1")
            if k == "i": e.set("i", "1")
            if k == "u": e.set("u", "dbl")
            if k == "s": e.set("strike", "sngStrike")
            if k == "sup": e.set("baseline", "30000")
            if k == "sub": e.set("baseline", "-25000")
    elif lvl == 2:
        r = run(p, "Small caps label ")
        rpr(r).set("cap", "small")
        r = run(p, "all caps spaced")
        e = rpr(r); e.set("cap", "all"); e.set("spc", "300"); e.set("sz", "1100")
    elif lvl == 3:
        r = run(p, "Level 4 at 9 pt with ", size=9)
        r = run(p, "a hyperlink", size=9)
        r.hyperlink.address = "https://example.org/openpresentation"
        r = run(p, " and ", size=9)
        r = run(p, "a jump to slide 5", size=9)
        # internal jump: action ppaction://hlinksldjump with a rel to the slide is added after all slides exist
        r._r.set("data-jump", "5")
    elif lvl == 4:
        run(p, text, size=7)
    elif text.startswith("Numbered"):
        run(p, text)
        pPr = p._p.get_or_add_pPr()
        bu = etree.SubElement(pPr, A + "buAutoNum"); bu.set("type", "arabicPeriod")
    else:
        run(p, text)
        pPr = p._p.get_or_add_pPr()
        bf = etree.SubElement(pPr, A + "buFont"); bf.set("typeface", "Arial")
        bc = etree.SubElement(pPr, A + "buChar"); bc.set("char", "→")
# autofit with font scale and line-spacing reduction
bodyPr = tf._txBody.find(A + "bodyPr")
for c in list(bodyPr):
    bodyPr.remove(c)
na = etree.SubElement(bodyPr, A + "normAutofit"); na.set("fontScale", "85000"); na.set("lnSpcReduction", "10000")
# rotated and vertical text boxes
tb = s.shapes.add_textbox(Inches(10.6), Inches(1.6), Inches(2.4), Inches(0.6))
tb.text_frame.text = "Rotated 15°"
tb.rotation = 15
tb = s.shapes.add_textbox(Inches(12.6), Inches(2.4), Inches(0.6), Inches(3.8))
tb.text_frame.text = "Vertical text 270"
tb.text_frame._txBody.find(A + "bodyPr").set("vert", "vert270")
add_transition(s, f'<p:transition xmlns:p="{NS["p"]}" spd="med"><p:push dir="u"/></p:transition>')
add_timing(s, [(s.placeholders[1].shape_id, "entr", 10, 0, "clickEffect", 500, FADE_IN)])

# ---------- 3. shapes gallery ------------------------------------------------------
s = prs.slides.add_slide(L_TONLY)
s.shapes.title.text = "Shapes: presets, gradients, alpha, shadows, outlines, rotation, flips"
gallery = [MSO_SHAPE.RECTANGLE, MSO_SHAPE.ROUNDED_RECTANGLE, MSO_SHAPE.OVAL, MSO_SHAPE.ISOSCELES_TRIANGLE,
           MSO_SHAPE.RIGHT_TRIANGLE, MSO_SHAPE.DIAMOND, MSO_SHAPE.PENTAGON, MSO_SHAPE.HEXAGON, MSO_SHAPE.OCTAGON,
           MSO_SHAPE.STAR_5_POINT, MSO_SHAPE.STAR_24_POINT, MSO_SHAPE.RIGHT_ARROW, MSO_SHAPE.LEFT_RIGHT_ARROW,
           MSO_SHAPE.CHEVRON, MSO_SHAPE.HEART, MSO_SHAPE.LIGHTNING_BOLT, MSO_SHAPE.MOON, MSO_SHAPE.CLOUD,
           MSO_SHAPE.DONUT, MSO_SHAPE.BLOCK_ARC, MSO_SHAPE.CAN, MSO_SHAPE.CUBE, MSO_SHAPE.FLOWCHART_DECISION,
           MSO_SHAPE.FLOWCHART_DOCUMENT, MSO_SHAPE.RECTANGULAR_CALLOUT, MSO_SHAPE.CLOUD_CALLOUT, MSO_SHAPE.WAVE,
           MSO_SHAPE.SMILEY_FACE, MSO_SHAPE.SUN, MSO_SHAPE.GEAR_6, MSO_SHAPE.PLAQUE, MSO_SHAPE.FRAME]
cols = 8
ids = []
for i, kind in enumerate(gallery):
    x = Inches(0.4 + (i % cols) * 1.6)
    y = Inches(1.5 + (i // cols) * 1.45)
    sh = s.shapes.add_shape(kind, x, y, Inches(1.3), Inches(1.15))
    sh.text = str(i + 1)
    f = sh.fill
    if i % 4 == 0:
        f.gradient(); f.gradient_angle = 45
        f.gradient_stops[0].color.rgb = RGBColor(0x1F, 0x4E, 0x79); f.gradient_stops[1].color.rgb = RGBColor(0x9D, 0xC3, 0xE6)
    elif i % 4 == 1:
        f.solid(); f.fore_color.rgb = RGBColor(0xC0, 0x39, 0x2B)
        sf = sh._element.spPr.find(A + "solidFill").find(A + "srgbClr")
        etree.SubElement(sf, A + "alpha").set("val", "55000")
    elif i % 4 == 2:
        f.patterned()
    else:
        f.background()
    ln = sh.line
    ln.width = Pt(1 + i % 5)
    ln.color.rgb = RGBColor(0x33, 0x33, 0x33)
    if i % 3 == 0:
        ln.dash_style = 4  # dash
    if i % 5 == 0:
        sh.rotation = 20 * (i % 9)
    if i % 7 == 3:
        sh._element.spPr.find(A + "xfrm").set("flipH", "1")
    if i % 6 == 2:  # outer shadow
        ef = etree.SubElement(sh._element.spPr, A + "effectLst")
        sd = etree.SubElement(ef, A + "outerShdw"); sd.set("blurRad", "50800"); sd.set("dist", "38100"); sd.set("dir", "2700000")
        etree.SubElement(etree.SubElement(sd, A + "srgbClr", val="000000"), A + "alpha", val="40000")
    ids.append(sh.shape_id)
# lines with arrowheads and connectors
c = s.shapes.add_connector(MSO_CONNECTOR.STRAIGHT, Inches(0.5), Inches(7.1), Inches(4.5), Inches(6.7))
c.line.width = Pt(2)
lnx = c.line._get_or_add_ln()
etree.SubElement(lnx, A + "headEnd").set("type", "triangle"); etree.SubElement(lnx, A + "tailEnd").set("type", "oval")
c = s.shapes.add_connector(MSO_CONNECTOR.ELBOW, Inches(5), Inches(6.7), Inches(8), Inches(7.2))
c = s.shapes.add_connector(MSO_CONNECTOR.CURVE, Inches(8.5), Inches(7.2), Inches(12.5), Inches(6.7))
add_transition(s, f'<p:transition xmlns:p="{NS["p"]}" spd="fast"><p:wipe dir="r"/></p:transition>')
add_timing(s, [(ids[9], "emph", 8, 0, "clickEffect", 2000, SPIN),
               (ids[14], "emph", 26, 0, "withEffect", 600, PULSE),
               (ids[2], "emph", 19, 0, "afterEffect", 1000, COLOR)])

# ---------- 4. groups, freeform, text anchors, motion path, morph transition -------
s = prs.slides.add_slide(L_TONLY)
s.shapes.title.text = "Groups inside groups, a freeform, text anchors"
outer = s.shapes.add_group_shape()
for j in range(3):
    inner = outer.shapes.add_group_shape()
    for k in range(3):
        sh = inner.shapes.add_shape(MSO_SHAPE.ROUNDED_RECTANGLE, Inches(0.6 + j * 2.1), Inches(1.6 + k * 1.0), Inches(1.9), Inches(0.8))
        sh.text = f"group {j}.{k}"
        sh.text_frame.vertical_anchor = (MSO_ANCHOR.TOP, MSO_ANCHOR.MIDDLE, MSO_ANCHOR.BOTTOM)[k]
        sh.fill.solid(); sh.fill.fore_color.rgb = RGBColor(0x2E + 40 * j, 0x86, 0x8B + 30 * k)
outer.rotation = 0
ff = s.shapes.build_freeform(Inches(7.2), Inches(2.0), scale=Inches(1) / 100)
pts = [(7.2 * 100 + 300 * math.cos(t / 10) * (1 + 0.3 * math.sin(5 * t / 10)), 2.0 * 100 + 300 + 220 * math.sin(t / 10)) for t in range(0, 63)]
ff.add_line_segments([(x - 720 + 720, y) for x, y in pts], close=True)
fshape = ff.convert_to_shape(Inches(7.2), Inches(1.6))
fshape.fill.solid(); fshape.fill.fore_color.rgb = RGBColor(0xF3, 0x9C, 0x12)
ball = s.shapes.add_shape(MSO_SHAPE.OVAL, Inches(0.6), Inches(5.6), Inches(0.8), Inches(0.8))
ball.fill.solid(); ball.fill.fore_color.rgb = RGBColor(0x8E, 0x44, 0xAD)
tb = s.shapes.add_textbox(Inches(7.2), Inches(5.6), Inches(5.6), Inches(1.4))
tb.text_frame.word_wrap = True
tb.text_frame.auto_size = MSO_AUTO_SIZE.SHAPE_TO_FIT_TEXT
tb.text_frame.text = "A text box that grows to fit its text, with insets of 0.3 inch on every side."
for side in ("lIns", "tIns", "rIns", "bIns"):
    tb.text_frame._txBody.find(A + "bodyPr").set(side, "274320")
morph = (f'<mc:AlternateContent xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006">'
         f'<mc:Choice xmlns:p159="http://schemas.microsoft.com/office/powerpoint/2015/09/main" Requires="p159">'
         f'<p:transition xmlns:p="{NS["p"]}" spd="slow"><p159:morph option="byObject"/></p:transition></mc:Choice>'
         f'<mc:Fallback><p:transition xmlns:p="{NS["p"]}" spd="slow"><p:fade/></p:transition></mc:Fallback></mc:AlternateContent>')
add_transition(s, morph)
add_timing(s, [(ball.shape_id, "path", 0, 0, "clickEffect", 2500, MOTION)])

# ---------- 5. tables -------------------------------------------------------------
s = prs.slides.add_slide(L_TONLY)
s.shapes.title.text = "Tables: merged cells, banding, fills, borders, alignment"
rows, ncols = 7, 6
gt = s.shapes.add_table(rows, ncols, Inches(0.6), Inches(1.5), Inches(12), Inches(5.4))
t = gt.table
t.first_row = True; t.horz_banding = True; t.first_col = True; t.last_row = True
for r in range(rows):
    for cc in range(ncols):
        cell = t.cell(r, cc)
        cell.text = f"R{r}C{cc}" if r else f"Header {cc}"
        cell.vertical_anchor = (MSO_ANCHOR.TOP, MSO_ANCHOR.MIDDLE, MSO_ANCHOR.BOTTOM)[(r + cc) % 3]
        cell.text_frame.paragraphs[0].alignment = (PP_ALIGN.LEFT, PP_ALIGN.CENTER, PP_ALIGN.RIGHT)[cc % 3]
        cell.margin_left = Inches(0.05 + 0.05 * (cc % 3))
t.cell(1, 1).merge(t.cell(2, 3)); t.cell(1, 1).text = "Merged 2 rows × 3 columns"
t.cell(4, 0).merge(t.cell(6, 0)); t.cell(4, 0).text = "Merged 3 rows"
c = t.cell(3, 4); c.fill.solid(); c.fill.fore_color.rgb = RGBColor(0xFF, 0xD9, 0x66); c.text = "£1,234,567.89"
c = t.cell(5, 5); c.text = "A long cell text that wraps inside its cell several times over"
t.columns[0].width = Inches(1.4); t.columns[5].width = Inches(2.8)
add_transition(s, f'<p:transition xmlns:p="{NS["p"]}" spd="med"><p:split orient="vert" dir="out"/></p:transition>')
add_timing(s, [(gt.shape_id, "entr", 22, 4, "clickEffect", 700, WIPE)])

# ---------- 6. charts --------------------------------------------------------------
s = prs.slides.add_slide(L_TONLY)
s.shapes.title.text = "Native charts: column, stacked bar, line, pie, doughnut, scatter, area"
cd = CategoryChartData(); cd.categories = ["Q1", "Q2", "Q3", "Q4"]
cd.add_series("Amiga", (12.5, 15.2, 9.8, 18.1)); cd.add_series("Atari", (8.1, 9.4, 12.0, 7.3)); cd.add_series("PC", (30, 28, 35, 40))
spots = [(XL_CHART_TYPE.COLUMN_CLUSTERED, 0, 0), (XL_CHART_TYPE.BAR_STACKED, 1, 0), (XL_CHART_TYPE.LINE_MARKERS, 2, 0),
         (XL_CHART_TYPE.AREA, 3, 0)]
charts = []
for kind, cx, cy in spots:
    gf = s.shapes.add_chart(kind, Inches(0.3 + cx * 3.25), Inches(1.4), Inches(3.15), Inches(2.8), cd)
    ch = gf.chart; ch.has_legend = True; ch.legend.position = XL_LEGEND_POSITION.BOTTOM; ch.legend.include_in_layout = False
    ch.has_title = True; ch.chart_title.text_frame.text = str(kind).split(".")[-1].split(" ")[0]
    charts.append(gf)
pd = CategoryChartData(); pd.categories = ["Write", "Calc", "Pub", "Present", "Base"]; pd.add_series("Share", (35, 30, 10, 15, 10))
gf = s.shapes.add_chart(XL_CHART_TYPE.PIE, Inches(0.3), Inches(4.3), Inches(3.15), Inches(3.0), pd)
pl = gf.chart.plots[0]; pl.has_data_labels = True; pl.data_labels.number_format = '0"%"'; pl.data_labels.number_format_is_linked = False
pl.series[0].points[1].format.fill.solid(); pl.series[0].points[1].format.fill.fore_color.rgb = RGBColor(0xC0, 0x00, 0x00)
pl.series[0].points[0]._element.get_or_add_explosion().set("val", "18") if hasattr(pl.series[0].points[0]._element, "get_or_add_explosion") else None
gf = s.shapes.add_chart(XL_CHART_TYPE.DOUGHNUT, Inches(3.55), Inches(4.3), Inches(3.15), Inches(3.0), pd)
xy = XyChartData(); ser = xy.add_series("MHz vs speed")
for mhz, sp in ((7, 0.6), (14, 1.2), (25, 3.1), (33, 4.8), (40, 6.0), (50, 9.5)):
    ser.add_data_point(mhz, sp)
gf = s.shapes.add_chart(XL_CHART_TYPE.XY_SCATTER_SMOOTH, Inches(6.8), Inches(4.3), Inches(3.15), Inches(3.0), xy)
gf.chart.plots[0].series[0].marker.style = XL_MARKER_STYLE.DIAMOND
cd2 = CategoryChartData(); cd2.categories = ["Mon", "Tue", "Wed", "Thu", "Fri"]; cd2.add_series("Visits", (40, 55, 47, 62, 58))
gf = s.shapes.add_chart(XL_CHART_TYPE.COLUMN_CLUSTERED, Inches(10.05), Inches(4.3), Inches(3.0), Inches(3.0), cd2)
p2 = gf.chart.plots[0]; p2.has_data_labels = True; p2.data_labels.position = XL_LABEL_POSITION.OUTSIDE_END
gf.chart.value_axis.tick_labels.number_format = '£#,##0'; gf.chart.value_axis.tick_labels.number_format_is_linked = False
add_transition(s, f'<p:transition xmlns:p="{NS["p"]}" spd="med"><p:cover dir="l"/></p:transition>')
add_timing(s, [(charts[0].shape_id, "entr", 10, 0, "clickEffect", 500, FADE_IN),
               (charts[1].shape_id, "entr", 10, 0, "afterEffect", 500, FADE_IN)])

# ---------- 7. pictures ------------------------------------------------------------
s = prs.slides.add_slide(L_TONLY)
s.shapes.title.text = "Pictures: PNG with alpha, JPEG, cropped, rotated, bordered, picture fill"
p1 = s.shapes.add_picture(png(640, 400, "gradient"), Inches(0.5), Inches(1.5), Inches(4))
p2 = s.shapes.add_picture(png(400, 400, "alpha"), Inches(4.8), Inches(1.5), Inches(2.6))
p3 = s.shapes.add_picture(png(800, 450, "jpeg"), Inches(8), Inches(1.5), Inches(4.8))
p3.crop_left = 0.1; p3.crop_right = 0.2; p3.crop_top = 0.15
p4 = s.shapes.add_picture(png(320, 320, "checker"), Inches(0.8), Inches(4.4), Inches(2.6))
p4.rotation = -12
p4.line.width = Pt(4); p4.line.color.rgb = RGBColor(0x2C, 0x3E, 0x50)
# a shape filled with a picture (blipFill on an ellipse)
img_part, rid = s.part.get_or_add_image_part(png(320, 320, "checker"))
el = s.shapes.add_shape(MSO_SHAPE.OVAL, Inches(4.5), Inches(4.3), Inches(3), Inches(2.6))
spPr = el._element.spPr
for child in list(spPr):
    if child.tag in (A + "solidFill", A + "noFill", A + "gradFill"):
        spPr.remove(child)
blip = etree.fromstring(f'<a:blipFill xmlns:a="{NS["a"]}" xmlns:r="{NS["r"]}" rotWithShape="1"><a:blip r:embed="{rid}"/><a:stretch><a:fillRect/></a:stretch></a:blipFill>')
spPr.find(A + "prstGeom").addnext(blip)
add_transition(s, f'<p:transition xmlns:p="{NS["p"]}" spd="slow"><p:dissolve/></p:transition>')
add_timing(s, [(p3.shape_id, "exit", 10, 0, "clickEffect", 800, FADE_OUT),
               (p4.shape_id, "emph", 8, 0, "clickEffect", 1500, SPIN)])

# ---------- 8. hidden slide ---------------------------------------------------------
s = prs.slides.add_slide(L_TC)
s.shapes.title.text = "This slide is hidden"
s.placeholders[1].text = "It must not show in the slide show or print, but must stay in the file."
s._element.set("show", "0")

# ---------- 9. section header with footer, slide number and long notes --------------
s = prs.slides.add_slide(L_SECTION)
s.shapes.title.text = "Section two"
for ph in s.placeholders:
    if ph.placeholder_format.idx == 1:
        ph.text = "A section header with a subtitle"
notes(s, "Long notes. " * 120)
add_transition(s, f'<p:transition xmlns:p="{NS["p"]}"><p:zoom/></p:transition>')

# ---------- 10. unicode and typography ----------------------------------------------
s = prs.slides.add_slide(L_TC)
s.shapes.title.text = "Characters: arrows, dashes, quotes, symbols, accents, scripts"
tf = s.placeholders[1].text_frame
tf.clear()
lines = ["Arrows → ← ↑ ↓ ⇒ ↔   Dashes – — ‒   Quotes “ ” ‘ ’ „ «»   Ellipsis …",
         "Symbols ™ © ® ° ± × ÷ ≤ ≥ ≠ ≈ ∞ √ € £ ¥ § ¶ † ‡ • ◦ ▪ ✓ ✗ ★ ♥",
         "Accents: Ångström, naïve café, Ελληνικά, Кириллица, Polski: zażółć gęślą jaźń",
         "Wide: 日本語のテキスト, 中文, 한국어   Emoji: 🚀 🎉 😀",
         "Right to left: עברית   العربية",
         "Tabs:\tone\ttwo\tthree   and a SupercalifragilisticexpialidociousWordThatCannotBreakAnywhereAtAllInThisBox",
         ""]
for i, ln in enumerate(lines):
    p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
    run(p, ln, size=16 if i < 3 else 14)
p = tf.add_paragraph(); run(p, "Exact 12 pt line spacing in a 16 pt paragraph", size=16)
p.line_spacing = Pt(12)
p = tf.add_paragraph(); run(p, "Space before 24 pt, after 0, centred", size=16)
p.space_before = Pt(24); p.alignment = PP_ALIGN.CENTER
p = tf.add_paragraph(); run(p, "Justified paragraph: " + "lorem ipsum dolor sit amet " * 6, size=12)
p.alignment = PP_ALIGN.JUSTIFY

# ---------- 11. backgrounds ----------------------------------------------------------
s = prs.slides.add_slide(L_BLANK)
bg = s.background.fill
bg.gradient(); bg.gradient_angle = 90
bg.gradient_stops[0].color.rgb = RGBColor(0x0B, 0x13, 0x2B); bg.gradient_stops[1].color.rgb = RGBColor(0x1C, 0x6E, 0x8C)
tb = s.shapes.add_textbox(Inches(1), Inches(2.5), Inches(11.3), Inches(2))
tf = tb.text_frame; tf.word_wrap = True
p = tf.paragraphs[0]; p.alignment = PP_ALIGN.CENTER
run(p, "White 54 pt text on a gradient background", size=54, bold=True, color="FFFFFF")
s2 = prs.slides.add_slide(L_BLANK)
img_part, rid = s2.part.get_or_add_image_part(png(640, 360, "gradient"))
bgxml = etree.fromstring(f'<p:bg xmlns:p="{NS["p"]}" xmlns:a="{NS["a"]}" xmlns:r="{NS["r"]}"><p:bgPr><a:blipFill dpi="0" rotWithShape="1"><a:blip r:embed="{rid}"/><a:stretch><a:fillRect/></a:stretch></a:blipFill><a:effectLst/></p:bgPr></p:bg>')
s2._element.find(P + "cSld").insert(0, bgxml)
tb = s2.shapes.add_textbox(Inches(1), Inches(5.5), Inches(11.3), Inches(1.2))
run(tb.text_frame.paragraphs[0], "Picture background with text over it", size=36, color="000000")
add_transition(s2, f'<p:transition xmlns:p="{NS["p"]}" spd="slow"><p:randomBar dir="vert"/></p:transition>')

# ---------- 12. two content and comparison layouts -----------------------------------
s = prs.slides.add_slide(L_TWO)
s.shapes.title.text = "Two content layout"
s.placeholders[1].text = "Left column\nwith two lines"
s.placeholders[2].text = "Right column\n• and a bullet"
s = prs.slides.add_slide(L_CMP)
s.shapes.title.text = "Comparison layout"
for ph in s.placeholders:
    if ph.placeholder_format.idx in (1, 2, 3, 4):
        ph.text = f"Placeholder {ph.placeholder_format.idx}"

# ---------- internal jump on slide 2 (ppaction://hlinksldjump) ------------------------
s2txt = prs.slides[1]
target = prs.slides[4]
for r in s2txt._element.iter(A + "r"):
    if r.get("data-jump"):
        del r.attrib["data-jump"]
        rel = s2txt.part.relate_to(target.part, "http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide")
        rp = r.find(A + "rPr")
        if rp is None:
            rp = etree.SubElement(r, A + "rPr"); r.insert(0, rp)
        h = etree.SubElement(rp, A + "hlinkClick"); h.set("{%s}id" % NS["r"], rel); h.set("action", "ppaction://hlinksldjump")

# ---------- footers: date, footer text, slide number on every slide ------------------
for i, sl in enumerate(prs.slides):
    tb = sl.shapes.add_textbox(Inches(11.9), Inches(7.05), Inches(1.2), Inches(0.35))
    p = tb.text_frame.paragraphs[0]; p.alignment = PP_ALIGN.RIGHT
    fld = etree.SubElement(p._p, A + "fld"); fld.set("id", "{B6F15528-21DE-4FAA-801E-634DDDAF4B2B}"); fld.set("type", "slidenum")
    etree.SubElement(fld, A + "rPr", lang="en-GB", sz="1000"); tx = etree.SubElement(fld, A + "t"); tx.text = str(i + 1)

# ---------- sections (p14:sectionLst) -------------------------------------------------
pres = prs.part._element
sid = [str(x.get("id")) for x in pres.find(P + "sldIdLst")]
ext = etree.fromstring(
    f'<p:extLst xmlns:p="{NS["p"]}"><p:ext uri="{{521415D9-36F7-43E2-AB2F-B90AF26B5E84}}">'
    f'<p14:sectionLst xmlns:p14="http://schemas.microsoft.com/office/powerpoint/2010/main">'
    f'<p14:section name="Basics" id="{{11111111-1111-1111-1111-111111111111}}"><p14:sldIdLst>'
    + "".join(f'<p14:sldId id="{x}"/>' for x in sid[:7]) +
    f'</p14:sldIdLst></p14:section><p14:section name="More" id="{{22222222-2222-2222-2222-222222222222}}"><p14:sldIdLst>'
    + "".join(f'<p14:sldId id="{x}"/>' for x in sid[7:]) +
    '</p14:sldIdLst></p14:section></p14:sectionLst></p:ext></p:extLst>')
pres.append(ext)

out = sys.argv[1] if len(sys.argv) > 1 else "stress.pptx"
prs.save(out)
print(out, len(prs.slides), "slides")
