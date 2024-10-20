#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <Adafruit_Fingerprint.h>
#include <string>
#include "arduino_secrets.h"

const std::string wifi_ssid = SECRET_SSID; // Wifi SSID
const std::string wifi_password = SECRET_WIFI_PASSWORD; //Wifi password
const std::string hostname = "fingerprint-sensor-front-door"; // hostname of the device

const std::string mqtt_server = SECRET_MQTT_SERVER; // MQTT server ip (if run on HA as addon -> HA ip)
const int mqtt_port = 1883; // default MQTT port
const std::string mqtt_user = SECRET_MQTT_USER; // MQTT username in hasio
const std::string mqtt_password = SECRET_MQTT_PASSWORD; // MQTT password
const int mqtt_interval = 5000; // MQTT rate limiting if no finger present
const unsigned int max_topic_subscription_retries = 10; // Maximum number of tries to subscribe to the required MQTT Channels. DO NOT CHANGE! (unless you really want to :shrug:)

const std::string root_topic = "fingerprint_sensor"; // root MQTT topic for all fingerprint sensors
const std::string project_topic = "front_door"; // project MQTT topic for just this one sensor. Change this if you plan to install multiple fingerprint sensors.

// "Optional meaning it would be optional if i would have coded it in for the values to be empty. if you would not like to use those values either leave them as is or change them to something funny."
const std::string name = "Fingerprint Sensor Front Door"; //Optional: Name of the device in HA
const std::string manufacturer = "TFT"; // Optional: Manufacturer displayed in HA
const std::string model = "AS608"; // Optional
const std::string hw_version = "1.0.0"; // Optional: Hardware Version of the device
const std::string sw_version = "1.0.0"; // Optional: Software Version of the device

const int sensor_tx = 12; // GPIO pin on the ESP. D6
const int sensor_rx = 14; // GPIO pin on the ESP. D5

std::string user_name; // Variable is set by MQTT callback
int user_id; // Variable is set by MQTT callback

// Static IP settings. Comment out if you want to use DHCP
IPAddress local_IP(10, 10, 1, 90);
IPAddress gateway(10, 10, 1, 1);
IPAddress subnet(255, 255, 255, 0);

// Setting up the serial connection and the sensor
SoftwareSerial mySerial(sensor_tx, sensor_rx);
Adafruit_Fingerprint sensor = Adafruit_Fingerprint(&mySerial);

WiFiClient wifiClient; // Initiate WiFi library
PubSubClient client(wifiClient); // Initiate PubSubClient library

void setup_wifi(){
  if (!WiFi.config(local_IP, gateway, subnet)) {
      Serial.println("STA Failed to configure. Will not use STA");
    }
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
  Serial.println("Connecting to WiFi...");

  while (WiFi.status() != WL_CONNECTED) {  // Wait till Wifi connected
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP()); // Print IP address
}

void setup_sensor(){
  sensor.begin(57600);
  if (sensor.verifyPassword()){
    Serial.println("Found fingerprint sensor!");
  } else {
    Serial.println("Did not find fingerprint sensor :( \n Restarting...");
    ESP.restart();
  }
}

uint8_t readnumber(){
  uint8_t num = 0;
  while (num = 0){
    while (!Serial.available());
    num = Serial.parseInt();
  }
  return num;
}

void callback(char* topic, byte* payload, unsigned int length){
  std::string message;
  for (int i = 0; i < length; i++){
    message += (char)payload[i];
  }

  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, message);
  if (error){
    return;
  }

  if (topic == (root_topic + "/" + project_topic + "/set_learning_mode").c_str()) {
    // Handle set learning mode
    Serial.println("Ready to enroll a fingerprint!");
    Serial.println("Please type in the ID # (from 1 to 127) you want to save this finger as...");
    client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Reading ID and Name");
    Serial.print("Enroll id #"); Serial.println(user_id);
    client.publish((root_topic +"/"+ project_topic +"/status").c_str(), ("Enroll ID"+ user_id));
    while(!get_fingerprint_enroll())
    return;
  }
  else if (topic == (root_topic + "/" + project_topic + "/delete_user").c_str()) {
    // Handle delete user
    return;
  }
  else if (topic == (root_topic + "/" + project_topic + "/set_user_id").c_str()) {
    // Handle set user id
    Serial.println("Setting the user_id");
    user_id = std::stoi(message);
    return;
  }
  else if (topic == (root_topic + "/" + project_topic + "/set_user_name").c_str()) {
    // Handle set user name
    Serial.println("Setting the user_name");
    user_name = message;
    return;
  }
}

