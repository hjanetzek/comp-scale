#include <e.h>
#include "e_mod_main.h"

#define OFFSET 32
#define PLACE_RUNS  10000
#define GROW_RUNS   1000
#define SHRINK_RUNS 2000
#define GROW 6.0

typedef struct _Item Item;
typedef struct _Slot Slot;

struct _Item
{
  Evas_Object *o, *o_win;
  E_Border *bd;
  E_Comp_Win *cw;
  E_Manager *man;
  double scale;
  int alpha;

  double x;
  double y;
  double w;
  double h;

  double mx;
  double my;

  /* border origin (is moved when scale on another desk) */
  double bd_x;
  double bd_y;

  /* current position to draw by compositor */
  int cur_x, cur_y, cur_w, cur_h;

  /* borders' desk distance to the current desk */
  int dx, dy;

  Eina_Bool overlaps;

  int in_slots;
  int slot_x;
  int slot_y;

  Item *next;
  Item *prev;
};

struct _Slot
{
  Eina_List *items;
  int x, y, w, h;
  Item *it;
  double min;
};

static Eina_Bool _scale_cb_mouse_down(void *data, int type, void *event);
static Eina_Bool _scale_cb_mouse_up(void *data, int type, void *event);
static Eina_Bool _scale_cb_mouse_move(void *data, int type, void *event);

static void _scale_win_cb_mouse_in(void *data, Evas *e, Evas_Object *obj, void *event_info);
static void _scale_win_cb_mouse_out(void *data, Evas *e, Evas_Object *obj, void *event_info);
static void _scale_win_cb_mouse_down(void *data, Evas *e, Evas_Object *obj, void *event_info);
static void _scale_win_cb_mouse_up(void *data, Evas *e, Evas_Object *obj, void *event_info);
static void _scale_win_cb_delorig(void *data, Evas *e, Evas_Object *obj, void *event_info);

static void _scale_win_new(Evas *e, E_Manager *man, E_Manager_Comp_Source *src, E_Desk *desk);
static void _scale_win_del(Item *it);

static void _scale_finish(void);
static void _scale_in(void);
static void _scale_out(int mode);
static void _scale_handler(void *data, const char *name, const char *info, int val, E_Object *obj, void *msgdata);


static Ecore_X_Window input_win = 0;
static E_Msg_Handler *msg_handler = NULL;

static Eina_List *items = NULL;
static Eina_List *popups = NULL;
static Eina_List *handlers = NULL;
static Ecore_Animator *scale_animator = NULL;
static Eina_Bool scale_state = EINA_FALSE;
static double start_time;
static E_Zone *zone = NULL;
static int max_x, max_y, min_x, min_y;
static int use_x, use_y, use_w, use_h;
static int max_width, max_height;
static int spacing;
static int step_count;
static Item *background = NULL;
static Item *selected_item = NULL;
static E_Desk *current_desk = NULL;
static int show_all_desks = EINA_FALSE;
static int send_to_desk = EINA_FALSE;
static int scale_layout;
static int init_method = 0;

static void
_scale_place_windows(double scale)
{
   Eina_List *l;
   Item *it;

   EINA_LIST_FOREACH(items, l, it)
     {
	it->cur_w = it->bd->w * scale + it->w * (1.0 - scale);
	it->cur_h = it->bd->h * scale + it->h * (1.0 - scale);
	it->cur_x = it->bd_x  * scale + it->x * (1.0 - scale);
	it->cur_y = it->bd_y  * scale + it->y * (1.0 - scale);

	evas_object_move(it->o, it->cur_x, it->cur_y);
	evas_object_resize(it->o, it->cur_w, it->cur_h);
     }
}

static Eina_Bool
_scale_redraw(void *data)
{
   Eina_List *l;
   Item *it;
   double scale, a, in, duration;

   if (show_all_desks)
     duration = scale_conf->desks_duration;
   else
     duration = scale_conf->scale_duration;

   if (scale_state)
     scale = (ecore_time_get() - start_time) / duration;
   else
     scale = 1.0 - (ecore_time_get() - start_time) / duration;

   if (scale > 1.0) scale = 1.0;
   if (scale < 0.0) scale = 0.0;

   in = log(10) * scale;
   in = 1.0 / exp(in*in);

   _scale_place_windows(in);

   if (scale_conf->fade_windows)
     {
	EINA_LIST_FOREACH(items, l, it)
	  {
	     a = 255.0;

	     if ((it->bd->desk != current_desk) && (selected_item != it))
	       {
		  double ax = it->cur_x - it->x;
		  double ay = it->cur_y - it->y;
		  double bx = it->bd_x  - it->x;
		  double by = it->bd_y  - it->y;

		  a = (1.0 - sqrt(ax*ax + ay*ay) / sqrt(bx*bx + by*by)) * 255;
	       }

	     it->alpha = a;
	     evas_object_color_set(it->o_win, a, a, a, a);
	  }
     }

   if (scale_conf->fade_popups)
     {
	a = 255.0 * in;

	EINA_LIST_FOREACH(popups, l, it)
	  evas_object_color_set(it->o_win, a, a, a, a);
     }

   if (scale_conf->fade_desktop && background)
     {
	a = 255.0 * (0.5 + in/2.0);

	evas_object_color_set(background->o_win, a, a, a, 255);
     }

   e_manager_comp_evas_update(e_manager_current_get());

   if (scale < 1.0 && scale > 0.0)
     return 1;

   if (scale == 0.0)
     _scale_finish();
   else
     _scale_place_windows(0.0);

   scale_animator = NULL;
   return 0;
}

static void
_scale_in()
{
   start_time = ecore_time_get();
   scale_state = EINA_TRUE;

   _scale_place_windows(1.0);

   if (!scale_animator)
     scale_animator = ecore_animator_add(_scale_redraw, NULL);
}

