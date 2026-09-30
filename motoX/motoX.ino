#include <Arduboy2.h>
#include <avr/pgmspace.h>
#include <math.h>

// Global Arduboy Instance
Arduboy2 arduboy;

// -------------------------------------------------------------
// 1. DATA TYPES & STRUCTS
// -------------------------------------------------------------
enum class RiderState : uint8_t {
    Grounded,
    Airborne,
    Crashing,
    Looping
};

struct Bike {
    float x = 16.0f;
    float y = 61.0f;

    float vx = 0.0f;
    float vy = 0.0f;

    float angle = 0.0f;      // Stored as float so fractional mid-air rotation accumulates properly
    float angularVel = 0.0f; // Mid-air rotation speed
    float wheelieAngle = 0.0f; // Smooth grounded wheelie pitch offset

    uint8_t nitros = 3;
    uint8_t nitroTimer = 0; // Replaced nitroActive with a timer for sustained boosting
    uint8_t accelSmokeTimer = 0; // Timer for standing start acceleration smoke effect
    uint8_t crashTimer = 0;

    float loopTheta = 0.0f; // Progress around the current loop in radians (0 = bottom, PI = top)
    uint8_t loopIndex = 0;
    bool loopArmed = false; // True once the bike has jumped in through the loop's opening
    bool loopLocked = false; // Blocks re-entering a loop right after leaving one; cleared on landing

    RiderState state = RiderState::Grounded;
};

struct Item {
    float x;
    float y;
    bool active;
};

enum class LoopType : uint8_t {
    Standard,   // Symmetric opening at the bottom, square frame
    Tilted,     // Opening swept toward the lower-left, chamfered frame
    Side,       // C-shape opening to the right, entered from below on its right side
    QuarterPipe // Floating curve -> vertical wall -> lip; caught from a jump, launches up and slightly back
};

struct Loop {
    uint16_t x;      // Center X in world space
    int8_t y;        // Center Y in world space (negative = above the top of the default screen)
    uint8_t radius;
    LoopType type;
};

// -------------------------------------------------------------
// 2. PHYSICS & CONSTANTS
// -------------------------------------------------------------
constexpr float GRAVITY = 0.075f; // Halved from 0.15f for lighter airtime
constexpr float DRAG = 0.970f; // Adjusted from 0.975f to slightly reduce top speed
constexpr float BASE_ACCEL = 0.055f; // Reduced from 0.07f for slightly slower acceleration
constexpr float NITRO_ACCEL = 0.08f; // Sustained acceleration per frame while active
constexpr uint8_t NITRO_FRAMES = 15; // Duration of boost (15 frames = 1/4 second)
constexpr float AIR_PITCH_SPEED = 0.1067f; // Calibrated for ~1 full 360-degree rotation per 1.0s at 60 FPS
constexpr float WHEELIE_RISE_SPEED = 0.12f; // Smooth upward rotation speed per frame
constexpr float WHEELIE_FALL_SPEED = 0.20f; // Recovery speed when releasing wheelie
constexpr float MAX_WHEELIE_THRESHOLD = 3.6f; // Pitch threshold before tipping backward & crashing
constexpr int8_t BIKE_Y_OFFSET = 5; // Height offset above ground line for visual clarity and bottom-edge clipping safety
constexpr float LOOP_GAP_HALF = 0.7f; // Half-angle (radians) of the open bottom of Standard loops
constexpr float TILTED_GAP_LO = -1.4f; // Tilted loop opening: from just below the left side...
constexpr float TILTED_GAP_HI = 0.5f;  // ...to just right of the bottom
constexpr float SIDE_GAP_LO = 0.6f; // Side (C) loop opening: from lower-right...
constexpr float SIDE_GAP_HI = 2.2f; // ...to upper-right
constexpr int16_t QP_WALL_LEN = 24; // Straight vertical section above a quarter pipe's curve
constexpr int16_t QP_LIP_LEN = 6;   // Short lip at the top of the wall
constexpr float QP_LIP_ANGLE = 0.35f; // Lip tilt back toward the left (radians from vertical)
constexpr float LOOP_ENTRY_TOLERANCE = 0.8f; // Extra angle beside the opening that still counts as entering
constexpr int16_t LOOP_FRAME_PAD = 4; // Visual spacing between the loop circle and its outer box frame
constexpr int16_t LOOP_FRAME_CHAMFER = 10; // Size of the cut corners on Tilted and Side loop frames
constexpr float LOOP_GRAVITY_SCALE = 0.5f; // Softer gravity inside loops for arcade feel; raise to make loops harder

// Global Game Objects
Bike playerBike;

constexpr uint8_t MAX_ITEMS = 3;
Item nitroItems[MAX_ITEMS] = {
    { 80.0f, 0.0f, true },
    { 200.0f, 0.0f, true },
    { 320.0f, 0.0f, true }
};

// -------------------------------------------------------------
// 3. SPRITE DATA (PROGMEM) 
// -------------------------------------------------------------
// 'smoke', 8x8px (mono, SSD1306 vertical pages, LSB=top)
const unsigned char epd_bitmap_smoke[] PROGMEM = {
    0x00, 0x00, 0x54, 0x00, 0x28, 0x00, 0x00, 0x00
};

