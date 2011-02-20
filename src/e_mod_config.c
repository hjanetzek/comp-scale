#include <e.h>
#include "e_mod_main.h"

struct _E_Config_Dialog_Data
{
  int		grow;
  int		tight;
  double	duration;
  double	spacing;
};


static void *_create_data(E_Config_Dialog *cfd);
static void _free_data(E_Config_Dialog *cfd, E_Config_Dialog_Data *cfdata);
static void _fill_data(E_Config_Dialog_Data *cfdata);
static Evas_Object *_basic_create(E_Config_Dialog *cfd, Evas *evas, E_Config_Dialog_Data *cfdata);
static int _basic_apply(E_Config_Dialog *cfd, E_Config_Dialog_Data *cfdata);

E_Config_Dialog *
e_int_config_scale_module(E_Container *con, const char *params)
{
   E_Config_Dialog *cfd = NULL;
   E_Config_Dialog_View *v = NULL;
   char buf[4096];

   if (e_config_dialog_find("Scale", "appearance/comp-scale")) return NULL;

   v = E_NEW(E_Config_Dialog_View, 1);
   if (!v) return NULL;

   v->create_cfdata	   = _create_data;
   v->free_cfdata	   = _free_data;
   v->basic.create_widgets = _basic_create;
   v->basic.apply_cfdata   = _basic_apply;

   snprintf(buf, sizeof(buf), "%s/e-module-scale.edj", scale_conf->module->dir);

   cfd = e_config_dialog_new(con, D_("Scale Windows Module"), "Scale",
                             "appearance/comp-scale", buf, 0, v, NULL);

   e_dialog_resizable_set(cfd->dia, 1);
   scale_conf->cfd = cfd;
   return cfd;
}

static void *
_create_data(E_Config_Dialog *cfd)
{
   E_Config_Dialog_Data *cfdata = NULL;

   cfdata = E_NEW(E_Config_Dialog_Data, 1);
   _fill_data(cfdata);
   return cfdata;
}

static void
_free_data(E_Config_Dialog *cfd, E_Config_Dialog_Data *cfdata)
{
   scale_conf->cfd = NULL;
   E_FREE(cfdata);
}

static void
_fill_data(E_Config_Dialog_Data *cfdata)
{
   cfdata->tight    = scale_conf->tight;
   cfdata->grow	    = scale_conf->grow;
   cfdata->duration = scale_conf->scale_duration;
   cfdata->spacing  = scale_conf->spacing;
}

static void
_cb_test(void *data, void *data2)
{
   E_Config_Dialog_Data *cfdata = data;
   
   scale_conf->grow	      = cfdata->grow;
   scale_conf->tight	      = cfdata->tight;
   scale_conf->scale_duration = cfdata->duration;
   scale_conf->spacing	      = cfdata->spacing;

   scale_run(e_manager_current_get());
}

static Evas_Object *
_basic_create(E_Config_Dialog *cfd, Evas *evas, E_Config_Dialog_Data *cfdata)
{
   Evas_Object *o = NULL, *of = NULL, *ow = NULL;

   o = e_widget_list_add(evas, 0, 0);

   of = e_widget_framelist_add(evas, D_("Layout Options"), 0);
   e_widget_framelist_content_align_set(of, 0.0, 0.0);
   ow = e_widget_check_add(evas, D_("Grow more!"), &(cfdata->grow));
   e_widget_framelist_object_append(of, ow);
   ow = e_widget_check_add(evas, D_("Keep it tight!"), &(cfdata->tight));
   e_widget_framelist_object_append(of, ow);

   ow = e_widget_label_add (evas, D_("Minimum space between windows"));
   e_widget_framelist_object_append (of, ow);
   ow = e_widget_slider_add (evas, 1, 0, D_("%1.0f"), 2.0, 64.0,
                             1.0, 0, &(cfdata->spacing), NULL,100);
   e_widget_framelist_object_append (of, ow);

   ow = e_widget_label_add (evas, D_("Scale duration"));
   e_widget_framelist_object_append (of, ow);
   ow = e_widget_slider_add (evas, 1, 0, D_("%1.2f"), 0.1, 3.0,
                             0.01, 0, &(cfdata->duration), NULL,100);
   e_widget_framelist_object_append (of, ow);

   ow = e_widget_button_add(evas, D_("Test"), NULL, _cb_test, cfdata, NULL);
   e_widget_framelist_object_append (of, ow);

   e_widget_list_object_append(o, of, 1, 1, 0.5);

   return o;
}

static int
_basic_apply(E_Config_Dialog *cfd, E_Config_Dialog_Data *cfdata)
{
   scale_conf->grow	      = cfdata->grow;
   scale_conf->tight	      = cfdata->tight;
   scale_conf->scale_duration = cfdata->duration;
   scale_conf->spacing	      = cfdata->spacing;
   e_config_save_queue();
   return 1;
}
