/*
 Train controller by KVP
 CC-NC-SA-BY 4.0

 tested with esp32-wroom-32
*/
#include "WiFi.h"
#include "AsyncUDP.h"
#include "Preferences.h"

#define PWM1_PIN (16)
#define PWM2_PIN (17)

void pwmSetup()
{
  ledcSetup(0, 32000, 8);
  ledcAttachPin(PWM1_PIN, 0);
  ledcWrite(0, 0);
  ledcSetup(1, 32000, 8);
  ledcAttachPin(PWM2_PIN, 1);
  ledcWrite(1, 0);
}

void pwmSet(int output, int value)
{
  int ch0 = -1, ch1 = -1;
  switch (output)
  {
    case 0:
      ch0 = 0;
      ch1 = 1;
      break;
    default:
      return;
  }
  if (value == 0)
  {
    ledcWrite(ch0, 0);
    ledcWrite(ch1, 0);
  } else if (value > 0)
  {
    ledcWrite(ch0, 0);
    ledcWrite(ch1, value);
  } else
  {
    ledcWrite(ch1, 0);
    ledcWrite(ch0, -value);
  }
}

const char *ssidDefault = "";
const char *apnPasswordDefault = "";

AsyncUDP udp;
Preferences preferences;

#define CONFIG_APNSSID "apnssid"
#define CONFIG_APNPASS "apnpass"
#define CONFIG_DEVPASS "devpass"

bool checkSum(uint8_t* data, size_t size)
{
  uint8_t chk = 0xff;
  if (size != 16)
    return false;
  for (size_t t = 0; t < (size - 1); t++)
    chk ^= data[t];
  //Serial.print("(");
  //Serial.print(chk, HEX);
  //Serial.print(") ");
  return (data[size - 1] == chk);
}

void sendAck()
{
  uint8_t pkt[16];
  memset(pkt, 0, sizeof(pkt));
  pkt[0] = 0x02;
  pkt[15] = 0xff;
  for (size_t t = 0; t < (sizeof(pkt) - 1); t++)
    pkt[15] ^= pkt[t];
  udp.write(pkt, sizeof(pkt));
}

#define DRIVE_CHANNELS (4)

int driveSpeeds[DRIVE_CHANNELS] = {};

void driveCommand(const uint8_t *data, size_t size)
{
  Serial.print("drive command");
  int v[4];
  for (int t = 0; t < DRIVE_CHANNELS; t++)
  {
    v[t] = (int)((int16_t)(((uint16_t)(data[t * 2])) | (((uint16_t)(data[t * 2 + 1])) << 8)));
    Serial.print(" ");
    Serial.print(v[t]);
  }
  for (int t = 0; t < DRIVE_CHANNELS; t++)
  {
    if (driveSpeeds[t] != v[t])
    {
      pwmSet(t, v[t]);
      driveSpeeds[t] = v[t];
    }
  }
}

void setup()
{
  Serial.begin(115200);
  pwmSetup();
  delay(1000);
  preferences.begin("tc", false);
  String ssid = preferences.getString(CONFIG_APNSSID, ssidDefault);
  String pass = preferences.getString(CONFIG_APNPASS, apnPasswordDefault);
  if (ssid.isEmpty())
  {
    Serial.println("WiFi is not set up!");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println("WiFi failed!");
    return;
  }
  if (udp.listen(3456))
  {
    Serial.print("UDP Listening on IP: ");
    Serial.println(WiFi.localIP());
    udp.onPacket([](AsyncUDPPacket packet)
    {
      Serial.print("UDP Packet Type: ");
      Serial.print(packet.isBroadcast()?"Broadcast":packet.isMulticast()?"Multicast":"Unicast");
      Serial.print(", From: ");
      Serial.print(packet.remoteIP());
      Serial.print(":");
      Serial.print(packet.remotePort());
      Serial.print(", To: ");
      Serial.print(packet.localIP());
      Serial.print(":");
      Serial.print(packet.localPort());
      Serial.print(", Length: ");
      Serial.print(packet.length());
      Serial.print(", Data: ");
      uint8_t* data = packet.data();
      size_t dataSize = packet.length();
      for(size_t t = 0; t < packet.length(); t++)
      {
        Serial.print(data[t], HEX);
        Serial.print(" ");
      }
      if (!checkSum(data, dataSize))
      {
        Serial.println("Bad checksum error!");
        return;
      }
      Serial.print("checksum ok ");
      switch(data[0])
      {
        case 0x01:
          driveCommand(data + 1, 8);
          sendAck();
          break;
        default:
          Serial.print("unknown command: 0x");
          Serial.print((unsigned)data[0], HEX);
          break;
      }
      Serial.println();
    });
  }
}

