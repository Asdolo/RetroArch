/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2011-2017 - Daniel De Matteis
 *  Copyright (C) 2014-2017 - Jean-André Santoni
 *  Copyright (C) 2016-2017 - Brad Parker
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include <compat/posix_string.h>
#include <compat/strl.h>
#include <file/file_path.h>
#include <formats/image.h>
#include <gfx/math/matrix_4x4.h>
#include <string/stdstring.h>
#include <lists/string_list.h>
#include <encodings/utf.h>

#ifdef HAVE_CONFIG_H
#include "../../config.h"
#endif

#ifndef HAVE_DYNAMIC
#include "../../frontend/frontend_driver.h"
#endif

#include "menu_generic.h"

#include "../menu_driver.h"
#include "../menu_animation.h"
#include "../menu_event.h"

#include "../widgets/menu_input_dialog.h"
#include "../widgets/menu_osk.h"

#include "../../core_info.h"
#include "../../core.h"
#include "../../configuration.h"
#include "../../retroarch.h"
#include "../../verbosity.h"
#include "../../tasks/tasks_internal.h"

#include "../../file_path_special.h"

/* This struct holds the y position and the line height for each menu entry */
typedef struct
{
   float line_height;
   float y;
   bool texture_switch_set;
   uintptr_t texture_switch;
   bool texture_switch2_set;
   uintptr_t texture_switch2;
   bool switch_is_on;
   bool do_draw_text;
} mui_node_t;

/* Textures used for the tabs and the switches */
enum
{
   MUI_TEXTURE_POINTER = 0,
   MUI_TEXTURE_BACK,
   MUI_TEXTURE_SWITCH_ON,
   MUI_TEXTURE_SWITCH_OFF,
   MUI_TEXTURE_TAB_MAIN,
   MUI_TEXTURE_TAB_PLAYLISTS,
   MUI_TEXTURE_TAB_SETTINGS,
   MUI_TEXTURE_KEY,
   MUI_TEXTURE_KEY_HOVER,
   MUI_TEXTURE_FOLDER,
   MUI_TEXTURE_PARENT_DIRECTORY,
   MUI_TEXTURE_IMAGE,
   MUI_TEXTURE_ARCHIVE,
   MUI_TEXTURE_VIDEO,
   MUI_TEXTURE_MUSIC,
   MUI_TEXTURE_QUIT,
   MUI_TEXTURE_HELP,
   MUI_TEXTURE_UPDATE,
   MUI_TEXTURE_HISTORY,
   MUI_TEXTURE_INFO,
   MUI_TEXTURE_ADD,
   MUI_TEXTURE_SETTINGS,
   MUI_TEXTURE_FILE,
   MUI_TEXTURE_PLAYLIST,
   MUI_TEXTURE_UPDATER,
   MUI_TEXTURE_QUICKMENU,
   MUI_TEXTURE_NETPLAY,
   MUI_TEXTURE_CORES,
   MUI_TEXTURE_SHADERS,
   MUI_TEXTURE_CONTROLS,
   MUI_TEXTURE_CLOSE,
   MUI_TEXTURE_CORE_OPTIONS,
   MUI_TEXTURE_CORE_CHEAT_OPTIONS,
   MUI_TEXTURE_RESUME,
   MUI_TEXTURE_RESTART,
   MUI_TEXTURE_ADD_TO_FAVORITES,
   MUI_TEXTURE_RUN,
   MUI_TEXTURE_RENAME,
   MUI_TEXTURE_DATABASE,
   MUI_TEXTURE_ADD_TO_MIXER,
   MUI_TEXTURE_SCAN,
   MUI_TEXTURE_REMOVE,
   MUI_TEXTURE_START_CORE,
   MUI_TEXTURE_LOAD_STATE,
   MUI_TEXTURE_SAVE_STATE,
   MUI_TEXTURE_UNDO_LOAD_STATE,
   MUI_TEXTURE_UNDO_SAVE_STATE,
   MUI_TEXTURE_STATE_SLOT,
   MUI_TEXTURE_TAKE_SCREENSHOT,
   MUI_TEXTURE_CONFIGURATIONS,
   MUI_TEXTURE_LOAD_CONTENT,
   MUI_TEXTURE_LAST
};

/* The menu has 3 tabs */
enum
{
   MUI_SYSTEM_TAB_MAIN = 0,
   MUI_SYSTEM_TAB_PLAYLISTS,
   MUI_SYSTEM_TAB_SETTINGS
};

#define MUI_SYSTEM_TAB_END MUI_SYSTEM_TAB_SETTINGS

typedef struct mui_handle
{
   unsigned tabs_height;
   unsigned line_height;
   unsigned shadow_height;
   unsigned scrollbar_width;
   unsigned icon_size;
   unsigned margin;
   unsigned glyph_width;
   unsigned glyph_width2;
   char box_message[1024];
   bool mouse_show;
   uint64_t frame_count;

   struct
   {
      int size;
   } cursor;

   struct
   {
      struct
      {
         float alpha;
      } arrow;

      menu_texture_item bg;
      menu_texture_item list[MUI_TEXTURE_LAST];
   } textures;

   struct
   {
      struct
      {
         unsigned idx;
         unsigned idx_old;
      } active;

      float x_pos;
      size_t selection_ptr_old;
      size_t selection_ptr;
   } categories;

   /* One font for the menu entries, one font for the labels */
   font_data_t *font;
   font_data_t *font2;
   video_font_raster_block_t raster_block;
   video_font_raster_block_t raster_block2;

   /* Y position of the vertical scroll */
   float scroll_y;
} mui_handle_t;

/* All variables related to colors should be grouped here */
typedef struct mui_theme {
   float header_bg_color[16];
   float highlighted_entry_color[16];
   float footer_bg_color[16];
   float body_bg_color[16];
   float active_tab_marker_color[16];
   float passive_tab_icon_color[16];

   uint32_t font_normal_color;
   uint32_t font_hover_color;
   uint32_t font_header_color;

   uint32_t sublabel_normal_color;
   uint32_t sublabel_hover_color;
} mui_theme_t;

/* Global struct, so any function can know what colors to use  */
mui_theme_t theme;

static void hex32_to_rgba_normalized(uint32_t hex, float* rgba, float alpha)
{
   rgba[0] = rgba[4] = rgba[8]  = rgba[12] = ((hex >> 16) & 0xFF) * (1.0f / 255.0f); /* r */
   rgba[1] = rgba[5] = rgba[9]  = rgba[13] = ((hex >> 8 ) & 0xFF) * (1.0f / 255.0f); /* g */
   rgba[2] = rgba[6] = rgba[10] = rgba[14] = ((hex >> 0 ) & 0xFF) * (1.0f / 255.0f); /* b */
   rgba[3] = rgba[7] = rgba[11] = rgba[15] = alpha;
}

static const char *mui_texture_path(unsigned id)
{
   switch (id)
   {
      case MUI_TEXTURE_POINTER:
         return "pointer.png";
      case MUI_TEXTURE_BACK:
         return "back.png";
      case MUI_TEXTURE_SWITCH_ON:
         return "on.png";
      case MUI_TEXTURE_SWITCH_OFF:
         return "off.png";
      case MUI_TEXTURE_TAB_MAIN:
         return "main_tab_passive.png";
      case MUI_TEXTURE_TAB_PLAYLISTS:
         return "playlists_tab_passive.png";
      case MUI_TEXTURE_TAB_SETTINGS:
         return "settings_tab_passive.png";
      case MUI_TEXTURE_KEY:
         return "key.png";
      case MUI_TEXTURE_KEY_HOVER:
         return "key-hover.png";
      case MUI_TEXTURE_FOLDER:
         return "folder.png";
      case MUI_TEXTURE_PARENT_DIRECTORY:
         return "parent_directory.png";
      case MUI_TEXTURE_IMAGE:
         return "image.png";
      case MUI_TEXTURE_VIDEO:
         return "video.png";
      case MUI_TEXTURE_MUSIC:
         return "music.png";
      case MUI_TEXTURE_ARCHIVE:
         return "archive.png";
      case MUI_TEXTURE_QUIT:
         return "quit.png";
      case MUI_TEXTURE_HELP:
         return "help.png";
      case MUI_TEXTURE_NETPLAY:
         return "netplay.png";
      case MUI_TEXTURE_CORES:
         return "cores.png";
      case MUI_TEXTURE_CONTROLS:
         return "controls.png";
      case MUI_TEXTURE_RESUME:
         return "resume.png";
      case MUI_TEXTURE_RESTART:
         return "restart.png";
      case MUI_TEXTURE_CLOSE:
         return "close.png";
      case MUI_TEXTURE_CORE_OPTIONS:
         return "core_options.png";
      case MUI_TEXTURE_CORE_CHEAT_OPTIONS:
         return "core_cheat_options.png";
      case MUI_TEXTURE_SHADERS:
         return "shaders.png";
      case MUI_TEXTURE_ADD_TO_FAVORITES:
         return "add_to_favorites.png";
      case MUI_TEXTURE_RUN:
         return "run.png";
      case MUI_TEXTURE_RENAME:
         return "rename.png";
      case MUI_TEXTURE_DATABASE:
         return "database.png";
      case MUI_TEXTURE_ADD_TO_MIXER:
         return "add_to_mixer.png";
      case MUI_TEXTURE_SCAN:
         return "scan.png";
      case MUI_TEXTURE_REMOVE:
         return "remove.png";
      case MUI_TEXTURE_START_CORE:
         return "start_core.png";
      case MUI_TEXTURE_LOAD_STATE:
         return "load_state.png";
      case MUI_TEXTURE_SAVE_STATE:
         return "save_state.png";
      case MUI_TEXTURE_UNDO_LOAD_STATE:
         return "undo_load_state.png";
      case MUI_TEXTURE_UNDO_SAVE_STATE:
         return "undo_save_state.png";
      case MUI_TEXTURE_STATE_SLOT:
         return "state_slot.png";
      case MUI_TEXTURE_TAKE_SCREENSHOT:
         return "take_screenshot.png";
      case MUI_TEXTURE_CONFIGURATIONS:
         return "configurations.png";
      case MUI_TEXTURE_LOAD_CONTENT:
         return "load_content.png";
      case MUI_TEXTURE_UPDATER:
         return "update.png";
      case MUI_TEXTURE_QUICKMENU:
         return "quickmenu.png";
      case MUI_TEXTURE_HISTORY:
         return "history.png";
      case MUI_TEXTURE_INFO:
         return "information.png";
      case MUI_TEXTURE_ADD:
         return "add.png";
      case MUI_TEXTURE_SETTINGS:
         return "settings.png";
      case MUI_TEXTURE_FILE:
         return "file.png";
      case MUI_TEXTURE_PLAYLIST:
         return "playlist.png";
   }

   return NULL;
}

