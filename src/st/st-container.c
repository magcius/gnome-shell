/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * st-container.c: Base class for St container actors
 *
 * Copyright 2007 OpenedHand
 * Copyright 2008, 2009 Intel Corporation.
 * Copyright 2010 Florian Müllner
 * Copyright 2010 Red Hat, Inc.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include "st-container.h"

G_DEFINE_ABSTRACT_TYPE (StContainer, st_container, ST_TYPE_WIDGET);

/**
 * st_container_get_children_list:
 * @container: An #StContainer
 *
 * Get the internal list of @container's child actors. This function
 * should only be used by subclasses of StContainer
 *
 * Returns: (element-type Clutter.Actor) (transfer none): list of @container's child actors
 */
GList *
st_container_get_children_list (StContainer *container)
{
  return clutter_actor_get_children (CLUTTER_ACTOR (container));
}

static void
st_container_foreach (ClutterContainer *container,
                      ClutterCallback   callback,
                      gpointer          user_data)
{
  g_list_foreach (clutter_actor_get_children (CLUTTER_ACTOR (container)), (GFunc) callback, user_data);
}

void
st_container_move_child (StContainer  *container,
                         ClutterActor *actor,
                         int           pos)
{
  clutter_actor_set_child_at_index (CLUTTER_ACTOR (container),
                                    actor, pos);
}

void
st_container_move_before (StContainer  *container,
                          ClutterActor *actor,
                          ClutterActor *sibling)
{
  clutter_actor_set_child_below_sibling (CLUTTER_ACTOR (container),
                                         actor, sibling);
}

/* filter @children to contain only only actors that overlap @rbox
 * when moving in @direction. (Assuming no transformations.)
 */
static GList *
filter_by_position (GList            *children,
                    ClutterActorBox  *rbox,
                    GtkDirectionType  direction)
{
  ClutterActorBox cbox;
  GList *l, *ret;
  ClutterActor *child;

  for (l = children, ret = NULL; l; l = l->next)
    {
      child = l->data;
      clutter_actor_get_allocation_box (child, &cbox);

      /* Filter out children if they are in the wrong direction from
       * @rbox, or if they don't overlap it. To account for floating-
       * point imprecision, an actor is "down" (etc.) from an another
       * actor even if it overlaps it by up to 0.1 pixels.
       */
      switch (direction)
        {
        case GTK_DIR_UP:
          if (cbox.y2 > rbox->y1 + 0.1)
            continue;
          if (cbox.x1 >= rbox->x2 || cbox.x2 <= rbox->x1)
            continue;
          break;

        case GTK_DIR_DOWN:
          if (cbox.y1 < rbox->y2 - 0.1)
            continue;
          if (cbox.x1 >= rbox->x2 || cbox.x2 <= rbox->x1)
            continue;
          break;

        case GTK_DIR_LEFT:
          if (cbox.x2 > rbox->x1 + 0.1)
            continue;
          if (cbox.y1 >= rbox->y2 || cbox.y2 <= rbox->y1)
            continue;
          break;

        case GTK_DIR_RIGHT:
          if (cbox.x1 < rbox->x2 - 0.1)
            continue;
          if (cbox.y1 >= rbox->y2 || cbox.y2 <= rbox->y1)
            continue;
          break;

        default:
          g_return_val_if_reached (NULL);
        }

      ret = g_list_prepend (ret, child);
    }

  g_list_free (children);
  return ret;
}

typedef struct {
  GtkDirectionType direction;
  ClutterActorBox box;
} StContainerChildSortData;

static int
sort_by_position (gconstpointer  a,
                  gconstpointer  b,
                  gpointer       user_data)
{
  ClutterActor *actor_a = (ClutterActor *)a;
  ClutterActor *actor_b = (ClutterActor *)b;
  StContainerChildSortData *sort_data = user_data;
  GtkDirectionType direction = sort_data->direction;
  ClutterActorBox abox, bbox;
  int ax, ay, bx, by;
  int cmp, fmid;

  /* Determine the relationship, relative to motion in @direction, of
   * the center points of the two actors. Eg, for %GTK_DIR_UP, we
   * return a negative number if @actor_a's center is below @actor_b's
   * center, and postive if vice versa, which will result in an
   * overall list sorted bottom-to-top.
   */

  clutter_actor_get_allocation_box (actor_a, &abox);
  ax = (int)(abox.x1 + abox.x2) / 2;
  ay = (int)(abox.y1 + abox.y2) / 2;
  clutter_actor_get_allocation_box (actor_b, &bbox);
  bx = (int)(bbox.x1 + bbox.x2) / 2;
  by = (int)(bbox.y1 + bbox.y2) / 2;

  switch (direction)
    {
    case GTK_DIR_UP:
      cmp = by - ay;
      break;
    case GTK_DIR_DOWN:
      cmp = ay - by;
      break;
    case GTK_DIR_LEFT:
      cmp = bx - ax;
      break;
    case GTK_DIR_RIGHT:
      cmp = ax - bx;
      break;
    default:
      g_return_val_if_reached (0);
    }

  if (cmp)
    return cmp;

  /* If two actors have the same center on the axis being sorted,
   * prefer the one that is closer to the center of the current focus
   * actor on the other axis. Eg, for %GTK_DIR_UP, prefer whichever
   * of @actor_a and @actor_b has a horizontal center closest to the
   * current focus actor's horizontal center.
   *
   * (This matches GTK's behavior.)
   */
  switch (direction)
    {
    case GTK_DIR_UP:
    case GTK_DIR_DOWN:
      fmid = (int)(sort_data->box.x1 + sort_data->box.x2) / 2;
      return abs (ax - fmid) - abs (bx - fmid);
    case GTK_DIR_LEFT:
    case GTK_DIR_RIGHT:
      fmid = (int)(sort_data->box.y1 + sort_data->box.y2) / 2;
      return abs (ay - fmid) - abs (by - fmid);
    default:
      g_return_val_if_reached (0);
    }
}