// motoXbike 16 x 256 (mono, SSD1306 vertical pages, LSB=top)
const int motoXbike_width  = 16;
const int motoXbike_height = 256;
const unsigned char motoXbike[] PROGMEM = {
  0x00, 0x00, 0x80, 0x80, 0x00, 0x80, 0xb0, 0xf8, 0xb8, 0x90, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00,
  0x00, 0x0c, 0x12, 0x12, 0x0d, 0x03, 0x07, 0x07, 0x01, 0x0d, 0x12, 0x13, 0x0c, 0x01, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0xf8, 0xb0, 0xc0, 0xc0, 0x40, 0xe0, 0x00, 0x60, 0x00, 0x00,
  0x00, 0x00, 0x12, 0x2a, 0x46, 0x2b, 0x17, 0x07, 0x07, 0x00, 0x07, 0x00, 0x04, 0x03, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x20, 0xf0, 0xf0, 0x90, 0x80, 0xc0, 0x30, 0xf0, 0x40, 0x60, 0xc0, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x0c, 0x72, 0x97, 0x17, 0x57, 0x06, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00,
  0x00, 0x00, 0x00, 0xe0, 0xe0, 0xd0, 0x80, 0xe0, 0xf8, 0x10, 0xb4, 0x90, 0x50, 0x40, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x27, 0x57, 0x4f, 0x53, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0xc0, 0xe0, 0xc0, 0x80, 0xfc, 0xea, 0x98, 0xa4, 0x24, 0x18, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x1b, 0x07, 0x1b, 0x25, 0x24, 0x18, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x80, 0x00, 0x3c, 0xd8, 0xfc, 0x84, 0xb0, 0x8c, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x01, 0x03, 0x07, 0x03, 0x01, 0x07, 0x1b, 0x07, 0x1a, 0x02, 0x10, 0x16, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x18, 0x20, 0x54, 0xd6, 0x90, 0xdc, 0xc0, 0x00, 0x00, 0x80, 0x80, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x07, 0x0e, 0x0e, 0x07, 0x01, 0x03, 0x03, 0x09, 0x07, 0x04, 0x04, 0x03, 0x00,
  0x00, 0x00, 0x60, 0x48, 0xa4, 0xe4, 0xd4, 0x80, 0xe0, 0xc0, 0x00, 0xe0, 0x20, 0x20, 0x80, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x0c, 0x1e, 0x0f, 0x0d, 0x01, 0x03, 0x01, 0x05, 0x01, 0x00, 0x00, 0x00,
  0x00, 0x80, 0x30, 0xc8, 0x48, 0xb0, 0x80, 0xe0, 0xe0, 0xc0, 0xb0, 0x48, 0x48, 0x30, 0x00, 0x00,
  0x00, 0x00, 0x01, 0x01, 0x01, 0x09, 0x1d, 0x1f, 0x0d, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00,
  0x00, 0xa0, 0x20, 0x00, 0x60, 0x00, 0xe0, 0xe0, 0xe8, 0xd2, 0x10, 0x5c, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x04, 0x04, 0x07, 0x04, 0x03, 0x03, 0x0d, 0x1f, 0x1e, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x80, 0x80, 0x80, 0x80, 0x40, 0xee, 0xfa, 0xe2, 0x6a, 0x30, 0x10, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x03, 0x04, 0x14, 0x16, 0x0e, 0x07, 0x03, 0x0d, 0x0e, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x06, 0xd2, 0xe2, 0xec, 0xc0, 0x18, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x04, 0x0e, 0x19, 0x0a, 0x2c, 0x1b, 0x07, 0x01, 0x03, 0x0f, 0x07, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x18, 0x24, 0xa4, 0xd8, 0xe0, 0xec, 0x80, 0xc0, 0xc0, 0x80, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x18, 0x24, 0x25, 0x19, 0x2b, 0x1f, 0x00, 0x01, 0x03, 0x01, 0x00, 0x00, 0x00,
  0x00, 0x70, 0x08, 0x50, 0x30, 0xe8, 0xd0, 0xc0, 0x80, 0xe0, 0xe0, 0xe0, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x11, 0x2b, 0x45, 0x2b, 0x13, 0x2c, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00,
  0x40, 0x40, 0x20, 0xd0, 0x70, 0xc0, 0xc0, 0x80, 0xe0, 0x70, 0x70, 0x60, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x03, 0x00, 0x01, 0x00, 0x03, 0x23, 0x69, 0x0b, 0x2b, 0x06, 0x14, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x80, 0x80, 0x00, 0x80, 0xb0, 0xf8, 0xb8, 0x90, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00,
  0x00, 0x0c, 0x12, 0x12, 0x0d, 0x03, 0x07, 0x07, 0x01, 0x0d, 0x12, 0x13, 0x0c, 0x01, 0x00, 0x00
};

// Static 2-frame animation (16x32px, 2 frames of 16x16px = 64 bytes total)
const unsigned char epd_bitmap_motoXbikeStatic [] PROGMEM = {
	0xa0, 0x00, 0x40, 0x40, 0x80, 0xc0, 0xf0, 0xf8, 0xf8, 0xd0, 0x40, 0xc0, 0x40, 0x80, 0x00, 0x00, 
	0x00, 0x0c, 0x12, 0x12, 0x0c, 0x01, 0x03, 0x03, 0x00, 0x0c, 0x12, 0x13, 0x0c, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x80, 0x80, 0x00, 0x80, 0xb0, 0xf8, 0xb8, 0x90, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 
	0x01, 0x0c, 0x12, 0x12, 0x0d, 0x03, 0x07, 0x07, 0x01, 0x0d, 0x12, 0x13, 0x0c, 0x01, 0x00, 0x00
};

// -------------------------------------------------------------
// 4. TRACK & TERRAIN DATA (PROGMEM)
// -------------------------------------------------------------
constexpr uint8_t TERRAIN_STEP_X = 8; // Pixels between heightmap samples

