

#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "logitech-mouse.h"

#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
    #include "Wire.h"
#endif

// class default I2C address is 0x68
// specific I2C addresses may be passed as a parameter here
// AD0 low = 0x68 (default for SparkFun breakout and InvenSense evaluation board)
// AD0 high = 0x69
MPU6050 mpu;
logiMouse logi_mouse(7, 8);

#define OUTPUT_READABLE_YAWPITCHROLL

// Unccomment if you are using an Arduino-Style Board
#define ARDUINO_BOARD

#define SIGN(x) ((x > 0) - (x < 0))
#define LED_PIN 13      // (Galileo/Arduino is 13)
#define GRAVITY_SCALE 16384.0
#define GRAVITY_CONSTANT 9.81
#define ACC_UPDATE_RATIO 0.98
#define ACC_UPDATE_RATIO_SLOW 0.998
#define INIT_ACC_UPDATE_RATIO 0.98
#define MOVE_THRESHOLD_HIGH 0.3
#define HIGH_THRESHOLD_RECOVERY_RATE 0.95
#define MOVE_THRESHOLD_LOW 0.15
#define MOVE_THRESHOLD_REVERSE 0.3
#define VELOCITY_THRESHOLD 0.03
#define UNDER_THRESHOLD_RESET 13
#define MOVE_VALUES_NUMBER 30
#define HIGH_PASS_LENGTH 3
#define NORM_FILTER 2
#define WHEEL_THRESHOLD 0.50
#define GYRO_THRESHOLD 1500
#define YAW_THRESHOLD 0.50
#define CLICK_ACCEL_THRESHOLD 0.3
#define CLICK_SIMPLE_ZERO_PASS_THRESHOLD 7
#define CLICK_DOUBLE_ZERO_PASS_THRESHOLD 12
#define CLICK_RESET_COUNT 4
#define YPR_THRESHOLD 0.01
#define YPR_AMP 1500

#define LOW_PASS_LENGTH 10

int low_pass_coeffs[LOW_PASS_LENGTH] = {1, 1, 2, 3, 4, 4, 3, 2, 1, 1};

struct acceleration_memory {
    float x[LOW_PASS_LENGTH];
    float y[LOW_PASS_LENGTH];
    float z[LOW_PASS_LENGTH];
};

struct gyro_val {
    int16_t ax;
    int16_t ay;
    int16_t az;
    int16_t gx;
    int16_t gy;
    int16_t gz;
};

bool blinkState = false;

// MPU control/status vars
bool dmpReady = false;  // set true if DMP init was successful
uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[64]; // FIFO storage buffer

// orientation/motion vars
VectorFloat gravity;    // [x, y, z]            gravity vector
Quaternion q;           // [w, x, y, z]         quaternion container
float euler[3];         // [psi, theta, phi]    Euler angle container
float last_ypr[3];
float ypr[3];           // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector

int error;

gyro_val av;  //accelereometer values
char click; 


// ================================================================
// ===               INTERRUPT DETECTION ROUTINE                ===
// ================================================================

// This function is not required when using the Galileo 
volatile bool mpuInterrupt = false;     // indicates whether MPU interrupt pin has gone high
void dmpDataReady() {
    mpuInterrupt = true;
}