static void
_scale_out(int mode)
{
   double duration, now = ecore_time_get();
   Item *ot, *it = selected_item;
   Eina_List *l;
   if (mode == 0)
     {
	selected_item = NULL;
     }
   else if (it && (mode == 1))
     {
	/* goto selected windows desk */

	current_desk = it->bd->desk;

	EINA_LIST_FOREACH(items, l, ot)
	  {
	     if (ot->bd->desk == it->bd->desk)
	       {
		  ot->bd_x = ot->bd->x;
		  ot->bd_y = ot->bd->y;
	       }
	     else
	       {
		  if (ot->dx > it->dx)
		    ot->bd_x = ot->bd->x + zone->w;
		  else if (ot->dx < it->dx)
		    ot->bd_x = ot->bd->x - zone->w;

		  if (ot->dy > it->dy)
		    ot->bd_y = ot->bd->y + zone->h;
		  else if (ot->dy < it->dy)
		    ot->bd_y = ot->bd->y - zone->h;
	       }
	  }
     }
   else if (it && (mode == 2))
     {
	send_to_desk = EINA_TRUE;
	it->bd_x = it->bd->x;
	it->bd_y = it->bd->y;
     }

   if (show_all_desks)
     duration = scale_conf->desks_duration;
   else
     duration = scale_conf->scale_duration;

   if (now - start_time < duration)
     start_time = now - (duration - (now - start_time));
   else
     start_time = now;

   if (!scale_animator)
     scale_animator = ecore_animator_add(_scale_redraw, NULL);

   if (selected_item)
     {
	it = selected_item;

	evas_object_raise(it->o);
	e_border_raise(it->bd);

	if ((init_method == GO_KEY) && (e_config->focus_policy != E_FOCUS_CLICK))
	  {
	     int slide = e_config->pointer_slide;
	     int focus = e_config->focus_policy;

	     e_config->pointer_slide = 1;
	     e_config->focus_policy = E_FOCUS_MOUSE;

	     e_border_focus_set_with_pointer(it->bd);

	     e_config->pointer_slide = slide;
	     e_config->focus_policy = focus;
	  }
	else
	  e_border_focus_set(it->bd, 1, 1);

	edje_object_signal_emit(it->o, "hide", "e");
     }

   scale_state = EINA_FALSE;
}

static void
_scale_finish()
{
   Ecore_Event_Handler *handler;
   Item *it;
   E_Desk *desk;

   e_grabinput_release(input_win, input_win);
   ecore_x_window_free(input_win);
   input_win = 0;

   desk = e_desk_current_get(zone);

   if (selected_item &&  selected_item->bd->desk != desk)
     {
	if (send_to_desk)
	  {
	     e_border_desk_set(selected_item->bd, desk);
	  }
	else
	  {
	     int tmp = e_config->desk_flip_animate_mode;
	     desk = selected_item->bd->desk;

	     e_config->desk_flip_animate_mode = 0;
	     e_desk_show(desk);
	     e_config->desk_flip_animate_mode = tmp;
	     current_desk = desk;
	  }
     }

   /* _scale_place_windows(1.0); */

   EINA_LIST_FREE(items, it)
     _scale_win_del(it);

   EINA_LIST_FREE(popups, it)
     _scale_win_del(it);

   if (background)
     _scale_win_del(background);

   EINA_LIST_FREE(handlers, handler)
     ecore_event_handler_del(handler);

   e_msg_handler_del(msg_handler);
   msg_handler = NULL;
   zone = NULL;
   selected_item = NULL;
   current_desk = NULL;
   background = NULL;
   send_to_desk = EINA_FALSE;
   show_all_desks = EINA_FALSE;

   e_manager_comp_evas_update(e_manager_current_get());
}

static void
_scale_win_cb_mouse_in(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
   Item *it = data;

   if (!scale_state)
     return;

   edje_object_signal_emit(it->o, "mouse,in", "e");
}

static void
_scale_win_cb_mouse_out(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
   Item *it = data;

   if (!scale_state)
     return;

   edje_object_signal_emit(it->o, "mouse,out", "e");
}

static void
_scale_win_cb_mouse_down(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
   Item *it = data;
   Evas_Event_Mouse_Down *ev = event_info;

   if (!scale_state)
     return;

   if (it->bd->desk == e_desk_current_get(it->bd->zone))
     {
	selected_item = it;
	_scale_out(1);
     }
   else if (ev->button == 1)
     {
	selected_item = it;
	_scale_out(1);
     }
   else if (ev->button == 3)
     {
	selected_item = it;
	_scale_out(2);
     }
}

static void
_scale_win_cb_mouse_up(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
   /* Item *it = data; */
}

static void
_scale_win_cb_delorig(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
   Item *it = data;

   if (it->bd)
     {
	_scale_out(0);
	items = eina_list_remove(items, it);
     }
   else
     {
	popups = eina_list_remove(popups, it);
     }

   _scale_win_del(it);
}

static void
_scale_win_del(Item *it)
{
   evas_object_event_callback_del(it->o_win, EVAS_CALLBACK_DEL,
				  _scale_win_cb_delorig);

   if (it->bd)
     {
	evas_object_event_callback_del(it->o, EVAS_CALLBACK_MOUSE_IN,
				       _scale_win_cb_mouse_in);

	evas_object_event_callback_del(it->o, EVAS_CALLBACK_MOUSE_OUT,
				       _scale_win_cb_mouse_out);

	evas_object_event_callback_del(it->o, EVAS_CALLBACK_MOUSE_DOWN,
				       _scale_win_cb_mouse_down);

	evas_object_event_callback_del(it->o, EVAS_CALLBACK_MOUSE_UP,
				       _scale_win_cb_mouse_up);

	e_manager_comp_src_hidden_set(it->man,
				      (E_Manager_Comp_Source *)it->cw,
				      EINA_FALSE);

	if (it->bd->desk != current_desk)
	  {
	     e_border_hide(it->bd, 2);
	     evas_object_hide(it->cw->shobj);
	  }

	evas_object_del(it->o_win);
	evas_object_del(it->o);

	e_object_unref(E_OBJECT(it->bd));
     }
   else
     {
	evas_object_color_set(it->o_win, 255, 255, 255, 255);
     }

   E_FREE(it);
}

