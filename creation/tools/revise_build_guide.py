from pathlib import Path
from io import BytesIO
import math
import textwrap
from xml.sax.saxutils import escape
from reportlab.pdfgen import canvas
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import Paragraph, Table, TableStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from pypdf import PdfReader, PdfWriter
from pypdf.generic import ContentStream, TextStringObject
from visual_wiring import illustrated_pages

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'tmp/pdfs'
SOURCE = ROOT / 'creation/backups/I-Shoot-Mo_ESP32_Complete-Build-Guide.before-two-modes.pdf'
TARGET = ROOT / 'creation/I-Shoot-Mo_ESP32_Complete-Build-Guide.pdf'
SKETCH = ROOT / 'creation/code/ishootmo_code/ishootmo_code.ino'
W, H = 595.2756, 841.8898
NAVY = colors.HexColor('#122e47')
BLUE = colors.HexColor('#147bb2')
TEAL = colors.HexColor('#087c70')
RED = colors.HexColor('#cf4147')
GOLD = colors.HexColor('#f6c643')
INK = colors.HexColor('#243c50')
MUTED = colors.HexColor('#536779')
PALE = colors.HexColor('#f0f5f9')
BLACK = colors.HexColor('#23313b')
WHITE = colors.white
pdfmetrics.registerFont(TTFont('Consolas', 'C:/Windows/Fonts/consola.ttf'))
STYLE = ParagraphStyle('body', fontName='Helvetica', fontSize=10,
                       leading=14.2, textColor=INK, spaceAfter=8)
SMALL = ParagraphStyle('small', parent=STYLE, fontSize=8.6, leading=11.6)
CELL = ParagraphStyle('cell', parent=STYLE, fontSize=9, leading=12)
GENERATED = []

def label(c, text, x, y, size=9, color=INK, bold=False):
    c.setFont('Helvetica-Bold' if bold else 'Helvetica', size)
    c.setFillColor(color)
    c.drawString(x, y, text)

def paragraph(c, text, x, top, width, style=STYLE):
    p = Paragraph(text, style)
    _, h = p.wrap(width, H)
    p.drawOn(c, x, top-h)
    return top-h-8

def page(title, subtitle=''):
    stream = BytesIO()
    c = canvas.Canvas(stream, pagesize=(W, H))
    c.setFillColor(NAVY); c.rect(0, H-32, W, 32, fill=1, stroke=0)
    label(c, 'I-SHOOT MO - ESP32 COMPLETE BUILD GUIDE', 45, H-21,
          9.5, WHITE, True)
    title_size=17
    while pdfmetrics.stringWidth(title,'Helvetica-Bold',title_size)>W-90:
        title_size-=0.5
    label(c, title, 45, H-77, title_size, NAVY, True)
    if subtitle:
        paragraph(c, subtitle, 45, H-91, W-90, SMALL)
    return c, stream

def finish(c, stream):
    c.save()
    reader = PdfReader(BytesIO(stream.getvalue()))
    GENERATED.append(reader)
    return reader.pages[0]

def box(c, x, y, width, height, fill=PALE, border=None):
    c.setFillColor(fill)
    c.setStrokeColor(border or fill)
    c.roundRect(x, y, width, height, 8, fill=1, stroke=bool(border))

def table(c, rows, widths, top):
    content = [[Paragraph(str(v), CELL) for v in row] for row in rows]
    t = Table(content, colWidths=widths, hAlign='LEFT')
    t.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,0), NAVY),
        ('TEXTCOLOR', (0,0), (-1,0), WHITE),
        ('ROWBACKGROUNDS', (0,1), (-1,-1), [PALE, WHITE]),
        ('VALIGN', (0,0), (-1,-1), 'TOP'),
        ('LEFTPADDING', (0,0), (-1,-1), 10),
        ('RIGHTPADDING', (0,0), (-1,-1), 10),
        ('TOPPADDING', (0,0), (-1,-1), 9),
        ('BOTTOMPADDING', (0,0), (-1,-1), 9),
        ('LINEBELOW', (0,-1), (-1,-1), .5, colors.HexColor('#cbd9e4')),
    ]))
    # Paragraphs own their text color; explicitly color the heading text.
    for p in content[0]:
        p.text = '<font color="white"><b>' + p.text + '</b></font>'
        p.frags = Paragraph(p.text, CELL).frags
    _, h = t.wrap(W-90, H)
    t.drawOn(c, 45, top-h)
    return top-h-18

