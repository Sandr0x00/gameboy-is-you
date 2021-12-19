#include <gb/gb.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <rand.h>

#include "sprites.c"
#include "levels.c"

#define DEBUG

#define NONE   0xff

uint8_t map[MAP_LEN];
uint8_t map_backup[MAP_LEN];
uint8_t mapping[40];
uint8_t get_mapping(uint16_t map_pos) {
	uint8_t elem = map[map_pos];
	if (elem < 40) {
		return mapping[elem];
	}
	return NONE;
}

// ============================================================
// Game state
// ============================================================
uint8_t state = 0; // max 16 atm
uint8_t key_cd = 0;
uint16_t move_cnt = 0;
uint64_t key_queue = 0;
// show motivational messages
bool show_messages = true;

// ============================================================
// RULES
// ============================================================
// BUG: potential overflow into win, since every shut is also stop
//-ME------------------
uint8_t me_objects[3];
uint8_t me_sz = 0;
//-STOP----------------
uint8_t stop_objects[3];
uint8_t stop_sz = 0;
//-WIN-----------------
uint8_t win_objects[3];
uint8_t win_sz = 0;
//-OPEN----------------
uint8_t open_objects[3];
uint8_t open_sz = 0;
//-SHUT----------------
uint8_t shut_objects[3];
uint8_t shut_sz = 0;
//-KILL----------------
uint8_t kill_objects[3];
uint8_t kill_sz = 0;
//-MOVE----------------
uint8_t move_objects[3];
uint8_t move_dir[40];
uint8_t move_sz = 0;
#ifdef IS_VOID
//-KILL----------------
uint8_t void_objects[3];
uint8_t void_sz = 0;
#endif

// ============================================================
// Objects
// ============================================================
#define W_IS   0x00
// objects
// #define O_BABA 0x01
// #define W_WALL 0x02
// #define W_ROCK 0x03
// #define W_FLAG 0x04
// #define W_DOOR 0x05
// #define W_KEY  0x06
// #define W_SKUL 0x07
// #define W_HXP  0x08
// #define W_WIRL 0x09
#define O_MIN  0x01
#define O_MAX  0x09
// attributes
#define A_YOU  0x10
#define A_WIN  0x11
#define A_STOP 0x12
#define A_KILL 0x13
#define A_OPEN 0x14
#define A_SHUT 0x15
#define A_MOVE 0x16
#define A_RAND 0x17
#define A_VOID 0x18
#define A_MIN  A_YOU
#define A_MAX  A_VOID

#define ENTITY_OFFSET 0x20
// ============================================================
// ENTITIES
// ============================================================
// #define E_BABA 0x21
// #define E_WALL 0x22
// #define E_ROCK 0x23
// #define E_FLAG 0x24
// #define E_DOOR 0x25
// #define E_KEY  0x26
// #define E_SKUL 0x27
// #define E_HXP  0x28
// #define E_VOID 0x29
#define E_MIN  0x21
#define E_MAX  0x29

const char FLAG[] =
#ifdef release
	// fake flag
	"hxp{\n\n real_flag_is_\n\n on_the_server}\0"
#else
	// flag
	"hxp{\n\n fl4g_is_w1n.\n\n   y0u_got_fl4g}\0"
#endif
;

/**
 * Display messages in the background layer.
 */
void display_chars(uint8_t x, uint8_t y, const uint8_t *text, const uint8_t length, const uint8_t reset) {
	uint8_t cell[1];
	uint8_t i;
	for (i = 0; i < length; i++) {
		x++;
		if (text[i] == '\0') {
			break;
		}
		if (text[i] == ' ') {
			continue;
		}
		if (text[i] == '\n') {
			y++;
			x -= reset;
			continue;
		}
		cell[0] = text[i];

		set_bkg_tiles(x - 1, y, 1, 1, cell);
	}
}

