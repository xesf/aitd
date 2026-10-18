#!/usr/bin/env python3
"""Name the Time Gate (TG320.EX_) routines and globals rebuilt in src/.

The C code annotates them as "TG 0x...". TG320.EX_ is a different build
from WAITD3.EXE, so the names live in this table instead of being scraped
from the sources like tools/sync_ida_names.py does.

    python3 tools/sync_tg_names.py [--dry-run]

Needs the ida-mcp server with TG320.EX_ loaded.
"""
import sys

from sync_ida_names import call

FUNCS = {
    0x4228a9: 'r3d_draw_body',
    0x421dad: 'transform_animated',
    0x42202c: 'transform_static',
    0x421b50: 'bone_rotate_points',
    0x421cbf: 'bone_rotate_tree',
    0x422256: 'r3d_world_to_camera',
    0x40c787: 'r3d_world_to_camera_vec',
    0x422a8a: 'draw_primitives',
    0x41b234: 'body_load_pages',
    0x41fe2f: 'texture_page',
    0x417cfe: 'texture_load_shade_table',
    0x411e2f: 'texture_light_level',
    0x41c9b3: 'texture_set_detail',
    0x41bc2c: 'texture_fill_textured',
    0x412e4f: 'texture_fill_gouraud',
    0x4133bc: 'texture_fill_textured_lit',
    0x41b0df: 'texture_span',
    0x412af8: 'texture_spans_lit',
    0x42ba34: 'gouraud_spans',
    0x40b767: 'world_camera_sees_room',
    0x40b95c: 'actor_update_lights',
    0x41778f: 'game_main',
    0x405f6b: 'main_menu_run',
    0x405c64: 'ring_draw',
    0x4062c3: 'difficulty_select',
    0x409397: 'options_menu',
    0x4146d1: 'world_load_background',
    0x414c35: 'load_mask_slots',
    0x414cd5: 'free_mask_slots',
    0x40c981: 'draw_masks',
    0x42a422: 'mask_zone_hit',
    0x42a478: 'draw_mask',
    0x40c7c7: 'camera_apply_lights',
    0x40a858: 'message_draw',
    0x405a14: 'font_set_colour',
    0x422c14: 'hyb_render',
    0x40c3b9: 'world_set_floor',
}

DATA = {
    0x4422a0: 'g_uv_table',
    0x45c4d0: 'g_texture_page_0',
    0x45c4cc: 'g_texture_page_1',
    0x4422b2: 'g_normal_count',
    0x4422ae: 'g_normal_offset',
    0x43c4b0: 'g_camera_points',
    0x440fb0: 'g_projected_points',
    0x4583a4: 'g_shade_table',
    0x43c333: 'g_light_dir_x',
    0x43c337: 'g_light_dir_y',
    0x451c14: 'g_light_dir_z',
    0x451d54: 'g_light_pos_x',
    0x451d50: 'g_light_pos_y',
    0x451d58: 'g_light_pos_z',
    0x442314: 'g_prim_collectors',
    0x442340: 'g_prim_drawers',
    0x451fcc: 'g_textures_pak',
}


def main():
    dry = '--dry-run' in sys.argv
    for addr, name in sorted(FUNCS.items()):
        print('func %#x -> %s' % (addr, name))
        if not dry:
            call('rename', {'batch': {'func': [{'addr': hex(addr), 'name': name}]}})
    for addr, name in sorted(DATA.items()):
        print('data %#x -> %s' % (addr, name))
        if dry:
            continue
        # The rename wants the current name: try IDA's default prefixes.
        for prefix in ('dword', 'word', 'byte', 'unk', 'off', name):
            old = name if prefix == name else '%s_%X' % (prefix, addr)
            r = call('rename', {'batch': {'data': [{'old': old, 'new': name}]}})
            if r['data'][0]['ok']:
                break
        else:
            print('  not renamed')


if __name__ == '__main__':
    main()