def section(c, title, y):
    label(c, title, 45, y, 12, NAVY, True)
    return y-15

def wire(c, points, color, width=1.6):
    c.setStrokeColor(color); c.setLineWidth(width)
    p = c.beginPath(); p.moveTo(*points[0])
    for point in points[1:]: p.lineTo(*point)
    c.drawPath(p)

def dot(c, x, y, color):
    c.setFillColor(color); c.circle(x, y, 2.3, fill=1, stroke=0)

def terminal(c, text, x, y):
    label(c, text, 58, y+5, 8.5, WHITE, True)
    dot(c, x, y, GOLD)

def controller(c, bottom=180, top=720):
    box(c, 45, bottom, 150, top-bottom, NAVY)
    label(c, 'WEMOS LOLIN32', 58, top-23, 10, WHITE, True)
    label(c, 'LOGICAL TERMINALS', 58, top-39, 7.4, colors.HexColor('#b9d8ea'))

def cover():
    c, s = page('')
    c.setFillColor(NAVY); c.rect(0, 245, W, H-245, fill=1, stroke=0)
    label(c, 'I-SHOOT MO,', 45, 717, 34, WHITE, True)
    label(c, 'I-SHOOT MO', 45, 672, 34, WHITE, True)
    label(c, 'SMART BEANBAG TOSS', 47, 625, 18, GOLD, True)
    for x, val, col in [(136,10,BLUE),(298,20,TEAL),(460,30,RED)]:
        c.setStrokeColor(col); c.setLineWidth(8)
        c.circle(x, 495, 49, fill=0, stroke=1)
        c.setFont('Helvetica-Bold', 30); c.setFillColor(WHITE)
        c.drawCentredString(x, 485, str(val))
    paragraph(c, '<font color="white"><b>Equal holes. Selected targets. Balanced opportunities.</b><br/>'
              'Aim only at the lit hole to earn its points.</font>',
              47, 394, 497)
    label(c, 'MODE 1  PRECISION', 47, 309, 13, GOLD, True)
    label(c, 'MODE 2  TARGET CHASE', 47, 283, 13, GOLD, True)
    paragraph(c, 'Updated build guide, complete wiring diagrams and Arduino sketch.<br/>'
              '<b>WEMOS LOLIN32 / ESP32-WROOM v1.0.0 - Micro-USB</b><br/>'
              'Revision: 13 November 2026', 45, 211, W-90)
    paragraph(c, '<b>Quick navigation</b><br/>Rules: pages 2-3 | Wiring: pages 5 and 7-13<br/>'
              'Upload: page 24 | Complete sketch: pages 25-31 | Checks: page 32',
              45, 125, W-90, SMALL)
    return finish(c,s)

def design():
    c,s = page('1. Final design and the two game modes',
               'The existing cabinet, three equal 150 mm holes and GPIO assignments are retained.')
    y = paragraph(c, 'Each hole has an IR sensor. One LED stays lit to identify the active target. '
                  'A beanbag earns 10, 20 or 30 points <b>only if it enters that lit hole</b>. '
                  'An unlit-hole detection automatically records a wrong throw worth zero.', 45, 697, W-90)
    y = table(c, [
        ['MODE', 'TARGET RULE', 'ROUND END'],
        ['<b>1 - Precision</b>', 'Six targets in shuffled order: each hole appears twice. '
         'Advance after a hit, wrong hole or MISS press.', 'After six throws.<br/>Maximum: <b>120</b>.'],
        ['<b>2 - Target Chase</b>', 'Prepared shuffled groups of 10, 20 and 30. '
         'Only a correct hit advances to the next target.', '<b>60 seconds</b>.<br/>Unlimited throws.'],
    ], [112, 270, W-90-382], y-4)
    y = section(c, 'How a throw is processed', y)
    flow = [('LIT TARGET', NAVY), ('BEANBAG', BLUE), ('SENSOR / MISS', TEAL)]
    for i,(name,col) in enumerate(flow):
        x = 45 + i*174
        box(c,x,y-58,157,46,col)
        label(c,name,x+12,y-40,10,WHITE,True)
        if i<2:
            wire(c,[(x+158,y-35),(x+170,y-35)],MUTED)
    y -= 84
    y = paragraph(c, '<b>Correct hole:</b> add its points, increment Hit, sound the buzzer.<br/>'
                  '<b>Unlit hole:</b> add zero, increment Wrong automatically.<br/>'
                  '<b>No hole:</b> operator presses MISS once; add zero and increment Miss.',
                  45,y,W-90)
    y = section(c, 'One physical throw = one recorded attempt', y-10)
    y = paragraph(c, 'Never press MISS after a sensor has already recorded that throw. '
                  'The firmware ignores additional throw events for 650 ms after a valid event; '
                  'sensor events are counted on HIGH-to-LOW transitions. Keep normal throws '
                  'at least 650 ms apart.',45,y,W-90)
    paragraph(c, '<b>Local operation:</b> this sketch runs the game on the ESP32 and reports events '
              'over USB Serial. Wi-Fi, player profiles and a saved leaderboard are future additions.',
              45,y-6,W-90,SMALL)
    return finish(c,s)

