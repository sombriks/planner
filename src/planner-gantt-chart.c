/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2004      Imendio HB
 * Copyright (C) 2001-2002 CodeFactory AB
 * Copyright (C) 2001-2002 Richard Hult <richard@imendio.com>
 * Copyright (C) 2001-2002 Mikael Hallendal <micke@imendio.com>
 * Copyright (C) 2004      Alvaro del Castillo <acs@barrapunto.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libgnome/gnome-i18n.h>
#include <libplanner/mrp-task.h>
#include "planner-marshal.h"
#include "planner-gantt-chart.h"
#include "planner-gantt-header.h"
#include "planner-gantt-background.h"
#include "planner-gantt-model.h"
#include "planner-gantt-row.h"
#include "planner-relation-arrow.h"
#include "planner-scale-utils.h"

/* Padding to the left and right of the contents of the chart. */
#define PADDING 100.0

#define ZOOM_IN_LIMIT 12
#define ZOOM_OUT_LIMIT 0

#define DEFAULT_ZOOM_LEVEL 7
#define SCALE(n) (f*pow(2,(n)-19))
#define ZOOM(x) (log((x)/f)/log(2)+19)

/* Font width factor. */
static gdouble f = 1.0;

#define CRITICAL_PATH_KEY "/apps/planner/views/gantt_view/highlight_critical_path"


typedef struct _TreeNode TreeNode; 
typedef void (*TreeFunc) (TreeNode *node, gpointer data);

struct _TreeNode {
	MrpTask          *task;
	GnomeCanvasItem  *item;
	TreeNode         *parent;
	TreeNode        **children;
	guint             num_children;
	guint             expanded : 1;
};

typedef struct {
	gulong   id;
	gpointer instance;
} ConnectData;

struct _PlannerChartPriv {
	GtkWidget       *header;
	GnomeCanvas     *canvas;

	GtkAdjustment   *hadjustment;
	GtkAdjustment   *vadjustment;
	
	GtkTreeModel    *model;
	TreeNode        *tree;
	PlannerTaskTree *view;
	
	GHashTable      *relation_hash;
	
	GnomeCanvasItem *background;

	gdouble          zoom;

	gint             row_height;

	/* Cached height. */
	gdouble          height;
	
	mrptime          project_start;
	mrptime          last_time;

	gboolean         height_changed;
	guint            reflow_idle_id;

	/* Critical path. */
	gboolean         highlight_critical;

	/* Keep a list of signal connection ids, so we can remove them
	 * easily.
	 */
	GList           *signal_ids;
};

/* Properties */
enum {
	PROP_0,
	PROP_HEADER_HEIGHT,
	PROP_ROW_HEIGHT,
	PROP_MODEL
};

enum {
	STATUS_UPDATED,
	RESOURCE_CLICKED,
	LAST_SIGNAL
};

static void        chart_class_init               (PlannerChartClass  *klass);
static void        chart_init                     (PlannerChart       *chart);
static void        chart_finalize                 (GObject            *object);
static void        chart_set_property             (GObject            *object,
							 guint               prop_id,
							 const GValue       *value,
							 GParamSpec         *pspec);
static void        chart_get_property             (GObject            *object,
							 guint               prop_id,
							 GValue             *value,
							 GParamSpec         *pspec);
static void        chart_destroy                  (GtkObject          *object); 
static void        chart_style_set                (GtkWidget          *widget,
							 GtkStyle           *prev_style);
static void        chart_realize                  (GtkWidget          *widget);
static void        chart_unrealize                (GtkWidget          *widget);
static void        chart_map                      (GtkWidget          *widget);
static void        chart_size_allocate            (GtkWidget          *widget,
							 GtkAllocation      *allocation);
static void        chart_set_adjustments          (PlannerChart       *chart,
							 GtkAdjustment      *hadj,
							 GtkAdjustment      *vadj);
static void        chart_row_changed              (GtkTreeModel       *model,
							 GtkTreePath        *path,
							 GtkTreeIter        *iter,
							 gpointer            data);
static void        chart_row_inserted             (GtkTreeModel       *model,
							 GtkTreePath        *path,
							 GtkTreeIter        *iter,
							 gpointer            data);
static void        chart_row_deleted              (GtkTreeModel       *model,
							 GtkTreePath        *path,
							 gpointer            data);
static void        chart_rows_reordered           (GtkTreeModel       *model,
							 GtkTreePath        *parent,
							 GtkTreeIter        *iter,
							 gint               *new_order,
							 gpointer            data);
static void        chart_relation_added           (MrpTask            *task,
							 MrpRelation        *relation,
							 PlannerChart  *chart);
static void        chart_relation_removed         (MrpTask            *task,
							 MrpRelation        *relation,
							 PlannerChart  *chart);
static void        chart_task_removed             (MrpTask            *task,
							 PlannerChart  *chart);
static void        chart_build_tree               (PlannerChart  *chart);
static void        chart_reflow                   (PlannerChart  *chart,
							 gboolean            height_changed);
static void        chart_reflow_now               (PlannerChart  *chart);
static TreeNode *  chart_insert_task              (PlannerChart  *chart,
							 GtkTreePath        *path,
							 MrpTask            *task);
static PlannerRelationArrow *
chart_add_relation                                (PlannerChart  *chart,
							 TreeNode           *task,
							 TreeNode           *predecessor,
							 MrpRelationType     type);
static void        chart_set_scroll_region        (PlannerChart  *chart,
							 gdouble             x1,
							 gdouble             y1,
							 gdouble             x2,
							 gdouble             y2);
static void        chart_set_zoom                 (PlannerChart  *chart,
							 gdouble             level);
static gint        chart_get_width                (PlannerChart  *chart);
static PlannerChartRow *chart_get_row_from_task   (PlannerChart  *chart,
							 MrpTask            *task);
static TreeNode *  chart_tree_node_new            (void);
static void        chart_tree_node_insert_path    (TreeNode           *node,
							 GtkTreePath        *path,
							 TreeNode           *new_node);
static TreeNode *  chart_tree_node_at_path        (TreeNode           *root,
							 GtkTreePath        *path);
static void        chart_tree_node_remove         (PlannerChart  *chart,
							 TreeNode           *node);
static void        chart_tree_traverse            (TreeNode           *node,
							 TreeFunc            func,
							 gpointer            data);

static guint         signals[LAST_SIGNAL];
static GtkVBoxClass *parent_class = NULL;


