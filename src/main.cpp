#include <Arduino.h>
#include <String.h>
#include <stdio.h>
#include <stdlib.h>
#include "time.h"
#include <Wire.h>
#include <SoftwareSerial.h>
#include <WiFi.h>
#include <SensirionI2CScd4x.h>
#include "MLX90640_API.h"
#include "MLX90640_I2C_Driver.h"
#include <TinyGPSPlus.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>

// ####################################################
// DEFINES
// ####################################################

#define TA_SHIFT 8 // Default shift for MLX90640 in open air
#define cells 768
#define THRESHOLD_TEMP 40
// ########################################
#define SSID "INTERNET"
#define PASSWD "6984786516"
// ########################################
// #define SSID "Wu Tang Lan"
// #define PASSWD "wireless8688"
// ########################################
#define API_TOKEN "8cNy1bBc8skmZ1atkjuh"
#define SERVER "http://hdiot.gr:8080"
#define GPS_RX_PIN 34
#define GPS_TX_PIN 12
#define GPS_BAUDRATE 9600
#define GPS_LOCK_TIMEOUT_SEC 120

#define AMBIENT_READ_INTERVAL 600 // SECONDS
#define FIRE_DETECT_INTERVAL 30   // SECONDS

// ####################################################
// SENSORS
// ####################################################

paramsMLX90640 mlx90640;
SensirionI2CScd4x scd4x;
TinyGPSPlus gps;
SoftwareSerial ss(GPS_RX_PIN, GPS_TX_PIN);

// ####################################################
// SENSOR READ VALUES
// ####################################################

float mlx90640To[768];
uint16_t co2 = 0;
float temperature = 0.;
float humidity = 0;
char gpsLongitude[20];
char gpsLatitude[20];

// ####################################################
// HELPER VARIABLES
// ####################################################

const byte MLX90640_address = 0x33; // Default 7-bit unshifted address of the MLX90640
bool isDataReady = false;
// int THRESHOLD_TEMP = 38;
int FirePixels = 0;
bool check_fire_flags[24][32] = {false};
uint16_t error;
char errorMessage[256];
char server_addr[80];
char http_request_data[100];
char fire_flag[5];
int gpsSatellites = 0;
bool gpsValid = false;

// ####################################################

// Returns true if the MLX90640 is detected on the I2C bus
boolean isConnected()
{
  Wire.beginTransmission((uint8_t)MLX90640_address);
  if (Wire.endTransmission() != 0)
    return (false); // Sensor did not ACK
  return (true);
}

// TODO:
// human rejection
// veltiosi tou algorithmou evresis flogas
//
boolean isThereFire()
{
  for (int x = 0; x < 768; x++)
  {
    if ((mlx90640To[x] >= 32) && (mlx90640To[x] <= 38) && (FirePixels >= 2))
    {
      mlx90640To[x] = 0;
    }
    if (mlx90640To[x] >= THRESHOLD_TEMP)
    {
      return (true);
    }
  }
  return (false);
}

void sendHttpPost()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    WiFiClient client;
    HTTPClient http;
    http.begin(client, server_addr);
    http.addHeader("Content-Type", "application/json");
    // Send HTTP POST request
    Serial.print(http_request_data);
    int http_response_code = http.POST(http_request_data);
    Serial.print("\nHTTP Response code: ");
    Serial.println(http_response_code);
    http.end();
  }
}
// delay function gia to gps
void smartDelay(unsigned long ms)
{
  unsigned long start = millis();
  do
  {
    while (ss.available())
      gps.encode(ss.read());
  } while (millis() - start < ms);
}

