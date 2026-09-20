#include <Arduboy2.h>
#include <avr/pgmspace.h>

// Global Arduboy Instance
Arduboy2 arduboy;

// -------------------------------------------------------------
// 1. DATA TYPES & STRUCTS (Must be defined first!)
// -------------------------------------------------------------
enum class RiderState : uint8_t {
    Grounded,
    Airborne,
    Crashing
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
    bool nitroActive = false;
    uint8_t crashTimer = 0;

    RiderState state = RiderState::Grounded;
};

// -------------------------------------------------------------
// 2. PHYSICS & CONSTANTS
// -------------------------------------------------------------
constexpr float GRAVITY = 0.25f;
constexpr float DRAG = 0.985f;
constexpr float BASE_ACCEL = 0.09f; // Reduced from 0.15f for a more controllable top speed
constexpr float NITRO_BOOST = 0.40f; // Scaled down from 0.60f
constexpr float AIR_PITCH_SPEED = 0.0533f; // Calibrated for ~1 full 360-degree rotation per 2.0s at 60 FPS
constexpr float WHEELIE_RISE_SPEED = 0.12f; // Smooth upward rotation speed per frame
constexpr float WHEELIE_FALL_SPEED = 0.20f; // Recovery speed when releasing wheelie
constexpr float MAX_WHEELIE_THRESHOLD = 3.6f; // Pitch threshold before tipping backward & crashing
constexpr int8_t BIKE_Y_OFFSET = 5; // Height offset above ground line for visual clarity and bottom-edge clipping safety

// 16-step directional offset vectors for wireframe bike rendering (6-pixel radius)
const int8_t PROGMEM CHASSIS_DX[16] = { 6,  5,  4,  2,  0, -2, -4, -5, -6, -5, -4, -2,  0,  2,  4,  5 };
const int8_t PROGMEM CHASSIS_DY[16] = { 0,  2,  4,  5,  6,  5,  4,  2,  0, -2, -4, -5, -6, -5, -4, -2 };

// Global Game Objects
Bike playerBike;

// -------------------------------------------------------------
// 3. TRACK & TERRAIN DATA (PROGMEM)
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
};

const uint16_t TRACK1_LENGTH_SAMPLES = sizeof(track1_heights) / sizeof(track1_heights[0]);
const float TRACK1_MAX_X = (TRACK1_LENGTH_SAMPLES - 1) * TERRAIN_STEP_X;

// -------------------------------------------------------------
// 4. PROGMEM LOOKUP FUNCTIONS
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