GType
planner_chart_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info = {
			sizeof (PlannerChartClass),
			NULL,		/* base_init */
			NULL,		/* base_finalize */
			(GClassInitFunc) chart_class_init,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			sizeof (PlannerChart),
			0,              /* n_preallocs */
			(GInstanceInitFunc) chart_init
		};

		type = g_type_register_static (GTK_TYPE_VBOX, "PlannerChart",
					       &info, 0);
	}
	
	return type;
}

static void
chart_class_init (PlannerChartClass *class)
{
	GObjectClass      *o_class;
	GtkObjectClass    *object_class;
	GtkWidgetClass    *widget_class;
	GtkContainerClass *container_class;

	parent_class = g_type_class_peek_parent (class);

	o_class         = (GObjectClass *) class;
	object_class    = (GtkObjectClass *) class;
	widget_class    = (GtkWidgetClass *) class;
	container_class = (GtkContainerClass *) class;

	o_class->set_property = chart_set_property;
	o_class->get_property = chart_get_property;
	o_class->finalize     = chart_finalize;

	object_class->destroy = chart_destroy;

	widget_class->style_set     = chart_style_set;
	widget_class->realize       = chart_realize;
	widget_class->map           = chart_map;
	widget_class->unrealize     = chart_unrealize;
	widget_class->size_allocate = chart_size_allocate;

	class->set_scroll_adjustments = chart_set_adjustments;
		
	widget_class->set_scroll_adjustments_signal =
		g_signal_new ("set_scroll_adjustments",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PlannerChartClass, set_scroll_adjustments),
			      NULL, NULL,
			      planner_marshal_VOID__OBJECT_OBJECT,
			      G_TYPE_NONE, 2,
			      GTK_TYPE_ADJUSTMENT, GTK_TYPE_ADJUSTMENT);

	signals[STATUS_UPDATED] =
		g_signal_new ("status-updated",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      planner_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);

	signals[RESOURCE_CLICKED] =
		g_signal_new ("resource-clicked",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      planner_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1,
			      MRP_TYPE_RESOURCE);

	/* Properties. */
	g_object_class_install_property (o_class,
					 PROP_MODEL,
					 g_param_spec_object ("model",
							      NULL,
							      NULL,
							      GTK_TYPE_TREE_MODEL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (o_class,
					 PROP_HEADER_HEIGHT,
					 g_param_spec_int ("header-height",
							   NULL,
							   NULL,
							   0, G_MAXINT, 0,
							   G_PARAM_READWRITE));

	g_object_class_install_property (o_class,
					 PROP_ROW_HEIGHT,
					 g_param_spec_int ("row-height",
							   NULL,
							   NULL,
							   0, G_MAXINT, 0,
							   G_PARAM_READWRITE));
}

static void
chart_init (PlannerChart *chart)
{
	PlannerChartPriv *priv;
	GConfClient           *gconf_client;
	
	gtk_widget_set_redraw_on_allocate (GTK_WIDGET (chart), FALSE);

	priv = g_new0 (PlannerChartPriv, 1);
	chart->priv = priv;

	priv->tree = chart_tree_node_new ();

	priv->zoom = DEFAULT_ZOOM_LEVEL;

	priv->height_changed = FALSE;
	priv->reflow_idle_id = 0;
	
	gtk_box_set_homogeneous (GTK_BOX (chart), FALSE);
	gtk_box_set_spacing (GTK_BOX (chart), 0);

	priv->header = g_object_new (PLANNER_TYPE_CHART_HEADER,
				     "scale", SCALE (priv->zoom),
				     "zoom", priv->zoom,
				     NULL);
	
	gtk_box_pack_start (GTK_BOX (chart),
			    GTK_WIDGET (priv->header),
			    FALSE,	/* expand */
			    TRUE,	/* fill */
			    0);		/* padding */

	priv->canvas = GNOME_CANVAS (gnome_canvas_new ());
	priv->canvas->close_enough = 5;
	gnome_canvas_set_center_scroll_region (priv->canvas, FALSE);

	/* Easiest way to get access to the chart from the canvas items. */
	g_object_set_data (G_OBJECT (priv->canvas), "chart", chart);
	
	gtk_box_pack_start (GTK_BOX (chart),
			    GTK_WIDGET (priv->canvas),
			    TRUE,
			    TRUE,
			    0);

	priv->row_height = -1;
	priv->height = -1;
	priv->project_start = MRP_TIME_INVALID;
	priv->last_time = MRP_TIME_INVALID;

	priv->background = gnome_canvas_item_new (gnome_canvas_root (priv->canvas),
						  PLANNER_TYPE_CHART_BACKGROUND,
						  "scale", SCALE (priv->zoom),
						  "zoom", priv->zoom,
						  NULL);

	priv->relation_hash = g_hash_table_new (NULL, NULL);

	gconf_client = planner_application_get_gconf_client ();
	priv->highlight_critical = gconf_client_get_bool (
		gconf_client, CRITICAL_PATH_KEY, NULL);
}

static void
chart_set_property (GObject      *object,
			  guint         prop_id,
			  const GValue *value,
			  GParamSpec   *pspec)
{
	PlannerChart *chart;

	chart = PLANNER_CHART (object);

	switch (prop_id) {
	case PROP_MODEL:
		planner_chart_set_model (chart, g_value_get_object (value));
		break;
	case PROP_HEADER_HEIGHT:
		g_object_set (chart->priv->header,
			      "height", g_value_get_int (value),
			      NULL);
		break;
	case PROP_ROW_HEIGHT:
		chart->priv->row_height = g_value_get_int (value);
		chart_reflow (chart, TRUE);
		break;
	default:
		break;
	}
}

static void
chart_get_property (GObject    *object,
			  guint       prop_id,
			  GValue     *value,
			  GParamSpec *pspec)
{
	PlannerChart *chart;

	chart = PLANNER_CHART (object);

	switch (prop_id) {
	case PROP_MODEL:
		g_value_set_object (value, G_OBJECT (chart->priv->model));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
chart_finalize (GObject *object)
{
	PlannerChart *chart = PLANNER_CHART (object);

	g_hash_table_destroy (chart->priv->relation_hash);
	
	g_free (chart->priv);

	if (G_OBJECT_CLASS (parent_class)->finalize) {
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
	}
}

static void
chart_destroy (GtkObject *object)
{
	PlannerChart *chart = PLANNER_CHART (object);

	planner_chart_set_model (chart, NULL);

	/* FIXME: free more stuff. */

	if (chart->priv->model != NULL) {
		g_object_unref (chart->priv->model);
		chart->priv->model = NULL;
	}

	if (GTK_OBJECT_CLASS (parent_class)->destroy) {
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
	}
}

static void
chart_style_set (GtkWidget *widget,
		       GtkStyle  *prev_style)
{
	PlannerChart     *chart;
	PlannerChartPriv *priv;
	PangoContext     *context;
	PangoFontMetrics *metrics;

	g_return_if_fail (PLANNER_IS_CHART (widget));

	if (GTK_WIDGET_CLASS (parent_class)->style_set) {
		GTK_WIDGET_CLASS (parent_class)->style_set (widget,
							    prev_style);
	}

	chart = PLANNER_CHART (widget);
	priv = chart->priv;

	context = gtk_widget_get_pango_context (widget);
	
	metrics = pango_context_get_metrics (context,
					     widget->style->font_desc,
					     NULL);
	
	f = 0.2 * pango_font_metrics_get_approximate_char_width (metrics) / PANGO_SCALE;

	/* Re-layout with the new factor. */
	chart_set_zoom (PLANNER_CHART (widget), priv->zoom);
}

static void
chart_realize (GtkWidget *widget)
{
	PlannerChart     *chart;
	PlannerChartPriv *priv;
	GdkColormap           *colormap;
	GtkStyle              *style;
	GtkWidget             *canvas;

	chart = PLANNER_CHART (widget);
	priv = chart->priv;

	canvas = GTK_WIDGET (priv->canvas);

	if (GTK_WIDGET_CLASS (parent_class)->realize) {
		(* GTK_WIDGET_CLASS (parent_class)->realize) (widget);
	}
	
	/* Set the background to white. */
	style = gtk_style_copy (canvas->style);
	colormap = gtk_widget_get_colormap (canvas);
	gdk_color_white (colormap, &style->bg[GTK_STATE_NORMAL]);
	gtk_widget_set_style (canvas, style);
	gtk_style_unref (style);

	chart_set_zoom (chart, priv->zoom);
}

static void
chart_unrealize (GtkWidget *widget)
{
	PlannerChart *chart;

	chart = PLANNER_CHART (widget);

	if (GTK_WIDGET_CLASS (parent_class)->unrealize) {
		(* GTK_WIDGET_CLASS (parent_class)->unrealize) (widget);
	}
}

/* We reflow when we are mapped. This updates the chart when changes are made
 * for example in the task view and this view is hidden.
 */
static void
chart_map (GtkWidget *widget)
{
	PlannerChart *chart;

	chart = PLANNER_CHART (widget);

	if (GTK_WIDGET_CLASS (parent_class)->map) {
		(* GTK_WIDGET_CLASS (parent_class)->map) (widget);
	}

	chart->priv->height_changed = TRUE;
	chart_reflow_now (chart);
}

static void
chart_size_allocate (GtkWidget     *widget,
			   GtkAllocation *allocation)
{
	PlannerChart *chart;
	gboolean           height_changed;

	height_changed = widget->allocation.height != allocation->height;
	
	GTK_WIDGET_CLASS (parent_class)->size_allocate (widget, allocation);

	chart = PLANNER_CHART (widget);

	/* Force reflow (if we are mapped), since it looks smoother with less
	 * jumping around.
	 */
	if (GTK_WIDGET_MAPPED (chart)) {
		chart_reflow_now (chart);
	}
}

static void
chart_set_adjustments (PlannerChart *chart,
			     GtkAdjustment     *hadj,
			     GtkAdjustment     *vadj)
{
	PlannerChartPriv *priv;
	gboolean               need_adjust = FALSE;

	g_return_if_fail (hadj == NULL || GTK_IS_ADJUSTMENT (hadj));
	g_return_if_fail (vadj == NULL || GTK_IS_ADJUSTMENT (vadj));

	priv = chart->priv;
	
	if (hadj == NULL) {
		hadj = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0));
	}
	if (vadj == NULL) {
		vadj = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0));
	}
	
	if (priv->hadjustment && (priv->hadjustment != hadj)) {
		g_object_unref (priv->hadjustment);
	}
	
	if (priv->vadjustment && (priv->vadjustment != vadj)) {
		g_object_unref (priv->vadjustment);
	}
	
	if (priv->hadjustment != hadj) {
		priv->hadjustment = hadj;
		g_object_ref (priv->hadjustment);
		gtk_object_sink (GTK_OBJECT (priv->hadjustment));

		gtk_widget_set_scroll_adjustments (priv->header,
						   hadj,
						   NULL);

		need_adjust = TRUE;
	}
	
	if (priv->vadjustment != vadj) {
		priv->vadjustment = vadj;
		g_object_ref (priv->vadjustment);
		gtk_object_sink (GTK_OBJECT (priv->vadjustment));
		
		need_adjust = TRUE;
	}
	
	if (need_adjust) {
		gtk_widget_set_scroll_adjustments (GTK_WIDGET (priv->canvas),
						   hadj,
						   vadj);
	}
}