const uint8_t track1_heights[] PROGMEM = {
    61, 61, 61, 61, 61, 61, 61, 61, // Flat start (0 - 56px)
    61, 61, 61, 61, 61, 61, 61, 61, // Flat start (0 - 56px)
    61, 61, 61, 61, 61, 61, 61, 61, // Flat start (0 - 56px)
    53, 45, 37, 29, 37, 45, 53, 61, // Whoops / Bumps (248 - 304px)
    61, 61, 61, 61, 61, 61, 61, 61, // Flat start (0 - 56px)
    61, 61, 61, 61, 61, 61, 61, 61, // Flat start (0 - 56px)
    56, 51, 46, 41, 36, 31,         // Ramp up (64 - 104px)
    31, 31, 31,                     // Crest (112 - 128px)
    36, 41, 46, 51, 56, 61,         // Ramp down (136 - 176px)
    61, 61, 61, 61, 61, 61, 61, 61, // Flat middle (184 - 240px)
    53, 45, 37, 29, 37, 45, 53, 61, // Whoops / Bumps (248 - 304px)
    61, 61, 61, 61, 61, 61, 61, 61  // Flat end (312 - 368px)
    // repeat course for testing:
    ,61, 61, 61, 61, 61, 61, 61, 61, // Flat start (0 - 56px)
    56, 51, 46, 41, 36, 31,         // Ramp up (64 - 104px)
    31, 31, 31,                     // Crest (112 - 128px)
    36, 41, 46, 51, 56, 61,         // Ramp down (136 - 176px)
    61, 61, 61, 61, 61, 61, 61, 61, // Flat middle (184 - 240px)
    53, 45, 37, 29, 37, 45, 53, 61, // Whoops / Bumps (248 - 304px)
    61, 61, 61, 61, 61, 61, 61, 61  // Flat end (312 - 368px)
        // repeat course for testing:
    ,61, 61, 61, 61, 61, 61, 61, 61, // Flat start (0 - 56px)
    56, 51, 46, 41, 36, 31,         // Ramp up (64 - 104px)
    31, 31, 31,                     // Crest (112 - 128px)
    36, 41, 46, 51, 56, 61,         // Ramp down (136 - 176px)
    61, 61, 61, 61, 61, 61, 61, 61, // Flat middle (184 - 240px)
    53, 45, 37, 29, 37, 45, 53, 61, // Whoops / Bumps (248 - 304px)
    61, 61, 61, 61, 61, 61, 61, 61  // Flat end (312 - 368px)
    // Arch outer surface (starts at x=1440); must stay in sync with ARCH_INNER below
    ,60, 58, 55, 50, 43, 33, 23,    // Curved quarter-pipe face
    21, 21, 21, 21, 21, 21, 21, 21, // Flat top
    29, 37, 45, 53, 61,             // Straight back slope
    61, 61, 61, 61, 61, 61, 61, 61  // Flat run-out
    // Kicker ramp for the floating quarter pipe + C loop (ramp top at x=1776)
    ,61, 61, 61, 61, 61, 61, 61, 61, // Flat approach
    56, 51, 46, 41, 36, 31,         // Ramp up
    31, 31, 31,                     // Crest
    36, 41, 46, 51, 56, 61,         // Ramp down
    61, 61, 61, 61, 61, 61, 61, 61, // Flat landing area
    61, 61, 61, 61, 61, 61, 61, 61,
    61, 61, 61, 61, 61, 61, 61, 61,
    61, 61, 61, 61, 61, 61, 61, 61
};

const uint16_t TRACK1_LENGTH_SAMPLES = sizeof(track1_heights) / sizeof(track1_heights[0]);
const float TRACK1_MAX_X = (TRACK1_LENGTH_SAMPLES - 1) * TERRAIN_STEP_X;

// Floating loops: reached by a nitro + UP boosted jump off the ramp before them
const Loop track1_loops[] PROGMEM = {
    { 450, -16, 28, LoopType::Standard },
    { 826, -16, 28, LoopType::Tilted },
    // Quarter pipe catches a nitro + UP jump off the ramp at x=1776; its lip launches up-left into the C.
    // Keep the C center about (-23, -7) from the lip end (x + r - 2, y - QP_WALL_LEN - 6).
    { 1906, -15, 24, LoopType::QuarterPipe },
    { 1905, -52, 24, LoopType::Side },
};
constexpr uint8_t TRACK1_LOOP_COUNT = sizeof(track1_loops) / sizeof(track1_loops[0]);

// Arches: the rideable outer surface lives in track1_heights; only the hollow inner edge is drawn from here
struct ArchPoint {
    uint8_t x; // Offset from the arch's start X
    int8_t y;  // Offset from the ground height at the arch's start X
};

const ArchPoint ARCH_INNER[] PROGMEM = {
    { 36, 0 }, { 50, -16 }, { 56, -26 }, { 60, -34 }, // Inside of the curved face
    { 118, -34 },                                     // Underside of the top
    { 152, 0 }                                        // Inside of the back slope
};
constexpr uint8_t ARCH_INNER_COUNT = sizeof(ARCH_INNER) / sizeof(ARCH_INNER[0]);
constexpr int16_t ARCH_WIDTH = 160;

const uint16_t track1_arches[] PROGMEM = { 1440 };
constexpr uint8_t TRACK1_ARCH_COUNT = sizeof(track1_arches) / sizeof(track1_arches[0]);

// -------------------------------------------------------------
// 5. PROGMEM LOOKUP FUNCTIONS
// -------------------------------------------------------------
float getGroundHeight(float worldX) {
    if (worldX < 0) worldX = 0;
    
    uint16_t sampleIndex = (uint16_t)(worldX / TERRAIN_STEP_X);
    
    if (sampleIndex >= TRACK1_LENGTH_SAMPLES - 1) {
        return pgm_read_byte(&track1_heights[TRACK1_LENGTH_SAMPLES - 1]);
    }

    uint8_t y1 = pgm_read_byte(&track1_heights[sampleIndex]);
    uint8_t y2 = pgm_read_byte(&track1_heights[sampleIndex + 1]);

    float localProgress = (worldX - (sampleIndex * TERRAIN_STEP_X)) / (float)TERRAIN_STEP_X;

    return y1 + (y2 - y1) * localProgress;
}

float getGroundSlope(float worldX) {
    uint16_t sampleIndex = (uint16_t)(worldX / TERRAIN_STEP_X);
    
    if (sampleIndex >= TRACK1_LENGTH_SAMPLES - 1) return 0.0f;

    int8_t heightDiff = (int8_t)pgm_read_byte(&track1_heights[sampleIndex + 1]) - 
                        (int8_t)pgm_read_byte(&track1_heights[sampleIndex]);

    return (float)heightDiff / (float)TERRAIN_STEP_X;
}

LoopType getLoopType(uint8_t index) {
    return (LoopType)pgm_read_byte(&track1_loops[index].type);
}

// Opening of a loop as an angle range (0 = bottom, positive = toward the right side)
void getLoopGap(uint8_t index, float& lo, float& hi) {
    switch (getLoopType(index)) {
        case LoopType::Tilted:
            lo = TILTED_GAP_LO;
            hi = TILTED_GAP_HI;
            break;
        case LoopType::Side:
            lo = SIDE_GAP_LO;
            hi = SIDE_GAP_HI;
            break;
        default:
            lo = -LOOP_GAP_HALF;
            hi = LOOP_GAP_HALF;
            break;
    }
}

