/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) Red Hat 2011
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

/* Code largely based on clutter-box.c
 * Written by Emanuelle Bassi, originally licensed under LGPL 2.1.
 * Copyright Intel Corporation 2009-2011
 */

/**
 * SECTION:st-layout-container
 * @short_description: A container class that uses the existing layout
 * manager machinery.
 *
 * The #StLayoutContainer bolts the Clutter layout manager machinery on
 * to the back of StContainer, giving us all the goodies like CSS theming.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "st-private.h"

#include <clutter/clutter.h>

#include "st-layout-container.h"

struct _StLayoutContainerPrivate
{
  ClutterLayoutManager *manager;
  guint changed_id;
};

enum {
  PROP_0,

  PROP_LAYOUT_MANAGER,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

G_DEFINE_TYPE (StLayoutContainer,
               st_layout_container,
               ST_TYPE_CONTAINER);

static void
set_layout_manager (StLayoutContainer *self,
                    ClutterLayoutManager *manager)
{
  StLayoutContainerPrivate *priv = self->priv;

  if (priv->manager == manager)
    return;

  if (priv->manager != NULL)
    {
      if (priv->changed_id != 0)
        g_signal_handler_disconnect (priv->manager, priv->changed_id);

      clutter_layout_manager_set_container (priv->manager, NULL);
      g_object_unref (priv->manager);

      priv->manager = NULL;
      priv->changed_id = 0;
    }

  if (manager != NULL)
    {
      priv->manager = g_object_ref_sink (manager);
      clutter_layout_manager_set_container (manager,
                                            CLUTTER_CONTAINER (self));

      priv->changed_id =
        g_signal_connect_swapped (priv->manager, "layout-changed",
                                  G_CALLBACK (clutter_actor_queue_relayout),
                                  self);
    }

  clutter_actor_queue_relayout (CLUTTER_ACTOR (self));

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_LAYOUT_MANAGER]);
}

static void
st_layout_container_set_property (GObject      *gobject,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  StLayoutContainer *self = ST_LAYOUT_CONTAINER (gobject);

  switch (prop_id)
    {
    case PROP_LAYOUT_MANAGER:
      set_layout_manager (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
st_layout_container_get_property (GObject    *gobject,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  StLayoutContainer *self = ST_LAYOUT_CONTAINER (gobject);
  StLayoutContainerPrivate *priv = self->priv;

  switch (prop_id)
    {
    case PROP_LAYOUT_MANAGER:
      g_value_set_object (value, priv->manager);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
st_layout_container_get_preferred_width (ClutterActor *actor,
                                         gfloat        for_height,
                                         gfloat       *min_width_p,
                                         gfloat       *natural_width_p)
{
  StLayoutContainerPrivate *priv = ST_LAYOUT_CONTAINER (actor)->priv;
  StThemeNode *theme_node = st_widget_get_theme_node (ST_WIDGET (actor));

  st_theme_node_adjust_for_height (theme_node, &for_height);

  clutter_layout_manager_get_preferred_width (priv->manager,
                                              CLUTTER_CONTAINER (actor),
                                              for_height,
                                              min_width_p, natural_width_p);

  st_theme_node_adjust_preferred_width (theme_node, min_width_p, natural_width_p);
}

static void
st_layout_container_get_preferred_height (ClutterActor *actor,
                                          gfloat        for_width,
                                          gfloat       *min_height_p,
                                          gfloat       *natural_height_p)
{
  StLayoutContainerPrivate *priv = ST_LAYOUT_CONTAINER (actor)->priv;
  StThemeNode *theme_node = st_widget_get_theme_node (ST_WIDGET (actor));

  st_theme_node_adjust_for_width (theme_node, &for_width);

  clutter_layout_manager_get_preferred_height (priv->manager,
                                               CLUTTER_CONTAINER (actor),
                                               for_width,
                                               min_height_p, natural_height_p);

  st_theme_node_adjust_preferred_height (theme_node, min_height_p, natural_height_p);
}

static void
st_layout_container_allocate (ClutterActor           *actor,
                              const ClutterActorBox  *box,
                              ClutterAllocationFlags  flags)
{
  StLayoutContainerPrivate *priv = ST_LAYOUT_CONTAINER (actor)->priv;
  StThemeNode *theme_node;
  ClutterActorBox content_box;

  CLUTTER_ACTOR_CLASS (st_layout_container_parent_class)->allocate (actor, box, flags);

  theme_node = st_widget_get_theme_node (ST_WIDGET (actor));
  st_theme_node_get_content_box (theme_node, box, &content_box);

  clutter_layout_manager_allocate (priv->manager,
                                   CLUTTER_CONTAINER (actor),
                                   &content_box, flags);
}

static void
st_layout_container_init (StLayoutContainer *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, ST_TYPE_LAYOUT_CONTAINER,
                                            StLayoutContainerPrivate);
}

static void
st_layout_container_class_init (StLayoutContainerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (StLayoutContainerPrivate));

  actor_class->get_preferred_width = st_layout_container_get_preferred_width;
  actor_class->get_preferred_height = st_layout_container_get_preferred_height;
  actor_class->allocate = st_layout_container_allocate;
  actor_class->paint = _st_container_paint;
  actor_class->pick = _st_container_pick;

  gobject_class->set_property = st_layout_container_set_property;
  gobject_class->get_property = st_layout_container_get_property;

  pspec = g_param_spec_object ("layout-manager",
                               "Layout Manager",
                               "The layout manager used by the container",
                               CLUTTER_TYPE_LAYOUT_MANAGER,
                               ST_PARAM_READWRITE |
                               G_PARAM_CONSTRUCT);
  obj_props[PROP_LAYOUT_MANAGER] = pspec;
  g_object_class_install_property (gobject_class,
                                   PROP_LAYOUT_MANAGER,
                                   pspec);

}

/*** Public Functions ***/

/**
 * st_layout_container_new:
 * @manager: A #ClutterLayoutManager
 *
 * Create a new #StLayoutContainer
 *
 * Returns: a new #StLayoutContainer
 */
ClutterActor *
st_layout_container_new (ClutterLayoutManager *manager)
{
  g_return_val_if_fail (CLUTTER_IS_LAYOUT_MANAGER (manager), NULL);

  return g_object_new (ST_TYPE_LAYOUT_CONTAINER,
                       "layout-manager", manager,
                       NULL);
}

/**
 * st_layout_container_set_layout_manager:
 * @self: a #StLayoutContainer
 * @manager: a #ClutterLayoutManager
 */
void
st_layout_container_set_layout_manager (StLayoutContainer *self,
                                        ClutterLayoutManager *manager)
{
  g_return_if_fail (ST_IS_LAYOUT_CONTAINER (self));
  g_return_if_fail (CLUTTER_IS_LAYOUT_MANAGER (manager));

  set_layout_manager (self, manager);
}

/**
 * st_layout_container_get_layout_manager:
 * @self: a #StLayoutContainer
 *
 * Returns: (transfer none): a #ClutterLayoutManager.
 */
ClutterLayoutManager *
st_layout_container_get_layout_manager (StLayoutContainer *self)
{
  g_return_val_if_fail (ST_IS_LAYOUT_CONTAINER (self), NULL);

  return self->priv->manager;
}