// ypologizei enan 32x24 pinaka pou deixnei pou yparxei fotia
void calcFireTable(bool print_flag)
{
  int cell = -1;
  int x;
  int check_cells[] = {0, 1, -1, -32, 32, -33, -31, 33, 31};
  int check_len = sizeof(check_cells) / sizeof(int);
  FirePixels = 0;

  // psaxnei gia pixel pano apto thresh temp
  for (x = 0; x < 768; x++)
  {
    if (mlx90640To[x] > THRESHOLD_TEMP)
    {
      cell = x;
    }
  }

  // ama exei vrei 1 pixel tote psaxnei kai sta geitonika tou gia fotia
  if (x != -1)
  {
    for (int j = 0; j < check_len; j++)
    {
      if (cell + check_cells[j] >= 0 && cell + check_cells[j] < 768)
      {
        if (mlx90640To[cell + check_cells[j]] > THRESHOLD_TEMP)
        {
          check_fire_flags[(int)((cell + check_cells[j]) / 32)][(cell + check_cells[j]) % 32] = true;
          FirePixels += 1;
        }
      }
    }
  }

  if (print_flag)
  {
    // ektyponei to 32x24 firetable  # -> fotia
    for (int z = 0; z < 24; z++)
    {
      for (int l = 0; l < 32; l++)
      {
        Serial.print("[");
        if (check_fire_flags[z][l])
        {
          Serial.print("#");
        }
        else
        {
          Serial.print("O");
        }
        Serial.print("]");
      }
      Serial.println("");
    }
    Serial.println("");
    Serial.println("");
  }
}

// arxikopoiei ton firetable (prepei na arxikopoiiseis prin kathe sensor read)
void initFireTable()
{
  for (int z = 0; z < 24; z++)
  {
    for (int l = 0; l < 32; l++)
    {
      check_fire_flags[z][l] = false;
    }
  }
}

// typonei enan 32x24 pinaka me thermokrasies pou diavase o MLX90640 sensoras
void printTempTable()
{
  for (int z = 0; z < 768; z++)
  {
    Serial.print("[");
    Serial.print(mlx90640To[z], 0);
    Serial.print("]");
    if ((!(z % 32)) && z != 0)
    {
      Serial.println("");
    }
    // Serial.println("");
  }
  Serial.println("");
  Serial.println("");
}

// use this function to get gps longitude to gpsLongitude string
void getGpsLong(float val, bool valid)
{
  if (!valid)
  {
    // while (len-- > 1)
    //   Serial.print('*');
    // Serial.print(' ');
    Serial.println("Longitude not valid");
  }
  else
  {
    Serial.print(val);
    sprintf(gpsLongitude, "%f", val);
    // int vi = abs((int)val);
    // int flen = prec + (val < 0.0 ? 2 : 1); // . and -
    // flen += vi >= 1000 ? 4 : vi >= 100 ? 3
    //                      : vi >= 10    ? 2
    //                                    : 1;
    // for (int i = flen; i < len; ++i)
    //   Serial.print(' ');
  }
  smartDelay(0);
}

// use this function to get gps latitude to gpsLatitude string
void getGpsLat(float val, bool valid)
{
  if (!valid)
  {
    // while (len-- > 1)
    //   Serial.print('*');
    // Serial.print(' ');
    Serial.println("Latitude not valid");
  }
  else
  {
    Serial.println(val);
    sprintf(gpsLatitude, "%f", val);
    // int vi = abs((int)val);
    // int flen = prec + (val < 0.0 ? 2 : 1); // . and -
    // flen += vi >= 1000 ? 4 : vi >= 100 ? 3: vi >= 10    ? 2: 1;
    // for (int i = flen; i < len; ++i)
    //   Serial.print(' ');
  }
  smartDelay(0);
}

// use this function to get a MLX sensor reading to mlx90640To table
void writeTempTable()
{
  uint16_t mlx90640Frame[834];
  int status = MLX90640_GetFrameData(MLX90640_address, mlx90640Frame);

  float vdd = MLX90640_GetVdd(mlx90640Frame, &mlx90640);
  float Ta = MLX90640_GetTa(mlx90640Frame, &mlx90640);

  float tr = Ta - TA_SHIFT; // Reflected temperature based on the sensor ambient temperature
  float emissivity = 0.95;

  for (int x = 0; x < 768; x++)
  {
    mlx90640To[x] = 0;
  }

  MLX90640_CalculateTo(mlx90640Frame, &mlx90640, emissivity, tr, mlx90640To);
}

