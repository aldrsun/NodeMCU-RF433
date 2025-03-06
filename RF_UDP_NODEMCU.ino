#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <vector>
#include <memory>

#include "constants.h" // SSID, PASSWORD, UDP_ADDRESS, UDP_PORT

#define READ_PIN(pin) ((GPI & (1 << pin)) != 0)

#define DATA_PIN 12       //D6
#define TRANSMIT_PIN 4    //D2

unsigned int data_counter = 1;
struct messageStruct {
  unsigned int id;
  String time;
  unsigned int code;
  unsigned long localTimeMillis;
};
std::vector<std::unique_ptr<messageStruct>> messageQueue;
unsigned long lastHandleQueueTime; // in milliseconds
const unsigned long handleQueuePeriod = 2000; // in milliseconds

const char* ssid = SSID;
const char* password = PASSWORD;
const char* udpAddress = UDP_ADDRESS;
const int udpPort = UDP_PORT;

WiFiUDP udp;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 10800, 600000); // 10800 is UTC offset in seconds

char incomingPacket[8];

IPAddress local_IP(192, 168, 1, 236);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns1(8, 8, 8, 8);
IPAddress dns2(8, 8, 4, 4);

void setup() {
  pinMode(DATA_PIN, INPUT);
  pinMode(TRANSMIT_PIN, OUTPUT);
  
  WiFi.config(local_IP, gateway, subnet, dns1, dns2);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  udp.begin(udpPort);

  timeClient.begin();
  timeClient.update();

  lastHandleQueueTime = millis();
}

void loop() {
  // Receive UDP packets and Process
  int packetSize = udp.parsePacket();
  if (packetSize) {
    int len = udp.read(incomingPacket, packetSize);
    if (len > 0) {
      incomingPacket[len] = 0;  // NULL TERMINATOR
    }
    if (len == 8) {
      uint32_t message_type    = (incomingPacket[0] << 24) | (incomingPacket[1] << 16) | (incomingPacket[2] << 8) | incomingPacket[3]; // Big-endian to integer
      uint32_t message_content = (incomingPacket[4] << 24) | (incomingPacket[5] << 16) | (incomingPacket[6] << 8) | incomingPacket[7]; // Big-endian to integer

      switch(message_type)
      {
        case 0:
          for (auto it = messageQueue.begin(); it != messageQueue.end();)
          {
              if ((*it)->id == message_content)
              {
                  messageQueue.erase(it);
                  break;
              }
              else
              {
                  ++it;
              }
          }
          break;
        case 1:
          transmit(message_content);
          break;
      }
    }
  }
  
  // Reveice RF Signals
  handleRFReceiver();

  // Handle Message Queue Periodically
  if(lastHandleQueueTime < millis() - handleQueuePeriod)
  {
    handleMessageQueue();
    lastHandleQueueTime = millis();
  }
}

bool handleRFReceiver()
{
  unsigned long receivedData = 0;
  if(readEV1527Signal(receivedData)) {
    // without adding blocking delay(), prevent multiple readings of the same button press event.
    static unsigned int lastReceivedCode = 0;
    static unsigned int lastReceivedCodeTimeMillis = millis();
    if(lastReceivedCode != receivedData || millis() - lastReceivedCodeTimeMillis > 10000)
    {
      lastReceivedCode = receivedData;
      lastReceivedCodeTimeMillis = millis();

      String message_time = timeClient.getFormattedTime();
      sendCodeUdp(data_counter, message_time, receivedData);
      std::unique_ptr<messageStruct> message_ptr = std::make_unique<messageStruct>(messageStruct{data_counter, message_time, receivedData, millis()});
      messageQueue.push_back(std::move(message_ptr));
      data_counter ++;
      
      return true;
    }
  }
  return false;
}

bool readEV1527Signal(unsigned long &data) {
  data = 0;
  int i = 0;
  static int lastState = HIGH;
  static unsigned long startTime = micros();
  static unsigned long preambleHigh = 0;
  static unsigned long preambleLow = 0;
  static bool preamble_state = false;

  if(READ_PIN(DATA_PIN) == lastState)
    return false;
  if(!preamble_state)
  {
    lastState = LOW;
    preambleHigh = micros() - startTime;
    startTime = micros();
    preamble_state = true;
    return false;
  }
  preambleLow = micros() - startTime;
  startTime = micros();
  preamble_state = false;
  lastState = HIGH;
  if(preambleHigh > 1000 || preambleLow < 4000 || preambleLow > 10000)
    return false;

  // BEGINNING OF A BLOCKING CODE
  while (i < 24) {
    while (READ_PIN(DATA_PIN) == HIGH) {}
    const unsigned long highDuration = micros() - startTime;
    startTime = micros();
    while (READ_PIN(DATA_PIN) == LOW) {}
    const unsigned long lowDuration = micros() - startTime;
    startTime = micros();
    if((highDuration < 350 && highDuration > 100 && lowDuration > 450 && lowDuration < 1000) ||
        (lowDuration < 350 && lowDuration > 100 && highDuration > 450 && highDuration < 1000))
    {
      data = (data << 1) | (highDuration > 400 ? 1 : 0);
      i ++;
    }
    else
    {
      return false;
    }
  }

  return true;
}

void transmit(int data) {
  for(int count = 0; count < 10; count ++)
  {
    //Send Preamble
    digitalWrite(TRANSMIT_PIN, HIGH);
    delayMicroseconds(200);
    digitalWrite(TRANSMIT_PIN, LOW);
    delayMicroseconds(6000);
    for (int i = 23; i >= 0; i--) {
        int bit = (data >> i) & 1;

        if (bit == 1) {
            // Send pulse for '1'
            digitalWrite(TRANSMIT_PIN, HIGH);
            delayMicroseconds(600);
            digitalWrite(TRANSMIT_PIN, LOW);
            delayMicroseconds(200);
        } else {
            // Send pulse for '0'
            digitalWrite(TRANSMIT_PIN, HIGH);
            delayMicroseconds(200);
            digitalWrite(TRANSMIT_PIN, LOW);
            delayMicroseconds(600);
        }
    }
  }
}

void handleMessageQueue()
{
  if(messageQueue.size() > 0)
  {
    if(millis() - messageQueue.front()->localTimeMillis > 10000)
    {
      sendCodeUdp(messageQueue.front()->id, messageQueue.front()->time, messageQueue.front()->code);
    }
  }
}

void sendCodeUdp(const unsigned int&id, const String& time, const unsigned int &code)
{
  String message = String(id) + ";" + String(2) + ";" + time + ";" + String(code);
  udp.beginPacket(udpAddress, udpPort);
  udp.write(message.c_str());
  udp.endPacket();
}