#ifdef DEBUG
void debug_print_addr(uint16_t addr) {
	uint16_t value = addr;
	uint8_t array[4];
	array[3]=(value &    0xf) >> 0;
	array[2]=(value &   0xff) >> 4;
	array[1]=(value &  0xfff) >> 8;
	array[0]=(value & 0xffff) >> 12;
	for (uint8_t a = 0; a < 4; a++) {
		if (array[a] < 10) {
			array[a] += 48;
		} else {
			array[a] += 87;
		}
	}
	display_chars(3,3, array, 4, 0);
}
#endif
uint8_t display_string(uint8_t x, uint8_t y, char *text) {
	uint8_t cell[1];
	uint8_t orig_x = x;
	uint8_t i = 1;
	char v;

	// basically while true, we never print more than 255 chars
	for (i = 1; i < 0xff; i++) {
		x++;
		v = *text++;
		if (v == '\0') {
			break;
		}
		if (v == ' ') {
			continue;
		}
		if (v == '\n') {
			y++;
			x = orig_x;
			continue;
		}
		if (v == '\r') {
			x -= 2;
			continue;
		}

		cell[0] = v;
		set_bkg_tiles(x - 1, y, 1, 1, cell);
	}
	return i;
}
// ============================================================
// Spiral transition effect
// ============================================================
bool in_transition = false;
uint16_t transition_timer = 0;
uint8_t transition_speedup = 1;
uint8_t transition_x = 0;
uint8_t transition_y = 0;
#define BLACK_TILE 126
#define GREY_TILE 124
uint8_t transition_tile[1] = { BLACK_TILE };
void start_transition(bool win) {
	if (win) {
		// win => black
		transition_tile[0] = BLACK_TILE;
	} else {
		// fail => grey
		transition_tile[0] = GREY_TILE;
	}
	in_transition = true;
	transition_timer = 0;
	transition_speedup = 1;
}
void transition() {
	static int8_t x = 0;
	static int8_t y = 0;
	static uint16_t next_change = 0;
	static uint8_t transition_direction = 0;
	static uint8_t corners = 0;
	uint8_t i;

	if (transition_timer == 0) {
		x = transition_x + 1;
		y = transition_y + 2;
		next_change = 1;
		transition_direction = J_UP;
		corners = 0;
	}

	bool force = false;
	for (i = 0; i < transition_speedup; i++) {
		transition_timer++;

		force = false;
		if (transition_direction == J_UP) {
			y--;
		} else if (transition_direction == J_LEFT) {
			x--;
		} else if (transition_direction == J_DOWN) {
			y++;
		} else if (transition_direction == J_RIGHT) {
			x++;
		}
		if (x == 1 && y == 2) {
			corners |= 0b1000;
		} else if (x == 1 && y == 16) {
			corners |= 0b0010;
		} else if (x == 18 && y == 2) {
			corners |= 0b0100;
		} else if (x == 18 && y == 16) {
			corners |= 0b0001;
		}
		if (y >= 2 && y <= 16 && x >= 1 && x <= 18) {
			set_bkg_tiles(x, y, 1, 1, transition_tile);
		}
		if (corners == 0b1111) {
			in_transition = false;
			return;
		}

		if (transition_timer == next_change / 2 + 1) {
			if (transition_direction == J_UP) {
				transition_direction = J_LEFT;
			} else if (transition_direction == J_LEFT) {
				transition_direction = J_DOWN;
			} else if (transition_direction == J_DOWN) {
				transition_direction = J_RIGHT;
			} else if (transition_direction == J_RIGHT) {
				transition_direction = J_UP;
			}
			next_change++;
			transition_timer = 1;
		}
	}
	if (transition_timer % 3 == 0) {
		transition_speedup *= transition_speedup;
		transition_speedup += 1;
	}
}

// Flip the given sprite on X axis.
//
// sprite_id: the id ("nb") of the sprite to update.
void flip_sprite_horiz(uint8_t sprite_id) {
	set_sprite_prop(sprite_id, get_sprite_prop(sprite_id) | S_FLIPX);
}

// Remove the flip the given sprite on X axis.
//
// sprite_id: the id ("nb") of the sprite to update.
void unflip_sprite_horiz(uint8_t sprite_id) {
	set_sprite_prop(sprite_id, get_sprite_prop(sprite_id) & ~S_FLIPX);
}

// ============================================================
// Scene Switches
// ============================================================