static void mui_context_reset_textures(mui_handle_t *mui)
{
   unsigned i;
   char iconpath[PATH_MAX_LENGTH];

   iconpath[0] = '\0';

   fill_pathname_application_special(iconpath, sizeof(iconpath),
         APPLICATION_SPECIAL_DIRECTORY_ASSETS_MATERIALUI_ICONS);

   for (i = 0; i < MUI_TEXTURE_LAST; i++)
      menu_display_reset_textures_list(mui_texture_path(i), iconpath, &mui->textures.list[i], TEXTURE_FILTER_MIPMAP_LINEAR);
}

static void mui_draw_icon(
      unsigned icon_size,
      uintptr_t texture,
      float x, float y,
      unsigned width, unsigned height,
      float rotation, float scale_factor,
      float *color)
{
   menu_display_ctx_rotate_draw_t rotate_draw;
   menu_display_ctx_draw_t draw;
   struct video_coords coords;
   math_matrix_4x4 mymat;

   menu_display_blend_begin();

   rotate_draw.matrix       = &mymat;
   rotate_draw.rotation     = rotation;
   rotate_draw.scale_x      = scale_factor;
   rotate_draw.scale_y      = scale_factor;
   rotate_draw.scale_z      = 1;
   rotate_draw.scale_enable = true;

   menu_display_rotate_z(&rotate_draw);

   coords.vertices      = 4;
   coords.vertex        = NULL;
   coords.tex_coord     = NULL;
   coords.lut_tex_coord = NULL;
   coords.color         = (const float*)color;

   draw.x               = x;
   draw.y               = height - y - icon_size;
   draw.width           = icon_size;
   draw.height          = icon_size;
   draw.coords          = &coords;
   draw.matrix_data     = &mymat;
   draw.texture         = texture;
   draw.prim_type       = MENU_DISPLAY_PRIM_TRIANGLESTRIP;
   draw.pipeline.id     = 0;

   menu_display_draw(&draw);
   menu_display_blend_end();
}

/* Draw a single tab */
static void mui_draw_tab(mui_handle_t *mui,
      unsigned i,
      unsigned width, unsigned height,
      float *tab_color,
      float *active_tab_color)
{
   unsigned tab_icon = 0;

   switch (i)
   {
      case MUI_SYSTEM_TAB_MAIN:
         tab_icon = MUI_TEXTURE_TAB_MAIN;
         if (i == mui->categories.selection_ptr)
            tab_color = active_tab_color;
         break;
      case MUI_SYSTEM_TAB_PLAYLISTS:
         tab_icon = MUI_TEXTURE_TAB_PLAYLISTS;
         if (i == mui->categories.selection_ptr)
            tab_color = active_tab_color;
         break;
      case MUI_SYSTEM_TAB_SETTINGS:
         tab_icon = MUI_TEXTURE_TAB_SETTINGS;
         if (i == mui->categories.selection_ptr)
            tab_color = active_tab_color;
         break;
   }

   mui_draw_icon(
         mui->icon_size,
         mui->textures.list[tab_icon],
         width / (MUI_SYSTEM_TAB_END+1) * (i+0.5) - mui->icon_size/2,
         height - mui->tabs_height,
         width,
         height,
         0,
         1,
         &tab_color[0]);
}

/* Draw the onscreen keyboard */
static void mui_render_keyboard(mui_handle_t *mui,
      video_frame_info_t *video_info,
      const char *grid[], unsigned id)
{
   int ptr_width, ptr_height;
   unsigned i;
   unsigned width    = video_info->width;
   unsigned height   = video_info->height;
   float dark[16]    =  {
      0.00, 0.00, 0.00, 0.85,
      0.00, 0.00, 0.00, 0.85,
      0.00, 0.00, 0.00, 0.85,
      0.00, 0.00, 0.00, 0.85,
   };

   float white[16]   =  {
      1.00, 1.00, 1.00, 1.00,
      1.00, 1.00, 1.00, 1.00,
      1.00, 1.00, 1.00, 1.00,
      1.00, 1.00, 1.00, 1.00,
   };

   menu_display_draw_quad(0, height/2.0, width, height/2.0,
         width, height,
         &dark[0]);

   ptr_width  = width / 11;
   ptr_height = height / 10;

   if (ptr_width >= ptr_height)
      ptr_width = ptr_height;

   for (i = 0; i < 44; i++)
   {
      int line_y        = (i / 11)*height/10.0;
      uintptr_t texture = mui->textures.list[MUI_TEXTURE_KEY];

      if (i == id)
         texture = mui->textures.list[MUI_TEXTURE_KEY_HOVER];

      menu_display_blend_begin();

      menu_display_draw_texture(
            width/2.0 - (11*ptr_width)/2.0 + (i % 11) * ptr_width,
            height/2.0 + ptr_height*1.5 + line_y,
            ptr_width, ptr_height,
            width, height,
            &white[0],
            texture);

      menu_display_draw_text(mui->font, grid[i],
            width/2.0 - (11*ptr_width)/2.0 + (i % 11) * ptr_width + ptr_width/2.0,
            height/2.0 + ptr_height + line_y + mui->font->size / 3,
            width, height, 0xffffffff, TEXT_ALIGN_CENTER, 1.0f,
            false, 0);
   }
}

/* Returns the OSK key at a given position */
static int mui_osk_ptr_at_pos(void *data, int x, int y,
      unsigned width, unsigned height)
{
   unsigned i;
   int ptr_width, ptr_height;
   mui_handle_t *mui = (mui_handle_t*)data;

   if (!mui)
      return -1;

   ptr_width  = width / 11;
   ptr_height = height / 10;

   if (ptr_width >= ptr_height)
      ptr_width = ptr_height;

   for (i = 0; i < 44; i++)
   {
      int line_y    = (i / 11)*height/10.0;
      int ptr_x     = width/2.0 - (11*ptr_width)/2.0 + (i % 11) * ptr_width;
      int ptr_y     = height/2.0 + ptr_height*1.5 + line_y - ptr_height;

      if (x > ptr_x && x < ptr_x + ptr_width
       && y > ptr_y && y < ptr_y + ptr_height)
         return i;
   }

   return -1;
}

/* Draw the tabs background */
static void mui_draw_tab_begin(mui_handle_t *mui,
      unsigned width, unsigned height,
      float *tabs_bg_color, float *tabs_separator_color)
{
   float scale_factor = menu_display_get_dpi();

   mui->tabs_height   = scale_factor / 3;

   /* tabs background */
   menu_display_draw_quad(0, height - mui->tabs_height, width,
         mui->tabs_height,
         width, height,
         tabs_bg_color);

   /* tabs separator */
   menu_display_draw_quad(0, height - mui->tabs_height, width,
         1,
         width, height,
         tabs_separator_color);
}

/* Draw the active tab */
static void mui_draw_tab_end(mui_handle_t *mui,
      unsigned width, unsigned height,
      unsigned header_height,
      float *active_tab_marker_color)
{
   /* active tab marker */
   unsigned tab_width = width / (MUI_SYSTEM_TAB_END+1);

   menu_display_draw_quad(
        (int)(mui->categories.selection_ptr * tab_width),
         height - (header_height/16),
         tab_width,
         header_height/16,
         width, height,
         &active_tab_marker_color[0]);
}

/* Compute the total height of the scrollable content */
static float mui_content_height(void)
{
   unsigned i;
   file_list_t *list  = menu_entries_get_selection_buf_ptr(0);
   float sum          = 0;
   size_t entries_end = menu_entries_get_end();

   for (i = 0; i < entries_end; i++)
   {
      mui_node_t *node  = (mui_node_t*)
         menu_entries_get_userdata_at_offset(list, i);
      sum              += node->line_height;
   }
   return sum;
}

/* Draw the scrollbar */
static void mui_draw_scrollbar(mui_handle_t *mui,
      unsigned width, unsigned height, float *coord_color)
{
   unsigned header_height = menu_display_get_header_height();
   float content_height   = mui_content_height();
   float total_height     = height - header_height - mui->tabs_height;
   float scrollbar_margin = mui->scrollbar_width;
   float scrollbar_height = total_height / (content_height / total_height);
   float y                = total_height * mui->scroll_y / content_height;

   /* apply a margin on the top and bottom of the scrollbar for aestetic */
   scrollbar_height      -= scrollbar_margin * 2;
   y                     += scrollbar_margin;

   if (content_height < total_height)
      return;

   /* if the scrollbar is extremely short, display it as a square */
   if (scrollbar_height <= mui->scrollbar_width)
      scrollbar_height = mui->scrollbar_width;

   menu_display_draw_quad(
         width - mui->scrollbar_width - scrollbar_margin,
         header_height + y,
         mui->scrollbar_width,
         scrollbar_height,
         width, height,
         coord_color);
}

static void mui_get_message(void *data, const char *message)
{
   mui_handle_t *mui   = (mui_handle_t*)data;

   if (!mui || !message || !*message)
      return;

   strlcpy(mui->box_message, message, sizeof(mui->box_message));
}