static gboolean
st_container_navigate_focus (StWidget         *widget,
                             ClutterActor     *from,
                             GtkDirectionType  direction)
{
  StContainer *container = ST_CONTAINER (widget);
  ClutterActor *container_actor, *focus_child;
  GList *children, *l;

  container_actor = CLUTTER_ACTOR (widget);
  if (from == container_actor)
    return FALSE;

  /* Figure out if @from is a descendant of @container, and if so,
   * set @focus_child to the immediate child of @container that
   * contains (or *is*) @from.
   */
  focus_child = from;
  while (focus_child && clutter_actor_get_parent (focus_child) != container_actor)
    focus_child = clutter_actor_get_parent (focus_child);

  if (st_widget_get_can_focus (widget))
    {
      if (!focus_child)
        {
          /* Accept focus from outside */
          clutter_actor_grab_key_focus (container_actor);
          return TRUE;
        }
      else
        {
          /* Yield focus from within: since @container itself is
           * focusable we don't allow the focus to be navigated
           * within @container.
           */
          return FALSE;
        }
    }

  /* See if we can navigate within @focus_child */
  if (focus_child && ST_IS_WIDGET (focus_child))
    {
      if (st_widget_navigate_focus (ST_WIDGET (focus_child), from, direction, FALSE))
        return TRUE;
    }

  /* At this point we know that we want to navigate focus to one of
   * @container's immediate children; the next one after @focus_child,
   * or the first one if @focus_child is %NULL. (With "next" and
   * "first" being determined by @direction.)
   */

  children = st_widget_get_focus_chain (ST_WIDGET (container));
  if (direction == GTK_DIR_TAB_FORWARD ||
      direction == GTK_DIR_TAB_BACKWARD)
    {
      if (direction == GTK_DIR_TAB_BACKWARD)
        children = g_list_reverse (children);

      if (focus_child)
        {
          /* Remove focus_child and any earlier children */
          while (children && children->data != focus_child)
            children = g_list_delete_link (children, children);
          if (children)
            children = g_list_delete_link (children, children);
        }
    }
  else /* direction is an arrow key, not tab */
    {
      StContainerChildSortData sort_data;

      /* Compute the allocation box of the previous focused actor, in
       * @container's coordinate space. If there was no previous focus,
       * use the coordinates of the appropriate edge of @container.
       *
       * Note that all of this code assumes the actors are not
       * transformed (or at most, they are all scaled by the same
       * amount). If @container or any of its children is rotated, or
       * any child is inconsistently scaled, then the focus chain will
       * probably be unpredictable.
       */
      if (focus_child)
        {
          clutter_actor_get_allocation_box (focus_child, &sort_data.box);
        }
      else
        {
          clutter_actor_get_allocation_box (CLUTTER_ACTOR (container), &sort_data.box);
          switch (direction)
            {
            case GTK_DIR_UP:
              sort_data.box.y1 = sort_data.box.y2;
              break;
            case GTK_DIR_DOWN:
              sort_data.box.y2 = sort_data.box.y1;
              break;
            case GTK_DIR_LEFT:
              sort_data.box.x1 = sort_data.box.x2;
              break;
            case GTK_DIR_RIGHT:
              sort_data.box.x2 = sort_data.box.x1;
              break;
            default:
              g_warn_if_reached ();
            }
        }
      sort_data.direction = direction;

      if (focus_child)
        children = filter_by_position (children, &sort_data.box, direction);
      if (children)
        children = g_list_sort_with_data (children, sort_by_position, &sort_data);
    }

  /* Now try each child in turn */
  for (l = children; l; l = l->next)
    {
      if (ST_IS_WIDGET (l->data))
        {
          if (st_widget_navigate_focus (l->data, from, direction, FALSE))
            {
              g_list_free (children);
              return TRUE;
            }
        }
    }

  g_list_free (children);
  return FALSE;
}

static void
clutter_container_iface_init (ClutterContainerIface *iface)
{
  iface->add = st_container_add;
  iface->remove = st_container_remove;
  iface->foreach = st_container_foreach;
  iface->raise = st_container_raise;
  iface->lower = st_container_lower;
  iface->sort_depth_order = st_container_sort_depth_order;
}

static void
st_container_init (StContainer *container)
{
  container->priv = ST_CONTAINER_GET_PRIVATE (container);
}

static void
st_container_class_init (StContainerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  StWidgetClass *widget_class = ST_WIDGET_CLASS (klass);
  StContainerClass *container_class = ST_CONTAINER_CLASS (klass);

  g_type_class_add_private (klass, sizeof (StContainerPrivate));

  object_class->dispose = st_container_dispose;

  actor_class->get_paint_volume = st_container_get_paint_volume;

  widget_class->navigate_focus = st_container_navigate_focus;
}