void win() {
	++state;
	start_transition(true);
}
void lose() {
	start_transition(false);
}

// ============================================================
// Rule Management
// ============================================================

uint8_t set_rule(uint8_t left, uint8_t right, uint8_t *transform_rules, uint8_t t_idx) {
	if (left >= O_MIN && left <= O_MAX) {
		if (right >= O_MIN && right <= O_MAX) {
			transform_rules[t_idx++] = left + ENTITY_OFFSET;
			transform_rules[t_idx++] = right + ENTITY_OFFSET;
		} else if (right >= A_MIN && right <= A_MAX) {
			left += ENTITY_OFFSET;
			switch (right)
			{
			case A_YOU:
				me_objects[me_sz++] = left;
				break;
			case A_WIN:
				win_objects[win_sz++] = left;
				break;
			case A_SHUT:
				shut_objects[shut_sz++] = left;
				// shut means also stop!
			case A_STOP:
				stop_objects[stop_sz++] = left;
				break;
			case A_OPEN:
				open_objects[open_sz++] = left;
				break;
			case A_KILL:
				kill_objects[kill_sz++] = left;
				break;
			case A_MOVE:
				move_objects[move_sz++] = left;
				break;
#ifdef IS_VOID
			case A_VOID:
				void_objects[void_sz++] = left;
				break;
#endif
			default:
				break;
			}
		}
	}
	return t_idx;

}

uint8_t rand_rule(uint8_t right) {
	if (right == A_RAND) {
		right ^= (key_queue >> 56) & 0xff;
		right ^= (key_queue >> 48) & 0xff;
		right ^= (key_queue >> 40) & 0xff;
		right ^= (key_queue >> 32) & 0xff;
		right ^= (key_queue >> 24) & 0xff;
		right ^= (key_queue >> 16) & 0xff;
		right ^= (key_queue >> 8) & 0xff;
		right ^= key_queue & 0xff;

#ifdef DEBUG
		if ((right >= A_MIN && right <= A_MAX) || (right >= O_MIN && right <= O_MAX) || (right >= E_MIN && right <= E_MAX)) {
			uint8_t cell[] = {right};
			set_bkg_tiles(1,1,1,1,cell);
		}
#endif
	}
	return right;
}

