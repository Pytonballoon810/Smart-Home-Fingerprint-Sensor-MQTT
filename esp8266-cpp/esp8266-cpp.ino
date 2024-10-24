#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <Adafruit_Fingerprint.h>
#include <string>
#include <map>
#include "arduino_secrets.h"

const std::string wifi_ssid = SECRET_SSID;                     // Wifi SSID
const std::string wifi_password = SECRET_WIFI_PASSWORD;        //Wifi password
const std::string hostname = "fingerprint-sensor-front-door";  // hostname of the device

const std::string mqtt_server = SECRET_MQTT_SERVER;      // MQTT server ip (if run on HA as addon -> HA ip)
const int mqtt_port = 1883;                              // default MQTT port
const std::string mqtt_user = SECRET_MQTT_USER;          // MQTT username in hasio
const std::string mqtt_password = SECRET_MQTT_PASSWORD;  // MQTT password
const int mqtt_interval = 5000;                          // MQTT rate limiting if no finger present
const unsigned int max_topic_subscription_retries = 10;  // Maximum number of tries to subscribe to the required MQTT Channels. DO NOT CHANGE! (unless you really want to :shrug:)
uint16_t mqtt_buffer_size = 512;

const std::string root_topic = "fingerprint_sensor";  // root MQTT topic for all fingerprint sensors
const std::string project_topic = "front_door";       // project MQTT topic for just this one sensor. Change this if you plan to install multiple fingerprint sensors.

// "Optional meaning it would be optional if i would have coded it in for the values to be empty. if you would not like to use those values either leave them as is or change them to something funny."
const int max_templates = 162;                             // Max amount of templates. Found in the datasheet of your sensor
const std::string name = "Fingerprint Sensor Front Door";  //Optional: Name of the device in HA
const std::string manufacturer = "TFT";                    // Optional: Manufacturer displayed in HA
const std::string model = "AS608";                         // Optional
const std::string hw_version = "1.0.0";                    // Optional: Hardware Version of the device
const std::string sw_version = "1.0.0";                    // Optional: Software Version of the device

const int sensor_tx = 12;  // GPIO pin on the ESP. D6
const int sensor_rx = 14;  // GPIO pin on the ESP. D5

const bool clear_database_on_reflash = false;  // Wether or not to clear the database when flashing the image

std::string user_name;  // Variable is set by MQTT callback
int user_id = -1;       // Variable is set by MQTT callback

std::string mode = "Reading";  // Client mode variable to keep track of what to do
std::string status;
std::string match_state = "Waiting...";
std::string last_match;
std::string template_database;
int template_count = 0;
volatile int finger_status = -1;
std::map<int, std::string> id_to_finger;

// Static IP settings. Comment out if you want to use DHCP
IPAddress local_IP(10, 10, 1, 90);
IPAddress gateway(10, 10, 1, 1);
IPAddress subnet(255, 255, 255, 0);

// Setting up the serial connection and the sensor
SoftwareSerial mySerial(sensor_tx, sensor_rx);
Adafruit_Fingerprint sensor = Adafruit_Fingerprint(&mySerial);

WiFiClient wifiClient;            // Initiate WiFi library
PubSubClient client(wifiClient);  // Initiate PubSubClient library

// Function to convert the map to a YAML formatted string
std::string mapToYAML(const std::map<int, std::string>& numberToString) {
  std::string yamlStr;

  for (const auto& pair : numberToString) {
    yamlStr += std::to_string(pair.first) + ": \"" + pair.second + "\"\n";
  }

  return yamlStr;
}

