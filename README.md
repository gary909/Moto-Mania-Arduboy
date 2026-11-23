"# Moto-Mania-Arduboy"

<img width="250" alt="pixelated-image (1)" src="https://github.com/user-attachments/assets/38b99415-62dc-4601-b526-e9dd287bdb56" />


TO DO

Courses.  Terrain is built from named sections in motoX.ino (FLAT8/12/16/24, ROLLERS8, WHOOPS8, JUMP_SMALL/MID/BIG/HIGH, TABLE_LONG, TERRACE31, ARCH_L/TOP/R).  Compose courses from these and keep each section's sample count intact so loops, arches and pickups don't shift.  Courses 2-4 are rebuilt; 5-7 still to do.

Add time pick ups

Add Speed pick ups

Add time count down

Add Rocks

Add Start/End Ramp


COURSES (2 laps per course, lap = one full pass of the heightmap)

| # | Name | Length | Loops | Arches | Signature features |
|---|------|--------|-------|--------|--------------------|
| 1 | Temp Test Course | 2104px | 3 + QP + Side | 1 | reference layout, left untouched |
| 2 | Rollers & First Air | 2088px | 2 | 2 | rollers, 15/20/30px jumps, no mud - the easy one |
| 3 | Rhythm & Tables | 2376px | 2 | 2 | roller pairs, 72px tabletop, whoops |
| 4 | Mud & Terraces | 2560px | 2 | 1 | two mud bogs, 30px tabletop, 30px cliff-drop terrace |
| 5 | Loops & Drops | 1872px | 4 | 2 | original layout (rebuild pending) |
| 6 | Terraces & Quarter-Pipe | 2016px | 3 + QP + Side | 2 | original layout (rebuild pending) |
| 7 | Grand Course | 2920px | 6 + QP + Side | 3 | original layout (rebuild pending) |


_________________________________

- Update 01.11.26: created  Title screen, level select screen, 7 blank courses

- added Add Dirt slow down Area

v0.36: rebuilt courses 2-4 as longer, section-built layouts (2088 / 2376 / 2560px) with roller runs, tabletops, a cliff-drop terrace and a second loop per course; nitro pickups now spread along each lap

v0.35: added half-height jump section (15px kicker, same 32-degree face) and used it in all 7 courses

v0.34: added 360-spin follower bikes

<img width="300" alt="ezgif-optimized" src="https://github.com/user-attachments/assets/be86a7c3-93ab-41d4-af82-3dbac1c91990" />


v0.33: added mud patch

v0.32: Moved info bar to bottom of screen

<img width="300" alt="IMG_20261024_165355888 (1)" src="https://github.com/user-attachments/assets/6413f8a3-4a73-49ea-8d5c-ce532b2e15ff" />


v0.31: added non functioning countdown timer

v0.30: added speed bar

v0.29: added nitro icons

v0.28: added verticle wall and vert loop - untested

v0.27: added half loop up jump

v0.26: added alt loop de loop

v0.25: added loop"


<img width="320" height="209" alt="VID_20260930_210156600-ezgif com-optimize" src="https://github.com/user-attachments/assets/54852e7b-f8c1-4f8a-80ff-92284bcb5d00" />


v0.24: added smoke puff to start takeoff"


<img width="320" height="186" alt="VID_20260929_110749288-ezgif com-optimize" src="https://github.com/user-attachments/assets/d0b93d47-88af-403c-99c8-17f835585a9b" />


v0.23: added smoke puff to nitro boost"

v0.22: added nitro pickups

<img width="300" alt="IMG_20260925_095534548" src="https://github.com/user-attachments/assets/a410f28d-196f-4c03-a8af-b952bc38ad29" />


v0.21: added nitro boost

v0.20: added static bike animation

<img width="16" height="32" alt="motoXbikeStatic" src="https://github.com/user-attachments/assets/79f59ab8-fd03-4f86-8219-0e6b8f255e66" />


<img width="300" alt="VID_20260922_133057994-ezgif com-optimize" src="https://github.com/user-attachments/assets/ecf194da-9169-4602-b441-5d9cf1e96d6f" />


v0.19: reduced jump height

v0.18: rotate/top speed/gravity improved - feels good now

v0.16: bike physics

v0.15: Added Bike img

<img width="300" alt="IMG_20260921_222416369" src="https://github.com/user-attachments/assets/e65b255c-48ca-490a-9088-52ae23ef1b9e" />

<img width="300" alt="IMG_20260921_221955115" src="https://github.com/user-attachments/assets/5b2e1fc6-49a1-46d7-ab8a-93057b3b1991" />

<img width="16" height="256" alt="motoXbike" src="https://github.com/user-attachments/assets/bba97bb7-f8f4-44ce-9558-6479056f721b" />

v0.14: Gravity improvement

v0.13: Cam follows bike in air

v0.12: Added up to jump button

v0.11: improved rotation & bike speed

v0.10: improved controls while taking off

v0.09: bike restarts on flat at correct angle

v0.08: lowered test course start height to 61px

v0.07: reduced font siz

<img width="300" height="400" alt="IMG_20260916_095001934 (1)" src="https://github.com/user-attachments/assets/a3201f0e-107e-437c-87e1-61b583f200bb" />


v0.06: Added wheelie mechanic

v0.05: Arrow pointing correct direction

v0.04: improved airborn rotation spee

V0.03: Bike goes airborne and stays in takeoff direction until user input


<img width="300" alt="image" src="https://github.com/user-attachments/assets/7108e405-9618-4d15-9301-3d238ac3e3e4" />


V0.02: Bike goes airborne. Changed icon to arrow.

V0.01: Bike responds to track elements

<img width="250" alt="pixelated-image" src="https://github.com/user-attachments/assets/3afd1fa2-edb4-4b21-bdaf-348a7422d54c" />



<img width="300" alt="IMG_20260914_093152150" src="https://github.com/user-attachments/assets/7a030761-4a23-4602-b276-1eba0d8cec5d" />
