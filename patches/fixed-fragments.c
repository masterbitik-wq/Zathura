/* Фрагменты исправленного zathura/page-widget.c
 * (для чтения без применения патча; не компилируется отдельно). */

/* 1) error-path cairo: nil-surface тоже нужно уничтожить */
static cairo_surface_t* draw_thumbnail_image(cairo_surface_t* surface, size_t max_size) {
  /* ... расчёт размеров ... */
  cairo_surface_t* thumbnail =
      cairo_surface_create_similar(surface, CAIRO_CONTENT_COLOR, unscaled_width, unscaled_height);
  if (cairo_surface_status(thumbnail) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(thumbnail);
    return NULL;
  }
  /* ... */
}

/* 2) вытеснение из кэша: всегда сбрасываем полноразмерную поверхность */
static void cb_cache_invalidated(ZathuraRenderRequest* request, void* data) {
  ZathuraPageWidget* widget = data;
  ZathuraPageWidgetPrivate* priv = zathura_page_widget_get_instance_private(widget);

  if (zathura_page_widget_have_surface(widget) == true && priv->cached == true) {
    const bool on_screen = page_widget_on_screen(GTK_WIDGET(widget));
    if (on_screen == false) {
      zathura_page_widget_update_surface(widget, NULL, false);
    } else {
      zathura_page_widget_update_surface(widget, NULL, true);
    }
  }
  priv->cached = false;
}

/* 3) при скролле: если страница уже не в кэше и не на экране — освободить */
void zathura_page_widget_update_view_time(ZathuraPageWidget* widget) {
  ZathuraPageWidgetPrivate* priv = zathura_page_widget_get_instance_private(widget);

  if (zathura_page_get_visibility(priv->page) == true) {
    zathura_render_request_update_view_time(priv->render_request);
  }
  if (priv->cached == false && page_widget_on_screen(GTK_WIDGET(widget)) == false) {
    zathura_page_widget_update_surface(widget, NULL, false);
  }
  if (priv->surface == NULL) {
    zathura_render_request(priv->render_request, g_get_real_time());
  }
}