static void
_scale_win_new(Evas *e, E_Manager *man, E_Manager_Comp_Source *src, E_Desk *desk)
{
   Item *it;

   if (!e_manager_comp_src_image_get(man, src)) return;

   E_Comp_Win *cw = (void*)src;

   if (!cw->bd)
     {
	if (cw->win == zone->container->bg_win)
	  {
	     if (scale_conf->fade_desktop)
	       {
		  it = E_NEW(Item, 1);
		  it->man = man;
		  it->o_win = e_manager_comp_src_shadow_get(man, src);
		  evas_object_event_callback_add(it->o_win, EVAS_CALLBACK_DEL,
		  				 _scale_win_cb_delorig, it);
		  background = it;
	       }
	  }
	else if (scale_conf->fade_popups)
	  {
	     it = E_NEW(Item, 1);
	     it->man = man;
	     it->o_win = e_manager_comp_src_shadow_get(man, src);
	     evas_object_event_callback_add(it->o_win, EVAS_CALLBACK_DEL,
	     				    _scale_win_cb_delorig, it);
	     popups = eina_list_append(popups, it);
	  }
   	return;
     }

   if (!cw->bd) return;

   if (cw->bd->zone != desk->zone)
     return;

   if ((!show_all_desks) && (cw->bd->desk != desk))
     return;

   if (cw->bd->iconic)
     return;

   it = E_NEW(Item, 1);
   it->scale = 1.0;

   e_object_ref(E_OBJECT(cw->bd));
   it->bd = cw->bd;
   it->man = man;
   it->cw = cw;

   e_manager_comp_src_hidden_set(man, src, EINA_TRUE);
   it->o_win = e_manager_comp_src_image_mirror_add(man, src);
   evas_object_show(it->o_win);
   it->o = edje_object_add(e);
   if (!e_theme_edje_object_set(it->o, "base/theme/modules/scale",
                                "modules/scale/win"))
     edje_object_file_set(it->o, scale_conf->theme_path, "modules/scale/win");

   evas_object_stack_below(it->o, it->o_win);
   evas_object_show(it->o);
   edje_object_part_swallow(it->o, "e.swallow.win", it->o_win);

   evas_object_event_callback_add(it->o_win, EVAS_CALLBACK_DEL,
				  _scale_win_cb_delorig, it);

   evas_object_event_callback_add(it->o, EVAS_CALLBACK_MOUSE_IN,
				  _scale_win_cb_mouse_in, it);

   evas_object_event_callback_add(it->o, EVAS_CALLBACK_MOUSE_OUT,
				  _scale_win_cb_mouse_out, it);

   evas_object_event_callback_add(it->o, EVAS_CALLBACK_MOUSE_DOWN,
				  _scale_win_cb_mouse_down, it);

   evas_object_event_callback_add(it->o, EVAS_CALLBACK_MOUSE_UP,
				  _scale_win_cb_mouse_up, it);

   /* evas_object_event_callback_add(it->o, EVAS_CALLBACK_MOUSE_MOVE,
    * 				  _scale_win_cb_mouse_move, it); */

   it->dx = cw->bd->desk->x - desk->x;
   it->dy = cw->bd->desk->y - desk->y;

   it->x = cw->bd->x + it->dx * desk->zone->w;
   it->y = cw->bd->y + it->dy * desk->zone->h;
   it->w = cw->bd->w;
   it->h = cw->bd->h;

   it->bd_x = it->x;
   it->bd_y = it->y;

   it->cur_x = it->x;
   it->cur_y = it->y;
   it->cur_w = it->w;
   it->cur_h = it->h;

   if (it->bd->desk != desk)
     e_border_show(it->bd);

   edje_object_signal_emit(it->o, "show", "e");

   items = eina_list_append(items, it);
}

static int
_scale_grow()
{
   Item *it, *ot;
   Eina_List *l, *ll;

   int cont = 0;
   int overlap;
   double grow_l, grow_r, grow_d, grow_u;

   double mean = 0;
   EINA_LIST_FOREACH(items, l, it)
     {
	it->scale = it->w / (double)it->bd->w;
	mean += it->scale;
     }

   mean /= (double) eina_list_count(items);

   EINA_LIST_FOREACH(items, l, it)
     {
	overlap = 0;

	if (it->scale > mean)
	  continue;

	if (it->w >= it->bd->w)
	  continue;
	if (it->h >= it->bd->h)
	  continue;

	if (it->bd->w > it->bd->h)
	  {
	     grow_l = grow_r = GROW;
	     grow_u = grow_d = GROW * (double)it->bd->h/(double)it->bd->w;
	  }
	else
	  {
	     grow_l = grow_r = GROW * (double)it->bd->w/(double)it->bd->h;
	     grow_u = grow_d = GROW;
	  }

	if (it->x - grow_l < min_x)
	  grow_l = 0;
	if (it->y - grow_u < min_y)
	  grow_u = 0;
	if (it->x + it->w + grow_r > max_x)
	  grow_r = 0;
	if (it->y + it->h + grow_d > max_y)
	  grow_d = 0;

	if ((grow_l + grow_r) == 0)
	  continue;
	if ((grow_u + grow_d) == 0)
	  continue;

	EINA_LIST_FOREACH(items, ll, ot)
	  {
	     if (it == ot)
	       continue;
	     if (grow_l && E_INTERSECTS(it->x - grow_l - spacing ,it->y,
					it->w + spacing*2, it->h + spacing*2,
					ot->x, ot->y, ot->w, ot->h))
	       grow_l = 0;

	     if (grow_r && E_INTERSECTS(it->x - spacing, it->y - spacing,
					it->w + grow_r + spacing*2, it->h + spacing*2,
					ot->x, ot->y, ot->w, ot->h))
	       grow_r = 0;

	     if ((grow_l == 0) && (grow_r == 0) && (overlap = 1))
	       break;

	     if (grow_u && E_INTERSECTS(it->x - spacing, it->y - spacing - grow_u,
					it->w + spacing*2, it->h + spacing*2,
					ot->x, ot->y, ot->w, ot->h))
	       grow_u = 0;

	     if (grow_d && E_INTERSECTS(it->x - spacing, it->y - spacing,
					it->w + spacing*2, it->h + grow_d + spacing*2,
					ot->x, ot->y, ot->w, ot->h))
	       grow_d = 0;

	     if ((grow_u == 0) && (grow_d == 0) && (overlap = 1))
	       break;
	  }

	if (overlap)
	  continue;

	if (it->bd->w > it->bd->h)
	  {
	     if ((grow_u > 0) && (grow_d > 0))
	       it->w += grow_l + grow_r;
	     else
	       it->w += MAX(grow_l, grow_r);

	     it->h = it->w * (double)it->bd->h / (double)it->bd->w;
	  }
	else
	  {
	     if ((grow_r > 0) && (grow_l > 0))
	       it->h += grow_u + grow_d;
	     else
	       it->h += MAX(grow_u, grow_d);

	     it->w = it->h * (double)it->bd->w / (double)it->bd->h;
	  }
	it->x -= grow_l;
	it->y -= grow_u;

	cont++;
     }

   return cont;
}

