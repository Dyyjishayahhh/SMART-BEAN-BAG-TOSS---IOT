"""Component-level, physically anchored illustrations for the LOLIN32 guide."""
from pathlib import Path
from io import BytesIO
import json
import math
from reportlab.pdfgen import canvas
from reportlab.lib import colors
from reportlab.lib.pagesizes import A3, landscape
from pypdf import PdfReader

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'tmp/pdfs'
DW, DH = 1600, 1132
PAGE = landscape(A3)
C = {k: colors.HexColor(v) for k,v in {
    'ink':'#17344a','muted':'#5c7182','pcb':'#171f25','gold':'#d9b865',
    'red':'#e34144','gnd':'#263440','blue':'#267bd7','green':'#19916e',
    'orange':'#ed8734','teal':'#079ba2','cyan':'#24bad0','yellow':'#dcae16',
    'purple':'#9952c9','gray':'#778594','white':'#f8fafc',
}.items()}

# Manufacturer photograph, antenna up / USB down. Left: 16 pads; right: 20.
# Source: archived WEMOS LOLIN32 v1.0.0 product sheet, 27 May 2017.
LEFT = ['3V','EN','VP','VN','32','33','34','35','25','26','27','14','12','13','5V','GND']
RIGHT = ['GND','TX','RX','3V','22','21','GND','GND','19','23','18','5','3V','17','16','4','0','GND','2','15']
EXPECTED = {
    'W01':('ESP:L3V','RAIL:3V3'), 'W02':('ESP:LGND','RAIL:GND'),
    'W03':('S30:OUT','ESP:L32'), 'W04':('S30:VCC','RAIL:3V3'), 'W05':('S30:GND','RAIL:GND'),
    'W06':('S10:OUT','ESP:L34'), 'W07':('S10:VCC','RAIL:3V3'), 'W08':('S10:GND','RAIL:GND'),
    'W09':('S20:OUT','ESP:L35'), 'W10':('S20:VCC','RAIL:3V3'), 'W11':('S20:GND','RAIL:GND'),
    'W12':('ESP:L25','LED10:R_IN'), 'W13':('LED10:K','RAIL:GND'),
    'W14':('ESP:L26','LED20:R_IN'), 'W15':('LED20:K','RAIL:GND'),
    'W16':('ESP:L27','LED30:R_IN'), 'W17':('LED30:K','RAIL:GND'),
    'W18':('ESP:R18','START:T1'), 'W19':('START:T2','RAIL:GND'),
    'W20':('ESP:R19','RESET:T1'), 'W21':('RESET:T2','RAIL:GND'),
    'W22':('ESP:R23','MISS:T1'), 'W23':('MISS:T2','RAIL:GND'),
    'W24':('ESP:L33','BUZZ:SIG'), 'W25':('BUZZ:VCC','RAIL:3V3'), 'W26':('BUZZ:GND','RAIL:GND'),
    'W27':('ESP:R21','OLED:SDA'), 'W28':('ESP:R22','OLED:SCL'),
    'W29':('OLED:VCC','RAIL:3V3'), 'W30':('OLED:GND','RAIL:GND'),
}
LENGTH = {1:8,2:8,3:60,4:60,5:60,6:42,7:42,8:42,9:42,10:42,11:42,
          12:30,13:30,14:30,15:30,16:50,17:50,18:20,19:20,20:15,21:15,
          22:20,23:20,24:30,25:30,26:30,27:35,28:35,29:35,30:35}

def text(c, value, x, y, size=14, color='ink', bold=False, align='left'):
    c.setFillColor(C.get(color,color))
    c.setFont('Helvetica-Bold' if bold else 'Helvetica',size)
    getattr(c, {'left':'drawString','right':'drawRightString','center':'drawCentredString'}[align])(x,y,value)

def roundbox(c,x,y,w,h,fill,stroke=None,r=8):
    c.setFillColor(C.get(fill,fill)); c.setStrokeColor(C.get(stroke or fill,stroke or fill))
    c.roundRect(x,y,w,h,r,fill=1,stroke=bool(stroke))

def screw(c,x,y,r=5):
    c.setFillColor(colors.HexColor('#d5b97e')); c.circle(x,y,r,fill=1,stroke=0)
    c.setFillColor(colors.HexColor('#415363')); c.circle(x,y,r*.55,fill=1,stroke=0)