static void
chart_row_changed (GtkTreeModel *model,
			 GtkTreePath  *path,
			 GtkTreeIter  *iter,
			 gpointer      data)
{
#if 0
	gboolean free_path = FALSE;

	g_return_if_fail (path != NULL || iter != NULL);

	if (path == NULL) {
		path = gtk_tree_model_get_path (model, iter);
		free_path = TRUE;
	}
	else if (iter == NULL) {
		gtk_tree_model_get_iter (model, iter, path);
	}

	if (free_path) {
		gtk_tree_path_free (path);
	}
#endif
}

static void
chart_row_inserted (GtkTreeModel *model,
			  GtkTreePath  *path,
			  GtkTreeIter  *iter,
			  gpointer      data)
{
	PlannerChart     *chart;
	PlannerChartPriv *priv;
	gboolean               free_path = FALSE;
	MrpTask               *task;
	TreeNode              *node;

	chart = data;
	priv = chart->priv;

	g_return_if_fail (path != NULL || iter != NULL);

	if (path == NULL) {
		path = gtk_tree_model_get_path (model, iter);
		free_path = TRUE;
	}
	else if (iter == NULL) {
		gtk_tree_model_get_iter (model, iter, path);
	}
	
	task = planner_chart_model_get_task (PLANNER_CHART_MODEL (model), iter);

	node = chart_insert_task (chart, path, task);
	
	chart_reflow (chart, TRUE);

	if (free_path) {
		gtk_tree_path_free (path);
	}
}