def fairness():
    c,s = page('1.1 Balanced targets and button controls',
               'Shuffling changes the order without changing the scoring opportunities.')
    y = section(c,'Mode 1 - Precision',696)
    y = paragraph(c,'The six-target deck is <b>10, 10, 20, 20, 30, 30</b>. It is shuffled '
                  'for each round. A wrong hole or MISS still consumes that target opportunity. '
                  'A perfect round always earns 120 points.',45,y,W-90)
    box(c,45,y-48,W-90,40)
    label(c,'Example only:  20  >  10  >  30  >  10  >  30  >  20',60,y-33,11,NAVY,True)
    y = section(c,'Mode 2 - Target Chase',y-83)
    y = paragraph(c,'Each completed group offers each hole once. The next target is always a '
                  'different hole. The sequence restarts identically for every player, including '
                  'after a reset or power cycle. A wrong hole or MISS keeps the current target lit.',45,y,W-90)
    y = paragraph(c,'The timer may end partway through a group. Using the same prepared sequence '
                  'keeps that partial group consistent across competitors. Keep '
                  '<font face="Consolas">CHASE_SEQUENCE_SEED</font> unchanged during a competition.',45,y,W-90)
    y = table(c,[['BUTTON','ACTION'],
                ['<b>START / MODE</b>','While idle: tap and release to switch between the two modes. '
                 'Hold for 0.9 second to start. On the final screen: press to return to idle.'],
                ['<b>RESET</b>','Cancel and clear the round immediately. Return to idle with the '
                 'same mode selected.'],
                ['<b>MISS</b>','Press once only when the beanbag misses every hole. Precision: '
                 'consume one throw and advance. Chase: record a miss and keep the target.']],
              [120,W-210],y-4)
    paragraph(c,'<b>Display:</b> Score, Aim, Hit, Wrong, Miss and remaining throws/time. '
              'The final screen shows the score and most frequently scored zone. '
              'Ties for top zone display the lowest-valued tied zone.',45,y,W-90,SMALL)
    return finish(c,s)

def sensor_diagram():
    c,s = page('5. Wiring diagram - three IR sensors (1/3)',
               'Logical connections, not physical pin locations. Wire IDs and lengths match the retained schedule.')
    controller(c,205,735)
    terminal(c,'3V3',195,655); terminal(c,'GND',195,675)
    wire(c,[(195,655),(265,655),(265,711),(520,711),(520,235)],RED,2)
    wire(c,[(195,675),(280,675),(280,725),(542,725),(542,235)],BLACK,2)
    label(c,'3V3',505,740,8,RED,True); label(c,'GND',532,740,8,BLACK,True)
    for val,gpio,base,wid,length,col in [(30,32,540,3,60,BLUE),(20,35,385,9,42,TEAL),(10,34,230,6,42,colors.HexColor('#df790b'))]:
        box(c,350,base,155,108,PALE,BLUE)
        label(c,f'{val}-POINT FC-51',362,base+88,10,NAVY,True)
        label(c,'VCC',466,base+65,8,RED,True)
        label(c,'GND',465,base+44,8,BLACK,True)
        label(c,'OUT',360,base+18,8,col,True)
        terminal(c,f'GPIO{gpio}',195,base+22)
        wire(c,[(195,base+22),(350,base+22)],col)
        label(c,f'W{wid:02}  {length} cm  OUT > GPIO{gpio}',203,base+31,7.2,col,True)
        wire(c,[(505,base+68),(520,base+68)],RED)
        wire(c,[(505,base+47),(542,base+47)],BLACK)
        dot(c,520,base+68,RED); dot(c,542,base+47,BLACK)
        label(c,f'W{wid+1:02}: VCC / W{wid+2:02}: GND - {length} cm each',350,base-15,7.5,MUTED)
    y=paragraph(c,'<b>Power:</b> sensor VCC to 3V3, sensor GND to common GND. '
                'Verify the actual module operates reliably at 3.3 V. Never send a 5 V signal into '
                'an ESP32 GPIO. Use the module pin labels; clone pin order can differ.',45,175,W-90,SMALL)
    paragraph(c,'<b>Detection:</b> OUT should be HIGH when clear and LOW during a beanbag pass. '
              'GPIO34/35 have no internal pull-up; verify the sensor supplies a defined idle HIGH. '
              'Crossing wires without a junction dot are not connected.',45,y,W-90,SMALL)
    return finish(c,s)

