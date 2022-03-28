/*
 * main.c
 *
 *  Created on: 2022 Mar 03 16:13:51
 *  Author: RNSANTELER
 */

#include "DAVE.h" //Declarations from DAVE Code Generation (includes SFR declaration)


// Constant settings (must be set hard-coded)
#define BTN_STD_PRESS_DURATION		60							// The minimum duration of a button press that will be registered as such (debouncing)
#define BTN_LONG_PRESS_DURATION		1000						// The minimum duration of a long button press that will be registered as such (debouncing)
#define BTN_LONGEST_PRESS_DURATION	4000						// The maximum duration of a button press
#define ADC_THRESHOLD_MAX			4095						// Maximum ADC value. Note: 1023 can be divided by 1, 3, 11, 31, 33, 93, 341 without decimals
#define ADC_THRESHOLD_INCREMENT		(ADC_THRESHOLD_MAX / 33)	// Value added/subtracted when adjusting threshold. 33 means there are 33 steps for setting thresholds
#define RELAY_LATCHTIME_MAX			60000						// Maximum configurable time that the threshold must be exceeded to trigger a state change of the relay
#define RELAY_LATCHTIME_INCREMENT	500							// Value added/subtracted when adjusting time
#define LED_PULSE_SHORT				200							// In ms. Duration of a short led pulse used for led pattern "number"
#define LED_PULSE_LONG				1100						// In ms. Duration of a long led pulse used for led pattern "number"
#define PWM_FULL_ON					PWM_CCU4_SYM_DUTY_MIN		// Integer that represents the lowest possible duty cycle of PWM
#define PWM_FULL_OFF				PWM_CCU4_SYM_DUTY_MAX		// Integer that represents the highest possible duty cycle of PWM
#define TIMESTAMP_DEACTIVATED		UINT32_MAX

// Dynamic settings (can be changed by user)
uint16_t relay_threshold_latchtime = 500; // Time in ms that the threshold must stay exceeded in order to trigger a state change (=basically a filter)
int32_t ADC_upper_threshold = 3000;    // Upper threshold that the ADC value must be exceed to trigger a state change (must be held exceeded for relay_threshold_latchtime)
int32_t ADC_lower_threshold = 2500;    // Upper threshold that the ADC value must be exceed to trigger a state change (must be held exceeded for relay_threshold_latchtime)


// State machines
typedef enum {USB_1_active, USB_2_active, USB_inactive} USB_states;
typedef enum {RELAY_HIGH, RELAY_LOW} relay_states;
typedef enum {SETUP_IDLE, SETUP_UPPER_TH, SETUP_LOWER_TH, SETUP_TIME_TH} setup_states;
USB_states USB_state;
relay_states relay_state;
setup_states setup_state;
typedef enum {LED_OFF, LED_ON, LED_NUMBER, LED_FADE_DOWN, LED_FADE_UP, LED_MATCH_RELAY_STATE} LED_patterns;
LED_patterns led_status_pattern = LED_OFF;
LED_patterns led_status_pattern_last = LED_OFF;
typedef enum {LED_PATTERN_CONTINUOUS, LED_PATTERN_SINGLE} LED_pattern_modes;
LED_pattern_modes led_pattern_mode = LED_PATTERN_CONTINUOUS;
LED_patterns led_status_pattern_after_single = LED_OFF; 	// Defines to what pattern will be switched after a LED_PATTERN_SINGLE execution
uint16_t led_number = 0;
uint16_t led_fadetime = 1500; // Time of one fade from one extreme to the other
uint16_t led_fadesteps = 1000; // Number of steps used to fade led