void setup_wifi() {
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

  // Local IP address
  Serial.print("Local IP Address: ");
  Serial.println(WiFi.localIP());

  // Subnet mask
  Serial.print("Subnet Mask: ");
  Serial.println(WiFi.subnetMask());

  // Gateway IP address
  Serial.print("Gateway IP: ");
  Serial.println(WiFi.gatewayIP());

  // DNS IP address
  Serial.print("DNS IP: ");
  Serial.println(WiFi.dnsIP());

  // MAC address of the ESP8266
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Signal strength (RSSI)
  Serial.print("Signal Strength (RSSI): ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");

  // WiFi Status
  Serial.print("Connection Status: ");
  switch (WiFi.status()) {
    case WL_CONNECTED:
      Serial.println("Connected");
      break;
    case WL_NO_SSID_AVAIL:
      Serial.println("SSID not available");
      break;
    case WL_CONNECT_FAILED:
      Serial.println("Connection failed");
      break;
    case WL_DISCONNECTED:
      Serial.println("Disconnected");
      break;
    default:
      Serial.println("Unknown status");
      break;
  }
}

void connect_mqtt() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    std::string topic = root_topic + "/" + project_topic + "/#";  // Multilevel wildcard for all project topics
    if (client.connect(hostname.c_str(), mqtt_user.c_str(), mqtt_password.c_str())) {
      Serial.println("connected");
      client.subscribe(topic.c_str());
      Serial.println(("Successfully subscribed to " + topic).c_str());
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" will try again in 5 seconds");
      delay(5000);
    }
  }
}

void publish_mqtt(std::string sub_topic, std::string payload, bool retain = false) {
  std::string topic = (root_topic + "/" + project_topic + sub_topic);

  int out;
  std::string error;
  if (sub_topic.compare("/status") == 0 && status.compare(payload) != 0) {
    out = client.publish(topic.c_str(), payload.c_str(), retain);
    if (out != 1) {
      error = "Had trouble publishing to /status.";
    }
  } else if (sub_topic.compare("/match_state") == 0 && match_state.compare(payload) != 0) {
    out = client.publish(topic.c_str(), payload.c_str(), retain);
    if (out != 1) {
      error = "Had trouble publishing to /match_state.";
    }
  } else if (sub_topic.compare("/template_count") == 0 && std::to_string(template_count).compare(payload) != 0) {
    out = client.publish(topic.c_str(), payload.c_str(), retain);
    if (out != 1) {
      error = "Had trouble publishing to /template_count.";
    }
  } else if (sub_topic.compare("/last_match") == 0 && last_match.compare(payload) != 0) {
    out = client.publish(topic.c_str(), payload.c_str(), retain);
    if (out != 1) {
      error = "Had trouble publishing to /last_match.";
    }
  } else {
    out = client.publish(topic.c_str(), payload.c_str(), retain);
    if (out != 1) {
      error = "Had trouble publishing to " + topic + ".";
    }
  }
  Serial.println(error.c_str());
}