/* Draw the modal */
static void mui_render_messagebox(mui_handle_t *mui,
      video_frame_info_t *video_info,
      const char *message, float *body_bg_color, uint32_t font_color)
{
   unsigned i, y_position;
   int x, y, line_height, longest = 0, longest_width = 0;
   unsigned width           = video_info->width;
   unsigned height          = video_info->height;
   struct string_list *list = (struct string_list*)
      string_split(message, "\n");

   if (!list)
      return;
   if (list->elems == 0)
      goto end;

   line_height = mui->font->size * 1.2;

   y_position = height / 2;
   if (menu_input_dialog_get_display_kb())
      y_position = height / 4;

   x = width  / 2;
   y = (int)(y_position - (list->size-1) * line_height / 2);

   /* find the longest line width */
   for (i = 0; i < list->size; i++)
   {
      const char *msg = list->elems[i].data;
      int len         = (int)utf8len(msg);
      if (len > longest)
      {
         longest = len;
         longest_width = font_driver_get_message_width(mui->font, msg, strlen(msg), 1);
      }
   }

   menu_display_set_alpha(body_bg_color, 1.0);

   menu_display_draw_quad(         x - longest_width/2.0 - mui->margin*2.0,
         y - line_height/2.0 - mui->margin*2.0,
         longest_width + mui->margin*4.0,
         line_height * list->size + mui->margin*4.0,
         width,
         height,
         &body_bg_color[0]);

   /* print each line */
   for (i = 0; i < list->size; i++)
   {
      const char *msg = list->elems[i].data;
      if (msg)
         menu_display_draw_text(mui->font, msg,
               x - longest_width/2.0,
               y + i * line_height + mui->font->size / 3,
               width, height, font_color, TEXT_ALIGN_LEFT, 1.0f, false, 0);

   }

   if (menu_input_dialog_get_display_kb())
      mui_render_keyboard(mui,
            video_info,
            menu_event_get_osk_grid(), menu_event_get_osk_ptr());

end:
   string_list_free(list);
}

/* Used for the sublabels */
static unsigned mui_count_lines(const char *str)
{
   unsigned c     = 0;
   unsigned lines = 1;

   for (c = 0; str[c]; c++)
      lines += (str[c] == '\n');
   return lines;
}

/* Compute the line height for each menu entries. */
static void mui_compute_entries_box(mui_handle_t* mui, int width)
{
   unsigned i;
   size_t usable_width = width - (mui->margin * 2);
   file_list_t *list   = menu_entries_get_selection_buf_ptr(0);
   float sum           = 0;
   size_t entries_end  = menu_entries_get_end();
   float scale_factor  = menu_display_get_dpi();

   for (i = 0; i < entries_end; i++)
   {
      char sublabel_str[255];
      unsigned lines   = 0;
      mui_node_t *node = (mui_node_t*)
            menu_entries_get_userdata_at_offset(list, i);

      sublabel_str[0]  = '\0';

      if (menu_entry_get_sublabel(i, sublabel_str, sizeof(sublabel_str)))
      {
         word_wrap(sublabel_str, sublabel_str, (int)(usable_width / mui->glyph_width2), false);
         lines = mui_count_lines(sublabel_str);
      }

      node->line_height  = (scale_factor / 3) + (lines * mui->font->size);
      node->y            = sum;
      sum               += node->line_height;
   }
}

/* Called on each frame. We use this callback to implement the touch scroll
with acceleration */
static void mui_render(void *data, bool is_idle)
{
   menu_animation_ctx_delta_t delta;
   float delta_time;
   unsigned bottom, width, height, header_height;
   size_t i             = 0;
   mui_handle_t *mui    = (mui_handle_t*)data;
   settings_t *settings = config_get_ptr();
   file_list_t *list    = menu_entries_get_selection_buf_ptr(0);

   if (!mui)
      return;

   video_driver_get_size(&width, &height);

   mui_compute_entries_box(mui, width);

   menu_animation_ctl(MENU_ANIMATION_CTL_DELTA_TIME, &delta_time);

   delta.current = delta_time;

   if (menu_animation_get_ideal_delta_time(&delta))
      menu_animation_update(delta.ideal);

   menu_display_set_width(width);
   menu_display_set_height(height);
   header_height = menu_display_get_header_height();

   if (settings->bools.menu_pointer_enable)
   {
      size_t ii;
      int16_t        pointer_y = menu_input_pointer_state(MENU_POINTER_Y_AXIS);
      float    old_accel_val   = 0.0f;
      float new_accel_val      = 0.0f;
      size_t entries_end       = menu_entries_get_size();

      for (ii = 0; ii < entries_end; ii++)
      {
         mui_node_t *node = (mui_node_t*)
               menu_entries_get_userdata_at_offset(list, ii);

         if (pointer_y > (-mui->scroll_y + header_height + node->y)
          && pointer_y < (-mui->scroll_y + header_height + node->y + node->line_height)
         )
         menu_input_ctl(MENU_INPUT_CTL_POINTER_PTR, &ii);
      }

      menu_input_ctl(MENU_INPUT_CTL_POINTER_ACCEL_READ, &old_accel_val);

      mui->scroll_y            -= old_accel_val;

      new_accel_val = old_accel_val * 0.96;

      menu_input_ctl(MENU_INPUT_CTL_POINTER_ACCEL_WRITE, &new_accel_val);
   }

   if (settings->bools.menu_mouse_enable)
   {
      size_t ii;
      int16_t mouse_y          = menu_input_mouse_state(MENU_MOUSE_Y_AXIS);
      size_t entries_end       = menu_entries_get_size();

      for (ii = 0; ii < entries_end; ii++)
      {
         mui_node_t *node = (mui_node_t*)
               menu_entries_get_userdata_at_offset(list, ii);

         if (mouse_y > (-mui->scroll_y + header_height + node->y)
          && mouse_y < (-mui->scroll_y + header_height + node->y + node->line_height)
         )
         menu_input_ctl(MENU_INPUT_CTL_MOUSE_PTR, &ii);
      }
   }

   if (mui->scroll_y < 0)
      mui->scroll_y = 0;

   bottom = mui_content_height() - height + header_height + mui->tabs_height;
   if (mui->scroll_y > bottom)
      mui->scroll_y = bottom;

   if (mui_content_height()
         < height - header_height - mui->tabs_height)
      mui->scroll_y = 0;

   menu_entries_ctl(MENU_ENTRIES_CTL_SET_START, &i);
}

/* Display an entry value on the right of the screen. */
static void mui_render_label_value(mui_handle_t *mui, mui_node_t *node,
      int i, int y, unsigned width, unsigned height,
      uint64_t index, uint32_t color, bool selected, const char *label,
      const char *value, float *label_color)
{
   /* This will be used instead of label_color if texture_switch is 'off' icon */
   float pure_white[16]=  {
      1.00, 1.00, 1.00, 1.00,
      1.00, 1.00, 1.00, 1.00,
      1.00, 1.00, 1.00, 1.00,
      1.00, 1.00, 1.00, 1.00,
   };

   menu_animation_ctx_ticker_t ticker;
   char label_str[255];
   char sublabel_str[255];
   char value_str[255];
   bool switch_is_on               = true;
   int value_len                   = (int)utf8len(value);
   int ticker_limit                = 0;
   uintptr_t texture_switch        = 0;
   uintptr_t texture_switch2       = 0;
   bool do_draw_text               = false;
   size_t usable_width             = width - (mui->margin * 2);
   uint32_t sublabel_color         = theme.sublabel_normal_color;
   enum msg_file_type hash_type    = msg_hash_to_file_type(msg_hash_calculate(value));
   float scale_factor              = menu_display_get_dpi();

   label_str[0] = value_str[0]     = 
      sublabel_str[0]              = '\0';

   if (value_len * mui->glyph_width > usable_width / 2)
      value_len    = (int)((usable_width/2) / mui->glyph_width);

   ticker_limit    = (int)((usable_width / mui->glyph_width) - (value_len + 2));

   ticker.s        = label_str;
   ticker.len      = ticker_limit;
   ticker.idx      = index;
   ticker.str      = label;
   ticker.selected = selected;

   menu_animation_ticker(&ticker);

   ticker.s        = value_str;
   ticker.len      = value_len;
   ticker.str      = value;

   menu_animation_ticker(&ticker);

   /* set switch_is_on */
   /* set texture_switch */
   if (string_is_equal(value, msg_hash_to_str(MENU_ENUM_LABEL_DISABLED)) ||
         (string_is_equal(value, msg_hash_to_str(MENU_ENUM_LABEL_VALUE_OFF))))
   {
      if (mui->textures.list[MUI_TEXTURE_SWITCH_OFF])
      {
         switch_is_on = false;
         texture_switch = mui->textures.list[MUI_TEXTURE_SWITCH_OFF];
      }
      else
         do_draw_text = true;
   }
   else if (string_is_equal(value, msg_hash_to_str(MENU_ENUM_LABEL_ENABLED)) ||
            (string_is_equal(value, msg_hash_to_str(MENU_ENUM_LABEL_VALUE_ON))))
   {
      if (mui->textures.list[MUI_TEXTURE_SWITCH_ON])
      {
         switch_is_on = true;
         texture_switch = mui->textures.list[MUI_TEXTURE_SWITCH_ON];
      }
      else
         do_draw_text = true;
   }
   /* set do_draw_text */
   else
   {
      switch (hash_type)
      {
         case FILE_TYPE_IN_CARCHIVE:
         case FILE_TYPE_COMPRESSED:
         case FILE_TYPE_MORE:
         case FILE_TYPE_CORE:
         case FILE_TYPE_DIRECT_LOAD:
         case FILE_TYPE_RDB:
         case FILE_TYPE_CURSOR:
         case FILE_TYPE_PLAIN:
         case FILE_TYPE_DIRECTORY:
         case FILE_TYPE_MUSIC:
         case FILE_TYPE_IMAGE:
         case FILE_TYPE_MOVIE:
            break;
         default:
            do_draw_text = true;
            break;
      }
   }

   /* set texture_switch2 */
   if (node->texture_switch2_set)
      texture_switch2 = node->texture_switch2;
   else
   {
      switch (hash_type)
      {
         case FILE_TYPE_COMPRESSED:
            texture_switch2 = mui->textures.list[MUI_TEXTURE_ARCHIVE];
            break;
         case FILE_TYPE_IMAGE:
            texture_switch2 = mui->textures.list[MUI_TEXTURE_IMAGE];
            break;
         default:
            break;
      }
   }

   /* Sublabel */
   if (menu_entry_get_sublabel(i, sublabel_str, sizeof(sublabel_str)))
   {
      word_wrap(sublabel_str, sublabel_str, (int)(usable_width / mui->glyph_width2), false);

      menu_display_draw_text(mui->font2, sublabel_str,
            mui->margin + (texture_switch2 ? mui->icon_size : 0),
            y + (scale_factor / 4) + mui->font->size,
            width, height, sublabel_color, TEXT_ALIGN_LEFT, 1.0f, false, 0);
   }

   menu_display_draw_text(mui->font, label_str,
         mui->margin + (texture_switch2 ? mui->icon_size : 0),
         y + (scale_factor / 5),
         width, height, color, TEXT_ALIGN_LEFT, 1.0f, false, 0);

   if (do_draw_text)
      menu_display_draw_text(mui->font, value_str,
            width - mui->margin,
            y + (scale_factor / 5),
            width, height, color, TEXT_ALIGN_RIGHT, 1.0f, false, 0);

   if (texture_switch2)
      mui_draw_icon(
            mui->icon_size,
            (uintptr_t)texture_switch2,
            0,
            y + (scale_factor / 6) - mui->icon_size/2,
            width,
            height,
            0,
            1,
            &label_color[0]
      );

   if (texture_switch)
      mui_draw_icon(
            mui->icon_size,
            (uintptr_t)texture_switch,
            width - mui->margin - mui->icon_size,
            y + (scale_factor / 6) - mui->icon_size/2,
            width,
            height,
            0,
            1,
            switch_is_on ? &label_color[0] :  &pure_white[0]
      );
}