// -------------------------------------------------------------
// 5. GAME LOGIC (Now safely below 'struct Bike')
// -------------------------------------------------------------
void updateBike(Bike& bike, float groundHeightAtX, float groundSlopeAtX) {
    switch (bike.state) {
        
        // -------------------------------------------------------------
        // 1. GROUNDED STATE
        // -------------------------------------------------------------
        case RiderState::Grounded: {
            bike.y = groundHeightAtX;

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
            float baseAngleFloat = (16.0f + (groundSlopeAtX * 2.0f)) - bike.wheelieAngle;
            while (baseAngleFloat < 0.0f) baseAngleFloat += 16.0f;
            while (baseAngleFloat >= 16.0f) baseAngleFloat -= 16.0f;
            bike.angle = baseAngleFloat;

            if (arduboy.pressed(A_BUTTON)) {
                bike.vx += BASE_ACCEL;
            }

            if (arduboy.justPressed(B_BUTTON) && bike.nitros > 0) {
                bike.vx += NITRO_BOOST;
                bike.nitros--;
            }

            bike.vx *= DRAG;
            bike.x += bike.vx;

            if (bike.x > TRACK1_MAX_X - 16) {
                bike.x = 0;
            }

            // Dynamic Takeoff Check: require UP_BUTTON input to achieve full jump launch
            float upwardVelocity = -bike.vx * groundSlopeAtX;
            if (groundSlopeAtX > 0.3f || upwardVelocity < -0.8f) {
                if (arduboy.pressed(UP_BUTTON)) {
                    bike.vy = upwardVelocity; // Full launch height when pressing UP
                } else {
                    bike.vy = upwardVelocity * 0.15f; // Damped low hop if UP is not pressed
                }
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
                bike.angularVel -= AIR_PITCH_SPEED;
            } else if (arduboy.pressed(RIGHT_BUTTON)) {
                bike.angularVel += AIR_PITCH_SPEED;
            }

            bike.angle += bike.angularVel;
            while (bike.angle >= 16.0f) bike.angle -= 16.0f;
            while (bike.angle < 0.0f)  bike.angle += 16.0f;

            bike.angularVel *= 0.60f;
            bike.vy += GRAVITY;
            bike.x += bike.vx;
            bike.y += bike.vy;

            if (bike.y >= groundHeightAtX) {
                bike.y = groundHeightAtX;

                // Match landing target detection to the new 2.0f terrain slope multiplier
                uint8_t landingAngle = (uint8_t)bike.angle % 16;
                uint8_t targetAngle = (uint8_t)(16 + (int8_t)(groundSlopeAtX * 2.0f)) % 16;
                int8_t angleDiff = abs((int8_t)landingAngle - (int8_t)targetAngle);

                if (angleDiff <= 2 || angleDiff >= 14) {
                    bike.vy = 0.0f;
                    bike.state = RiderState::Grounded;
                } else {
                    bike.state = RiderState::Crashing;
                    bike.crashTimer = 60;
                    bike.vx = 0.0f;
                    bike.vy = 0.0f;
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
    }
}

// -------------------------------------------------------------
// 6. RENDERING HELPER & TINY 3x5 HUD FONT
// -------------------------------------------------------------
void drawTinyChar(int16_t x, int16_t y, char c) {
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
                arduboy.drawPixel(x + col, y + row, WHITE);
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

void drawTerrain(float cameraX) {
    for (int16_t screenX = 0; screenX < 128; screenX += 2) {
        float worldX = cameraX + screenX;
        int16_t y1 = (int16_t)getGroundHeight(worldX);
        int16_t y2 = (int16_t)getGroundHeight(worldX + 2);
        
        arduboy.drawLine(screenX, y1, screenX + 2, y2, WHITE);
    }
}

void drawBikeWireframe(int16_t screenX, int16_t screenY, uint8_t angle) {
    int8_t dx = (int8_t)pgm_read_byte(&CHASSIS_DX[angle % 16]);
    int8_t dy = (int8_t)pgm_read_byte(&CHASSIS_DY[angle % 16]);

    // Lift bike center slightly above screenY so chassis doesn't clip into terrain line
    int16_t renderY = screenY - BIKE_Y_OFFSET;

    // Main bike frame line
    arduboy.drawLine(screenX - dx, renderY - dy, screenX + dx, renderY + dy, WHITE);
    
    // Front wheel dot to show direction/pitch clearly (pointing forward in direction of motion)
    arduboy.fillCircle(screenX + dx, renderY + dy, 1, WHITE);
}

// -------------------------------------------------------------
// 7. ARDUINO SETUP & LOOP
// -------------------------------------------------------------
void setup() {
    arduboy.begin();
    arduboy.setFrameRate(60);
}

void loop() {
    if (!arduboy.nextFrame()) return;
    arduboy.pollButtons();
    arduboy.clear();

    float currentGround = getGroundHeight(playerBike.x);
    float currentSlope  = getGroundSlope(playerBike.x);

    updateBike(playerBike, currentGround, currentSlope);

    float cameraX = playerBike.x - 32.0f;
    if (cameraX < 0) cameraX = 0;

    drawTerrain(cameraX);

    int16_t bikeScreenX = (int16_t)(playerBike.x - cameraX);
    int16_t bikeScreenY = (int16_t)playerBike.y;
    
    // Render wireframe chassis line with front-wheel directional dot
    drawBikeWireframe(bikeScreenX, bikeScreenY, (uint8_t)playerBike.angle % 16);

    // Ultra-compact 3x5 HUD rendering
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