void stabilize() {
    VectorFloat accel;
    VectorFloat moy_acc(3.0,3.0,3.0);
    if (!dmpReady) return;
    Serial.println("start stabilization");
    int count = 0;
    do {
        
        error = get_gyro_vals();
        if (error == 0) {
            mpu.getMotion6(&(av.ax), &(av.ay), &(av.az), &(av.gx), &(av.gy), &(av.gz));
            accel.x = GRAVITY_CONSTANT * float(av.ax) / GRAVITY_SCALE;
            accel.y = GRAVITY_CONSTANT * float(av.ay) / GRAVITY_SCALE;
            accel.z = GRAVITY_CONSTANT * float(av.az) / GRAVITY_SCALE;

            //update mean acceleration
            moy_acc.x = moy_acc.x * INIT_ACC_UPDATE_RATIO + accel.x * (1.0 - INIT_ACC_UPDATE_RATIO);
            moy_acc.y = moy_acc.y * INIT_ACC_UPDATE_RATIO + accel.y * (1.0 - INIT_ACC_UPDATE_RATIO);
            moy_acc.z = moy_acc.z * INIT_ACC_UPDATE_RATIO + accel.z * (1.0 - INIT_ACC_UPDATE_RATIO);
            accel.sub(moy_acc);
            Serial.print(-3);
            
            Serial.print(", ");
            Serial.print(-3 - MOVE_THRESHOLD_HIGH);
            Serial.print(", ");
            Serial.print(-3 + MOVE_THRESHOLD_HIGH);
            Serial.print(", ");
            Serial.print(-3 + accel.y);
            Serial.print(", ");
            Serial.print(count);
            Serial.println();
            if (abs(accel.x) < 0.03 && abs(accel.y) < 0.03 && abs(accel.z) < 0.03)
                count++;
            else
                count = 0;
        }
        delay(2);
    } while(count < 5);
    last_ypr[0] = ypr[0];
    last_ypr[1] = ypr[1];
    last_ypr[2] = ypr[2];
}
// ================================================================
// ===                      INITIAL SETUP                       ===
// ================================================================
void setup() {
    // join I2C bus (I2Cdev library doesn't do this automatically)
    #if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
        Wire.begin();
        int TWBR; // 400kHz I2C clock (200kHz if CPU is 8MHz)
        TWBR = 24;
    #elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
        Fastwire::setup(400, true);
    #endif
    Serial.begin(230400);

    // initialize device
    Serial.println(F("Initializing I2C devices..."));
    mpu.initialize();

    // verify connection
    Serial.println(F("Testing device connections..."));
    Serial.println(F("MPU6050 connection "));
    Serial.print(mpu.testConnection() ? F("successful") : F("failed"));

    // load and configure the DMP
    Serial.println(F("Initializing DMP..."));
    devStatus = mpu.dmpInitialize();
    Serial.println(mpu.getRate());
    mpu.setRate(8);

    // supply your own gyro offsets here, scaled for min sensitivity
    mpu.setXGyroOffset(120);
    mpu.setYGyroOffset(-38);
    mpu.setZGyroOffset(36);
    mpu.setXAccelOffset(-4360);
    mpu.setYAccelOffset(-3695);
    mpu.setZAccelOffset(1295);

    // make sure it worked (returns 0 if so)
    if (devStatus == 0) {
        // turn on the DMP, now that it's ready
        Serial.println(F("Enabling DMP..."));
        mpu.setDMPEnabled(true);

        // enable Arduino interrupt detection
        Serial.println(F("Enabling interrupt detection (Arduino external interrupt 0)..."));
        attachInterrupt(0, dmpDataReady, RISING);
        mpuIntStatus = mpu.getIntStatus();

        // set our DMP Ready flag so the main loop() function knows it's okay to use it
        Serial.println(F("DMP ready! Waiting for first interrupt..."));
        dmpReady = true;

        // get expected DMP packet size for later comparison
        packetSize = mpu.dmpGetFIFOPacketSize();
    } else {
        // ERROR!
        // 1 = initial memory load failed
        // 2 = DMP configuration updates failed
        // (if it's going to break, usually the code will be 1)
        Serial.print(F("DMP Initialization failed (code "));
        Serial.print(devStatus);
        Serial.println(F(")"));
    }

    // configure LED for output
    pinMode(LED_PIN, OUTPUT);
    if (!dmpReady) 
        {Serial.println("INIT FAILED!");}
    stabilize();
    
    Serial.print("begin started : ");
    Serial.println(logi_mouse.begin());
    Serial.println("begin ended");
    Serial.println("pair started");
    logi_mouse.pair();
    Serial.println("pair ended");
    
}
// ================================================================
// ===                    MAIN PROGRAM LOOP                     ===
// ================================================================
char avoid_val = 0;
void loop() {
    // if programming failed, don't try to do anything
    if (!dmpReady) return;
    error = get_gyro_vals();
    if (error == 0) {
        int move_x = 0; 
        int move_y = 0; 
        if (abs(last_ypr[0] - ypr[0]) > YPR_THRESHOLD) {
            move_x = int((last_ypr[0] - ypr[0]) * YPR_AMP);
            last_ypr[0] = ypr[0];
        }
        if (abs(last_ypr[1] - ypr[1]) > YPR_THRESHOLD) {
            move_y = int((last_ypr[1] - ypr[1]) * YPR_AMP);
            last_ypr[1] = ypr[1];
        }
        if ( (move_x || move_y) && avoid_val == 0 ) {
            uint32_t t1 = micros();
            logi_mouse.move(-move_x, move_y);
            Serial.println(micros() - t1);
        } else {
            //Serial.println("No move");
        }
        avoid_val = 0;
        /*
        Serial.print(ypr[0] + 6);
        Serial.print(", ");
        Serial.print(last_ypr[0] + 6);
        Serial.print(", ");
        Serial.print(6);
        Serial.print(", ");
        Serial.print(ypr[1] + 3 );
        Serial.print(", ");
        Serial.print(last_ypr[1] + 3 );
        Serial.print(", ");
        Serial.print(3);
        Serial.print(", ");
        Serial.print(ypr[2]);
        Serial.print(", ");
        Serial.print(0);
        Serial.print(", ");
        Serial.print(move_x - 3);
        Serial.print(", ");
        Serial.print(-3);
        Serial.print(", ");
        Serial.print(move_y - 6);
        Serial.print(", ");
        Serial.print(-6);
        Serial.println();
        */
        
    } else {
        //Serial.println("Error happened");
    }
}


