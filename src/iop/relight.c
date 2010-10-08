/*
    This file is part of darktable,
    copyright (c) 2010 Henrik Andersson.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#ifdef HAVE_GEGL
  #include <gegl.h>
#endif
#include "develop/develop.h"
#include "develop/imageop.h"
#include "control/control.h"
#include "dtgtk/button.h"
#include "dtgtk/slider.h"
#include "dtgtk/gradientslider.h"
#include "gui/gtk.h"
#include "gui/presets.h"
#include "LibRaw/libraw/libraw.h"

#define CLIP(x) ((x<0)?0.0:(x>1.0)?1.0:x)
DT_MODULE(1)

typedef struct dt_iop_relight_params_t
{
  float ev;
  float center;
  float compression;
}
dt_iop_relight_params_t;

void init_presets (dt_iop_module_t *self)
{
 /* sqlite3_exec(darktable.db, "begin", NULL, NULL, NULL);

  dt_gui_presets_add_generic(_("Neutral Grey ND2 (soft)"), self->op, &(dt_iop_relight_params_t){1,0,0,50,0,0} , sizeof(dt_iop_relight_params_t), 1);
  dt_gui_presets_add_generic(_("Neutral Grey ND4 (soft)"), self->op, &(dt_iop_relight_params_t){2,0,0,50,0,0} , sizeof(dt_iop_relight_params_t), 1);
  dt_gui_presets_add_generic(_("Neutral Grey ND8 (soft)"), self->op, &(dt_iop_relight_params_t){3,0,0,50,0,0} , sizeof(dt_iop_relight_params_t), 1);
  dt_gui_presets_add_generic(_("Neutral Grey ND2 (hard)"), self->op, &(dt_iop_relight_params_t){1,75,0,50,0,0} , sizeof(dt_iop_relight_params_t), 1);
  dt_gui_presets_add_generic(_("Neutral Grey ND4 (hard)"), self->op, &(dt_iop_relight_params_t){2,75,0,50,0,0} , sizeof(dt_iop_relight_params_t), 1);
  dt_gui_presets_add_generic(_("Neutral Grey ND8 (hard)"), self->op, &(dt_iop_relight_params_t){3,75,0,50,0,0} , sizeof(dt_iop_relight_params_t), 1);
  dt_gui_presets_add_generic(_("Orange ND2 (soft)"), self->op, &(dt_iop_relight_params_t){1,0,0,50,0.102439,0.8} , sizeof(dt_iop_relight_params_t), 1);
  dt_gui_presets_add_generic(_("Yellow ND2 (soft)"), self->op, &(dt_iop_relight_params_t){1,0,0,50,0.151220,0.5} , sizeof(dt_iop_relight_params_t), 1);
  dt_gui_presets_add_generic(_("Purple ND2 (soft)"), self->op, &(dt_iop_relight_params_t){1,0,0,50,0.824390,0.5} , sizeof(dt_iop_relight_params_t), 1);
  dt_gui_presets_add_generic(_("Green ND2 (soft)"), self->op, &(dt_iop_relight_params_t){1,0,0,50, 0.302439,0.5} , sizeof(dt_iop_relight_params_t), 1);
  dt_gui_presets_add_generic(_("Red ND2 (soft)"), self->op, &(dt_iop_relight_params_t){1,0,0,50,0,0.5} , sizeof(dt_iop_relight_params_t), 1);
  dt_gui_presets_add_generic(_("Blue ND2 (soft)"), self->op, &(dt_iop_relight_params_t){1,0,0,50,0.663415,0.5} , sizeof(dt_iop_relight_params_t), 1);
  dt_gui_presets_add_generic(_("Brown ND4 (soft)"), self->op, &(dt_iop_relight_params_t){2,0,0,50,0.082927,0.25} , sizeof(dt_iop_relight_params_t), 1);
  
  sqlite3_exec(darktable.db, "commit", NULL, NULL, NULL);*/
}

typedef struct dt_iop_relight_gui_data_t
{
  GtkVBox   *vbox1,  *vbox2;                                            // left and right controlboxes
  GtkLabel  *label1,*label2,*label3;            		       	// ev, center, compression
  GtkDarktableSlider *scale1,*scale2;        			// ev,compression
  GtkDarktableGradientSlider *gslider1;				// center
  GtkDarktableButton *button1;                     // Pick median lightess
}
dt_iop_relight_gui_data_t;

