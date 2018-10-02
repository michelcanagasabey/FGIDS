#include <SPI.h>
#include <SD.h>

const int chipSelect = 10;
String dataString = "";
long data;
void setup() {
  // Open serial communications and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }


  Serial.print("Initializing SD card...");

  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect)) {
    Serial.println("Card failed, or not present");
    // don't do anything more:
    while (1);
  }
  Serial.println("card initialized.");
}
void loop() {
   File dataFile = SD.open("datalog.txt", FILE_WRITE);
   if (dataFile) {
    dataFile.println(data);    
    // print to the serial port too:
    Serial.print("bytes written = ");
    Serial.print(dataFile.println(String(data)));
    Serial.print(", ");
    dataFile.close();
    Serial.println(data);
  }
  data = random(1000);
  delay(1000);

}
