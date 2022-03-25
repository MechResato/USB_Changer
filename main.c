/*
 * main.c
 *
 *  Created on: 2022 Mar 03 16:13:51
 *  Author: RNSANTELER
 */

#include "DAVE.h" //Declarations from DAVE Code Generation (includes SFR declaration)


// Constant settings (must be set hard-coded)
#define BTN_STD_PRESS_DURATION    60						// The minimum duration of a button press that will be registered as such (debouncing)
#define BTN_LONG_PRESS_DURATION   1000						// The minimum duration of a long button press that will be registered as such (debouncing)
#define ADC_THRESHOLD_MAX         4095						// Maximum ADC value. Note: 1023 can be divided by 1, 3, 11, 31, 33, 93, 341 without decimals
#define ADC_THRESHOLD_INCREMENT   (ADC_THRESHOLD_MAX / 33)	// Value added/subtracted when adjusting threshold. 33 means there are 33 steps for setting thresholds
#define RELAY_LATCHTIME_MAX       60000						// Maximum configurable time that the threshold must be exceeded to trigger a state change of the relay
#define RELAY_LATCHTIME_INCREMENT 500						// Value added/subtracted when adjusting time
#define LED_PULSE_SHORT 		  300						//
#define LED_PULSE_LONG	 		  1100						//

// Dynamic settings (can be changed by user)
uint16_t relay_threshold_latchtime = 50; // Time in ms that the threshold must stay exceeded in order to trigger a state change (=basically a filter)
int32_t ADC_upper_threshold = 3000;    // Upper threshold that the ADC value must be exceed to trigger a state change (must be held exceeded for relay_threshold_latchtime)
int32_t ADC_lower_threshold = 2500;    // Upper threshold that the ADC value must be exceed to trigger a state change (must be held exceeded for relay_threshold_latchtime)


// State machines
typedef enum {USB_1_active, USB_2_active, USB_inactive} USB_states;
typedef enum {RELAY_HIGH, RELAY_LOW} relay_states;
typedef enum {SETUP_IDLE, SETUP_UPPER_TH, SETUP_LOWER_TH, SETUP_TIME_TH} setup_states;
USB_states USB_state;
relay_states relay_state;
setup_states setup_state;
typedef enum {LED_OFF, LED_ON, LED_NUMBER} LED_patterns;
LED_patterns led_relay_pattern = LED_OFF;
LED_patterns led_relay_pattern_last = LED_OFF;
uint16_t led_number = 0;
//ToDo: implement led_relay_pattern and number. After setup return to current relay state.

// Buttons
typedef enum {BTNPRESS_NOT, BTNPRESS_STD, BTNPRESS_LONG} button_press_states;
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
int button_usb_pressed_debug = 0;
int button_usb_pressed_long_debug = 0;
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
// reset_relay_led - gets state of relay and sets relay led according
//****************************************************************************
void reset_relay_led(){
	uint32_t state = DIGITAL_IO_GetInput(&IO_RELAY);
	if(state == 0){
		led_relay_pattern = LED_OFF;
		DIGITAL_IO_SetOutputHigh(&IO_LED_RELAY);
	}
	else{
		led_relay_pattern = LED_ON;
		DIGITAL_IO_SetOutputLow(&IO_LED_RELAY);
	}
}

//****************************************************************************
// manage_leds -
//****************************************************************************