typedef struct dt_iop_relight_data_t
{
  float ev;			          	// The ev of relight 0-8 EV
  float center;		          	// the center light value for relight
  float compression;			        
}
dt_iop_relight_data_t;

const char *name()
{
  return _("relight");
}


int 
groups () 
{
  return IOP_GROUP_EFFECT;
}

static inline float
f (const float t, const float c, const float x)
{
  return (t/(1.0f + powf(c, -x*6.0f)) + (1.0f-t)*(x*.5f+.5f));
}

static inline void hue2rgb(float m1,float m2,float hue,float *channel)
{
  if(hue<0.0) hue+=1.0;
  else if(hue>1.0) hue-=1.0;
  
  if( (6.0*hue) < 1.0) *channel=(m1+(m2-m1)*hue*6.0);
  else if((2.0*hue) < 1.0) *channel=m2;
  else if((3.0*hue) < 2.0) *channel=(m1+(m2-m1)*((2.0/3.0)-hue)*6.0);
  else *channel=m1;
}

void rgb2hsl(float r,float g,float b,float *h,float *s,float *l) 
{
  float pmax=fmax(r,fmax(g,b));
  float pmin=fmin(r,fmin(g,b));
  float delta=(pmax-pmin);
  
  *h=*s=*l=0;
  *l=(pmin+pmax)/2.0;
 
  if(pmax!=pmin) 
  {
    *s=*l<0.5?delta/(pmax+pmin):delta/(2.0-pmax-pmin);
  
    if(pmax==r) *h=(g-b)/delta;
    if(pmax==g) *h=2.0+(b-r)/delta;
    if(pmax==b) *h=4.0+(r-g)/delta;
    *h/=6.0;
    if(*h<0.0) *h+=1.0;
    else if(*h>1.0) *h-=1.0;
  }
}

static inline void hsl2rgb(float *r,float *g,float *b,float h,float s,float l)
{
  float m1,m2;
  *r=*g=*b=l;
  if( s==0) return;
  m2=l<0.5?l*(1.0+s):l+s-l*s;
  m1=(2.0*l-m2);
  hue2rgb(m1,m2,h +(1.0/3.0), r);
  hue2rgb(m1,m2,h, g);
  hue2rgb(m1,m2,h - (1.0/3.0), b);

}

#define GAUSS(a,b,c,x) (a*pow(2.718281828,(-pow((x-b),2)/(pow(c,2)))))

void process (struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, void *ivoid, void *ovoid, const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out)
{
  dt_iop_relight_data_t *data = (dt_iop_relight_data_t *)piece->data;
  float *in  = (float *)ivoid;
  float *out = (float *)ovoid;
  float hsl[3]={0};
 
  const float a = 1.0;                                                                  // Height of top
  const float b = -1.0+(data->center*2);                                  // Center of top
  const float c = 0.00001+(0.4*(data->compression/100.0));      // Width
  
#ifdef _OPENMP
  #pragma omp parallel for default(none) shared(roi_out, in, out,data,hsl,stderr) schedule(static)
#endif
  for(int k=0;k<roi_out->width*roi_out->height;k++)
  {
    rgb2hsl (in[3*k+0],in[3*k+1],in[3*k+2],&hsl[0],&hsl[1],&hsl[2]);
    const float x = -1.0+(hsl[2]*2.0);
    float gauss = GAUSS(a,b,c,x);
    if(isnan(gauss) || isinf(gauss)) gauss=1.0;
    float relight = 1.0 / exp2f ( -data->ev * gauss);
    if(isnan(relight) || isinf(relight)) relight=1.0;
    
    for(int l=0;l<3;l++)
      out[3*k+l] = CLIP (in[3*k+l]*relight);
  }
}

static void
picker_callback (GtkDarktableButton *button, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_request_focus(self);
  self->request_color_pick = 1;
}

static void
ev_callback (GtkDarktableSlider *slider, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_relight_params_t *p = (dt_iop_relight_params_t *)self->params;
  p->ev = dtgtk_slider_get_value(slider);
  dt_dev_add_history_item(darktable.develop, self);
}

static void
compression_callback (GtkDarktableSlider *slider, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_relight_params_t *p = (dt_iop_relight_params_t *)self->params;
  p->compression = dtgtk_slider_get_value(slider);
  dt_dev_add_history_item(darktable.develop, self);
}

