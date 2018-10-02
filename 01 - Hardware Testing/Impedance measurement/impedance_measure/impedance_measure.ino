#include <AD5933_generic.h>
#include <Communication.h>

/******************************************************************************/
/************************ Variables Definitions *******************************/
/******************************************************************************/
float           temperature = 0;
float           impedanceK  = 0;
double          impedance   = 0;
double          gainFactor  = 0;



void setup() {
  // put your setup code here, to run once:
    Serial.begin(57600);
/* Initialize AD5933 device. */
    if(AD5933_Init() == 1)
    {
        Serial.println("AD5933 OK");
    }
    else
    {
        Serial.println("AD5933 Error");
    }
    
    /* Reset the device. */
    AD5933_Reset();
    
    /* Select the source of the AD5933 system clock. */
    AD5933_SetSystemClk(AD5933_CONTROL_EXT_SYSCLK, 16000000UL);
    
    /* Set range and gain. */
    AD5933_SetRangeAndGain(AD5933_RANGE_2000mVpp, AD5933_GAIN_X1);

    /* set settling cycles */
    AD5933_ConfigSettlingCycles(511,AD5933_MUL_BY_4);
    
    /* Read the temperature. */
    temperature = AD5933_GetTemperature();
    Serial.print("Temp = ");
    Serial.println(temperature);
    Serial.println("Connect the calibration resistance and press any key...");
    while(Serial.available()==0){}  

    Serial.flush();
    /* Configure the sweep parameters */
    AD5933_ConfigSweep(50000, 0, 511);
    
    /* Start the sweep operation. */
    AD5933_StartSweep();
    /* Calculate the gain factor for an impedance of 47kohms. */
    gainFactor = AD5933_CalculateGainFactor(820, AD5933_FUNCTION_REPEAT_FREQ);
    Serial.print("gainFactor = ");
    Serial.print(gainFactor);
      
}

void loop() {
  // put your main code here, to run repeatedly:
  /* Calculates the impedance between the VOUT and VIN pins. */
  impedance = AD5933_CalculateImpedance(gainFactor, AD5933_FUNCTION_REPEAT_FREQ);
  impedanceK = (float)impedance;
  impedanceK /= 1000;
  Serial.println(impedanceK);
}