uint8_t get_fingerprint_enroll(){
  int p = -1;
  Serial.print("Waiting for valid finger to enroll as #"); Serial.println(user_id);
  client.publish((root_topic +"/"+ project_topic +"/status").c_str(), ("Waiting for valid finger to enroll as #"+ user_id));
  while (p != FINGERPRINT_OK) {
    p = sensor.getImage();
    switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image taken");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Communication error");
      break;
    case FINGERPRINT_IMAGEFAIL:
      Serial.println("Imaging error");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Imaging error");
      break;
    default:
      Serial.println("Unknown error");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Unknown error");
      break;
    }
  }

  // OK success!

  p = sensor.image2Tz(1);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Communication error");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Could not find fingerprint features");
      return p;
    default:
      Serial.println("Unknown error");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Unknown error");
      return p;
  }
  
  Serial.println("Remove finger");
  client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Remove finger");
  delay(2000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = sensor.getImage();
  }
  Serial.print("ID "); Serial.println(user_id);
  p = -1;
  Serial.println("Place same finger again");
  client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Place same finger again");

  while (p != FINGERPRINT_OK) {
    p = sensor.getImage();
    switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image taken");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Communication error");
      break;
    case FINGERPRINT_IMAGEFAIL:
      Serial.println("Imaging error");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Imaging error");
      break;
    default:
      Serial.println("Unknown error");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Unknown error");
      break;
    }
  }

  // OK success!

  p = sensor.image2Tz(2);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Communication error");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Could not find fingerprint features");
      return p;
    default:
      Serial.println("Unknown error");
      client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Unknown error");
      return p;
  }
  
  // OK converted!
  Serial.print("Creating model for #");  Serial.println(user_id);
  client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Creating model");
  p = sensor.createModel();
  if (p == FINGERPRINT_OK) {
    Serial.println("Prints matched!");
    client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Prints matched for model");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Communication error");
    return p;
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
    Serial.println("Fingerprints did not match");
    client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Fingerprints did not match");
    return p;
  } else {
    Serial.println("Unknown error");
    client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Unknown error");
    return p;
  }   
  
  Serial.print("ID "); Serial.println(user_id);
  p = sensor.storeModel(int(user_id));
  if (p == FINGERPRINT_OK) {
    Serial.println("Stored!");
    client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Stored!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Communication error");
    return p;
  } else if (p == FINGERPRINT_BADLOCATION) {
    Serial.println("Could not store in that location");
    client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Could not store in that location");
    return p;
  } else if (p == FINGERPRINT_FLASHERR) {
    Serial.println("Error writing to flash");
    client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Error writing to flash");
    return p;
  } else {
    Serial.println("Unknown error");
    client.publish((root_topic +"/"+ project_topic +"/status").c_str(), "Unknown error");
    return p;
  }
  // Add a return value to satisfy the compiler
  return p; 
}