class Scene:
    def __init__(self,title,sub,number):
        self.title,self.sub,self.number=title,sub,number
        self.parts=[]; self.pins={}; self.wires=[]
    def part(self,name,x,y,scale,draw,anchors):
        self.parts.append((x,y,scale,draw))
        for key,(px,py) in anchors.items(): self.pins[name+':'+key]=(x+px*scale,y+py*scale)
    def connect(self,n,a,b,via,color,label=None):
        wid=f'W{n:02d}'
        assert (a,b)==EXPECTED[wid], (wid,a,b,EXPECTED[wid])
        assert not any(w['id']==wid for w in self.wires), wid
        self.wires.append(dict(id=wid,a=a,b=b,points=[self.pins[a],*via,self.pins[b]],
                               color=color,label=label,length=LENGTH[n]))
    def finish(self,c):
        c.saveState(); c.scale(PAGE[0]/DW,PAGE[1]/DH)
        c.setFillColor(colors.HexColor('#f5f8fb')); c.rect(0,0,DW,DH,fill=1,stroke=0)
        c.setFillColor(C['ink']); c.rect(0,DH-122,DW,122,fill=1,stroke=0)
        text(c,'I-SHOOT MO  /  COMPONENT WIRING',36,DH-37,16,colors.HexColor('#b3d2e7'),True)
        text(c,self.title,36,DH-79,29,colors.white,True)
        text(c,f'{self.number:02d} / 04',DW-40,DH-45,15,colors.white,True,'right')
        text(c,self.sub,36,DH-150,14)
        # Simulator-like dotted work surface, with a generous space for parts.
        c.setFillColor(colors.HexColor('#dbe4ec'))
        for x in range(30,DW,20):
            for y in range(230,970,20): c.circle(x,y,.8,fill=1,stroke=0)
        for x,y,s,draw in self.parts:
            c.saveState(); c.translate(x,y); c.scale(s,s); draw(c); c.restoreState()
        # White edge separates crossing jumper wires without implying junctions.
        for w in self.wires:
            p=c.beginPath(); p.moveTo(*w['points'][0])
            for xy in w['points'][1:]: p.lineTo(*xy)
            c.setLineJoin(1); c.setLineCap(1)
            c.setStrokeColor(colors.HexColor('#f5f8fb')); c.setLineWidth(7); c.drawPath(p)
            c.setStrokeColor(C[w['color']]); c.setLineWidth(3.5); c.drawPath(p)
            for xy in (w['points'][0],w['points'][-1]):
                c.setFillColor(C[w['color']]); c.circle(*xy,3.1,fill=1,stroke=0)
        for w in self.wires:
            if not w['label']: continue
            x,y=w['label']
            msg=w['id'] if self.number==1 else f"{w['id']}  {w['length']} cm"
            c.setFont('Helvetica-Bold',11)
            width=c.stringWidth(msg,'Helvetica-Bold',11)+13
            roundbox(c,x-5,y-5,width,19,colors.white,r=4)
            text(c,msg,x,y,11,w['color'],True)
        # Light explanatory band, separate from the editable wiring canvas.
        text(c,'RED = 3.3V   |   BLACK = GND   |   OTHER COLORS = SIGNALS',36,93,13,'ink',True)
        text(c,'Antenna up, Micro-USB down. Follow the printed pin name if a clone differs. Crossing wires do not join unless they share a terminal.',36,71,12,'muted')
        text(c,'FC-51 and buzzer pin order can vary: use the VCC / GND / OUT or SIG labels on your actual module.',36,51,12,'muted')
        c.restoreState(); c.showPage()