static void
_scale_displace(Item *it, Item *ot, int disp)
{
   /* cycle items with same center around to get even	distribution.
      dont try to understand this but these initial conditions
      are important.. you can have up to 9 maximized windows.
      if you can figure out more please tell me :) */

   if (disp % 8 == 0)
     {
	// 1.
	it->y -= 1;
	it->x -= 1;
	// 2.
	ot->x += 1;
	ot->y -= 1;
     }
   else if (disp % 8 == 1)
     {
	// 3.
	ot->y += 1;
     }
   else if (disp % 8 == 2)
     {
	// 4.
	ot->x -= 2;
	ot->y += 2;
     }
   else if (disp % 8 == 3)
     {
	// 5.
	ot->x += 2;
	ot->y += 2;
     }
   else if (disp % 8 == 4)
     {
	// 6.
	ot->x += 1;
	ot->y += 1;
     }
   else if (disp % 8 == 5)
     {
	// 7.
	ot->x -= 1;
	ot->y += 1;
     }
   else if (disp % 8 == 6)
     {
	// 8.
	ot->x -= 1;
	ot->y -= 1;
     }
   else if (disp % 8 == 7)
     {
	ot->x += 1;
	ot->y -= 1;
     }
}

static int
_scale_place(int offset)
{
   Item *it, *ot;
   Eina_List *l, *ll;
   int overlap = 0;
   int outside = 0;

   EINA_LIST_FOREACH(items, l, it)
     {
	it->mx = it->x;
	it->my = it->y;
     }

   int disp = 0;

   EINA_LIST_FOREACH(items, l, it)
     {
	EINA_LIST_FOREACH(items, ll, ot)
	  {
	     int w = it->w;
	     int h = it->h;

	     if (it == ot)
	       continue;

	     if (!E_INTERSECTS(it->x - offset, it->y - offset,
			       it->w + offset*2, it->h + offset*2,
			       ot->x, ot->y, ot->w, ot->h))
	       continue;

	     overlap += 1;

	     it->overlaps++;

	     if (it->x < ot->x)
	       w += it->x - ot->x;
	     if (w < 0) w = 0;

	     if (it->x + it->w > ot->x + ot->w)
	       w = ot->x + ot->w - it->x;

	     if (it->y < ot->y)
	       h += it->y - ot->y;
	     if (h < 0) h = 0;

	     if (it->y + it->h > ot->y + ot->h)
	       h = ot->y + ot->h - it->y;

	     double dist_y = (it->y + it->h/2) - (ot->y + ot->h/2);
	     double dist_x = (it->x + it->w/2) - (ot->x + ot->w/2);

	     if (dist_x == 0 && dist_y == 0)
	       {
		  _scale_displace(it, ot, disp);
		  disp++;
	       }
	     else if (w > h)
	       {
		  if (dist_y)
		    {
		       dist_y = (dist_y > 0 ? 2 : -2);
		       it->my += dist_y;
		    }
		  if (dist_x)
		    {
		       dist_x = (dist_x > 0 ? 1 : -1);
		       it->mx += dist_x;
		    }
	       }
	     else //if (w < h)
	       {
		  if (dist_y)
		    {
		       dist_y = (dist_y > 0 ? 1 : -1);
		       it->my += dist_y;
		    }
		  if (dist_x)
		    {
		       dist_x = (dist_x > 0 ? 2 : -2);
		       it->mx += dist_x;
		    }
	       }
	  }
     }

   EINA_LIST_FOREACH(items, l, it)
     {
   	it->x = it->mx;
   	it->y = it->my;

   	if (it->x < min_x)
	  {
	     outside = 1;
	     it->x = min_x;
	  }

   	if (it->y < min_y)
	  {
	     outside = 1;
	     it->y = min_y;
	  }

   	if (it->x + it->w > max_x)
	  {
	     outside = 1;
	     it->x = max_x - it->w;
	  }

   	if (it->y + it->h > max_y)
	  {
	     outside = 1;
	     it->y = max_y - it->h;
	  }
     }

   if (!(overlap || outside))
     return 0;

   if (outside && (step_count++ > 50))
     {
	double zone_diag = sqrt(zone->w * zone->h);
	double sw, sh;
	step_count = 0;
	EINA_LIST_FOREACH(items, l, it)
	  {
	     if (!it->overlaps)
	       continue;

	     if (it->scale <= 0.005)
	       continue;

	     it->scale -= it->scale *
	       (0.001 + (sqrt(it->bd->w * it->bd->h) / zone_diag) / 50.0);

	     sw = (double)it->bd->w * it->scale;
	     sh = (double)it->bd->h * it->scale;
	     it->x += (it->w - sw)/2.0;
	     it->y += (it->h - sh)/2.0;
	     it->w = sw;
	     it->h = sh;

	     it->overlaps = 0;
	  }
	return 1;
     }

   return overlap || outside;
}

static int
_scale_shrink()
{
   Eina_List *l, *ll;
   Item *it, *ot;
   int shrunk = 0;
   double move_x;
   double move_y;

   EINA_LIST_REVERSE_FOREACH(items, l, it)
     {
	if (show_all_desks)
	  {
	     move_x = ((it->x + it->w/2.0) - zone->w/2.0) / 5.0;
	     move_y = ((it->y + it->h/2.0) - zone->h/2.0) / 5.0;
	  }
	else
	  {
	     move_x = ((it->x + it->w/2.0) -
		       (double)(it->bd->x + it->bd->w/2.0)) / 10.0;
	     move_y = ((it->y + it->h/2.0) -
		       (double)(it->bd->y + it->bd->h/2.0)) / 10.0;
	  }

	if (!(move_y || move_x))
	  continue;

	EINA_LIST_FOREACH(items, ll, ot)
	  {
	     if (it == ot) continue;

	     while(move_x)
	       {
		  if (E_INTERSECTS(it->x - move_x, it->y, it->w, it->h,
				   ot->x - spacing, ot->y - spacing,
				   ot->w + spacing*2, ot->h + spacing*2))
		    move_x = move_x / 2.0;
		  else break;
	       }

	     while(move_y)
	       {
		  if (E_INTERSECTS(it->x, it->y - move_y, it->w, it->h,
				   ot->x - spacing, ot->y - spacing,
				   ot->w + spacing*2, ot->h + spacing*2))
		    move_y = move_y / 2.0;
		  else break;
	       }

	     if (!(move_y || move_x)) break;
	  }

	it->x -= move_x;
	it->y -= move_y;

	if (move_y > 1 || move_x > 1)
	  shrunk++;
     }

   return shrunk;
}