def oled_diagram():
    c,s=page('5. Wiring diagram - OLED and buttons (2/3)',
             'Use printed pin labels, not terminal positions shown. SH1106 or SSD1306: select the actual driver in code.')
    controller(c,170,735)
    box(c,350,547,185,151,PALE,BLUE)
    label(c,'128 x 64 I2C OLED',362,677,10,NAVY,True)
    for pin,y,txt,col,wid in [('GPIO21',650,'SDA',BLUE,27),('GPIO22',625,'SCL',TEAL,28),('3V3',600,'VCC',RED,29),('GND',575,'GND',BLACK,30)]:
        terminal(c,pin,195,y); wire(c,[(195,y),(350,y)],col)
        label(c,f'W{wid}  35 cm',212,y+7,7.8,col,True)
        label(c,txt,361,y-3,8.8,col,True)
    wire(c,[(195,185),(548,185),(548,482)],BLACK,2)
    terminal(c,'GND',195,185)
    for name,gpio,y,wid,length,col in [('START / MODE',18,460,18,20,BLUE),('RESET',19,355,20,15,RED),('MISS',23,250,22,20,TEAL)]:
        box(c,350,y-27,175,70,PALE)
        label(c,name,362,y+23,10,NAVY,True)
        terminal(c,f'GPIO{gpio}',195,y)
        wire(c,[(195,y),(350,y)],col)
        label(c,f'W{wid}  {length} cm',212,y+7,8,col,True)
        dot(c,371,y,col); dot(c,396,y,col)
        wire(c,[(350,y),(371,y)],col)
        wire(c,[(373,y+3),(393,y+13)],col)
        wire(c,[(396,y),(525,y),(548,y)],BLACK)
        dot(c,548,y,BLACK)
        label(c,f'W{wid+1}  {length} cm to GND',404,y-15,7.3,BLACK)
    paragraph(c,'<b>Buttons:</b> momentary, normally open. One terminal goes to its GPIO; '
              'the other goes to GND. INPUT_PULLUP is enabled in the sketch, so no external '
              'button pull-up resistor is needed. For a four-leg button, use contacts on '
              'opposite switched sides, verified with a continuity tester.',45,135,W-90,SMALL)
    return finish(c,s)

def output_diagram():
    c,s=page('5. Wiring diagram - LEDs, buzzer and power (3/3)',
             'Use printed GPIO labels. One LED stays ON as the target; each LED needs its own 220-ohm resistor.')
    controller(c,175,735)
    wire(c,[(195,190),(537,190),(537,669)],BLACK,2)
    terminal(c,'GND',195,190)
    for val,gpio,y,wid,length,col in [(10,25,640,12,30,BLUE),(20,26,550,14,30,TEAL),(30,27,460,16,50,RED)]:
        terminal(c,f'GPIO{gpio}',195,y)
        wire(c,[(195,y),(254,y)],col)
        pts=[(254,y)]
        for i in range(8): pts.append((258+i*4,y+(5 if i%2==0 else -5)))
        pts += [(290,y),(392,y)]
        wire(c,pts,col)
        c.setFillColor(col); c.setStrokeColor(NAVY)
        c.circle(407,y,15,fill=1,stroke=1)
        wire(c,[(422,y),(537,y)],BLACK)
        dot(c,537,y,BLACK)
        label(c,f'{val}-POINT LED',367,y+29,9,NAVY,True)
        label(c,'220 ohm',252,y+15,8,INK,True)
        label(c,f'W{wid}  {length} cm',203,y-22,7.8,col)
        label(c,f'W{wid+1}  {length} cm',439,y-22,7.8,BLACK)
        label(c,'+',384,y-4,9,NAVY,True); label(c,'-',425,y-4,9,NAVY,True)
    box(c,350,245,162,111,PALE,BLUE)
    label(c,'ACTIVE BUZZER',362,336,10,NAVY,True)
    for pin,y,txt,col,wid in [('GPIO33',315,'SIG / S',GOLD,24),('3V3',283,'VCC',RED,25)]:
        terminal(c,pin,195,y); wire(c,[(195,y),(350,y)],col)
        label(c,f'W{wid}  30 cm',212,y+7,8,INK)
        label(c,txt,362,y-3,8.8,INK,True)
    label(c,'GND',460,263,8,BLACK,True)
    wire(c,[(512,267),(537,267)],BLACK); dot(c,537,267,BLACK)
    label(c,'W26  30 cm',434,232,7.8,BLACK)
    y=paragraph(c,'<b>USB power:</b> power the ESP32 through its Micro-USB socket using a data '
                'cable for programming, or a regulated 5 V USB source for play. W01: 3V3 to '
                'the 3V3 distribution bus (8 cm). W02: GND to the common GND bus (8 cm).',45,145,W-90,SMALL)
    paragraph(c,'<b>Module checks:</b> use a 3-pin low-trigger active buzzer rated to work at 3.3 V. '
              'A raw buzzer or higher-current lamp needs a suitable driver. LED anode (+) faces '
              'the resistor; cathode (- / flat side) goes to GND.',45,y,W-90,SMALL)
    return finish(c,s)

