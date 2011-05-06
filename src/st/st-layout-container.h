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

#if !defined(ST_H_INSIDE) && !defined(ST_COMPILATION)
#error "Only <st/st.h> can be included directly.h"
#endif

#ifndef __ST_LAYOUT_CONTAINER_H__
#define __ST_LAYOUT_CONTAINER_H__

#include <glib-object.h>
#include <st/st-container.h>

G_BEGIN_DECLS

#define ST_TYPE_LAYOUT_CONTAINER            (st_layout_container_get_type())
#define ST_LAYOUT_CONTAINER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), ST_TYPE_LAYOUT_CONTAINER, StLayoutContainer))
#define ST_LAYOUT_CONTAINER_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), ST_TYPE_LAYOUT_CONTAINER, StLayoutContainer const))
#define ST_LAYOUT_CONTAINER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  ST_TYPE_LAYOUT_CONTAINER, StLayoutContainerClass))
#define ST_IS_LAYOUT_CONTAINER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), ST_TYPE_LAYOUT_CONTAINER))
#define ST_IS_LAYOUT_CONTAINER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  ST_TYPE_LAYOUT_CONTAINER))
#define ST_LAYOUT_CONTAINER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  ST_TYPE_LAYOUT_CONTAINER, StLayoutContainerClass))

typedef struct _StLayoutContainer        StLayoutContainer;
typedef struct _StLayoutContainerClass   StLayoutContainerClass;
typedef struct _StLayoutContainerPrivate StLayoutContainerPrivate;

struct _StLayoutContainer
{
  StContainer parent;

  /*< private >*/
  StLayoutContainerPrivate *priv;
};

struct _StLayoutContainerClass
{
  StContainerClass parent_class;
};

GType st_layout_container_get_type (void) G_GNUC_CONST;

void
st_layout_container_set_layout_manager (StLayoutContainer *self,
                                        ClutterLayoutManager *manager);

ClutterLayoutManager *
st_layout_container_get_layout_manager (StLayoutContainer *self);

ClutterActor *
st_layout_container_new (ClutterLayoutManager *manager);

G_END_DECLS

#endif /* __ST_LAYOUT_CONTAINER_H__ */