static void
chart_row_has_child_toggled (GtkTreeModel *model,
				   GtkTreePath  *path,
				   GtkTreeIter  *iter,
				   gpointer      data)
{
	PlannerChart     *chart;
	PlannerChartPriv *priv;
	gboolean               free_path = FALSE;
	TreeNode              *node;

	chart = data;
	priv = chart->priv;

	g_return_if_fail (path != NULL || iter != NULL);

	if (path == NULL) {
		path = gtk_tree_model_get_path (model, iter);
		free_path = TRUE;
	}
	
	node = chart_tree_node_at_path (priv->tree, path);
	gnome_canvas_item_request_update (node->item);
	
	if (free_path) {
		gtk_tree_path_free (path);
	}
}

static void
chart_remove_children (PlannerChart *chart,
			     TreeNode          *node)
{
	gint i;
	
	for (i = 0; i < node->num_children; i++) {
		chart_remove_children (chart, node->children[i]);
	}

	gtk_object_destroy (GTK_OBJECT (node->item));
	node->item = NULL;
	node->task = NULL;

	g_free (node->children);
	node->children = NULL;
	g_free (node);
}

static void
chart_row_deleted (GtkTreeModel *model,
			 GtkTreePath  *path,
			 gpointer      data)
{
	PlannerChart *chart = data;
	TreeNode          *node;
	
	g_return_if_fail (path != NULL);

	node = chart_tree_node_at_path (chart->priv->tree, path);

	chart_tree_node_remove (chart, node);
	chart_remove_children (chart, node);

	chart_reflow (chart, TRUE);
}

static void
chart_rows_reordered (GtkTreeModel *model,
			    GtkTreePath  *parent,
			    GtkTreeIter  *iter,
			    gint         *new_order,
			    gpointer      data)
{
#if 0
	gint       len;

	len = gtk_tree_model_iter_n_children (model, iter);

	if (len < 2) {
		return;
	}

	/* FIXME: impl. */
#endif
}

static void
chart_build_tree_do (PlannerChart *chart,
			   GtkTreeIter       *iter,
			   GHashTable        *hash)
{
	PlannerChartPriv *priv;
	GtkTreeIter            child;
	GtkTreePath           *path;
	MrpTask               *task;
	TreeNode              *node;

	priv = chart->priv;
	
	do {
		task = planner_chart_model_get_task (PLANNER_CHART_MODEL (priv->model), iter);

		path = gtk_tree_model_get_path (priv->model, iter);

		node = chart_insert_task (chart, path, task);
		g_hash_table_insert (hash, task, node);

		gtk_tree_path_free (path);
		
		if (gtk_tree_model_iter_children (priv->model, &child, iter)) {
			chart_build_tree_do (chart, &child, hash);
		}
	} while (gtk_tree_model_iter_next (priv->model, iter));
}

static void
chart_build_relations (PlannerChart *chart,
			     GtkTreeIter       *iter,
			     GHashTable        *hash)
{
	PlannerChartPriv *priv;
	GtkTreeIter            child;
	MrpTask               *task;
	MrpRelation           *relation;
	MrpTask               *predecessor;
	TreeNode              *task_node;
	TreeNode              *predecessor_node;
	GList                 *relations, *l;
	PlannerRelationArrow  *arrow;
	MrpRelationType	       rel_type;

	priv = chart->priv;

	do {
		task = planner_chart_model_get_task (
			PLANNER_CHART_MODEL (priv->model), iter);
		
		relations = mrp_task_get_predecessor_relations (task);

		for (l = relations; l; l = l->next) {
			relation = l->data;

			predecessor = mrp_relation_get_predecessor (relation);
			
			task_node = g_hash_table_lookup (hash, task);
			predecessor_node = g_hash_table_lookup (hash, predecessor);

			rel_type = mrp_relation_get_relation_type(relation);

			arrow = chart_add_relation (chart, 
							  task_node, 
							  predecessor_node, 
							  rel_type);

			g_hash_table_insert (priv->relation_hash, relation, arrow);
		}
		
		if (gtk_tree_model_iter_children (priv->model, &child, iter)) {
			chart_build_relations (chart, &child, hash);
		}
		
	} while (gtk_tree_model_iter_next (priv->model, iter));
}

static void
chart_build_tree (PlannerChart *chart)
{
	GtkTreeIter  iter;
	GtkTreePath *path;
	GHashTable  *hash;

	path = gtk_tree_path_new_root ();
	if (!gtk_tree_model_get_iter (chart->priv->model, &iter, path)) {
		gtk_tree_path_free (path);
		return;
	}

	hash = g_hash_table_new (NULL, NULL);
	
	chart_build_tree_do (chart, &iter, hash);

	gtk_tree_model_get_iter (chart->priv->model, &iter, path);
	chart_build_relations (chart, &iter, hash);
			    
	gtk_tree_path_free (path);
	g_hash_table_destroy (hash);

	/* FIXME: free paths used as keys. */
}

static gboolean
node_is_visible (TreeNode *node)
{
	g_return_val_if_fail (node->parent != NULL, FALSE);
	
	while (node->parent) {
		if (!node->parent->expanded) {
			return FALSE;
		}
		node = node->parent;
	}
	
	return TRUE;
}

static gdouble
chart_reflow_do (PlannerChart *chart, TreeNode *root, gdouble start_y)
{
	gdouble   row_y;
	TreeNode *node;
	guint     i;
	gint      row_height;

	if (root->children == NULL) {
		return start_y;
	}
	
	node = root->children[0];
	row_y = start_y;

	row_height = chart->priv->row_height;
	if (row_height == -1) {
		row_height = 23;
	}
	
	for (i = 0; i < root->num_children; i++) {
		node = root->children[i];

		if (node_is_visible (node)) {
			g_object_set (node->item,
				      "y", row_y,
				      "height", (double) row_height,
				      NULL);

			row_y += row_height;

			if (node->children != NULL) {
				row_y += chart_reflow_do (chart, node, row_y);
			}
		}
	}

	return row_y - start_y;
}