def materials():
    c,s=page('2. Hardware and materials reference',
             'Check these against the assembled project. The actual OLED driver determines the software setting.')
    y=table(c,[['ELECTRONICS','QTY','SPECIFICATION / USE'],
      ['WEMOS LOLIN32 / ESP32-WROOM','1','Micro-USB, existing controller.'],
      ['FC-51 / LM393 IR module','4','Three installed; one spare. Verify 3.3 V operation and active-LOW OUT.'],
      ['128 x 64 I2C OLED','1','SH1106 default; SSD1306 supported by changing OLED_IS_SH1106 to 0.'],
      ['Active buzzer module','1','Three pins; LOW-trigger; rated for 3.3 V operation.'],
      ['Momentary push button','3','Normally open: START/MODE, RESET and MISS.'],
      ['LEDs + 220-ohm resistors','3 + 3','Blue 10 / green 20 / red 30. One resistor per LED.'],
      ['Breadboard / perfboard / headers','1 set','Breadboard for tests; perfboard and removable headers for final wiring.'],
      ['22 AWG stranded wire','12 m','Original cut schedule uses 10.08 m; add service loops as specified.'],
      ['USB data cable / USB supply','1 each','Data cable for uploads; regulated 5 V USB power for the controller.'],
    ],[197,40,W-90-237],696)
    y=section(c,'Structure and finishing - retained design',y)
    y=table(c,[['PART / MATERIAL','REFERENCE'],
      ['Target face','600 x 750 x 9 mm plywood; three 150 mm holes.'],
      ['Base and supports','600 x 300 x 12 mm base; two side braces; about 11-degree rear tilt.'],
      ['Chutes and collection box','Three rear-covered, open-bottom chutes. See detailed cut lists and retained assembly diagrams.'],
      ['Beanbags','Six soft bags, about 90 x 90 mm; verify actual bags pass and trigger reliably.'],
      ['Finishing and fasteners','Paint, wood screws, M3 hardware, standoffs, cable ties, heat-shrink and labels.'],
    ],[197,W-90-197],y)
    paragraph(c,'<b>Display fit:</b> measure the actual module before cutting its window or mounting '
              'holes. Screen size alone does not establish SH1106 versus SSD1306. This revision '
              'uses your existing hardware; historical shopping links and price estimates are omitted.',
              45,y,W-90,SMALL)
    return finish(c,s)