// Buttons
typedef enum {BTNPRESS_NOT, BTNPRESS_STD, BTNPRESS_LONG, BTNPRESS_LONGEST} button_press_states;
button_press_states buttonpress_usb = BTNPRESS_NOT;
button_press_states buttonpress_up = BTNPRESS_NOT;
button_press_states buttonpress_down = BTNPRESS_NOT;
uint32_t button_usb_pressed_timestamp = 0; // If a button is pressed (or state = HIGH) the current time will be saved (used to calculate duration)
uint32_t button_up_pressed_timestamp = 0;
uint32_t button_down_pressed_timestamp = 0;
uint16_t button_usb_pressed_duration = 0; // If a button is released (or state = LOW) duration is calculated (in ms)
uint16_t button_up_pressed_duration = 0;
uint16_t button_down_pressed_duration = 0;
#define SW_ON 0
#define SW_OFF 1

// ADC
uint32_t ADC_val_current = 0;
uint32_t ADC_val_upper_thres_exceed_timestamp = 0; // If this is 0 the threshold is not exceeded. If threshold is exceeded this marks the point when it got started to be exceeded
uint32_t ADC_val_lower_thres_exceed_timestamp = 0;

// Debug
int systime_debug = 0;



//****************************************************************************
// delay_ms - millisecond delay function
//****************************************************************************
void delay_ms(uint32_t ms){
	uint32_t targetMicroSec = SYSTIMER_GetTime() + (ms*1000);
	while(targetMicroSec > SYSTIMER_GetTime())
		__NOP(); // do nothing
}

//****************************************************************************
// reset_status_led_to_relay_state - gets state of relay and sets relay led according
//****************************************************************************
void reset_status_led_to_relay_state(){
	uint32_t state = DIGITAL_IO_GetInput(&IO_RELAY);
	if(state == 0){
		led_status_pattern = LED_OFF;
		PWM_CCU4_SetDutyCycle(&PWM_CCU4_LED_STATUS, PWM_FULL_OFF);
	}
	else{
		led_status_pattern = LED_ON;
		PWM_CCU4_SetDutyCycle(&PWM_CCU4_LED_STATUS, PWM_FULL_ON);
	}
}