static gboolean
chart_reflow_idle (PlannerChart *chart)
{
	PlannerChartPriv *priv;
	mrptime                t1, t2;
	gdouble                x1, y1, x2, y2;
	gdouble                width, height;
	gdouble                bx1, bx2;
	GtkAllocation          allocation;

	priv = chart->priv;
	
	if (priv->height_changed || priv->height == -1) {
		height = chart_reflow_do (chart, priv->tree, 0);
		priv->height = height;
	} else {
		height = priv->height;
	}

	allocation = GTK_WIDGET (priv->canvas)->allocation;
	
	t1 = priv->project_start;
	t2 = priv->last_time;

	x1 = t1 * SCALE (priv->zoom) - PADDING;
	x2 = t2 * SCALE (priv->zoom) + PADDING;

	y2 = height;
	y1 = 0;

	width = MAX (x2 - x1, allocation.width - 1.0);
	height = MAX (y2 - y1, allocation.height - 1.0);

	/* Also make sure that everything actually fits horizontally.
	 * Note: this is the only thing that we use ::get_bounds for,
	 * so make sure to not implement that for anything that shouldn't
	 * expand the scroll region.
	 */
	gnome_canvas_item_get_bounds (priv->canvas->root,
				      &bx1, NULL,
				      &bx2, NULL);

	/* Put some padding after the right-most coordinate. */
	bx2 += PADDING;
		
	width = MAX (width, bx2 - bx1);

	x2 = x1 + width;
	
	chart_set_scroll_region (chart,
				       x1,
				       y1,
				       x2,
				       y1 + height);

	if (x1 > -1 && x2 > -1) {
		g_object_set (priv->header, 
			      "x1", x1,
			      "x2", x2,
			      NULL);
	}

	priv->height_changed = FALSE;
	priv->reflow_idle_id = 0;

	return FALSE;
}

static void
chart_reflow_now (PlannerChart *chart)
{
	if (!GTK_WIDGET_MAPPED (chart)) {
		return;
	}

	chart_reflow_idle (chart);
}

static void
chart_reflow (PlannerChart *chart, gboolean height_changed)
{
	if (!GTK_WIDGET_MAPPED (chart)) {
		return;
	}

	chart->priv->height_changed |= height_changed;

	if (chart->priv->reflow_idle_id != 0) {
		return;
	}
	
	chart->priv->reflow_idle_id = g_idle_add ((GSourceFunc) chart_reflow_idle, chart);
}

static TreeNode *
chart_insert_task (PlannerChart *chart,
			 GtkTreePath  *path,
			 MrpTask      *task)
{
	PlannerChartPriv *priv;
	GnomeCanvasItem       *item;
	TreeNode              *tree_node;

	priv = chart->priv;

	item = gnome_canvas_item_new (gnome_canvas_root (priv->canvas),
				      PLANNER_TYPE_CHART_ROW,
				      "task", task,
				      "scale", SCALE (priv->zoom),
				      "zoom", priv->zoom,
				      NULL);
	planner_chart_row_init_menu (PLANNER_CHART_ROW (item));
	
	tree_node = chart_tree_node_new ();
	tree_node->item = item;
	tree_node->task = task;

	chart_tree_node_insert_path (priv->tree, path, tree_node);

	g_signal_connect (task,
			  "relation-added",
			  G_CALLBACK (chart_relation_added),
			  chart);
	
	g_signal_connect (task,
			  "relation-removed",
			  G_CALLBACK (chart_relation_removed),
			  chart);

	g_signal_connect (task,
			  "removed",
			  G_CALLBACK (chart_task_removed),
			  chart);

	return tree_node;
}

static PlannerRelationArrow *
chart_add_relation (PlannerChart *chart,
			  TreeNode          *task,
			  TreeNode     	    *predecessor,
			  MrpRelationType    type)
{
	return planner_relation_arrow_new (PLANNER_CHART_ROW (task->item),
					   PLANNER_CHART_ROW (predecessor->item),
					   type);
}

static void
show_hide_descendants (TreeNode *node, gboolean show)
{
	gint i;

	for (i = 0; i < node->num_children; i++) {
		planner_chart_row_set_visible (PLANNER_CHART_ROW (node->children[i]->item),
					  show);

		if (!show || (show && node->children[i]->expanded)) {
			show_hide_descendants (node->children[i], show);
		}
	}
}

static void
collapse_descendants (TreeNode *node)
{
	gint i;

	for (i = 0; i < node->num_children; i++) {
		node->children[i]->expanded = FALSE;
		collapse_descendants (node->children[i]);
	}
}

GtkWidget *
planner_chart_new (void)
{

	return planner_chart_new_with_model (NULL);
}

GtkWidget *
planner_chart_new_with_model (GtkTreeModel *model)
{
	PlannerChart *chart;
	
	chart = PLANNER_CHART (gtk_type_new (planner_chart_get_type ()));

	if (model) {
		planner_chart_set_model (chart, model);
	}

	return GTK_WIDGET (chart);
}

void
planner_chart_expand_row (PlannerChart *chart, GtkTreePath *path)
{
	TreeNode *node;

	g_return_if_fail (PLANNER_IS_CHART (chart));

	node = chart_tree_node_at_path (chart->priv->tree, path);

	if (node) {
		node->expanded = TRUE;
		show_hide_descendants (node, TRUE);
		chart_reflow (chart, TRUE);
	}
}

void
planner_chart_collapse_row (PlannerChart *chart, GtkTreePath *path)
{
	TreeNode *node;

	g_return_if_fail (PLANNER_IS_CHART (chart));
	
	node = chart_tree_node_at_path (chart->priv->tree, path);

	if (node) {
		node->expanded = FALSE;
		collapse_descendants (node);
		show_hide_descendants (node, FALSE);
		chart_reflow (chart, TRUE);
	}
}

static void
chart_project_start_changed (MrpProject   *project,
				   GParamSpec   *spec,
				   PlannerChart *chart)
{
	mrptime t;

	t = mrp_project_get_project_start (project);
	chart->priv->project_start = t;

	g_object_set (chart->priv->background,
		      "project-start", t,
		      NULL);
	
	chart_reflow_now (chart);
}

static void
chart_root_finish_changed (MrpTask           *root,
				 GParamSpec        *spec,
				 PlannerChart *chart)
{
	chart->priv->last_time = mrp_task_get_finish (root);
	chart_reflow (chart, FALSE);
}