/* TODO add slot item an calc distance only once */
static Slot *cur_slot = NULL;

static double
_slot_dist(const Item *it, const Slot *slot)
{
   double dx = it->x - slot->x;
   double dy = it->y - slot->y;
   double dw = (it->x + it->w) - (slot->x + slot->w);
   double dh = (it->y + it->h) - (slot->y + slot->h);

   return (sqrt(dx*dx + dy*dy + dw*dw + dh*dh));
}

static int
_cb_sort_nearest(const void *d1, const void *d2)
{
   const Item *it1 = d1;
   const Item *it2 = d2;

   return (_slot_dist(it1, cur_slot) < _slot_dist(it2, cur_slot)) ? -1 : 1;
}

static int
_scale_place_slotted()
{
   Eina_List *l, *ll, *slots = NULL;
   Slot *slot, *slot2;
   Item *it;
   int rows, cols, cnt, x, y, w, h, start;
   int fast = 0;
   int cont = 0, i = 0;
   double min_x, max_x, min_y, max_y;

   cnt = eina_list_count(items);

   rows = sqrt(cnt);
   cols = cnt/rows;

   if (cols*rows < cnt)
     cols += 1;

   if (cnt <= 2)
     {
   	cols = 2;
   	rows = 1;
     }
   else if (cnt <= 4)
     {
   	cols = 2;
   	rows = 2;
     }
   else if (cnt <= 6)
     {
   	cols = 3;
   	rows = 2;
     }
   else if (cnt <= 8)
     {
   	cols = 3;
   	rows = 3;
     }

   DBG("%d rows, %d cols -- cnt %d\n", rows, cols, cnt);

   max_x = max_y = 0;
   min_x = min_y = 100000;

   EINA_LIST_FOREACH(items, l, it)
     {
   	if (it->x < min_x) min_x = it->x;
   	if (it->y < min_y) min_y = it->y;
   	if (it->x + it->w > max_x) max_x = it->x + it->w;
   	if (it->y + it->h > max_y) max_y = it->y + it->h;
     }

   w = (max_x - min_x) / cols;
   h = (max_y - min_y) / rows;
   l = items;

   for (y = 0; y < rows; y++)
     {
	for (x = 0; x < cols; x++)
	  {
	     if (fast && !l) break;

	     slot = E_NEW(Slot, 1);
	     slot->x = min_x + x * w;
	     slot->y = min_y + y * h;
	     slot->w = w;
	     slot->h = h;

	     if (fast)
	       {
		  slot->it = eina_list_data_get(l);
		  if (l) l = l->next;
	       }
	     else
	       {
		  cur_slot = slot;
		  slot->items = eina_list_clone(items);
		  slot->items = eina_list_sort(slot->items, cnt,
					       _cb_sort_nearest);
		  slot->it = eina_list_data_get(slot->items);
		  slot->items = eina_list_remove_list(slot->items,
						      slot->items);
		  slot->min = _slot_dist(slot->it, slot);
	       }

	     slots = eina_list_append(slots, slot);

	     DBG("add slot: %dx%d,   \t%f -> %d:%d\n",
		 slot->x, slot->y, slot->min,
		 (int)(slot->it->x), (int)(slot->it->y));
	  }
     }

   if (!fast)
     {
	cont = 1;
	EINA_LIST_FOREACH(items, l, it)
	  it->in_slots = cols * rows;
     }

   for (i = 0; (i < PLACE_RUNS) && cont; i++)
     {
	cont = 0;
	EINA_LIST_FOREACH(slots, l, slot)
	  {
	     EINA_LIST_FOREACH(slots, ll, slot2)
	       {
		  if (slot == slot2)
		    continue;

		  if (slot->it != slot2->it)
		    continue;

		  Item *it1 = eina_list_data_get(slot->items);
		  Item *it2 = eina_list_data_get(slot2->items);
		  if (!it1 || !it2)
		    continue;

		  double d1 = _slot_dist(it1, slot);
		  double d2 = _slot_dist(it2, slot2);

		  cont = 1;

		  DBG("%dx%d - compare:\n\ts1: %dx%d (%dx%d:%f),"
		      "\n\ts2 %dx%d (%dx%d:%f)\n",
		      (int)slot->it->x, (int)slot->it->y,
		      slot->x, slot->y, (int)it1->x, (int)it1->y, d1,
		      slot2->x, slot2->y, (int)it2->x, (int)it2->y, d2);

		  if ((slot->it->in_slots > 1) &&
		      (slot->min + d1 >= slot2->min + d2))
		    {
		       slot->it->in_slots--;
		       slot->it = it1;
		       slot->min = d1;
		       slot->items = eina_list_remove_list(slot->items,
							   slot->items);
		       break;
		    }
	       }
	  }
     }

   cont = 1;

   for (i = 0; (i < PLACE_RUNS) && cont; i++)
     {
        cont = 0;
	EINA_LIST_FOREACH(slots, l, slot)
	  {
	     EINA_LIST_FOREACH(l->next, ll, slot2)
	       {
		  double d1, d2;

		  d1 = _slot_dist(slot->it, slot) + _slot_dist(slot2->it, slot2);
		  d2 = _slot_dist(slot->it, slot2) + _slot_dist(slot2->it, slot);
		  if (d1 > d2)
		    {
		       it = slot->it;
		       slot->it = slot2->it;
		       slot2->it = it;
		       cont = 1;
		    }
	       }
	  }
     }

   EINA_LIST_FOREACH(slots, l, slot)
     {
	if (!slot->it)
	  continue;

	EINA_LIST_FOREACH(slots, ll, slot2)
	  {
	     if (slot == slot2)
	       continue;

	     if (slot->it != slot2->it)
	       continue;

	     if (_slot_dist(slot->it, slot) > _slot_dist(slot2->it, slot2))
	       slot->it = NULL;
	     else
	       slot2->it = NULL;
	  }
     }

   h = (zone->h - spacing) / rows;
   w = (zone->w - spacing) / cols;

   for (y = 0, l = slots; l && (y < rows); y++)
     {
	ll = l;
	for (x = 0, cnt = 0; l &&  x < cols; x++, l = eina_list_next(l))
	  {
	     slot = eina_list_data_get(l);
	     if (slot->it) cnt++;
	  }

	if (cnt < cols)
	  start = (w * (cols - cnt))/2.0;
	else
	  start = spacing/2.0;

	for (x = 0; ll &&  x < cnt; ll = eina_list_next(ll))
	  {
	     slot = eina_list_data_get(ll);
	     if (!slot->it) continue;

	     slot->x = start + x * w;
	     slot->y = y * h;
	     slot->w = w;
	     slot->h = h;
	     x++;
	  }
     }
   Item *prev = NULL;
   Item *first = NULL;

   EINA_LIST_FOREACH(slots, l, slot)
     {
	if (slot->it)
	  {
	     it = slot->it;

	     it->prev = prev;
	     prev = it;

	     if (it->prev)
	       it->prev->next = it;

	     if (!first)
	       first = it;

	     it->slot_x = slot->x;
	     it->slot_y = slot->y;

	     if (it->w > slot->w - spacing)
	       {
		  it->w = slot->w - spacing;
		  it->h = it->w * (double)it->bd->h / (double)it->bd->w;
	       }
	     if (it->h > slot->h - spacing)
	       {
		  it->h = slot->h - spacing;
		  it->w = it->h * (double)it->bd->w / (double)it->bd->h;
	       }
	     it->x = slot->x + (slot->w - it->w)/2.0;
	     it->y = slot->y + (slot->h - it->h)/2.0;

	     DBG("place: %d:%d %dx%d -> %d:%d %dx%d\n",
		 (int)it->bd_x, (int)it->bd_y, (int)it->bd->w, (int)it->bd->h,
		 (int)it->x, (int)it->y, (int)it->w, (int)it->h);
	  }

	EINA_LIST_FREE(slot->items, it);
	E_FREE(slot);
     }

   first->prev = prev;
   prev->next = first;

   return 1;
}