def board(scene,x,y,s=1):
    anchors={}
    for side,names,px in [('L',LEFT,14),('R',RIGHT,226)]:
        for i,name in enumerate(names):
            key=side+name
            if key in anchors: key+=str(i)
            anchors[key]=(px,490-i*22)
    def draw(c):
        roundbox(c,5,-5,240,560,colors.HexColor('#ccd5dc'),r=16)
        roundbox(c,0,0,240,560,'pcb',colors.HexColor('#080c10'),16)
        for a,b in [(21,540),(219,540)]: screw(c,a,b,8)
        # Antenna trace and metal radio shield.
        c.setStrokeColor(colors.HexColor('#8e7c53')); c.setLineWidth(3)
        p=c.beginPath(); p.moveTo(60,540)
        for px,py in [(180,540),(180,531),(73,531),(73,522),(167,522),(167,513),(88,513)]: p.lineTo(px,py)
        c.drawPath(p)
        roundbox(c,49,320,142,185,colors.HexColor('#a5afb3'),colors.HexColor('#77848a'),3)
        roundbox(c,53,324,134,177,colors.HexColor('#c5cbcc'),r=2)
        text(c,'ESP-WROOM-32',120,460,12,colors.HexColor('#657379'),True,'center')
        text(c,'Wi-Fi + Bluetooth',120,439,8,colors.HexColor('#7d8a8c'),False,'center')
        for i,txt in enumerate(['ESPRESSIF','FCC ID: 2AC7Z','CE  1313']):
            text(c,txt,120,399-i*19,9,colors.HexColor('#859191'),False,'center')
        for i in range(11):
            c.setFillColor(C['gold']); c.rect(44,330+i*14,4,6,fill=1,stroke=0)
            c.rect(192,330+i*14,4,6,fill=1,stroke=0)
        for side,names,px in [('L',LEFT,14),('R',RIGHT,226)]:
            for i,name in enumerate(names):
                py=490-i*22
                c.setFillColor(C['gold']); c.ellipse(px-7,py-4,px+7,py+4,fill=1,stroke=0)
                c.setFillColor(colors.HexColor('#5a4c2c')); c.circle(px,py,2,fill=1,stroke=0)
                text(c,name,27 if side=='L' else 213,py+5,9.8,colors.white,True,'left' if side=='L' else 'right')
        # Reset switch, supporting ICs, capacitors and silkscreen.
        roundbox(c,111,254,26,42,colors.HexColor('#b9bec2'),r=2)
        c.setFillColor(colors.HexColor('#3b4147')); c.circle(124,275,9,fill=1,stroke=0)
        text(c,'RESET',143,272,8,colors.white)
        text(c,'LOLIN32',119,198,16,colors.white,True,'center')
        text(c,'V1.0.0',133,181,9,colors.white,False,'center')
        for bx,by,bw,bh in [(55,260,27,19),(75,217,19,29),(133,77,36,37),(52,108,27,18),(86,137,20,21)]:
            roundbox(c,bx,by,bw,bh,colors.HexColor('#080d12'),r=1)
            for q in range(4):
                c.setFillColor(colors.HexColor('#8c959b'))
                c.rect(bx-4,by+3+q*7,4,3,fill=1,stroke=0)
                c.rect(bx+bw,by+3+q*7,4,3,fill=1,stroke=0)
        for bx,by in [(90,302),(159,300),(60,239),(166,239),(94,106),(181,105),(59,177),(80,176)]:
            roundbox(c,bx,by,11,5,colors.HexColor('#bcae8b'),r=1)
        roundbox(c,111,220,12,5,colors.HexColor('#87d19c'),r=1)
        roundbox(c,46,151,12,5,colors.HexColor('#d4aa3c'),r=1)
        text(c,'CHG',62,151,7,colors.white)
        # Unused JST battery socket; USB connector is the power entry.
        roundbox(c,26,22,48,43,colors.HexColor('#eee1bd'),r=2)
        roundbox(c,30,28,31,27,colors.HexColor('#b6a789'),r=1)
        for py in [32,48]:
            c.setStrokeColor(colors.HexColor('#bdbdb3')); c.setLineWidth(2); c.line(62,py,88,py)
        roundbox(c,107,-5,60,40,colors.HexColor('#bec7cc'),colors.HexColor('#6e7b83'),4)
        roundbox(c,114,-5,46,15,colors.HexColor('#47525a'),r=3)
        for px in range(121,155,6):
            c.setFillColor(C['gold']); c.rect(px,-1,3,5,fill=1,stroke=0)
    anchors['USB']=(137,-6)
    scene.part('ESP',x,y,s,draw,anchors)

def rails(scene,x=50,y=110,width=1500):
    def draw(c):
        roundbox(c,3,-4,width,104,colors.HexColor('#d8e0e5'),r=10)
        roundbox(c,0,0,width,104,colors.HexColor('#fefefe'),colors.HexColor('#cbd4dc'),10)
        for py,col,mark in [(70,'red','+'),(40,'blue','-')]:
            c.setStrokeColor(C[col]); c.setLineWidth(2); c.line(20,py+11,width-20,py+11)
            for px in range(28,int(width)-16,18):
                roundbox(c,px-2.4,py-2.4,4.8,4.8,colors.HexColor('#b6bec7'),r=.8)
            text(c,mark,8,py-3,13,col,True)
        text(c,'3.3 V',width-28,86,10,'red',True,'right')
        text(c,'GND',width-28,22,10,'gnd',True,'right')
        text(c,'COMMON POWER RAILS',width/2,8,10,'muted',True,'center')
    scene.part('POWER',x,y,1,draw,{})
    # Each wire lands on a different hole; row copper connects the holes.
    for n in range(1,31):
        scene.pins[f'RAIL:3V3@{n}']=(x+28+18*((n*3)%int((width-60)/18)),y+70)
        scene.pins[f'RAIL:GND@{n}']=(x+28+18*((n*2)%int((width-60)/18)),y+40)
    scene.pins['RAIL:3V3']=(x+width-60,y+70)
    scene.pins['RAIL:GND']=(x+width-42,y+40)