void send_HA_discovery() {
  // followed docs at https://www.home-assistant.io/integrations/mqtt/#configuration-via-mqtt-discovery
  const std::string status_payload = "{\"state_topic\":\"" + root_topic + "/" + project_topic + "/status\",\"value_template\":\"{{ value }}\",\"unique_id\":\"" + root_topic + "_" + project_topic + "_status\",\"name\":\"Fingerprint Sensor Status " + project_topic + "\",\"device\":{\"identifiers\":[\"" + root_topic + "_" + project_topic + "_001\"],\"name\":\"" + name + "\",\"manufacturer\":\"" + manufacturer + "\",\"model\":\"" + model + "\",\"hw_version\":\"" + hw_version + "\",\"sw_version\":\"" + sw_version + "\"}}";
  // {
  //   "state_topic": "root_topic/project_topic/status",
  //   "value_template": "{{ value }}",
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
  Serial.println("Sending Payload:");
  Serial.println(status_payload.c_str());
  bool status_payload_out = client.publish(("homeassistant/sensor/" + root_topic + "/" + project_topic + "_status/config").c_str(), status_payload.c_str(), true);
  Serial.print("Code: ");
  Serial.println(status_payload_out);

  const std::string match_state_payload = "{\"state_topic\":\"" + root_topic + "/" + project_topic + "/match_state\",\"value_template\":\"{{ value }}\",\"unique_id\":\"" + root_topic + "_" + project_topic + "_match_state\",\"name\":\"Fingerprint Match State " + project_topic + "\",\"device\":{\"identifiers\":[\"" + root_topic + "_" + project_topic + "_001\"]}}";
  // {
  //   "state_topic": "root_topic/project_topic/match_state",
  //   "value_template": "{{ value }}",
  //   "unique_id": "root_topic_project_topic_match_state",
  //   "name": "Fingerprint Match State project_topic",
  //   "device": {
  //     "identifiers": ["root_topic_project_topic_001"]
  //   }
  // }
  Serial.println("Sending Payload:");
  Serial.println(match_state_payload.c_str());
  bool match_state_payload_out = client.publish(("homeassistant/sensor/" + root_topic + "/" + project_topic + "_match_state/config").c_str(), match_state_payload.c_str(), true);
  Serial.print("Code: ");
  Serial.println(match_state_payload_out);

  const std::string last_match_payload = "{\"state_topic\": \"" + root_topic + "/" + project_topic + "/last_match\",\"value_template\": \"{{ value }}\",\"unique_id\": \"" + root_topic + "_" + project_topic + "_last_match\",\"name\": \"Last Matched Person " + project_topic + "\",\"device\": {\"identifiers\": [\"" + root_topic + "_" + project_topic + "_001\"]}}";
  // {
  //   "state_topic": "root_topic/project_topic/last_match",
  //   "value_template": "{{ value }}",
  //   "unique_id": "root_topic_project_topic_last_match",
  //   "name": "Last Matched Person project_topic",
  //   "device": {
  //     "identifiers": ["root_topic_project_topic_001"]
  //   }
  // }
  Serial.println("Sending Payload:");
  Serial.println(last_match_payload.c_str());
  bool last_match_payload_out = client.publish(("homeassistant/sensor/" + root_topic + "/" + project_topic + "_last_match/config").c_str(), last_match_payload.c_str(), true);
  Serial.print("Code: ");
  Serial.println(last_match_payload_out);

  const std::string template_count_payload = "{\"state_topic\":\"" + root_topic + "/" + project_topic + "/template_count\",\"value_template\":\"{{ value }}\",\"unique_id\":\"" + root_topic + "_" + project_topic + "_template_count\",\"name\":\"Fingerprint Template Count " + project_topic + "\",\"device\":{\"identifiers\":[\"" + root_topic + "_" + project_topic + "_001\"]}}";
  // {
  //   "state_topic": "root_topic/project_topic/template_count",
  //   "value_template": "{{ value }}",
  //   "unique_id": "root_topic_project_topic_template_count",
  //   "name": "Fingerprint Template Count project_topic",
  //   "device": {
  //     "identifiers": ["root_topic_project_topic_001"]
  //   }
  // }
  Serial.println("Sending Payload:");
  Serial.println(template_count_payload.c_str());
  bool template_count_payload_out = client.publish(("homeassistant/sensor/" + root_topic + "/" + project_topic + "_template_count/config").c_str(), template_count_payload.c_str(), true);
  Serial.print("Code: ");
  Serial.println(template_count_payload_out);

  const std::string template_database = "{\"state_topic\":\"" + root_topic + "/" + project_topic + "/template_database\",\"value_template\":\"{{ value }}\",\"unique_id\":\"" + root_topic + "_" + project_topic + "_template_database\",\"name\":\"Fingerprint Template database " + project_topic + "\",\"device\":{\"identifiers\":[\"" + root_topic + "_" + project_topic + "_001\"]}}";
  // {
  //   "state_topic": "root_topic/project_topic/template_database",
  //   "value_template": "{{ value }}",
  //   "unique_id": "root_topic_project_topic_template_database",
  //   "name": "Fingerprint Template database project_topic",
  //   "device": {
  //     "identifiers": ["root_topic_project_topic_001"]
  //   }
  // }
  Serial.println("Sending Payload:");
  Serial.println(template_database.c_str());
  bool template_database_out = client.publish(("homeassistant/sensor/" + root_topic + "/" + project_topic + "_template_database/config").c_str(), template_database.c_str(), true);
  Serial.print("Code: ");
  Serial.println(template_database_out);

  const std::string set_learning_mode_payload = "{\"command_topic\":\"" + root_topic + "/" + project_topic + "/set_learning_mode\",\"payload_press\":\"PRESS\",\"unique_id\":\"" + root_topic + "_" + project_topic + "_set_learning_mode\",\"name\":\"Set Learning Mode " + project_topic + "\",\"device\":{\"identifiers\":[\"" + root_topic + "_" + project_topic + "_001\"]}}";
  // {
  //   "command_topic": "root_topic/project_topic/set_learning_mode",
  //   "payload_press": "PRESS",
  //   "unique_id": "root_topic_project_topic_set_learning_mode",
  //   "name": "Set Learning Mode project_topic",
  //   "device": {
  //     "identifiers": ["root_topic_project_topic_001"]
  //   }
  // }
  Serial.println("Sending Payload:");
  Serial.println(set_learning_mode_payload.c_str());
  bool set_learning_mode_payload_out = client.publish(("homeassistant/button/" + root_topic + "/" + project_topic + "_set_learning_mode/config").c_str(), set_learning_mode_payload.c_str(), true);
  Serial.print("Code: ");
  Serial.println(set_learning_mode_payload_out);

  const std::string delete_user_payload = "{\"command_topic\":\"" + root_topic + "/" + project_topic + "/delete_user\",\"payload_press\":\"PRESS\",\"unique_id\":\"" + root_topic + "_" + project_topic + "_delete_user\",\"name\":\"Delete User " + project_topic + "\",\"device\":{\"identifiers\":[\"" + root_topic + "_" + project_topic + "_001\"]}}";
  // {
  //   "command_topic": "root_topic/project_topic/delete_user",
  //   "payload_press": "PRESS",
  //   "unique_id": "root_topic_project_topic_delete_user",
  //   "name": "Delete User project_topic",
  //   "device": {
  //     "identifiers": ["root_topic_project_topic_001"]
  //   }
  // }
  Serial.println("Sending Payload:");
  Serial.println(delete_user_payload.c_str());
  bool delete_user_payload_out = client.publish(("homeassistant/button/" + root_topic + "/" + project_topic + "_delete_user/config").c_str(), delete_user_payload.c_str(), true);
  Serial.print("Code: ");
  Serial.println(delete_user_payload_out);

  const std::string set_user_id_payload = "{\"command_topic\":\"" + root_topic + "/" + project_topic + "/set_user_id\",\"value_template\":\"{{ value }}\",\"unique_id\":\"" + root_topic + "_" + project_topic + "_set_user_id\",\"name\":\"User ID Input " + project_topic + "\",\"device\":{\"identifiers\":[\"" + root_topic + "_" + project_topic + "_001\"]}, \"pattern\": " + R"("[1-9]|[1-9][0-9]|1[0-6][0-2]")" + "}";
  // {
  //   "command_topic": "root_topic/project_topic/set_user_id",
  //   "value_template": "{{ value }}",
  //   "unique_id": "root_topic_project_topic_set_user_id",
  //   "name": "User ID Input project_topic",
  //   "device": {
  //     "identifiers": ["root_topic_project_topic_001"]
  //   },
  //   "pattern": "\b([1-9]|[1-9][0-9]|1[0-5][0-9]|16[0-2])\b"
  // Numbers from 1 to 9
  // Numbers from 10 to 99
  // Numbers from 100 to 162
  // }
  Serial.println("Sending Payload:");
  Serial.println(set_user_id_payload.c_str());
  bool set_user_id_payload_out = client.publish(("homeassistant/text/" + root_topic + "/" + project_topic + "_set_user_id/config").c_str(), set_user_id_payload.c_str(), true);
  Serial.print("Code: ");
  Serial.println(set_user_id_payload_out);

  std::string set_user_name_payload = "{\"command_topic\":\"" + root_topic + "/" + project_topic + "/set_user_name\",\"value_template\":\"{{ value }}\",\"unique_id\":\"" + root_topic + "_" + project_topic + "_set_user_name\",\"name\":\"User Name Input " + project_topic + "\",\"device\":{\"identifiers\":[\"" + root_topic + "_" + project_topic + "_001\"]}, \"pattern\": " + R"("(?=(?:[^ ]* ?[^ ]* ?[^ ]* ?[^ ]*$))(?=.{1,50}$)[A-Za-z ]{1,50}")" + "}";
  // {
  //   "command_topic": "root_topic/project_topic/set_user_name",
  //   "value_template": "{{ value }}",
  //   "unique_id": "root_topic_project_topic_set_user_name",
  //   "name": "User Name Input project_topic",
  //   "device": {
  //     "identifiers": ["root_topic_project_topic_001"]
  //   },
  //   "pattern": "^(?=(?:[^ ]* ?[^ ]* ?[^ ]* ?[^ ]*$))(?=.{1,50}$)[A-Za-z ]{1,50}$"
  // Contains only lowercase letters (a-z), uppercase letters (A-Z), and spaces.
  // At most 50 characters long.
  // Contains at most 3 spaces.
  // }
  Serial.println("Sending Payload:");
  Serial.println(set_user_name_payload.c_str());
  bool set_user_name_payload_out = client.publish(("homeassistant/text/" + root_topic + "/" + project_topic + "_set_user_name/config").c_str(), set_user_name_payload.c_str(), true);
  Serial.print("Code: ");
  Serial.println(set_user_name_payload_out);
}