//****************************************************************************
// manage_status_led - blink the status led according to the given pattern and (user interface)
//****************************************************************************
void manage_status_led(){
	static uint16_t led_pattern_state;
	static uint32_t led_pattern_state_timestamp;
	static uint16_t led_pattern_state_length;

	static uint16_t fade_duty_step;

	// Check target pattern an initiate
	if(led_status_pattern != led_status_pattern_last){
		switch (led_status_pattern){
			case LED_OFF:
				PWM_CCU4_SetDutyCycle(&PWM_CCU4_LED_STATUS, PWM_FULL_OFF);
				break;
			case LED_ON:
				PWM_CCU4_SetDutyCycle(&PWM_CCU4_LED_STATUS, PWM_FULL_ON);
				break;
			case LED_NUMBER:
				if(led_number >= 1){
					led_pattern_state_timestamp = SYSTIMER_GetTime();
					led_pattern_state_length = LED_PULSE_SHORT;
					PWM_CCU4_SetDutyCycle(&PWM_CCU4_LED_STATUS, PWM_FULL_OFF);
					led_pattern_state = 0;
				}
				break;
			case LED_FADE_DOWN:
				if(led_fadetime > 0){
					led_pattern_state_timestamp = SYSTIMER_GetTime();
					led_pattern_state_length = led_fadetime/led_fadesteps;
					fade_duty_step = PWM_FULL_OFF/led_fadesteps;
					PWM_CCU4_SetDutyCycle(&PWM_CCU4_LED_STATUS, PWM_FULL_ON);
					led_pattern_state = 0;
				}
				break;
			case LED_FADE_UP:
				if(led_fadetime > 0){
					led_pattern_state_timestamp = SYSTIMER_GetTime();
					led_pattern_state_length = led_fadetime/led_fadesteps;
					fade_duty_step = PWM_FULL_OFF/led_fadesteps;
					PWM_CCU4_SetDutyCycle(&PWM_CCU4_LED_STATUS, PWM_FULL_OFF);
					led_pattern_state = 0;
				}
				break;
			case LED_MATCH_RELAY_STATE:
				reset_status_led_to_relay_state();
				break;
		}
		led_status_pattern_last = led_status_pattern;
	}

	// Handle LED_NUMBER pattern
	if(led_status_pattern == LED_NUMBER){
		if((SYSTIMER_GetTime() - led_pattern_state_timestamp) / 1000 >= led_pattern_state_length){
			// Next state
			led_pattern_state++;

			// Check if LED must be powered on or off for this state
			if(led_pattern_state % 2)
				PWM_CCU4_SetDutyCycle(&PWM_CCU4_LED_STATUS, PWM_FULL_ON);
			else
				PWM_CCU4_SetDutyCycle(&PWM_CCU4_LED_STATUS, PWM_FULL_OFF);

			// Detect last low phase and make it longer
			if(led_pattern_state == (led_number*2) && led_pattern_mode == LED_PATTERN_CONTINUOUS)
				led_pattern_state_length = LED_PULSE_LONG;
			else
				led_pattern_state_length = LED_PULSE_SHORT;

			// Store current time
			led_pattern_state_timestamp = SYSTIMER_GetTime();

			// Check if LED pattern is finished
			if(led_pattern_state > led_number*2){
				if(led_pattern_mode == LED_PATTERN_CONTINUOUS) // Repeat pattern
					led_pattern_state = 1;
				else if(led_pattern_mode == LED_PATTERN_SINGLE){ // Reset led and pattern mode
					led_pattern_mode = LED_PATTERN_CONTINUOUS;
					led_status_pattern = led_status_pattern_after_single;
				}
			}
		}
	}

	// Handle LED_FADE_UP pattern
	else if(led_status_pattern == LED_FADE_DOWN){
		if((SYSTIMER_GetTime() - led_pattern_state_timestamp) / 1000 >= led_pattern_state_length){
			// Set intensity of led to a level based on maximum value and current step
			PWM_CCU4_SetDutyCycle(&PWM_CCU4_LED_STATUS, (led_pattern_state*fade_duty_step) + PWM_FULL_ON);

			// Store current time
			led_pattern_state_timestamp = SYSTIMER_GetTime();

			// Next state
			led_pattern_state++;

			// Make last state longer
			if(led_pattern_state == led_fadesteps-1)
				led_pattern_state_length = led_pattern_state_length + 400;

			// Check if LED pattern is finished
			if(led_pattern_state >= led_fadesteps){
				if(led_pattern_mode == LED_PATTERN_CONTINUOUS){ // Repeat pattern
					led_pattern_state_length = led_pattern_state_length - 400;
					led_pattern_state = 0;
				}
				else if(led_pattern_mode == LED_PATTERN_SINGLE){ // Reset led and pattern mode
					led_pattern_mode = LED_PATTERN_CONTINUOUS;
					led_status_pattern = led_status_pattern_after_single;
				}
			}
		}
	}

	// Handle LED_FADE_DOWN pattern
	else if(led_status_pattern == LED_FADE_UP){
		if((SYSTIMER_GetTime() - led_pattern_state_timestamp) / 1000 >= led_pattern_state_length){
			// Set intensity of led to a level based on maximum value and current step
			PWM_CCU4_SetDutyCycle(&PWM_CCU4_LED_STATUS, PWM_FULL_OFF - (led_pattern_state*fade_duty_step) );

			// Store current time
			led_pattern_state_timestamp = SYSTIMER_GetTime();

			// Next state
			led_pattern_state++;

			// Make last state longer
			if(led_pattern_state == led_fadesteps-1)
				led_pattern_state_length = led_pattern_state_length + 400;

			// Check if LED pattern is finished
			if(led_pattern_state >= led_fadesteps){
				if(led_pattern_mode == LED_PATTERN_CONTINUOUS){ // Repeat pattern
					led_pattern_state_length = led_pattern_state_length - 400;
					led_pattern_state = 0;
				}
				else if(led_pattern_mode == LED_PATTERN_SINGLE){ // Reset led and pattern mode
					led_pattern_mode = LED_PATTERN_CONTINUOUS;
					led_status_pattern = led_status_pattern_after_single;
				}
			}
		}
	}
}