void update_rules() {
	uint8_t transform_rules[10];
	uint8_t t_idx = 0;

	// always reset before rule-update
	me_sz = 0;
	win_sz = 0;
	shut_sz = 0;
	stop_sz = 0;
	open_sz = 0;
	kill_sz = 0;
	move_sz = 0;
#ifdef IS_VOID
	void_sz = 0;
#endif

	uint16_t i;

	// find "is"
	for (i = 0; i < MAP_LEN; i++) {
		if (get_mapping(i) == W_IS) {
			// TODO: this might be wanky. create a verify level with edge-cases (literally)
			// we found "is", search left and right
			if ((i % MAP_WIDTH) >= 1 && (i % MAP_WIDTH) <= MAP_WIDTH) {
				uint8_t left = get_mapping(i - 1);
				uint8_t right = get_mapping(i + 1);
				right = rand_rule(right);
				t_idx = set_rule(left, right, transform_rules, t_idx);
			}

			// top and bottom for rules
			if (i >= MAP_WIDTH && i <= MAP_LEN - MAP_WIDTH) {
				uint8_t top = get_mapping(i - MAP_WIDTH);
				uint8_t bottom = get_mapping(i + MAP_WIDTH);
				bottom = rand_rule(bottom);
				t_idx = set_rule(top, bottom, transform_rules, t_idx);
			}
		}
	}

	// nothing is set to be a player
	if (me_sz == 0) {
		lose();
		return;
	}

	// check if me == win
	for (i = 0; i < me_sz; i++) {
		for (uint8_t j = 0; j < win_sz; j++) {
			if (me_objects[i] == win_objects[j]) {
				// I am win!
				win();
				return;
			}
		}
	}

	bool found_me = false;

	// transform rules
	for (i = 0; i < MAP_LEN; i++) {
		if (map[i] == NONE) {
			continue;
		}
		uint8_t elem = get_mapping(i);
		if (elem >= E_MIN && elem <= E_MAX) {
			for (uint8_t j = 0; j < t_idx; j+=2) {
				if (elem == transform_rules[j]) {
					// transform this into something else
					mapping[map[i]] = transform_rules[j + 1];
					set_sprite_tile(map[i], transform_rules[j + 1]);
					break;
				}
			}
			for (uint8_t j = 0; j < me_sz; j++) {
				if (elem == me_objects[j]) {
					found_me = true;
					break;
				}
			}
		}
	}

	// the player does not exist
	if (!found_me) {
		lose();
		return;
	}

#ifdef IS_VOID
	#define FAIL 0xfe
	for (i = 0; i < MAP_LEN; i++) {
		uint8_t elem = get_mapping(i);
		// uint8_t tl = FAIL;
		// uint8_t tr = FAIL;
		// uint8_t bl = FAIL;
		// uint8_t br = FAIL;
		// uint8_t t  = FAIL;
		// uint8_t r  = FAIL;
		// uint8_t l  = FAIL;
		// uint8_t b  = FAIL;
		if (elem >= E_MIN && elem <= E_MAX) {
			// BUG: we can steal stuff from outer bounds
			for (uint8_t j = 0; j < void_sz; j++) {
				if (elem == void_objects[j]) {
					// move near elements
					uint8_t x = from_map_x(i);
					uint8_t y = from_map_y(i);
					// t = top, c = center, l = left, r = right, b = bottom
					//                     tltctrrcbrbcbllc
					// uint64_t positions = 0xfefefefefefefefe;
					uint64_t positions = 0;
					// if (x > 1 && y > 1) {
					// 	positions ^= 0xfe << 56;
						positions |= ((uint64_t)map[i - MAP_WIDTH - 1]) << 56;
					// }
					// if (y > 1) {
					// 	positions ^= 0xfe << 48;
						positions |= ((uint64_t)map[i - MAP_WIDTH]) << 48;
					// }
					// if (x > 1 && y < MAP_WIDTH) {
					// 	positions ^= 0xfe << 40;
						positions |= ((uint64_t)map[i - MAP_WIDTH + 1]) << 40;
					// }
					// if (x < MAP_WIDTH) {
					// 	positions ^= 0xfe << 32;
						positions |= ((uint64_t)map[i + 1]) << 32;
					// }
					// if (x < MAP_HEIGHT && y < MAP_WIDTH) {
					// 	positions ^= 0xfe << 24;
						positions |= ((uint64_t)map[i + MAP_WIDTH + 1]) << 24;
					// }
					// if (y < MAP_HEIGHT) {
					// 	positions ^= 0xfe << 16;
						positions |= ((uint64_t)map[i + MAP_WIDTH]) << 16;
					// }
					// if (x < MAP_HEIGHT && y > 1) {
					// 	positions ^= 0xfe << 8;
						positions |= ((uint64_t)map[i + MAP_WIDTH - 1]) << 8;
					// }
					// if (x > 1) {
					// 	positions ^= 0xfe;
						positions |= (uint64_t)map[i - 1];
					// }
					positions = (positions >> 16) | ((positions & 0xffff) << 48);

					// if ((positions & 0x00fefe0000000000) != 0x00fefe0000000000) { // top
					// 	if ((positions & 0x000000fefe000000) != 0x000000fefe000000) { // right
					// 		if ((positions & 0x0000000000fefe00) != 0x0000000000fefe00) { // bot
					// 			if ((positions & 0xfe000000000000fe) != 0xfe000000000000fe) { // left
					// 			}
					// 		}
					// 	}
					// }
					uint8_t id;
					id = (positions >> 56) & 0xff;
					map[i - MAP_WIDTH - 1] = id;
					if (id != NONE) {
						move_sprite(id, (x + 1) * 8, (y + 3) * 8);
					}
					id = (positions >> 48) & 0xff;
					map[i - MAP_WIDTH] = id;
					if (id != NONE) {
						move_sprite(id, (x + 2) * 8, (y + 3) * 8);
					}
					id = (positions >> 40) & 0xff;
					map[i - MAP_WIDTH + 1] = id;
					if (id != NONE) {
						move_sprite(id, (x + 3) * 8, (y + 3) * 8);
					}
					id = (positions >> 32) & 0xff;
					map[i + 1] = id;
					if (id != NONE) {
						move_sprite(id, (x + 3) * 8, (y + 4) * 8);
					}
					id = (positions >> 24) & 0xff;
					map[i + MAP_WIDTH + 1] = id;
					if (id != NONE) {
						move_sprite(id, (x + 3) * 8, (y + 5) * 8);
					}
					id = (positions >> 16) & 0xff;
					map[i + MAP_WIDTH] = id;
					if (id != NONE) {
						move_sprite(id, (x + 2) * 8, (y + 5) * 8);
					}
					id = (positions >> 8) & 0xff;
					map[i + MAP_WIDTH - 1] = id;
					if (id != NONE) {
						move_sprite(id, (x + 1) * 8, (y + 5) * 8);
					}
					id = positions & 0xff;
					map[i - 1] = id;
					if (id != NONE) {
						move_sprite(id, (x + 1) * 8, (y + 4) * 8);
					}
					break;
				}
			}
		}
	}
#endif
}


