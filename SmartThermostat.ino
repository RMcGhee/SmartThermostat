#include <config.h>
#include <ds3231.h>
#include <Wire.h>
#define DS3231_I2C_ADDRESS 0x68
#define TMP36_BASE 50

// set the greatest temp drop (in fahrenheit)
#define MAX_TEMP_DROP 10
// set the max internal temp of the rig (in celsius)
#define MAX_INTERNAL_TEMP 70.0
#define MIN_MILLIS_STATE_CHANGE 60000
#define SMOOTH_FACTOR 4

//for Arduino Pro Mini
//pin A4 is SDA (reset button at bottom, just right of ATM328)
//pin A5 is SCL

int ap_temp_pot = 0;
int ap_start_pot = 1;
int ap_end_pot = 2;
int ap_temp = 6;
int dp_blink = 13;
int dp_relay = 10;
int state_dp_relay = 0;
unsigned long millis_last_state_change;

void setup() {
  // put your setup code here, to run once:
  pinMode(dp_blink, OUTPUT);
  pinMode(dp_relay, OUTPUT);
  digitalWrite(dp_blink, LOW);
  Wire.begin();
  //Serial.begin(9600);
  //while(!Serial);
  millis_last_state_change = millis()-50000;

}

void loop() {
  static float RTC_temp, base_temp = 60.0, set_temp = 60.0, env_temp_f = 0.0;;
  static int minutes_difference[3], pot_read = 0;
  static float time_delta_temp = 0.0;
  static boolean can_state_change = true;
  static int temp_range = 20;
  RTC_temp = DS3231_get_treg();

  if(RTC_temp > MAX_INTERNAL_TEMP){
    // thermal shutoff, turn off the relay and delay 60 seconds. 
    digitalWrite(dp_relay, LOW);
    delay(60000);
  }
  if(in_time_window(minutes_difference)){
    time_delta_temp = get_temp_drop(minutes_difference);
    can_state_change = ( (unsigned int) (millis() - millis_last_state_change) > MIN_MILLIS_STATE_CHANGE);

    pot_read += (analogRead(ap_temp_pot) - pot_read)/SMOOTH_FACTOR;
    env_temp_f = get_env_temp_f((boolean) state_dp_relay);
    set_temp = base_temp + ((float) (pot_read/1023.0) * temp_range);

    set_temp -= time_delta_temp;

    //turn on conditions
    if(env_temp_f < set_temp && can_state_change && !state_dp_relay){
      digitalWrite(dp_relay, HIGH);
      state_dp_relay = HIGH;
      millis_last_state_change = millis();

    // turn off conditions  
    }else if(env_temp_f >= set_temp && can_state_change && state_dp_relay){
      digitalWrite(dp_relay, LOW);
      state_dp_relay = LOW;
      millis_last_state_change = millis();
    }
    /*
    Serial.print("1 env_temp_f: ");
    Serial.println(env_temp_f);
    Serial.print("Set temp: ");
    Serial.println(set_temp);
    Serial.print("time_delta_temp: ");
    Serial.println(time_delta_temp);
    Serial.print("\n");
    */
    delay(2000);
  }else{
    digitalWrite(dp_relay, LOW);
    state_dp_relay = LOW;
    delay(2000);
  }
}

// checks if in time window.
boolean in_time_window(int *mins){
  static struct ts c_time, s_time, e_time;
  static int *min_start_end = mins, *min_start_now = (mins+1), *min_now_end = (mins+2);
  static int max_minutes = 0;
  static float time_delta_temp;
  static bool after_start, before_end;
  read_DS3231_time(&c_time);
  
  //update start and end times by reading appropriate pots.
  set_start_end_time(&s_time, &e_time);
  if(((c_time.hour == s_time.hour) && (c_time.min >= s_time.min)) || 
    (c_time.hour > s_time.hour)){
    after_start = true;
  }else{
    after_start = false;
  }

  if(((c_time.hour == e_time.hour) && (c_time.min <= e_time.min)) || 
    (c_time.hour < e_time.hour)){
    before_end = true;
  }else{
    before_end = false;
  }

  //get distance in minutes between times.
  *min_start_now = min_diff(c_time, s_time);
  *min_start_end = min_diff(e_time, s_time);
  *min_now_end = min_diff(c_time, e_time);

  //If current time is after the start or before the end (works when start time is greater
  // than end time).
  if(after_start || before_end){
    return true;
  }
  return false;
}