static void mui_render_menu_list(
      video_frame_info_t *video_info,
      mui_handle_t *mui,
      unsigned width, unsigned height,
      uint32_t font_normal_color,
      uint32_t font_hover_color,
      float *menu_list_color)
{
   size_t i;
   float sum                               = 0;
   size_t entries_end                      = 0;
   file_list_t *list                       = NULL;
   uint64_t frame_count                    = mui->frame_count;
   unsigned header_height                  = 
      menu_display_get_header_height();

   mui->raster_block.carr.coords.vertices  = 0;
   mui->raster_block2.carr.coords.vertices = 0;

   menu_entries_ctl(MENU_ENTRIES_CTL_START_GET, &i);

   list                                    = 
      menu_entries_get_selection_buf_ptr(0);
   
   entries_end = menu_entries_get_end();

   for (i = 0; i < entries_end; i++)
   {
      char rich_label[255];
      char entry_value[255];
      bool entry_selected = false;
      mui_node_t *node    = (mui_node_t*)
            menu_entries_get_userdata_at_offset(list, i);
      size_t selection    = menu_navigation_get_selection();
      int               y = header_height - mui->scroll_y + sum;
      rich_label[0]       = 
         entry_value[0]   = '\0';

      menu_entry_get_value((unsigned)i, NULL, entry_value, sizeof(entry_value));
      menu_entry_get_rich_label((unsigned)i, rich_label, sizeof(rich_label));

      entry_selected = selection == i;

      /* Render label, value, and associated icons */

      mui_render_label_value(
         mui,
         node,
         (int)i,
         y,
         width,
         height,
         frame_count / 20,
         font_hover_color,
         entry_selected,
         rich_label,
         entry_value,
         menu_list_color
      );

      sum += node->line_height;
   }
}


static size_t mui_list_get_size(void *data, enum menu_list_type type)
{
   switch (type)
   {
      case MENU_LIST_PLAIN:
         return menu_entries_get_stack_size(0);
      case MENU_LIST_TABS:
         return MUI_SYSTEM_TAB_END;
      default:
         break;
   }

   return 0;
}

static int mui_get_core_title(char *s, size_t len)
{
   settings_t *settings           = config_get_ptr();
   const char *core_name          = NULL;
   const char *core_version       = NULL;
   rarch_system_info_t *info      = runloop_get_system_info();
   struct retro_system_info *system = &info->info;

   core_name                      = system->library_name;
   core_version                   = system->library_version;

   if (!settings->bools.menu_core_enable)
      return -1;

   if (info)
   {
      if (string_is_empty(core_name))
         core_name = info->info.library_name;
      if (!core_version)
         core_version = info->info.library_version;
   }

   if (string_is_empty(core_name))
      core_name    = msg_hash_to_str(MENU_ENUM_LABEL_VALUE_NO_CORE);
   if (!core_version)
      core_version = "";

   snprintf(s, len, "%s %s", core_name, core_version);

   return 0;
}

static void mui_draw_bg(menu_display_ctx_draw_t *draw,
      video_frame_info_t *video_info)
{
   bool add_opacity       = false;
   float opacity_override = video_info->menu_wallpaper_opacity;

   menu_display_blend_begin();

   draw->x               = 0;
   draw->y               = 0;
   draw->pipeline.id     = 0;
   draw->pipeline.active = false;

   if (video_info->libretro_running)
   {
      add_opacity      = true;
      opacity_override = video_info->menu_framebuffer_opacity;
   }

   menu_display_draw_bg(draw, video_info, add_opacity,
         opacity_override);
   menu_display_draw(draw);
   menu_display_blend_end();
}