static void
chart_relation_added (MrpTask           *task,
			    MrpRelation       *relation,
			    PlannerChart *chart)
{
	GtkTreePath          *task_path;
	GtkTreePath          *predecessor_path;
	TreeNode             *task_node;
	TreeNode             *predecessor_node;
	PlannerRelationArrow *arrow;
	MrpTask              *predecessor;
	MrpRelationType	      rel_type;

	predecessor = mrp_relation_get_predecessor (relation);

	if (g_getenv ("PLANNER_DEBUG_UNDO_TASK")) {
		g_message ("Adding a new relation arrow (%p): %s (%p) -> %s (%p)", 
			   relation, 
			   mrp_task_get_name (predecessor), predecessor,
			   mrp_task_get_name (task), task);
	}
	
	if (task == predecessor) {
		/* We are only interested in the successor task. */
		return;
	}

	task_path = planner_chart_model_get_path_from_task (
		PLANNER_CHART_MODEL (chart->priv->model), task);
	predecessor_path = planner_chart_model_get_path_from_task (
		PLANNER_CHART_MODEL (chart->priv->model),
		predecessor);
	
	task_node = chart_tree_node_at_path (chart->priv->tree,
						   task_path);
	predecessor_node = chart_tree_node_at_path (chart->priv->tree,
							  predecessor_path);
	
	rel_type = mrp_relation_get_relation_type (relation);
	
	arrow = chart_add_relation (chart,
					  task_node,
					  predecessor_node,
					  rel_type);

	g_hash_table_insert (chart->priv->relation_hash, relation, arrow);
}

static void
chart_relation_removed (MrpTask           *task,
			      MrpRelation       *relation,
			      PlannerChart *chart)
{
	GnomeCanvasItem *arrow;
	MrpTask         *predecessor;

	predecessor = mrp_relation_get_predecessor (relation);
	
	if (task == predecessor) {
		/* We are only interested in the successor task. */
		return;
	}

	arrow = g_hash_table_lookup (chart->priv->relation_hash, relation);
	if (arrow != NULL) {
		g_hash_table_remove (chart->priv->relation_hash, relation);
		
		gtk_object_destroy (GTK_OBJECT (arrow));
		chart_reflow (chart, FALSE);
	}
}

static void
chart_task_removed (MrpTask            *task,
			  PlannerChart  *chart)
{
	GList *l, *relations;

	if (g_getenv ("PLANNER_DEBUG_UNDO_TASK")) {
		g_message ("Task removed: %s", mrp_task_get_name (task));
		g_message ("Cleaning signals for task: %s", mrp_task_get_name (task));
	}

	relations = mrp_task_get_predecessor_relations (task);
	for (l = relations; l; l = l->next) {
		chart_relation_removed (task, l->data, chart);
	}
	
	relations = mrp_task_get_successor_relations (task);
	for (l = relations; l; l = l->next) {
		chart_relation_removed (task, l->data, chart);
	} 
	
	g_signal_handlers_disconnect_by_func (task, chart_relation_added, chart);
	g_signal_handlers_disconnect_by_func (task, chart_relation_removed, chart);
	g_signal_handlers_disconnect_by_func (task, chart_task_removed, chart);
}

static gboolean
chart_task_moved_task_traverse_func (MrpTask *task, PlannerChart *chart)
{
	GList                 *relations;
	GList                 *l;
	MrpRelation           *relation;
	PlannerChartPriv *priv;
	PlannerRelationArrow  *arrow;
	PlannerChartRow       *row;

	priv = chart->priv;

	row = chart_get_row_from_task (chart, task);

	relations = mrp_task_get_predecessor_relations (task);
	for (l = relations; l; l = l->next) {
		relation = l->data;
		
		arrow = g_hash_table_lookup (priv->relation_hash, relation);
		if (arrow) {
			planner_relation_arrow_set_successor (arrow, row);
		}
	}
	
	relations = mrp_task_get_successor_relations (task);
	for (l = relations; l; l = l->next) {
		relation = l->data;
		
		arrow = g_hash_table_lookup (priv->relation_hash, relation);
		if (arrow) {
			planner_relation_arrow_set_predecessor (arrow, row);
		}
	}

	return FALSE;
}

static void
chart_task_moved (MrpProject        *project,
			MrpTask           *task,
			PlannerChart *chart)
{
	PlannerChartPriv *priv;

	priv = chart->priv;

	/* Note: Seems like we don't need this? */
	/*chart_reflow_now (chart);*/

	/* When a task has been indented or otherwise moved, it gets re-inserted
	 * in the new place. The relation arrow must be connected to the new
	 * canvas row items. So we check if any of the relations has an
	 * arrow item associated with it, and reconnects if so.
	 */
	mrp_project_task_traverse (project,
				   task,
				   (MrpTaskTraverseFunc) chart_task_moved_task_traverse_func,
				   chart);
}

static void
chart_add_signal (PlannerChart *chart, gpointer instance, gulong id)
{
	ConnectData *data;

	data = g_new0 (ConnectData, 1);

	data->instance = instance;
	data->id = id;
	
	chart->priv->signal_ids = g_list_prepend (chart->priv->signal_ids,
						  data);
}

static void
chart_disconnect_signals (PlannerChart *chart)
{
	GList       *l;
	ConnectData *data;

	for (l = chart->priv->signal_ids; l; l = l->next) {
		data = l->data;
		
		g_signal_handler_disconnect (data->instance,
					     data->id);
		g_free (data);
	}

	g_list_free (chart->priv->signal_ids);
	chart->priv->signal_ids = NULL;
}

PlannerTaskTree *
planner_chart_get_view (PlannerChart *chart)
{
	g_return_val_if_fail (PLANNER_IS_CHART (chart), NULL);
	
	return chart->priv->view;
}

void
planner_chart_set_view (PlannerChart *chart, PlannerTaskTree *view)
{
	g_return_if_fail (PLANNER_IS_TASK_TREE (view));
	
	chart->priv->view = view;
}

GtkTreeModel *
planner_chart_get_model (PlannerChart *chart)
{
	g_return_val_if_fail (PLANNER_IS_CHART (chart), NULL);

	return chart->priv->model;
}

