#include <SPI.h>
#include <SD.h>
#include <Time.h>
#include <TimeLib.h>
#include <DS1302RTC.h>
#include<Wire.h>


// Set pins:  CE, IO,CLK
DS1302RTC RTC(14,15,16);

/* Pin definitions */
#define COUNTER 8
#define BUTTON 2
#define RED_LED 5
#define MPU1_ADDRESS 0x68
#define MPU2_ADDRESS 0x69  

const int chipSelect = 10;

#define PWM_DEBUG
#ifdef PWM_DEBUG
#define PWM_PIN 3
#endif

#define FREQUENCY_SAMPLES 10
#define LOG_TIMEOUT 0
#define CPU_FREQUENCY 15992430.43 /* must be a float. use 16000000.0 as default */
#define CAPACITY 0.0000000005 /* in Farad */
#define ZERO_INDUCTANCE 1.86 /* in microHenry (uH) */
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
float measured_inductance;
float zero_calibration;
float adjusted_inductance;
bool timeToLog = false;
uint8_t rtcErrorCnt = 0;
uint8_t samples =0; 
float frequency =0;

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
  Wire.begin();
  Wire.beginTransmission(address);
  Wire.write(0x6B);  // PWR_MGMT_1 register
  Wire.write(0);     // set to zero (wakes up the MPU-6050)
  Wire.endTransmission(true);
  Wire.beginTransmission(address);
  Wire.write(0x3F);  // starting with register 0x3B (ACCEL_XOUT_H)
  Wire.endTransmission(false);
  Wire.requestFrom(address,2,true);
  int16_t ACZ = Wire.read()<<8|Wire.read();
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
  pinMode(BUTTON, INPUT);
  pinMode(COUNTER, INPUT);
  pinMode(RED_LED, OUTPUT);
  //pinMode(CHIPSELECT, OUTPUT);
  Serial.begin(57600);
 
  overflow_count = 0;
  capture_count = 0;
  capture_count_sav = 0;
  total_count = 0;
  total_count_sav = 0;
  done = 0;
  zero_calibration = ZERO_INDUCTANCE;
  
  /*
   * TCCR1A – Timer/Counter1 Control Register A, page 132
   * Bit 7:6 – COM1A1:0: Compare Output Mode for Channel A (Physical Pin 15, Arduino Pin 9)
   * Bit 5:4 – COM1B1:0: Compare Output Mode for Channel B (Physical Pin 16, Arduino Pin 10)
   * Bit 3:2 - Reserved
   * Bit 1:0 – WGM11:0: Waveform Generation Mode
   */   
  TCCR1A = 0b00000000; //No output
  
  /*
   * TCCR1B – Timer/Counter1 Control Register B, page 134
   * Bit 7 – ICNC1: Input Capture Noise Canceler
   * Bit 6 – ICES1: Input Capture Edge Select
   * Bit 5 – Reserved
   * Bit 4:3 – WGM13:2: Waveform Generation Mode
   * Bit 2:0 – CS12:0: Clock Select
   */
    TCCR1B = 0b00000001; //No Noise Canceler, Falling Edge, Prescaler=1
  
  /*
   * TIMSK1 – Timer/Counter1 Interrupt Mask Register, page 136
   * Bit 7, 6 – Reserved
   * Bit 5 – ICIE1: Timer/Counter1, Input Capture Interrupt Enable
   * Bit 4, 3 – Reserved
   * Bit 2 – OCIE1B: Timer/Counter1, Output Compare B Match Interrupt Enable
   * Bit 1 – OCIE1A: Timer/Counter1, Output Compare A Match Interrupt Enable
   * Bit 0 – TOIE1: Timer/Counter1, Overflow Interrupt Enable
   */
    //TIMSK1 = 0b00100001; //Input Capture and Overflow Interrupts

  delay(1000);

  if (RTC.haltRTC())
  {
   Serial.println("The DS1302 is stopped.  Please run the SetTime");
   Serial.println();
  }
  if (!RTC.writeEN())
  {
    Serial.println("The DS1302 is write protected. This normal.");
    Serial.println();
  }
  RTC.writeEN(0);
  delay(2000);
  RTC.read(tnow);
  RTC.read(tnow);
  
  //setTime(11,49,0,7,10,2018);
  //time_t t = now();
  //Serial.println(t);
 // RTC.set(t);
  
 
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
    Serial.println("Card failed, or not present");
    digitalWrite(RED_LED, HIGH);
    while(1);
  }
#ifdef PWM_DEBUG
pinMode(PWM_PIN, OUTPUT);
analogWrite(PWM_PIN, 128);
#endif

tlast.Hour = tnow.Hour;
tlast.Minute = tnow.Minute;  

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
      }   
  
    }
    else if(temp != 0)
    {
      rtcErrorCnt++;
    }
    else 
    {
      Serial.println("DS1302 read error!  Please check the circuitry.");
      rtcErrorCnt = 0;
    }
  }  
  if(done==1)
  {
    measured_frequency = (CPU_FREQUENCY * capture_count_sav) / total_count_sav;
    samples++;
    //Serial.print(measured_frequency);
    //Serial.print("Hz");
    //Serial.print(",");
    //Serial.println(samples);
    frequency += measured_frequency;
    if(samples >= FREQUENCY_SAMPLES)
    {
      frequency /= FREQUENCY_SAMPLES;
      int16_t acceleration = get_az(MPU1_ADDRESS);
      dataFile = SD.open(fileName, FILE_WRITE);
      print2digits(tlast.Hour);
      Serial.print(":");
      print2digits(tlast.Minute);
      Serial.print(",");
      Serial.print(frequency);
      Serial.print(",");
      Serial.println(acceleration);
      if (dataFile)
      {
        print2digitsToSD(tlast.Hour);
        dataFile.print(":");
        print2digitsToSD(tlast.Minute);
        dataFile.print(",");
        dataFile.print(String(frequency));
        dataFile.print(",");
        dataFile.println(acceleration);
        dataFile.close();
        digitalWrite(RED_LED, LOW);
      }
      else
      {
        Serial.println("SD error");
        digitalWrite(RED_LED, HIGH);
      }
      TIMSK1 = 0x00;//0b00100001; //Input Capture and Overflow Interrupts
      timeToLog = false;
      frequency =0;
    }
    
    done = 0;
    
    /*    
    measured_inductance = 2 * PI * measured_frequency * sqrt(CAPACITY);
    measured_inductance = 1000000 / (measured_inductance * measured_inductance);
    
    if(digitalRead(BUTTON)==0) //Button pressed
    {
      zero_calibration = measured_inductance;
    }
    adjusted_inductance = measured_inductance - zero_calibration;
    if(abs(adjusted_inductance)<1)
    {
      Serial.print("Induc:        nH");

      Serial.println(1000*adjusted_inductance);      
    }
    else
    {
      Serial.print("Induc:        uH");

      Serial.print(adjusted_inductance);
    }
    
*/
  }
  else if(done==2)
  {
    Serial.print("Not resonating  ");
    if(abs(zero_calibration)<1)
    {
       Serial.print("Calib:        nH");

      Serial.println(1000*zero_calibration);      
    }
    else
    {

      Serial.print("Calib:        uH");
      Serial.println(zero_calibration);
    }
    done = 0;
    TIMSK1 = 0x00;
  }
}


