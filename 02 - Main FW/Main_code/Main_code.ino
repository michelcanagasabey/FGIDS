#include <SPI.h>
#include <SD.h>
#include <Time.h>
#include <TimeLib.h>
#include <DS1302RTC.h>
#include<Wire.h>

#define MOD2
// Set pins:  CE, IO,CLK
//DS1302RTC RTC(17,16,15);
//#define DS1302_GND_PIN 14

#if defined (MOD1)
  DS1302RTC RTC(17,16,15);
  #define DS1302_GND_PIN 14

#else #if defined (MOD2)
  DS1302RTC RTC(14,15,16);
  #define DS1302_GND_PIN 17
#endif
/* Pin definitions */
#define COUNTER 8
#define OPTOCOUPLER 9
#define RED_LED 5
#define GREEN_LED 4
#define MPU1_ADDRESS 0x68
#define MPU2_ADDRESS 0x69  

const int chipSelect = 10;

//#define PWM_DEBUG
#ifdef PWM_DEBUG
#define PWM_PIN 3
#endif

#define FREQUENCY_SAMPLES 10
#define LOG_TIMEOUT 0
#define CPU_FREQUENCY 15992430.43 /* must be a float. use 16000000.0 as default */
#define OVERFLOW_COUNT_MINIMUM 64
#define OVERFLOW_COUNT_MAXIMUM 128

/* Frequency measurement */
volatile uint8_t overflow_count;
volatile uint16_t capture_count;
volatile uint16_t capture_count_sav;
volatile int32_t total_count;
volatile int32_t total_count_sav;
volatile uint8_t done;
float measured_frequency;
bool timeToLog = false;
uint8_t rtcErrorCnt = 0;
uint8_t samples =0; 
float frequency =0;
int32_t acceleration1, acceleration2;

/*RTC*/
tmElements_t tnow,tlast;

/*SD*/
File dataFile;
char fileName[10];

enum{
  SITTING = 1,
  STANDING = 2,
  SLEEPING =3,
  ERROR = 4
}position;

int16_t get_az(uint8_t address)
{
  Wire.beginTransmission(address);
  Wire.write(0x3F);  // starting with register 0x3B (ACCEL_XOUT_H)
  Wire.endTransmission(false);
  Wire.requestFrom(address,2,true);
  int16_t ACZ = Wire.read()<<8|Wire.read();
  //Serial.println(ACZ);
  Wire.endTransmission(true);
  return ACZ;
}
void print2digits(int number)
{
  if (number >= 0 && number < 10)
    Serial.write('0');
  Serial.print(number);
}

void print2digitsToSD(int number)
{
  if (number >= 0 && number < 10)    
    dataFile.print('0');
  dataFile.print(String(number));  
}