void
planner_chart_set_model (PlannerChart *chart,
			       GtkTreeModel      *model)
{
	PlannerChartPriv *priv;
	MrpTask               *root;
	MrpProject            *project;
	gulong                 id;
	
	g_return_if_fail (PLANNER_IS_CHART (chart));

	priv = chart->priv;

	if (model == priv->model) {
		return;
	}

	if (priv->model) {
		chart_disconnect_signals (chart);
		g_object_unref (priv->model);
	}

	priv->model = model;

	if (model) {
		g_object_ref (model);

		chart_build_tree (chart);

		project = planner_chart_model_get_project (PLANNER_CHART_MODEL (model));
		root = mrp_project_get_root_task (project);

		g_object_set (priv->background, "project", project, NULL);
		
		id = g_signal_connect (project,
				       "notify::project-start",
				       G_CALLBACK (chart_project_start_changed),
				       chart);
		chart_add_signal (chart, project, id);

		g_signal_connect (root,
				  "notify::finish",
				  G_CALLBACK (chart_root_finish_changed),
				  chart);

		/* Connect with _after so that we get our event after the model
		 * is done.
		 */
		id = g_signal_connect_after (project,
					     "task-moved",
					     G_CALLBACK (chart_task_moved),
					     chart);
		chart_add_signal (chart, project, id);

		id = g_signal_connect (model,
				       "row-changed",
				       G_CALLBACK (chart_row_changed),
				       chart);
		chart_add_signal (chart, model, id);

		id = g_signal_connect (model,
				       "row-inserted",
				       G_CALLBACK (chart_row_inserted),
				       chart);
		chart_add_signal (chart, model, id);
		
		id = g_signal_connect (model,
				       "row-deleted",
				       G_CALLBACK (chart_row_deleted),
				       chart);
		chart_add_signal (chart, model, id);
		
		id = g_signal_connect (model,
				       "rows-reordered",
				       G_CALLBACK (chart_rows_reordered),
				       chart);
		chart_add_signal (chart, model, id);

		id = g_signal_connect (model,
				       "row-has-child-toggled",
				       G_CALLBACK (chart_row_has_child_toggled),
				       chart);
		chart_add_signal (chart, model, id);
		
		priv->project_start = mrp_project_get_project_start (project);

		g_object_set (priv->background,
			      "project-start", priv->project_start,
			      NULL);
	
		priv->last_time = mrp_task_get_finish (root);
		
		/* Force a reflow initially to avoid visible reflow on
		 * start-up .
		 */
		priv->height_changed = TRUE;
		chart_reflow_now (chart);
	}
	
	g_object_notify (G_OBJECT (chart), "model");
}

/* TreeNode */

static TreeNode *
chart_tree_node_new (void)
{
	TreeNode *node;
	
	node = g_new0 (TreeNode, 1);
	node->expanded = TRUE;

	return node;
}

static void
chart_tree_node_insert_path (TreeNode *node, GtkTreePath *path, TreeNode *new_node)
{
	gint *indices, i, depth;

	depth = gtk_tree_path_get_depth (path);
	indices = gtk_tree_path_get_indices (path);

	for (i = 0; i < depth - 1; i++) {
		node = node->children[indices[i]];
	}
	
	node->num_children++;
	node->children = g_realloc (node->children, sizeof (gpointer) * node->num_children);

	if (node->num_children - 1 == indices[i]) {
		/* Don't need to move if the new node is at the end. */
	} else {
		memmove (node->children + indices[i] + 1,
			 node->children + indices[i],
			 sizeof (gpointer) * (node->num_children - indices[i] - 1));
	}

	node->children[indices[i]] = new_node;

	new_node->parent = node;
}

static void
chart_tree_node_remove (PlannerChart *chart, TreeNode *node)
{
	TreeNode *parent;
	gint     i, pos;

	parent = node->parent;

	pos = -1;
	for (i = 0; i < parent->num_children; i++) {
		if (parent->children[i] == node) {
			pos = i;
			break;
		}
	}

	g_assert (pos != -1);

	memmove (parent->children + pos,
		 parent->children + pos + 1,
		 sizeof (gpointer) * (parent->num_children - pos - 1));

	parent->num_children--;
	parent->children = g_realloc (parent->children, sizeof (gpointer) * parent->num_children);

	if (g_getenv ("PLANNER_DEBUG_UNDO_TASK")) {
		g_message ("Cleaning signals for: %s", mrp_task_get_name (node->task));
	}

	g_signal_handlers_disconnect_by_func (node->task, chart_relation_added, chart);
	g_signal_handlers_disconnect_by_func (node->task, chart_relation_removed, chart);
	g_signal_handlers_disconnect_by_func (node->task, chart_task_removed, chart);
	
	node->parent = NULL;
}

static TreeNode *
chart_tree_node_at_path (TreeNode *node, GtkTreePath *path)
{
	gint *indices, i, depth;

	depth = gtk_tree_path_get_depth (path);
	indices = gtk_tree_path_get_indices (path);

	for (i = 0; i < depth; i++) {
		/* FIXME: workaround for a bug in the view, the
		 * row_expanded (and collapsed) should not be handled before we
		 * have inserted the rows.
		 */
		if (node->num_children <= indices[i]) {
			return NULL;
		}
		
		node = node->children[indices[i]];
	}
	
	return node;
}

void
planner_chart_scroll_to (PlannerChart *chart, time_t t)
{
	/*gint x1, x2;*/
	
	g_return_if_fail (PLANNER_IS_CHART (chart));

	/* FIXME: add range check. */

	/* FIXME: "port" to mrptime. */
#if 0
	x1 = chart->priv->project_start * chart->priv->hscale;
	x2 = t * chart->priv->hscale;

	gnome_canvas_scroll_to (chart->priv->canvas, x2 - x1, 0);
#endif	
}
	
static void
chart_tree_traverse (TreeNode *node, TreeFunc func, gpointer data)
{
	gint      i;
	TreeNode *child;

	for (i = 0; i < node->num_children; i++) {
		child = node->children[i];

		chart_tree_traverse (child, func, data);
	}

	func (node, data);
}

static void
chart_set_scroll_region (PlannerChart *chart,
			       gdouble            x1,
			       gdouble            y1,
			       gdouble            x2,
			       gdouble            y2)
{
	GnomeCanvas *canvas;
	gdouble      ox1, oy1, ox2, oy2;
	
	canvas = chart->priv->canvas;

	gnome_canvas_get_scroll_region (canvas,
					&ox1,
					&oy1,
					&ox2,
					&oy2);
	
	if (ox1 == x1 && oy1 == y1 && ox2 == x2 && oy2 == y2) {
		return;
	}

	gnome_canvas_set_scroll_region (canvas,
					x1,
					y1,
					x2,
					y2);
}