def inventory():
    c,s=page('11. Complete inventory before final testing',
             'Reconciled with the two-mode sketch and the wiring diagrams in this revision.')
    y=table(c,[['ELECTRONIC PART','QTY','CHECK'],
      ['ESP32 + Micro-USB data cable','1 each','WEMOS LOLIN32 / ESP32-WROOM; cable supports data.'],
      ['FC-51 / LM393 modules','4','Three installed plus one spare; VCC/GND/OUT labels checked.'],
      ['128 x 64 I2C OLED','1','SH1106 or SSD1306; select the matching code option.'],
      ['Blue / green / red LEDs','3','Anodes face resistors; cathodes to common GND.'],
      ['220-ohm resistors','3','One per LED, in series.'],
      ['Normally-open push buttons','3','START/MODE, RESET, MISS; connect GPIO to GND.'],
      ['Low-trigger active buzzer','1','Three pins: SIG, VCC, GND; 3.3 V operation.'],
      ['Breadboard + perfboard','1 each','Perfboard at least 50 x 70 mm; no shorts.'],
      ['Headers + 3-pin connectors','1 set','Removable ESP32 and labeled sensor connections.'],
      ['USB power bank / supply','1','Stable regulated 5 V USB source; verify operation under load.'],
    ],[195,42,W-90-237],696)
    y=section(c,'Wire, mounting hardware and consumables',y)
    y=table(c,[['ITEM','REFERENCE QUANTITY / CHECK'],
      ['22 AWG stranded wire','12 m total: 4 m black, 2.5 m red, 5.5 m assorted signal colors.'],
      ['25 mm wood screws','About 80; use the retained structural cut list.'],
      ['M3 bolts, nuts, washers','About 25 sets; size to actual module mounting holes.'],
      ['Nylon standoffs','8; about 10 mm clearance for electronics.'],
      ['Jumpers / cable ties','About 40 jumpers for tests; 30 cable ties for cable routing.'],
      ['Heat-shrink + labels','Insulate every exposed joint; label both ends W01-W30.'],
    ],[195,W-90-195],y)
    paragraph(c,'<b>Tools:</b> multimeter, soldering tools, wire stripper, screwdrivers, ruler, '
              'square and appropriate woodworking tools. Keep beanbag paths clear and the '
              'electronics accessible for testing. The structural cut list follows on the next page.',
              45,y,W-90,SMALL)
    return finish(c,s)

def final_wiring():
    c,s=page('11.5 Wire, program, calibrate and finish',
             'The existing structural instructions and wire-cut schedule are retained.')
    table(c,[['STEP','DO THIS','PASS RESULT'],
      ['37','Build 3V3 and common-GND buses. Mount the ESP32 on headers and standoffs.','No 3V3-to-GND short.'],
      ['38','Sensors: OUT 10/20/30 to GPIO34/35/32. VCC to 3V3; GND common.','Clear HIGH; beanbag LOW.'],
      ['39','LEDs: GPIO25/26/27 through separate 220-ohm resistors to 10/20/30 anodes.','Each LED works independently.'],
      ['40','START/MODE to GPIO18; RESET to GPIO19; MISS to GPIO23. Other contact to GND.','Buttons normally open.'],
      ['41','OLED SDA21 / SCL22. Buzzer SIG33. Connect each VCC to 3V3 and GND common.','Module labels verified.'],
      ['42','Install W01-W30 using the retained cut schedule; insulate joints and secure cables.','No exposed copper or blocked chute.'],
      ['43','Select the correct OLED option in the sketch. Upload and open Serial Monitor at 115200.','Idle screen and READY message.'],
      ['44','Calibrate each sensor with every beanbag color; shield from ambient light.','20 passes detected once per hole.'],
      ['45','In Precision, follow the lit target for six throws.','Each zone selected twice; perfect score 120.'],
      ['46','Test one wrong hole and one MISS. Do not press MISS for a sensor-recorded throw.','Zero points; one throw each.'],
      ['47','In Chase, test correct hit, wrong hole, MISS and the 60-second deadline.','Only correct hits advance; timer ends round.'],
      ['48','Test RESET, replay, power-bank operation and cable security.','Stable operation during a full demo.'],
    ],[44,325,W-90-369],696)
    return finish(c,s)