// Offset from a quarter pipe's center and travel direction after distance t along its surface
void quarterPipePose(float r, float t, float& dx, float& dy, float& dir) {
    float arcLen = r * (PI / 2);
    if (t <= arcLen) {
        dir = t / r;
        dx = r * sinf(dir);
        dy = r * cosf(dir);
    } else if (t <= arcLen + QP_WALL_LEN) {
        dir = PI / 2;
        dx = r;
        dy = -(t - arcLen);
    } else {
        float u = t - arcLen - QP_WALL_LEN;
        dir = PI / 2 + QP_LIP_ANGLE;
        dx = r - u * sinf(QP_LIP_ANGLE);
        dy = -QP_WALL_LEN - u * cosf(QP_LIP_ANGLE);
    }
}

// -------------------------------------------------------------
// 6. GAME LOGIC
// -------------------------------------------------------------
void updateItems(Bike& bike) {
    for (uint8_t i = 0; i < MAX_ITEMS; i++) {
        if (nitroItems[i].active) {
            float dx = bike.x - nitroItems[i].x;
            
            // Calculate relative vertical distance. The bike sprite visual center
            // is roughly 8 pixels above the terrain contact point (bike.y), minus the offset.
            float dy = (bike.y - BIKE_Y_OFFSET - 8) - nitroItems[i].y; 
            
            // Basic bounding box collision detection (~12px radius leeway)
            if (abs(dx) < 12.0f && abs(dy) < 12.0f) {
                bike.nitros += 3;
                nitroItems[i].active = false;
            }
        }
    }
}