void send_HA_discovery(){
  // followed docs at https://www.home-assistant.io/integrations/mqtt/#configuration-via-mqtt-discovery
  const std::string status_payload = "{\"device_class\":\"sensor\",\"state_topic\":\"" + root_topic + "/" + project_topic + "/status\",\"value_template\":\"{{ value_json.status }}\",\"unique_id\":\"" + root_topic + "_" + project_topic + "_status\",\"name\":\"Fingerprint Sensor Status " + project_topic + "\",\"device\":{\"identifiers\":[\"" + root_topic + "_" + project_topic + "_001\"],\"name\":\"" + name + "\",\"manufacturer\":\"" + manufacturer + "\",\"model\":\"" + model + "\",\"hw_version\":\"" + hw_version + "\",\"sw_version\":\"" + sw_version + "\"}}";
// {
//   "device_class": "sensor",
//   "state_topic": "root_topic/project_topic/status",
//   "value_template": "{{ value_json.status }}",
//   "unique_id": "root_topic_project_topic_status",
//   "name": "Fingerprint Sensor Status project_topic",
//   "device": {
//     "identifiers": ["root_topic_project_topic_001"],
//     "name": "name",
//     "manufacturer": "manufacturer",
//     "model": "model",
//     "hw_version": "hw_version",
//     "sw_version": "sw_version"
//   }
// }
  client.publish(("homeassistant/sensor/" + root_topic + "/" + project_topic + "_status/config").c_str(), status_payload.c_str(), true);

  const std::string match_state_payload = "{\"device_class\":\"sensor\",\"state_topic\":\"" + root_topic + "/" + project_topic + "/match_state\",\"value_template\":\"{{ value_json.match_state }}\",\"unique_id\":\"" + root_topic + "_" + project_topic + "_match_state\",\"name\":\"Fingerprint Match State " + project_topic + "\",\"device\":{\"identifiers\":[\"" + root_topic + "_" + project_topic + "_001\"]}}";
// {
//   "device_class": "sensor",
//   "state_topic": "root_topic/project_topic/match_state",
//   "value_template": "{{ value_json.match_state }}",
//   "unique_id": "root_topic_project_topic_match_state",
//   "name": "Fingerprint Match State project_topic",
//   "device": {
//     "identifiers": ["root_topic_project_topic_001"]
//   }
// }
  client.publish(("homeassistant/sensor/" + root_topic + "/" + project_topic + "_match_state/config").c_str(), match_state_payload.c_str(), true);

  const std::string last_match_payload = "{\"device_class\": \"sensor\",\"state_topic\": \"" + root_topic + "/" + project_topic + "/last_match\",\"value_template\": \"{{ value_json.last_match }}\",\"unique_id\": \"" + root_topic + "_" + project_topic + "_last_match\",\"name\": \"Last Matched Person " + project_topic + "\",\"device\": {\"identifiers\": [\"" + root_topic + "_" + project_topic + "_001\"]}}";
// {
//   "device_class": "sensor",
//   "state_topic": "root_topic/project_topic/last_match",
//   "value_template": "{{ value_json.last_match }}",
//   "unique_id": "root_topic_project_topic_last_match",
//   "name": "Last Matched Person project_topic",
//   "device": {
//     "identifiers": ["root_topic_project_topic_001"]
//   }
// }
  client.publish(("homeassistant/sensor/" + root_topic + "/" + project_topic + "_last_match/config").c_str(), last_match_payload.c_str(), true);

  const std::string set_learning_mode_payload = "{\"command_topic\":\"" + root_topic + "/" + project_topic + "/set_learning_mode\",\"payload_press\":\"ON\",\"unique_id\":\"" + root_topic + "_" + project_topic + "_set_learning_mode\",\"name\":\"Set Learning Mode " + project_topic + "\",\"device\":{\"identifiers\":[\"" + root_topic + "_" + project_topic + "_001\"]}}";
// {
//   "command_topic": "root_topic/project_topic/set_learning_mode",
//   "payload_press": "ON",
//   "unique_id": "root_topic_project_topic_set_learning_mode",
//   "name": "Set Learning Mode project_topic",
//   "device": {
//     "identifiers": ["root_topic_project_topic_001"]
//   }
// }
  client.publish(("homeassistant/button/" + root_topic + "/" + project_topic + "_set_learning_mode/config").c_str(), set_learning_mode_payload.c_str(), true);

  const std::string delete_user_payload = "{\"command_topic\":\"" + root_topic + "/" + project_topic + "/delete_user\",\"payload_press\":\"ON\",\"unique_id\":\"" + root_topic + "_" + project_topic + "_delete_user\",\"name\":\"Delete User " + project_topic + "\",\"device\":{\"identifiers\":[\"" + root_topic + "_" + project_topic + "_001\"]}}";
// {
//   "command_topic": "root_topic/project_topic/delete_user",
//   "payload_press": "ON",
//   "unique_id": "root_topic_project_topic_delete_user",
//   "name": "Delete User project_topic",
//   "device": {
//     "identifiers": ["root_topic_project_topic_001"]
//   }
// }
  client.publish(("homeassistant/button/" + root_topic + "/" + project_topic + "_delete_user/config").c_str(), delete_user_payload.c_str(), true);

  const std::string set_user_id_payload = "{\"command_topic\":\"" + root_topic + "/" + project_topic + "/set_user_id\",\"value_template\":\"{{ value_json.user_id }}\",\"unique_id\":\"" + root_topic + "_" + project_topic + "_set_user_id\",\"name\":\"User ID Input " + project_topic + "\",\"device\":{\"identifiers\":[\"" + root_topic + "_" + project_topic + "_001\"]}}";
// {
//   "command_topic": "root_topic/project_topic/set_user_id",
//   "value_template": "{{ value_json.user_id }}",
//   "unique_id": "root_topic_project_topic_set_user_id",
//   "name": "User ID Input project_topic",
//   "device": {
//     "identifiers": ["root_topic_project_topic_001"]
//   }
// }
  client.publish(("homeassistant/text/" + root_topic + "/" + project_topic + "_set_user_id/config").c_str(), set_user_id_payload.c_str(), true);

  std::string set_user_name_payload = "{\"command_topic\":\"" + root_topic + "/" + project_topic + "/set_user_name\",\"value_template\":\"{{ value_json.user_name }}\",\"unique_id\":\"" + root_topic + "_" + project_topic + "_set_user_name\",\"name\":\"User Name Input " + project_topic + "\",\"device\":{\"identifiers\":[\"" + root_topic + "_" + project_topic + "_001\"]}}";
// {
//   "command_topic": "root_topic/project_topic/set_user_name",
//   "value_template": "{{ value_json.user_name }}",
//   "unique_id": "root_topic_project_topic_set_user_name",
//   "name": "User Name Input project_topic",
//   "device": {
//     "identifiers": ["root_topic_project_topic_001"]
//   }
// }
  client.publish(("homeassistant/text/" + root_topic + "/" + project_topic + "_set_user_name/config").c_str(), set_user_name_payload.c_str(), true);

}

