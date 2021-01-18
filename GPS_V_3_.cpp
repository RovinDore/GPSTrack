/* ============================================================================================================================
    Version:        3
    App:            My Ride GPS tracker
    Description:    This version updates location in json {coords{lat, lng} speed, degrees, time, gpsState, busName}
                    smart enough to turn off gpsstate while running only update gpsState to false once.
                    get details(busName) from firebase in on setup.
    Owner:          Rovin Dore
    Features:       ON LED, GPS LED, ACTIVE LED, ACTIVE SWITCH 15 interval
============================================================================================================================ */

//Libraries
#include <TinyGPS++.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <FirebaseArduino.h>

// Global variables
#define FIREBASE_HOST "" //Enter firebase credentials
#define FIREBASE_AUTH "" //Enter firebase credentials
#define WIFI_SSID "QJ4HT"
#define WIFI_PASSWORD "RCS5X8Q6YZ9QK6S7"

/*
   This sketch demonstrates the normal use of a TinyGPS++ (TinyGPSPlus) object.
   It requires the use of SoftwareSerial, and assumes that you have a
   9600-baud serial GPS device hooked up on pins D2(RX) and D1(TX).
*/

// GPS Pin details
static const int RXPin = 05, TXPin = 04;
static const uint32_t GPSBaud = 9600;

//other pins
const int WIFI_LED      = 12;   // GREEN        
const int GPS_LED       = 14;   // RED
const int ACTIVE_LED    = 15;   // BLUE
const int ACTIVE_SWITCH = 0;    // Switch

// The TinyGPS++ object
TinyGPSPlus gps;

// The serial connection to the GPS device
SoftwareSerial gpsSerial(RXPin, TXPin);

//Global variables
unsigned long currentMillis;
const long interval             = 30000;        // interval to send location (milliseconds)
unsigned long previousMillis    = 0;            // will store last time location was updated
int retryTime_FB                = 0;
bool GPS_ACTIVE                 = true;
bool DEBUG                      = true;         // print details to serial
bool doSwitch                   = true;         // use ACTIVE switch?
bool PUSH_DATA                  = false;        // insert new coords into database or set one
bool CAR_ON                     = true;         // use later for ignition switch

//FB details
const String DEVICE_ID  = "QrtknI1IiEqWuDNjz05dvw";
const String DB_Name    = "DevBoard";
String DB_ROOT          = "/MyRide/";
String DB_URL           = "/MyRide/BusGEO/"+DB_Name+"/";
String DB_URL_PUSH      = "/MyRide/PushGEO/"+DB_Name+"/";
String DB_URL_GET       = "/Info/"+DEVICE_ID+"/";
String BUSNAME          = "";

void setup(){
    pinMode(WIFI_LED, OUTPUT);
    pinMode(GPS_LED, OUTPUT);
    pinMode(ACTIVE_LED, OUTPUT);
    pinMode(ACTIVE_SWITCH, INPUT_PULLUP);

    Serial.begin(9600);
    gpsSerial.begin(GPSBaud);

    //Connect to wifi
    initWifi();
}

void initWifi(){
    int retryTime = 0;

    // connect to wifi.
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("connecting to wifi: ");
    while (WiFi.status() != WL_CONNECTED){
        retryTime += 1;
        Serial.print(".");
        
        //Blink LED to show status
        digitalWrite(WIFI_LED, HIGH);
        delay(500);
        digitalWrite(WIFI_LED, LOW);
        delay(500);

        // if 10 seconds passed and not connected run begin code again
        if (retryTime >= 10){
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            retryTime = 0;
            Serial.println();
        }
    }

    Serial.println();
    Serial.print("Connected: ");
    Serial.println(WiFi.localIP());

    //Connect to firebase db
    Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
    if (Firebase.failed()) {
        Serial.print("Couldn't connect to Firebase:");
        Serial.println(Firebase.error());  
        delay(500);
        Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
    } else{
        Serial.println("Connected to firebase");

        // digitalWrite(WIFI_LED, HIGH);
        BUSNAME = Firebase.getString(DB_URL_GET+"trackerName");
        delay(500);
        PUSH_DATA = Firebase.getBool(DB_URL_GET+"pushData");
        delay(500);
        
        while(Firebase.success() != true){
            BUSNAME = Firebase.getString(DB_URL_GET+"trackerName");
            PUSH_DATA = Firebase.getBool(DB_URL_GET+"pushData");
            
            //Blink LED to show status
            digitalWrite(ACTIVE_LED, HIGH);
            delay(500);
            digitalWrite(ACTIVE_LED, LOW);
            delay(500);
        }
    }

    //Keep LED on
    digitalWrite(WIFI_LED, HIGH);
}

void loop(){
    //Check if WIFI connected all times
    if (WiFi.status() == WL_CONNECTED){
        if(doSwitch == true){
            readSwitch();
        }
        if(CAR_ON == true){
            mainProcess();
        } else{
            Serial.println("Vehicle Off");
        }
    } else{
        initWifi();
    }
}

// Read status of ACTIVE_SWITCH
void readSwitch(){
    // if switch on set GPS_ACTIVE and LED on
    if(digitalRead(ACTIVE_SWITCH) == HIGH){
        GPS_ACTIVE = true;
        digitalWrite(ACTIVE_LED, HIGH);
    } else{
        GPS_ACTIVE = false;
        digitalWrite(ACTIVE_LED, LOW);
    }
}

