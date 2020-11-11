#include <SPI.h>
#include <MFRC522.h>
#include "FirebaseESP8266.h"
#include<ESP8266WiFi.h>

/**
 * Pinout ESP8266 E12 - MRFC522
 * =============================
 * E12          ||       RC522
 * =============================
 * D2     -------------  SDA(SS)
 * D5     -------------  SCK
 * D7     -------------  MOSI
 * D6     -------------  MISO(SCL)
 *        -------------  IRQ
 * GND    -------------  GND
 * D1     -------------  RST
 * 3.3v  -------------  3.3v
 * 
 */

#define PinSS D2
#define PinRST D1

// Set these to your WIFI settings
#define WIFI_SSID "wifi_ssid"
#define WIFI_PASSWORD "pass"

//Set these to the Firebase project settings
#define FIREBASE_URL "firebase_url"
#define FIREBASE_DB_SECRET "firebase_db_secret"
#define FIREBASE_FCM_SERVER_KEY "fcm_key"

#define MINIMUM_TIME_BETWEEN_CHECKIN 60
#define ROLLBACK_TIME 3600

MFRC522 reader(PinSS, PinRST);

FirebaseData firebaseData;
FirebaseData firebaseFCMData;

String rfidTagOld = "";
String rfidTagNew = "";
String busNumber = "KA-04-E-2365";
int timeCounter = 0;

void setup() {  
  Serial.begin(115200);

  Serial.println("Setup begin");
  
  SPI.begin();
  reader.PCD_DumpVersionToSerial();
  Serial.println("Performing PDC self test: ");
  Serial.print(reader.PCD_PerformSelfTest());
  reader.PCD_Init();
  
  // connect to wifi.
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.print("connected: ");
  Serial.println(WiFi.localIP());

  //begin Firebase
  Firebase.begin(FIREBASE_URL, FIREBASE_DB_SECRET);
  Firebase.reconnectWiFi(true);
  Firebase.setMaxRetry(firebaseData, 3);
  Firebase.setMaxErrorQueue(firebaseData, 30);
  Firebase.setReadTimeout(firebaseData, 1000 * 60);
  Firebase.setwriteSizeLimit(firebaseData, "tiny");
  
  Serial.println("Setup Done");
}

void loop() {
  if (timeCounter > ROLLBACK_TIME) {
    rfidTagOld = "";
    rfidTagNew = "";
    timeCounter = 0;
  }
  if (reader.PICC_IsNewCardPresent()) {
    readCard();
    if (isRegisterEnabled()) {
      registerCard();
    }
    else {
      if (rfidTagNew != rfidTagOld) {
        updateCardCheckedIn();
        timeCounter = 0;
      }
      else if (rfidTagNew == rfidTagOld && timeCounter > MINIMUM_TIME_BETWEEN_CHECKIN) {
        updateCardCheckedIn();
        timeCounter = 0;
      }
    }    
  }
  timeCounter++; 
  delay(1000);
}

void registerCard() {
  QueryFilter filter;
  filter.orderBy("cardID");
  filter.equalTo(rfidTagNew);
  if (Firebase.getJSON(firebaseData, "/Cards", filter)) {
    String jsonStr;
    firebaseData.jsonObject().toString(jsonStr, true);
    Serial.println(jsonStr);
    if (jsonStr != "{}") {
      Serial.println("This rfid card is already present!");
    }
    else {
      FirebaseJson newCardJson;
      newCardJson.set("cardID", rfidTagNew);
      newCardJson.set("isAvailable", true);
      newCardJson.set("linkedUser", "");
      if (Firebase.pushJSON(firebaseData, "/Cards", newCardJson)) {
        Serial.println("Card registered successfully!");     
      }
      else {
        Serial.println("Firebase reg push failed : ");
        Serial.print(firebaseData.errorReason());
      }
    }    
  }
  else {
    if (firebaseData.errorReason() != "") {
      Serial.println("Firebase query cards failed : ");
      Serial.print(firebaseData.errorReason());
    }
    
  }
  filter.clear();
  return;
}