/* Main function of the menu driver. Takes care of drawing the header, the tabs,
and the menu list */
static void mui_frame(void *data, video_frame_info_t *video_info)
{
   mui_handle_t *mui = (mui_handle_t*)data;
   if (!mui)
      return;

   /* This controls the main background color */
   menu_display_ctx_clearcolor_t clearcolor;
   
   menu_animation_ctx_ticker_t ticker;
   menu_display_ctx_draw_t draw;
   char msg[255];
   char title[255];
   char title_buf[255];
   char title_msg[255];
   float black_bg[16];
   float pure_white[16];
   float white_bg[16]; 
   float white_transp_bg[16];
   float grey_bg[16];
   float shadow_bg[16]=  {
      0.00, 0.00, 0.00, 0.00,
      0.00, 0.00, 0.00, 0.00,
      0.00, 0.00, 0.00, 0.20,
      0.00, 0.00, 0.00, 0.20,
   };

   file_list_t *list               = NULL;
   mui_node_t *node                = NULL;
   unsigned width                  = video_info->width;
   unsigned height                 = video_info->height;
   unsigned ticker_limit           = 0;
   unsigned i                      = 0;
   unsigned header_height          = 0;
   size_t selection                = 0;
   size_t title_margin             = 0;
   
   bool background_rendered        = false;
   bool libretro_running           = video_info->libretro_running;

   mui->frame_count++;

   msg[0] = title[0] = title_buf[0] = title_msg[0] = '\0';

   /* https://material.google.com/style/color.html#color-color-palette */
   uint32_t blue_500          = 0x2196F3;
   uint32_t blue_50           = 0xE3F2FD;
   uint32_t blue_grey_500     = 0x607D8B;
   uint32_t blue_grey_50      = 0xECEFF1;
   uint32_t red_500           = 0xF44336;
   uint32_t red_50            = 0xFFEBEE;
   uint32_t green_500         = 0x4CAF50;
   uint32_t green_50          = 0xE8F5E9;
   uint32_t yellow_500        = 0xFFEB3B;
   uint32_t yellow_50         = 0xFFFDE7;

   uint32_t greyish_blue      = 0x38474F;
   uint32_t almost_black      = 0x212121;
   uint32_t color_nv_body     = 0x202427;
   uint32_t color_nv_accent   = 0x77B900;
   uint32_t color_nv_header   = 0x282F37 ;

   uint32_t black_opaque_54        = 0x0000008A;
   uint32_t black_opaque_87        = 0x000000DE;
   uint32_t white_opaque_70        = 0xFFFFFFB3;

   /* Palette of colors needed throughout the file */
   hex32_to_rgba_normalized(0x000000, black_bg, 0.75);
   hex32_to_rgba_normalized(0xFFFFFF, pure_white, 1.0);
   hex32_to_rgba_normalized(0xFAFAFA, white_bg, 1.0);
   hex32_to_rgba_normalized(0xFAFAFA, white_transp_bg, 0.90);
   hex32_to_rgba_normalized(0xC7C7C7, grey_bg, 0.90);

   memcpy(theme.passive_tab_icon_color, grey_bg, sizeof(grey_bg));

   switch (video_info->materialui_color_theme)
   {
      case MATERIALUI_THEME_BLUE:
         hex32_to_rgba_normalized(blue_500,  theme.header_bg_color,         1.00);
         hex32_to_rgba_normalized(blue_50,   theme.highlighted_entry_color, 0.90);
         hex32_to_rgba_normalized(0xFFFFFF,  theme.footer_bg_color,         1.00);
         hex32_to_rgba_normalized(0xFAFAFA,  theme.body_bg_color,           0.90);
         hex32_to_rgba_normalized(blue_500,  theme.active_tab_marker_color, 1.00);

         theme.font_normal_color     = black_opaque_54;
         theme.font_hover_color      = black_opaque_87;
         theme.font_header_color     = 0xffffffff;
         theme.sublabel_normal_color = 0x888888ff;
         theme.sublabel_hover_color  = 0x888888ff;

         clearcolor.r            = 1.00f;
         clearcolor.g            = 1.00f;
         clearcolor.b            = 1.00f;
         clearcolor.a            = 0.75f;
         break;
      case MATERIALUI_THEME_BLUE_GREY:
         hex32_to_rgba_normalized(blue_grey_500,   theme.header_bg_color,         1.00);
         hex32_to_rgba_normalized(blue_grey_50,    theme.highlighted_entry_color, 0.90);
         hex32_to_rgba_normalized(0xFFFFFF,        theme.footer_bg_color,         1.00);
         hex32_to_rgba_normalized(0xFAFAFA,        theme.body_bg_color,           0.90);
         hex32_to_rgba_normalized(blue_grey_500,   theme.active_tab_marker_color, 1.00);

         theme.font_normal_color     = black_opaque_54;
         theme.font_hover_color      = black_opaque_87;
         theme.font_header_color     = 0xFFFFFFFF;
         theme.sublabel_normal_color = 0x888888FF;
         theme.sublabel_hover_color  = 0x888888FF;

         clearcolor.g            = 1.00f;
         clearcolor.r            = 1.00f;
         clearcolor.b            = 1.00f;
         clearcolor.a            = 0.75f;
         break;
      case MATERIALUI_THEME_GREEN:
         hex32_to_rgba_normalized(green_500, theme.header_bg_color,         1.00);
         hex32_to_rgba_normalized(green_50,  theme.highlighted_entry_color, 0.90);
         hex32_to_rgba_normalized(0xFFFFFF,  theme.footer_bg_color,         1.00);
         hex32_to_rgba_normalized(0xFAFAFA,  theme.body_bg_color,           0.90);
         hex32_to_rgba_normalized(green_500, theme.active_tab_marker_color, 1.00);

         theme.font_normal_color     = black_opaque_54;
         theme.font_hover_color      = black_opaque_87;
         theme.font_header_color     = 0xFFFFFFFF;
         theme.sublabel_normal_color = 0x888888FF;
         theme.sublabel_hover_color  = 0x888888FF;

         clearcolor.r            = 1.0f;
         clearcolor.g            = 1.0f;
         clearcolor.b            = 1.0f;
         clearcolor.a            = 0.75f;
         break;
      case MATERIALUI_THEME_RED:
         hex32_to_rgba_normalized(red_500,   theme.header_bg_color,         1.00);
         hex32_to_rgba_normalized(red_50,    theme.highlighted_entry_color, 0.90);
         hex32_to_rgba_normalized(0xFFFFFF,  theme.footer_bg_color,         1.00);
         hex32_to_rgba_normalized(0xFAFAFA,  theme.body_bg_color,           0.90);
         hex32_to_rgba_normalized(red_500,   theme.active_tab_marker_color, 1.00);

         theme.font_normal_color     = black_opaque_54;
         theme.font_hover_color      = black_opaque_87;
         theme.font_header_color     = 0xFFFFFFFF;
         theme.sublabel_normal_color = 0x888888FF;
         theme.sublabel_hover_color  = 0x888888FF;


         clearcolor.r            = 1.0f;
         clearcolor.g            = 1.0f;
         clearcolor.b            = 1.0f;
         clearcolor.a            = 0.75f;
         break;
      case MATERIALUI_THEME_YELLOW:
         hex32_to_rgba_normalized(yellow_500, theme.header_bg_color,         1.00);
         hex32_to_rgba_normalized(yellow_50, theme.highlighted_entry_color,  0.90);
         hex32_to_rgba_normalized(0xFFFFFF, theme.footer_bg_color,           1.00);
         hex32_to_rgba_normalized(0xFAFAFA, theme.body_bg_color,             0.90);
         hex32_to_rgba_normalized(yellow_500, theme.active_tab_marker_color, 1.00);

         theme.font_normal_color     = black_opaque_54;
         theme.font_hover_color      = black_opaque_87;
         theme.font_header_color     = 0xBBBBBBBB;
         theme.sublabel_normal_color = 0x888888FF;
         theme.sublabel_hover_color  = 0x888888ff;

         clearcolor.r            = 1.0f;
         clearcolor.g            = 1.0f;
         clearcolor.b            = 1.0f;
         clearcolor.a            = 0.75f;
         break;
      case MATERIALUI_THEME_DARK_BLUE:
         hex32_to_rgba_normalized(greyish_blue, theme.header_bg_color,         1.00);
         hex32_to_rgba_normalized(0xC7C7C7,     theme.highlighted_entry_color, 0.90);
         hex32_to_rgba_normalized(0x212121,     theme.footer_bg_color,         1.00);
         hex32_to_rgba_normalized(0x212121,     theme.body_bg_color,           0.90);
         hex32_to_rgba_normalized(0x566066,     theme.active_tab_marker_color, 1.00);

         theme.font_normal_color       = white_opaque_70;
         theme.font_hover_color        = 0xFFFFFFFF;
         theme.font_header_color       = 0xFFFFFFFF;
         theme.sublabel_normal_color   = white_opaque_70;
         theme.sublabel_hover_color    = white_opaque_70;


         clearcolor.r            = theme.body_bg_color[0];
         clearcolor.g            = theme.body_bg_color[1];
         clearcolor.b            = theme.body_bg_color[2];
         clearcolor.a            = 0.75f;
         break;
      case MATERIALUI_THEME_NVIDIA_SHIELD:
         hex32_to_rgba_normalized(color_nv_header, theme.header_bg_color,         1.00);
         hex32_to_rgba_normalized(color_nv_accent, theme.highlighted_entry_color, 0.90);
         hex32_to_rgba_normalized(color_nv_body,   theme.footer_bg_color,         1.00);
         hex32_to_rgba_normalized(color_nv_body,   theme.body_bg_color,           0.90);
         hex32_to_rgba_normalized(0xFFFFFF, theme.active_tab_marker_color, 0.90);

         theme.font_normal_color     = white_opaque_70;
         theme.font_hover_color      = 0xFFFFFFFF;
         theme.font_header_color     = 0xFFFFFFFF;
         theme.sublabel_normal_color = white_opaque_70;
         theme.sublabel_hover_color  = white_opaque_70;

         clearcolor.r            = theme.body_bg_color[0];
         clearcolor.g            = theme.body_bg_color[1];
         clearcolor.b            = theme.body_bg_color[2];
         clearcolor.a            = 0.75f;
         break;
   }

   menu_display_set_alpha(theme.header_bg_color, video_info->menu_header_opacity);
   menu_display_set_alpha(theme.footer_bg_color, video_info->menu_footer_opacity);

   menu_display_set_viewport(video_info->width, video_info->height);
   header_height = menu_display_get_header_height();

   if (libretro_running)
   {
      draw.x                  = 0;
      draw.y                  = 0;
      draw.width              = width;
      draw.height             = height;
      draw.coords             = NULL;
      draw.matrix_data        = NULL;
      draw.texture            = menu_display_white_texture;
      draw.prim_type          = MENU_DISPLAY_PRIM_TRIANGLESTRIP;
      draw.color              = &theme.body_bg_color[0];
      draw.vertex             = NULL;
      draw.tex_coord          = NULL;
      draw.vertex_count       = 4;

      draw.pipeline.id        = 0;
      draw.pipeline.active    = false;
      draw.pipeline.backend_data = NULL;

      mui_draw_bg(&draw, video_info);
   }
   else
   {
      menu_display_clear_color(&clearcolor);

      if (mui->textures.bg)
      {
         background_rendered     = true;

         menu_display_set_alpha(white_transp_bg, 0.30);

         draw.x                  = 0;
         draw.y                  = 0;
         draw.width              = width;
         draw.height             = height;
         draw.coords             = NULL;
         draw.matrix_data        = NULL;
         draw.texture            = mui->textures.bg;
         draw.prim_type          = MENU_DISPLAY_PRIM_TRIANGLESTRIP;
         draw.color              = &white_transp_bg[0];
         draw.vertex             = NULL;
         draw.tex_coord          = NULL;
         draw.vertex_count       = 4;

         draw.pipeline.id        = 0;
         draw.pipeline.active    = false;
         draw.pipeline.backend_data = NULL;

         if (draw.texture)
            draw.color           = &white_bg[0];

         mui_draw_bg(&draw, video_info);

         /* Restore opacity of transposed white background */
         menu_display_set_alpha(white_transp_bg, 0.90);
      }
   }

   menu_entries_get_title(title, sizeof(title));

   selection = menu_navigation_get_selection();

   if (background_rendered || libretro_running)
      menu_display_set_alpha(grey_bg, 0.75);
   else
      menu_display_set_alpha(grey_bg, 1.0);

   /* highlighted entry */
   list             = menu_entries_get_selection_buf_ptr(0);
   node             = (mui_node_t*)menu_entries_get_userdata_at_offset(
         list, selection);

   if (node)
      menu_display_draw_quad(
      0,
      header_height - mui->scroll_y + node->y,
      width,
      node->line_height,
      width,
      height,
      &theme.highlighted_entry_color[0]
   );

   font_driver_bind_block(mui->font, &mui->raster_block);
   font_driver_bind_block(mui->font2, &mui->raster_block2);

   if (menu_display_get_update_pending())
      mui_render_menu_list(
            video_info,
            mui,
            width,
            height,
            theme.font_normal_color,
            theme.font_hover_color,
            &theme.active_tab_marker_color[0]
            );

   font_driver_flush(video_info->width, video_info->height, mui->font);
   font_driver_bind_block(mui->font, NULL);

   font_driver_flush(video_info->width, video_info->height, mui->font2);
   font_driver_bind_block(mui->font2, NULL);

   menu_animation_ctl(MENU_ANIMATION_CTL_SET_ACTIVE, NULL);

   /* header */
   menu_display_draw_quad(
      0,
      0,
      width,
      header_height,
      width,
      height,
      &theme.header_bg_color[0]);

   mui->tabs_height = 0;

   /* display tabs if depth equal one, if not hide them */
   if (mui_list_get_size(mui, MENU_LIST_PLAIN) == 1)
   {
      mui_draw_tab_begin(mui, width, height, &theme.footer_bg_color[0], &grey_bg[0]);

      for (i = 0; i <= MUI_SYSTEM_TAB_END; i++)
         mui_draw_tab(mui, i, width, height, &theme.passive_tab_icon_color[0], &theme.active_tab_marker_color[0]);

      mui_draw_tab_end(mui, width, height, header_height, &theme.active_tab_marker_color[0]);
   }

   menu_display_draw_quad(
      0,
      header_height,
      width,
      mui->shadow_height,
      width,
      height,
      &shadow_bg[0]);

   title_margin = mui->margin;

   if (menu_entries_ctl(MENU_ENTRIES_CTL_SHOW_BACK, NULL))
   {
      title_margin = mui->icon_size;
      mui_draw_icon(
         mui->icon_size,
         mui->textures.list[MUI_TEXTURE_BACK],
         0,
         0,
         width,
         height,
         0,
         1,
         &pure_white[0]
      );
   }

   ticker_limit = (width - mui->margin*2) / mui->glyph_width;

   ticker.s        = title_buf;
   ticker.len      = ticker_limit;
   ticker.idx      = mui->frame_count / 100;
   ticker.str      = title;
   ticker.selected = true;

   menu_animation_ticker(&ticker);

   /* Title */
   if (mui_get_core_title(title_msg, sizeof(title_msg)) == 0)
   {
      int ticker_limit, value_len;
      char title_buf_msg_tmp[255];
      char title_buf_msg[255];
      size_t         usable_width = width - (mui->margin * 2);

      title_buf_msg_tmp[0] = title_buf_msg[0] = '\0';

      snprintf(title_buf_msg, sizeof(title_buf), "%s (%s)",
            title_buf, title_msg);
      value_len       = (int)utf8len(title_buf);
      ticker_limit    = (int)((usable_width / mui->glyph_width) - (value_len + 2));

      ticker.s        = title_buf_msg_tmp;
      ticker.len      = ticker_limit;
      ticker.idx      = mui->frame_count / 20;
      ticker.str      = title_buf_msg;
      ticker.selected = true;

      menu_animation_ticker(&ticker);

      strlcpy(title_buf, title_buf_msg_tmp, sizeof(title_buf));
   }

   menu_display_draw_text(mui->font, title_buf,
         title_margin,
         header_height / 2 + mui->font->size / 3,
         width, height, theme.font_header_color, TEXT_ALIGN_LEFT, 1.0f, false, 0);

   mui_draw_scrollbar(mui, width, height, &grey_bg[0]);

   if (menu_input_dialog_get_display_kb())
   {
      const char *str          = menu_input_dialog_get_buffer();
      const char *label        = menu_input_dialog_get_label_buffer();

      menu_display_draw_quad(0, 0, width, height, width, height, &black_bg[0]);
      snprintf(msg, sizeof(msg), "%s\n%s", label, str);
   
      mui_render_messagebox(mui, video_info,
               msg, &theme.body_bg_color[0], theme.font_hover_color);
   }

   if (!string_is_empty(mui->box_message))
   {
      menu_display_draw_quad(0, 0, width, height, width, height, &black_bg[0]);

      mui_render_messagebox(mui, video_info,
               mui->box_message, &theme.body_bg_color[0], theme.font_hover_color);
      
      mui->box_message[0] = '\0';
   }

   if (mui->mouse_show)
      menu_display_draw_cursor(
            &white_bg[0],
            mui->cursor.size,
            mui->textures.list[MUI_TEXTURE_POINTER],
            menu_input_mouse_state(MENU_MOUSE_X_AXIS),
            menu_input_mouse_state(MENU_MOUSE_Y_AXIS),
            width,
            height);

   menu_display_restore_clear_color();
   menu_display_unset_viewport(video_info->width, video_info->height);
}

