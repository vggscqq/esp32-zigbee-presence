// vgscq was here

#ifndef ZIGBEE_MODE_ED
#error "Zigbee end device mode is not selected in Tools->Zigbee mode"
#endif

#include <Zigbee.h>
#include "esp_random.h"

// mmWave Sensor Configuration
#define RX_PIN D2
#define TX_PIN D3
#define sensorSerial Serial0
#define LD2410_BAUD_RATE 9600
#define FRAME_HEADER 0xF1F2F3F4
#define FRAME_FOOTER 0xF5F6F7F8
#define MAX_FRAME_LENGTH 64

#pragma pack(push, 1)
typedef struct {
    uint8_t dataType;
    uint8_t header;
    uint8_t targetStatus;
    uint16_t movingDistance;
    uint8_t movingEnergy;
    uint16_t stationaryDistance;
    uint8_t stationaryEnergy;
    uint16_t detectionDistance;
    uint8_t tail;
    uint8_t calibration;
} BasicTargetData;
#pragma pack(pop)

// Zigbee Configuration
#define OCCUPANCY_SENSOR_ENDPOINT_NUMBER 10
uint8_t button = BOOT_PIN;

ZigbeeOccupancySensor zbOccupancySensor(OCCUPANCY_SENSOR_ENDPOINT_NUMBER);

// Sensor Processing Variables
uint8_t frameBuffer[MAX_FRAME_LENGTH];
uint8_t frameIndex = 0;
bool inFrame = false;
uint16_t expectedLength = 0;
bool currentOccupancy = false;
bool lastOccupancy = false;

bool b_getTargetState(uint8_t state) {
    return (state != 0x00); // 0x00 = no target, others = presence
}

void parseSensorData(uint8_t* data, uint16_t length) {
    // Verify frame structure
    //if(*(uint32_t*)data != FRAME_HEADER || 
    //   *(uint32_t*)(data + length - 4) != FRAME_FOOTER) return;
    if(*(uint32_t*)data != FRAME_HEADER || *(uint32_t*)(data + length - 4) != FRAME_FOOTER) {
      Serial.println("Bad frame recieved.");
      return;
    }

    BasicTargetData* target = (BasicTargetData*)(data + 6);
    currentOccupancy = b_getTargetState(target->targetStatus);
    //Serial.print("Occupancy upraded: ");
    //Serial.println(currentOccupancy);
}

// Modified serial processing with non-blocking behavior
void processSerial() {
    static uint32_t lastByteTime = 0;
    const uint32_t frameTimeout = 100; // ms
    
    while(sensorSerial.available()) {
        uint8_t byte = sensorSerial.read();
        uint32_t now = millis();

        // Reset frame if too old
        if(inFrame && (now - lastByteTime > frameTimeout)) {
            frameIndex = 0;
            inFrame = false;
            expectedLength = 0;
        }
        lastByteTime = now;

        // Frame detection state machine
        if(!inFrame) {
            // Shift buffer (FIFO for header detection)
            if(frameIndex >= 4) {
                memmove(frameBuffer, frameBuffer+1, 3);
                frameIndex = 3;
            }
            frameBuffer[frameIndex++] = byte;

            // Header check
            if(frameIndex >= 4 && *(uint32_t*)frameBuffer == FRAME_HEADER) {
                inFrame = true;
                frameIndex = 4; // Keep header in buffer
            }
        } 
        else {
            frameBuffer[frameIndex++] = byte;

            // Get expected length after receiving 6 bytes
            if(frameIndex == 6) {
                expectedLength = *(uint16_t*)(frameBuffer + 4) + 10;
            }

            // Process complete frame
            if(frameIndex >= expectedLength && expectedLength > 0) {
                parseSensorData(frameBuffer, expectedLength);
                
                // Reset for next frame
                inFrame = false;
                frameIndex = 0;
                expectedLength = 0;
                return; // Exit after processing one complete frame
            }

            // Buffer overflow protection
            if(frameIndex >= MAX_FRAME_LENGTH) {
                inFrame = false;
                frameIndex = 0;
                expectedLength = 0;
            }
        }
        
        // Yield to other tasks after processing each byte
        if(millis() - lastByteTime > 10) break;
    }
}

void setup() {
    Serial.begin(9600);
    sensorSerial.begin(LD2410_BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
    
    pinMode(button, INPUT_PULLUP);
    
    zbOccupancySensor.setManufacturerAndModel("Espressif", "Zigbee-mmWave-Sensor");
    Zigbee.addEndpoint(&zbOccupancySensor);

    if(!Zigbee.begin()) {
        Serial.println("Zigbee init failed!");
        ESP.restart();
    }
    
    while(!Zigbee.connected()) delay(100);
    Serial.println("Zigbee connected");
}

void loop() {
    processSerial();

    if(currentOccupancy != lastOccupancy) {
        zbOccupancySensor.setOccupancy(currentOccupancy);
        zbOccupancySensor.report();
        lastOccupancy = currentOccupancy;
        Serial.printf("Occupancy: %s\n", currentOccupancy ? "Detected" : "Clear");
    }

    // Factory reset handling
    if(digitalRead(button) == LOW) {
        delay(100);
        int start = millis();
        while(digitalRead(button) == LOW && (millis() - start) < 3000);
        if(millis() - start >= 3000) {
            Zigbee.factoryReset();
            ESP.restart();
        }
    }
    
    delay(50);
}