def sensor(scene,name,x,y,s=1):
    def draw(c):
        roundbox(c,3,-4,240,160,colors.HexColor('#cad7df'),r=8)
        roundbox(c,0,0,240,160,colors.HexColor('#1677ae'),colors.HexColor('#095884'),7)
        for xx,yy in [(13,13),(227,13),(13,147),(227,147)]: screw(c,xx,yy,5)
        text(c,'FC-51',119,143,12,colors.white,True,'center')
        text(c,'IR OBSTACLE SENSOR',120,105,8,colors.HexColor('#e0f3ff'),False,'center')
        # Visible emitter and receiver barrels extend from the board's front.
        for xx,col in [(62,colors.HexColor('#e0e9ee')),(177,colors.HexColor('#171d22'))]:
            roundbox(c,xx-15,133,30,49,col,colors.HexColor('#7a8b96'),9)
            c.setFillColor(colors.HexColor('#f6fafc') if xx==62 else colors.HexColor('#4b535a'))
            c.ellipse(xx-11,173,xx+11,185,fill=1,stroke=0)
        # LM393 comparator, trim potentiometer and two indicator LEDs.
        roundbox(c,36,61,60,25,colors.HexColor('#161b22'),r=2)
        for q in range(4):
            c.setFillColor(colors.HexColor('#c7cdd0')); c.rect(41+q*14,56,5,5,fill=1,stroke=0)
            c.rect(41+q*14,86,5,5,fill=1,stroke=0)
        text(c,'LM393',66,70,7,colors.HexColor('#a3b0bd'),False,'center')
        roundbox(c,141,52,56,39,colors.HexColor('#17417d'),colors.HexColor('#aabcc7'),3)
        c.setFillColor(colors.HexColor('#d7dce0')); c.circle(169,72,13,fill=1,stroke=0)
        c.setStrokeColor(colors.HexColor('#5c6a76')); c.setLineWidth(3); c.line(160,72,178,72)
        for xx,col in [(112,'red'),(126,'green')]: roundbox(c,xx,60,8,5,C[col],r=1)
        for xx,label in [(55,'VCC'),(120,'GND'),(185,'OUT')]:
            text(c,label,xx,22,10,colors.white,True,'center')
            roundbox(c,xx-7,-6,14,18,colors.HexColor('#1b2530'),r=1)
            c.setFillColor(C['gold']); c.rect(xx-2,-14,4,19,fill=1,stroke=0)
    scene.part(name,x,y,s,draw,{'VCC':(55,-14),'GND':(120,-14),'OUT':(185,-14)})

def oled(scene,x,y,s=1):
    def draw(c):
        roundbox(c,3,-4,240,190,colors.HexColor('#c8d7e1'),r=9)
        roundbox(c,0,0,240,190,colors.HexColor('#136a98'),colors.HexColor('#0c4c70'),7)
        for xx,yy in [(12,12),(228,12),(12,178),(228,178)]: screw(c,xx,yy,5)
        roundbox(c,19,40,202,130,colors.HexColor('#10181f'),r=2)
        roundbox(c,27,47,186,117,colors.HexColor('#07303d'),r=1)
        text(c,'I-SHOOT MO',120,143,15,colors.HexColor('#5ae1ff'),True,'center')
        text(c,'1 PRECISION',42,116,12,colors.HexColor('#5ae1ff'),True)
        text(c,'S:0    AIM:30',42,89,12,colors.HexColor('#5ae1ff'),True)
        text(c,'128 x 64 I2C OLED',120,178,9,colors.white,True,'center')
        for xx,name in [(40,'GND'),(90,'VCC'),(140,'SCL'),(190,'SDA')]:
            text(c,name,xx,21,8.5,colors.white,True,'center')
            roundbox(c,xx-6,-7,12,19,colors.HexColor('#17232d'),r=1)
            c.setFillColor(C['gold']); c.rect(xx-2,-14,4,19,fill=1,stroke=0)
    scene.part('OLED',x,y,s,draw,{'GND':(40,-14),'VCC':(90,-14),'SCL':(140,-14),'SDA':(190,-14)})

