#include <gb/gb.h>
#include <gb/cgb.h>

#include <gbdk/font.h>
#include <gbdk/console.h> // gotoxy()

#include <stdbool.h> // bool, true, false
#include <string.h> // memcpy
#include <stdio.h> // printf()
#include <rand.h> // initarand(), arand()

//* ------------------------------------------------------------------------------------------- *//
//* -----------------------------------------  NOTES  ----------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

/*
	This is an attempt to do a reusable fade-effect for GBC.

	Its not performant (actually its pretty expensive), and should probably use `cpu_fast()` if you are using a lot of palettes.
	Definitely should be banked, as it takes A LOT of legwork and code to get there.

	However, I did try to make it agnostic of what palettes are used where, and how many.
	So it hopefully its less management during game-logic, and can be just used as a util.

	The basic premise is:
		- keep a lookup-table when you set palettes, to track which are used where.
		- memcpy the currently used const palettes to WRAM, so they can be adjusted.
		- fade the WRAM. load the WRAM. repeat until finished.
	
															- Anchor
*/

//* ------------------------------------------------------------------------------------------- *//
//* -------------------------------------  COMMON MACROS  ------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

//+ ------------------------------  SYSTEM  ------------------------------- +//

//+ --  SOUND  -- +//

#define SOUND_ON \
	NR52_REG = 0x80; /* turns on sound */ \
	NR51_REG = 0xFF; /* turns on L/R for all channels */ \
	NR50_REG = 0x77; /* sets volume to max for L/R */

//+ --  PALETTES  -- +//

#define MAX_HARDWARE_PALETTES 8 // 8 palettes for sprites, 8 palettes for bkg

#define PALETTE_NULL_FLAG 0xFF // for palette_bkg_to_edit, when current_bkg_palettes[idx] is NULL

#define PALETTE_SIZE 4 // palette has 4 rgb colors
#define PALETTE_BYTES (PALETTE_SIZE * sizeof(uint16_t)) // total bytes of one palette

#define BKG_PALMASK 0x07 // mask for palette bits (bits 0-2)

//* ------------------------------------------------------------------------------------------- *//
//* --------------------------------------  GAME MACROS  -------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

//+ --  PALETTES  -- +//

#define FADE_STEP_GBC 4
#define FADE_STEP_COUNTER_GBC 8 // 8 * 4 = 32

//* ------------------------------------------------------------------------------------------- *//
//* --------------------------------------  DEFINITIONS  -------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

//+ ------------------------------  SYSTEM  ------------------------------- +//

bool is_gbc;
// bool is_cpu_fast;

//+ -------------------------------  FONT  -------------------------------- +//

font_t font;

//+ -----------------------------  PALETTES  ------------------------------ +//

bool is_faded = FALSE;
bool to_black = TRUE;

const palette_color_t* current_bkg_palettes_LUT[MAX_HARDWARE_PALETTES]; // pointers, hardware-palette-index to currently used const palette
const palette_color_t* current_sprite_palettes_LUT[MAX_HARDWARE_PALETTES];

// palette_color_t palette_bkg_to_edit_0[PALETTE_SIZE]; // not fading palette-0, to keep the background text
palette_color_t palette_bkg_to_edit_1[PALETTE_SIZE]; // memcpy currently used const palettes to WRAM to edit values
palette_color_t palette_bkg_to_edit_2[PALETTE_SIZE];
palette_color_t palette_bkg_to_edit_3[PALETTE_SIZE];
palette_color_t palette_bkg_to_edit_4[PALETTE_SIZE];
palette_color_t palette_bkg_to_edit_5[PALETTE_SIZE];
palette_color_t palette_bkg_to_edit_6[PALETTE_SIZE];
palette_color_t palette_bkg_to_edit_7[PALETTE_SIZE];

// palette_color_t palette_sprite_to_edit_0[PALETTE_SIZE];
palette_color_t palette_sprite_to_edit_1[PALETTE_SIZE];
palette_color_t palette_sprite_to_edit_2[PALETTE_SIZE];
palette_color_t palette_sprite_to_edit_3[PALETTE_SIZE];
palette_color_t palette_sprite_to_edit_4[PALETTE_SIZE];
palette_color_t palette_sprite_to_edit_5[PALETTE_SIZE];
palette_color_t palette_sprite_to_edit_6[PALETTE_SIZE];
palette_color_t palette_sprite_to_edit_7[PALETTE_SIZE];

//* ------------------------------------------------------------------------------------------- *//
//* ----------------------------------------  ASSETS  ----------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