void setup_mqtt() {
  Serial.println("Connecting to MQTT...");
  client.setServer(mqtt_server.c_str(), mqtt_port);
  if (client.setBufferSize(mqtt_buffer_size) != true) {
    Serial.println("Failed to resize MQTT message buffer size. Restarting...");
    ESP.restart();
  }
  client.setCallback(callback);

  connect_mqtt();

  Serial.println("Sending HA discovery...");
  send_HA_discovery();  // send MQTT payload for HA MQTT discovery

  Serial.println("MQTT set up!");
}


void set_template_count() {
  sensor.getTemplateCount();
  template_count = sensor.templateCount;
  client.publish((root_topic + "/" + project_topic + "/template_count").c_str(), (std::to_string(template_count)).c_str(), true);
}

void setup_sensor() {
  sensor.begin(57600);
  if (sensor.verifyPassword()) {
    client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Found fingerprint sensor!");
    // Serial.println("Found fingerprint sensor!");
    set_template_count();
  } else {
    client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Did not find fingerprint sensor :( Restarting...");
    // Serial.println("Did not find fingerprint sensor :( \n Restarting...");
    ESP.restart();
  }
}

uint8_t readnumber() {
  uint8_t num = 0;
  while (num = 0) {
    while (!Serial.available())
      ;
    num = Serial.parseInt();
  }
  return num;
}

void callback(char* topic, byte* payload, unsigned int length) {
  std::string message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  std::string topic_str(topic);

  Serial.print("Topic: ");
  Serial.println(topic_str.c_str());
  Serial.print("Message: ");
  Serial.println(message.c_str());

  const std::string learning_topic = root_topic + "/" + project_topic + "/set_learning_mode";
  const std::string delete_topic = root_topic + "/" + project_topic + "/delete_user";
  const std::string match_state_topic = root_topic + "/" + project_topic + "/match_state";
  const std::string status_topic = root_topic + "/" + project_topic + "/status";
  const std::string set_user_id_topic = root_topic + "/" + project_topic + "/set_user_id";
  const std::string set_user_name_topic = root_topic + "/" + project_topic + "/set_user_name";
  const std::string template_count_topic = root_topic + "/" + project_topic + "/template_count";

  if (topic_str.compare(learning_topic) == 0) {
    // Handle set learning mode
    mode = "Learning";

    // Serial.print("Enroll id #"); Serial.println(user_id);
    client.publish((root_topic + "/" + project_topic + "/status").c_str(), ("Enroll ID" + user_id));
    while (!get_fingerprint_enroll())
      client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Reading");
    return;
  } else if (topic_str.compare(delete_topic) == 0) {
    sensor.deleteModel(user_id);
    publish_mqtt("/status", "Deleted user with ID " + user_id);
  } else if (topic_str.compare(match_state_topic) == 0) {
    match_state = message;  // Keep local track of the sensor states also published in MQTT
  } else if (topic_str.compare(status_topic) == 0) {
    status = message;  // -"-
  } else if (topic_str.compare(set_user_id_topic) == 0) {
    // Handle set user id
    if (user_id <= 0 && user_id > max_templates) {  // Test for valid user ID, limited by sensor used. Should be unnecessary as we use a regex to check the input in HA directly but some request make it through anyways
      client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Invalid User ID specified. Please specify a ID > 0");
    }
    user_id = std::stoi(message);
  } else if (topic_str.compare(set_user_name_topic) == 0) {
    // Handle set user name
    user_name = message;
  } else if (topic_str.compare(template_count_topic) == 0) {
    // Handle set user name
    template_database = mapToYAML(id_to_finger);
    Serial.println(template_database.c_str());
    publish_mqtt("/template_database", template_database.c_str());
  }
}

uint8_t get_fingerprint_enroll() {
  mode = "Reading";

  // Should be unnecessary as we use a regex to check the input in HA directly but some request make it through anyways
  if ((user_id <= 0) || (user_id > max_templates)) {  // Test for valid user ID, limited by sensor used
    return 1;
  }

  int p = -1;
  // Serial.print("Waiting for valid finger to enroll as #"); Serial.println(user_id);
  while (p != FINGERPRINT_OK) {
    p = sensor.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Image taken");
        // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Image taken");
        break;
      case FINGERPRINT_NOFINGER:
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Communication error");
        break;
      case FINGERPRINT_IMAGEFAIL:
        Serial.println("Imaging error");
        // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Imaging error");
        break;
      default:
        Serial.println("Unknown error");
        // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Unknown error");
        break;
    }
  }

  // OK success!

  p = sensor.image2Tz(1);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Communication error");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Could not find fingerprint features");
      return p;
    default:
      Serial.println("Unknown error");
      // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Unknown error");
      return p;
  }

  Serial.println("Remove finger");
  client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Remove finger");
  delay(700);
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = sensor.getImage();
  }
  Serial.print("ID ");
  Serial.println(user_id);
  p = -1;
  Serial.println("Place same finger again");
  client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Place same finger again");

  while (p != FINGERPRINT_OK) {
    p = sensor.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Image taken");
        // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Image taken");
        break;
      case FINGERPRINT_NOFINGER:
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Communication error");
        break;
      case FINGERPRINT_IMAGEFAIL:
        Serial.println("Imaging error");
        // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Imaging error");
        break;
      default:
        Serial.println("Unknown error");
        // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Unknown error");
        break;
    }
  }

  // OK success!

  p = sensor.image2Tz(2);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Communication error");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Could not find fingerprint features");
      return p;
    default:
      Serial.println("Unknown error");
      // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Unknown error");
      return p;
  }

  // OK converted!
  Serial.print("Creating model for #");
  Serial.println(user_id);
  // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Creating model");
  p = sensor.createModel();
  if (p == FINGERPRINT_OK) {
    Serial.println("Prints matched!");
    // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Prints matched for model");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Communication error");
    return p;
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
    Serial.println("Fingerprints did not match");
    // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Fingerprints did not match");
    return p;
  } else {
    Serial.println("Unknown error");
    // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Unknown error");
    return p;
  }

  Serial.print("Finally storing model with ID ");
  Serial.println(user_id);
  p = sensor.storeModel(user_id);
  if (p == FINGERPRINT_OK) {
    id_to_finger[user_id] = user_name;
    Serial.println("Stored!");
    client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Stored!");
    delay(25);
  } else {
    client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Template could not be stored!");
    if (p == FINGERPRINT_PACKETRECIEVEERR) {
      Serial.println("Communication error");
      // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Communication error");
      return p;
    } else if (p == FINGERPRINT_BADLOCATION) {
      Serial.println("Could not store in that location");
      // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Could not store in that location");
      return p;
    } else if (p == FINGERPRINT_FLASHERR) {
      Serial.println("Error writing to flash");
      // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Error writing to flash");
      return p;
    } else {
      Serial.println("Unknown error");
      // client.publish((root_topic + "/" + project_topic + "/status").c_str(), "Unknown error");
      return p;
    }
  }
  // Add a return value to satisfy the compiler
  return 1;
}