// ============================================================
// Movement + State Update
// ============================================================

/*
 * Returns whether we can move the tile or not.
 */
uint8_t move_tile(int8_t x, int8_t y, uint8_t direction) {

	uint8_t id = map[to_map(x,y)];

	if (id == NONE) {
		// We can move to an empty tile
		return true;
	}

	int8_t old_x = x;
	int8_t old_y = y;

	// set our move-intent
	switch (direction)
	{
	case J_DOWN:
		y++;
		break;
	case J_UP:
		y--;
		break;
	case J_LEFT:
		x--;
		break;
	case J_RIGHT:
		x++;
		break;
	default:
		// don't move when we get weird inputs, diagonal is not supported
		return false;
	}

	// we cannot move outside of our box
	if (x >= MAP_WIDTH) {
		return false;
	} else if (x < 0) {
		return false;
	}
	if (y >= MAP_HEIGHT) {
		return false;
	} else if (y < 0) {
		return false;
	}

	// check the next tile before actually advancing recursively
	uint8_t next_id = map[to_map(x,y)];

	if (next_id < 40 && id < 40) {
		uint8_t i;
		uint8_t j;
		// next and old are both existing in here, no need to further sanity check
		uint8_t next = mapping[next_id];

		// check win condition
		for (i = 0; i < me_sz; i++) {
			if (mapping[id] == me_objects[i]) {
				for (j = 0; j < win_sz; j++) {
					if (next == win_objects[j]) {
						win();
						return true;
					}
				}
			}
		}

		bool triggered = false;
		// check death: kill player or destroy obj
		for (i = 0; i < kill_sz; i++) {
			if (next == kill_objects[i]) {
				set_sprite_tile(id, 127);
				set_sprite_tile(next_id, 127);
				move_sprite(id, 31, 31);
				move_sprite(next_id, 31, 31);
				mapping[id] = NONE;
				mapping[next_id] = NONE;
				map[to_map(old_x,old_y)] = NONE;
				map[to_map(x,y)] = NONE;
				triggered = true;
			}
		}

		// check open/shut interaction
		// TODO: only works in the open->shut direction, not shut->open
		for (i = 0; i < open_sz; i++) {
			if (mapping[id] == open_objects[i]) {
				for (j = 0; j < shut_sz; j++) {
					if (next == shut_objects[j]) {
						// we push something which opens into something which is shut
						set_sprite_tile(id, 127);
						set_sprite_tile(next_id, 127);
						move_sprite(id, 31, 31);
						move_sprite(next_id, 31, 31);
						mapping[id] = NONE;
						mapping[next_id] = NONE;
						map[to_map(old_x,old_y)] = NONE;
						map[to_map(x,y)] = NONE;
						triggered = true;
					}
				}
			}
		}
		if (triggered) {
			return true;
		}

		// check for stop objects
		for (i = 0; i < stop_sz; i++) {
			if (next == stop_objects[i]) {
				return false;
			}
		}

	}

	// debug delay
	// for (uint64_t i = 0; i < 10000; i++){}

	// try to move next tile recursively
	if (!move_tile(x, y, direction)) {
		return false;
	}

	// update background (data)
	map[to_map(x,y)] = id;
	map[to_map(old_x,old_y)] = NONE;
	// update foreground (visuals)
	move_sprite(id, (x + 2) * 8, (y + 4) * 8);
	if (mapping[id] >= E_MIN && mapping[id] <= E_MAX) {
		// align so moving would move it into the correct direction
		move_dir[id] = direction;
	}
	return true;

}