//****************************************************************************
// main - function to manage, debounce and interpret button presses
//****************************************************************************
void manage_buttons(void)
{
	/// Detect start of press and save current system time
	if(button_usb_pressed_timestamp == 0 && DIGITAL_IO_GetInput(&IO_SW_USB) == SW_ON)
		button_usb_pressed_timestamp = SYSTIMER_GetTime();
	if(button_up_pressed_timestamp == 0 && DIGITAL_IO_GetInput(&IO_SW_UP) == SW_ON)
		button_up_pressed_timestamp = SYSTIMER_GetTime();
	if(button_down_pressed_timestamp == 0 && DIGITAL_IO_GetInput(&IO_SW_DOWN) == SW_ON)
		button_down_pressed_timestamp = SYSTIMER_GetTime();

	// USB BUTTON: If a press in ongoing and release is detected, calculate time difference
	if(button_usb_pressed_timestamp != 0 && DIGITAL_IO_GetInput(&IO_SW_USB) == SW_OFF){
		button_usb_pressed_duration = (SYSTIMER_GetTime() - button_usb_pressed_timestamp) / 1000; // convert us to ms
		button_usb_pressed_timestamp = 0;
		// Interpret button press and activate "button pressed" marker
		if(button_usb_pressed_duration >= BTN_LONGEST_PRESS_DURATION)
			buttonpress_usb = BTNPRESS_NOT; // In this case the press is already handled
		else if(button_usb_pressed_duration >= BTN_LONG_PRESS_DURATION)
			buttonpress_usb = BTNPRESS_LONG;
		else if(button_usb_pressed_duration >= BTN_STD_PRESS_DURATION)
			buttonpress_usb = BTNPRESS_STD;
	}
	// USB BUTTON: If press is to long reset (simulate that press ended)
	else if(button_usb_pressed_timestamp != 0 && button_usb_pressed_timestamp != TIMESTAMP_DEACTIVATED && ((SYSTIMER_GetTime() - button_usb_pressed_timestamp) / 1000) > BTN_LONGEST_PRESS_DURATION){
		button_usb_pressed_timestamp = TIMESTAMP_DEACTIVATED; // deactivate timestamp till button is released
		buttonpress_usb = BTNPRESS_LONGEST;
	}

	// UP BUTTON: If a press in ongoing and release is detected, calculate time difference
	if(button_up_pressed_timestamp != 0 && DIGITAL_IO_GetInput(&IO_SW_UP) == SW_OFF){
		button_up_pressed_duration = (SYSTIMER_GetTime() - button_up_pressed_timestamp) / 1000; // convert us to ms
		button_up_pressed_timestamp = 0;
		// Interpret button press and activate "button pressed" marker
		if(button_up_pressed_duration >= BTN_LONGEST_PRESS_DURATION)
			buttonpress_up = BTNPRESS_NOT; // In this case the press is already handled
		else if(button_up_pressed_duration >= BTN_LONG_PRESS_DURATION)
			buttonpress_up = BTNPRESS_LONG;
		else if(button_up_pressed_duration >= BTN_STD_PRESS_DURATION)
			buttonpress_up = BTNPRESS_STD;

	}
	// UP BUTTON: If press is to long reset (simulate that press ended)
	else if(button_up_pressed_timestamp != 0 && button_up_pressed_timestamp != TIMESTAMP_DEACTIVATED && ((SYSTIMER_GetTime() - button_up_pressed_timestamp) / 1000) > BTN_LONGEST_PRESS_DURATION){
		button_up_pressed_timestamp = TIMESTAMP_DEACTIVATED;
		buttonpress_up = BTNPRESS_LONGEST;
	}

	// DOWN BUTTON: If a press in ongoing and release is detected, calculate time difference
	if(button_down_pressed_timestamp != 0 && DIGITAL_IO_GetInput(&IO_SW_DOWN) == SW_OFF){
		button_down_pressed_duration = (SYSTIMER_GetTime() - button_down_pressed_timestamp) / 1000; // convert us to ms
		button_down_pressed_timestamp = 0;
		// Interpret button press and activate "button pressed" marker. The code that is reacting to it must reset it afterwards!
		if(button_down_pressed_duration >= BTN_LONGEST_PRESS_DURATION)
			buttonpress_down = BTNPRESS_NOT; // In this case the press is already handled
		else if(button_down_pressed_duration >= BTN_LONG_PRESS_DURATION)
			buttonpress_down = BTNPRESS_LONG;
		else if(button_down_pressed_duration >= BTN_STD_PRESS_DURATION)
			buttonpress_down = BTNPRESS_STD;
	}
	// DOWN BUTTON: If press is to long reset (simulate that press ended)
	else if(button_down_pressed_timestamp != 0 && button_down_pressed_timestamp != TIMESTAMP_DEACTIVATED && ((SYSTIMER_GetTime() - button_down_pressed_timestamp) / 1000) > BTN_LONGEST_PRESS_DURATION){
		button_down_pressed_timestamp = TIMESTAMP_DEACTIVATED;
		buttonpress_down = BTNPRESS_LONGEST;
	}
}