int get_fingerprint_read() {
  if (match_state.compare("Waiting...") != 0) {
    Serial.println(("Updating match_state. Value was:" + match_state).c_str());
    client.publish((root_topic + "/" + project_topic + "/match_state").c_str(), "Waiting...");
  }
  uint8_t p = sensor.getImage();
  if (p != 2) {
    Serial.println(p);
  }
  if (p != FINGERPRINT_OK) return -1;

  p = sensor.image2Tz();
  if (p != 2) {
    Serial.println(p);
  }
  if (p != FINGERPRINT_OK) return -1;

  p = sensor.fingerFastSearch();
  if (p != FINGERPRINT_OK) return -2;

  // found a match!
  Serial.print("Found ID #");
  Serial.print(sensor.fingerID);
  Serial.print(" with confidence of ");
  Serial.println(sensor.confidence);
  if (sensor.confidence >= 70) {
    publish_mqtt("/match_state", "Matched!");
    publish_mqtt("/last_match", id_to_finger[sensor.fingerID].c_str());
    last_match = id_to_finger[sensor.fingerID];
  } else publish_mqtt("/match_state", "Missmatch!");

  return sensor.fingerID;
}

void delete_all_templates() {
  if (sensor.emptyDatabase()) {
    Serial.println("Cleared the database!");
  } else Serial.println("Failed to clear database!");
}

