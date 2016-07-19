/*
 * KSCUK_UFO.c
 *
 * Created: 27. 8. 2014 13:35:18
 *  Author: Jan
 */ 
#include <avr/io.h>
#include <util/delay.h>
#include <async_usart.hpp>
#include <usart0.hpp>
#include <format.hpp>
#include <avr/pgmspace.h>
#include <pin.hpp>
#include <portd.hpp>
#include <string.h>
#include "ws2812_config.h"
#include "light_ws2812.h"

using avrlib::async_usart;
using avrlib::usart0;
using avrlib::send;
using avrlib::format;
using avrlib::send_spgm;
using avrlib::portd;
using avrlib::pin;

#define NUM_LEDS 78
#define OFFSET	4

cRGB buffer[3*NUM_LEDS + OFFSET];
uint8_t table[256];

async_usart<usart0, 32, 128> control;
pin<portd, 0> rx_pin;
pin<portd, 1> tx_pin;
pin<portd, 3> led_pin;

ISR(USART_RX_vect) {
	control.intr_rx();
}

void fadeOut(struct cRGB color)
{
	const uint8_t LEN = 60;
	const uint8_t MAX_PROGRESS = 1;
	
	static uint8_t progress;
	static uint8_t position;
	
	for(int i = 0; i != LEN; i++) {
		int r = (color.r * table[i*255/LEN])/255;
		int g = (color.g * table[i*255/LEN])/255;
		int b = (color.b * table[i*255/LEN])/255;
		
		int index = (position + i) % NUM_LEDS + OFFSET;
		buffer[index].r = r;
		buffer[index].g = g;
		buffer[index].b = b;
		
		index = (position + i + 5) % NUM_LEDS + OFFSET + NUM_LEDS;
		buffer[index].r = r;
		buffer[index].g = g;
		buffer[index].b = b;
		
		index = (position + i + 10) % NUM_LEDS + OFFSET + 2*NUM_LEDS;
		buffer[index].r = r;
		buffer[index].g = g;
		buffer[index].b = b;
	}
	
	progress++;
	if(progress == MAX_PROGRESS) {
		progress = 0;
		position++;
		if(position == NUM_LEDS)
			position = 0;
	}
	
}

void verticalRotation(struct cRGB color) 
{
	const uint8_t MAX_PROGRESS = 8;
	const uint8_t PERIOD = 4;
	
	static uint8_t position = 0;
	static uint8_t progress = 0;
	
	uint8_t val = table[(uint16_t)progress*255/MAX_PROGRESS];
	struct cRGB firstColor;
	firstColor.r = color.r * val / 255;
	firstColor.g = color.g * val / 255;
	firstColor.b = color.b * val / 255;
	
	val = 255 - val;
	struct cRGB secondColor;
	secondColor.r = color.r * val / 255;
	secondColor.g = color.g * val / 255;
	secondColor.b = color.b * val / 255;
	
	struct cRGB nullCol = {0, 0, 0};
	
	for(uint8_t i = 0; i != NUM_LEDS; i++) {
		struct cRGB col;
		uint8_t mod = (i + position) % PERIOD;

		if(mod == 0)
			col = firstColor;
		else if(mod == 1)
			col = secondColor;
		else
			col = nullCol;
	
		buffer[i + OFFSET] = col;
		buffer[NUM_LEDS + i + OFFSET] = col;
		buffer[2*NUM_LEDS + i + OFFSET] = col;
	}
	
	progress++;
	if(progress == MAX_PROGRESS) {
		progress = 0;
		position++;
		if(position == NUM_LEDS)
		position = 0;
	}
}