def button(scene,name,x,y,s=1,color='blue'):
    def draw(c):
        # Panel-mount, two-terminal momentary switch: cap, metal bezel and tabs.
        roundbox(c,-37,-2,74,75,colors.HexColor('#4d5963'),r=7)
        for yy in [9,17,25]:
            c.setStrokeColor(colors.HexColor('#869098')); c.setLineWidth(2); c.line(-37,yy,37,yy)
        c.setFillColor(colors.HexColor('#d9dfe3')); c.circle(0,69,44,fill=1,stroke=0)
        c.setFillColor(colors.HexColor('#8f9da6')); c.circle(0,69,38,fill=1,stroke=0)
        c.setFillColor(C[color]); c.circle(0,73,33,fill=1,stroke=0)
        c.setFillColor(colors.Color(1,1,1,alpha=.25)); c.ellipse(-24,78,16,95,fill=1,stroke=0)
        for xx in [-22,22]:
            roundbox(c,xx-5,-20,10,28,colors.HexColor('#bac3c9'),r=2)
            c.setFillColor(colors.HexColor('#697987')); c.circle(xx,-13,2.5,fill=1,stroke=0)
        text(c,name,0,132,14,'ink',True,'center')
    scene.part(name,x,y,s,draw,{'T1':(-22,-20),'T2':(22,-20)})

def led(scene,name,x,y,s=1,color='blue',compact=False):
    def draw(c):
        # LED dome and separate axial 220-ohm resistor on the anode lead.
        c.setStrokeColor(colors.HexColor('#a2adb6')); c.setLineWidth(4)
        c.line(-12,-36,-12,6); c.line(12,-36,12,6)
        roundbox(c,-22,0,44,37,C[color],r=8)
        c.setFillColor(C[color]); c.circle(0,35,22,fill=1,stroke=0)
        roundbox(c,-26,-2,52,7,C[color],r=2)
        c.setFillColor(colors.Color(1,1,1,alpha=.35)); c.ellipse(-15,25,-4,48,fill=1,stroke=0)
        c.setStrokeColor(colors.HexColor('#a2adb6')); c.setLineWidth(3)
        c.line(-12,-132,-12,-36)
        roundbox(c,-23,-112,22,45,colors.HexColor('#d9c49a'),colors.HexColor('#b29c73'),5)
        for yy,col in [(-76,'red'),(-86,'red'),(-96,colors.HexColor('#7f4c2d')),(-106,'gold')]:
            c.setFillColor(C.get(col,col)); c.rect(-23,yy,22,4,fill=1,stroke=0)
        if compact:
            text(c,name.replace('LED',''),0,20,13,colors.white,True,'center')
        else:
            text(c,name.replace('LED','')+'-POINT',0,77,13,'ink',True,'center')
        text(c,'220 ohm',20,-93,11,'muted')
        text(c,'A',-19,-49,10,'ink',True,'right'); text(c,'K',23,-33,10,'ink',True)
    scene.part(name,x,y,s,draw,{'R_IN':(-12,-132),'K':(12,-36)})

def buzzer(scene,x,y,s=1,compact=False):
    def draw(c):
        roundbox(c,0,0,170,150,colors.HexColor('#1874a7'),colors.HexColor('#0d4f75'),6)
        for xx,yy in [(12,12),(158,12),(12,138),(158,138)]: screw(c,xx,yy,4)
        c.setFillColor(colors.HexColor('#1b2329')); c.circle(85,91,48,fill=1,stroke=0)
        c.setFillColor(colors.HexColor('#303d46')); c.circle(85,95,41,fill=1,stroke=0)
        c.setFillColor(colors.HexColor('#080d11')); c.circle(85,95,9,fill=1,stroke=0)
        text(c,'BUZZER' if compact else 'ACTIVE',85,64,10,colors.HexColor('#81949f'),True,'center')
        for xx,label in [(30,'SIG'),(85,'GND'),(140,'VCC')]:
            text(c,label,xx,21,8.5,colors.white,True,'center')
            roundbox(c,xx-6,-6,12,17,colors.HexColor('#17232d'),r=1)
            c.setFillColor(C['gold']); c.rect(xx-2,-14,4,19,fill=1,stroke=0)
        if not compact: text(c,'LOW-TRIGGER BUZZER',85,172,13,'ink',True,'center')
    scene.part('BUZZ',x,y,s,draw,{'SIG':(30,-14),'GND':(85,-14),'VCC':(140,-14)})

