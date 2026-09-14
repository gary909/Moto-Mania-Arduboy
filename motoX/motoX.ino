#include <Arduboy2.h>

// Global Arduboy Instance
Arduboy2 arduboy;

// Game States
enum class RiderState : uint8_t {
    Grounded,
    Airborne,
    Crashing
};

struct Bike {
    float x = 0.0f;
    float y = 32.0f; // Default starting height on screen

    float vx = 0.0f;
    float vy = 0.0f;

    uint8_t angle = 0;       // 0 to 15 mapped to rotated sprite angles
    float angularVel = 0.0f; // Mid-air rotation speed

    uint8_t nitros = 3;
    bool nitroActive = false;
    uint8_t crashTimer = 0;

    RiderState state = RiderState::Grounded;
};

// Physics Constants
constexpr float GRAVITY = 0.25f;
constexpr float DRAG = 0.985f;
constexpr float BASE_ACCEL = 0.15f;
constexpr float NITRO_BOOST = 0.60f;
constexpr float AIR_PITCH_SPEED = 0.4f;

// Global Game Objects
Bike playerBike;

// Dummy Ground Mock (returns flat ground at y=48, slope=0.0)
float getGroundHeight(float x) { return 48.0f; }
float getGroundSlope(float x)  { return 0.0f;  }

void updateBike(Bike& bike, float groundHeightAtX, float groundSlopeAtX) {
    switch (bike.state) {
        
        // -------------------------------------------------------------
        // 1. GROUNDED STATE
        // -------------------------------------------------------------
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

            if (bike.y < groundHeightAtX - 2.0f) {
                bike.vy = -bike.vx * groundSlopeAtX;
                bike.state = RiderState::Airborne;
            }
            break;
        }

        // -------------------------------------------------------------
        // 2. AIRBORNE STATE
        // -------------------------------------------------------------
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

        // -------------------------------------------------------------
        // 3. CRASHING STATE
        // -------------------------------------------------------------
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

void setup() {
    arduboy.begin();
    arduboy.setFrameRate(60);
}

void loop() {
    if (!arduboy.nextFrame()) return;
    arduboy.pollButtons();
    arduboy.clear();

    // Fetch terrain data at bike position
    float currentGround = getGroundHeight(playerBike.x);
    float currentSlope  = getGroundSlope(playerBike.x);

    // Update Physics State Machine
    updateBike(playerBike, currentGround, currentSlope);

    // Basic On-screen Debug Rendering
    arduboy.setCursor(0, 0);
    arduboy.print(F("Speed: ")); arduboy.print(playerBike.vx);
    arduboy.setCursor(0, 10);
    arduboy.print(F("Nitros: ")); arduboy.print(playerBike.nitros);

    // Draw simple ground line & bike box placeholder
    arduboy.drawFastHLine(0, 48, 128, WHITE);
    arduboy.fillRect(20, (int16_t)playerBike.y - 4, 8, 4, WHITE);

    arduboy.display();
}