//****************************************************************************
// main - primary loop function
//****************************************************************************
int main(void)
{
	// Initialization of DAVE APPs
	DAVE_STATUS_t status;
	status = DAVE_Init();

	// Error routine
	if (status != DAVE_STATUS_SUCCESS) {
		while(1U){
			DIGITAL_IO_SetOutputLow(&IO_LED_USB1);
			DIGITAL_IO_SetOutputLow(&IO_LED_USB2);
			DIGITAL_IO_SetOutputLow(&IO_LED_R_STATUS);
			delay_ms(500);
			DIGITAL_IO_SetOutputHigh(&IO_LED_USB1);
			DIGITAL_IO_SetOutputHigh(&IO_LED_USB2);
			DIGITAL_IO_SetOutputHigh(&IO_LED_R_STATUS);
			delay_ms(500);
		}
	}

	/// - Set initial state -
	// Enable USB chip and switch to USB1
	DIGITAL_IO_SetOutputLow(&IO_USB_SI);
	DIGITAL_IO_SetOutputLow(&IO_USB_OE);
	// Enable USB1
	DIGITAL_IO_SetOutputHigh(&IO_USBPWR_1);
	DIGITAL_IO_SetOutputLow(&IO_LED_USB1);
	// Disable USB2
	DIGITAL_IO_SetOutputLow(&IO_USBPWR_2);
	DIGITAL_IO_SetOutputHigh(&IO_LED_USB2);
	// Disable Relay and set LED off
	DIGITAL_IO_SetOutputLow(&IO_RELAY);
	PWM_CCU4_SetDutyCycle(&PWM_CCU4_LED_STATUS, PWM_FULL_OFF);
	// Initialize next value conversion
	ADC_MEASUREMENT_StartConversion(&ADC_SENSOR);

	int main_loop_count = 0;

	// Main loop
	while(1U)
	{
		// - Status LED handling -
		manage_status_led();
		main_loop_count++;
		systime_debug = SYSTIMER_GetTime();

		//// - Button handling -
		manage_buttons();

		/// - USB Channel handling -
		switch (USB_state){
			case USB_1_active:
				// State code - none atm

				// Transition statement
				if(buttonpress_usb == BTNPRESS_STD){
					DIGITAL_IO_SetOutputLow(&IO_USBPWR_1);
					DIGITAL_IO_SetOutputHigh(&IO_USB_SI);
					DIGITAL_IO_SetOutputLow(&IO_LED_USB2);
					DIGITAL_IO_SetOutputHigh(&IO_LED_USB1);
					DIGITAL_IO_SetOutputHigh(&IO_USBPWR_2);
					buttonpress_usb = BTNPRESS_NOT;

					USB_state = USB_2_active;
				}
				break;
			case USB_2_active:
				// State code - none atm

				// Transition statement
				if(buttonpress_usb == BTNPRESS_STD){
					DIGITAL_IO_SetOutputLow(&IO_USBPWR_2);
					DIGITAL_IO_SetOutputLow(&IO_USB_SI);
					DIGITAL_IO_SetOutputLow(&IO_LED_USB1);
					DIGITAL_IO_SetOutputHigh(&IO_LED_USB2);
					DIGITAL_IO_SetOutputHigh(&IO_USBPWR_1);
					buttonpress_usb = BTNPRESS_NOT;

					USB_state = USB_1_active;
				}
				break;
			case USB_inactive:
				// Currently not implemented!
				break;
		}

		/// - Relay handling -
		// Check for state change triggers based on current state
		switch (relay_state){
			case RELAY_LOW:
				// State code
				// Check if upper threshold is exceeded. If it is and timestamp is not already set - save timestamp. If timestamp is already saved and threshold is not exceeded anymore reset timestamp
				if     (ADC_val_upper_thres_exceed_timestamp == 0 && ADC_val_current > ADC_upper_threshold){
					ADC_val_upper_thres_exceed_timestamp = SYSTIMER_GetTime();
				}
				else if(ADC_val_upper_thres_exceed_timestamp != 0 && ADC_val_current < ADC_upper_threshold){
					ADC_val_upper_thres_exceed_timestamp = 0;
				}

				// Transition statement
				// Check if threshold are exceeded long enough to trigger a switch
				if(ADC_val_upper_thres_exceed_timestamp != 0){
					uint16_t upperThresholdExceedDuration = (SYSTIMER_GetTime() - ADC_val_upper_thres_exceed_timestamp)/1000;
					if(upperThresholdExceedDuration > relay_threshold_latchtime){
						relay_state = RELAY_HIGH;
						DIGITAL_IO_SetOutputHigh(&IO_RELAY);
						ADC_val_upper_thres_exceed_timestamp = 0;
						if(setup_state == SETUP_IDLE)
							reset_status_led_to_relay_state();
					}
				}
				break;
			case RELAY_HIGH:
				// State code
				// Check if lower threshold is exceeded. If it is and timestamp is not already set - save timestamp. If timestamp is already saved and threshold is not exceeded anymore reset timestamp
				if(ADC_val_lower_thres_exceed_timestamp == 0 && ADC_val_current < ADC_lower_threshold){
					ADC_val_lower_thres_exceed_timestamp = SYSTIMER_GetTime();
				}
				else if(ADC_val_lower_thres_exceed_timestamp != 0 && ADC_val_current > ADC_lower_threshold){
					ADC_val_lower_thres_exceed_timestamp = 0;
				}

				// Transition statement
				// Check if threshold are exceeded long enough to trigger a switch
				if(ADC_val_lower_thres_exceed_timestamp != 0){
					uint16_t lowerThresholdExceedDuration = (SYSTIMER_GetTime() - ADC_val_lower_thres_exceed_timestamp)/1000;
					if(lowerThresholdExceedDuration > relay_threshold_latchtime){
						relay_state = RELAY_LOW;
						DIGITAL_IO_SetOutputLow(&IO_RELAY);
						ADC_val_lower_thres_exceed_timestamp = 0;
						if(setup_state == SETUP_IDLE)
							reset_status_led_to_relay_state();
					}
				}
				break;
		}
		// Init next value conversion
		ADC_MEASUREMENT_StartConversion(&ADC_SENSOR);

		/// - Relay settings handling - Todo auto exit menus after time?, led signal when reaching max?, upper threshold cant be lower than lower threshold?
		switch(setup_state){
			case SETUP_IDLE:
				/// Interpret button press and change to according setup sub-menu (state)
				// A long  press of up or down brings system in time setup menu
				// A short press of up         brings system in upper threshold setup menu
				// A short press of down       brings system in lower threshold setup menu
				if(buttonpress_up == BTNPRESS_LONG || buttonpress_down == BTNPRESS_LONG){
					setup_state = SETUP_TIME_TH;
					led_status_pattern = LED_NUMBER;
					led_number = 1;
				}
				else if(buttonpress_up == BTNPRESS_STD){
					setup_state = SETUP_UPPER_TH;
					led_status_pattern = LED_FADE_UP;
					//led_status_pattern = LED_NUMBER;
					//led_number = 5;
				}
				else if(buttonpress_down == BTNPRESS_STD){
					setup_state = SETUP_LOWER_TH;
					led_status_pattern = LED_FADE_DOWN;
					//led_status_pattern = LED_NUMBER;
					//led_number = 3;
				}
				break;
			case SETUP_UPPER_TH:
				// Blink relay LED

				/// Interpret button press:
				// A long  press of up or down brings system back to setup idle
				// A short press of up         increases the upper threshold value
				// A short press of down       decreases the upper threshold value
				// A longest press of up saves the current ADC value as threshold
				if(buttonpress_up == BTNPRESS_LONG || buttonpress_down == BTNPRESS_LONG){
					setup_state = SETUP_IDLE;
					led_status_pattern = LED_MATCH_RELAY_STATE;
				}
				else if(buttonpress_up == BTNPRESS_STD){ // Increase
					ADC_upper_threshold += ADC_THRESHOLD_INCREMENT;
					// If maximum is reached blink led 2 times, then continue fading
					if(ADC_upper_threshold > ADC_THRESHOLD_MAX){
						ADC_upper_threshold = ADC_THRESHOLD_MAX;
						led_number = 2;
						led_status_pattern = LED_NUMBER;
						led_pattern_mode = LED_PATTERN_SINGLE;
						led_status_pattern_after_single = LED_FADE_UP;
					}
				}
				else if(buttonpress_down == BTNPRESS_STD){ // Decrease
					ADC_upper_threshold -= ADC_THRESHOLD_INCREMENT;
					// If minimum is reached blink led 2 times, then continue fading
					if(ADC_upper_threshold <= 0){
						ADC_upper_threshold = 0;
						led_number = 2;
						led_status_pattern = LED_NUMBER;
						led_pattern_mode = LED_PATTERN_SINGLE;
						led_status_pattern_after_single = LED_FADE_UP;
					}
					//if(ADC_upper_threshold <= ADC_lower_threshold)
						//ADC_upper_threshold = ADC_lower_threshold;
				}
				else if(buttonpress_up == BTNPRESS_LONGEST){
					// Save current ADC value as threshold and exit setup menu
					ADC_upper_threshold = ADC_val_current;
					setup_state = SETUP_IDLE;
					// Blink LED 3 times (user info) and return to operation where led matches the state of the relay
					led_number = 3;
					led_status_pattern = LED_NUMBER;
					led_pattern_mode = LED_PATTERN_SINGLE;
					led_status_pattern_after_single = LED_MATCH_RELAY_STATE;
				}
				break;
			case SETUP_LOWER_TH:
				// Blink relay LED

				/// Interpret button press:
				// A long  press of up or down brings system back to setup idle
				// A short press of up         increases the lower threshold value
				// A short press of down       decreases the lower threshold value
				// A longest press of down saves the current ADC value as threshold
				if(buttonpress_up == BTNPRESS_LONG || buttonpress_down == BTNPRESS_LONG){
					setup_state = SETUP_IDLE;
					led_status_pattern = LED_MATCH_RELAY_STATE;
				}
				else if(buttonpress_up == BTNPRESS_STD){ // Increase
					ADC_lower_threshold += ADC_THRESHOLD_INCREMENT;
					// If maximum is reached blink led 2 times, then continue fading
					if(ADC_lower_threshold > ADC_THRESHOLD_MAX){
						ADC_lower_threshold = ADC_THRESHOLD_MAX;
						led_number = 2;
						led_status_pattern = LED_NUMBER;
						led_pattern_mode = LED_PATTERN_SINGLE;
						led_status_pattern_after_single = LED_FADE_DOWN;
					}
				}
				else if(buttonpress_down == BTNPRESS_STD){ // Decrease
					ADC_lower_threshold -= ADC_THRESHOLD_INCREMENT;
					// If minimum is reached blink led 2 times, then continue fading
					if(ADC_lower_threshold <= 0){
						ADC_lower_threshold = 0;
						led_number = 2;
						led_status_pattern = LED_NUMBER;
						led_pattern_mode = LED_PATTERN_SINGLE;
						led_status_pattern_after_single = LED_FADE_DOWN;
					}
				}
				else if(buttonpress_down == BTNPRESS_LONGEST){
					// Save current ADC value as threshold
					ADC_lower_threshold = ADC_val_current;
					setup_state = SETUP_IDLE;
					// Blink LED 3 times (user info) and return to operation where led matches the state of the relay
					led_number = 3;
					led_status_pattern = LED_NUMBER;
					led_pattern_mode = LED_PATTERN_SINGLE;
					led_status_pattern_after_single = LED_MATCH_RELAY_STATE;
				}
				break;
			case SETUP_TIME_TH:
				/// Interpret button press:
				// A long  press of up or down brings system back to setup idle
				// A short press of up         increases the threshold exceed time
				// A short press of down       decreases the threshold exceed time
				if(buttonpress_up == BTNPRESS_LONG || buttonpress_down == BTNPRESS_LONG){
					setup_state = SETUP_IDLE;
					led_status_pattern = LED_MATCH_RELAY_STATE;
				}
				else if(buttonpress_up == BTNPRESS_STD){
					relay_threshold_latchtime += RELAY_LATCHTIME_INCREMENT;
					if(relay_threshold_latchtime > RELAY_LATCHTIME_MAX){
						relay_threshold_latchtime = RELAY_LATCHTIME_MAX;
						led_number = 2;
						led_status_pattern = LED_NUMBER;
						led_pattern_mode = LED_PATTERN_SINGLE;
						led_status_pattern_after_single = LED_FADE_DOWN;
					}
				}
				else if(buttonpress_down == BTNPRESS_STD){
					relay_threshold_latchtime -= RELAY_LATCHTIME_INCREMENT;
					if(relay_threshold_latchtime <= 0){
						relay_threshold_latchtime = 0;
						led_number = 2;
						led_status_pattern = LED_NUMBER;
						led_pattern_mode = LED_PATTERN_SINGLE;
						led_status_pattern_after_single = LED_FADE_DOWN;
					}
				}
				break;
		}

		// Reset all button presses
		buttonpress_usb = BTNPRESS_NOT;
		buttonpress_up = BTNPRESS_NOT;
		buttonpress_down = BTNPRESS_NOT;
	}
}



//void Adc_Measurement_Handler()
//{
////	#if(UC_SERIES == XMC11)
////		ADC_val_current = ADC_MEASUREMENT_GetGlobalResult();
////		ADC_val_current = ADC_val_current >> ((uint32_t)ADC_SENSOR.iclass_config_handle->conversion_mode_standard * (uint32_t)2);
////	#endif
//}

int meas_invalid_count = 0;

void Adc_Measurement_Handler()
{
	//uint8_t channel_num;
	//uint8_t group_num;
	uint32_t adc_register;

	#if(UC_SERIES == XMC11)
	adc_register = ADC_MEASUREMENT_GetGlobalDetailedResult();
	#endif

	if((bool)(adc_register >> VADC_GLOBRES_VF_Pos))
	{
		//channel_num = (adc_register & VADC_GLOBRES_CHNR_Msk) >> VADC_GLOBRES_CHNR_Pos;
		//group_num = ADC_MEASUREMENT_Channel_A.group_index;
		ADC_val_current = (adc_register & VADC_GLOBRES_RESULT_Msk) >> ((uint32_t)(ADC_SENSOR.iclass_config_handle->conversion_mode_standard) * (uint32_t)2);
	}
	else{
		meas_invalid_count++;
	}

}