// Determine if to send gps location
void mainProcess(){
    // This sketch displays information every time a new sentence is correctly encoded.
    if (gpsSerial.available() > 0){
        if (gps.encode(gpsSerial.read())){
            if(DEBUG == true){
                displayInfo();
            }
            
            //Get current milliseconds 
            currentMillis = millis();

            //if location from gps valid
            if(gps.location.isValid()){ 
                if (currentMillis - previousMillis >= interval) {
                    sendLocation();
                }
            }
        }
    }
        
    if (millis() > 5000 && gps.charsProcessed() < 10)
        Serial.println(F("No GPS detected: check wiring."));
}

// Get and send gps location
void sendLocation(){
    String myDate;
    float myLat;
    float myLng;
    double mySpeed;
    double myDeg;
    bool fbState = true;

    //if switch off check if bus already turned off in backend
    if(GPS_ACTIVE == false){
        fbState = Firebase.getBool(DB_URL+"gpsState");
        delay(500);
        while(Firebase.success() != true){
            fbState = Firebase.getBool(DB_URL+"gpsState");
            delay(500);
        }
    } 

    if(fbState == true){
        if (DEBUG)
            Serial.println("About to send location");

        //Set values for json data
        myLat   = gps.location.lat(), 6;
        myLng   = gps.location.lng(), 6;
        mySpeed = gps.speed.mph();
        myDeg   = gps.course.deg();
        myDate  = String(gps.date.year()) + '-';
        myDate  = myDate + String(gps.date.month()) + '-';
        myDate  = myDate + String(gps.date.day()) + ' ';
        
        //Concatinate time values
        if (gps.time.hour() < 10) 
            myDate  = myDate + '0' + gps.time.hour() + ':';
        else
            myDate  = myDate + gps.time.hour() + ':';
        
        if (gps.time.minute() < 10)
            myDate  = myDate + '0' + gps.time.minute() + ':';
        else
            myDate  = myDate + gps.time.minute() + ':';
        
        if (gps.time.second() < 10) 
            myDate  = myDate + '0' + gps.time.second();  
        else
            myDate  = myDate + gps.time.second(); 
        //2018-01-27 10:30
        
        //Create JsonObject with gps data
        const size_t bufferSize = JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(6);
        DynamicJsonBuffer jsonBuffer(bufferSize);
        JsonObject& root = jsonBuffer.createObject();
        JsonObject& coords = root.createNestedObject("coords");
        coords["lat"] = myLat;
        coords["lng"] = myLng;
        root["time"] = myDate;
        root["speed"] = mySpeed;
        root["degrees"] = myDeg;
        root["gpsState"] = GPS_ACTIVE;
        root["busName"] = BUSNAME;

        if(DEBUG){
            root.printTo(Serial);
        }

        //Push or set data
        if(PUSH_DATA){
            Firebase.push(DB_URL_PUSH, root);
        } else{
            Firebase.set(DB_URL, root);
        }

        if(Firebase.failed()){
            retryTime_FB += 1;
            
            if(DEBUG){
                Serial.println();
                Serial.println("Setting location failed: " + String(retryTime_FB));
                Serial.println(Firebase.error()); 
            }

            if(retryTime_FB >= 5){
                Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
                previousMillis = millis();
                retryTime_FB = 0;
            }
        } else{
            if(DEBUG){
                Serial.println();
                Serial.println("Location sent: " + String(retryTime_FB));
            }
                
            if(GPS_ACTIVE == true){
                digitalWrite(GPS_LED, HIGH);
                delay(500);
                digitalWrite(GPS_LED, LOW);
            }

            retryTime_FB = 0;
            // save the last time you sent location
            previousMillis = millis();
        }
    } else{
        previousMillis = millis();
    }
}

// show details in serial
void displayInfo(){
    Serial.print(F("Location: ")); 
    if (gps.location.isValid()){
        Serial.print(gps.location.lat(), 6);
        Serial.print(F(","));
        Serial.print(gps.location.lng(), 6);
    } else{
        Serial.print(F("INVALID"));
    }

    Serial.print(F("  Date/Time: "));
    if (gps.date.isValid()){
        Serial.print(gps.date.month());
        Serial.print(F("/"));
        Serial.print(gps.date.day());
        Serial.print(F("/"));
        Serial.print(gps.date.year());
    } else{
        Serial.print(F("INVALID"));
    }

    Serial.print(F(" "));
    if(gps.time.isValid()){
        if (gps.time.hour() < 10) 
            Serial.print(F("0"));
        Serial.print(gps.time.hour());
        Serial.print(F(":"));
        if (gps.time.minute() < 10) 
            Serial.print(F("0"));
        Serial.print(gps.time.minute());
        Serial.print(F(":"));
        if (gps.time.second() < 10) 
            Serial.print(F("0"));
        Serial.print(gps.time.second());
        Serial.print(F("."));
        if (gps.time.centisecond() < 10) 
            Serial.print(F("0"));
        Serial.print(gps.time.centisecond());
    } else{
        Serial.print(F("INVALID"));
    }

    Serial.println();
}