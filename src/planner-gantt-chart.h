/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2001-2002 CodeFactory AB
 * Copyright (C) 2001-2002 Richard Hult <richard@imendio.com>
 * Copyright (C) 2001-2002 Mikael Hallendal <micke@imendio.com>
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

#ifndef __PLANNER_CHART_H__
#define __PLANNER_CHART_H__

#include <gtk/gtkwidget.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtktreemodel.h>
#include <libgnomecanvas/gnome-canvas.h>
#include "planner-task-tree.h"

#define PLANNER_TYPE_CHART		(planner_chart_get_type ())
#define PLANNER_CHART(obj)		(GTK_CHECK_CAST ((obj), PLANNER_TYPE_CHART, PlannerChart))
#define PLANNER_CHART_CLASS(klass)	(GTK_CHECK_CLASS_CAST ((klass), PLANNER_TYPE_CHART, PlannerChartClass))
#define PLANNER_IS_CHART(obj)		(GTK_CHECK_TYPE ((obj), PLANNER_TYPE_CHART))
#define PLANNER_IS_CHART_CLASS(klass)	(GTK_CHECK_CLASS_TYPE ((obj), PLANNER_TYPE_CHART))
#define PLANNER_CHART_GET_CLASS(obj)	(GTK_CHECK_GET_CLASS ((obj), PLANNER_TYPE_CHART, PlannerChartClass))

typedef struct _PlannerChart           PlannerChart;
typedef struct _PlannerChartClass      PlannerChartClass;
typedef struct _PlannerChartPriv       PlannerChartPriv;

struct _PlannerChart
{
	GtkVBox           parent;
	PlannerChartPriv *priv;
};

struct _PlannerChartClass
{
	GtkVBoxClass parent_class;

	void  (* set_scroll_adjustments) (PlannerChart  *chart,
					  GtkAdjustment *hadjustment,
					  GtkAdjustment *vadjustment);
};


GType            planner_chart_get_type         (void) G_GNUC_CONST;

GtkWidget       *planner_chart_new              (void);

GtkWidget       *planner_chart_new_with_model   (GtkTreeModel  *model);

PlannerTaskTree *planner_chart_get_view         (PlannerChart *chart);

void planner_chart_set_view                     (PlannerChart *chart,
						       PlannerTaskTree *view);

GtkTreeModel    *planner_chart_get_model        (PlannerChart  *tree_view);

void             planner_chart_set_model        (PlannerChart  *tree_view,
						       GtkTreeModel  *model);

void             planner_chart_expand_row       (PlannerChart  *chart,
						       GtkTreePath   *path);

void             planner_chart_collapse_row     (PlannerChart  *chart,
						       GtkTreePath   *path);

void             planner_chart_scroll_to        (PlannerChart  *chart,
						       time_t         t);

void             planner_chart_zoom_in          (PlannerChart  *chart);

void             planner_chart_zoom_out         (PlannerChart  *chart);

void             planner_chart_zoom_to_fit      (PlannerChart  *chart);

gdouble          planner_chart_get_zoom         (PlannerChart  *chart);

void             planner_chart_can_zoom         (PlannerChart  *chart,
						       gboolean      *in,
						       gboolean      *out);

void             planner_chart_status_updated   (PlannerChart  *chart,
						       const gchar   *message);

void             planner_chart_resource_clicked (PlannerChart  *chart,
						       MrpResource   *resource);

void
planner_chart_set_highlight_critical_tasks      (PlannerChart  *chart,
						       gboolean       state);

gboolean
planner_chart_get_highlight_critical_tasks      (PlannerChart  *chart);


#endif /* __PLANNER_CHART_H__ */