def usb_supply(scene,x,y,s=1):
    def draw(c):
        roundbox(c,3,-4,220,80,colors.HexColor('#d5dee5'),r=13)
        roundbox(c,0,0,220,80,colors.HexColor('#f6f7f8'),colors.HexColor('#b7c4cd'),13)
        text(c,'USB POWER',110,48,14,'ink',True,'center')
        text(c,'5 V INPUT TO ESP32 ONLY',110,25,10,'muted',False,'center')
        roundbox(c,-8,25,12,27,colors.HexColor('#6c7a84'),r=2)
        for i in range(4):
            c.setFillColor(C['blue']); c.circle(164+i*10,65,2,fill=1,stroke=0)
    scene.part('USB',x,y,s,draw,{'PORT':(-8,39)})
    # Thick black USB cable, apart from numbered circuit hookup wires.
    a=scene.pins['ESP:USB']; b=scene.pins['USB:PORT']
    def cable(c):
        c.setStrokeColor(colors.HexColor('#384550')); c.setLineWidth(10); c.setLineCap(1)
        p=c.beginPath(); p.moveTo(*a); p.lineTo(a[0],b[1]); p.lineTo(*b); c.drawPath(p)
        text(c,'Micro-USB data / power cable',min(a[0],b[0]),min(a[1],b[1])-22,11,'muted')
    scene.parts.append((0,0,1,cable))

def endpoint(scene,which,n):
    # Land on an individual, visible rail hole instead of stacking all wires.
    p=scene.pins[f'RAIL:{which}@{n}']
    scene.pins[f'RAIL:{which}']=p
    return p

def add(scene,n,via,color,label=None):
    a,b=EXPECTED[f'W{n:02}']
    if b.startswith('RAIL:'): endpoint(scene,b.split(':')[1],n)
    scene.connect(n,a,b,via,color,label)

def detail_sensors():
    z=Scene('IR SENSORS - REAL HEADER CONNECTIONS','Three FC-51 modules, 3.3V power rails and the actual LOLIN32 header layout.',2)
    board(z,1040,315,1.05); rails(z)
    for name,base in [('S30',720),('S20',495),('S10',270)]:
        sensor(z,name,220,base)
        def tag(c,n=name,b=base): text(c,n[1:]+'-POINT HOLE',520,b+108,16,'ink',True)
        z.parts.append((0,0,1,tag))
    add(z,1,[(970,z.pins['ESP:L3V'][1]),(970,180)],'red',(973,850))
    add(z,2,[(950,z.pins['ESP:LGND'][1]),(950,150)],'gnd',(953,405))
    for name,out,vcc,gnd,viax,signal,color,px,gx in [
        ('S30',3,4,5,900,'L32','blue',120,60),
        ('S20',9,10,11,930,'L35','green',140,80),
        ('S10',6,7,8,870,'L34','orange',160,100)]:
        sx,sy=z.pins[name+':OUT']; tx,ty=z.pins['ESP:'+signal]
        add(z,out,[(sx,sy-14),(viax,sy-14),(viax,ty)],color,(600,sy-6))
        vx,vy=z.pins[name+':VCC']; ex,ey=endpoint(z,'3V3',vcc)
        add(z,vcc,[(vx,vy-10),(px,vy-10),(px,ey)],'red',(174,vy-4))
        gx0,gy=z.pins[name+':GND']; ex,ey=endpoint(z,'GND',gnd)
        add(z,gnd,[(gx0,gy-20),(gx,gy-20),(gx,ey)],'gnd',(55,gy-14))
    return z