void updateBike(Bike& bike, float groundHeightAtX, float groundSlopeAtX) {
    // Process sustained nitro boost globally so it works on the ground and in the air
    if (arduboy.justPressed(B_BUTTON) && bike.nitros > 0 && bike.nitroTimer == 0) {
        bike.nitros--;
        bike.nitroTimer = NITRO_FRAMES;
    }

    if (bike.nitroTimer > 0) {
        bike.nitroTimer--;
        bike.vx += NITRO_ACCEL;
        
        // Hard cap horizontal speed to prevent tunneling through terrain while boosting
        if (bike.vx > 4.5f) {
            bike.vx = 4.5f;
        }
    }

    if (bike.accelSmokeTimer > 0) {
        bike.accelSmokeTimer--;
    }

    switch (bike.state) {
        
        // -------------------------------------------------------------
        // 1. GROUNDED STATE
        // -------------------------------------------------------------
        case RiderState::Grounded: {
            bike.y = groundHeightAtX;
            bike.loopArmed = false;
            bike.loopLocked = false;

            // Progressive wheelie control with tipping/crash check
            if (arduboy.pressed(LEFT_BUTTON)) {
                bike.wheelieAngle += WHEELIE_RISE_SPEED;

                // Tipping point check: holding wheelie too long causes a backward flip crash
                if (bike.wheelieAngle >= MAX_WHEELIE_THRESHOLD) {
                    bike.state = RiderState::Crashing;
                    bike.crashTimer = 60;
                    bike.vx = 0.0f;
                    bike.vy = 0.0f;
                    bike.wheelieAngle = 0.0f;
                    bike.nitroTimer = 0; // Cancel nitro on crash
                    bike.accelSmokeTimer = 0; // Cancel smoke on crash
                    break;
                }
            } else {
                bike.wheelieAngle -= WHEELIE_FALL_SPEED;
                if (bike.wheelieAngle < 0.0f) {
                    bike.wheelieAngle = 0.0f;
                }
            }

            // Calculate current frame angle interpolating pitch smoothly relative to terrain slope
            // Multiplier changed to 2.0f so a 1.0 (45deg) slope maps cleanly to 2 steps on the 16-step dial
            float baseAngleFloat = (16.0f - (groundSlopeAtX * 2.0f)) + bike.wheelieAngle;
            while (baseAngleFloat < 0.0f) baseAngleFloat += 16.0f;
            while (baseAngleFloat >= 16.0f) baseAngleFloat -= 16.0f;
            bike.angle = baseAngleFloat;

            // Calculate true vertical momentum from the previous frame's horizontal movement
            float previousGroundY = getGroundHeight(bike.x - bike.vx);
            float actualVy = groundHeightAtX - previousGroundY;

            if (arduboy.pressed(A_BUTTON)) {
                // Trigger standing start smoke effect if accelerating from a stop
                if (bike.vx < 0.01f) {
                    bike.accelSmokeTimer = NITRO_FRAMES;
                }
                bike.vx += BASE_ACCEL;
            }

            bike.vx *= DRAG;
            bike.x += bike.vx;

            if (bike.x > TRACK1_MAX_X - 16) {
                bike.x = 0;
                
                // Respawn all items to make them reusable for the next lap
                for (uint8_t i = 0; i < MAX_ITEMS; i++) {
                    nitroItems[i].active = true;
                }
            }

            // Dynamic Takeoff Check: allow natural air off ramps only (no flat-ground jumping)
            float newGroundY = getGroundHeight(bike.x);
            float projectedY = groundHeightAtX + actualVy;

            bool naturalTakeoff = (projectedY < newGroundY - 0.2f);

            if (naturalTakeoff) {
                // If launching off a ramp (upward momentum actualVy < -0.1f) AND holding UP, boost the jump height
                if (arduboy.pressed(UP_BUTTON) && actualVy < -0.1f) {
                    // Boosted jump: upward ramp momentum + extra boost
                    bike.vy = actualVy - 0.8f;
                    if (bike.vy < -2.8f) bike.vy = -2.8f; // Cap max height to avoid leaving the screen
                } else {
                    // Natural takeoff (ramp or sudden drop without jump boost)
                    if (actualVy < 0.0f) {
                        bike.vy = actualVy * 0.85f; // Retain most of the upward speed
                    } else {
                        bike.vy = 0.0f; // Just falling off a ledge neutrally
                    }
                }
                bike.y = groundHeightAtX + bike.vy;
                bike.angularVel = 0.0f; // Reset angular velocity so takeoff pitch holds stable until player inputs pitch
                bike.wheelieAngle = 0.0f;
                bike.state = RiderState::Airborne;
            }
            break;
        }

        // -------------------------------------------------------------
        // 2. AIRBORNE STATE
        // -------------------------------------------------------------
        case RiderState::Airborne: {
            bike.wheelieAngle = 0.0f;

            if (arduboy.pressed(LEFT_BUTTON)) {
                bike.angularVel += AIR_PITCH_SPEED;
            } else if (arduboy.pressed(RIGHT_BUTTON)) {
                bike.angularVel -= AIR_PITCH_SPEED;
            }

            bike.angle += bike.angularVel;
            while (bike.angle >= 16.0f) bike.angle -= 16.0f;
            while (bike.angle < 0.0f)  bike.angle += 16.0f;

            bike.angularVel *= 0.60f;
            bike.vy += GRAVITY;
            bike.x += bike.vx;
            bike.y += bike.vy;

            // Loop capture: arm when entering near the opening, attach when hitting the inner wall
            for (uint8_t i = 0; i < TRACK1_LOOP_COUNT && !bike.loopLocked; i++) {
                float cx = pgm_read_word(&track1_loops[i].x);
                float cy = (int8_t)pgm_read_byte(&track1_loops[i].y);
                float r  = pgm_read_byte(&track1_loops[i].radius);
                float dx = bike.x - cx;
                float dy = bike.y - cy;
                float pdx = dx - bike.vx;
                float pdy = dy - bike.vy;
                bool wasInside = (pdx * pdx + pdy * pdy) < r * r;
                bool isInside  = (dx * dx + dy * dy) < r * r;
                if (wasInside == isInside) continue;

                float theta = atan2f(dx, dy); // 0 = bottom of the loop
                bool inGap;
                bool nearOpening;
                if (getLoopType(i) == LoopType::QuarterPipe) {
                    // Only the bottom-right quarter of the circle is solid; the wall above it is not catchable
                    inGap = theta < 0.0f || theta > PI / 2;
                    nearOpening = inGap;
                } else {
                    float gapLo, gapHi;
                    getLoopGap(i, gapLo, gapHi);
                    inGap = theta > gapLo && theta < gapHi;
                    // Widened test: a diagonal ramp jump usually clips the arc just beside the opening
                    nearOpening = theta > gapLo - LOOP_ENTRY_TOLERANCE && theta < gapHi + LOOP_ENTRY_TOLERANCE;
                }

                if (!wasInside) {
                    if (nearOpening) {
                        bike.loopArmed = true;
                        bike.loopIndex = i;
                    }
                } else if (bike.loopArmed && bike.loopIndex == i && !inGap) {
                    bike.loopArmed = false;
                    // Moving right, or moving forward along the wall (e.g. launched up-left off a quarter pipe lip)
                    if (bike.vx > 0.0f || bike.vx * cosf(theta) - bike.vy * sinf(theta) > 0.0f) {
                        if (theta < 0.0f) theta += 2.0f * PI;
                        bike.vx = sqrtf(bike.vx * bike.vx + bike.vy * bike.vy);
                        bike.vy = 0.0f;
                        bike.loopTheta = theta;
                        bike.x = cx + r * sinf(theta);
                        bike.y = cy + r * cosf(theta);
                        bike.angle = theta * (8.0f / PI);
                        bike.angularVel = 0.0f;
                        bike.state = RiderState::Looping;
                        return;
                    }
                }
            }

            // Handle track wrap-around in mid-air
            if (bike.x > TRACK1_MAX_X - 16) {
                bike.x = 0;
                // Respawn items for the next lap
                for (uint8_t i = 0; i < MAX_ITEMS; i++) {
                    nitroItems[i].active = true;
                }
            }

            if (bike.y >= groundHeightAtX) {
                bike.y = groundHeightAtX;

                // Match landing target detection to the new 2.0f terrain slope multiplier
                uint8_t landingAngle = (uint8_t)bike.angle % 16;
                uint8_t targetAngle = (uint8_t)(16 - (int8_t)(groundSlopeAtX * 2.0f)) % 16;
                int8_t angleDiff = abs((int8_t)landingAngle - (int8_t)targetAngle);

                if (angleDiff <= 2 || angleDiff >= 14) {
                    bike.vy = 0.0f;
                    bike.state = RiderState::Grounded;
                } else {
                    bike.state = RiderState::Crashing;
                    bike.crashTimer = 60;
                    bike.vx = 0.0f;
                    bike.vy = 0.0f;
                    bike.nitroTimer = 0; // Cancel nitro on crash
                    bike.accelSmokeTimer = 0; // Cancel smoke on crash
                }
            }
            break;
        }

        // -------------------------------------------------------------
        // 3. CRASHING STATE
        // -------------------------------------------------------------
        case RiderState::Crashing: {
            bike.wheelieAngle = 0.0f;
            if (bike.crashTimer > 0) {
                bike.crashTimer--;
            } else {
                // Scan backward to relocate player onto the nearest prior flat section
                float resetX = bike.x;
                while (resetX > 0.0f && getGroundSlope(resetX) != 0.0f) {
                    resetX -= TERRAIN_STEP_X;
                }
                if (resetX < 0.0f) resetX = 0.0f;

                bike.x = resetX;
                bike.y = getGroundHeight(resetX);
                bike.angle = 0.0f; // Flat forward alignment
                bike.vx = 0.0f;
                bike.vy = 0.0f;
                bike.state = RiderState::Grounded;
            }
            break;
        }

        // -------------------------------------------------------------
        // 4. LOOPING STATE (bike.vx is reused as speed along the loop)
        // -------------------------------------------------------------
        case RiderState::Looping: {
            float cx = pgm_read_word(&track1_loops[bike.loopIndex].x);
            float cy = (int8_t)pgm_read_byte(&track1_loops[bike.loopIndex].y);
            float r  = pgm_read_byte(&track1_loops[bike.loopIndex].radius);
            bool quarterPipe = getLoopType(bike.loopIndex) == LoopType::QuarterPipe;
            const float g = GRAVITY * LOOP_GRAVITY_SCALE;

            // Quarter pipes: loopTheta * r is distance along curve + wall + lip. Loops: loopTheta is the angle.
            auto pose = [&](float theta, float& dx, float& dy, float& dir) {
                if (quarterPipe) {
                    quarterPipePose(r, theta * r, dx, dy, dir);
                } else {
                    dir = theta;
                    dx = r * sinf(theta);
                    dy = r * cosf(theta);
                }
            };

            float exitTheta;
            if (quarterPipe) {
                exitTheta = PI / 2 + (float)(QP_WALL_LEN + QP_LIP_LEN) / r;
            } else {
                float gapLo, gapHi;
                getLoopGap(bike.loopIndex, gapLo, gapHi);
                exitTheta = 2.0f * PI + gapLo;
            }

            float dx, dy, dir;
            pose(bike.loopTheta, dx, dy, dir);
            float s = sinf(dir);
            float c = cosf(dir);

            if (arduboy.pressed(A_BUTTON)) bike.vx += BASE_ACCEL;
            // Gravity slows the climb and speeds up the descent; no DRAG here or loops become impossible
            bike.vx -= g * s;

            // Track can no longer hold the bike (too slow), or it has reached the opening: fly off.
            // Quarter pipes never drop the bike; it rolls back down instead.
            bool tooSlow = !quarterPipe && (bike.vx <= 0.0f || (bike.vx * bike.vx) / r + g * c < 0.0f);
            if (tooSlow || bike.loopTheta >= exitTheta) {
                if (!tooSlow) {
                    // Snap to the exit point so the launch direction is exact
                    pose(exitTheta, dx, dy, dir);
                    s = sinf(dir);
                    c = cosf(dir);
                    bike.x = cx + dx;
                    bike.y = cy + dy;
                }
                float speed = bike.vx;
                bike.vx = speed * c;
                bike.vy = -speed * s;
                bike.angularVel = 0.0f;
                bike.loopArmed = false;
                bike.loopLocked = !quarterPipe; // A quarter pipe launch must still be able to catch the loop above
                bike.state = RiderState::Airborne;
                break;
            }

            bike.loopTheta += bike.vx / r;

            if (bike.loopTheta <= 0.0f) {
                // Rolled back off the bottom end of a quarter pipe: drop off moving left
                bike.x = cx;
                bike.y = cy + r;
                bike.vy = 0.0f;
                bike.angle = 0.0f;
                bike.loopLocked = true;
                bike.state = RiderState::Airborne;
                break;
            }

            pose(bike.loopTheta, dx, dy, dir);
            bike.x = cx + dx;
            bike.y = cy + dy;
            bike.angle = dir * (8.0f / PI); // Travel direction on the 16-step dial
            break;
        }
    }
}