/* Compute the positions of the widgets */
static void mui_layout(mui_handle_t *mui, bool video_is_threaded)
{
   float scale_factor;
   int new_font_size, new_font_size2;
   unsigned width, height, new_header_height;

   video_driver_get_size(&width, &height);

   /* Mobiles platforms may have very small display metrics
    * coupled to a high resolution, so we should be DPI aware
    * to ensure the entries hitboxes are big enough.
    *
    * On desktops, we just care about readability, with every widget
    * size proportional to the display width. */
   scale_factor         = menu_display_get_dpi();

   new_header_height    = scale_factor / 3;
   new_font_size        = scale_factor / 9;
   new_font_size2       = scale_factor / 12;

   mui->shadow_height   = scale_factor / 36;
   mui->scrollbar_width = scale_factor / 36;
   mui->tabs_height     = scale_factor / 3;
   mui->line_height     = scale_factor / 3;
   mui->margin          = scale_factor / 9;
   mui->icon_size       = scale_factor / 3;

   menu_display_set_header_height(new_header_height);

   /* we assume the average glyph aspect ratio is close to 3:4 */
   mui->glyph_width = new_font_size * 3/4;
   mui->glyph_width2 = new_font_size2 * 3/4;

   mui->font = menu_display_font(
         APPLICATION_SPECIAL_DIRECTORY_ASSETS_MATERIALUI_FONT,
         new_font_size,
         video_is_threaded);

   mui->font2 = menu_display_font(
         APPLICATION_SPECIAL_DIRECTORY_ASSETS_MATERIALUI_FONT,
         new_font_size2,
         video_is_threaded);

   if (mui->font) /* calculate a more realistic ticker_limit */
   {
      unsigned m_width =
         font_driver_get_message_width(mui->font, "a", 1, 1);

      if (m_width)
         mui->glyph_width = m_width;
   }

   if (mui->font2) /* calculate a more realistic ticker_limit */
   {
      unsigned m_width2 =
         font_driver_get_message_width(mui->font2, "t", 1, 1);

      if (m_width2)
         mui->glyph_width2 = m_width2;
   }
}

static void *mui_init(void **userdata, bool video_is_threaded)
{
   mui_handle_t   *mui = NULL;
   menu_handle_t *menu = (menu_handle_t*)
      calloc(1, sizeof(*menu));

   if (!menu)
      goto error;

   if (!menu_display_init_first_driver(video_is_threaded))
      goto error;

   mui = (mui_handle_t*)calloc(1, sizeof(mui_handle_t));

   if (!mui)
      goto error;

   *userdata = mui;

   mui->cursor.size  = 64.0;

   return menu;
error:
   if (menu)
      free(menu);
   return NULL;
}

static void mui_free(void *data)
{
   mui_handle_t *mui   = (mui_handle_t*)data;

   if (!mui)
      return;

   video_coord_array_free(&mui->raster_block.carr);
   video_coord_array_free(&mui->raster_block2.carr);

   font_driver_bind_block(NULL, NULL);
}

static void mui_context_bg_destroy(mui_handle_t *mui)
{
   if (!mui)
      return;

   video_driver_texture_unload(&mui->textures.bg);
   video_driver_texture_unload(&menu_display_white_texture);
}

static void mui_context_destroy(void *data)
{
   unsigned i;
   mui_handle_t *mui   = (mui_handle_t*)data;

   if (!mui)
      return;

   for (i = 0; i < MUI_TEXTURE_LAST; i++)
      video_driver_texture_unload(&mui->textures.list[i]);

   menu_display_font_free(mui->font);

   mui_context_bg_destroy(mui);
}

/* Upload textures to the gpu */
static bool mui_load_image(void *userdata, void *data, enum menu_image_type type)
{
   mui_handle_t *mui = (mui_handle_t*)userdata;

   switch (type)
   {
      case MENU_IMAGE_NONE:
         break;
      case MENU_IMAGE_WALLPAPER:
         mui_context_bg_destroy(mui);
         video_driver_texture_load(data,
               TEXTURE_FILTER_MIPMAP_LINEAR, &mui->textures.bg);
         menu_display_allocate_white_texture();
         break;
      case MENU_IMAGE_THUMBNAIL:
      case MENU_IMAGE_SAVESTATE_THUMBNAIL:
         break;
   }

   return true;
}

/* Compute the scroll value depending on the highlighted entry */
static float mui_get_scroll(mui_handle_t *mui)
{
   unsigned width, height, half = 0;
   size_t selection             = menu_navigation_get_selection();

   if (!mui)
      return 0;

   video_driver_get_size(&width, &height);

   if (mui->line_height)
      half = (height / mui->line_height) / 3;

   if (selection < half)
      return 0;

   return ((selection + 2 - half) * mui->line_height);
}

/* The navigation pointer has been updated (for example by pressing up or down
on the keyboard). We use this function to animate the scroll. */
static void mui_navigation_set(void *data, bool scroll)
{
   menu_animation_ctx_entry_t entry;
   mui_handle_t *mui    = (mui_handle_t*)data;
   float     scroll_pos = mui ? mui_get_scroll(mui) : 0.0f;

   if (!mui || !scroll)
      return;

   entry.duration     = 10;
   entry.target_value = scroll_pos;
   entry.subject      = &mui->scroll_y;
   entry.easing_enum  = EASING_IN_OUT_QUAD;
   entry.tag          = -1;
   entry.cb           = NULL;

   if (entry.subject)
      menu_animation_push(&entry);
}

static void mui_list_set_selection(void *data, file_list_t *list)
{
   mui_navigation_set(data, true);
}

/* The navigation pointer is set back to zero */
static void mui_navigation_clear(void *data, bool pending_push)
{
   size_t i             = 0;
   mui_handle_t *mui    = (mui_handle_t*)data;
   if (!mui)
      return;

   menu_entries_ctl(MENU_ENTRIES_CTL_SET_START, &i);
   mui->scroll_y = 0;
}

static void mui_navigation_set_last(void *data)
{
   mui_navigation_set(data, true);
}

static void mui_navigation_alphabet(void *data, size_t *unused)
{
   mui_navigation_set(data, true);
}

/* A new list had been pushed. We update the scroll value */
static void mui_populate_entries(
      void *data, const char *path,
      const char *label, unsigned i)
{
   mui_handle_t *mui    = (mui_handle_t*)data;
   if (!mui)
      return;

   mui->scroll_y = mui_get_scroll(mui);
}

/* Context reset is called on launch or when a core is launched */
static void mui_context_reset(void *data, bool is_threaded)
{
   mui_handle_t *mui              = (mui_handle_t*)data;
   settings_t *settings           = config_get_ptr();

   if (!mui || !settings)
      return;

   mui_layout(mui, is_threaded);
   mui_context_bg_destroy(mui);
   menu_display_allocate_white_texture();
   mui_context_reset_textures(mui);

   if (path_file_exists(settings->paths.path_menu_wallpaper))
      task_push_image_load(settings->paths.path_menu_wallpaper, 
            menu_display_handle_wallpaper_upload, NULL);
}

static int mui_environ(enum menu_environ_cb type, void *data, void *userdata)
{
   mui_handle_t *mui              = (mui_handle_t*)userdata;

   switch (type)
   {
      case MENU_ENVIRON_ENABLE_MOUSE_CURSOR:
         if (!mui)
            return -1;
         mui->mouse_show = true;
         break;
      case MENU_ENVIRON_DISABLE_MOUSE_CURSOR:
         if (!mui)
            return -1;
         mui->mouse_show = false;
         break;
      case 0:
      default:
         break;
   }

   return -1;
}

/* Called before we push the new list after clicking on a tab */
static void mui_preswitch_tabs(mui_handle_t *mui, unsigned action)
{
   size_t stack_size       = 0;
   file_list_t *menu_stack = NULL;

   if (!mui)
      return;

   menu_stack = menu_entries_get_menu_stack_ptr(0);
   stack_size = menu_stack->size;

   if (menu_stack->list[stack_size - 1].label)
      free(menu_stack->list[stack_size - 1].label);
   menu_stack->list[stack_size - 1].label = NULL;

   switch (mui->categories.selection_ptr)
   {
      case MUI_SYSTEM_TAB_MAIN:
         menu_stack->list[stack_size - 1].label =
            strdup(msg_hash_to_str(MENU_ENUM_LABEL_MAIN_MENU));
         menu_stack->list[stack_size - 1].type =
            MENU_SETTINGS;
         break;
      case MUI_SYSTEM_TAB_PLAYLISTS:
         menu_stack->list[stack_size - 1].label =
            strdup(msg_hash_to_str(MENU_ENUM_LABEL_PLAYLISTS_TAB));
         menu_stack->list[stack_size - 1].type =
            MENU_PLAYLISTS_TAB;
         break;
      case MUI_SYSTEM_TAB_SETTINGS:
         menu_stack->list[stack_size - 1].label =
            strdup(msg_hash_to_str(MENU_ENUM_LABEL_SETTINGS_TAB));
         menu_stack->list[stack_size - 1].type =
            MENU_SETTINGS;
         break;
   }
}