def detail_controls():
    z=Scene('OLED + THREE BUTTONS - FULL COMPONENT VIEW','Four-pin I2C OLED and two-terminal normally-open buttons. Internal button pull-ups are enabled in the sketch.',3)
    board(z,220,315,1.05); rails(z)
    oled(z,1010,700,1.2)
    for name,cx,col in [('START',825,'blue'),('RESET',1085,'red'),('MISS',1345,'yellow')]: button(z,name,cx,345,1.1,col)
    add(z,1,[(140,z.pins['ESP:L3V'][1]),(140,180)],'red',(145,815))
    add(z,2,[(120,z.pins['ESP:LGND'][1]),(120,150)],'gnd',(125,420))
    for n,pin,target,lane,y,col in [(27,'R21','SDA',760,630,'teal'),(28,'R22','SCL',790,650,'cyan')]:
        tx,ty=z.pins['OLED:'+target]
        add(z,n,[(lane,z.pins['ESP:'+pin][1]),(lane,y),(tx,y)],col,(800,y+9))
    vx,vy=z.pins['OLED:VCC']; add(z,29,[(vx,665),(1430,665),(1430,180)],'red',(1260,675))
    gx,gy=z.pins['OLED:GND']; add(z,30,[(gx,605),(1470,605),(1470,150)],'gnd',(1260,615))
    for n,gpin,name,lane,yy,col in [(18,'R18','START',710,300,'blue'),(20,'R19','RESET',680,280,'gray'),(22,'R23','MISS',650,260,'purple')]:
        tx,ty=z.pins[name+':T1']
        add(z,n,[(lane,z.pins['ESP:'+gpin][1]),(lane,yy),(tx,yy)],col,(lane+8,yy+9))
        gx,gy=z.pins[name+':T2']; end=endpoint(z,'GND',n+1)
        add(z,n+1,[(gx,225),(end[0],225)],'gnd',(gx+8,240))
    return z

def detail_outputs():
    z=Scene('LEDs + BUZZER + USB POWER - FULL COMPONENT VIEW','Every LED has a physical 220-ohm resistor. The USB power bank powers the ESP32, while modules use 3.3V.',4)
    board(z,1060,315,1.05); rails(z)
    for val,cx,col in [(10,270,'blue'),(20,500,'green'),(30,730,'red')]: led(z,'LED'+str(val),cx,700,1.1,col)
    buzzer(z,540,270,1.15); usb_supply(z,1320,230,.9)
    add(z,1,[(1490,z.pins['ESP:L3V'][1]),(1490,180)],'red',(1380,850))
    # Route the supply around the board, never across the radio module.
    z.wires[-1]['points']=[z.pins['ESP:L3V'],(1000,z.pins['ESP:L3V'][1]),(1000,930),(1555,930),(1555,180),z.pins['RAIL:3V3']]
    add(z,2,[(1030,z.pins['ESP:LGND'][1]),(1030,150)],'gnd',(1038,420))
    for n,pin,val,lane,yy,col in [(12,'L25',10,970,540,'blue'),(14,'L26',20,940,520,'green'),(16,'L27',30,910,500,'orange')]:
        tx,ty=z.pins[f'LED{val}:R_IN']
        add(z,n,[(lane,z.pins['ESP:'+pin][1]),(lane,yy),(tx,yy)],col,(550,yy+8))
        gx,gy=z.pins[f'LED{val}:K']; end=endpoint(z,'GND',n+1)
        add(z,n+1,[(gx,150)],'gnd',(gx+10,610))
    sx,sy=z.pins['BUZZ:SIG']
    add(z,24,[(990,z.pins['ESP:L33'][1]),(990,240),(sx,240)],'yellow',(700,248))
    vx,vy=z.pins['BUZZ:VCC']; end=endpoint(z,'3V3',25)
    add(z,25,[(vx,210),(end[0],210)],'red',(vx+8,218))
    gx,gy=z.pins['BUZZ:GND']; end=endpoint(z,'GND',26)
    add(z,26,[(gx,150)],'gnd',(gx+8,195))
    return z