void updateCardCheckedIn() {
  QueryFilter filter;
  filter.orderBy("cardID");
  filter.equalTo(rfidTagNew);
  if (Firebase.getJSON(firebaseData, "/Cards", filter)) {
    String jsonStr;
    firebaseData.jsonObject().toString(jsonStr, true);
    Serial.println(jsonStr);
    if (jsonStr != "{}") {
      String cardUid = getUID(jsonStr); 
      String path = "/CheckIns";
      FirebaseJson newTapInJson;
      newTapInJson.set("cardID", rfidTagNew);
      newTapInJson.set("busReg", busNumber);
      if (Firebase.pushJSON(firebaseData, path, newTapInJson)) {
        Serial.println("Card checkin recorded!");
        path += "/";
        path += firebaseData.pushName();
        path += "/timestamp";
        if (Firebase.setTimestamp(firebaseData, path)) {
          Serial.println("Timestamp added to checkin!");
//          sendFCMMessage(cardUid);
        }
        else {
          Serial.println("Firebase adding timestamp to checkin failed : ");
          Serial.print(firebaseData.errorReason());
        }
        rfidTagOld = rfidTagNew;
      }
      else {
        Serial.println("Firebase card checkin failed : ");
        Serial.print(firebaseData.errorReason());
      }
    }
    else {
      Serial.println("This rfid card is not yet registered!");
    }
  }
  else {
    Serial.println("Firebase update fetch failed : ");
    Serial.print(firebaseData.errorReason());
  }
  return;  
}

void sendFCMMessage(String cardUid) {
  String path = "/Cards/" + cardUid + "/linkedUser";
  if (Firebase.getString(firebaseData, path)) {
    if (firebaseData.stringData() == "") {
      Serial.println("No user linked to this card");
    }
    else {
      String linkedUser = firebaseData.stringData();
      String linkedUserpath = "/Users/" + linkedUser + "/FCMToken";
      if (Firebase.getString(firebaseData, linkedUserpath)) {
        if (firebaseData.stringData() == "") {
          Serial.println("No FCM token");
        }
        else {
          firebaseFCMData.fcm.begin(FIREBASE_FCM_SERVER_KEY);
          firebaseFCMData.fcm.addDeviceToken(firebaseData.stringData());

          firebaseFCMData.fcm.setPriority("normal");
          firebaseFCMData.fcm.setTimeToLive(5000);
          String message = "Your rfid card was checked into bus " + busNumber;
          firebaseFCMData.fcm.setNotifyMessage("Registered card checked in", message);
          if (Firebase.sendMessage(firebaseFCMData, 1))
          {
            //Success, print the result returned from server
            Serial.println(firebaseFCMData.fcm.getSendResult());
          }
          else
          {
            //Failed, print the error reason
            Serial.println(firebaseFCMData.errorReason());
          }
        }
      }
      else {
        Serial.println("Firebase FCM token failed: ");
        Serial.print(firebaseData.errorReason());
      }
    }
  }
  else {
    Serial.println("Firebase fetch linked user failed: ");
    Serial.print(firebaseData.errorReason());
  }
}

void readCard() {
  if(reader.PICC_ReadCardSerial()) {
    rfidTagNew = "";
    Serial.println("Tag ID: ");
    for(byte i = 0; i < reader.uid.size;i++) {
      Serial.print(reader.uid.uidByte[i], HEX);
      rfidTagNew += String(reader.uid.uidByte[i], HEX);
    }
    Serial.println();
  }
  return;
}

bool isRegisterEnabled() {
  if (Firebase.getBool(firebaseData,"/isRegisterCardEnabled")) {
    if (firebaseData.boolData() == 1) {
      return true;
    }
    else {
      return false;
    }
  }
  else {
    Serial.println("Firebase fetch card enabled fetch failed : ");
    Serial.println(firebaseData.errorReason());
    return false;
  }
}

String getUID(String jsonStr) {
  String finalUID = "";
  int indexOfStartUID = jsonStr.indexOf('"');
  indexOfStartUID += 1;
  int indexOfEndUID = jsonStr.indexOf(':');
  indexOfEndUID -= 1;
  finalUID = jsonStr.substring(indexOfStartUID,indexOfEndUID);
  return finalUID;
}
