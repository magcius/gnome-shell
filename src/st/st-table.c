/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * st-table.c: Table layout widget
 *
 * Copyright 2008, 2009 Intel Corporation.
 * Copyright 2009, 2010 Red Hat, Inc.
 * Copyright 2009 Abderrahim Kitouni
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:st-table
 * @short_description: A multi-child layout container based on rows
 * and columns
 *
 * #StTable is a multi-child layout container based on a table arrangement
 * with rows and columns. #StTable adds several child properties to it's
 * children that control their position and size in the table.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "st-table.h"

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <clutter/clutter.h>

#include "st-enum-types.h"
#include "st-marshal.h"
#include "st-private.h"
#include "st-table-child.h"
#include "st-table-private.h"

enum
{
  PROP_0,

  PROP_HOMOGENEOUS,

  PROP_ROW_COUNT,
  PROP_COL_COUNT,
};

#define ST_TABLE_GET_PRIVATE(obj)    \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), ST_TYPE_TABLE, StTablePrivate))

typedef struct
{
  guint expand : 1;
  guint shrink : 1;
  guint is_visible : 1;

  gfloat min_size;
  gfloat pref_size;
  gfloat final_size;

} DimensionData;

struct _StTablePrivate
{
  gint    col_spacing;
  gint    row_spacing;

  gint    visible_rows;
  gint    visible_cols;

  gint    n_rows;
  gint    n_cols;

  gint    active_row;
  gint    active_col;

  GArray *columns;
  GArray *rows;

  guint   homogeneous : 1;
};

static void st_table_container_iface_init (ClutterContainerIface *iface);

G_DEFINE_TYPE_WITH_CODE (StTable, st_table, ST_TYPE_CONTAINER,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_CONTAINER,
                                                st_table_container_iface_init));



/*
 * ClutterContainer Implementation
 */
static void
st_table_actor_removed (ClutterContainer *container,
                        ClutterActor     *actor)
{
  StTablePrivate *priv = ST_TABLE (container)->priv;
  GList *list, *children;
  gint n_rows = 0;
  gint n_cols = 0;

  /* Calculate and update the number of rows / columns */
  children = st_container_get_children_list (ST_CONTAINER (container));
  for (list = children; list; list = list->next)
    {
      ClutterActor *child = CLUTTER_ACTOR (list->data);
      StTableChild *meta;

      if (child == actor)
          continue;

      meta = (StTableChild *) clutter_container_get_child_meta (container, child);
      n_rows = MAX (n_rows, meta->row + 1);
      n_cols = MAX (n_cols, meta->col + 1);
    }

  g_object_freeze_notify (G_OBJECT (container));

  if (priv->n_rows != n_rows)
    {
      priv->n_rows = n_rows;
      g_object_notify (G_OBJECT (container), "row-count");
    }

  if (priv->n_cols != n_cols)
    {
      priv->n_cols = n_cols;
      g_object_notify (G_OBJECT (container), "column-count");
    }

  g_object_thaw_notify (G_OBJECT (container));
}

static void
st_table_container_iface_init (ClutterContainerIface *iface)
{
  iface->actor_removed = st_table_actor_removed;
  iface->child_meta_type = ST_TYPE_TABLE_CHILD;
}

/* StTable Class Implementation */