void setup()
{
  pinMode(COUNTER, INPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(OPTOCOUPLER, OUTPUT);
  digitalWrite(OPTOCOUPLER, LOW);
  digitalWrite(RED_LED, HIGH);
  pinMode(DS1302_GND_PIN, OUTPUT);
  digitalWrite(DS1302_GND_PIN, LOW);
 
  Serial.begin(57600);

  overflow_count = 0;
  capture_count = 0;
  capture_count_sav = 0;
  total_count = 0;
  total_count_sav = 0;
  done = 0;
  //zero_calibration = ZERO_INDUCTANCE;
  
  TCCR1A = 0b00000000; //No output
  TCCR1B = 0b00000001; //No Noise Canceler, Falling Edge, Prescaler=1
  delay(1000);

  if (RTC.haltRTC())
  {
   Serial.println("RTC Halt");
  }
  if (!RTC.writeEN())
  {
    Serial.println("RTC WP");
  }
  RTC.writeEN(0);
  
  #ifdef SET_TIME 
  setTime(22,20,0,17,10,2018);
  time_t t = now();
  Serial.println(t);
  RTC.set(t);
  #endif
  delay(1000);
 
 if (! RTC.read(tnow))
 {
    Serial.print("  Time = ");
    print2digits(tnow.Hour);
    Serial.write(':');
    print2digits(tnow.Minute);
    Serial.write(':');
    print2digits(tnow.Second);
    Serial.print(", Date (D/M/Y) = ");
    Serial.print(tnow.Day);
    Serial.write('/');
    Serial.print(tnow.Month);
    Serial.write('/');
    Serial.print(tmYearToCalendar(tnow.Year));
    Serial.print(", DoW = ");
    Serial.print(tnow.Wday);
    Serial.println();
 }
 else
 {
   Serial.println("RTC Error");
   digitalWrite(RED_LED, HIGH);
   while(1);
 }

  Serial.println("sd");
  fileName[0] = tnow.Day/10 +0x30;
  fileName[1] = tnow.Day%10 +0x30;
  fileName[2] = '-';
  fileName[3] = tnow.Month/10 +0x30;
  fileName[4] = tnow.Month%10 +0x30;
  fileName[5] = '.';
  fileName[6] = 'T';
  fileName[7] = 'X';
  fileName[8] = 'T'; 
  fileName[9] = '\0'; 
  Serial.println(fileName);
  if (SD.begin(chipSelect))
  {
    dataFile = SD.open(fileName,FILE_WRITE);
    if(dataFile)
    {
     Serial.println("SD success");
     dataFile.close(); 
    }       
  }
  else
  {
    Serial.println("SD Error");
    digitalWrite(RED_LED, HIGH);
    while(1);
  }
#ifdef PWM_DEBUG
pinMode(PWM_PIN, OUTPUT);
analogWrite(PWM_PIN, 128);
#endif
Serial.println("after sd");
  tlast.Hour = tnow.Hour;
  tlast.Minute = tnow.Minute;  

  Wire.begin();
  Wire.beginTransmission(MPU1_ADDRESS);
  Wire.write(0x6B);  // PWR_MGMT_1 register
  Wire.write(0);     // set to zero (wakes up the MPU-6050)
  Wire.endTransmission(true);

  Wire.beginTransmission(MPU2_ADDRESS);
  Wire.write(0x6B);  // PWR_MGMT_1 register
  Wire.write(0);     // set to zero (wakes up the MPU-6050)
  Wire.endTransmission(true);
  digitalWrite(RED_LED, LOW);
  Serial.println("init finished");
}

ISR(TIMER1_OVF_vect)
{
  overflow_count++;
  total_count += 65536;
  if(overflow_count>OVERFLOW_COUNT_MAXIMUM)
  {
    done = 2;
  }
}

ISR(TIMER1_CAPT_vect)
{
  ++capture_count;
  if(overflow_count>=OVERFLOW_COUNT_MINIMUM)
  {
    uint16_t timer_value;
    uint8_t timer_low;
    timer_low = ICR1L;
    timer_value = ICR1H;
    timer_value <<= 8;
    timer_value |= timer_low;
    total_count_sav = total_count + timer_value;
    total_count = 0;
    total_count -= timer_value;
    capture_count_sav = capture_count;
    capture_count = 0;
    overflow_count = 0;
    done = 1;
  }
}

void loop()
{
  RTC.read(tnow);
  delay(1000);  
  if(!timeToLog)
  {
    uint8_t temp = RTC.read(tnow);
    if (temp==0 && rtcErrorCnt < 2)
    {
      /*
      print2digits(tnow.Hour);
      Serial.write(':');
      print2digits(tnow.Minute);
      Serial.write(' , ');
      print2digits(tlast.Hour);
      Serial.write(':');
      print2digits(tlast.Minute);
      Serial.write('\n');
      */
      rtcErrorCnt = 0;
      /* check log time */
      if(tlast.Hour != tnow.Hour || (tnow.Minute - tlast.Minute) > LOG_TIMEOUT)
      {
        tlast.Hour = tnow.Hour;
        tlast.Minute = tnow.Minute;
        timeToLog = true;
        samples = 0;
        TIMSK1 = 0b00100001; //Input Capture and Overflow Interrupts
        digitalWrite(GREEN_LED, HIGH);
        digitalWrite(OPTOCOUPLER, HIGH);
      }   
  
    }
    else if(temp != 0)
    {
      rtcErrorCnt++;
    }
    else 
    {
      Serial.println("RTC Error");
      rtcErrorCnt = 0;
    }
  }  
  if(done==1)
  {
    //Ignore the first reading 
    if(samples > 0)
    {
      measured_frequency = (CPU_FREQUENCY * capture_count_sav) / total_count_sav;
      samples++;
      acceleration1 += get_az(MPU1_ADDRESS);
      acceleration2 += get_az(MPU2_ADDRESS);
      frequency += measured_frequency;
      //Serial.print(acceleration1);
      //Serial.print(",");
      //Serial.println(acceleration2);
      //Serial.print(measured_frequency);
      //Serial.print("Hz");
      //Serial.print(",");
      //Serial.println(samples);
    }
    else
    {
      samples++;    
    }
    if(samples > FREQUENCY_SAMPLES)
    {
      frequency /= FREQUENCY_SAMPLES;
      acceleration1 /= FREQUENCY_SAMPLES;
      acceleration2 /= FREQUENCY_SAMPLES;
      dataFile = SD.open(fileName, FILE_WRITE);
      print2digits(tlast.Hour);
      Serial.print(":");
      print2digits(tlast.Minute);
      Serial.print(",");
      Serial.print(frequency);
      Serial.print(",");
      Serial.print(acceleration1);
      Serial.print(",");
      Serial.println(acceleration2);
      if (dataFile)
      {
        print2digitsToSD(tlast.Hour);
        dataFile.print(":");
        print2digitsToSD(tlast.Minute);
        dataFile.print(",");
        dataFile.print(String(frequency));
        dataFile.print(",");
        dataFile.print(acceleration1);
        dataFile.print(",");
        dataFile.println(acceleration2);
        dataFile.close();
        digitalWrite(GREEN_LED, LOW);
        digitalWrite(OPTOCOUPLER, LOW);
      }
      else
      {
        Serial.println("SD error");
        digitalWrite(GREEN_LED, LOW);
        digitalWrite(RED_LED, HIGH);
      }
      TIMSK1 = 0x00;//0b00100001; //Input Capture and Overflow Interrupts
      timeToLog = false;
      frequency =0;
    }
    
    done = 0;
  }
  else if(done==2)
  {
    Serial.print("Not resonating  ");
    done = 0;
    TIMSK1 = 0x00;
    digitalWrite(RED_LED, HIGH);
    while(1);
  }
}