enum CommandEnum
{
  CMD_HELP,
  CMD_INFO,
  CMD_REBOOT,
  CMD_SETAPNSSID,
  CMD_SETAPNPASS,
  CMD_SETDEVPASS,
};

typedef struct
{
  const char* commandName;
  CommandEnum commandEnum;
}CommandEntry;

const CommandEntry commandList[] =
{
  {"help", CMD_HELP},
  {"info", CMD_INFO},
  {"reboot", CMD_REBOOT},
  {"setapnssid ", CMD_SETAPNSSID},
  {"setapnpass ", CMD_SETAPNPASS},
  {"setdevpass ", CMD_SETDEVPASS},
};

void commandSelect(int index, const char* commandParams, size_t commandParamsSize)
{
  Serial.print("command id=");
  Serial.print(index);
  Serial.print(" params=");
  Serial.print(commandParams);
  Serial.println();
  switch (commandList[index].commandEnum)
  {
    case CMD_HELP:
      Serial.println("Available commands: info setapnssid setapnpass setdevpass");
      break;
    case CMD_INFO:
      Serial.print("apn: ssid=");
      Serial.print(preferences.getString(CONFIG_APNSSID, ssidDefault));
      Serial.print(" ip=");
      Serial.print(WiFi.localIP());
      Serial.print(" device pass is ");
      Serial.print(preferences.getString(CONFIG_DEVPASS, "").isEmpty() ? "not set" : "set");
      Serial.println();
      break;
    case CMD_REBOOT:
      ESP.restart();
      while (true)
        delay(10000);
      break;
    case CMD_SETAPNSSID:
      preferences.putString(CONFIG_APNSSID, commandParams);
      Serial.println("ok");
      break;
    case CMD_SETAPNPASS:
      preferences.putString(CONFIG_APNPASS, commandParams);
      Serial.println("ok");
      break;
    case CMD_SETDEVPASS:
      if (commandParams[0] == 0)
        preferences.remove(CONFIG_DEVPASS);
      else
        preferences.putString(CONFIG_DEVPASS, commandParams);
      Serial.println("ok");
      break;
  }
}

void commandParser(const char* cmd, size_t size)
{
  if ((size == 0) || (cmd[0] == '#'))
    return;
  Serial.print("Command:");
  Serial.print(cmd);
  Serial.println();
  for (int t = 0; t < (sizeof(commandList) / sizeof(CommandEntry)); t++)
  {
    size_t len = strlen(commandList[t].commandName);
    if (strncmp(cmd, commandList[t].commandName, len) == 0)
    {
      commandSelect(t, cmd + len, size - len);
      break;
    }
  }
}

char commandBuffer[256];
size_t commandSize = 0;

void loop()
{
  //udp.broadcast("testing...");
  int c = Serial.read();
  if (c > 0)
  {
    switch (c)
    {
      case '\r':
        break;
      case '\n':
        commandBuffer[commandSize] = 0;
        commandParser(commandBuffer, commandSize);
        commandSize = 0;
        break;
      default:
        if (commandSize < (sizeof(commandBuffer) - 1))
          commandBuffer[commandSize++] = c;
        break;
    }
  }
}
