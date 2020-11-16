/* 
   web_server.ino

   Example code for serving a web page over a WiFi network, displaying
   environment data read from the Metriful MS430.
   
   This example is designed for the following WiFi enabled hosts:
   * Arduino Nano 33 IoT
   * Arduino MKR WiFi 1010
   * ESP8266 boards (e.g. Wemos D1, NodeMCU)
   * ESP32 boards (e.g. DOIT DevKit v1)

   All environment data values are measured and displayed on a text 
   web page generated by the host, which acts as a simple web server. 
   
   The host can either connect to an existing WiFi network, or generate 
   its own for other devices to connect to (Access Point mode).

   Copyright 2020 Metriful Ltd. 
   Licensed under the MIT License - for further details see LICENSE.txt

   For code examples, datasheet and user guide, visit 
   https://github.com/metriful/sensor
*/

#include <Metriful_sensor.h>
#include <WiFi_functions.h>

//////////////////////////////////////////////////////////
// USER-EDITABLE SETTINGS

// Choose how often to read and update data (every 3, 100, or 300 seconds)
// The web page can be refreshed more often but the data will not change
uint8_t cycle_period = CYCLE_PERIOD_3_S;

// Choose whether to create a new WiFi network (host as Access Point),
// or connect to an existing WiFi network.
bool createWifiNetwork = false;
// If creating a WiFi network, a static (fixed) IP address ("theIP") is 
// specified by the user.  Otherwise, if connecting to an existing 
// network, an IP address is automatically allocated and the serial 
// output must be viewed at startup to see this allocated IP address.

// Provide the SSID (name) and password for the WiFi network. Depending
// on the choice of createWifiNetwork, this is either created by the 
// host (Access Point mode) or already exists.
// To avoid problems, do not create a network with the same SSID name
// as an already existing network.
char SSID[] = "PUT WIFI NETWORK NAME HERE IN QUOTES"; // network SSID (name)
char password[] = "PUT WIFI PASSWORD HERE IN QUOTES"; // network password; must be at least 8 characters

// Choose a static IP address for the host, only used when generating 
// a new WiFi network (createWifiNetwork = true). The served web 
// page will be available at  http://<IP address here>
IPAddress theIP(192, 168, 12, 20); 
// e.g. theIP(192, 168, 12, 20) means an IP of 192.168.12.20
//      and the web page will be at http://192.168.12.20

// END OF USER-EDITABLE SETTINGS
//////////////////////////////////////////////////////////

#if !defined(HAS_WIFI)
#error ("This example program has been created for specific WiFi enabled hosts only.")
#endif

WiFiServer server(80);
uint16_t refreshPeriodSeconds;

// Structs for data
AirData_t airData = {0};
AirQualityData_t airQualityData = {0};
LightData_t lightData = {0}; 
ParticleData_t particleData = {0};
SoundData_t soundData = {0};

// Storage for the web page text
char lineBuffer[100] = {0};
char pageBuffer[2300] = {0};