// -------------------------------------------------------------
// 7. RENDERING HELPER & TINY 3x5 HUD FONT
// -------------------------------------------------------------
void drawTinyChar(int16_t x, int16_t y, char c, uint8_t color = WHITE) {
    uint8_t cols[3] = {0, 0, 0};
    if (c >= '0' && c <= '9') {
        static const uint8_t PROGMEM DIGITS[10][3] = {
            {0x1F, 0x11, 0x1F}, // 0
            {0x00, 0x1F, 0x00}, // 1
            {0x1D, 0x15, 0x17}, // 2
            {0x15, 0x15, 0x1F}, // 3
            {0x07, 0x04, 0x1F}, // 4
            {0x17, 0x15, 0x1D}, // 5
            {0x1F, 0x15, 0x1D}, // 6
            {0x01, 0x01, 0x1F}, // 7
            {0x1F, 0x15, 0x1F}, // 8
            {0x17, 0x15, 0x1F}  // 9
        };
        for (uint8_t i = 0; i < 3; i++) cols[i] = pgm_read_byte(&DIGITS[c - '0'][i]);
    } else {
        switch (c) {
            case '.': cols[1] = 0x10; break;
            case ':': cols[1] = 0x0A; break;
            case 'S': cols[0] = 0x17; cols[1] = 0x15; cols[2] = 0x1D; break;
            case 'P': cols[0] = 0x1F; cols[1] = 0x05; cols[2] = 0x03; break;
            case 'D': cols[0] = 0x1F; cols[1] = 0x11; cols[2] = 0x0E; break;
            case 'N': cols[0] = 0x1F; cols[1] = 0x02; cols[2] = 0x1F; break;
            case 'I': cols[0] = 0x01; cols[1] = 0x1F; cols[2] = 0x01; break;
            case 'T': cols[0] = 0x01; cols[1] = 0x1F; cols[2] = 0x01; break;
            case 'R': cols[0] = 0x1F; cols[1] = 0x05; cols[2] = 0x1A; break;
            case 'O': cols[0] = 0x0E; cols[1] = 0x11; cols[2] = 0x0E; break;
            default: return;
        }
    }

    for (uint8_t col = 0; col < 3; col++) {
        uint8_t b = cols[col];
        for (uint8_t row = 0; row < 5; row++) {
            if (b & (1 << row)) {
                arduboy.drawPixel(x + col, y + row, color);
            }
        }
    }
}

void drawTinyString(int16_t x, int16_t y, const char* str) {
    while (*str) {
        drawTinyChar(x, y, *str);
        x += (*str == '.' || *str == ':') ? 3 : 4;
        str++;
    }
}

void drawItems(float cameraX, float cameraY) {
    for (uint8_t i = 0; i < MAX_ITEMS; i++) {
        if (nitroItems[i].active) {
            int16_t screenX = (int16_t)(nitroItems[i].x - cameraX);
            int16_t screenY = (int16_t)(nitroItems[i].y - cameraY);
            
            // Only draw if visible on screen
            if (screenX > -10 && screenX < 138) {
                // Fill 9x9 solid white square centered on the item's position
                arduboy.fillRect(screenX - 4, screenY - 4, 9, 9, WHITE);
                
                // Draw black 'N' inside (3x5 character, mathematically centered inside 9x9)
                drawTinyChar(screenX - 1, screenY - 2, 'N', BLACK);
            }
        }
    }
}

void drawTerrain(float cameraX, float cameraY) {
    for (int16_t screenX = 0; screenX < 128; screenX += 2) {
        float worldX = cameraX + screenX;
        
        // Offset terrain rendering by the vertical camera position
        int16_t y1 = (int16_t)(getGroundHeight(worldX) - cameraY);
        int16_t y2 = (int16_t)(getGroundHeight(worldX + 2) - cameraY);
        
        arduboy.drawLine(screenX, y1, screenX + 2, y2, WHITE);
    }
}