static PlannerChartRow *
chart_get_row_from_task (PlannerChart *chart,
			       MrpTask           *task)
{
	PlannerChartModel *model;
	GtkTreePath  *path;
	TreeNode     *node;

	model = PLANNER_CHART_MODEL (chart->priv->model);
	
	path = planner_chart_model_get_path_from_task (model, task);

	node = chart_tree_node_at_path (chart->priv->tree, path);

	gtk_tree_path_free (path);

	return PLANNER_CHART_ROW (node->item);
}

static void
scale_func (TreeNode *node, gpointer data)
{
	PlannerChart     *chart = PLANNER_CHART (data);
	PlannerChartPriv *priv = chart->priv;

	if (node->item) {
		gnome_canvas_item_set (GNOME_CANVAS_ITEM (node->item),
				       "scale", SCALE (priv->zoom),
				       "zoom", priv->zoom,
				       NULL);
	}
}

static void
chart_set_zoom (PlannerChart *chart, gdouble zoom)
{
	PlannerChartPriv *priv;

	priv = chart->priv;

	priv->zoom = zoom;

	chart_tree_traverse (priv->tree, scale_func, chart);

	g_object_set (priv->header,
		      "scale", SCALE (priv->zoom),
		      "zoom", priv->zoom,
		      NULL);
	gnome_canvas_item_set (GNOME_CANVAS_ITEM (priv->background),
			       "scale", SCALE (priv->zoom),
			       "zoom", priv->zoom,
			       NULL);

	chart_reflow_now (chart);
}

static mrptime 
chart_get_center (PlannerChart *chart)
{
	PlannerChartPriv *priv;
	gint                   x1, width, x;
	
	priv = chart->priv;

	gnome_canvas_get_scroll_offsets (priv->canvas, &x1, NULL);
	width = GTK_WIDGET (priv->canvas)->allocation.width;

	x = x1 + width / 2 - PADDING;

	x += floor (priv->project_start * SCALE (priv->zoom) + 0.5);

	return floor (x / SCALE (priv->zoom) + 0.5);
}

static void 
chart_set_center (PlannerChart *chart, mrptime t)
{
	PlannerChartPriv *priv;
	gint                   x, x1, width;
	
	priv = chart->priv;

	x = floor (t * SCALE (priv->zoom) + 0.5);

	width = GTK_WIDGET (priv->canvas)->allocation.width;

	x1 = x - width / 2 + PADDING;

	x1 -= floor (priv->project_start * SCALE (priv->zoom) + 0.5);

	gnome_canvas_scroll_to (chart->priv->canvas, x1, 0);
}

void
planner_chart_zoom_in (PlannerChart *chart)
{
	PlannerChartPriv *priv;
	mrptime                mt;

	g_return_if_fail (PLANNER_IS_CHART (chart));

	priv = chart->priv;

	mt = chart_get_center (chart);
	chart_set_zoom (chart, priv->zoom + 1);
	chart_set_center (chart, mt);
}

void
planner_chart_zoom_out (PlannerChart *chart)
{
	PlannerChartPriv *priv;
	mrptime                mt;

	g_return_if_fail (PLANNER_IS_CHART (chart));

	priv = chart->priv;

	mt = chart_get_center (chart);
	chart_set_zoom (chart, priv->zoom - 1);
	chart_set_center (chart, mt);
}

void
planner_chart_can_zoom (PlannerChart *chart,
			 gboolean     *in,
			 gboolean     *out)
{
	PlannerChartPriv *priv;

	g_return_if_fail (PLANNER_IS_CHART (chart));

	priv = chart->priv;
	
	if (in) {
		*in = (priv->zoom < ZOOM_IN_LIMIT);
	}
	
	if (out) {
		*out = (priv->zoom > ZOOM_OUT_LIMIT);
	}
}

void
planner_chart_zoom_to_fit (PlannerChart *chart)
{
	PlannerChartPriv *priv;
	gdouble                t;
	gdouble                zoom;
	gdouble                alloc;

	g_return_if_fail (PLANNER_IS_CHART (chart));

	priv = chart->priv;

	t = chart_get_width (chart);
	if (t == -1) {
		return;
	}
	
	alloc = GTK_WIDGET (chart)->allocation.width - PADDING * 2;

	zoom = planner_scale_clamp_zoom (ZOOM (alloc / t));
	chart_set_zoom (chart, zoom);
}

gdouble
planner_chart_get_zoom (PlannerChart  *chart)
{
	PlannerChartPriv *priv;

	g_return_val_if_fail (PLANNER_IS_CHART (chart), 0);

	priv = chart->priv;

	return priv->zoom; 
}

static gint
chart_get_width (PlannerChart *chart)
{
	PlannerChartPriv *priv;

	priv = chart->priv;

	if (priv->project_start == MRP_TIME_INVALID ||
	    priv->last_time == MRP_TIME_INVALID) {
		return -1;
	}

	return priv->last_time - priv->project_start;
}

void
planner_chart_status_updated (PlannerChart *chart,
				    const gchar       *message)
{
	g_return_if_fail (PLANNER_IS_CHART (chart));

	g_signal_emit (chart, signals[STATUS_UPDATED], 0, message);
}

void
planner_chart_resource_clicked (PlannerChart *chart,
				      MrpResource       *resource)
{
	g_return_if_fail (PLANNER_IS_CHART (chart));

	g_signal_emit (chart, signals[RESOURCE_CLICKED], 0, resource);
}

void
planner_chart_set_highlight_critical_tasks (PlannerChart *chart,
						  gboolean           state)
{
	PlannerChartPriv *priv;
	GConfClient           *gconf_client;
	
	g_return_if_fail (PLANNER_IS_CHART (chart));

	priv = chart->priv;

	if (priv->highlight_critical == state) {
		return;
	}
	
	priv->highlight_critical = state;
	
	gtk_widget_queue_draw (GTK_WIDGET (priv->canvas));

	gconf_client = planner_application_get_gconf_client ();
	gconf_client_set_bool (gconf_client,
			       CRITICAL_PATH_KEY,
			       state,
			       NULL);
}

gboolean
planner_chart_get_highlight_critical_tasks (PlannerChart *chart)
{
	g_return_val_if_fail (PLANNER_IS_CHART (chart), FALSE);

	return chart->priv->highlight_critical;
}