def overview():
    z=Scene('COMPLETE ASSEMBLY - ALL 30 HOOKUP WIRES','One visual layout of the ESP32, three sensors, three LEDs, three buttons, OLED, buzzer and shared power rails.',1)
    board(z,650,300,.9); rails(z)
    for name,y in [('S30',770),('S20',550),('S10',330)]:
        sensor(z,name,140,y,.9)
        def tag(c,n=name,b=y): text(c,n[1:]+'-POINT HOLE',375,b+120,13,'ink',True)
        z.parts.append((0,0,1,tag))
    oled(z,1170,755,1.0)
    for name,x,col in [('START',1050,'blue'),('RESET',1240,'red'),('MISS',1430,'yellow')]: button(z,name,x,490,.86,col)
    for val,x,col in [(10,420,'blue'),(20,520,'green'),(30,600,'red')]: led(z,'LED'+str(val),x,340,.86,col,True)
    buzzer(z,1220,220,.9,True); usb_supply(z,900,245,.9)
    add(z,1,[(610,z.pins['ESP:L3V'][1]),(610,180)],'red',(612,795))
    add(z,2,[(592,z.pins['ESP:LGND'][1]),(592,150)],'gnd',(600,424))
    for name,out,vcc,gnd,gpio,lane,col,px,gx in [
        ('S30',3,4,5,'L32',565,'blue',78,48),
        ('S20',9,10,11,'L35',540,'green',100,62),
        ('S10',6,7,8,'L34',510,'orange',120,86)]:
        sx,sy=z.pins[name+':OUT']; yy=sy-32
        add(z,out,[(sx,yy),(lane,yy),(lane,z.pins['ESP:'+gpio][1])],col,(365,yy+8))
        vx,vy=z.pins[name+':VCC']; end=endpoint(z,'3V3',vcc)
        add(z,vcc,[(vx,vy-20),(px,vy-20),(px,180)],'red',(95,vy-15))
        gx0,gy=z.pins[name+':GND']; end=endpoint(z,'GND',gnd)
        add(z,gnd,[(gx0,gy-30),(gx,gy-30),(gx,150)],'gnd',(155,gy-25))
    for n,gpio,val,lane,bypass,yy,col in [(12,'L25',10,560,380,430,'blue'),(14,'L26',20,580,480,420,'green'),(16,'L27',30,600,560,410,'orange')]:
        tx,ty=z.pins[f'LED{val}:R_IN']
        add(z,n,[(lane,z.pins['ESP:'+gpio][1]),(lane,yy),(bypass,yy),(bypass,ty)],col,(lane+7,490-(n-12)*10))
        gx,gy=z.pins[f'LED{val}:K']; end=endpoint(z,'GND',n+1)
        add(z,n+1,[(gx,150)],'gnd',(gx+5,275))
    for n,gpio,name,lane,yy,col in [(18,'R18','START',970,442,'blue'),(20,'R19','RESET',990,422,'gray'),(22,'R23','MISS',1010,402,'purple')]:
        tx,ty=z.pins[name+':T1']
        add(z,n,[(lane,z.pins['ESP:'+gpio][1]),(lane,yy),(tx,yy)],col,(lane+12,yy+7))
        gx,gy=z.pins[name+':T2']; end=endpoint(z,'GND',n+1)
        add(z,n+1,[(gx,378),(1500-(n-18)*3,378),(1500-(n-18)*3,150)],'gnd',(gx+4,385))
    sx,sy=z.pins['BUZZ:SIG']
    add(z,24,[(625,z.pins['ESP:L33'][1]),(625,224),(1180,224),(1180,sy)],'yellow',(1070,232))
    vx,vy=z.pins['BUZZ:VCC']; end=endpoint(z,'3V3',25)
    add(z,25,[(vx,207),(end[0],207)],'red',(1400,220))
    gx,gy=z.pins['BUZZ:GND']; end=endpoint(z,'GND',26)
    add(z,26,[(gx,150)],'gnd',(gx+8,170))
    for n,gpio,target,lane,yy,col in [(27,'R21','SDA',1000,686,'teal'),(28,'R22','SCL',1020,707,'cyan')]:
        tx,ty=z.pins['OLED:'+target]
        add(z,n,[(lane,z.pins['ESP:'+gpio][1]),(lane,yy),(tx,yy)],col,(1060,yy+8))
    vx,vy=z.pins['OLED:VCC']; add(z,29,[(vx,726),(1530,726),(1530,180)],'red',(1412,735))
    gx,gy=z.pins['OLED:GND']; add(z,30,[(gx,663),(1550,663),(1550,150)],'gnd',(1412,672))
    assert {w['id'] for w in z.wires}==set(EXPECTED)
    return z

READERS=[]
def illustrated_pages():
    WORK.mkdir(parents=True,exist_ok=True)
    scenes=[overview(),detail_sensors(),detail_controls(),detail_outputs()]
    path=WORK/'visual-wiring.pdf'
    c=canvas.Canvas(str(path),pagesize=PAGE)
    for scene in scenes: scene.finish(c)
    c.save()
    # Preserve a human-readable net map beside the illustrations.
    out=ROOT/'creation/wiring/source/connection-map.json'
    out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps({'board':'WEMOS LOLIN32 v1.0.0','orientation':'antenna up; USB down',
        'header_left_top_to_bottom':LEFT,'header_right_top_to_bottom':RIGHT,
        'manufacturer_reference':'https://img.sauf.ca/pictures/2017-11-04/8c3db4dd04f1c0bfdd1a99da9106e0c5.pdf',
        'connections':[{k:v for k,v in w.items() if k not in ['points','label']} for w in scenes[0].wires]},indent=2),encoding='utf-8')
    reader=PdfReader(path); READERS.append(reader)
    return list(reader.pages)

if __name__=='__main__': illustrated_pages()