// Midpoint circle that skips pixels inside the opening between angles lo and hi (span must be < PI)
void drawOpenCircle(int16_t x0, int16_t y0, int16_t r, float lo, float hi) {
    // Fixed-point (x64) edge directions so the per-pixel test is integer-only
    int16_t cLo = (int16_t)(cosf(lo) * 64.0f);
    int16_t sLo = (int16_t)(sinf(lo) * 64.0f);
    int16_t cHi = (int16_t)(cosf(hi) * 64.0f);
    int16_t sHi = (int16_t)(sinf(hi) * 64.0f);

    auto plot = [&](int16_t dx, int16_t dy) {
        bool inGap = (cLo * dx - sLo * dy) > 0 && (sHi * dy - cHi * dx) > 0;
        if (!inGap) arduboy.drawPixel(x0 + dx, y0 + dy, WHITE);
    };

    int16_t f = 1 - r;
    int16_t ddx = 1;
    int16_t ddy = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    plot(0, r);
    plot(0, -r);
    plot(r, 0);
    plot(-r, 0);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddy += 2;
            f += ddy;
        }
        x++;
        ddx += 2;
        f += ddx;

        plot(x, y);   plot(-x, y);
        plot(x, -y);  plot(-x, -y);
        plot(y, x);   plot(-y, x);
        plot(y, -x);  plot(-y, -x);
    }
}

// Bottom-right quarter of a midpoint circle (from the bottom point to the right side)
void drawQuarterArc(int16_t x0, int16_t y0, int16_t r) {
    int16_t f = 1 - r;
    int16_t ddx = 1;
    int16_t ddy = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    arduboy.drawPixel(x0, y0 + r, WHITE);
    arduboy.drawPixel(x0 + r, y0, WHITE);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddy += 2;
            f += ddy;
        }
        x++;
        ddx += 2;
        f += ddx;

        arduboy.drawPixel(x0 + x, y0 + y, WHITE);
        arduboy.drawPixel(x0 + y, y0 + x, WHITE);
    }
}

void drawLoops(float cameraX, float cameraY) {
    for (uint8_t i = 0; i < TRACK1_LOOP_COUNT; i++) {
        int16_t r = pgm_read_byte(&track1_loops[i].radius);
        float cx = pgm_read_word(&track1_loops[i].x);
        int16_t outer = r + LOOP_FRAME_PAD;
        int16_t sx = (int16_t)(cx - cameraX);
        if (sx + outer < 0 || sx - outer > 127) continue;
        int16_t sy = (int16_t)((int8_t)pgm_read_byte(&track1_loops[i].y) - cameraY);
        LoopType type = getLoopType(i);

        if (type == LoopType::QuarterPipe) {
            int16_t lipDx = (int16_t)(QP_LIP_LEN * sinf(QP_LIP_ANGLE) + 0.5f);
            int16_t lipDy = (int16_t)(QP_LIP_LEN * cosf(QP_LIP_ANGLE) + 0.5f);
            int16_t wallTop = sy - QP_WALL_LEN;
            // Riding surface (curve, wall, lip) and a parallel outer edge, joined by end caps
            drawQuarterArc(sx, sy, r);
            arduboy.drawFastVLine(sx + r, wallTop, QP_WALL_LEN + 1, WHITE);
            arduboy.drawLine(sx + r, wallTop, sx + r - lipDx, wallTop - lipDy, WHITE);
            drawQuarterArc(sx, sy, outer);
            arduboy.drawFastVLine(sx + outer, wallTop, QP_WALL_LEN + 1, WHITE);
            arduboy.drawLine(sx + outer, wallTop, sx + outer - lipDx, wallTop - lipDy, WHITE);
            arduboy.drawFastVLine(sx, sy + r, LOOP_FRAME_PAD + 1, WHITE);
            arduboy.drawFastHLine(sx + r - lipDx, wallTop - lipDy, LOOP_FRAME_PAD + 1, WHITE);
            continue;
        }

        float gapLo, gapHi;
        getLoopGap(i, gapLo, gapHi);
        int16_t loDx = (int16_t)(r * sinf(gapLo));
        int16_t loDy = (int16_t)(r * cosf(gapLo));
        int16_t hiDx = (int16_t)(r * sinf(gapHi));
        int16_t hiDy = (int16_t)(r * cosf(gapHi));
        int16_t cut = (type == LoopType::Standard) ? 0 : LOOP_FRAME_CHAMFER;
        int16_t left = sx - outer;
        int16_t top = sy - outer;

        drawOpenCircle(sx, sy, r, gapLo, gapHi);

        if (type == LoopType::Side) {
            // C frame: chamfered left corners, long top arm, short bottom arm ending at the circle
            int16_t bottom = sy + outer;
            arduboy.drawFastHLine(left + cut, top, 2 * outer - cut + 1, WHITE);
            arduboy.drawFastVLine(sx + outer, top, outer + hiDy + 1, WHITE);
            arduboy.drawFastHLine(sx + hiDx, sy + hiDy, outer - hiDx + 1, WHITE);
            arduboy.drawLine(left, top + cut, left + cut, top, WHITE);
            arduboy.drawFastVLine(left, top + cut, 2 * (outer - cut) + 1, WHITE);
            arduboy.drawLine(left, bottom - cut, left + cut, bottom, WHITE);
            arduboy.drawFastHLine(left + cut, bottom, outer - cut + loDx + 1, WHITE);
            arduboy.drawFastVLine(sx + loDx, sy + loDy, outer - loDy + 1, WHITE);
            continue;
        }

        // Frame: top bar (chamfered corners on Tilted), side pillars, and feet meeting the circle's open ends
        arduboy.drawFastHLine(left + cut, top, 2 * (outer - cut) + 1, WHITE);
        if (cut > 0) {
            arduboy.drawLine(left, top + cut, left + cut, top, WHITE);
            arduboy.drawLine(sx + outer - cut, top, sx + outer, top + cut, WHITE);
        }
        arduboy.drawFastVLine(left, top + cut, outer - cut + loDy + 1, WHITE);
        arduboy.drawFastVLine(sx + outer, top + cut, outer - cut + hiDy + 1, WHITE);
        arduboy.drawFastHLine(left, sy + loDy, outer + loDx + 1, WHITE);
        arduboy.drawFastHLine(sx + hiDx, sy + hiDy, outer - hiDx + 1, WHITE);
    }
}

