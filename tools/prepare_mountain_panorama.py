#!/usr/bin/env python3
"""Prepare the photographic background; development only (ImageMagick).

Pass the original CC0 photograph as the first argument. Source and license are
in assets/environment/CREDITS.md. No image processing happens in the game.
"""
from pathlib import Path
import hashlib
import struct
import sys
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
# Traced skyline in a 1600 x 611 reference. Stay just inside the mountain
# boundary to avoid carrying the photographed sky into the dynamic sky.
SKYLINE = [
    (0,402),(38,420),(80,443),(97,435),(117,409),(145,393),
    (159,389),(187,374),(208,375),(224,372),(240,383),(258,382),
    (282,369),(304,364),(324,372),(343,358),(358,356),(380,371),
    (402,369),(427,384),(452,372),(474,353),(496,322),(518,307),
    (543,285),(560,265),(578,270),(594,264),(616,269),(639,258),
    (661,258),(682,252),(707,253),(733,249),(756,245),(778,249),
    (802,243),(827,239),(850,234),(873,242),(894,251),(918,254),
    (946,254),(970,245),(990,244),(1012,251),(1035,240),
    (1053,229),(1076,214),(1091,201),(1104,195),(1116,200),
    (1135,214),(1151,218),(1169,227),(1183,232),(1204,247),
    (1221,251),(1238,254),(1254,266),(1274,268),(1295,276),
    (1322,276),(1348,288),(1370,295),(1394,298),(1415,298),
    (1435,303),(1450,299),(1473,308),(1494,298),(1519,305),
    (1527,287),(1540,302),(1554,297),(1571,282),(1583,286),
    (1599,294),
]

source = Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix='perfect-v-panorama-') as tmp:
    mask = Path(tmp) / 'mask.png'
    cutout = Path(tmp) / 'cutout.png'
    outline = SKYLINE + [(1600,611),(0,611)]
    polygon = 'polygon ' + ' '.join(f'{x},{y+1.5}' for x,y in outline)
    subprocess.run(['magick','-size','1600x611','xc:black','-fill','white',
                    '-draw',polygon,'-resize','2048x768!','-blur','0x.65',str(mask)],check=True)
    # Neutralize small warm foreground patches without painting fake snow.
    subprocess.run(['magick',str(source),'-resize','2048x768!','-modulate','100,45,100',
                    '-channel','R','-evaluate','multiply','.95','+channel',
                    '-channel','G','-evaluate','multiply','.98','+channel',
                    '-channel','B','-evaluate','multiply','1.02','+channel',
                    str(mask),'-alpha','off','-compose','CopyOpacity','-composite',
                    str(cutout)],check=True)
    # Reflect at the photographed edges to close the ring without a hard
    # rectangular seam. This is a decorative wrap, not a geographic 360 capture.
    data = subprocess.check_output(['magick',str(cutout),'(','+clone','-flop',')',
                                    '+append','-flip','-depth','8','BGRA:-'])
width,height = 4096,768
assert len(data)==width*height*4
# BMP V4 specifies alpha explicitly; plain BI_RGB BMP can discard it in SDL.
header = struct.pack('<IiiHHIIiiII',108,width,height,1,32,3,len(data),2835,2835,0,0)
header += struct.pack('<IIIII',0x00ff0000,0x0000ff00,0x000000ff,0xff000000,0x73524742)
header += bytes(48)
assert len(header)==108
destination = ROOT / 'assets/environment/mountains.bmp'
destination.parent.mkdir(parents=True,exist_ok=True)
destination.write_bytes(struct.pack('<2sIHHI',b'BM',122+len(data),0,0,122)+header+data)
print(destination, 'source SHA256:',hashlib.sha256(source.read_bytes()).hexdigest())