def upload():
    c,s=page('12. Open and upload the Arduino sketch',
             'Use the supplied .ino file. The following pages reproduce exactly the same code.')
    y=paragraph(c,'<b>1.</b> Open <font face="Consolas">ishootmo_code.ino</font> from its '
                '<font face="Consolas">ishootmo_code</font> folder in Arduino IDE 2.x.<br/>'
                '<b>2.</b> In File &gt; Preferences, add the Espressif stable board-manager URL:',45,698,W-90)
    y=paragraph(c,'<font size="8.5" face="Consolas">https://espressif.github.io/arduino-esp32/package_esp32_index.json</font>',45,y,W-90)
    y=paragraph(c,'<b>3.</b> In Boards Manager, install <b>esp32 by Espressif Systems</b>. '
                'Select <b>WEMOS LOLIN32</b>. If unavailable, select ESP32 Dev Module.<br/>'
                '<b>4.</b> In Library Manager, install <b>Adafruit GFX Library</b>, '
                '<b>Adafruit SH110X</b> and <b>Adafruit SSD1306</b>. Accept dependencies, '
                'including Adafruit BusIO. Wire and esp_random come with the ESP32 core.',45,y,W-90)
    y=table(c,[['YOUR DISPLAY','SETTING NEAR TOP OF SKETCH'],
              ['SH1106, 128 x 64 I2C (default)','<font face="Consolas">#define OLED_IS_SH1106 1</font>'],
              ['SSD1306, 128 x 64 I2C','<font face="Consolas">#define OLED_IS_SH1106 0</font>']],
            [238,W-90-238],y-3)
    y=paragraph(c,'<b>5.</b> Verify the OLED model from its listing or label; screen size alone '
                'does not prove its driver. The default I2C address is <b>0x3C</b>. Use 0x3D '
                'only if your device is found there.<br/>'
                '<b>6.</b> Connect a Micro-USB <b>data</b> cable. Select the correct COM port. '
                'Click Verify, then Upload. If it remains on Connecting, hold BOOT while '
                'connecting and release when writing starts.<br/>'
                '<b>7.</b> Open Serial Monitor at <b>115200 baud</b>. Tap START/MODE to '
                'select a mode, then hold it for 0.9 second to begin.',45,y,W-90)
    y=section(c,'Firmware settings you may adjust',y-5)
    y=paragraph(c,'<font face="Consolas">SENSOR_LOCKOUT_MS = 650</font>: minimum time between '
                'counted throw events. Increase only if one bag still counts twice.<br/>'
                '<font face="Consolas">CHASE_SEQUENCE_SEED</font>: changing it prepares a '
                'different Chase sequence. Keep it fixed for all competitors.',45,y,W-90,SMALL)
    paragraph(c,'<b>Verification:</b> compiled for WEMOS LOLIN32 with the installed ESP32 core '
              '3.3.11. Both OLED settings and eight compile-time game-rule checks were verified. '
              'No firmware has been uploaded to the assembled hardware during this revision.',45,y,W-90,SMALL)
    return finish(c,s)

def code_pages():
    lines=SKETCH.read_text(encoding='utf-8').splitlines()
    per=62
    pages=[]
    total=math.ceil(len(lines)/per)
    for start in range(0,len(lines),per):
        c,s=page(f'13. Complete Arduino sketch ({start//per+1}/{total})',
                 'Exact listing of the supplied ishootmo_code.ino. Prefer opening the file rather than copying from PDF.')
        c.setFillColor(PALE); c.roundRect(45,65,W-90,639,6,fill=1,stroke=0)
        for offset,line in enumerate(lines[start:start+per]):
            y=689-offset*10.05
            c.setFont('Consolas',7.6)
            c.setFillColor(MUTED); c.drawString(54,y,f'{start+offset+1:03d}')
            c.setFillColor(TEAL if line.lstrip().startswith('//') else INK)
            c.drawString(77,y,line)
        pages.append(finish(c,s))
    return pages

def troubleshooting():
    c,s=page('14. Troubleshooting and final checks',
             'Check module labels and the Serial Monitor before changing the wiring or scoring rules.')
    y=table(c,[['SYMPTOM','CHECK'],
      ['Code will not compile','Install the display libraries and dependencies. Select the ESP32 board package, not an AVR board.'],
      ['Blank / scrambled OLED','Match OLED_IS_SH1106 to the real driver. Check 128 x 64 resolution, SDA21, SCL22, 3V3/GND and I2C address.'],
      ['Sensor never triggers / stays LOW','Adjust sensitivity, shield ambient light and verify reliable 3.3 V operation. Check actual VCC/GND/OUT labels.'],
      ['Wrong hole gives no points','Expected: only the lit hole scores. The wrong-hole counter increases automatically.'],
      ['One bag counts twice','Adjust mounting and sensitivity first. Increase SENSOR_LOCKOUT_MS if needed; this also limits how fast throws can count.'],
      ['MISS seems ignored','MISS counts only during a game and outside the throw lockout. Never use it after a hole sensor recorded the throw.'],
      ['Multiple-sensor warning','Check sensor aiming, shielding and wiring. Simultaneous hole detections are ignored to avoid favoring one hole.'],
      ['Buzzer stays on','Sketch expects LOW-trigger active module. Verify module type and idle HIGH signal.'],
      ['Upload fails / board resets','Use a data cable and correct COM port. Try BOOT during connection. Inspect loose connections, shorts and USB supply.'],
    ],[148,W-90-148],696)
    y=section(c,'Acceptance checklist',y-1)
    y=paragraph(c,'1. Each sensor detects 20 beanbag passes without duplicates.<br/>'
                '2. Precision selects each hole twice and ends after six attempts; six correct hits total 120.<br/>'
                '3. A wrong hole and a manual MISS each count once and award zero.<br/>'
                '4. Chase repeats the same sequence, advances only on correct hits and stops at 60 seconds.<br/>'
                '5. RESET clears the round; START returns from the final screen.<br/>'
                '6. LEDs identify the active target; wires stay clear of beanbags; run a full demo on USB power.',45,y,W-90,SMALL)
    y=section(c,'Official technical references',y-14)
    links=[('Espressif Arduino-ESP32 installation','https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html'),
           ('ESP32 GPIO capabilities','https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gpio.html'),
           ('Adafruit SH1106G library reference','https://adafruit.github.io/Adafruit_SH110x/html/class_adafruit___s_h1106_g.html'),
           ('Adafruit SSD1306 examples','https://learn.adafruit.com/monochrome-oled-breakouts/arduino-library-and-examples'),
           ('WEMOS LOLIN32 physical board layout (manufacturer archive)','https://img.sauf.ca/pictures/2017-11-04/8c3db4dd04f1c0bfdd1a99da9106e0c5.pdf')]
    for text,url in links:
        y=paragraph(c,f'<link href="{url}" color="#147bb2">{text}</link>',45,y,W-90,SMALL)
    return finish(c,s)