static void
center_callback(GtkDarktableGradientSlider *slider, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_relight_params_t *p = (dt_iop_relight_params_t *)self->params;
  
  {
    p->center = dtgtk_gradient_slider_get_value(slider);
    dt_dev_add_history_item(darktable.develop, self);
  }
}



void commit_params (struct dt_iop_module_t *self, dt_iop_params_t *p1, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_relight_params_t *p = (dt_iop_relight_params_t *)p1;
#ifdef HAVE_GEGL
  fprintf(stderr, "[relight] TODO: implement gegl version!\n");
  // pull in new params to gegl
#else
  dt_iop_relight_data_t *d = (dt_iop_relight_data_t *)piece->data;
  d->ev = p->ev;
  d->compression = p->compression;
  d->center = p->center;
#endif
}

void init_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
#ifdef HAVE_GEGL
  // create part of the gegl pipeline
  piece->data = NULL;
#else
  piece->data = malloc(sizeof(dt_iop_relight_data_t));
  memset(piece->data,0,sizeof(dt_iop_relight_data_t));
  self->commit_params(self, self->default_params, pipe, piece);
#endif
}

void cleanup_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
#ifdef HAVE_GEGL
  // clean up everything again.
  (void)gegl_node_remove_child(pipe->gegl, piece->input);
  // no free necessary, no data is alloc'ed
#else
  free(piece->data);
#endif
}

void gui_update(struct dt_iop_module_t *self)
{
  dt_iop_module_t *module = (dt_iop_module_t *)self;
  
  self->request_color_pick = 0;
  self->color_picker_box[0] = self->color_picker_box[1] = .25f;
  self->color_picker_box[2] = self->color_picker_box[3] = .75f;
  
  dt_iop_relight_gui_data_t *g = (dt_iop_relight_gui_data_t *)self->gui_data;
  dt_iop_relight_params_t *p = (dt_iop_relight_params_t *)module->params;
  dtgtk_slider_set_value (g->scale1, p->ev);
  dtgtk_slider_set_value (g->scale2, p->compression);
  dtgtk_gradient_slider_set_value(g->gslider1,p->center);
}

void init(dt_iop_module_t *module)
{
  module->params = malloc(sizeof(dt_iop_relight_params_t));
  module->default_params = malloc(sizeof(dt_iop_relight_params_t));
  module->default_enabled = 0;
  module->priority = 252;
  module->params_size = sizeof(dt_iop_relight_params_t);
  module->gui_data = NULL;
  dt_iop_relight_params_t tmp = (dt_iop_relight_params_t){0.33,0,50};
  memcpy(module->params, &tmp, sizeof(dt_iop_relight_params_t));
  memcpy(module->default_params, &tmp, sizeof(dt_iop_relight_params_t));
  
}

void cleanup(dt_iop_module_t *module)
{
  free(module->gui_data);
  module->gui_data = NULL;
  free(module->params);
  module->params = NULL;
}

int button_released(struct dt_iop_module_t *self, double x, double y, int which, uint32_t state)
{
    self->request_color_pick=0;
    return 1;
}

static gboolean
expose (GtkWidget *widget, GdkEventExpose *event, dt_iop_module_t *self)
{ 
  // capture gui color picked event.
  if(darktable.gui->reset) return FALSE;
  if(self->picked_color_max_Lab[0] < self->picked_color_min_Lab[0]) return FALSE;
  if(!self->request_color_pick) return FALSE;
  const float *Lab = self->picked_color_Lab;

  dt_iop_relight_params_t *p = (dt_iop_relight_params_t *)self->params;
  p->center = Lab[0];
  dt_dev_add_history_item(darktable.develop, self);
  darktable.gui->reset = 1;
  dt_iop_relight_gui_data_t *g = (dt_iop_relight_gui_data_t *)self->gui_data;
  dtgtk_gradient_slider_set_value(DTGTK_GRADIENT_SLIDER(g->gslider1),p->center);
  darktable.gui->reset = 0;

  return FALSE;
}

