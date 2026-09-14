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
    float y = 48.0f;

    float vx = 0.0f;
    float vy = 0.0f;

    uint8_t angle = 0;       // 0 to 15 mapped to rotated sprite angles
    float angularVel = 0.0f; // Mid-air rotation speed

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
constexpr float BASE_ACCEL = 0.15f;
constexpr float NITRO_BOOST = 0.60f;
constexpr float AIR_PITCH_SPEED = 0.4f;

// Global Game Objects
Bike playerBike;

// -------------------------------------------------------------
// 3. TRACK & TERRAIN DATA (PROGMEM)
// -------------------------------------------------------------
constexpr uint8_t TERRAIN_STEP_X = 8; // Pixels between heightmap samples

const uint8_t track1_heights[] PROGMEM = {
    48, 48, 48, 48, 48, 48, 48, 48, // Flat start (0 - 56px)
    44, 40, 36, 32, 28, 24,         // Ramp up (64 - 104px)
    24, 24, 24,                     // Crest (112 - 128px)
    28, 32, 36, 40, 44, 48,         // Ramp down (136 - 176px)
    48, 48, 48, 48, 48, 48, 48, 48, // Flat middle (184 - 240px)
    40, 32, 24, 16, 24, 32, 40, 48, // Whoops / Bumps (248 - 304px)
    48, 48, 48, 48, 48, 48, 48, 48  // Flat end (312 - 368px)
    // repeat course for testing:
    ,48, 48, 48, 48, 48, 48, 48, 48, // Flat start (0 - 56px)
    44, 40, 36, 32, 28, 24,         // Ramp up (64 - 104px)
    24, 24, 24,                     // Crest (112 - 128px)
    28, 32, 36, 40, 44, 48,         // Ramp down (136 - 176px)
    48, 48, 48, 48, 48, 48, 48, 48, // Flat middle (184 - 240px)
    40, 32, 24, 16, 24, 32, 40, 48, // Whoops / Bumps (248 - 304px)
    48, 48, 48, 48, 48, 48, 48, 48  // Flat end (312 - 368px)
        // repeat course for testing:
    ,48, 48, 48, 48, 48, 48, 48, 48, // Flat start (0 - 56px)
    44, 40, 36, 32, 28, 24,         // Ramp up (64 - 104px)
    24, 24, 24,                     // Crest (112 - 128px)
    28, 32, 36, 40, 44, 48,         // Ramp down (136 - 176px)
    48, 48, 48, 48, 48, 48, 48, 48, // Flat middle (184 - 240px)
    40, 32, 24, 16, 24, 32, 40, 48, // Whoops / Bumps (248 - 304px)
    48, 48, 48, 48, 48, 48, 48, 48  // Flat end (312 - 368px)
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
        
        case RiderState::Grounded: {
            bike.y = groundHeightAtX;
            bike.angle = (uint8_t)(8 + (groundSlopeAtX * 4.0f)) % 16;

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

            if (bike.y < groundHeightAtX - 2.0f) {
                bike.vy = -bike.vx * groundSlopeAtX;
                bike.state = RiderState::Airborne;
            }
            break;
        }

        case RiderState::Airborne: {
            if (arduboy.pressed(LEFT_BUTTON)) {
                bike.angularVel -= AIR_PITCH_SPEED;
            } else if (arduboy.pressed(RIGHT_BUTTON)) {
                bike.angularVel += AIR_PITCH_SPEED;
            }

            float currentAngleFloat = bike.angle + bike.angularVel;
            if (currentAngleFloat >= 16.0f) currentAngleFloat -= 16.0f;
            if (currentAngleFloat < 0.0f)  currentAngleFloat += 16.0f;
            bike.angle = (uint8_t)currentAngleFloat;

            bike.angularVel *= 0.85f;
            bike.vy += GRAVITY;
            bike.x += bike.vx;
            bike.y += bike.vy;

            if (bike.y >= groundHeightAtX) {
                bike.y = groundHeightAtX;

                uint8_t targetAngle = (uint8_t)(8 + (groundSlopeAtX * 4.0f)) % 16;
                int8_t angleDiff = abs((int8_t)bike.angle - (int8_t)targetAngle);

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

        case RiderState::Crashing: {
            if (bike.crashTimer > 0) {
                bike.crashTimer--;
            } else {
                bike.angle = 0;
                bike.state = RiderState::Grounded;
            }
            break;
        }
    }
}

// -------------------------------------------------------------
// 6. RENDERING HELPER
// -------------------------------------------------------------
void drawTerrain(float cameraX) {
    for (int16_t screenX = 0; screenX < 128; screenX += 2) {
        float worldX = cameraX + screenX;
        int16_t y1 = (int16_t)getGroundHeight(worldX);
        int16_t y2 = (int16_t)getGroundHeight(worldX + 2);
        
        arduboy.drawLine(screenX, y1, screenX + 2, y2, WHITE);
    }
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
    arduboy.fillRect(bikeScreenX - 4, bikeScreenY - 6, 8, 6, WHITE);

    arduboy.setCursor(0, 0);
    arduboy.print(F("Spd:")); arduboy.print(playerBike.vx, 1);
    arduboy.setCursor(64, 0);
    arduboy.print(F("Nitros:")); arduboy.print(playerBike.nitros);

    if (playerBike.state == RiderState::Crashing) {
        arduboy.setCursor(44, 20);
        arduboy.print(F("CRASH!"));
    }

    arduboy.display();
}