void horizontalRotation(struct cRGB color)
{
	//const uint8_t ROWS = 3;
	const uint8_t MAX_PROGRESS = 16;
	
	static uint8_t progress = 0;
	static uint8_t comb = 0;
	
	uint8_t combinations[][2] = {{0, 1}, {1, 2}, {2, 1}, {1, 0}};
	
	uint8_t position = combinations[comb][0];
	uint8_t nextLine = combinations[comb][1];
		
	uint8_t val = table[(uint16_t)progress*255/MAX_PROGRESS];
	
	val = 255 - val;
	struct cRGB firstColor;
	firstColor.r = color.r * val / 255;
	firstColor.g = color.g * val / 255;
	firstColor.b = color.b * val / 255;
	
	val = 255 - val;
	struct cRGB secondColor;
	secondColor.r = color.r * val / 255;
	secondColor.g = color.g * val / 255;
	secondColor.b = color.b * val / 255;
	
	struct cRGB nullCol = {0, 0, 0};
	
	for(uint8_t i = 0; i != NUM_LEDS; i++) {
		buffer[i + OFFSET] = buffer[NUM_LEDS + i + OFFSET] = buffer[2*NUM_LEDS + i + OFFSET] = nullCol;
		buffer[position*NUM_LEDS + i + OFFSET] = firstColor;
		buffer[nextLine*NUM_LEDS + i + OFFSET] = secondColor;
	}
	
	progress++;
	if(progress == MAX_PROGRESS) {
		progress = 0;
		comb++;
		if(comb == sizeof(combinations)/2)
			comb = 0;
	}
}

void flash(struct cRGB color)
{
	static uint8_t count = 0;
	const uint8_t period = 30;
	
	if(count >= period) {
		color.r = color.b = color.g = 0;
	}
	
	for (uint8_t i = 0; i != 3*NUM_LEDS; i++)
	{
		buffer[i + OFFSET] = color;
	}
	
	count++;
	if(count == 2*period)
		count = 0;
}

enum Type {VERT, HORIZ, FLASH, FADE};
struct Anim {
	enum Type type;
	uint16_t duration;
	struct cRGB colorStart, colorEnd;
};