void gui_init(struct dt_iop_module_t *self)
{
  self->gui_data = malloc(sizeof(dt_iop_relight_gui_data_t));
  dt_iop_relight_gui_data_t *g = (dt_iop_relight_gui_data_t *)self->gui_data;
  dt_iop_relight_params_t *p = (dt_iop_relight_params_t *)self->params;

  self->widget = gtk_table_new (3,2,FALSE);
  g_signal_connect(G_OBJECT(self->widget), "expose-event", G_CALLBACK(expose), self);
  
  gtk_table_set_col_spacing(GTK_TABLE(self->widget), 0, 10);
  gtk_table_set_row_spacing(GTK_TABLE(self->widget), 0, 5);
  
  /* adding the labels */
  g->label1 = GTK_LABEL(gtk_label_new(_("light")));     // EV
  g->label2 = GTK_LABEL(gtk_label_new(_("center")));
  g->label3 = GTK_LABEL(gtk_label_new(_("compression")));
  gtk_misc_set_alignment(GTK_MISC(g->label1), 0.0, 0.5);
  gtk_misc_set_alignment(GTK_MISC(g->label2), 0.0, 0.5);
  gtk_misc_set_alignment(GTK_MISC(g->label3), 0.0, 0.5);
  
  gtk_table_attach (GTK_TABLE (self->widget), GTK_WIDGET (g->label1), 0,1,0,1,GTK_FILL,0,0,0);
  gtk_table_attach (GTK_TABLE (self->widget), GTK_WIDGET (g->label2), 0,1,1,2,GTK_FILL,0,0,0);
  gtk_table_attach (GTK_TABLE (self->widget), GTK_WIDGET (g->label3), 0,1,2,3,GTK_FILL,0,0,0);
  
  g->scale1 = DTGTK_SLIDER(dtgtk_slider_new_with_range(DARKTABLE_SLIDER_BAR,-4.0, 4.0, 0.1, p->ev, 2));
  g->scale2 = DTGTK_SLIDER(dtgtk_slider_new_with_range(DARKTABLE_SLIDER_BAR,0.0, 100.0, 0.1, p->compression, 0));
  dtgtk_slider_set_format_type(g->scale2,DARKTABLE_SLIDER_FORMAT_PERCENT);
  
  gtk_table_attach_defaults (GTK_TABLE (self->widget), GTK_WIDGET (g->scale1), 1,2,0,1);
  gtk_table_attach_defaults (GTK_TABLE (self->widget), GTK_WIDGET (g->scale2), 1,2,2,3);
 

 
  /* lightnessslider */
  GtkBox *hbox=GTK_BOX (gtk_hbox_new (FALSE,2));
  int lightness=32768;
  g->gslider1=DTGTK_GRADIENT_SLIDER(dtgtk_gradient_slider_new_with_color((GdkColor){0,0,0,0},(GdkColor){0,lightness,lightness,lightness}));
  gtk_object_set(GTK_OBJECT(g->gslider1), "tooltip-text", _("select the center of filllight"), (char *)NULL);
  g_signal_connect (G_OBJECT (g->gslider1), "value-changed",
        G_CALLBACK (center_callback), self);
  g->button1 = DTGTK_BUTTON (dtgtk_button_new (dtgtk_cairo_paint_aspectflip, 0));
  g_signal_connect (G_OBJECT (g->button1), "clicked",
        G_CALLBACK (picker_callback), self);
  
  gtk_box_pack_start(hbox,GTK_WIDGET (g->gslider1),TRUE,TRUE,0);
  gtk_box_pack_start(hbox,GTK_WIDGET (g->button1),FALSE,FALSE,0);
  gtk_table_attach_defaults (GTK_TABLE (self->widget), GTK_WIDGET (hbox), 1,2,1,2);

  
  gtk_object_set(GTK_OBJECT(g->scale1), "tooltip-text", _("the filllight in EV"), (char *)NULL);
  /* xgettext:no-c-format */
  gtk_object_set(GTK_OBJECT(g->scale2), "tooltip-text", _("compression of filllight:\n0% = soft, 100% = hard"), (char *)NULL);
  
  g_signal_connect (G_OBJECT (g->scale1), "value-changed",
        G_CALLBACK (ev_callback), self);
  g_signal_connect (G_OBJECT (g->scale2), "value-changed",
        G_CALLBACK (compression_callback), self);
}

void gui_cleanup(struct dt_iop_module_t *self)
{
  self->request_color_pick = 0;
  free(self->gui_data);
  self->gui_data = NULL;
}