void manage_status_led(){
	static uint16_t led_number_state;
	static uint32_t led_number_state_timestamp;
	static uint16_t led_number_state_length;

	// Check target pattern an initiate
	if(led_relay_pattern != led_relay_pattern_last){
		switch (led_relay_pattern){
			case LED_OFF:
				DIGITAL_IO_SetOutputHigh(&IO_LED_RELAY);
				break;
			case LED_ON:
				DIGITAL_IO_SetOutputLow(&IO_LED_RELAY);
				break;
			case LED_NUMBER:
				if(led_number >= 1){
					led_number_state_timestamp = SYSTIMER_GetTime();
					led_number_state_length = LED_PULSE_SHORT;
					DIGITAL_IO_SetOutputHigh(&IO_LED_RELAY);
					led_number_state = 0;
				}
				break;
		}
		led_relay_pattern_last = led_relay_pattern;
	}

	// Handle LED_NUMBER pattern
	if(led_relay_pattern == LED_NUMBER){
		if((SYSTIMER_GetTime() - led_number_state_timestamp) / 1000 >= led_number_state_length){
			// Next state
			led_number_state++;

			// Check if LED must be powered on or off for this state
			if(led_number_state % 2)
				DIGITAL_IO_SetOutputLow(&IO_LED_RELAY);
			else
				DIGITAL_IO_SetOutputHigh(&IO_LED_RELAY);

			// Detect last low phase and make it longer
			if(led_number_state == (led_number*2))
				led_number_state_length = LED_PULSE_LONG;
			else
				led_number_state_length = LED_PULSE_SHORT;

			// Store current time
			led_number_state_timestamp = SYSTIMER_GetTime();

			// Check if LED pattern is finished
			if(led_number_state > led_number*2){
				//led_relay_pattern = LED_OFF;
				led_number_state = 1;
			}
		}
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
	DIGITAL_IO_SetOutputHigh(&IO_LED_RELAY);
	// Init next value conversion
	ADC_MEASUREMENT_StartConversion(&ADC_SENSOR);

	int main_loop_count = 0;

	// Main loop
	while(1U)
	{
		// - Status LED handling -
		manage_status_led();
		main_loop_count++;
		systime_debug = SYSTIMER_GetTime();

		/// - Button handling -
		// Detect press and save current system time
		if(button_usb_pressed_timestamp == 0 && DIGITAL_IO_GetInput(&IO_SW_USB) == SW_ON)
			button_usb_pressed_timestamp = SYSTIMER_GetTime();
		if(button_up_pressed_timestamp == 0 && DIGITAL_IO_GetInput(&IO_SW_UP) == SW_ON)
			button_up_pressed_timestamp = SYSTIMER_GetTime();
		if(button_down_pressed_timestamp == 0 && DIGITAL_IO_GetInput(&IO_SW_DOWN) == SW_ON)
			button_down_pressed_timestamp = SYSTIMER_GetTime();
		// If a press in ongoing and release is detected, calculate time difference
		if(button_usb_pressed_timestamp != 0 && DIGITAL_IO_GetInput(&IO_SW_USB) == SW_OFF){
			button_usb_pressed_duration = (SYSTIMER_GetTime() - button_usb_pressed_timestamp) / 1000; // convert us to ms
			button_usb_pressed_timestamp = 0;
			// Interpret button press and activate "button pressed" marker
			if(button_usb_pressed_duration >= BTN_LONG_PRESS_DURATION)
				buttonpress_usb = BTNPRESS_LONG;
			else if(button_usb_pressed_duration >= BTN_STD_PRESS_DURATION)
				buttonpress_usb = BTNPRESS_STD;


		}
		if(button_up_pressed_timestamp != 0 && DIGITAL_IO_GetInput(&IO_SW_UP) == SW_OFF){
			button_up_pressed_duration = (SYSTIMER_GetTime() - button_up_pressed_timestamp) / 1000; // convert us to ms
			button_up_pressed_timestamp = 0;
			// Interpret button press and activate "button pressed" marker
			if(button_up_pressed_duration >= BTN_LONG_PRESS_DURATION){
				buttonpress_up = BTNPRESS_LONG;
				button_usb_pressed_long_debug++;
			}
			else if(button_up_pressed_duration >= BTN_STD_PRESS_DURATION){
				buttonpress_up = BTNPRESS_STD;
				button_usb_pressed_debug++;
			}

		}
		if(button_down_pressed_timestamp != 0 && DIGITAL_IO_GetInput(&IO_SW_DOWN) == SW_OFF){
			button_down_pressed_duration = (SYSTIMER_GetTime() - button_down_pressed_timestamp) / 1000; // convert us to ms
			button_down_pressed_timestamp = 0;
			// Interpret button press and activate "button pressed" marker. The code that is reacting to it must reset it afterwards!
			if(button_down_pressed_duration >= BTN_LONG_PRESS_DURATION)
				buttonpress_down = BTNPRESS_LONG;
			else if(button_down_pressed_duration >= BTN_STD_PRESS_DURATION)
				buttonpress_down = BTNPRESS_STD;
		}

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
		// Get ADC value
		//ADC_val_current = ADC_MEASUREMENT_GetDetailedResult(&ADC_SENSOR);

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
						reset_relay_led();
						ADC_val_upper_thres_exceed_timestamp = 0;
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
						reset_relay_led();
						ADC_val_lower_thres_exceed_timestamp = 0;
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
					led_relay_pattern = LED_NUMBER;
					led_number = 1;
				}
				else if(buttonpress_up == BTNPRESS_STD){
					setup_state = SETUP_UPPER_TH;
					led_relay_pattern = LED_NUMBER;
					led_number = 2;
				}
				else if(buttonpress_down == BTNPRESS_STD){
					setup_state = SETUP_LOWER_TH;
					led_relay_pattern = LED_NUMBER;
					led_number = 3;
				}
				break;
			case SETUP_UPPER_TH:
				// Blink relay LED

				/// Interpret button press:
				// A long  press of up or down brings system back to setup idle
				// A short press of up         increases the upper threshold value
				// A short press of down       decreases the upper threshold value
				if(buttonpress_up == BTNPRESS_LONG || buttonpress_down == BTNPRESS_LONG){
					setup_state = SETUP_IDLE;
					reset_relay_led();
				}
				else if(buttonpress_up == BTNPRESS_STD){
					ADC_upper_threshold += ADC_THRESHOLD_INCREMENT;
					if(ADC_upper_threshold > ADC_THRESHOLD_MAX)
						ADC_upper_threshold = ADC_THRESHOLD_MAX;
				}
				else if(buttonpress_down == BTNPRESS_STD){
					ADC_upper_threshold -= ADC_THRESHOLD_INCREMENT;
					if(ADC_upper_threshold <= 0)
						ADC_upper_threshold = 0;
					//if(ADC_upper_threshold <= ADC_lower_threshold)
						//ADC_upper_threshold = ADC_lower_threshold;
				}
				break;
			case SETUP_LOWER_TH:
				// Blink relay LED

				/// Interpret button press:
				// A long  press of up or down brings system back to setup idle
				// A short press of up         increases the lower threshold value
				// A short press of down       decreases the lower threshold value
				if(buttonpress_up == BTNPRESS_LONG || buttonpress_down == BTNPRESS_LONG){
					setup_state = SETUP_IDLE;
					reset_relay_led();
				}
				else if(buttonpress_up == BTNPRESS_STD){
					ADC_lower_threshold += ADC_THRESHOLD_INCREMENT;
					if(ADC_lower_threshold > ADC_THRESHOLD_MAX)
						ADC_lower_threshold = ADC_THRESHOLD_MAX;
				}
				else if(buttonpress_down == BTNPRESS_STD){
					ADC_lower_threshold -= ADC_THRESHOLD_INCREMENT;
					if(ADC_lower_threshold <= 0)
						ADC_lower_threshold = 0;
				}
				break;
			case SETUP_TIME_TH:
				/// Interpret button press:
				// A long  press of up or down brings system back to setup idle
				// A short press of up         increases the threshold exceed time
				// A short press of down       decreases the threshold exceed time
				if(buttonpress_up == BTNPRESS_LONG || buttonpress_down == BTNPRESS_LONG){
					setup_state = SETUP_IDLE;
					reset_relay_led();
				}
				else if(buttonpress_up == BTNPRESS_STD){
					relay_threshold_latchtime += RELAY_LATCHTIME_INCREMENT;
					if(relay_threshold_latchtime > RELAY_LATCHTIME_MAX)
						relay_threshold_latchtime = RELAY_LATCHTIME_MAX;
				}
				else if(buttonpress_down == BTNPRESS_STD){
					relay_threshold_latchtime -= RELAY_LATCHTIME_INCREMENT;
					if(relay_threshold_latchtime <= 0)
						relay_threshold_latchtime = 0;
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