void setup() {
  // Initialize the host's pins, set up the serial port and reset:
  SensorHardwareSetup(I2C_ADDRESS); 
  
  if (createWifiNetwork) {
    // The host generates its own WiFi network ("Access Point") with
    // a chosen static IP address
    if (!createWiFiAP(SSID, password, theIP)) {
      Serial.println("Failed to create access point.");
      while (true) {
        yield();
      }
    }
  }
  else {
    // The host connects to an existing Wifi network
    
    // Wait for the serial port to start because the user must be able
    // to see the printed IP address in the serial monitor
    while (!Serial) {
      yield();
    }

    // Attempt to connect to the Wifi network and obtain the IP
    // address. Because the address is not known before this point,
    // a serial monitor must be used to display it to the user.
    connectToWiFi(SSID, password);
    theIP = WiFi.localIP();
  }
 
  // Print the IP address: use this address in a browser to view the 
  // generated web page
  Serial.print("View your page at http://");
  Serial.println(theIP);

  // Start the web server
  server.begin();
  
  ////////////////////////////////////////////////////////////////////
  
  // Select how often to auto-refresh the web page. This should be done at
  // least as often as new data are obtained. A more frequent refresh is 
  // best for long cycle periods because the page refresh is not 
  // synchronized with the cycle. Users can also manually refresh the page.
  if (cycle_period == CYCLE_PERIOD_3_S) {
    refreshPeriodSeconds = 3;
  }
  else if (cycle_period == CYCLE_PERIOD_100_S) {
    refreshPeriodSeconds = 30;
  }
  else { // CYCLE_PERIOD_300_S
    refreshPeriodSeconds = 50;
  }
  
  // Apply the chosen settings to the Metriful board
  uint8_t particleSensor = PARTICLE_SENSOR;
  TransmitI2C(I2C_ADDRESS, PARTICLE_SENSOR_SELECT_REG, &particleSensor, 1);
  TransmitI2C(I2C_ADDRESS, CYCLE_TIME_PERIOD_REG, &cycle_period, 1);
  ready_assertion_event = false;
  TransmitI2C(I2C_ADDRESS, CYCLE_MODE_CMD, 0, 0);
}

void loop() {

  // While waiting for the next data release, respond to client requests
  // by serving the web page with the last available data. Initially the
  // data will be all zero (until the first data readout has completed).
  while (!ready_assertion_event) {
    handleClientRequests();
    yield();
  }
  ready_assertion_event = false;
  
  // new data are now ready

  /* Read data from the MS430 into the data structs. 
  For each category of data (air, sound, etc.) a pointer to the data struct is 
  passed to the ReceiveI2C() function. The received byte sequence fills the data 
  struct in the correct order so that each field within the struct receives
  the value of an environmental quantity (temperature, sound level, etc.)
  */ 
  
  // Air data
  // Choose output temperature unit (C or F) in Metriful_sensor.h
  ReceiveI2C(I2C_ADDRESS, AIR_DATA_READ, (uint8_t *) &airData, AIR_DATA_BYTES);
  
  /* Air quality data
  The initial self-calibration of the air quality data may take several
  minutes to complete. During this time the accuracy parameter is zero 
  and the data values are not valid.
  */ 
  ReceiveI2C(I2C_ADDRESS, AIR_QUALITY_DATA_READ, (uint8_t *) &airQualityData, AIR_QUALITY_DATA_BYTES);
  
  // Light data
  ReceiveI2C(I2C_ADDRESS, LIGHT_DATA_READ, (uint8_t *) &lightData, LIGHT_DATA_BYTES);
  
  // Sound data
  ReceiveI2C(I2C_ADDRESS, SOUND_DATA_READ, (uint8_t *) &soundData, SOUND_DATA_BYTES);
  
  /* Particle data
  This requires the connection of a particulate sensor (invalid 
  values will be obtained if this sensor is not present).
  Specify your sensor model (PPD42 or SDS011) in Metriful_sensor.h
  Also note that, due to the low pass filtering used, the 
  particle data become valid after an initial initialization 
  period of approximately one minute.
  */ 
  if (PARTICLE_SENSOR != PARTICLE_SENSOR_OFF) {
    ReceiveI2C(I2C_ADDRESS, PARTICLE_DATA_READ, (uint8_t *) &particleData, PARTICLE_DATA_BYTES);
  }
  
  // Create the web page ready for client requests
  assembleWebPage();
  
  // Check WiFi is still connected
  if (!createWifiNetwork) {
    uint8_t wifiStatus = WiFi.status();
    if (wifiStatus != WL_CONNECTED) {
      // There is a problem with the WiFi connection: attempt to reconnect.
      Serial.print("Wifi status: ");
      Serial.println(interpret_WiFi_status(wifiStatus));
      connectToWiFi(SSID, password);
      theIP = WiFi.localIP();
      Serial.print("View your page at http://");
      Serial.println(theIP);
      ready_assertion_event = false;
    }
  }
}