int main(void)
{
	sei();
	control.usart().open(115200UL, true);
	rx_pin.make_input();
	rx_pin.pullup();
	tx_pin.make_output();
	
	led_pin.make_output();
	//Prepare table for smooth transitions
	for(unsigned int i = 0; i != 256; i++)
	{
		float c = sin(M_PI_2 * (float)i / 255.0);
		c = c*c;
		c = c*c;
		table[i] = (uint8_t)(c * 255);
	}
	
	//Animation data
	struct Anim animation[] = {
			/*{ FADE, 1000, {64, 64, 64}, {0, 64, 0}},
			{ FADE, 1000, {0, 64, 0}, {0, 0, 64}},
			{ FLASH, 300, {65, 65, 64}, {64, 64, 64}},
			{ HORIZ, 1000, {64, 255, 0}, {0, 0, 64}},
			{ HORIZ, 1000, {0, 0, 64}, {255, 64, 0}},
			{ VERT, 1000, {255, 64, 0}, {0, 0, 64}},	
			{ VERT, 1000, {0, 0, 64}, {64, 0, 0}},
			{ FLASH, 500, {0, 65, 0}, {0, 0, 0}},	*/
			{ FADE, 1000, {255, 255, 255}, {0, 255, 0}},
			{ FADE, 1000, {0, 255, 0}, {0, 0, 255}},
			{ FLASH, 300, {255, 255, 255}, {255, 255, 255}},
			{ HORIZ, 1000, {64, 255, 0}, {0, 0, 64}},
			{ HORIZ, 1000, {0, 0, 64}, {255, 64, 0}},
			{ VERT, 1000, {255, 64, 0}, {0, 0, 255}},
			{ VERT, 1000, {0, 0, 255}, {255, 0, 0}},
			{ FLASH, 500, {0, 255, 0}, {0, 0, 0}},
		};
	
	//Perform animation in endless loop
	bool enabled = true;
	int16_t anim = 0;
	int16_t duration = animation[0].duration;
	int16_t brightness = 10;
	char mode = 'c';
    while(1) {		
		if(!control.empty()) {
			char cmd = control.read();
			switch(cmd)
			{
			case 'h':
				send_spgm(control, PSTR("UFO2.0, welcome!\nUsage:\nh - help\nc - classic mode\ns - start/stop\nf - full frame\ng - half frame front\nd - half frame back\nqwertyuiop - brightness\n\nThank you for using UFO2.0\n"));
				break;
			case 's':
				enabled = !enabled;
				if(enabled)
					send(control, "UFO is ON\n");
				else
					send(control, "UFO is OFF\n");
				break;
			case 'c':
			case 'f':
			case 'g':
			case 'd':
				mode = cmd;
				format(control, "Mode set to: % \n") % mode;
				break;
			case 'q':
				brightness = 1;
				break;
			case 'w':
				brightness = 2;
				break;
			case 'e':
				brightness = 3;
				break;
			case 'r':
				brightness = 4;
				break;
			case 't':
				brightness = 5;
				break;
			case 'z':
			case 'y':
				brightness = 6;
				break;
			case 'u':
				brightness = 7;
				break;
			case 'i':
				brightness = 8;
				break;
			case 'o':
				brightness = 9;
				break;
			case 'p':
				brightness = 10;
				break;
			default:
				send(control, "Invalid command! Please see help (press h)\n");
				break;
			}
		}
		
		if(enabled) {
			led_pin.set();
		}
		else {
			led_pin.clear();
		}
		
		//Prepare color
		struct cRGB color;
		uint8_t val = table[(uint32_t)duration*255/animation[anim].duration];
		color.r = (animation[anim].colorStart.r * val + animation[anim].colorEnd.r * (255 - val))/255;
		color.g = (animation[anim].colorStart.g * val + animation[anim].colorEnd.g * (255 - val))/255;
		color.b = (animation[anim].colorStart.b * val + animation[anim].colorEnd.b * (255 - val))/255;
		
		color.r = (uint16_t)color.r * brightness / 10;
		color.g = (uint16_t)color.g * brightness / 10;
		color.b = (uint16_t)color.b * brightness / 10;
		//Perform animation
		if(enabled) {
			switch(mode) {
			case 'c':
				switch(animation[anim].type) {
					case FLASH:
					flash(color);
					break;
					case VERT:
					verticalRotation(color);
					break;
					case HORIZ:
					horizontalRotation(color);
					break;
					case FADE:
					fadeOut(color);
					break;
					default:
					break;
				}
				break;
			case 'f':
				memset((char*)buffer + 3 * OFFSET, brightness * 255 / 10, 9 * NUM_LEDS);
				break;
			case 'g': {
					memset(buffer, 0, 3*(3*NUM_LEDS + OFFSET));
					memset((char*)buffer + 3 * OFFSET + 0 * NUM_LEDS * 3 + 3 * NUM_LEDS / 2, brightness * 255 / 10, NUM_LEDS * 3 / 2);
					memset((char*)buffer + 3 * OFFSET + 1 * NUM_LEDS * 3 + 3 * NUM_LEDS / 2, brightness * 255 / 10, NUM_LEDS * 3 / 2);
					memset((char*)buffer + 3 * OFFSET + 2 * NUM_LEDS * 3 + 3 * NUM_LEDS / 2, brightness * 255 / 10, NUM_LEDS * 3 / 2);
				
					const uint8_t intensity = 128;
				
					for(int i = 0; i != NUM_LEDS / 6; i++) {
						buffer[OFFSET + i].r = intensity * brightness / 10;
					}
					
					for(int i = 0; i != NUM_LEDS / 6; i++) {
						buffer[OFFSET + NUM_LEDS * 2 / 6 + i].r = intensity * brightness / 10;
					}
					break;
				}
			case 'd':
				memset(buffer, 0, 3*(3*NUM_LEDS + OFFSET));
				memset((char*)buffer + 3 * OFFSET + 0 * NUM_LEDS * 3, brightness * 255 / 10, NUM_LEDS * 3 / 2);
				memset((char*)buffer + 3 * OFFSET + 1 * NUM_LEDS * 3, brightness * 255 / 10, NUM_LEDS * 3 / 2);
				memset((char*)buffer + 3 * OFFSET + 2 * NUM_LEDS * 3, brightness * 255 / 10, NUM_LEDS * 3 / 2);
				break;
			default:
				format(control, "Unknown mode % \n") % mode;
				break;
			}
			
		}
		else {
			memset(buffer, 0, 3*(3*NUM_LEDS + OFFSET));
		}
		
		ws2812_setleds(buffer, 3*NUM_LEDS + OFFSET);
		_delay_ms(5);
		
		duration--;
		if(duration == 0) {
			anim++;
			if(anim == sizeof(animation)/sizeof(struct Anim))
				anim = 0;
			duration = animation[anim].duration;
		}
		control.process_tx();
    }
}