float get_temp_drop(int* mins){
  //minutes_difference has min_start_end at 0, min_start_now at 1, min_now_end at 2.
  int *min_start_end = mins, *min_start_now = (mins+1), *min_now_end = (mins+2);
  static float time_delta_temp;
  
  time_delta_temp = (((float) *min_start_now/(*min_start_end)) * MAX_TEMP_DROP);
  
  //If the minutes left in the night is less than 75, raise temp to set_temp
  // to make waking more pleasant.
  if(*min_now_end < 75){
     time_delta_temp = 0.0;
  }
  return time_delta_temp;
}

// sets the start and end times from the pots.
void set_start_end_time(struct ts *t_start, struct ts *t_end){
  //viable range is from 1800 to 2400 for the start time. 
  //viable range is from 0500 to 1100 for the end time.
  //Pot can read in 0-1023.
  //get the pot value
  // hour offset specifies the start of the range of values for hour.
  // 1800 for start, 0500 for end.
  static int spread_72 = 0, start_hour_offset = 18, end_hour_offset = 5;
  static int start_pot = 0, end_pot = 0;

  start_pot += (analogRead(ap_start_pot) - start_pot) / SMOOTH_FACTOR;
  spread_72 = start_pot / 14;
  
  //if pot is near max values, just set to latest time.
  if(spread_72 >= 72){
    t_start->hour = start_hour_offset + 6;
    t_start->min = 0;
  }else{
    //divide by 12 to get the hour offset (6 hour range)
    t_start->hour = (spread_72 / 12) + start_hour_offset;
    //mod by 12 to get min in intervals of 5.
    t_start->min = (spread_72 % 12) * 5;
  }
  
  end_pot += (analogRead(ap_end_pot) - end_pot) / SMOOTH_FACTOR;
  spread_72 = end_pot / 14;
  if(spread_72 >= 72){
    t_end->hour = end_hour_offset + 6;
    t_end->min = 0;
  }else{
    //divide by 12 to get the hour offset (6 hour range)
    t_end->hour = (spread_72 / 12) + end_hour_offset;
    //mod by 12 to get min in intervals of 5.
    t_end->min = (spread_72 % 12) * 5;
  }

  /*
  Serial.print("Start: ");
  Serial.print(t_start->hour);
  Serial.print(":");
  Serial.println(t_start->min);
  Serial.print(t_end->hour);
  Serial.print(":");
  Serial.println(t_end->min);
  */
}

void read_DS3231_time(struct ts *t){
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(1); // set DS3231 register pointer to 00h
  Wire.endTransmission();
  Wire.requestFrom(DS3231_I2C_ADDRESS, 3);
  // request three bytes of data from DS3231 starting from register 01h
  t->min = bcdToDec(Wire.read());
  t->hour = bcdToDec(Wire.read() & 0x3f);
  /*
  Serial.print("Now: ");
  Serial.print(t->hour);
  Serial.print(":");
  Serial.println(t->min);
  */
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

float get_env_temp_f(boolean relay_on){
    static float raw_volt, smooth_volt = 0.79, temp_c;
    static int i;
    raw_volt = 0.0;
    for(i = 0; i < 30; i++){
      raw_volt += (float) analogRead(ap_temp);
    }
    raw_volt = raw_volt/30;
    raw_volt = raw_volt * (5.0/1024);
    smooth_volt += (raw_volt - smooth_volt) / SMOOTH_FACTOR;
    
    temp_c = (smooth_volt * 100) - TMP36_BASE;
    return ((temp_c * 9/5) + 32);
}