const unsigned char white_tile[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char basic_tiles[] = {
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // white
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

	0xFF,0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00, // light-gray
	0xFF,0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,

	0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,0xFF, // dark-gray
	0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,0xFF,

	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // black
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};

//* ------------------------------------------------------------------------------------------- *//
//* ---------------------------------------  PALLETES  ---------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

const palette_color_t palette_all_black[] = {
	RGB(0, 0, 0),
	RGB(0, 0, 0),
	RGB(0, 0, 0),
	RGB(0, 0, 0)
};

const palette_color_t palette_all_white[] = {
	RGB(31, 31, 31),
	RGB(31, 31, 31),
	RGB(31, 31, 31),
	RGB(31, 31, 31)
};

const palette_color_t palette_reds[] = { // 4 shades of red
	RGB(31, 21, 21),
	RGB(31, 14, 14),
	RGB(31, 7, 7),
	RGB(31, 0, 0)
};

const palette_color_t palette_greens[] = { // 4 shades of green
	RGB(21, 31, 21),
	RGB(14, 31, 14),
	RGB(7, 31, 7),
	RGB(0, 31, 0)
};

const palette_color_t palette_blues[] = { // 4 shades of blue
	RGB(21, 21, 31),
	RGB(14, 14, 31),
	RGB(7, 7, 31),
	RGB(0, 0, 31)
};

const palette_color_t palette_oranges[] = { // 4 shades of orange
	RGB(31, 31, 24),
	RGB(31, 28, 16),
	RGB(31, 25, 8),
	RGB(31, 22, 0)
};

const palette_color_t palette_cyans[] = { // 4 shades of cyan
	RGB(21, 31, 31),
	RGB(14, 31, 28),
	RGB(7, 31, 25),
	RGB(0, 31, 22)
};

const palette_color_t palette_purples[] = { // 4 shades of purple
	RGB(30, 24, 30),
	RGB(28, 16, 28),
	RGB(23, 8, 23),
	RGB(21, 0, 21)
};

//* ------------------------------------------------------------------------------------------- *//
//* ------------------------------------------  SFX  ------------------------------------------ *//
//* ------------------------------------------------------------------------------------------- *//

void sfx_1(void) {
	// CHN-1:   1, 0, 7, 1, 2, 13, 0, 5, 1847, 0, 1, 1, 0
	NR10_REG = 0x17; // freq sweep
	NR11_REG = 0x42; // duty, length
	NR12_REG = 0xD5; // envelope 
	NR13_REG = 0x37; // freq lbs
	NR14_REG = 0x87; // init, cons, freq msbs 
}

void sfx_2(void) {
	// CHN-1:   6, 0, 4, 2, 2, 13, 0, 5, 1847, 0, 1, 1, 0
	NR10_REG = 0x64; // freq sweep
	NR11_REG = 0x82; // duty, length
	NR12_REG = 0xD5; // envelope 
	NR13_REG = 0x37; // freq lbs
	NR14_REG = 0x87; // init, cons, freq msbs 
}

void sfx_3(void) {
	// CHN-1:	6, 0, 5, 2, 3, 13, 1, 4, 1785, 0, 1, 1, 0
	NR10_REG = 0x65; // freq sweep
	NR11_REG = 0x83; // duty, length
	NR12_REG = 0xDC; // envelope 
	NR13_REG = 0xF9; // freq lbs   
	NR14_REG = 0x86; // init, cons, freq msbs 
}

void sfx_4(void) {
	// CHN-1:	6, 1, 5, 2, 5, 13, 0, 1, 1885, 0, 1, 1, 0
	NR10_REG = 0x6D; // freq sweep
	NR11_REG = 0x85; // duty, length
	NR12_REG = 0xD1; // envelope 
	NR13_REG = 0x5D; // freq lbs   
	NR14_REG = 0x87; // init, cons, freq msbs 
}

//* ------------------------------------------------------------------------------------------- *//
//* ----------------------------------------  SYSTEM  ----------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

void set_cpu(void) {

	CRITICAL {
		if (_cpu == CGB_TYPE) is_gbc = true;
		if (is_gbc) {
			cpu_fast();
			// is_cpu_fast = true;

			set_default_palette(); // palette-0, grayscale
		}
	}

}

void clear_sprite_tiles(void) {

	for (uint8_t i = 0; i < 127; i++) {
		set_sprite_data(i, 1, white_tile);
	}

}

void init_system(void) {

	set_cpu();

	clear_sprite_tiles(); // clear VRAM

	SHOW_BKG;
	SHOW_SPRITES;

	SOUND_ON;
	DISPLAY_ON;

}

//* ------------------------------------------------------------------------------------------- *//
//* -----------------------------------------  INITS  ----------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

void clear_current_bkg_palettes_LUT(void) {

	for (uint8_t i = 0; i < MAX_HARDWARE_PALETTES; i++) {
		current_bkg_palettes_LUT[i] = NULL;
	}

}

void clear_current_sprite_palettes_LUT(void) {

	for (uint8_t i = 0; i < MAX_HARDWARE_PALETTES; i++) {
		current_sprite_palettes_LUT[i] = NULL;
	}

}

void init_palettes(void) {

	// NOTE: make a function like this per 'SCENE', dont need to use all palette slots

	clear_current_bkg_palettes_LUT();
	clear_current_sprite_palettes_LUT();

	set_bkg_palette(1, 1, palette_reds); // TODO: how to set multiple palettes at once?
	current_bkg_palettes_LUT[1] = palette_reds;
	set_bkg_palette(2, 1, palette_greens);
	current_bkg_palettes_LUT[2] = palette_greens;
	set_bkg_palette(3, 1, palette_blues);
	current_bkg_palettes_LUT[3] = palette_blues;
	set_bkg_palette(4, 1, palette_oranges);
	current_bkg_palettes_LUT[4] = palette_oranges;
	set_bkg_palette(5, 1, palette_cyans);
	current_bkg_palettes_LUT[5] = palette_cyans;
	set_bkg_palette(6, 1, palette_purples);
	current_bkg_palettes_LUT[6] = palette_purples;
	
	set_sprite_palette(1, 1, palette_reds);
	current_sprite_palettes_LUT[1] = palette_reds;
	set_sprite_palette(2, 1, palette_greens);
	current_sprite_palettes_LUT[2] = palette_greens;
	set_sprite_palette(3, 1, palette_blues);
	current_sprite_palettes_LUT[3] = palette_blues;
	set_sprite_palette(4, 1, palette_oranges);
	current_sprite_palettes_LUT[4] = palette_oranges;
	set_sprite_palette(5, 1, palette_cyans);
	current_sprite_palettes_LUT[5] = palette_cyans;
	set_sprite_palette(6, 1, palette_purples);
	current_sprite_palettes_LUT[6] = palette_purples;

}

void init_scene(void) {

	gotoxy(1, 1);
	printf("GB COLOR FADE :");
	gotoxy(1, 2);
	printf("------------------");

	gotoxy(1, 5);
	printf("OBJ: ");

	gotoxy(1, 8);
	printf("BKG: ");

	gotoxy(1, 12);
	printf("------------------");
	gotoxy(1, 13);
	printf("  A:   Randomize  ");
	gotoxy(1, 14);
	printf("  B:   Fade       ");
	gotoxy(1, 15);
	printf("  SL:  Black      ");
	gotoxy(1, 16);
	printf("  ST:  Reset      ");

}

void init_sprites(void) {

	set_sprite_data(0, 4, basic_tiles); // tile-data

	set_sprite_tile(0, 0); // oam-data
	set_sprite_tile(1, 1);
	set_sprite_tile(2, 2);
	set_sprite_tile(3, 3);

	move_sprite(0, (6 * 8) + 8, (5 * 8) + 16); // oam-pos
	move_sprite(1, (7 * 8) + 8, (5 * 8) + 16);
	move_sprite(2, (8 * 8) + 8, (5 * 8) + 16);
	move_sprite(3, (9 * 8) + 8, (5 * 8) + 16);

}

void init_backgrounds(void) {

	set_bkg_data(128, 4, basic_tiles); // tile-data

	uint8_t tile_1 = 128; // { 0x80 }
	uint8_t tile_2 = 129; // { 0x81 }
	uint8_t tile_3 = 130; // { 0x82 }
	uint8_t tile_4 = 131; // { 0x83 }

	set_bkg_tile_xy(6, 8, tile_1); // tile-maps
	set_bkg_tile_xy(7, 8, tile_2);
	set_bkg_tile_xy(8, 8, tile_3);
	set_bkg_tile_xy(9, 8, tile_4);

}

//* ------------------------------------------------------------------------------------------- *//
//* ---------------------------------------  ROUTINES  ---------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

void randomize_palette_assignments(void) { // randomly assign a loaded-palette to each sprite and bkg-tile

	initarand(DIV_REG); // seed

	uint8_t rand_num;

	rand_num = (arand() % 6) + 1; // random between 0-5, then + 1 for 1-6
	set_sprite_prop(0, rand_num); // oam-prop / palette
	
	rand_num = (arand() % 6) + 1;
	set_sprite_prop(1, rand_num);

	rand_num = (arand() % 6) + 1;
	set_sprite_prop(2, rand_num);

	rand_num = (arand() % 6) + 1;
	set_sprite_prop(3, rand_num);

	rand_num = (arand() % 6) + 1;
	set_bkg_attribute_xy(6, 8, rand_num); // bkg-prop / palette

	rand_num = (arand() % 6) + 1;
	set_bkg_attribute_xy(7, 8, rand_num);

	rand_num = (arand() % 6) + 1;
	set_bkg_attribute_xy(8, 8, rand_num);

	rand_num = (arand() % 6) + 1;
	set_bkg_attribute_xy(9, 8, rand_num);

}

void fade_palette_to_color_from_black(uint16_t palette_to_edit[PALETTE_SIZE], const uint16_t original_palette[PALETTE_SIZE]) {

	// TODO: maybe instead of incrementing all RGB values at once, implement a threshold (hiwater) where only values above are incremented

	for (int i = 0; i < PALETTE_SIZE; i++) {
		uint16_t color = palette_to_edit[i]; // grab one of the 4 colors of the palette
		uint16_t original_color = original_palette[i]; // grab the original color from the original palette

		uint8_t r = color & 0x1F; // extract red, green, blue values from 5-bit color
		uint8_t g = (color >> 5) & 0x1F;
		uint8_t b = (color >> 10) & 0x1F;

		uint8_t original_r = original_color & 0x1F; // extract original red, green, blue values from 5-bit color
		uint8_t original_g = (original_color >> 5) & 0x1F;
		uint8_t original_b = (original_color >> 10) & 0x1F;

		r = (r < original_r) ? (original_r - r > FADE_STEP_GBC ? r + FADE_STEP_GBC : original_r) : original_r; // add step-amount, but clamp value to original-value
		g = (g < original_g) ? (original_g - g > FADE_STEP_GBC ? g + FADE_STEP_GBC : original_g) : original_g;
		b = (b < original_b) ? (original_b - b > FADE_STEP_GBC ? b + FADE_STEP_GBC : original_b) : original_b;

		palette_to_edit[i] = RGB(r, g, b); // set new color to pallete
	}

}

void fade_palette_to_color_from_white(uint16_t palette_to_edit[PALETTE_SIZE], const uint16_t original_palette[PALETTE_SIZE]) {

	// TODO: maybe instead of decrementing all RGB values at once, implement a threshold (hiwater) where only values above are decremented

	for (int i = 0; i < PALETTE_SIZE; i++) {
		uint16_t color = palette_to_edit[i]; // grab one of the 4 colors of the palette
		uint16_t original_color = original_palette[i]; // grab the original color from the original palette

		uint8_t r = color & 0x1F; // extract red, green, blue values from 5-bit color
		uint8_t g = (color >> 5) & 0x1F;
		uint8_t b = (color >> 10) & 0x1F;

		uint8_t original_r = original_color & 0x1F; // extract original red, green, blue values from 5-bit color
		uint8_t original_g = (original_color >> 5) & 0x1F;
		uint8_t original_b = (original_color >> 10) & 0x1F;

		r = (r > original_r) ? (r - FADE_STEP_GBC > original_r ? r - FADE_STEP_GBC : original_r) : original_r;  // subtract step-amount, but clamp value to original-value
		g = (g > original_g) ? (g - FADE_STEP_GBC > original_g ? g - FADE_STEP_GBC : original_g) : original_g;
		b = (b > original_b) ? (b - FADE_STEP_GBC > original_b ? b - FADE_STEP_GBC : original_b) : original_b;

		palette_to_edit[i] = RGB(r, g, b); // set new color to palette
	}

}

void fade_palette_to_black(uint16_t palette_to_edit[PALETTE_SIZE]) {

	// TODO: maybe instead of decrementing all RGB values at once, implement a threshold (hiwater) where only values above are decremented

	for (int i = 0; i < PALETTE_SIZE; i++) {
		uint16_t color = palette_to_edit[i]; // grab one of the 4 colors of the palette

		uint8_t r = color & 0x1F; // extract red, green, blue values from 5-bit color
		uint8_t g = (color >> 5) & 0x1F;
		uint8_t b = (color >> 10) & 0x1F;

		r = r > FADE_STEP_GBC ? r - FADE_STEP_GBC : 0; // subtract step-amount, but clamp value to 0
		g = g > FADE_STEP_GBC ? g - FADE_STEP_GBC : 0;
		b = b > FADE_STEP_GBC ? b - FADE_STEP_GBC : 0;

		palette_to_edit[i] = RGB(r, g, b); // set new color to pallete
	}

}

void fade_palette_to_white(uint16_t palette_to_edit[PALETTE_SIZE]) {

	// TODO: maybe instead of incrementing all RGB values at once, implement a threshold (hiwater) where only values above are incremented

	for (int i = 0; i < PALETTE_SIZE; i++) {
		uint16_t color = palette_to_edit[i]; // grab one of the 4 colors of the palette

		uint8_t r = color & 0x1F; // extract red, green, blue values from 5-bit color
		uint8_t g = (color >> 5) & 0x1F;
		uint8_t b = (color >> 10) & 0x1F;

		r = r < 27 ? r + FADE_STEP_GBC : 31; // add step-amount, but clamp value to 31
		g = g < 27 ? g + FADE_STEP_GBC : 31;
		b = b < 27 ? b + FADE_STEP_GBC : 31;

		palette_to_edit[i] = RGB(r, g, b); // set new color to pallete
	}

}

static inline void copy_current_palettes_to_wram(void) { // copy currently used const palettes to WRAM to edit values

	//+ --  BKG  -- +//

	if (current_bkg_palettes_LUT[1] != NULL) memcpy(palette_bkg_to_edit_1, current_bkg_palettes_LUT[1], PALETTE_BYTES);
	else palette_bkg_to_edit_1[0] = PALETTE_NULL_FLAG;

	if (current_bkg_palettes_LUT[2] != NULL) memcpy(palette_bkg_to_edit_2, current_bkg_palettes_LUT[2], PALETTE_BYTES);
	else palette_bkg_to_edit_2[0] = PALETTE_NULL_FLAG;

	if (current_bkg_palettes_LUT[3] != NULL) memcpy(palette_bkg_to_edit_3, current_bkg_palettes_LUT[3], PALETTE_BYTES);
	else palette_bkg_to_edit_3[0] = PALETTE_NULL_FLAG;

	if (current_bkg_palettes_LUT[4] != NULL) memcpy(palette_bkg_to_edit_4, current_bkg_palettes_LUT[4], PALETTE_BYTES);
	else palette_bkg_to_edit_4[0] = PALETTE_NULL_FLAG;

	if (current_bkg_palettes_LUT[5] != NULL) memcpy(palette_bkg_to_edit_5, current_bkg_palettes_LUT[5], PALETTE_BYTES);
	else palette_bkg_to_edit_5[0] = PALETTE_NULL_FLAG;

	if (current_bkg_palettes_LUT[6] != NULL) memcpy(palette_bkg_to_edit_6, current_bkg_palettes_LUT[6], PALETTE_BYTES);
	else palette_bkg_to_edit_6[0] = PALETTE_NULL_FLAG;

	//+ --  SPRITES  -- +//

	if (current_sprite_palettes_LUT[1] != NULL) memcpy(palette_sprite_to_edit_1, current_sprite_palettes_LUT[1], PALETTE_BYTES);
	else palette_sprite_to_edit_1[0] = PALETTE_NULL_FLAG;

	if (current_sprite_palettes_LUT[2] != NULL) memcpy(palette_sprite_to_edit_2, current_sprite_palettes_LUT[2], PALETTE_BYTES);
	else palette_sprite_to_edit_2[0] = PALETTE_NULL_FLAG;

	if (current_sprite_palettes_LUT[3] != NULL) memcpy(palette_sprite_to_edit_3, current_sprite_palettes_LUT[3], PALETTE_BYTES);
	else palette_sprite_to_edit_3[0] = PALETTE_NULL_FLAG;

	if (current_sprite_palettes_LUT[4] != NULL) memcpy(palette_sprite_to_edit_4, current_sprite_palettes_LUT[4], PALETTE_BYTES);
	else palette_sprite_to_edit_4[0] = PALETTE_NULL_FLAG;

	if (current_sprite_palettes_LUT[5] != NULL) memcpy(palette_sprite_to_edit_5, current_sprite_palettes_LUT[5], PALETTE_BYTES);
	else palette_sprite_to_edit_5[0] = PALETTE_NULL_FLAG;

	if (current_sprite_palettes_LUT[6] != NULL) memcpy(palette_sprite_to_edit_6, current_sprite_palettes_LUT[6], PALETTE_BYTES);
	else palette_sprite_to_edit_6[0] = PALETTE_NULL_FLAG;

	vsync(); // NOTE: saftey - cycle overflow

}

static inline void copy_black_palette_to_wram(void) { // copy black palette to WRAM to edit values

	//+ --  BKG  -- +//

	if (current_bkg_palettes_LUT[1] != NULL) memcpy(palette_bkg_to_edit_1, palette_all_black, PALETTE_BYTES);
	else palette_bkg_to_edit_1[0] = PALETTE_NULL_FLAG;

	if (current_bkg_palettes_LUT[2] != NULL) memcpy(palette_bkg_to_edit_2, palette_all_black, PALETTE_BYTES);
	else palette_bkg_to_edit_2[0] = PALETTE_NULL_FLAG;

	if (current_bkg_palettes_LUT[3] != NULL) memcpy(palette_bkg_to_edit_3, palette_all_black, PALETTE_BYTES);
	else palette_bkg_to_edit_3[0] = PALETTE_NULL_FLAG;

	if (current_bkg_palettes_LUT[4] != NULL) memcpy(palette_bkg_to_edit_4, palette_all_black, PALETTE_BYTES);
	else palette_bkg_to_edit_4[0] = PALETTE_NULL_FLAG;

	if (current_bkg_palettes_LUT[5] != NULL) memcpy(palette_bkg_to_edit_5, palette_all_black, PALETTE_BYTES);
	else palette_bkg_to_edit_5[0] = PALETTE_NULL_FLAG;

	if (current_bkg_palettes_LUT[6] != NULL) memcpy(palette_bkg_to_edit_6, palette_all_black, PALETTE_BYTES);
	else palette_bkg_to_edit_6[0] = PALETTE_NULL_FLAG;

	//+ --  SPRITES  -- +//

	if (current_sprite_palettes_LUT[1] != NULL) memcpy(palette_sprite_to_edit_1, palette_all_black, PALETTE_BYTES);
	else palette_sprite_to_edit_1[0] = PALETTE_NULL_FLAG;

	if (current_sprite_palettes_LUT[2] != NULL) memcpy(palette_sprite_to_edit_2, palette_all_black, PALETTE_BYTES);
	else palette_sprite_to_edit_2[0] = PALETTE_NULL_FLAG;

	if (current_sprite_palettes_LUT[3] != NULL) memcpy(palette_sprite_to_edit_3, palette_all_black, PALETTE_BYTES);
	else palette_sprite_to_edit_3[0] = PALETTE_NULL_FLAG;

	if (current_sprite_palettes_LUT[4] != NULL) memcpy(palette_sprite_to_edit_4, palette_all_black, PALETTE_BYTES);
	else palette_sprite_to_edit_4[0] = PALETTE_NULL_FLAG;

	if (current_sprite_palettes_LUT[5] != NULL) memcpy(palette_sprite_to_edit_5, palette_all_black, PALETTE_BYTES);
	else palette_sprite_to_edit_5[0] = PALETTE_NULL_FLAG;

	if (current_sprite_palettes_LUT[6] != NULL) memcpy(palette_sprite_to_edit_6, palette_all_black, PALETTE_BYTES);
	else palette_sprite_to_edit_6[0] = PALETTE_NULL_FLAG;

	vsync(); // NOTE: saftey - cycle overflow

}

static inline void copy_white_palette_to_wram(void) { // copy white palette to WRAM to edit values

	//+ --  BKG  -- +//

	if (current_bkg_palettes_LUT[1] != NULL) memcpy(palette_bkg_to_edit_1, palette_all_white, PALETTE_BYTES);
	else palette_bkg_to_edit_1[0] = PALETTE_NULL_FLAG;

	if (current_bkg_palettes_LUT[2] != NULL) memcpy(palette_bkg_to_edit_2, palette_all_white, PALETTE_BYTES);
	else palette_bkg_to_edit_2[0] = PALETTE_NULL_FLAG;

	if (current_bkg_palettes_LUT[3] != NULL) memcpy(palette_bkg_to_edit_3, palette_all_white, PALETTE_BYTES);
	else palette_bkg_to_edit_3[0] = PALETTE_NULL_FLAG;

	if (current_bkg_palettes_LUT[4] != NULL) memcpy(palette_bkg_to_edit_4, palette_all_white, PALETTE_BYTES);
	else palette_bkg_to_edit_4[0] = PALETTE_NULL_FLAG;

	if (current_bkg_palettes_LUT[5] != NULL) memcpy(palette_bkg_to_edit_5, palette_all_white, PALETTE_BYTES);
	else palette_bkg_to_edit_5[0] = PALETTE_NULL_FLAG;

	if (current_bkg_palettes_LUT[6] != NULL) memcpy(palette_bkg_to_edit_6, palette_all_white, PALETTE_BYTES);
	else palette_bkg_to_edit_6[0] = PALETTE_NULL_FLAG;

	//+ --  SPRITES  -- +//

	if (current_sprite_palettes_LUT[1] != NULL) memcpy(palette_sprite_to_edit_1, palette_all_white, PALETTE_BYTES);
	else palette_sprite_to_edit_1[0] = PALETTE_NULL_FLAG;

	if (current_sprite_palettes_LUT[2] != NULL) memcpy(palette_sprite_to_edit_2, palette_all_white, PALETTE_BYTES);
	else palette_sprite_to_edit_2[0] = PALETTE_NULL_FLAG;

	if (current_sprite_palettes_LUT[3] != NULL) memcpy(palette_sprite_to_edit_3, palette_all_white, PALETTE_BYTES);
	else palette_sprite_to_edit_3[0] = PALETTE_NULL_FLAG;

	if (current_sprite_palettes_LUT[4] != NULL) memcpy(palette_sprite_to_edit_4, palette_all_white, PALETTE_BYTES);
	else palette_sprite_to_edit_4[0] = PALETTE_NULL_FLAG;

	if (current_sprite_palettes_LUT[5] != NULL) memcpy(palette_sprite_to_edit_5, palette_all_white, PALETTE_BYTES);
	else palette_sprite_to_edit_5[0] = PALETTE_NULL_FLAG;

	if (current_sprite_palettes_LUT[6] != NULL) memcpy(palette_sprite_to_edit_6, palette_all_white, PALETTE_BYTES);
	else palette_sprite_to_edit_6[0] = PALETTE_NULL_FLAG;

	vsync(); // NOTE: saftey - cycle overflow

}

void fade_to_color_from_black_gbc(void) {

	// NOTE: not fading palette-0, to keep the background text

	copy_black_palette_to_wram(); // NOTE: subengine - copy black palette to WRAM to edit values

	uint8_t fade_counter = FADE_STEP_COUNTER_GBC;

	while (fade_counter > 0) {

		if (palette_bkg_to_edit_1[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_black(palette_bkg_to_edit_1, current_bkg_palettes_LUT[1]); // fade
			set_bkg_palette(1, 1, palette_bkg_to_edit_1); // set palette
		}
		if (palette_bkg_to_edit_2[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_black(palette_bkg_to_edit_2, current_bkg_palettes_LUT[2]);
			set_bkg_palette(2, 1, palette_bkg_to_edit_2);
		}
		if (palette_bkg_to_edit_3[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_black(palette_bkg_to_edit_3, current_bkg_palettes_LUT[3]);
			set_bkg_palette(3, 1, palette_bkg_to_edit_3);
		}
		if (palette_bkg_to_edit_4[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_black(palette_bkg_to_edit_4, current_bkg_palettes_LUT[4]);
			set_bkg_palette(4, 1, palette_bkg_to_edit_4);
		}
		if (palette_bkg_to_edit_5[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_black(palette_bkg_to_edit_5, current_bkg_palettes_LUT[5]);
			set_bkg_palette(5, 1, palette_bkg_to_edit_5);
		}
		if (palette_bkg_to_edit_6[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_black(palette_bkg_to_edit_6, current_bkg_palettes_LUT[6]);
			set_bkg_palette(6, 1, palette_bkg_to_edit_6);
		}

		if (palette_sprite_to_edit_1[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_black(palette_sprite_to_edit_1, current_sprite_palettes_LUT[1]);
			set_sprite_palette(1, 1, palette_sprite_to_edit_1);
		}
		if (palette_sprite_to_edit_2[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_black(palette_sprite_to_edit_2, current_sprite_palettes_LUT[2]);
			set_sprite_palette(2, 1, palette_sprite_to_edit_2);
		}
		if (palette_sprite_to_edit_3[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_black(palette_sprite_to_edit_3, current_sprite_palettes_LUT[3]);
			set_sprite_palette(3, 1, palette_sprite_to_edit_3);
		}
		if (palette_sprite_to_edit_4[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_black(palette_sprite_to_edit_4, current_sprite_palettes_LUT[4]);
			set_sprite_palette(4, 1, palette_sprite_to_edit_4);
		}
		if (palette_sprite_to_edit_5[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_black(palette_sprite_to_edit_5, current_sprite_palettes_LUT[5]);
			set_sprite_palette(5, 1, palette_sprite_to_edit_5);
		}
		if (palette_sprite_to_edit_6[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_black(palette_sprite_to_edit_6, current_sprite_palettes_LUT[6]);
			set_sprite_palette(6, 1, palette_sprite_to_edit_6);
		}

		vsync();
		if (fade_counter > 1) { vsync(); vsync(); } // adding extra time

		fade_counter--;
	}

	is_faded = FALSE;

}

void fade_to_color_from_white_gbc(void) {

	// NOTE: not fading palette-0, to keep the background text

	copy_white_palette_to_wram(); // NOTE: subengine - copy black palette to WRAM to edit values

	uint8_t fade_counter = FADE_STEP_COUNTER_GBC;

	while (fade_counter > 0) {

		if (palette_bkg_to_edit_1[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_white(palette_bkg_to_edit_1, current_bkg_palettes_LUT[1]); // fade
			set_bkg_palette(1, 1, palette_bkg_to_edit_1); // set palette
		}
		if (palette_bkg_to_edit_2[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_white(palette_bkg_to_edit_2, current_bkg_palettes_LUT[2]);
			set_bkg_palette(2, 1, palette_bkg_to_edit_2);
		}
		if (palette_bkg_to_edit_3[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_white(palette_bkg_to_edit_3, current_bkg_palettes_LUT[3]);
			set_bkg_palette(3, 1, palette_bkg_to_edit_3);
		}
		if (palette_bkg_to_edit_4[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_white(palette_bkg_to_edit_4, current_bkg_palettes_LUT[4]);
			set_bkg_palette(4, 1, palette_bkg_to_edit_4);
		}
		if (palette_bkg_to_edit_5[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_white(palette_bkg_to_edit_5, current_bkg_palettes_LUT[5]);
			set_bkg_palette(5, 1, palette_bkg_to_edit_5);
		}
		if (palette_bkg_to_edit_6[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_white(palette_bkg_to_edit_6, current_bkg_palettes_LUT[6]);
			set_bkg_palette(6, 1, palette_bkg_to_edit_6);
		}

		if (palette_sprite_to_edit_1[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_white(palette_sprite_to_edit_1, current_sprite_palettes_LUT[1]);
			set_sprite_palette(1, 1, palette_sprite_to_edit_1);
		}
		if (palette_sprite_to_edit_2[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_white(palette_sprite_to_edit_2, current_sprite_palettes_LUT[2]);
			set_sprite_palette(2, 1, palette_sprite_to_edit_2);
		}
		if (palette_sprite_to_edit_3[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_white(palette_sprite_to_edit_3, current_sprite_palettes_LUT[3]);
			set_sprite_palette(3, 1, palette_sprite_to_edit_3);
		}
		if (palette_sprite_to_edit_4[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_white(palette_sprite_to_edit_4, current_sprite_palettes_LUT[4]);
			set_sprite_palette(4, 1, palette_sprite_to_edit_4);
		}
		if (palette_sprite_to_edit_5[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_white(palette_sprite_to_edit_5, current_sprite_palettes_LUT[5]);
			set_sprite_palette(5, 1, palette_sprite_to_edit_5);
		}
		if (palette_sprite_to_edit_6[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_color_from_white(palette_sprite_to_edit_6, current_sprite_palettes_LUT[6]);
			set_sprite_palette(6, 1, palette_sprite_to_edit_6);
		}

		vsync();
		if (fade_counter > 1) { vsync(); vsync(); } // adding extra time

		fade_counter--;
	}

	is_faded = FALSE;

}

void fade_to_black_gbc(void) {

	// NOTE: not fading palette-0, to keep the background text

	copy_current_palettes_to_wram(); // NOTE: subengine - copy currently used const palettes to WRAM to edit values

	uint8_t fade_counter = FADE_STEP_COUNTER_GBC;

	while (fade_counter > 0) {

		if (palette_bkg_to_edit_1[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_black(palette_bkg_to_edit_1); // fade
			set_bkg_palette(1, 1, palette_bkg_to_edit_1); // set palette
		}
		if (palette_bkg_to_edit_2[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_black(palette_bkg_to_edit_2);
			set_bkg_palette(2, 1, palette_bkg_to_edit_2);
		}
		if (palette_bkg_to_edit_3[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_black(palette_bkg_to_edit_3);
			set_bkg_palette(3, 1, palette_bkg_to_edit_3);
		}
		if (palette_bkg_to_edit_4[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_black(palette_bkg_to_edit_4);
			set_bkg_palette(4, 1, palette_bkg_to_edit_4);
		}
		if (palette_bkg_to_edit_5[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_black(palette_bkg_to_edit_5);
			set_bkg_palette(5, 1, palette_bkg_to_edit_5);
		}
		if (palette_bkg_to_edit_6[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_black(palette_bkg_to_edit_6);
			set_bkg_palette(6, 1, palette_bkg_to_edit_6);
		}

		if (palette_sprite_to_edit_1[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_black(palette_sprite_to_edit_1);
			set_sprite_palette(1, 1, palette_sprite_to_edit_1);
		}
		if (palette_sprite_to_edit_2[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_black(palette_sprite_to_edit_2);
			set_sprite_palette(2, 1, palette_sprite_to_edit_2);
		}
		if (palette_sprite_to_edit_3[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_black(palette_sprite_to_edit_3);
			set_sprite_palette(3, 1, palette_sprite_to_edit_3);
		}
		if (palette_sprite_to_edit_4[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_black(palette_sprite_to_edit_4);
			set_sprite_palette(4, 1, palette_sprite_to_edit_4);
		}
		if (palette_sprite_to_edit_5[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_black(palette_sprite_to_edit_5);
			set_sprite_palette(5, 1, palette_sprite_to_edit_5);
		}
		if (palette_sprite_to_edit_6[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_black(palette_sprite_to_edit_6);
			set_sprite_palette(6, 1, palette_sprite_to_edit_6);
		}

		vsync();
		if (fade_counter > 1) { vsync(); vsync(); } // adding extra time

		fade_counter--;
	}

	is_faded = TRUE;

}

void fade_to_white_gbc(void) {

	// NOTE: not fading palette-0, to keep the background text

	copy_current_palettes_to_wram(); // NOTE: subengine - copy currently used const palettes to WRAM to edit values

	uint8_t fade_counter = FADE_STEP_COUNTER_GBC;

	while (fade_counter > 0) {

		if (palette_bkg_to_edit_1[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_white(palette_bkg_to_edit_1); // fade
			set_bkg_palette(1, 1, palette_bkg_to_edit_1); // set palette
		}
		if (palette_bkg_to_edit_2[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_white(palette_bkg_to_edit_2);
			set_bkg_palette(2, 1, palette_bkg_to_edit_2);
		}
		if (palette_bkg_to_edit_3[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_white(palette_bkg_to_edit_3);
			set_bkg_palette(3, 1, palette_bkg_to_edit_3);
		}
		if (palette_bkg_to_edit_4[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_white(palette_bkg_to_edit_4);
			set_bkg_palette(4, 1, palette_bkg_to_edit_4);
		}
		if (palette_bkg_to_edit_5[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_white(palette_bkg_to_edit_5);
			set_bkg_palette(5, 1, palette_bkg_to_edit_5);
		}
		if (palette_bkg_to_edit_6[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_white(palette_bkg_to_edit_6);
			set_bkg_palette(6, 1, palette_bkg_to_edit_6);
		}

		if (palette_sprite_to_edit_1[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_white(palette_sprite_to_edit_1);
			set_sprite_palette(1, 1, palette_sprite_to_edit_1);
		}
		if (palette_sprite_to_edit_2[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_white(palette_sprite_to_edit_2);
			set_sprite_palette(2, 1, palette_sprite_to_edit_2);
		}
		if (palette_sprite_to_edit_3[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_white(palette_sprite_to_edit_3);
			set_sprite_palette(3, 1, palette_sprite_to_edit_3);
		}
		if (palette_sprite_to_edit_4[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_white(palette_sprite_to_edit_4);
			set_sprite_palette(4, 1, palette_sprite_to_edit_4);
		}
		if (palette_sprite_to_edit_5[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_white(palette_sprite_to_edit_5);
			set_sprite_palette(5, 1, palette_sprite_to_edit_5);
		}
		if (palette_sprite_to_edit_6[0] != PALETTE_NULL_FLAG) {
			fade_palette_to_white(palette_sprite_to_edit_6);
			set_sprite_palette(6, 1, palette_sprite_to_edit_6);
		}

		vsync();
		if (fade_counter > 1) { vsync(); vsync(); } // adding extra time

		fade_counter--;
	}

	is_faded = TRUE;
}

void handle_inputs(void) {

	static uint8_t prev_joypad = NULL;
	uint8_t current_joypad = joypad();

	if ((current_joypad & J_A) && !(prev_joypad & J_A)) {
		if (!is_faded) { randomize_palette_assignments(); sfx_1(); }
	}
	else if ((current_joypad & J_B) && !(prev_joypad & J_B)) {
		if (!is_faded) {
			sfx_4();
			if (to_black) fade_to_black_gbc();
			else fade_to_white_gbc();
		} else {
			sfx_3();
			if (to_black) fade_to_color_from_black_gbc();
			else fade_to_color_from_white_gbc();
		}
	}
	else if ((current_joypad & J_SELECT) && !(prev_joypad & J_SELECT)) {
		if (is_faded) return; // dont allow changing color if faded

		to_black = !to_black;
		sfx_2();
		if (to_black) {
			gotoxy(1, 15);
			printf("  SL:  Black      ");
		} else {
			gotoxy(1, 15);
			printf("  SL:  White      ");
		}

	}
	else if ((current_joypad & J_START) && !(prev_joypad & J_START)) {
		reset();
	}

	prev_joypad = current_joypad;

}

//* ------------------------------------------------------------------------------------------- *//
//* -----------------------------------------  GAME  ------------------------------------------ *//
//* ------------------------------------------------------------------------------------------- *//

void gbc_only_error() {

	if (!is_gbc) {
		while (TRUE) {
			gotoxy(6, 7);
			printf("GBC ONLY");
		}
	}

}

void init_game(void) {

	font_init();
    font = font_load(font_spect);

	gbc_only_error(); // subengine

	init_palettes();

	init_scene(); // header and controls text
	init_sprites(); 
	init_backgrounds();

	randomize_palette_assignments(); // randomly assign a loaded-palette to each sprite and bkg-tile

}

//* ------------------------------------------------------------------------------------------- *//
//* -----------------------------------------  MAIN  ------------------------------------------ *//
//* ------------------------------------------------------------------------------------------- *//

void main(void) {

	init_system();
	init_game();

	while (TRUE) {
		handle_inputs();
		vsync();
	}

}