static int
_cb_sort_center(const void *d1, const void *d2)
{
   const Item *it1 = d1;
   const Item *it2 = d2;

   double dx1 = ((it1->x + it1->w/2.0) - (double)(max_width/2));
   double dy1 = ((it1->y + it1->h/2.0) - (double)(max_height/2));

   double dx2 = ((it2->x + it2->w/2.0) - (double)(max_width/2));
   double dy2 = ((it2->y + it2->h/2.0) - (double)(max_height/2));

   return (sqrt(dx1*dx1 + dy1*dy1) > sqrt(dx2*dx2 + dy2*dy2)) ? -1 : 1;
}

static void
_scale_place_natural()
{
   Eina_List *l;
   int offset, i = 0;
   Item *it;

   max_width  = zone->w;
   max_height = zone->h;

   if (show_all_desks)
     {
	max_width  = zone->h * zone->desk_x_count;
	max_height = zone->w * zone->desk_y_count;
     }

   items = eina_list_sort(items, eina_list_count(items), _cb_sort_center);

   offset = spacing;

   if (scale_conf->grow && (spacing < OFFSET))
     offset = OFFSET;
   if (scale_conf->tight && (spacing < OFFSET))
     offset = OFFSET;

   step_count = 0;

   while ((i++ < PLACE_RUNS) &&
	  (_scale_place(offset) ||
	   (min_x < use_x) ||
	   (min_y < use_y) ||
	   (max_x > use_w) ||
	   (max_y > use_h)))
     {
	/* shrink region to visible region */
	if (min_x < use_x) min_x += 2;
	if (min_y < use_y) min_y += 2;
	if (min_x > use_x) min_x = use_x;
	if (min_y > use_y) min_y = use_y;

	if (max_x > use_w) max_x -= 2;
	if (max_y > use_h) max_y -= 2;
	if (max_x < use_w) max_x = use_w;
	if (max_y < use_h) max_y = use_h;

	if (!show_all_desks)
	  continue;

	/* move other desks windows into visible region */
	EINA_LIST_FOREACH(items, l, it)
	  {
	     if ((min_x < use_x) && (it->dx < 0) && it->x < 0) it->x += 4.0;
	     if ((min_y < use_y) && (it->dy < 0) && it->y < 0) it->y += 4.0;

	     if ((max_x > use_w) && (it->dx > 0) && it->x > zone->w) it->x -= 4.0;
	     if ((max_y > use_h) && (it->dy > 0) && it->y > zone->h) it->y -= 4.0;
	  }
     }
}


#if 0
static Item *
_scale_item_select(int direction)
{
   double min;
   Item *last, *min;
   Eina_List *l;

   if (direction == DIR_RIGHT)
     {
	EINA_LIST_FOREACH(items, l, it2)
	  {

	  }
     }
   else if (direction == DIR_LEFT)
     {
	EINA_LIST_FOREACH(items, l, it2)
	  {

	  }
     }
   else if (direction == DIR_UP)
     {
	EINA_LIST_FOREACH(items, l, it2)
	  {

	  }
     }
   else if (direction == DIR_DOWN)
     {
	EINA_LIST_FOREACH(items, l, it2)
	  {

	  }
     }
}
#endif

static void
_scale_switch(const char *params)
{
   Item *it, *sel;

   sel = selected_item;

   if (params[0] == 0)
     {
	_scale_out(1);
	return;
     }

   if ((!sel->next) || (!sel->prev))
     return;
   
   if (!strcmp(params, "_next"))
     {
	it = sel->next;
     }
   else if (!strcmp(params, "_prev"))
     {
	it = sel->prev;
     }
   else if (!strcmp(params, "_left"))
     {
	it = sel->prev;
	
	if (it->slot_y != sel->slot_y)
	  {
	     it = sel;
	     
	     while(sel->slot_y == it->next->slot_y)
	       {
		  it = it->next;
		  if (it == sel) break;
	       }
	  }
     }
   else if (!strcmp(params, "_right"))
     {
	it = sel->next;
	
	if (it->slot_y != sel->slot_y)
	  {
	     it = sel;
	     while(sel->slot_y == it->prev->slot_y)
	       {
		  it = it->prev;
		  if (it == sel) break;
	       }
	  }
     }
   else if (!strcmp(params, "_up"))
     {
	it = sel;
	
	while((sel->slot_y == it->slot_y) ||
	      (sel->slot_x < it->slot_x))
	  {
	     it = it->prev;
	     if (it == sel) break;
	  }
     }
   else if (!strcmp(params, "_down"))
     {
	it = sel;
	
	while((sel->slot_y == it->slot_y) ||
	      (sel->slot_x > it->slot_x))
	  {
	     it = it->next;
	     if (it == sel) break;
	  }
     }

   if (it == sel)
     return;

   edje_object_signal_emit(sel->o, "mouse,out", "e");
   edje_object_signal_emit(it->o, "mouse,in", "e");
   e_border_focus_set(it->bd, 1, 1);
   selected_item = it;
}