int click_detection() 
{
    return 0;
}


/*
Calcutate q, gravity and ypr from MPU6050
return 0 on success
-1 on fail or not enough data to read
*/
int get_gyro_vals() 
{

    // wait for MPU interrupt or extra packet(s) available

    #ifdef ARDUINO_BOARD
        while (!mpuInterrupt && fifoCount < packetSize) {
        }
    #endif

    // reset interrupt flag and get INT_STATUS byte
    mpuInterrupt = false;
    mpuIntStatus = mpu.getIntStatus();

    // get current FIFO count
    fifoCount = mpu.getFIFOCount();

    // check for overflow (this should never happen unless our code is too inefficient)
    if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
        // reset so we can continue cleanly
        mpu.resetFIFO();
        Serial.println(F("FIFO overflow!"));
        avoid_val = 1;
        return -1;
    // otherwise, check for DMP data ready interrupt (this should happen frequently)
    } else if (mpuIntStatus & 0x02) {
        // wait for correct available data length, should be a VERY short wait
        while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();

        // read a packet from FIFO
        mpu.getFIFOBytes(fifoBuffer, packetSize);
        
        // track FIFO count here in case there is > 1 packet available
        // (this lets us immediately read more without waiting for an interrupt)
        fifoCount -= packetSize;

        #ifdef OUTPUT_READABLE_YAWPITCHROLL
            // display Euler angles in degrees
            mpu.dmpGetQuaternion(&q, fifoBuffer);
            mpu.dmpGetGravity(&gravity, &q);
            mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
            
        #endif
        return 0;
    }
    return -1;
}

Quaternion user_repere(Quaternion quat) 
{
    float angle;
    Quaternion r(0,0,0,0);
    VectorFloat v(1,0,0);
    v.rotate(&quat);
    angle = -atan2(v.y,v.x);
    r.w = cos(angle/2.0);
    r.z = sin(angle/2.0);
    return r.getProduct(quat);
}