/* This callback is not caching anything. We use it to navigate the tabs
with the keyboard */
static void mui_list_cache(void *data,
      enum menu_list_type type, unsigned action)
{
   size_t list_size;
   mui_handle_t *mui   = (mui_handle_t*)data;

   if (!mui)
      return;

   list_size = MUI_SYSTEM_TAB_END;

   switch (type)
   {
      case MENU_LIST_PLAIN:
         break;
      case MENU_LIST_HORIZONTAL:
         mui->categories.selection_ptr_old = mui->categories.selection_ptr;

         switch (action)
         {
            case MENU_ACTION_LEFT:
               if (mui->categories.selection_ptr == 0)
               {
                  mui->categories.selection_ptr = list_size;
                  mui->categories.active.idx    = (unsigned)(list_size - 1);
               }
               else
                  mui->categories.selection_ptr--;
               break;
            default:
               if (mui->categories.selection_ptr == list_size)
               {
                  mui->categories.selection_ptr = 0;
                  mui->categories.active.idx = 1;
               }
               else
                  mui->categories.selection_ptr++;
               break;
         }

         mui_preswitch_tabs(mui, action);
         break;
      default:
         break;
   }
}

/* A new list has been pushed. We use this callback to customize a few lists for
this menu driver */
static int mui_list_push(void *data, void *userdata,
      menu_displaylist_info_t *info, unsigned type)
{
   menu_displaylist_ctx_parse_entry_t entry;
   int ret                = -1;
   core_info_list_t *list = NULL;
   menu_handle_t *menu    = (menu_handle_t*)data;

   (void)userdata;

   switch (type)
   {
      case DISPLAYLIST_LOAD_CONTENT_LIST:
         menu_entries_ctl(MENU_ENTRIES_CTL_CLEAR, info->list);

         menu_entries_append_enum(info->list,
               msg_hash_to_str(MENU_ENUM_LABEL_VALUE_FAVORITES),
               msg_hash_to_str(MENU_ENUM_LABEL_FAVORITES),
               MENU_ENUM_LABEL_FAVORITES,
               MENU_SETTING_ACTION, 0, 0);

         core_info_get_list(&list);
         if (core_info_list_num_info_files(list))
         {
            menu_entries_append_enum(info->list,
                  msg_hash_to_str(MENU_ENUM_LABEL_VALUE_DOWNLOADED_FILE_DETECT_CORE_LIST),
                  msg_hash_to_str(MENU_ENUM_LABEL_DOWNLOADED_FILE_DETECT_CORE_LIST),
                  MENU_ENUM_LABEL_DOWNLOADED_FILE_DETECT_CORE_LIST,
                  MENU_SETTING_ACTION, 0, 0);
         }

         if (frontend_driver_parse_drive_list(info->list, true) != 0)
            menu_entries_append_enum(info->list, "/",          
                  msg_hash_to_str(MENU_ENUM_LABEL_FILE_DETECT_CORE_LIST_PUSH_DIR),
                  MENU_ENUM_LABEL_FILE_DETECT_CORE_LIST_PUSH_DIR,
                  MENU_SETTING_ACTION, 0, 0);

         menu_entries_append_enum(info->list,
               msg_hash_to_str(MENU_ENUM_LABEL_VALUE_MENU_FILE_BROWSER_SETTINGS),
               msg_hash_to_str(MENU_ENUM_LABEL_MENU_FILE_BROWSER_SETTINGS),
               MENU_ENUM_LABEL_MENU_FILE_BROWSER_SETTINGS,
               MENU_SETTING_ACTION, 0, 0);

         info->need_push    = true;
         info->need_refresh = true;
         ret = 0;
         break;
      case DISPLAYLIST_MAIN_MENU:
         {
            rarch_system_info_t *system = runloop_get_system_info();
            menu_entries_ctl(MENU_ENTRIES_CTL_CLEAR, info->list);

            entry.data            = menu;
            entry.info            = info;
            entry.parse_type      = PARSE_ACTION;
            entry.add_empty_entry = false;

            if (!string_is_empty(system->info.library_name) &&
                  !string_is_equal(system->info.library_name,
                     msg_hash_to_str(MENU_ENUM_LABEL_VALUE_NO_CORE)))
            {
               entry.enum_idx      = MENU_ENUM_LABEL_CONTENT_SETTINGS;
               menu_displaylist_ctl(DISPLAYLIST_SETTING_ENUM, &entry);
            }

#ifndef HAVE_DYNAMIC
            if (frontend_driver_has_fork())
#endif
            {
               entry.enum_idx      = MENU_ENUM_LABEL_CORE_LIST;
               menu_displaylist_ctl(DISPLAYLIST_SETTING_ENUM, &entry);
            }

            if (system->load_no_content)
            {
               entry.enum_idx      = MENU_ENUM_LABEL_START_CORE;
               menu_displaylist_ctl(DISPLAYLIST_SETTING_ENUM, &entry);
            }


            entry.enum_idx      = MENU_ENUM_LABEL_LOAD_CONTENT_LIST;
            menu_displaylist_ctl(DISPLAYLIST_SETTING_ENUM, &entry);

            entry.enum_idx      = MENU_ENUM_LABEL_LOAD_CONTENT_HISTORY;
            menu_displaylist_ctl(DISPLAYLIST_SETTING_ENUM, &entry);

#if defined(HAVE_NETWORKING)
#ifdef HAVE_LAKKA
            entry.enum_idx      = MENU_ENUM_LABEL_UPDATE_LAKKA;
            menu_displaylist_ctl(DISPLAYLIST_SETTING_ENUM, &entry);
#else
            {
               settings_t *settings      = config_get_ptr();
               if (settings->bools.menu_show_online_updater)
               {
                  entry.enum_idx      = MENU_ENUM_LABEL_ONLINE_UPDATER;
                  menu_displaylist_ctl(DISPLAYLIST_SETTING_ENUM, &entry);
               }
            }
#endif

            entry.enum_idx      = MENU_ENUM_LABEL_NETPLAY;
            menu_displaylist_ctl(DISPLAYLIST_SETTING_ENUM, &entry);
#endif
            entry.enum_idx      = MENU_ENUM_LABEL_INFORMATION_LIST;
            menu_displaylist_ctl(DISPLAYLIST_SETTING_ENUM, &entry);
#ifndef HAVE_DYNAMIC
            entry.enum_idx      = MENU_ENUM_LABEL_RESTART_RETROARCH;
            menu_displaylist_ctl(DISPLAYLIST_SETTING_ENUM, &entry);
#endif
            entry.enum_idx      = MENU_ENUM_LABEL_CONFIGURATIONS_LIST;
            menu_displaylist_ctl(DISPLAYLIST_SETTING_ENUM, &entry);

            entry.enum_idx      = MENU_ENUM_LABEL_HELP_LIST;
            menu_displaylist_ctl(DISPLAYLIST_SETTING_ENUM, &entry);
#if !defined(IOS)
            entry.enum_idx      = MENU_ENUM_LABEL_QUIT_RETROARCH;
            menu_displaylist_ctl(DISPLAYLIST_SETTING_ENUM, &entry);
#endif
#if defined(HAVE_LAKKA)
            entry.enum_idx      = MENU_ENUM_LABEL_REBOOT;
            menu_displaylist_ctl(DISPLAYLIST_SETTING_ENUM, &entry);

            entry.enum_idx      = MENU_ENUM_LABEL_SHUTDOWN;
            menu_displaylist_ctl(DISPLAYLIST_SETTING_ENUM, &entry);
#endif
            info->need_push    = true;
            ret = 0;
         }
         break;
   }
   return ret;
}

/* Returns the active tab id */
static size_t mui_list_get_selection(void *data)
{
   mui_handle_t *mui   = (mui_handle_t*)data;

   if (!mui)
      return 0;

   return mui->categories.selection_ptr;
}

/* The pointer or the mouse is pressed down. We use this callback to
highlight the entry that has been pressed */
static int mui_pointer_down(void *userdata,
      unsigned x, unsigned y,
      unsigned ptr, menu_file_list_cbs_t *cbs,
      menu_entry_t *entry, unsigned action)
{
   unsigned width, height;
   unsigned header_height;
   size_t entries_end         = menu_entries_get_size();
   mui_handle_t *mui          = (mui_handle_t*)userdata;

   if (!mui)
      return 0;

   header_height = menu_display_get_header_height();
   video_driver_get_size(&width, &height);

   if (y < header_height)
   {

   }
   else if (y > height - mui->tabs_height)
   {

   }
   else if (ptr <= (entries_end - 1))
   {
      size_t ii;
      file_list_t *list  = menu_entries_get_selection_buf_ptr(0);

      for (ii = 0; ii < entries_end; ii++)
      {
         mui_node_t *node = (mui_node_t*)
               menu_entries_get_userdata_at_offset(list, ii);

         if (y > (-mui->scroll_y + header_height + node->y)
          && y < (-mui->scroll_y + header_height + node->y + node->line_height)
         )
            menu_navigation_set_selection(ii);
      }


   }

   return 0;
}

/* The pointer or the left mouse button has been released.
If we clicked on the header, we perform a cancel action.
If we clicked on the tabs, we switch to a new list.
If we clicked on a menu entry, we call the entry action callback. */
static int mui_pointer_up(void *userdata,
      unsigned x, unsigned y,
      unsigned ptr, menu_file_list_cbs_t *cbs,
      menu_entry_t *entry, unsigned action)
{
   unsigned width, height;
   unsigned header_height, i;
   size_t entries_end         = menu_entries_get_size();
   mui_handle_t *mui          = (mui_handle_t*)userdata;

   if (!mui)
      return 0;

   header_height = menu_display_get_header_height();
   video_driver_get_size(&width, &height);

   if (y < header_height)
   {
      size_t selection = menu_navigation_get_selection();
      return menu_entry_action(entry, (unsigned)selection, MENU_ACTION_CANCEL);
   }
   else if (y > height - mui->tabs_height)
   {
      file_list_t *menu_stack    = menu_entries_get_menu_stack_ptr(0);
      file_list_t *selection_buf = menu_entries_get_selection_buf_ptr(0);

      for (i = 0; i <= MUI_SYSTEM_TAB_END; i++)
      {
         unsigned tab_width = width / (MUI_SYSTEM_TAB_END + 1);
         unsigned start = tab_width * i;

         if ((x >= start) && (x < (start + tab_width)))
         {
            mui->categories.selection_ptr = i;

            mui_preswitch_tabs(mui, action);

            if (cbs && cbs->action_content_list_switch)
               return cbs->action_content_list_switch(selection_buf, menu_stack,
                     "", "", 0);
         }
      }
   }
   else if (ptr <= (entries_end - 1))
   {
      size_t ii;
      file_list_t *list  = menu_entries_get_selection_buf_ptr(0);

      for (ii = 0; ii < entries_end; ii++)
      {
         mui_node_t *node = (mui_node_t*)
               menu_entries_get_userdata_at_offset(list, ii);

         if (y > (-mui->scroll_y + header_height + node->y)
          && y < (-mui->scroll_y + header_height + node->y + node->line_height)
         )
         {
            if (ptr == ii && cbs && cbs->action_select)
               return menu_entry_action(entry, (unsigned)ii, MENU_ACTION_SELECT);
         }
      }
   }

   return 0;
}