void display_move_cnt(uint8_t x, uint8_t y) {
	uint16_t tmp = move_cnt;
	char no[] = {'0','0','0','0','0',0x00};
	if (tmp > 9999) {
		no[0] += tmp / 10000;
		tmp %= 10000;
	}
	if (tmp > 999) {
		no[1] += tmp / 1000;
		tmp %= 1000;
	}
	if (tmp > 99) {
		no[2] += tmp / 100;
		tmp %= 100;
	}
	if (tmp > 9) {
		no[3] += tmp / 10;
		tmp %= 10;
	}
	no[4] += tmp;
	display_string(x, y, no);
}

void move(uint8_t direction) {
	// display current move count in header
	if (move_cnt < UINT16_MAX) {
		move_cnt++;
		display_move_cnt(15, 0);
	}

	uint16_t i;
	uint8_t j;

	uint8_t x;
	uint8_t y;
	for (i = 0; i < MAP_LEN; i++) {
		if (map_backup[i] == NONE) {
			continue;
		}
		// move move-objects
		for (j = 0; j < move_sz; j++) {
			if (mapping[map_backup[i]] == move_objects[j]) {
				x = from_map_x(i);
				y = from_map_y(i);
				// try to move the tile, if it's a player tile (we can have multiple player tiles)
				// TODO: change direction when negative.
				uint8_t id = map_backup[i];
				if (!move_tile(x, y, move_dir[id])) {
					// we couldn't move, so go to other direction
					uint8_t dir = move_dir[id];
					switch (dir)
					{
					case J_UP:
						dir = J_DOWN;
						break;
					case J_DOWN:
						dir = J_UP;
						break;
					case J_LEFT:
						dir = J_RIGHT;
						break;
					case J_RIGHT:
					default:
						dir = J_LEFT;
						break;
					}
					move_tile(x, y, dir);
					move_dir[id] = dir;
				}
				if (move_dir[id] == J_LEFT) {
					flip_sprite_horiz(map_backup[i]);
				} else if (move_dir[id] == J_RIGHT) {
					unflip_sprite_horiz(map_backup[i]);
				}
			}
		}
	}
	memcpy(&map_backup, &map, MAP_LEN * sizeof(uint8_t));

	// A means don't move
	if (direction != J_A) {
		for (i = 0; i < MAP_LEN; i++) {
			if (map_backup[i] == NONE) {
				continue;
			}
			// move myself
			for (j = 0; j < me_sz; j++) {
				if (mapping[map_backup[i]] == me_objects[j]) {
					if (direction == J_LEFT) {
						flip_sprite_horiz(map_backup[i]);
					} else if (direction == J_RIGHT) {
						unflip_sprite_horiz(map_backup[i]);
					}
					transition_x = from_map_x(i);
					transition_y = from_map_y(i);
					// try to move the tile, if it's a player tile (we can have multiple player tiles)
					move_tile(transition_x, transition_y, direction);
				}
			}
		}
		memcpy(&map_backup, &map, MAP_LEN * sizeof(uint8_t));
	}

	update_rules();
}