static Eina_Bool
_scale_cb_key_down(void *data, int type, void *event)
{
   Ecore_Event_Key *ev = event;

   if (ev->window != input_win)
     return ECORE_CALLBACK_PASS_ON;

   if (!strcmp(ev->key, "Up"))
     _scale_switch("_up");
   else if (!strcmp(ev->key, "Down"))
     _scale_switch("_down");
   else if (!strcmp(ev->key, "Left"))
     _scale_switch("_left");
   else if (!strcmp(ev->key, "Right"))
     _scale_switch("_right");
   else if (!strcmp(ev->key, "h"))
     _scale_switch("_left");
   else if (!strcmp(ev->key, "j"))
     _scale_switch("_down");
   else if (!strcmp(ev->key, "k"))
     _scale_switch("_up");
   else if (!strcmp(ev->key, "l"))
     _scale_switch("_right");
   else if (!strcmp(ev->key, "p"))
     _scale_switch("_prev");
   else if (!strcmp(ev->key, "n"))
     _scale_switch("_next");
   else if (!strcmp(ev->key, "Return"))
     _scale_out(1);
   else
   if (!strcmp(ev->key, "space"))
     _scale_out(1);
   else if (!strcmp(ev->key, "Escape"))
     {
	/* TODO go to previously focused window */
	_scale_out(0);
     }
   else
     {
	E_Action *act;
	Eina_List *l;
	E_Config_Binding_Key *bind;
	E_Binding_Modifier mod;

	for (l = e_config->key_bindings; l; l = l->next)
	  {
	     bind = l->data;
	     mod = 0;

	     if (bind->action && strcmp(bind->action, "scale-windows")) continue;
	     if (!bind->params || strncmp(bind->params, "go_scale", 8)) continue;

	     if (ev->modifiers & ECORE_EVENT_MODIFIER_SHIFT)
               mod |= E_BINDING_MODIFIER_SHIFT;
	     if (ev->modifiers & ECORE_EVENT_MODIFIER_CTRL)
               mod |= E_BINDING_MODIFIER_CTRL;
	     if (ev->modifiers & ECORE_EVENT_MODIFIER_ALT)
               mod |= E_BINDING_MODIFIER_ALT;
	     if (ev->modifiers & ECORE_EVENT_MODIFIER_WIN)
               mod |= E_BINDING_MODIFIER_WIN;

	     if (bind->key && (!strcmp(bind->key, ev->keyname)) &&
		 ((bind->modifiers == mod) || (bind->any_mod)))
	       {
		  if (!(act = e_action_find(bind->action))) continue;
		  if (act->func.go_key)
		    act->func.go_key(E_OBJECT(zone), bind->params, ev);
		  else if (act->func.go)
		    act->func.go(E_OBJECT(zone), bind->params);
	       }
	  }
     }
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_scale_cb_key_up(void *data, int type, void *event)
{
   Ecore_Event_Key *ev = event;

   if (ev->window != input_win)
     return ECORE_CALLBACK_PASS_ON;

   if (!scale_state)
     return ECORE_CALLBACK_PASS_ON;

   if (!e_mod_hold_modifier_check(event))
     _scale_out(1);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_scale_run(E_Manager *man)
{
   Eina_List *l;
   E_Manager_Comp_Source *src;
   Ecore_Event_Handler *h;
   Evas *e;
   int i;
   Item *it;
   E_Border *bd;
   
   e = e_manager_comp_evas_get(man);
   if (!e) return EINA_FALSE;

   zone = e_util_zone_current_get(e_manager_current_get());
   current_desk = e_desk_current_get(zone);

   start_time = ecore_time_get();

   input_win = ecore_x_window_input_new(zone->container->win, 0, 0, 1, 1);
   ecore_x_window_show(input_win);
   if (!e_grabinput_get(input_win, 0, input_win))
     {
	ecore_x_window_free(input_win);
	input_win = 0;
	return EINA_FALSE;
     }

   msg_handler = e_msg_handler_add(_scale_handler, NULL);

   h = ecore_event_handler_add(ECORE_EVENT_MOUSE_BUTTON_DOWN,
			       _scale_cb_mouse_down, e);
   handlers = eina_list_append(handlers, h);

   h = ecore_event_handler_add(ECORE_EVENT_MOUSE_BUTTON_UP,
			       _scale_cb_mouse_up, e);
   handlers = eina_list_append(handlers, h);

   h = ecore_event_handler_add(ECORE_EVENT_MOUSE_MOVE,
			       _scale_cb_mouse_move, e);
   handlers = eina_list_append(handlers, h);

   h = ecore_event_handler_add(ECORE_EVENT_KEY_DOWN,
   			       _scale_cb_key_down, e);
   handlers = eina_list_append(handlers, h);

   h = ecore_event_handler_add(ECORE_EVENT_KEY_UP,
   			       _scale_cb_key_up, e);
   handlers = eina_list_append(handlers, h);

   EINA_LIST_FOREACH((Eina_List *)e_manager_comp_src_list(man), l, src)
     _scale_win_new(e, man, src, current_desk);

   if (eina_list_count(items) < 1)
     {
	_scale_finish();
	return EINA_FALSE;
     }

   if ((eina_list_count(items) < 2) && (!show_all_desks))
     {
	_scale_finish();
	return EINA_FALSE;
     }

   if (show_all_desks)
     spacing = scale_conf->desks_spacing;
   else
     spacing = scale_conf->spacing;

   if (!scale_conf->fade_popups)
     {
	e_zone_useful_geometry_get(zone, &use_x, &use_y, &use_w, &use_h);
	use_w += use_x - spacing*2;
	use_h += use_y - spacing*2;
	use_x += spacing;
	use_y += spacing;
     }
   else
     {
	use_w = zone->w - spacing;
	use_h = zone->h - spacing;
	use_x = use_y = spacing;
     }

   min_x = -zone->w * zone->desk_x_current;
   min_y = -zone->h * zone->desk_y_current;
   max_x =  zone->w + zone->w * ((zone->desk_x_count - 1) - zone->desk_x_current);
   max_y =  zone->h + zone->h * ((zone->desk_y_count - 1) - zone->desk_y_current);

   /* scale all windows down to be next to each other on one zone */
   if (scale_layout)
     _scale_place_slotted();
   else
     _scale_place_natural();

   min_x = use_x;
   min_y = use_y;
   max_x = use_w;
   max_y = use_h;

   if ((scale_conf->grow && !show_all_desks) ||
       (scale_conf->desks_grow && show_all_desks))
     {
	i = 0;
   	while (i++ < GROW_RUNS && _scale_grow());
	DBG("grow %d", i);
     }

   if ((scale_conf->tight && !show_all_desks) ||
       (scale_conf->desks_tight && show_all_desks))
     {
	items = eina_list_sort(items, eina_list_count(items), _cb_sort_center);
	i = 0;
	while (i++ < SHRINK_RUNS && _scale_shrink());
	DBG("shrunk %d", i);
     }

   if (show_all_desks)
     {
	/* center and move windows near visible desk
	 * to make the sliding smoother */

	min_x = zone->w;
	min_y = zone->h;
	max_x = 0;
	max_y = 0;

	EINA_LIST_FOREACH(items, l, it)
	  {
	     if (it->x < min_x) min_x = it->x;
	     if (it->y < min_y) min_y = it->y;
	     if (it->x + it->w > max_x) max_x = it->x + it->w;
	     if (it->y + it->h > max_y) max_y = it->y + it->h;
	  }

	EINA_LIST_FOREACH(items, l, it)
	  {
	     it->x = (it->x - min_x) + use_x + ((use_w - use_x) - (max_x - min_x))/2.0;
	     it->y = (it->y - min_y) + use_y + ((use_h - use_y) - (max_y - min_y))/2.0;

	     if (it->dx > 0) it->bd_x =  zone->w + it->bd->x/4;
	     if (it->dy > 0) it->bd_y =  zone->h + it->bd->y/4;
	     if (it->dx < 0) it->bd_x = -zone->w + (zone->w - it->bd->w) + it->bd->x/4;
	     if (it->dy < 0) it->bd_y = -zone->h + (zone->h - it->bd->h) + it->bd->y/4;
	  }
     }

   DBG("time: %f\n", ecore_time_get() - start_time);

   evas_event_feed_mouse_in(e, ecore_x_current_time_get(), NULL);
   evas_event_feed_mouse_move(e, -1000000, -1000000,
                              ecore_x_current_time_get(), NULL);

   bd = e_border_focused_get();

   EINA_LIST_FOREACH(items, l , it)
     if (it->bd == bd) break;

   if (it)
     selected_item = it;
   else
     selected_item = eina_list_data_get(items);

   edje_object_signal_emit(selected_item->o, "mouse,in", "e");

   if (scale_conf->pager_fade_windows)
     {
	EINA_LIST_FOREACH(items, l, it)
	  if (it->bd->desk != current_desk)
	    evas_object_color_set(it->o_win, 0, 0, 0, 0);
     }

   _scale_in();

   return EINA_TRUE;
}


static Eina_Bool
_scale_cb_mouse_move(void *data, int type, void *event)
{
   Ecore_Event_Mouse_Move *ev = event;

   evas_event_feed_mouse_move((Evas *) data, ev->x, ev->y, ev->timestamp, NULL);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_scale_cb_mouse_down(void *data, int type, void *event)
{
   Ecore_Event_Mouse_Button *ev = event;
   Evas_Button_Flags flags = EVAS_BUTTON_NONE;
   Eina_List *l;
   Item *it;

   EINA_LIST_FOREACH(items, l, it)
     if (E_INTERSECTS(ev->x, ev->y, 1, 1, it->x, it->y, it->w, it->h))
       break;

   if (!it)
     {
	_scale_out(1);
	return ECORE_CALLBACK_PASS_ON;
     }

   if (ev->double_click) flags |= EVAS_BUTTON_DOUBLE_CLICK;
   if (ev->triple_click) flags |= EVAS_BUTTON_TRIPLE_CLICK;
   evas_event_feed_mouse_down((Evas *)data, ev->buttons, flags, ev->timestamp, NULL);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_scale_cb_mouse_up(void *data, int type, void *event)
{
   Ecore_Event_Mouse_Button *ev = event;
   Evas_Button_Flags flags = EVAS_BUTTON_NONE;

   evas_event_feed_mouse_up((Evas *)data, ev->buttons, flags, ev->timestamp, NULL);

   return ECORE_CALLBACK_PASS_ON;
}

Eina_Bool
scale_run(E_Manager *man, const char *params, int _init_method)
{
   Eina_Bool ret = EINA_FALSE;

   if (!strncmp(params, "go_scale_all", 12))
     {
	scale_layout = scale_conf->desks_layout_mode;
	show_all_desks = EINA_TRUE;
     }

   else
     {
	scale_layout = scale_conf->layout_mode;
	show_all_desks = EINA_FALSE;
     }

   init_method = _init_method;

   if (init_method == GO_KEY)
     scale_layout = 1;

   if (scale_state)
     {
	if (show_all_desks)
	  _scale_switch(params+12);
	else
	  _scale_switch(params+8);
     }
   else
     {
	if (input_win)
	  return ret;

	ret = _scale_run(man);
     }

   return ret;
}


static void
_scale_handler(void *data, const char *name, const char *info, int val,
	       E_Object *obj, void *msgdata)
{
   E_Manager *man = (E_Manager *)obj;
   E_Manager_Comp_Source *src = (E_Manager_Comp_Source *)msgdata;
   Evas *e;

   if (strcmp(name, "comp.manager")) return;

   DBG("handler... '%s' '%s'\n", name, info);
   
   /* XXX disabled for now. */
   return;

   e = e_manager_comp_evas_get(man);
   if (!strcmp(info, "change.comp"))
     {
        if (!e) DBG("TTT: No comp manager\n");
        else DBG("TTT: comp canvas = %p\n", e);
     }
   else if (!strcmp(info, "resize.comp"))
     {
        DBG("%s: %p | %p\n", info, man, src);
     }
   else if (!strcmp(info, "add.src"))
     {
        /* DBG("%s: %p | %p\n", info, man, src); */
        _scale_win_new(e, man, src, e_desk_current_get(e_util_zone_current_get(man)));

     }
   else if (!strcmp(info, "del.src"))
     {
        DBG("%s: %p | %p\n", info, man, src);
     }
   else if (!strcmp(info, "config.src"))
     {

        DBG("%s: %p | %p\n", info, man, src);
     }
   else if (!strcmp(info, "visible.src"))
     {
        DBG("%s: %p | %p\n", info, man, src);
     }
}
