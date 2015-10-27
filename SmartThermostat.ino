#include <config.h>
#include <ds3231.h>
#include <Wire.h>
#define DS3231_I2C_ADDRESS 0x68

// set the greatest temp drop (in fahrenheit)
#define MAX_TEMP_DROP 10
// set the max internal temp of the rig (in celsius)
#define MAX_INTERNAL_TEMP 70.0
#define MIN_MILLIS_STATE_CHANGE 5000

//for Arduino Pro Mini
//pin A4 is SDA (reset button at bottom, just right of ATM328)
//pin A5 is SCL

int ap_temp = 3;
int ap_temp_pot = 0;
int dp_blink = 13;
int dp_SSR = 13;
int room_temp = 0, pot_read = 0, temp_range = 20;
float base_temp = 60.0, set_temp = 60.0, time_delta_temp = 0.0, temp_f;
unsigned long millis_last_state_change;
boolean can_state_change = true;
int state_dp_SSR = 0;
struct ts c_time;

void setup() {
  // put your setup code here, to run once:
  pinMode(dp_blink, OUTPUT);
  digitalWrite(dp_blink, LOW);
  Wire.begin();
  //If attempting to use an arduino UBS serial interface to read serial monitor,
  // set the serial monitor rate to half that of the pro mini's rate below.
  // The arduino USB2Serial runs on a 16Mhz clock, whereas the pro mini runs
  // at 8Mhz (3.3V model)
  Serial.begin(9600);
  while(!Serial);
  millis_last_state_change = millis() - 10000;

  Serial.println("Begin the transmission of data:");
}

void loop() {
  static float RTC_temp;
  RTC_temp = DS3231_get_treg();

  if(RTC_temp > MAX_INTERNAL_TEMP){
    // thermal shutoff, turn off the SSR and delay 60 seconds. 
    digitalWrite(dp_SSR, LOW);
    delay(60000);
  }
  if(in_time_window(&time_delta_temp)){
    can_state_change = ( (unsigned int) (millis() - millis_last_state_change) > MIN_MILLIS_STATE_CHANGE);
    room_temp = analogRead(ap_temp);
    pot_read = analogRead(ap_temp_pot);
    temp_f = (float) room_temp/6.8;
    set_temp = base_temp + ((float) (pot_read/1023.0) * temp_range);

    // time_delta_temp is the lowering of the temperature to match the sleep/wake cycle.
    // time_delta_temp starts at 0 at beginning of night, goes up until its at
    // MAX_TEMP_DROP. Because set_temp is set each time this loop runs, time_delta_temp
    // is subtracted each time. 
    // temperature rises to set_temp for the last hour to make waking more pleasant.
    set_temp -= time_delta_temp;

    if(temp_f < set_temp && can_state_change && !state_dp_SSR){
      digitalWrite(dp_SSR, HIGH);
      state_dp_SSR = HIGH;
      millis_last_state_change = millis();

    // turn off conditions  
    }else if(temp_f >= set_temp && can_state_change && state_dp_SSR){
      digitalWrite(dp_SSR, LOW);
      state_dp_SSR = LOW;
      millis_last_state_change = millis();
    }
    Serial.print("1 temp_f: ");
    Serial.println(temp_f);
    Serial.print("Set temp: ");
    Serial.println(set_temp);
    Serial.print("time_delta_temp: ");
    Serial.println(time_delta_temp);

    Serial.print("\n");
    delay(1000);
  }else{
    digitalWrite(dp_SSR, LOW);
    delay(3000);
  }
}

// checks if in time window, also sets the temp drop due to sleep/wake cycle.
boolean in_time_window(float *time_delta_temp){
  static struct ts c_time;
  static struct ts s_time, e_time;
  static int diff_current_start;
  static int diff_end_start;
  static int diff_current_end;
  static int max_minutes = 0;
  read_DS3231_time(&c_time);
  
  //update start and end times by reading appropriate pots.
  set_start_end_time(&s_time, 1);
  set_start_end_time(&e_time, 0);

  //get distance to c_time from s_time and to e_time from s_time.
  diff_current_start = min_diff(c_time, s_time);
  diff_end_start = min_diff(e_time, s_time);
  diff_current_end = min_diff(c_time, e_time);

  //If current time is later than the start time.
  //Set the time_delta temp to appropriate temperature.
  if (diff_current_start < diff_end_start){
    *time_delta_temp = (((float) diff_current_start/diff_end_start) * MAX_TEMP_DROP);
    
    //If the minutes left in the night is less than 60, raise temp to set_temp
    // to make waking more pleasant.
    if(diff_current_end < 60){
      *time_delta_temp = 0.0;
    }
    return true;
  }
  return false;
}


// sets the start and end times from the pots.
void set_start_end_time(struct ts *t, int start){
  //viable range is from 1800 to 2400 for the start time. 
  //viable range is from 0500 to 1100 for the end time.
  //Pot can read in 0-1023.
  //get the pot value
  static int pot = 0, spread_72 = 0, hour_offset;
  static int ap_pot;
  // start time pot is on pin 1. end time pot is on pin 2.
  // hour offset specifies the start of the range of values for hour.
  // 1800 for start, 0500 for end.
  if(start){ 
    ap_pot = 1;
    hour_offset = 18;
  }
  else{ 
    ap_pot = 2;
    hour_offset = 5;
  }

  pot = analogRead(ap_pot);
  spread_72 = pot / 14;
  
  //if pot is near max values, just set to latest time.
  if(spread_72 >= 72){
    t->hour = hour_offset + 6;
    t->min = 0;
  }else{
    //divide by 12 to get the hour offset (6 hour range)
    t->hour = (spread_72 / 12) + hour_offset;
    //mod by 12 to get min in intervals of 5.
    t->min = (spread_72 % 12) * 5;
  }
  if(start){
    Serial.println("Start: ");
  }
  Serial.print(t->hour);
  Serial.print(":");
  Serial.println(t->min);
}

void read_DS3231_time(struct ts *t){
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(1); // set DS3231 register pointer to 00h
  Wire.endTransmission();
  Wire.requestFrom(DS3231_I2C_ADDRESS, 3);
  // request three bytes of data from DS3231 starting from register 01h
  t->min = bcdToDec(Wire.read());
  t->hour = bcdToDec(Wire.read() & 0x3f);
}

byte bcdToDec(byte val){
  return( (val/16*10) + (val%16) );
}

//Get the difference in minutes between end and start. Catches rollover at 2400.
int min_diff(struct ts end_time, struct ts start_time){
  static int result;
  result = end_time.min - start_time.min;
  if(start_time.hour > end_time.hour) result += 60*(24-start_time.hour+end_time.hour);
  else result += 60*(end_time.hour-start_time.hour);
  return result;
}