void setup() {
  Serial.begin(9600);
  Serial.println();

  setup_wifi();

  setup_mqtt();

  setup_sensor();

  if (clear_database_on_reflash) delete_all_templates();  // Mainly for debuggning purposses. Clears the database everytime a new image gets flashed

  for (const auto& pair : id_to_finger) {
    Serial.print("ID: ");
    Serial.print(pair.first);
    Serial.print(", Name: ");
    Serial.println(pair.second.c_str());
  }

  template_database = mapToYAML(id_to_finger);
  publish_mqtt("/template_database", template_database.c_str());
  Serial.println("Database:");
  Serial.println(template_database.c_str());

  publish_mqtt("/match_state", "Waiting...", true);  // Set default value for match state
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wifi disconnected. Restarting...");
    ESP.restart();
  }
  if (!client.connected()) {
    Serial.println("MQTT client disconnected. Reconnecting...");
    connect_mqtt();
  }

  if (mode.compare("Reading") == 0) {
    finger_status = get_fingerprint_read();
    if (finger_status != -1 and finger_status != -2) {
      Serial.print("Match");
    } else {
      if (finger_status == -2) {
        for (int ii = 0; ii < 5; ii++) {
          Serial.print("Not Match");
        }
        client.publish((root_topic + "/" + project_topic + "/match_state").c_str(), "Missmatch!");
      }
    }
  }

  if (sensor.templateCount != template_count) {
    set_template_count();
  }

  Serial.print(".");
  client.loop();
  delay(100);  // DO NOT RUN AT FULL SPEED!
}