void drawArches(float cameraX, float cameraY) {
    for (uint8_t i = 0; i < TRACK1_ARCH_COUNT; i++) {
        float ax = pgm_read_word(&track1_arches[i]);
        int16_t sx = (int16_t)(ax - cameraX);
        if (sx + ARCH_WIDTH < 0 || sx > 127) continue;
        int16_t sy = (int16_t)(getGroundHeight(ax) - cameraY);

        for (uint8_t p = 0; p + 1 < ARCH_INNER_COUNT; p++) {
            arduboy.drawLine(
                sx + pgm_read_byte(&ARCH_INNER[p].x),     sy + (int8_t)pgm_read_byte(&ARCH_INNER[p].y),
                sx + pgm_read_byte(&ARCH_INNER[p + 1].x), sy + (int8_t)pgm_read_byte(&ARCH_INNER[p + 1].y),
                WHITE);
        }
    }
}

// -------------------------------------------------------------
// 8. ARDUINO SETUP & LOOP
// -------------------------------------------------------------
void setup() {
    arduboy.begin();
    arduboy.setFrameRate(60);
    
    // Set vertical position for items to hover slightly above the terrain
    for (uint8_t i = 0; i < MAX_ITEMS; i++) {
        nitroItems[i].y = getGroundHeight(nitroItems[i].x) - 14.0f;
    }
}

void loop() {
    if (!arduboy.nextFrame()) return;
    arduboy.pollButtons();
    arduboy.clear();

    float currentGround = getGroundHeight(playerBike.x);
    float currentSlope  = getGroundSlope(playerBike.x);

    updateBike(playerBike, currentGround, currentSlope);
    updateItems(playerBike);

    float cameraX = playerBike.x - 32.0f;
    if (cameraX < 0) cameraX = 0;

    // Calculate vertical camera position. 
    // This keeps the bike at least 24 pixels from the top of the screen when launching high into the air.
    float cameraY = playerBike.y - 24.0f;
    if (cameraY > 0.0f) {
        cameraY = 0.0f; // Clamps the camera so it doesn't push the ground beneath the standard level
    }

    drawTerrain(cameraX, cameraY);
    drawLoops(cameraX, cameraY);
    drawArches(cameraX, cameraY);
    drawItems(cameraX, cameraY);

    int16_t bikeScreenX = (int16_t)(playerBike.x - cameraX);
    int16_t bikeScreenY = (int16_t)(playerBike.y - cameraY); // Subtract cameraY from the bike's screen Y
    
    // Apply BIKE_Y_OFFSET to lift the sprite visually above the terrain line
    // (inside a loop, the lift points toward the loop center instead of straight up)
    float liftX = 0.0f;
    float liftY = BIKE_Y_OFFSET;
    if (playerBike.state == RiderState::Looping) {
        liftX = sinf(playerBike.angle * (PI / 8.0f)) * BIKE_Y_OFFSET;
        liftY = cosf(playerBike.angle * (PI / 8.0f)) * BIKE_Y_OFFSET;
    }

    // Offset the 16x16 sprite by -8 so it renders perfectly centered on the coordinate
    int16_t renderX = bikeScreenX - 8 - (int16_t)liftX;
    int16_t renderY = bikeScreenY - 8 - (int16_t)liftY;

    // Determine whether to play the 2-frame static idle animation or active rotation frames
    const unsigned char* activeBitmap;
    uint8_t activeFrameIndex;

    // Only play the static animation if grounded, stopped, AND on a perfectly flat surface
    bool isStatic = (playerBike.state == RiderState::Grounded && playerBike.vx < 0.01f && getGroundSlope(playerBike.x) == 0.0f);

    if (isStatic) {
        activeBitmap = epd_bitmap_motoXbikeStatic;
        // 60 FPS / 7 = ~8.5 updates/sec (loops the 2-frame idle sequence roughly 4 times per second)
        activeFrameIndex = (arduboy.frameCount / 7) % 2;
    } else {
        activeBitmap = motoXbike;
        activeFrameIndex = (uint8_t)playerBike.angle % 16;
    }
    
    // Draw smoke effect behind exhaust pipe while nitro boost or standing acceleration is active
    if (playerBike.nitroTimer > 0 || playerBike.accelSmokeTimer > 0) {
        // Convert current 16-step bike angle into radians
        float rad = playerBike.angle * (3.14159265f / 8.0f);
        
        // Calculate offset 10 pixels directly behind the visual center of the motorbike
        const float dist = 10.0f;
        int16_t smokeX = (int16_t)(bikeScreenX - liftX - cosf(rad) * dist - 4.0f);
        int16_t smokeY = (int16_t)(bikeScreenY - liftY + sinf(rad) * dist - 4.0f);

        arduboy.drawBitmap(smokeX, smokeY, epd_bitmap_smoke, 8, 8, WHITE);
    }

    // A 16x16 pixel frame uses 32 bytes (16 columns * 2 vertical 8-bit pages)
    arduboy.drawBitmap(renderX, renderY, activeBitmap + (activeFrameIndex * 32), 16, 16, WHITE);

    // Ultra-compact 3x5 HUD rendering (UI stays absolute, unaffected by camera)
    char hudBuffer[16];
    snprintf(hudBuffer, sizeof(hudBuffer), "SPD:%d.%d", (int)playerBike.vx, (int)(playerBike.vx * 10) % 10);
    drawTinyString(0, 0, hudBuffer);

    snprintf(hudBuffer, sizeof(hudBuffer), "NITROS:%d", playerBike.nitros);
    drawTinyString(78, 0, hudBuffer);

    if (playerBike.state == RiderState::Crashing) {
        arduboy.setCursor(44, 20);
        arduboy.print(F("CRASH!"));
    }

    arduboy.display();
}