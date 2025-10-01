/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Keyboard configuration */

#ifndef __KEYBOARD_CUSTOMIZATION_H
#define __KEYBOARD_CUSTOMIZATION_H

/*
 * KEYBOARD_COLS_MAX has the build time column size. It's used to allocate
 * exact spaces for arrays. Actual keyboard scanning is done using
 * keyboard_cols, which holds a runtime column size.
 */
#define KEYBOARD_COLS_MAX 16
#define KEYBOARD_ROWS 8

/* Columns for keys we particularly care about */
#define BANSHEE_KEYBOARD_COL_DOWN 8
#define BANSHEE_KEYBOARD_ROW_DOWN 1
#define BANSHEE_KEYBOARD_COL_ESC 5
#define BANSHEE_KEYBOARD_ROW_ESC 7
#define BANSHEE_KEYBOARD_COL_KEY_H 7
#define BANSHEE_KEYBOARD_ROW_KEY_H 2
#define BANSHEE_KEYBOARD_COL_KEY_R 6
#define BANSHEE_KEYBOARD_ROW_KEY_R 6
#define BANSHEE_KEYBOARD_COL_LEFT_ALT 3
#define BANSHEE_KEYBOARD_ROW_LEFT_ALT 1
#define BANSHEE_KEYBOARD_COL_REFRESH 4
#define BANSHEE_KEYBOARD_ROW_REFRESH 6
#define BANSHEE_KEYBOARD_COL_ID2_REFRESH 5
#define BANSHEE_KEYBOARD_ROW_ID2_REFRESH 2
#define BANSHEE_KEYBOARD_COL_RIGHT_ALT 3
#define BANSHEE_KEYBOARD_ROW_RIGHT_ALT 0
#define KEYBOARD_DEFAULT_COL_VOL_UP 13
#define KEYBOARD_DEFAULT_ROW_VOL_UP 3
#define BANSHEE_KEYBOARD_COL_LEFT_SHIFT 9
#define BANSHEE_KEYBOARD_ROW_LEFT_SHIFT 1
#define KEYBOARD_SCANCODE_CALLBACK 1

/* Adding support for fn keys */
#define SCANCODE_FN 0x00ff
#define SCANCODE_ESC 0x0076
#define SCANCODE_DELETE 0xe071
#define SCANCODE_K 0x0042
#define SCANCODE_P 0x004D
#define SCANCODE_S 0x001B
#define SCANCODE_SPACE 0x0029

enum kb_fn_table {
        KB_FN_F1 = BIT(0),
        KB_FN_F2 = BIT(1),
        KB_FN_F3 = BIT(2),
        KB_FN_F4 = BIT(3),
        KB_FN_F5 =  BIT(4),
        KB_FN_F6 = BIT(5),
        KB_FN_F7 = BIT(6),
        KB_FN_F8 = BIT(7),
        KB_FN_F9 = BIT(8),
        KB_FN_F10 = BIT(9),
        KB_FN_F11 = BIT(10),
        KB_FN_F12 = BIT(11),
        KB_FN_DELETE = BIT(12),
        KB_FN_K = BIT(13),
	KB_FN_S = BIT(14),
	KB_FN_LEFT = BIT(15),
	KB_FN_RIGHT = BIT(16),
	KB_FN_UP = BIT(17),
	KB_FN_DOWN = BIT(18),
	KB_FN_ESC = BIT(19),
	KB_FN_B = BIT(20),
	KB_FN_P = BIT(21),
	KB_FN_SPACE = BIT(22),
};


void board_id_keyboard_col_inverted(int board_id);

#endif /* __KEYBOARD_CUSTOMIZATION_H */