void handleClientRequests(void) {
  // Check for incoming client requests
  WiFiClient client = server.available();   
  if (client) { 
    bool blankLine = false;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        if (c == '\n') {
          // Two consecutive newline characters indicates the end of the client HTTP request
          if (blankLine) {
            // Send the page as a response
            client.print(pageBuffer);
            break; 
          }
          else {
            blankLine = true;
          }
        }
        else if (c != '\r') { 
          // Carriage return (\r) is disregarded for blank line detection
          blankLine = false;
        }
      }
    }
    delay(10);
    // Close the connection:
    client.stop();
  }
}

// Create a simple text web page showing the environment data in 
// separate category tables, using HTML and CSS
void assembleWebPage(void) {
  sprintf(pageBuffer,"HTTP/1.1 200 OK\r\n" 
                     "Content-type: text/html\r\n" 
                     "Connection: close\r\n"
                     "Refresh: %u\r\n\r\n",refreshPeriodSeconds);

  strcat(pageBuffer,"<!DOCTYPE HTML><html><head>"
                    "<meta charset='UTF-8'>"
                    "<title>Metriful Sensor Demo</title>"
                    "<style>"
                    "h1{font-size: 3vw;}"
                    "h2{font-size: 2vw; margin-top: 4vw;}"
                    "a{padding: 1vw; font-size: 2vw;}"
                    "table,th,td{font-size: 2vw;}"
                    "body{padding: 0vw 2vw;}"
                    "th,td{padding: 0.05vw 1vw; text-align: left;}"
                    "#v1{text-align: right; width: 10vw;}"
                    "#v2{text-align: right; width: 13vw;}"
                    "#v3{text-align: right; width: 10vw;}"
                    "#v4{text-align: right; width: 10vw;}"
                    "#v5{text-align: right; width: 11vw;}"
                    "</style></head>"
                    "<body><h1>Indoor Environment Data</h1>");
  
  //////////////////////////////////////
  
  strcat(pageBuffer,"<p><h2>Air Data</h2><table>");
  
  uint8_t T_intPart = 0;
  uint8_t T_fractionalPart = 0;
  bool isPositive = true;
  const char * unit = getTemperature(&airData, &T_intPart, &T_fractionalPart, &isPositive);
  sprintf(lineBuffer,"<tr><td>Temperature</td><td id='v1'>%s%u.%u</td><td>%s</td></tr>",
                     isPositive?"":"-", T_intPart, T_fractionalPart, unit);
  strcat(pageBuffer,lineBuffer);
  
  sprintf(lineBuffer,"<tr><td>Pressure</td><td id='v1'>%" PRIu32 "</td><td>Pa</td></tr>", airData.P_Pa);
  strcat(pageBuffer,lineBuffer);
  
  sprintf(lineBuffer,"<tr><td>Humidity</td><td id='v1'>%u.%u</td><td>%%</td></tr>", 
          airData.H_pc_int, airData.H_pc_fr_1dp);
  strcat(pageBuffer,lineBuffer);
  
  sprintf(lineBuffer,"<tr><td>Gas Sensor Resistance</td>"
                     "<td id='v1'>%" PRIu32 "</td><td>" OHM_SYMBOL "</td></tr></table></p>",
                      airData.G_ohm);
  strcat(pageBuffer,lineBuffer);

  //////////////////////////////////////
  
  strcat(pageBuffer,"<p><h2>Air Quality Data</h2>");
  
  if (airQualityData.AQI_accuracy == 0) {
    sprintf(lineBuffer,"<a>%s</a></p>",interpret_AQI_accuracy(airQualityData.AQI_accuracy));
    strcat(pageBuffer,lineBuffer);
  }
  else {
    sprintf(lineBuffer,"<table><tr><td>Air Quality Index</td><td id='v2'>%u.%u</td><td></td></tr>",
            airQualityData.AQI_int, airQualityData.AQI_fr_1dp);
    strcat(pageBuffer,lineBuffer);
    
    sprintf(lineBuffer,"<tr><td>Air Quality Summary</td><td id='v2'>%s</td><td></td></tr>",
            interpret_AQI_value(airQualityData.AQI_int));
    strcat(pageBuffer,lineBuffer);
  
    sprintf(lineBuffer,"<tr><td>Estimated CO" SUBSCRIPT_2 "</td><td id='v2'>%u.%u</td><td>ppm</td></tr>",
            airQualityData.CO2e_int, airQualityData.CO2e_fr_1dp);
    strcat(pageBuffer,lineBuffer);
  
    sprintf(lineBuffer,"<tr><td>Equivalent Breath VOC</td>"
                       "<td id='v2'>%u.%02u</td><td>ppm</td></tr></table></p>",
            airQualityData.bVOC_int, airQualityData.bVOC_fr_2dp);
    strcat(pageBuffer,lineBuffer);
  }
  
  //////////////////////////////////////
  
  strcat(pageBuffer,"<p><h2>Sound Data</h2><table>");
  
  sprintf(lineBuffer,"<tr><td>A-weighted Sound Pressure Level</td>"
                     "<td id='v3'>%u.%u</td><td>dBA</td></tr>",
                     soundData.SPL_dBA_int, soundData.SPL_dBA_fr_1dp);
  strcat(pageBuffer,lineBuffer);
  
  for (uint8_t i=0; i<SOUND_FREQ_BANDS; i++) {
    sprintf(lineBuffer,"<tr><td>Frequency Band %u (%u Hz) SPL</td>"
                       "<td id='v3'>%u.%u</td><td>dB</td></tr>",
        i+1, sound_band_mids_Hz[i], soundData.SPL_bands_dB_int[i], soundData.SPL_bands_dB_fr_1dp[i]);
    strcat(pageBuffer,lineBuffer);
  }
  
  sprintf(lineBuffer,"<tr><td>Peak Sound Amplitude</td>"
                     "<td id='v3'>%u.%02u</td><td>mPa</td></tr></table></p>",
          soundData.peak_amp_mPa_int, soundData.peak_amp_mPa_fr_2dp);
  strcat(pageBuffer,lineBuffer);

  //////////////////////////////////////
  
  strcat(pageBuffer,"<p><h2>Light Data</h2><table>");
  
  sprintf(lineBuffer,"<tr><td>Illuminance</td><td id='v4'>%u.%02u</td><td>lux</td></tr>",
          lightData.illum_lux_int, lightData.illum_lux_fr_2dp);
  strcat(pageBuffer,lineBuffer);
  
  sprintf(lineBuffer,"<tr><td>White Light Level</td><td id='v4'>%u</td><td></td></tr>"
                     "</table></p>", lightData.white);
  strcat(pageBuffer,lineBuffer);
  
  //////////////////////////////////////
  
  if (PARTICLE_SENSOR != PARTICLE_SENSOR_OFF) {
    strcat(pageBuffer,"<p><h2>Air Particulate Data</h2><table>");
    
    sprintf(lineBuffer,"<tr><td>Sensor Duty Cycle</td><td id='v5'>%u.%02u</td><td>%%</td></tr>",
            particleData.duty_cycle_pc_int, particleData.duty_cycle_pc_fr_2dp);
    strcat(pageBuffer,lineBuffer);
    
    char unitsBuffer[7] = {0};
    if (PARTICLE_SENSOR == PARTICLE_SENSOR_PPD42) {
      strcpy(unitsBuffer,"ppL");
    }
    else if (PARTICLE_SENSOR == PARTICLE_SENSOR_SDS011) {
      strcpy(unitsBuffer,SDS011_UNIT_SYMBOL);
    }
    else {
      strcpy(unitsBuffer,"(?)");
    }
    sprintf(lineBuffer,"<tr><td>Particle Concentration</td>"
                       "<td id='v5'>%u.%02u</td><td>%s</td></tr></table></p>",
            particleData.concentration_int, particleData.concentration_fr_2dp, unitsBuffer);
    strcat(pageBuffer,lineBuffer);
  }

  //////////////////////////////////////
  
  strcat(pageBuffer,"</body></html>");
}