void load_background() {
	uint8_t i;

	set_bkg_tiles(0, 0, BACKGROUND_WIDTH, BACKGROUND_HEIGHT, BACKGROUND);

	initrand(state);
	uint8_t r;
	uint8_t cell[1];
	for (i = 0; i < 10; i++) {
		r = rand();
		cell[0] = (r & 0x3) + 91;
		set_bkg_tiles(
			((r & 0xf0) >> 0x4) + 2,
			((r & 0x0e) >> 0x0) + 2,
			1, 1, cell);
	}

	uint8_t cnt;
	char* msg;
	if (show_messages) {
		msg = msgs[state];
		cnt = *msg++;
		for (i = 0; i < cnt; i++) {
			msg += 2 + display_string(msg[0], msg[1], &msg[2]);
		}
	}
	display_string(0, 0, level_names[state]);
	display_move_cnt(15, 0);

#ifdef DEBUG
	debug_print_addr((uint16_t)&rand_rule);
	// debug_print_addr((uint16_t)&state);
	// debug_print_addr((uint16_t)&move_cnt);
#endif
}

// ============================================================
// Initialize Level X
// ============================================================
void load_level() {
	uint16_t i;

	// reset sprites
	for (i = 0; i < 40; i++) {
		set_sprite_tile(i, 127);
		unflip_sprite_horiz(i);
	}

	transition_x = 9;
	transition_y = 7;

	// show flag or normal background
	if (state == sizeof(levels) / sizeof(uint8_t*)) {
		set_bkg_tiles(0, 0, BACKGROUND_WIDTH, BACKGROUND_HEIGHT, BACKGROUND);
		display_string(0, 0, "Congratulations");
		display_string(4, 3, "Steps:");
		display_move_cnt(11, 3);
		display_string(2, 8, FLAG);
		exit(0);
	} else {
		load_background();
	}

	uint8_t* level = levels[state];
	memcpy(map, level, MAP_LEN);
	memcpy(map_backup, level, MAP_LEN);
	uint8_t sprite_idx = 0x00;
	for (i = 0; i < MAP_LEN; i++) {
		if (level[i] != NONE) {
			if (sprite_idx < 40) {
				mapping[sprite_idx] = level[i];
				map[i] = sprite_idx;
				map_backup[i] = sprite_idx;
				// if (level[i] == 0x24) {
				// 	debug_print_addr(sprite_idx);
				// }
				set_sprite_tile(sprite_idx, level[i]);
				move_sprite(sprite_idx, (from_map_x(i) + 2) * 8, (from_map_y(i) + 4) * 8);
			}
			sprite_idx++;
		}
	}
	if (sprite_idx >= 40) {
#ifdef DEBUG
		debug_print_addr(sprite_idx);
#endif
		exit(1);
	}

	update_rules();

}

// ============================================================
// Main Loop
// ============================================================

void main(void) {
	uint8_t keys = 0;
	// Update player's animation every 8 frame to slow down the animation (8 frames = ~133 ms between each animation frames)
	uint8_t frame_skip = 8;

	set_bkg_data(0, 128, SPRITES);

	// Copy the tilemap in the video memory
	set_bkg_tiles(0, 0, BACKGROUND_WIDTH, BACKGROUND_HEIGHT, BACKGROUND);

	// move_bkg(OFFSET, OFFSET);
	SHOW_BKG;


	// Load sprites' tiles in video memory
	set_sprite_data(0, 128, SPRITES);

	// Use 8x8 or 8x16 sprites
	SPRITES_8x8;

	load_level();

	// Makes sprites "layer" visible
	SHOW_SPRITES;

	while (true) {
		// Wait for v-blank (screen refresh)
		wait_vbl_done();

		// start spiral transition
		if (in_transition) {
			// in_transition--;
			transition();
			if (!in_transition) {
				// TODO: actually do something based on the state
				load_level();
			}
			continue;
		}

		// Read joypad keys to know if the player is walking
		// and in which direction
		keys = joypad();
		key_queue <<= 8;
		key_queue |= keys;

		if (key_cd > 1) {
			key_cd--;
			continue;
		} else if (keys != 0) {
			key_cd = 5;
		}
		switch (keys)
		{
		case J_UP:
		case J_DOWN:
		case J_LEFT:
		case J_RIGHT:
		case J_A:
			move(keys);
			break;
		case J_B:
			show_messages = !show_messages;
			load_background();
			break;
		case J_SELECT:
			lose();
			break;
		default:
			// multi-keys should not work
			break;
		}
	}
}
