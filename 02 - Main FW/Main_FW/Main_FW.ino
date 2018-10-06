#include <SPI.h>
#include <SD.h>
#include <Time.h>
#include <TimeLib.h>
#include <DS1302RTC.h>


// Set pins:  CE, IO,CLK
DS1302RTC RTC(5,6,7);

/* Pin definitions */
#define COUNTER 8

#define BUTTON 2
const int chipSelect = 10;
#define PWM_DEBUG
#ifdef PWM_DEBUG
#define PWM_PIN 3
#endif




#define LOG_TIMEOUT 1
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

/*RTC*/
tmElements_t tnow,tlast;

/*SD*/
File dataFile;
long data;
char fileName[10];

void print2digits(int number) {
  if (number >= 0 && number < 10)
    Serial.write('0');
  Serial.print(number);
}

void setup()
{
  pinMode(BUTTON, INPUT);
  pinMode(COUNTER, INPUT);
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
    TIMSK1 = 0b00100001; //Input Capture and Overflow Interrupts

  delay(1000);
  
  //Turn off timer0
  //TCCR0B = 0x00;
  //TIMSK0 = 0x00;

  if (RTC.haltRTC()) {
   Serial.println("The DS1302 is stopped.  Please run the SetTime");
   Serial.println();
  }
  if (!RTC.writeEN()) {
    Serial.println("The DS1302 is write protected. This normal.");
    Serial.println();
  }
  RTC.writeEN(0);
  delay(1000);
 if (! RTC.read(tnow)) {
    Serial.println("The DS1302 is write protected. This normal.");
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
  if (SD.begin(chipSelect)) {
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
 
  Serial.println("in Main");
  delay(1000);
  if (! RTC.read(tnow)) {
    print2digits(tnow.Hour);
    Serial.write(':');
    print2digits(tnow.Minute);
    Serial.write(',');
    print2digits(tlast.Hour);
    Serial.write(':');
    print2digits(tlast.Minute);
    Serial.write('\n');
    /* check log time */
    if(tlast.Hour != tnow.Hour || (tnow.Minute - tlast.Minute) > LOG_TIMEOUT)
    {
      tlast.Hour = tnow.Hour;
      tlast.Minute = tnow.Minute;
      timeToLog = true;
      Serial.println("timeToLog = true");
      //TCCR1B = 0b00000001; //No Noise Canceler, Falling Edge, Prescaler=1
      //TIMSK1 = 0b00100001; //Input Capture and Overflow Interrupts
    }   
  
  }
  else {
    Serial.println("DS1302 read error!  Please check the circuitry.");
  }
  
  if(done==1)
  {
    measured_frequency = (CPU_FREQUENCY * capture_count_sav) / total_count_sav;
    Serial.print("Freq:        kHz");
    Serial.println(measured_frequency/1000);
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
    done = 0;
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
  }
}