void setup_mqtt() {
  Serial.println("Connecting to MQTT...");
  client.setServer(mqtt_server.c_str(), mqtt_port);
  client.setCallback(callback);

  client.connect(hostname.c_str(), mqtt_user.c_str(), mqtt_password.c_str());
  while (!client.connected()) {
    Serial.println("MQTT connect failed, retrying...");
    delay(100);
    client.connect(hostname.c_str(), mqtt_user.c_str(), mqtt_password.c_str());
  }
  Serial.println("Sending HA discovery...");
  send_HA_discovery(); // send MQTT payload for HA MQTT discovery

  Serial.println("MQTT set up!");
}

void setup() {
  Serial.begin(9600);
  Serial.println();
  sensor.begin(57600);
  setup_wifi();
  setup_mqtt();

  std::string topic = root_topic +"/"+project_topic;
  unsigned int retries = 0;
  while (!client.subscribe(topic.c_str())){
    if (retries < max_topic_subscription_retries){
      Serial.println(("Failed to subscribe to "+ topic +". Retrying in 3s").c_str());
      Serial.println(10-retries + "remaining");
      delay(3000);
    } else {
      Serial.println("Max retries reached. Please check your Config. Restarting...");
      ESP.restart();
    }
  }
  Serial.println(("Successfully subscribed to "+ topic).c_str());

}

void loop() {
  if (WiFi.status() != WL_CONNECTED){
    Serial.println("Wifi disconnected. Restarting...");
    ESP.restart();
  }
  if (!client.connected()){
    Serial.println("MQTT client disconnected. Restarting...");
    ESP.restart();
  }
  client.loop();

}