def update_display_text(p, reader):
    content=p.get_contents()
    if not content: return
    stream=ContentStream(content,reader)
    def change(obj):
        if isinstance(obj,TextStringObject):
            return TextStringObject(str(obj).replace('SSD1306','SH1106').replace('0.96 in','1.3 in'))
        return obj
    for operands,operator in stream.operations:
        if operator in [b'Tj',b"'",b'"']:
            for i,item in enumerate(operands): operands[i]=change(item)
        elif operator==b'TJ':
            for i,item in enumerate(operands[0]): operands[0][i]=change(item)
    p.replace_contents(stream)

def footer(p, number, total):
    width,height=float(p.mediabox.width),float(p.mediabox.height)
    stream=BytesIO(); c=canvas.Canvas(stream,pagesize=(width,height))
    c.setFillColor(WHITE); c.rect(0,0,width,31,fill=1,stroke=0)
    c.setStrokeColor(colors.HexColor('#d4e0e9')); c.setLineWidth(.5)
    c.line(45,30,width-45,30)
    label(c,'WEMOS LOLIN32 | Two modes | Revision 13 Nov 2026',45,16,7.1,MUTED)
    c.setFont('Helvetica',7.1); c.setFillColor(MUTED)
    c.drawRightString(width-45,16,f'Page {number} of {total}')
    c.save(); reader=PdfReader(BytesIO(stream.getvalue())); GENERATED.append(reader)
    p.merge_page(reader.pages[0])

def main():
    WORK.mkdir(parents=True,exist_ok=True)
    original=PdfReader(SOURCE)
    for p in original.pages: update_display_text(p,original)
    pages=[cover(),design(),fairness()]
    pages += [materials(),original.pages[3],original.pages[4]]
    pages += illustrated_pages()
    pages += [inventory() if i==15 else original.pages[i] for i in range(8,20)]
    pages += [final_wiring(),upload()]
    pages += code_pages()
    pages += [troubleshooting()]
    writer=PdfWriter()
    for i,p in enumerate(pages):
        if i: footer(p,i+1,len(pages))
        writer.add_page(p)
    writer.add_metadata({'/Title':'I-Shoot Mo - ESP32 Complete Build Guide',
                         '/Subject':'Precision and Target Chase; wiring and Arduino sketch',
                         '/Author':'I-Shoot Mo Project', '/Keywords':'ESP32 LOLIN32 SH1106 SSD1306 beanbag'})
    for name,index in [('Game rules',1),('Fairness and controls',2),('GPIO plan',4),
                       ('Wiring diagrams',6),('Wire cut schedule',10),('Construction',13),
                       ('Upload instructions',23),('Complete Arduino sketch',24),
                       ('Troubleshooting',len(pages)-1)]:
        writer.add_outline_item(name,index)
    temp=WORK/'revised-guide.pdf'
    with temp.open('wb') as f: writer.write(f)
    import shutil
    shutil.copy2(temp,TARGET)
    print(f'Updated {TARGET.name}: {len(pages)} pages; {len(SKETCH.read_text().splitlines())} code lines.')

if __name__=='__main__': main()