static void
st_table_set_property (GObject      *gobject,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
  StTable *table = ST_TABLE (gobject);

  switch (prop_id)
    {
    case PROP_HOMOGENEOUS:
      if (table->priv->homogeneous != g_value_get_boolean (value))
        {
          table->priv->homogeneous = g_value_get_boolean (value);
          clutter_actor_queue_relayout ((ClutterActor *) gobject);
        }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
st_table_get_property (GObject    *gobject,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
  StTablePrivate *priv = ST_TABLE (gobject)->priv;

  switch (prop_id)
    {
    case PROP_HOMOGENEOUS:
      g_value_set_boolean (value, priv->homogeneous);
      break;

    case PROP_COL_COUNT:
      g_value_set_int (value, priv->n_cols);
      break;

    case PROP_ROW_COUNT:
      g_value_set_int (value, priv->n_rows);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
st_table_finalize (GObject *gobject)
{
  StTablePrivate *priv = ST_TABLE (gobject)->priv;

  g_array_free (priv->columns, TRUE);
  g_array_free (priv->rows, TRUE);

  G_OBJECT_CLASS (st_table_parent_class)->finalize (gobject);
}

static void
st_table_homogeneous_allocate (ClutterActor          *self,
                               const ClutterActorBox *content_box,
                               gboolean               flags)
{
  GList *list, *children;
  gfloat col_width, row_height;
  gint row_spacing, col_spacing;
  StTablePrivate *priv = ST_TABLE (self)->priv;
  gboolean ltr = st_widget_get_direction (ST_WIDGET (self)) == ST_TEXT_DIRECTION_LTR;

  col_spacing = priv->col_spacing;
  row_spacing = priv->row_spacing;

  col_width = (int) ((content_box->x2 - content_box->x1
                      - (col_spacing * (priv->n_cols - 1)))
                     / priv->n_cols + 0.5);
  row_height = (int) ((content_box->y2 - content_box->y1
                      - (row_spacing * (priv->n_rows - 1)))
                      / priv->n_rows + 0.5);

  children = st_container_get_children_list (ST_CONTAINER (self));
  for (list = children; list; list = list->next)
    {
      gint row, col, row_span, col_span;
      StTableChild *meta;
      ClutterActor *child;
      ClutterActorBox childbox;
      StAlign x_align, y_align;
      gboolean x_fill, y_fill;

      child = CLUTTER_ACTOR (list->data);

      meta = (StTableChild *) clutter_container_get_child_meta (CLUTTER_CONTAINER (self), child);

      if (!meta->allocate_hidden && !CLUTTER_ACTOR_IS_VISIBLE (child))
        continue;

      /* get child properties */
      col = meta->col;
      row = meta->row;
      row_span = meta->row_span;
      col_span = meta->col_span;
      x_align = meta->x_align;
      y_align = meta->y_align;
      x_fill = meta->x_fill;
      y_fill = meta->y_fill;

      if (ltr)
        {
          childbox.x1 = content_box->x1 + (col_width + col_spacing) * col;
          childbox.x2 = childbox.x1 + (col_width * col_span) + (col_spacing * (col_span - 1));
        }
      else
        {
          childbox.x2 = content_box->x2 - (col_width + col_spacing) * col;
          childbox.x1 = childbox.x2 - (col_width * col_span) - (col_spacing * (col_span - 1));
        }

      childbox.y1 = content_box->y1 + (row_height + row_spacing) * row;
      childbox.y2 = childbox.y1 + (row_height * row_span) + (row_spacing * (row_span - 1));

      _st_allocate_fill (ST_WIDGET (self), child, &childbox,
                         x_align, y_align, x_fill, y_fill);

      clutter_actor_allocate (child, &childbox, flags);
    }

}


static void
st_table_calculate_col_widths (StTable *table,
                               gint     for_width)
{
  StTablePrivate *priv = table->priv;
  GList *list, *children;
  gint i;
  DimensionData *columns;

  g_array_set_size (priv->columns, 0);
  g_array_set_size (priv->columns, priv->n_cols);
  columns = &g_array_index (priv->columns, DimensionData, 0);


  /* Reset all the visible attributes for the columns */
  priv->visible_cols = 0;
  for (i = 0; i < priv->n_cols; i++)
    columns[i].is_visible = FALSE;

  children = st_container_get_children_list (ST_CONTAINER (table));

  /* STAGE ONE: calculate column widths for non-spanned children */
  for (list = children; list; list = list->next)
    {
      StTableChild *meta;
      ClutterActor *child;
      DimensionData *col;
      gfloat w_min, w_pref;

      child = CLUTTER_ACTOR (list->data);

      meta = (StTableChild *) clutter_container_get_child_meta (CLUTTER_CONTAINER (table), child);

      if (!meta->allocate_hidden && !CLUTTER_ACTOR_IS_VISIBLE (child))
        continue;

      if (meta->col_span > 1)
        continue;

      col = &columns[meta->col];

      /* If this child is visible, then its column is visible */
      if (!col->is_visible)
        {
          col->is_visible = TRUE;
          priv->visible_cols++;
        }

      _st_actor_get_preferred_width (child, -1, meta->y_fill, &w_min, &w_pref);

      col->min_size = MAX (col->min_size, w_min);
      col->final_size = col->pref_size = MAX (col->pref_size, w_pref);
      col->expand = MAX (col->expand, meta->x_expand);
    }

  /* STAGE TWO: take spanning children into account */
  for (list = children; list; list = list->next)
    {
      StTableChild *meta;
      ClutterActor *child;
      gfloat w_min, w_pref;
      gfloat min_width, pref_width;
      gint start_col, end_col;
      gint n_expand;

      child = CLUTTER_ACTOR (list->data);

      meta = (StTableChild *) clutter_container_get_child_meta (CLUTTER_CONTAINER (table), child);

      if (!meta->allocate_hidden && !CLUTTER_ACTOR_IS_VISIBLE (child))
        continue;

      if (meta->col_span < 2)
        continue;

      start_col = meta->col;
      end_col = meta->col + meta->col_span - 1;

      _st_actor_get_preferred_width (child, -1, meta->y_fill, &w_min, &w_pref);


      /* check there is enough room for this actor */
      min_width = 0;
      pref_width = 0;
      n_expand = 0;
      for (i = start_col; i <= end_col; i++)
        {
          min_width += columns[i].min_size;
          pref_width += columns[i].pref_size;

          if (columns[i].expand)
            {
              n_expand++;
            }

          /* If this child is visible, then the columns it spans
             are also visible */
          if (!columns[i].is_visible)
            {
              columns[i].is_visible = TRUE;
              priv->visible_cols++;
            }

          columns[i].expand = MAX (columns[i].expand, meta->x_expand);
        }
      min_width += priv->col_spacing * (meta->col_span - 1);
      pref_width += priv->col_spacing * (meta->col_span - 1);


      /* see st_table_calculate_row_heights() for comments */
      /* (1) */
      if (w_min > min_width)
        {

          /* (2) */
          /* we can start from preferred width and decrease */
          if (pref_width > w_min)
            {
              for (i = start_col; i <= end_col; i++)
                {
                  columns[i].final_size = columns[i].pref_size;
                }

              while (pref_width > w_min)
                {
                  for (i = start_col; i <= end_col; i++)
                    {
                      if (columns[i].final_size > columns[i].min_size)
                        {
                          columns[i].final_size--;
                          pref_width--;
                        }
                    }
                }
              for (i = start_col; i <= end_col; i++)
                {
                  columns[i].min_size = columns[i].final_size;
                }

            }
          else
            {
              /* (3) */
              /* we can expand from preferred size */
              gfloat expand_by;

              expand_by = w_pref - pref_width;

              for (i = start_col; i <= end_col; i++)
                {
                  if (n_expand)
                    {
                      if (columns[i].expand)
                        columns[i].min_size =
                          columns[i].pref_size + expand_by / n_expand;
                    }
                  else
                    {
                      columns[i].min_size =
                        columns[i].pref_size + expand_by / meta->col_span;
                    }

                }
            }
        }


    }


  /* calculate final widths */
  if (for_width >= 0)
    {
      gfloat min_width, pref_width;
      gint n_expand;

      min_width = 0;
      pref_width = 0;
      n_expand = 0;
      for (i = 0; i < priv->n_cols; i++)
        {
          pref_width += columns[i].pref_size;
          min_width += columns[i].min_size;
          if (columns[i].expand)
            n_expand++;
        }
      pref_width += priv->col_spacing * (priv->n_cols - 1);
      min_width += priv->col_spacing * (priv->n_cols - 1);

      if (for_width <= min_width)
        {
          /* erk, we can't shrink this! */
          for (i = 0; i < priv->n_cols; i++)
            {
              columns[i].final_size = columns[i].min_size;
            }
          return;
        }

      if (for_width == pref_width)
        {
          /* perfect! */
          for (i = 0; i < priv->n_cols; i++)
            {
              columns[i].final_size = columns[i].pref_size;
            }
          return;
        }

      /* for_width is between min_width and pref_width */
      if (for_width < pref_width && for_width > min_width)
        {
          gfloat width;

          /* shrink columns until they reach min_width */

          /* start with all columns at preferred size */
          for (i = 0; i < priv->n_cols; i++)
            {
              columns[i].final_size = columns[i].pref_size;
            }
          width = pref_width;

          while (width > for_width)
            {
              for (i = 0; i < priv->n_cols; i++)
                {
                  if (columns[i].final_size > columns[i].min_size)
                    {
                      columns[i].final_size--;
                      width--;
                    }
                }
            }

          return;
        }

      /* expand columns */
      if (for_width > pref_width)
        {
          gfloat extra_width = for_width - pref_width;
          gint remaining;

          if (n_expand)
            remaining = (gint) extra_width % n_expand;
          else
            remaining = (gint) extra_width % priv->n_cols;

          for (i = 0; i < priv->n_cols; i++)
            {
              if (columns[i].expand)
                {
                  if (n_expand)
                    {
                      columns[i].final_size =
                        columns[i].pref_size + (extra_width / n_expand);
                    }
                  else
                    {
                      columns[i].final_size =
                        columns[i].pref_size + (extra_width / priv->n_cols);
                    }
                }
              else
                columns[i].final_size = columns[i].pref_size;
            }

          /* distribute the remainder among children */
          i = 0;
          while (remaining)
            {
              columns[i].final_size++;
              i++;
              remaining--;
            }
        }
    }

}

static void
st_table_calculate_row_heights (StTable *table,
                                gint     for_height)
{
  StTablePrivate *priv = ST_TABLE (table)->priv;
  GList *list, *children;
  gint i;
  DimensionData *rows, *columns;

  g_array_set_size (priv->rows, 0);
  g_array_set_size (priv->rows, priv->n_rows);
  rows = &g_array_index (priv->rows, DimensionData, 0);

  columns = &g_array_index (priv->columns, DimensionData, 0);

  /* Reset the visible rows */
  priv->visible_rows = 0;
  for (i = 0; i < priv->n_rows; i++)
    rows[i].is_visible = FALSE;

  /* STAGE ONE: calculate row heights for non-spanned children */
  children = st_container_get_children_list (ST_CONTAINER (table));
  for (list = children; list; list = list->next)
    {
      StTableChild *meta;
      ClutterActor *child;
      DimensionData *row;
      gfloat c_min, c_pref;

      child = CLUTTER_ACTOR (list->data);

      meta = (StTableChild *)
        clutter_container_get_child_meta (CLUTTER_CONTAINER (table), child);

      if (!meta->allocate_hidden && !CLUTTER_ACTOR_IS_VISIBLE (child))
        continue;

      if (meta->row_span > 1)
        continue;

      row = &rows[meta->row];

      /* If this child is visible, then its row is visible */
      if (!row->is_visible)
        {
          row->is_visible = TRUE;
          priv->visible_rows++;
        }

      _st_actor_get_preferred_height (child, columns[meta->col].final_size,
                                      meta->x_fill, &c_min, &c_pref);

      row->min_size = MAX (row->min_size, c_min);
      row->final_size = row->pref_size = MAX (row->pref_size, c_pref);
      row->expand = MAX (row->expand, meta->y_expand);
    }



  /* STAGE TWO: take spanning children into account */
  for (list = children; list; list = list->next)
    {
      StTableChild *meta;
      ClutterActor *child;
      gfloat c_min, c_pref;
      gfloat min_height, pref_height;
      gint start_row, end_row;
      gint n_expand;

      child = CLUTTER_ACTOR (list->data);

      meta = (StTableChild *)
        clutter_container_get_child_meta (CLUTTER_CONTAINER (table), child);

      if (!meta->allocate_hidden && !CLUTTER_ACTOR_IS_VISIBLE (child))
        continue;

      if (meta->row_span < 2)
        continue;

      start_row = meta->row;
      end_row = meta->row + meta->row_span - 1;

      _st_actor_get_preferred_height (child, columns[meta->col].final_size,
                                      meta->x_fill, &c_min, &c_pref);


      /* check there is enough room for this actor */
      min_height = 0;
      pref_height = 0;
      n_expand = 0;
      for (i = start_row; i <= end_row; i++)
        {
          min_height += rows[i].min_size;
          pref_height += rows[i].pref_size;

          if (rows[i].expand)
            {
              n_expand++;
            }

          /* If this actor is visible, then all the rows is spans are visible */
          if (!rows[i].is_visible)
            {
              rows[i].is_visible = TRUE;
              priv->visible_rows++;
            }
          rows[i].expand = MAX (rows[i].expand, meta->y_expand);
        }
      min_height += priv->row_spacing * (meta->row_span - 1);
      pref_height += priv->row_spacing * (meta->row_span - 1);

      /* 1) If the minimum height of the rows spanned is less than the minimum
       * height of the child that is spanning them, then we must increase the
       * minimum height of the rows spanned.
       *
       * 2) If the preferred height of the spanned rows is more that the minimum
       * height of the spanning child, then we can start at this size and
       * decrease each row evenly.
       *
       * 3) If the preferred height of the rows is more than the minimum height
       * of the spanned child, then we can start at the preferred height and
       * expand.
       */
      /* (1) */
      if (c_min > min_height)
        {

          /* (2) */
          /* we can start from preferred height and decrease */
          if (pref_height > c_min)
            {
              for (i = start_row; i <= end_row; i++)
                {
                  rows[i].final_size = rows[i].pref_size;
                }

              while (pref_height > c_min)
                {
                  for (i = start_row; i <= end_row; i++)
                    {
                      if (rows[i].final_size > rows[i].min_size)
                        {
                          rows[i].final_size--;
                          pref_height--;
                        }
                    }
                }
              for (i = start_row; i <= end_row; i++)
                {
                  rows[i].min_size = rows[i].final_size;
                }

            }
          else
            {
              /* (3) */
              /* we can expand from preferred size */
              gfloat expand_by;

              expand_by = c_pref - pref_height;

              for (i = start_row; i <= end_row; i++)
                {
                  if (n_expand)
                    {
                      if (rows[i].expand)
                        rows[i].min_size =
                          rows[i].pref_size + expand_by / n_expand;
                    }
                  else
                    {
                      rows[i].min_size =
                        rows[i].pref_size + expand_by / meta->row_span;
                    }

                }
            }
        }

    }


  /* calculate final heights */
  if (for_height >= 0)
    {
      gfloat min_height, pref_height;
      gint n_expand;

      min_height = 0;
      pref_height = 0;
      n_expand = 0;
      for (i = 0; i < priv->n_rows; i++)
        {
          pref_height += rows[i].pref_size;
          min_height += rows[i].min_size;
          if (rows[i].expand)
            n_expand++;
        }
      pref_height += priv->row_spacing * (priv->n_rows - 1);
      min_height += priv->row_spacing * (priv->n_rows - 1);

      if (for_height <= min_height)
        {
          /* erk, we can't shrink this! */
          for (i = 0; i < priv->n_rows; i++)
            {
              rows[i].final_size = rows[i].min_size;
            }
          return;
        }

      if (for_height == pref_height)
        {
          /* perfect! */
          for (i = 0; i < priv->n_rows; i++)
            {
              rows[i].final_size = rows[i].pref_size;
            }
          return;
        }

      /* for_height is between min_height and pref_height */
      if (for_height < pref_height && for_height > min_height)
        {
          gfloat height;

          /* shrink rows until they reach min_height */

          /* start with all rows at preferred size */
          for (i = 0; i < priv->n_rows; i++)
            {
              rows[i].final_size = rows[i].pref_size;
            }
          height = pref_height;

          while (height > for_height)
            {
              for (i = 0; i < priv->n_rows; i++)
                {
                  if (rows[i].final_size > rows[i].min_size)
                    {
                      rows[i].final_size--;
                      height--;
                    }
                }
            }

          return;
        }

      /* expand rows */
      if (for_height > pref_height)
        {
          gfloat extra_height = for_height - pref_height;
          gint remaining;

          if (n_expand)
            remaining = (gint) extra_height % n_expand;
          else
            remaining = (gint) extra_height % priv->n_rows;

          for (i = 0; i < priv->n_rows; i++)
            {
              if (rows[i].expand)
                {
                  if (n_expand)
                    {
                      rows[i].final_size =
                        rows[i].pref_size + (extra_height / n_expand);
                    }
                  else
                    {
                      rows[i].final_size =
                        rows[i].pref_size + (extra_height / priv->n_rows);
                    }
                }
              else
                rows[i].final_size = rows[i].pref_size;
            }

          /* distribute the remainder among children */
          i = 0;
          while (remaining)
            {
              rows[i].final_size++;
              i++;
              remaining--;
            }
        }
    }

}

static void
st_table_calculate_dimensions (StTable *table,
                               gfloat for_width,
                               gfloat for_height)
{
  st_table_calculate_col_widths (table, for_width);
  st_table_calculate_row_heights (table, for_height);
}

static void
st_table_preferred_allocate (ClutterActor          *self,
                             const ClutterActorBox *content_box,
                             gboolean               flags)
{
  GList *list, *children;
  gint row_spacing, col_spacing;
  gint i;
  StTable *table;
  StTablePrivate *priv;
  gboolean ltr;
  DimensionData *rows, *columns;

  table = ST_TABLE (self);
  priv = ST_TABLE (self)->priv;

  col_spacing = (priv->col_spacing);
  row_spacing = (priv->row_spacing);

  st_table_calculate_dimensions (table,
                                 content_box->x2 - content_box->x1,
                                 content_box->y2 - content_box->y1);

  ltr = (st_widget_get_direction (ST_WIDGET (self)) == ST_TEXT_DIRECTION_LTR);
  rows = &g_array_index (priv->rows, DimensionData, 0);
  columns = &g_array_index (priv->columns, DimensionData, 0);

  children = st_container_get_children_list (ST_CONTAINER (self));
  for (list = children; list; list = list->next)
    {
      gint row, col, row_span, col_span;
      gint col_width, row_height;
      StTableChild *meta;
      ClutterActor *child;
      ClutterActorBox childbox;
      gint child_x, child_y;
      StAlign x_align, y_align;
      gboolean x_fill, y_fill;

      child = CLUTTER_ACTOR (list->data);

      meta = (StTableChild *) clutter_container_get_child_meta (CLUTTER_CONTAINER (self), child);

      if (!meta->allocate_hidden && !CLUTTER_ACTOR_IS_VISIBLE (child))
        continue;

      /* get child properties */
      col = meta->col;
      row = meta->row;
      row_span = meta->row_span;
      col_span = meta->col_span;
      x_align = meta->x_align;
      y_align = meta->y_align;
      x_fill = meta->x_fill;
      y_fill = meta->y_fill;

      /* initialise the width and height */
      col_width = columns[col].final_size;
      row_height = rows[row].final_size;

      /* Add the widths of the spanned columns:
       *
       * First check that we have a non-zero span. Then we loop over each of
       * the columns that we're spanning but we stop short if we go past the
       * number of columns in the table. This is necessary to avoid accessing
       * uninitialised memory. We add the spacing in here too since we only
       * want to add as much spacing as times we successfully span.
       */
      if (col + col_span > priv->n_cols)
        g_warning ("StTable: the child at %d,%d's col-span, %d, exceeds number of columns, %d",
                   col, row, col_span, priv->n_cols);
      if (row + row_span > priv->n_rows)
        g_warning ("StTable: the child at %d,%d's row-span, %d, exceeds number of rows, %d",
                   col, row, row_span, priv->n_rows);

      if (col_span > 1)
        {
          for (i = col + 1; i < col + col_span && i < priv->n_cols; i++)
            {
              col_width += columns[i].final_size;
              col_width += col_spacing;
            }
        }

      /* add the height of the spanned rows */
      if (row_span > 1)
        {
          for (i = row + 1; i < row + row_span && i < priv->n_rows; i++)
            {
              row_height += rows[i].final_size;
              row_height += row_spacing;
            }
        }

      /* calculate child x */
      if (ltr)
        {
          child_x = (int) content_box->x1
                    + col_spacing * col;
          for (i = 0; i < col; i++)
            child_x += columns[i].final_size;
        }
      else
        {
          child_x = (int) content_box->x2
                    - col_spacing * col;
          for (i = 0; i < col; i++)
            child_x -= columns[i].final_size;
        }

      /* calculate child y */
      child_y = (int) content_box->y1
                + row_spacing * row;
      for (i = 0; i < row; i++)
        child_y += rows[i].final_size;

      /* set up childbox */
      if (ltr)
        {
          childbox.x1 = (float) child_x;
          childbox.x2 = (float) MAX (0, child_x + col_width);
        }
      else
        {
          childbox.x2 = (float) child_x;
          childbox.x1 = (float) MAX (0, child_x - col_width);
        }

      childbox.y1 = (float) child_y;
      childbox.y2 = (float) MAX (0, child_y + row_height);


      _st_allocate_fill (ST_WIDGET (self), child, &childbox,
                         x_align, y_align, x_fill, y_fill);

      clutter_actor_allocate (child, &childbox, flags);
    }
}

static void
st_table_allocate (ClutterActor          *self,
                   const ClutterActorBox *box,
                   ClutterAllocationFlags flags)
{
  StTablePrivate *priv = ST_TABLE (self)->priv;
  StThemeNode *theme_node = st_widget_get_theme_node (ST_WIDGET (self));
  ClutterActorBox content_box;

  CLUTTER_ACTOR_CLASS (st_table_parent_class)->allocate (self, box, flags);

  if (priv->n_cols < 1 || priv->n_rows < 1)
    {
      return;
    };

  st_theme_node_get_content_box (theme_node, box, &content_box);

  if (priv->homogeneous)
    st_table_homogeneous_allocate (self, &content_box, flags);
  else
    st_table_preferred_allocate (self, &content_box, flags);
}

static void
st_table_get_preferred_width (ClutterActor *self,
                              gfloat        for_height,
                              gfloat       *min_width_p,
                              gfloat       *natural_width_p)
{
  gfloat total_min_width, total_pref_width;
  StTablePrivate *priv = ST_TABLE (self)->priv;
  StThemeNode *theme_node = st_widget_get_theme_node (ST_WIDGET (self));
  gint i;
  DimensionData *columns;

  if (priv->n_cols < 1)
    {
      *min_width_p = 0;
      *natural_width_p = 0;
      return;
    }

  /* use min_widths to help allocation of height-for-width widgets */
  st_table_calculate_dimensions (ST_TABLE (self), -1, for_height);

  columns = &g_array_index (priv->columns, DimensionData, 0);

  /* start off with row spacing */
  total_min_width = (priv->visible_cols - 1) * (float)(priv->col_spacing);
  total_pref_width = total_min_width;

  for (i = 0; i < priv->n_cols; i++)
    {
      total_min_width += columns[i].min_size;
      total_pref_width += columns[i].pref_size;
    }

  /* If we were requested width-for-height, then we reported minimum/natural
   * heights based on our natural width. If we were allocated less than our
   * natural width, then we need more height. So in the width-for-height
   * case we need to disable shrinking.
   */
  if (for_height >= 0)
    total_min_width = total_pref_width;

  if (min_width_p)
    *min_width_p = total_min_width;
  if (natural_width_p)
    *natural_width_p = total_pref_width;

  st_theme_node_adjust_preferred_width (theme_node, min_width_p, natural_width_p);
}

static void
st_table_get_preferred_height (ClutterActor *self,
                               gfloat        for_width,
                               gfloat       *min_height_p,
                               gfloat       *natural_height_p)
{
  gfloat total_min_height, total_pref_height;
  StTablePrivate *priv = ST_TABLE (self)->priv;
  StThemeNode *theme_node = st_widget_get_theme_node (ST_WIDGET (self));
  gint i;
  DimensionData *rows;

  /* We only support height-for-width allocation. So if we are called
   * width-for-height, calculate heights based on our natural width
   */
  if (for_width < 0)
    {
      float natural_width;

      clutter_actor_get_preferred_width (self, -1, NULL, &natural_width);
      for_width = natural_width;
    }

  if (priv->n_rows < 1)
    {
      *min_height_p = 0;
      *natural_height_p = 0;
      return;
    }

  st_theme_node_adjust_for_width (theme_node, &for_width);

  /* use min_widths to help allocation of height-for-width widgets */
  st_table_calculate_dimensions (ST_TABLE (self), for_width, -1);

  rows = &g_array_index (priv->rows, DimensionData, 0);

  /* start off with row spacing */
  total_min_height = (priv->visible_rows - 1) * (float)(priv->row_spacing);
  total_pref_height = total_min_height;

  for (i = 0; i < priv->n_rows; i++)
    {
      total_min_height += rows[i].min_size;
      total_pref_height += rows[i].pref_size;
    }

  if (min_height_p)
    *min_height_p = total_min_height;
  if (natural_height_p)
    *natural_height_p = total_pref_height;

  st_theme_node_adjust_preferred_height (theme_node, min_height_p, natural_height_p);
}

static void
st_table_paint (ClutterActor *self)
{
  GList *list, *children;

  /* make sure the background gets painted first */
  CLUTTER_ACTOR_CLASS (st_table_parent_class)->paint (self);

  children = st_container_get_children_list (ST_CONTAINER (self));
  for (list = children; list; list = list->next)
    {
      ClutterActor *child = CLUTTER_ACTOR (list->data);
      if (CLUTTER_ACTOR_IS_VISIBLE (child))
        clutter_actor_paint (child);
    }
}

static void
st_table_pick (ClutterActor       *self,
               const ClutterColor *color)
{
  GList *list, *children;

  /* Chain up so we get a bounding box painted (if we are reactive) */
  CLUTTER_ACTOR_CLASS (st_table_parent_class)->pick (self, color);

  children = st_container_get_children_list (ST_CONTAINER (self));
  for (list = children; list; list = list->next)
    {
      if (CLUTTER_ACTOR_IS_VISIBLE (list->data))
        clutter_actor_paint (CLUTTER_ACTOR (list->data));
    }
}

static void
st_table_show_all (ClutterActor *table)
{
  GList *l, *children;

  children = st_container_get_children_list (ST_CONTAINER (table));
  for (l = children; l; l = l->next)
    clutter_actor_show_all (CLUTTER_ACTOR (l->data));

  clutter_actor_show (table);
}

static void
st_table_hide_all (ClutterActor *table)
{
  GList *l, *children;

  clutter_actor_hide (table);

  children = st_container_get_children_list (ST_CONTAINER (table));
  for (l = children; l; l = l->next)
    clutter_actor_hide_all (CLUTTER_ACTOR (l->data));
}

static void
st_table_style_changed (StWidget *self)
{
  StTablePrivate *priv = ST_TABLE (self)->priv;
  StThemeNode *theme_node = st_widget_get_theme_node (self);
  int old_row_spacing = priv->row_spacing;
  int old_col_spacing = priv->col_spacing;
  double row_spacing, col_spacing;

  row_spacing = st_theme_node_get_length (theme_node, "spacing-rows");
  priv->row_spacing = (int)(row_spacing + 0.5);
  col_spacing = st_theme_node_get_length (theme_node, "spacing-columns");
  priv->col_spacing = (int)(col_spacing + 0.5);

  if (priv->row_spacing != old_row_spacing ||
      priv->col_spacing != old_col_spacing)
    clutter_actor_queue_relayout (CLUTTER_ACTOR (self));

  ST_WIDGET_CLASS (st_table_parent_class)->style_changed (self);
}

static void
st_table_class_init (StTableClass *klass)
{
  GParamSpec *pspec;
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  StWidgetClass *widget_class = ST_WIDGET_CLASS (klass);

  g_type_class_add_private (klass, sizeof (StTablePrivate));

  gobject_class->set_property = st_table_set_property;
  gobject_class->get_property = st_table_get_property;
  gobject_class->finalize = st_table_finalize;

  actor_class->paint = st_table_paint;
  actor_class->pick = st_table_pick;
  actor_class->allocate = st_table_allocate;
  actor_class->get_preferred_width = st_table_get_preferred_width;
  actor_class->get_preferred_height = st_table_get_preferred_height;
  actor_class->show_all = st_table_show_all;
  actor_class->hide_all = st_table_hide_all;

  widget_class->style_changed = st_table_style_changed;

  pspec = g_param_spec_boolean ("homogeneous",
                                "Homogeneous",
                                "Homogeneous rows and columns",
                                TRUE,
                                ST_PARAM_READWRITE);
  g_object_class_install_property (gobject_class,
                                   PROP_HOMOGENEOUS,
                                   pspec);

  pspec = g_param_spec_int ("row-count",
                            "Row Count",
                            "The number of rows in the table",
                            0, G_MAXINT, 0,
                            ST_PARAM_READABLE);
  g_object_class_install_property (gobject_class,
                                   PROP_ROW_COUNT,
                                   pspec);

  pspec = g_param_spec_int ("column-count",
                            "Column Count",
                            "The number of columns in the table",
                            0, G_MAXINT, 0,
                            ST_PARAM_READABLE);
  g_object_class_install_property (gobject_class,
                                   PROP_COL_COUNT,
                                   pspec);
}

static void
st_table_init (StTable *table)
{
  table->priv = ST_TABLE_GET_PRIVATE (table);

  table->priv->n_cols = 0;
  table->priv->n_rows = 0;

  table->priv->columns = g_array_new (FALSE, TRUE, sizeof (DimensionData));
  table->priv->rows = g_array_new (FALSE, TRUE, sizeof (DimensionData));
}

/* used by StTableChild to update row/column count */
void _st_table_update_row_col (StTable *table,
                               gint     row,
                               gint     col)
{
  if (col > -1)
    table->priv->n_cols = MAX (table->priv->n_cols, col + 1);

  if (row > -1)
    table->priv->n_rows = MAX (table->priv->n_rows, row + 1);

}

/*** Public Functions ***/

/**
 * st_table_new:
 *
 * Create a new #StTable
 *
 * Returns: a new #StTable
 */
StWidget*
st_table_new (void)
{
  return g_object_new (ST_TYPE_TABLE, NULL);
}

/**
 * st_table_get_row_count:
 * @table: A #StTable
 *
 * Retrieve the current number rows in the @table
 *
 * Returns: the number of rows
 */
gint
st_table_get_row_count (StTable *table)
{
  g_return_val_if_fail (ST_IS_TABLE (table), -1);

  return ST_TABLE (table)->priv->n_rows;
}

/**
 * st_table_get_column_count:
 * @table: A #StTable
 *
 * Retrieve the current number of columns in @table
 *
 * Returns: the number of columns
 */
gint
st_table_get_column_count (StTable *table)
{
  g_return_val_if_fail (ST_IS_TABLE (table), -1);

  return ST_TABLE (table)->priv->n_cols;
}