/* The menu system can insert menu entries on the fly. 
 * It is used in the shaders UI, the wifi UI, 
 * the netplay lobby, etc. 
 *
 * This function allocates the mui_node_t
 *for the new entry. */
static void mui_list_insert(void *userdata,
      file_list_t *list,
      const char *path,
      const char *fullpath,
      const char *label,
      size_t list_size,
      unsigned type)
{
   float scale_factor;
   int i                  = (int)list_size;
   mui_node_t *node       = NULL;
   mui_handle_t *mui      = (mui_handle_t*)userdata;

   if (!mui || !list)
      return;

   node = (mui_node_t*)menu_entries_get_userdata_at_offset(list, i);

   if (!node)
      node = (mui_node_t*)calloc(1, sizeof(mui_node_t));

   if (!node)
   {
      RARCH_ERR("GLUI node could not be allocated.\n");
      return;
   }

   scale_factor              = menu_display_get_dpi();

   node->line_height         = scale_factor / 3;
   node->y                   = 0;
   node->texture_switch_set  = false;
   node->texture_switch2_set = false;
   node->texture_switch      = 0;
   node->texture_switch2     = 0;
   node->switch_is_on        = false;
   node->do_draw_text        = false;

   switch (type)
   {
      case FILE_TYPE_PARENT_DIRECTORY:
         node->texture_switch2     = mui->textures.list[MUI_TEXTURE_PARENT_DIRECTORY];
         node->texture_switch2_set = true;
         break;
      case FILE_TYPE_PLAYLIST_COLLECTION:
         node->texture_switch2     = mui->textures.list[MUI_TEXTURE_PLAYLIST];
         node->texture_switch2_set = true;
         break;
      case FILE_TYPE_RDB:
         node->texture_switch2     = mui->textures.list[MUI_TEXTURE_DATABASE];
         node->texture_switch2_set = true;
         break;
      case 32: /* TODO: Need to find out what this is */
      case FILE_TYPE_RDB_ENTRY:
         node->texture_switch2     = mui->textures.list[MUI_TEXTURE_SETTINGS];
         node->texture_switch2_set = true;
         break;
      case FILE_TYPE_IN_CARCHIVE:
      case FILE_TYPE_PLAIN:
         node->texture_switch2     = mui->textures.list[MUI_TEXTURE_FILE];
         node->texture_switch2_set = true;
         break;
      case FILE_TYPE_MUSIC:
         node->texture_switch2     = mui->textures.list[MUI_TEXTURE_MUSIC];
         node->texture_switch2_set = true;
         break;
      case FILE_TYPE_MOVIE:
         node->texture_switch2     = mui->textures.list[MUI_TEXTURE_VIDEO];
         node->texture_switch2_set = true;
         break;
      case FILE_TYPE_DIRECTORY:
         node->texture_switch2     = mui->textures.list[MUI_TEXTURE_FOLDER];
         node->texture_switch2_set = true;
         break;
      default:
         if (
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_INFORMATION_LIST))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_NO_CORE_INFORMATION_AVAILABLE))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_NO_ITEMS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_NO_CORE_OPTIONS_AVAILABLE))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_NO_SETTINGS_FOUND))
               )
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_INFO];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_SCAN_THIS_DIRECTORY)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_SCAN];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_LOAD_CONTENT_HISTORY)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_HISTORY];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_HELP_LIST)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_HELP];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_RESTART_CONTENT)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_RESTART];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_RESUME_CONTENT)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_RESUME];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_CLOSE_CONTENT)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_CLOSE];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_CORE_OPTIONS)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_CORE_OPTIONS];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_CORE_CHEAT_OPTIONS)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_CORE_CHEAT_OPTIONS];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_CORE_INPUT_REMAPPING_OPTIONS)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_CONTROLS];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_SHADER_OPTIONS)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_SHADERS];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_CORE_LIST)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_CORES];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_RUN)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_RUN];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_ADD_TO_FAVORITES)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_ADD_TO_FAVORITES];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_PLAYLIST_ENTRY_RENAME)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_RENAME];
            node->texture_switch2_set = true;
         }
         else if (
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_ADD_TO_MIXER))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_ADD_TO_MIXER_AND_COLLECTION))
               )
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_ADD_TO_MIXER];
            node->texture_switch2_set = true;
         }
         else if (
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_START_CORE))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_RUN_MUSIC))
               )
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_START_CORE];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_LOAD_STATE))
               )
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_LOAD_STATE];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_SAVE_STATE))
               ||
               (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_SAVE_CURRENT_CONFIG_OVERRIDE_CORE)))
               ||
               (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_SAVE_CURRENT_CONFIG_OVERRIDE_GAME)))
               )
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_SAVE_STATE];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_UNDO_LOAD_STATE)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_UNDO_LOAD_STATE];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_UNDO_SAVE_STATE)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_UNDO_SAVE_STATE];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_STATE_SLOT)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_STATE_SLOT];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_TAKE_SCREENSHOT)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_TAKE_SCREENSHOT];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_CONFIGURATIONS_LIST)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_CONFIGURATIONS];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_LOAD_CONTENT_LIST)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_LOAD_CONTENT];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_DELETE_ENTRY)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_REMOVE];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_NETPLAY)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_NETPLAY];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_CONTENT_SETTINGS)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_QUICKMENU];
            node->texture_switch2_set = true;
         }
         else if (
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_ONLINE_UPDATER))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_UPDATE_CORE_INFO_FILES))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_UPDATE_AUTOCONFIG_PROFILES))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_UPDATE_ASSETS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_UPDATE_CHEATS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_UPDATE_DATABASES))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_UPDATE_OVERLAYS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_UPDATE_CG_SHADERS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_UPDATE_GLSL_SHADERS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_UPDATE_SLANG_SHADERS))
               )
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_UPDATER];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_SCAN_DIRECTORY)) || 
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_SCAN_FILE))
               )
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_ADD];
            node->texture_switch2_set = true;
         }
         else if (string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_QUIT_RETROARCH)))
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_QUIT];
            node->texture_switch2_set = true;
         }
         else if (
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_MENU_FILE_BROWSER_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_DRIVER_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_VIDEO_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_AUDIO_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_INPUT_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_INPUT_HOTKEY_BINDS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_CORE_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_CONFIGURATION_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_SAVING_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_LOGGING_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_FRAME_THROTTLE_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_RECORDING_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_ONSCREEN_DISPLAY_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_USER_INTERFACE_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_RETRO_ACHIEVEMENTS_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_WIFI_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_NETWORK_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_NETPLAY_LAN_SCAN_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_LAKKA_SERVICES))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_PLAYLIST_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_USER_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_DIRECTORY_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_PRIVACY_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_MENU_VIEWS_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_MENU_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_ONSCREEN_OVERLAY_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_ONSCREEN_NOTIFICATIONS_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_ACCOUNTS_LIST))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_REWIND_SETTINGS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_ACCOUNTS_RETRO_ACHIEVEMENTS))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_CORE_UPDATER_LIST))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_THUMBNAILS_UPDATER_LIST))
               ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_DOWNLOAD_CORE_CONTENT_DIRS))
               )
               {
                  node->texture_switch2     = mui->textures.list[MUI_TEXTURE_SETTINGS];
                  node->texture_switch2_set = true;
               }
         else if (
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_FAVORITES)) ||
               string_is_equal(label, msg_hash_to_str(MENU_ENUM_LABEL_DOWNLOADED_FILE_DETECT_CORE_LIST))
               )
         {
            node->texture_switch2     = mui->textures.list[MUI_TEXTURE_FOLDER];
            node->texture_switch2_set = true;
         }
         break;
   }

   file_list_set_userdata(list, i, node);
}

/* Clearing the current menu list */
static void mui_list_clear(file_list_t *list)
{
   size_t i;
   size_t size = list->size;

   for (i = 0; i < size; ++i)
   {
      menu_animation_ctx_subject_t subject;
      float *subjects[2];
      mui_node_t *node = (mui_node_t*)
         menu_entries_get_userdata_at_offset(list, i);

      if (!node)
         continue;

      subjects[0] = &node->line_height;
      subjects[1] = &node->y;

      subject.count = 2;
      subject.data  = subjects;

      menu_animation_ctl(MENU_ANIMATION_CTL_KILL_BY_SUBJECT, &subject);

      file_list_free_userdata(list, i);
   }
}

menu_ctx_driver_t menu_ctx_mui = {
   NULL,
   mui_get_message,
   generic_menu_iterate,
   mui_render,
   mui_frame,
   mui_init,
   mui_free,
   mui_context_reset,
   mui_context_destroy,
   mui_populate_entries,
   NULL,
   mui_navigation_clear,
   NULL,
   NULL,
   mui_navigation_set,
   mui_navigation_set_last,
   mui_navigation_alphabet,
   mui_navigation_alphabet,
   generic_menu_init_list,
   mui_list_insert,
   NULL,
   NULL,
   mui_list_clear,
   mui_list_cache,
   mui_list_push,
   mui_list_get_selection,
   mui_list_get_size,
   NULL,
   mui_list_set_selection,
   NULL,
   mui_load_image,
   "glui",
   mui_environ,
   NULL,
   NULL,
   NULL,
   NULL,
   NULL,
   mui_osk_ptr_at_pos,
   NULL,
   NULL,
   mui_pointer_down,
   mui_pointer_up,
};