void setup()
{
  Wire.begin();
  Wire.setClock(8000000); // Increase I2C clock speed to 800kHz
  Serial.begin(115200);   // Fast serial as possible

  // #########################################################################################################
  // Initialize GPS & WIFI BEGIN
  // #########################################################################################################
  ss.begin(GPS_BAUDRATE);

  sprintf(server_addr, "%s/api/v1/%s/telemetry", SERVER, API_TOKEN);

  Serial.println("Waiting for first measurement... (5 sec)");
  WiFi.begin(SSID, PASSWD);
  Serial.println("Connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());

  gpsSatellites = gps.satellites.value();
  gpsValid = gps.satellites.isValid();
  smartDelay(0);
  unsigned long timeout = millis();
  while (gpsSatellites <= 3 && ((millis() - timeout) < GPS_LOCK_TIMEOUT_SEC*1000))
  {
    Serial.print("Waiting for GPS\n");
    // delay(2000);
    smartDelay(1000);
    sprintf(http_request_data, "{\"GPS_Satellites\":\"%d\"}", gpsSatellites);
    sendHttpPost();
    gpsSatellites = gps.satellites.value();
    gpsValid = gps.satellites.isValid();
    
  }

  smartDelay(1000);
  getGpsLat(gps.location.lat(), gps.location.isValid());
  getGpsLong(gps.location.lng(), gps.location.isValid());
  smartDelay(0);
  sprintf(http_request_data, "{\"GPS_Satellites\":\"%d\",\"latitude\":\"%s\",\"longitude\":\"%s\"}", gpsSatellites, gpsLatitude, gpsLongitude);
  sendHttpPost();

  // #########################################################################################################
  // Initialize GPS & WIFI END
  // #########################################################################################################

  // #########################################################################################################
  // Initialize MLX sensor BEGIN
  // #########################################################################################################
  if (isConnected() == false)
  {
    Serial.println("MLX90640 not detected at default I2C address. Please check wiring. Freezing.");
    while (1)
      ;
  }

  // Get device parameters - We only have to do this once
  int status;
  uint16_t eeMLX90640[832];
  status = MLX90640_DumpEE(MLX90640_address, eeMLX90640);
  if (status != 0)
    Serial.println("Failed to load system parameters");

  status = MLX90640_ExtractParameters(eeMLX90640, &mlx90640);
  if (status != 0)
    Serial.println("Parameter extraction failed");

  // Once params are extracted, we can release eeMLX90640 array

  MLX90640_SetRefreshRate(MLX90640_address, 0x02); // Set rate to 2Hz
  // MLX90640_SetRefreshRate(MLX90640_address, 0x07); //Set rate to 64Hz

  // #########################################################################################################
  // Initialize MLX sensor END
  // #########################################################################################################

  // #########################################################################################################
  // Initialize SCD41 sensor BEGIN
  // #########################################################################################################

  scd4x.begin(Wire);
  // stop potentially previously started measurement
  error = scd4x.stopPeriodicMeasurement();
  if (error)
  {
    Serial.print("Error trying to execute stopPeriodicMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }

  // Start Measurement
  error = scd4x.startPeriodicMeasurement();
  if (error)
  {
    Serial.print("Error trying to execute startPeriodicMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }

  Serial.println("\nWaiting for first measurement... (5 sec)");

  // #########################################################################################################
  // Initialize SCD41 sensor END
  // #########################################################################################################
  /*
  error = scd4x.getDataReadyFlag(isDataReady);
  if (error)
  {
    Serial.print("Error trying to execute readMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
    return;
  }
  if (!isDataReady)
  {
    return;
  }
  error = scd4x.readMeasurement(co2, temperature, humidity);
  if (error)
  {
    Serial.print("Error trying to execute readMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }
  else if (co2 == 0)
  {
    Serial.println("Invalid sample detected, skipping.");
  }
  else
  {
    ;
  }

  sprintf(http_request_data, "{\"ambient_temp\":\"%f\",\"ambient_humd\":\"%f\",\"co2\":\"%d\"}", temperature, humidity, co2);
  sendHttpPost();
  */
}

void loop()
{
  // #########################################################################################################
  // Take initial reading from SCD41
  // #########################################################################################################

  bool stay = true;
  int timeout = 0;
  if (timeout <= 3)
  {
    while (stay)
    {
      error = scd4x.getDataReadyFlag(isDataReady);
      if (isDataReady)
      {
        stay = false;
      }
      // Serial.println("Waiting for reading SCD41");
      Serial.print(". ");
      timeout += 1;
      delay(5000);
    }

    error = scd4x.readMeasurement(co2, temperature, humidity);
    if (error)
    {
      Serial.print("Error trying to execute readMeasurement(): ");
      errorToString(error, errorMessage, 256);
      Serial.println(errorMessage);
    }
    else if (co2 == 0)
    {
      Serial.println("Invalid sample detected, skipping.");
    }
    else
    {
      Serial.print("Ambient temperature is:");
      Serial.println(temperature);
      Serial.print("Ambient humidity is:");
      Serial.println(humidity);
      Serial.print("CO2 concentration is:");
      Serial.print(co2);
      Serial.println(" PPM");
    }
    sprintf(http_request_data, "{\"ambient_temp\":\"%f\",\"ambient_humd\":\"%f\",\"co2\":\"%d\"}", temperature, humidity, co2);
    sendHttpPost();
  }

  // #########################################################################################################
  // Setting the initial threshold temperature used to detect fire
  // #########################################################################################################

  // TODO:
  // na vrethei o tropos pou tha setaroume to threshold temp
  // THRESHOLD_TEMP = (int)temperature + 15;
  // THRESHOLD_TEMP = 38;

  // #########################################################################################################
  // Getting the MLX reading
  // #########################################################################################################
  bool fire_fuse = false;
  for (int fireDetect = 0; fireDetect <= AMBIENT_READ_INTERVAL / FIRE_DETECT_INTERVAL; fireDetect++)
  {
    writeTempTable();
    bool fireExist = isThereFire();
    Serial.print("Fire Check ");
    Serial.print(fireDetect);
    Serial.print(": ");
    Serial.println(fireExist);
    if (!fireExist && fireDetect == 0)
    {
      fire_fuse = false;
      strcpy(fire_flag, "No");
      sprintf(http_request_data, "{\"fire\":\"%s\"}", fire_flag);
      sendHttpPost();
    }
    if (!fireExist && fire_fuse == true)
    {
      fire_fuse = false;
      strcpy(fire_flag, "No");
      sprintf(http_request_data, "{\"fire\":\"%s\"}", fire_flag);
      sendHttpPost();
    }
    if (fireExist && fire_fuse == false)
    {
      fire_fuse = true;
      Serial.println("[FIRE DETECTED]");
      smartDelay(1000);
      getGpsLat(gps.location.lat(), gps.location.isValid());
      getGpsLong(gps.location.lng(), gps.location.isValid());
      strcpy(fire_flag, "Yes");
      sprintf(http_request_data, "{\"fire\":\"%s\",\"latitude\":\"%s\",\"longitude\":\"%s\"}", fire_flag, gpsLatitude, gpsLongitude);
      sendHttpPost();
    }
    // if (fire_fuse)
    // {
    //   fireDetect = 0;
    //   Serial.println("[FIRE DETECTED] RESET DEVICE");
    // }

    delay(FIRE_DETECT_INTERVAL * 1000);
  }
}
