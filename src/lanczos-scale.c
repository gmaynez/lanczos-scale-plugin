// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wignored-qualifiers"
#endif
#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "gimp-io.h"

#define PLUG_IN_PROC   "plug-in-lanczos-scale"
#define PLUG_IN_BINARY "lanczos-scale"
#define PLUG_IN_ROLE   "gimp-lanczos-scale"

#define LANCZOS_RESPONSE_RESET 1

typedef enum
{
  TARGET_SELECTED_DRAWABLE,
  TARGET_VISIBLE_IMAGE,
} TargetMode;

typedef enum
{
  OUTPUT_NEW_LAYER,
  OUTPUT_REPLACE_DRAWABLE,
  OUTPUT_NEW_IMAGE,
} OutputMode;

typedef struct _LanczosScale      LanczosScale;
typedef struct _LanczosScaleClass LanczosScaleClass;

typedef struct
{
  GimpProcedureConfig *config;
  gint                 width;
  gint                 height;
  gchar               *kernel;
  gboolean             linear_light;
} DialogDefaults;

struct _LanczosScale
{
  GimpPlugIn parent_instance;
};

struct _LanczosScaleClass
{
  GimpPlugInClass parent_class;
};

#define LANCZOS_SCALE_TYPE (lanczos_scale_get_type ())

GType                   lanczos_scale_get_type         (void) G_GNUC_CONST;

static GList          * lanczos_scale_query_procedures (GimpPlugIn           *plug_in);
static GimpProcedure  * lanczos_scale_create_procedure (GimpPlugIn           *plug_in,
                                                        const gchar          *name);
static gboolean         lanczos_scale_set_i18n         (GimpPlugIn           *plug_in,
                                                        const gchar          *procedure_name,
                                                        gchar               **gettext_domain,
                                                        gchar               **catalog_dir);
static GimpValueArray * lanczos_scale_run              (GimpProcedure        *procedure,
                                                        GimpRunMode           run_mode,
                                                        GimpImage            *image,
                                                        GimpDrawable        **drawables,
                                                        GimpProcedureConfig  *config,
                                                        gpointer              run_data);

G_DEFINE_TYPE (LanczosScale, lanczos_scale, GIMP_TYPE_PLUG_IN)

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif
GIMP_MAIN (LANCZOS_SCALE_TYPE)
#ifdef __clang__
#pragma clang diagnostic pop
#endif

static void
lanczos_scale_class_init (LanczosScaleClass *klass)
{
  GimpPlugInClass *plug_in_class = GIMP_PLUG_IN_CLASS (klass);

  plug_in_class->query_procedures = lanczos_scale_query_procedures;
  plug_in_class->create_procedure = lanczos_scale_create_procedure;
  plug_in_class->set_i18n         = lanczos_scale_set_i18n;
}

static void
lanczos_scale_init (LanczosScale *self G_GNUC_UNUSED)
{
}

static GList *
lanczos_scale_query_procedures (GimpPlugIn *plug_in G_GNUC_UNUSED)
{
  return g_list_append (NULL, g_strdup (PLUG_IN_PROC));
}

static gboolean
lanczos_scale_set_i18n (GimpPlugIn   *plug_in G_GNUC_UNUSED,
                        const gchar  *procedure_name G_GNUC_UNUSED,
                        gchar       **gettext_domain G_GNUC_UNUSED,
                        gchar       **catalog_dir G_GNUC_UNUSED)
{
  return FALSE;
}

static GimpChoice *
lanczos_scale_create_target_choice (void)
{
  return gimp_choice_new_with_values ("selected-drawable", TARGET_SELECTED_DRAWABLE,
                                      "Selected drawable", NULL,
                                      "visible-image", TARGET_VISIBLE_IMAGE,
                                      "Visible image", NULL,
                                      NULL);
}

static GimpChoice *
lanczos_scale_create_kernel_choice (void)
{
  return gimp_choice_new_with_values ("lanczos3", LANCZOS_KERNEL_3,
                                      "Lanczos 3", NULL,
                                      "lanczos2", LANCZOS_KERNEL_2,
                                      "Lanczos 2", NULL,
                                      NULL);
}

static GimpChoice *
lanczos_scale_create_output_choice (void)
{
  return gimp_choice_new_with_values ("new-layer", OUTPUT_NEW_LAYER,
                                      "New layer", NULL,
                                      "replace-drawable", OUTPUT_REPLACE_DRAWABLE,
                                      "Replace drawable", NULL,
                                      "new-image", OUTPUT_NEW_IMAGE,
                                      "New image", NULL,
                                      NULL);
}

static GimpProcedure *
lanczos_scale_create_procedure (GimpPlugIn  *plug_in,
                                const gchar *name)
{
  GimpProcedure *procedure = NULL;

  if (g_strcmp0 (name, PLUG_IN_PROC) == 0)
    {
      procedure = gimp_image_procedure_new (plug_in,
                                            name,
                                            GIMP_PDB_PROC_TYPE_PLUGIN,
                                            lanczos_scale_run,
                                            NULL,
                                            NULL);

      gimp_procedure_set_image_types (procedure, "RGB*, GRAY*");
      gimp_procedure_set_sensitivity_mask (procedure,
                                           GIMP_PROCEDURE_SENSITIVE_DRAWABLE);
      gimp_procedure_set_menu_label (procedure, "_Lanczos Scale...");
      gimp_procedure_add_menu_path (procedure, "<Image>/Image/[Scale]");
      gimp_procedure_add_menu_path (procedure, "<Image>/Layer/[Scale]");
      gimp_procedure_set_documentation (procedure,
                                        "Scale the selected layer with a custom Lanczos resampler",
                                        "Scales the selected layer drawable in place using a separable Lanczos2 or Lanczos3 filter.",
                                        PLUG_IN_PROC);
      gimp_procedure_set_attribution (procedure,
                                      "OpenAI Codex",
                                      "OpenAI Codex",
                                      "2026");

      gimp_procedure_add_choice_argument (procedure, "target",
                                          "Source",
                                          "Pixels to scale",
                                          lanczos_scale_create_target_choice (),
                                          "selected-drawable",
                                          G_PARAM_READWRITE);

      gimp_procedure_add_int_argument (procedure, "new-width",
                                       "_Width",
                                       "Scaled output width in pixels",
                                       1, GIMP_MAX_IMAGE_SIZE, 1,
                                       G_PARAM_READWRITE);

      gimp_procedure_add_int_argument (procedure, "new-height",
                                       "_Height",
                                       "Scaled output height in pixels",
                                       1, GIMP_MAX_IMAGE_SIZE, 1,
                                       G_PARAM_READWRITE);

      gimp_procedure_add_unit_aux_argument (procedure, "size-unit",
                                            "Size unit",
                                            "Output size unit of measure",
                                            TRUE, TRUE, gimp_unit_pixel (),
                                            G_PARAM_READWRITE);

      gimp_procedure_add_choice_argument (procedure, "kernel",
                                          "Interpolation",
                                          "Lanczos kernel radius",
                                          lanczos_scale_create_kernel_choice (),
                                          "lanczos3",
                                          G_PARAM_READWRITE);

      gimp_procedure_add_choice_argument (procedure, "output-mode",
                                          "Output",
                                          "Where to write the scaled result",
                                          lanczos_scale_create_output_choice (),
                                          "replace-drawable",
                                          G_PARAM_READWRITE);

      gimp_procedure_add_boolean_argument (procedure, "linear-light",
                                           "Linear light",
                                           "Resample in linear-light floating point",
                                           TRUE,
                                           G_PARAM_READWRITE);

      gimp_procedure_add_string_argument (procedure, "name",
                                          "Name",
                                          "Output layer or image layer name",
                                          "Lanczos Scale",
                                          G_PARAM_READWRITE);

      gimp_procedure_add_image_return_value (procedure, "image",
                                             "Image",
                                             "Output image, or the original image when writing into it",
                                             TRUE,
                                             G_PARAM_READWRITE);

      gimp_procedure_add_layer_return_value (procedure, "new-layer",
                                             "New layer",
                                             "Output layer, when a layer is created",
                                             TRUE,
                                             G_PARAM_READWRITE);
    }

  return procedure;
}

static GimpImageBaseType
base_type_from_drawable (GimpDrawable *drawable)
{
  switch (gimp_drawable_type (drawable))
    {
    case GIMP_RGB_IMAGE:
    case GIMP_RGBA_IMAGE:
      return GIMP_RGB;

    case GIMP_GRAY_IMAGE:
    case GIMP_GRAYA_IMAGE:
      return GIMP_GRAY;

    case GIMP_INDEXED_IMAGE:
    case GIMP_INDEXEDA_IMAGE:
      return GIMP_INDEXED;
    }

  return GIMP_RGB;
}

static GimpLayer *
create_output_layer (GimpImage    *image,
                     GimpDrawable *source,
                     const gchar  *name,
                     gint          width,
                     gint          height)
{
  GimpLayer *layer;

  layer = gimp_layer_new (image,
                          name && *name ? name : "Lanczos Scale",
                          width,
                          height,
                          gimp_drawable_type (source),
                          100.0,
                          gimp_image_get_default_new_layer_mode (image));

  return layer;
}

static gboolean
copy_image_metadata (GimpImage *src,
                     GimpImage *dst)
{
  GimpColorProfile *profile;
  GimpUnit         *unit;
  gdouble           xres;
  gdouble           yres;

  profile = gimp_image_get_color_profile (src);
  if (profile)
    {
      gimp_image_set_color_profile (dst, profile);
      g_clear_object (&profile);
    }

  if (gimp_image_get_resolution (src, &xres, &yres))
    gimp_image_set_resolution (dst, xres, yres);

  unit = gimp_image_get_unit (src);
  if (unit)
    gimp_image_set_unit (dst, unit);

  return TRUE;
}

static void
delete_failed_output (OutputMode  output_mode,
                      GimpImage  *output_image,
                      GimpLayer  *output_layer)
{
  if (output_image)
    {
      gimp_image_delete (output_image);
      return;
    }

  if (output_mode == OUTPUT_NEW_LAYER && output_layer)
    gimp_item_delete (GIMP_ITEM (output_layer));
}

static GimpLayer *
insert_layer_near_source (GimpImage    *image,
                          GimpLayer    *layer,
                          GimpDrawable *source)
{
  GimpLayer *parent = NULL;
  gint       position = 0;

  if (source && gimp_item_is_layer (GIMP_ITEM (source)))
    {
      GimpItem *parent_item = gimp_item_get_parent (GIMP_ITEM (source));

      if (parent_item)
        parent = GIMP_LAYER (parent_item);

      position = gimp_image_get_item_position (image, GIMP_ITEM (source));
    }

  if (! gimp_image_insert_layer (image, layer, parent, position))
    return NULL;

  return layer;
}

static GimpLayer *
create_new_image_with_layer (GimpImage    *source_image,
                             GimpDrawable *source,
                             const gchar  *name,
                             gint          width,
                             gint          height,
                             GimpImage   **new_image)
{
  GimpLayer *layer;

  *new_image = gimp_image_new_with_precision (width,
                                              height,
                                              base_type_from_drawable (source),
                                              gimp_image_get_precision (source_image));
  if (! *new_image)
    return NULL;

  gimp_image_undo_disable (*new_image);
  copy_image_metadata (source_image, *new_image);

  layer = create_output_layer (*new_image, source, name, width, height);
  if (! layer)
    return NULL;

  if (! gimp_image_insert_layer (*new_image, layer, NULL, 0))
    return NULL;

  return layer;
}

static void
progress_cb (gdouble  fraction,
             gpointer data G_GNUC_UNUSED)
{
  gimp_progress_update (fraction);
}

static GeglBuffer *
copy_source_buffer (GimpDrawable *drawable,
                    gint          width,
                    gint          height)
{
  GeglBuffer *src;
  GeglBuffer *copy;

  src = gimp_drawable_get_buffer (drawable);
  copy = gegl_buffer_new (GEGL_RECTANGLE (0, 0, width, height),
                          gimp_drawable_get_format (drawable));

  gegl_buffer_copy (src, GEGL_RECTANGLE (0, 0, width, height),
                    GEGL_ABYSS_NONE,
                    copy, GEGL_RECTANGLE (0, 0, width, height));

  g_clear_object (&src);

  return copy;
}

static GtkWidget *
create_size_coordinates (GimpProcedureConfig *config,
                         gint                 width,
                         gint                 height,
                         gdouble              xres,
                         gdouble              yres)
{
  GtkWidget *coordinates;
  GtkWidget *chainbutton;

  coordinates = gimp_coordinates_new (gimp_unit_pixel (),
                                      "%a",
                                      TRUE,
                                      TRUE,
                                      10,
                                      GIMP_SIZE_ENTRY_UPDATE_SIZE,
                                      TRUE,
                                      TRUE,
                                      "_Width:",
                                      width,
                                      xres,
                                      1.0,
                                      GIMP_MAX_IMAGE_SIZE,
                                      0.0,
                                      width,
                                      "_Height:",
                                      height,
                                      yres,
                                      1.0,
                                      GIMP_MAX_IMAGE_SIZE,
                                      0.0,
                                      height);

  chainbutton = GIMP_COORDINATES_CHAINBUTTON (GIMP_SIZE_ENTRY (coordinates));
  g_object_set_data (G_OBJECT (chainbutton),
                     "constrains-ratio",
                     GINT_TO_POINTER (TRUE));

  gimp_prop_coordinates_connect (G_OBJECT (config),
                                 "new-width",
                                 "new-height",
                                 "size-unit",
                                 coordinates,
                                 NULL,
                                 xres,
                                 yres);

  return coordinates;
}

static GtkWidget *
create_quality_frame (GimpProcedureConfig *config)
{
  GtkWidget *frame;
  GtkWidget *grid;
  GtkWidget *label;
  GtkWidget *combo;
  GtkWidget *linear_light;

  frame = gimp_frame_new ("Quality");
  grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 8);

  label = gtk_label_new_with_mnemonic ("Interpolation:");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);

  combo = gimp_prop_choice_combo_box_new (G_OBJECT (config), "kernel");
  gtk_widget_set_hexpand (combo, TRUE);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);

  linear_light = gimp_prop_check_button_new (G_OBJECT (config),
                                             "linear-light",
                                             "Linear light");

  gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), combo, 1, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), linear_light, 1, 1, 1, 1);

  gtk_container_add (GTK_CONTAINER (frame), grid);

  gtk_widget_show (label);
  gtk_widget_show (combo);
  gtk_widget_show (linear_light);
  gtk_widget_show (grid);
  gtk_widget_show (frame);

  return frame;
}

static void
hide_saved_settings_box (GtkWidget *dialog)
{
  GtkWidget *content_area;
  GList     *children;

  content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  children = gtk_container_get_children (GTK_CONTAINER (content_area));

  for (GList *iter = children; iter; iter = g_list_next (iter))
    {
      if (GTK_IS_BUTTON_BOX (iter->data))
        gtk_widget_hide (GTK_WIDGET (iter->data));
    }

  g_list_free (children);
}

static void
make_reset_button_plain (GtkWidget *dialog)
{
  GtkWidget *button;
  GtkWidget *child;
  GtkWidget *label;

  button = gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog),
                                               LANCZOS_RESPONSE_RESET);
  if (! button || ! GTK_IS_BUTTON (button))
    return;

  child = gtk_bin_get_child (GTK_BIN (button));
  if (child)
    gtk_container_remove (GTK_CONTAINER (button), child);

  label = gtk_label_new_with_mnemonic ("_Reset");
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), button);
  gtk_container_add (GTK_CONTAINER (button), label);
  gtk_widget_show (label);
}

static void
reset_dialog_defaults (const DialogDefaults *defaults)
{
  g_object_set (defaults->config,
                "new-width", defaults->width,
                "new-height", defaults->height,
                "size-unit", gimp_unit_pixel (),
                "kernel", defaults->kernel,
                "linear-light", defaults->linear_light,
                NULL);
}

static gboolean
run_clean_dialog (GtkWidget            *dialog,
                  const DialogDefaults *defaults)
{
  while (TRUE)
    {
      gint response = gimp_dialog_run (GIMP_DIALOG (dialog));

      if (response == LANCZOS_RESPONSE_RESET)
        {
          reset_dialog_defaults (defaults);
          continue;
        }

      return response == GTK_RESPONSE_OK;
    }
}

static gboolean
run_dialog (GimpProcedure       *procedure,
            GimpProcedureConfig *config,
            GimpImage           *image,
            GimpDrawable        *drawable,
            gboolean             has_drawable)
{
  GtkWidget *dialog;
  GtkWidget *coordinates;
  GtkWidget *content_area;
  GtkWidget *size_frame;
  GtkWidget *quality_frame;
  DialogDefaults defaults;
  gint       width;
  gint       height;
  gdouble    xres;
  gdouble    yres;
  gboolean   run;

  gimp_ui_init (PLUG_IN_BINARY);

  if (! has_drawable)
    return FALSE;

  width = gimp_drawable_get_width (drawable);
  height = gimp_drawable_get_height (drawable);

  g_object_set (config,
                "target", "selected-drawable",
                "output-mode", "replace-drawable",
                NULL);

  gimp_image_get_resolution (image, &xres, &yres);
  g_object_set (config,
                "new-width", width,
                "new-height", height,
                NULL);
  g_object_get (config,
                "kernel", &defaults.kernel,
                "linear-light", &defaults.linear_light,
                NULL);

  defaults.config = config;
  defaults.width = width;
  defaults.height = height;

  dialog = gimp_procedure_dialog_new (procedure,
                                      config,
                                      "Lanczos Scale");
  gimp_procedure_dialog_set_ok_label (GIMP_PROCEDURE_DIALOG (dialog),
                                      "_Scale");
  hide_saved_settings_box (dialog);
  make_reset_button_plain (dialog);
  gimp_dialog_set_alternative_button_order (GTK_DIALOG (dialog),
                                            GTK_RESPONSE_HELP,
                                            LANCZOS_RESPONSE_RESET,
                                            GTK_RESPONSE_OK,
                                            GTK_RESPONSE_CANCEL,
                                            -1);

  coordinates = create_size_coordinates (config, width, height, xres, yres);
  size_frame = gimp_frame_new ("Image Size");
  gtk_container_add (GTK_CONTAINER (size_frame), coordinates);
  gtk_widget_show (coordinates);
  gtk_widget_show (size_frame);

  content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  gtk_box_pack_start (GTK_BOX (content_area), size_frame, FALSE, FALSE, 0);

  quality_frame = create_quality_frame (config);
  gtk_box_pack_start (GTK_BOX (content_area), quality_frame, FALSE, FALSE, 0);

  run = run_clean_dialog (dialog, &defaults);

  gtk_widget_destroy (dialog);
  g_free (defaults.kernel);

  return run;
}

static gboolean
validate_options (TargetMode     target,
                  OutputMode     output_mode,
                  gint           n_drawables,
                  GimpDrawable  *drawable,
                  GError       **error)
{
  if (target == TARGET_SELECTED_DRAWABLE)
    {
      if (n_drawables != 1)
        {
          g_set_error (error, GIMP_PLUG_IN_ERROR, 0,
                       "Procedure '%s' needs exactly one selected drawable for selected-drawable target.",
                       PLUG_IN_PROC);
          return FALSE;
        }

      if (! gimp_item_is_layer (GIMP_ITEM (drawable)))
        {
          g_set_error_literal (error, GIMP_PLUG_IN_ERROR, 0,
                               "Lanczos Scale currently supports selected layer drawables only.");
          return FALSE;
        }
    }

  if (target == TARGET_VISIBLE_IMAGE)
    {
      if (output_mode == OUTPUT_REPLACE_DRAWABLE)
        {
          g_set_error_literal (error, GIMP_PLUG_IN_ERROR, 0,
                               "Replace mode is only available for selected drawable target.");
          return FALSE;
        }

      if (n_drawables == 0 && output_mode != OUTPUT_NEW_IMAGE)
        {
          g_set_error_literal (error, GIMP_PLUG_IN_ERROR, 0,
                               "Visible-image target with no selected drawable must output to a new image.");
          return FALSE;
        }
    }

  if (output_mode == OUTPUT_REPLACE_DRAWABLE &&
      (! drawable || ! gimp_item_is_layer (GIMP_ITEM (drawable))))
    {
      g_set_error_literal (error, GIMP_PLUG_IN_ERROR, 0,
                           "Replace mode requires a selected layer drawable.");
      return FALSE;
    }

  return TRUE;
}

static GimpValueArray *
return_with_outputs (GimpProcedure    *procedure,
                     GimpPDBStatusType status,
                     GError           *error,
                     GimpImage        *image,
                     GimpLayer        *layer)
{
  GimpValueArray *return_vals;

  return_vals = gimp_procedure_new_return_values (procedure, status, error);

  if (status == GIMP_PDB_SUCCESS)
    {
      GIMP_VALUES_SET_IMAGE (return_vals, 1, image);
      GIMP_VALUES_SET_LAYER (return_vals, 2, layer);
    }

  return return_vals;
}

static GimpValueArray *
lanczos_scale_run (GimpProcedure        *procedure,
                   GimpRunMode           run_mode,
                   GimpImage            *image,
                   GimpDrawable        **drawables,
                   GimpProcedureConfig  *config,
                   gpointer              run_data G_GNUC_UNUSED)
{
  GimpDrawable       *selected_drawable = NULL;
  GimpDrawable       *source_drawable = NULL;
  GimpLayer          *visible_layer = NULL;
  GimpLayer          *output_layer = NULL;
  GimpImage          *output_image = NULL;
  GeglBuffer         *src_buffer = NULL;
  GeglBuffer         *dst_buffer = NULL;
  LanczosGimpFormat   format_info;
  TargetMode          target;
  OutputMode          output_mode;
  LanczosKernel       kernel;
  gboolean            linear_light;
  gchar              *output_name = NULL;
  gint                n_drawables;
  gint                src_width;
  gint                src_height;
  gint                dst_width;
  gint                dst_height;
  gboolean            undo_started = FALSE;
  GError             *error = NULL;

  gegl_init (NULL, NULL);

  n_drawables = gimp_core_object_array_get_length ((GObject **) drawables);
  if (n_drawables == 1)
    selected_drawable = drawables[0];
  else if (n_drawables > 1)
    {
      g_set_error (&error, GIMP_PLUG_IN_ERROR, 0,
                   "Procedure '%s' only works with zero or one selected drawable.",
                   gimp_procedure_get_name (procedure));
      return return_with_outputs (procedure, GIMP_PDB_CALLING_ERROR,
                                  error, NULL, NULL);
    }

  if (run_mode == GIMP_RUN_INTERACTIVE)
    {
      if (! run_dialog (procedure, config, image,
                        selected_drawable, n_drawables == 1))
        return return_with_outputs (procedure, GIMP_PDB_CANCEL,
                                    NULL, NULL, NULL);
    }

  target = gimp_procedure_config_get_choice_id (config, "target");
  output_mode = gimp_procedure_config_get_choice_id (config, "output-mode");
  kernel = gimp_procedure_config_get_choice_id (config, "kernel");

  g_object_get (config,
                "new-width", &dst_width,
                "new-height", &dst_height,
                "linear-light", &linear_light,
                "name", &output_name,
                NULL);

  if (! validate_options (target, output_mode, n_drawables,
                          selected_drawable, &error))
    {
      g_free (output_name);
      return return_with_outputs (procedure, GIMP_PDB_CALLING_ERROR,
                                  error, NULL, NULL);
    }

  if (target == TARGET_VISIBLE_IMAGE)
    {
      visible_layer = gimp_layer_new_from_visible (image, image,
                                                   "Lanczos Scale Source");
      if (! visible_layer)
        {
          g_set_error_literal (&error, GIMP_PLUG_IN_ERROR, 0,
                               "Could not create a source layer from the visible image.");
          g_free (output_name);
          return return_with_outputs (procedure, GIMP_PDB_EXECUTION_ERROR,
                                      error, NULL, NULL);
        }

      source_drawable = GIMP_DRAWABLE (visible_layer);
    }
  else
    {
      source_drawable = selected_drawable;
    }

  src_width = gimp_drawable_get_width (source_drawable);
  src_height = gimp_drawable_get_height (source_drawable);

  if (! lanczos_gimp_format_for_drawable (source_drawable,
                                          linear_light,
                                          &format_info,
                                          &error))
    {
      if (visible_layer)
        gimp_item_delete (GIMP_ITEM (visible_layer));
      g_free (output_name);
      return return_with_outputs (procedure, GIMP_PDB_CALLING_ERROR,
                                  error, NULL, NULL);
    }

  gimp_progress_init ("Lanczos Scale");

  if (output_mode == OUTPUT_NEW_IMAGE)
    {
      output_layer = create_new_image_with_layer (image,
                                                  source_drawable,
                                                  output_name,
                                                  dst_width,
                                                  dst_height,
                                                  &output_image);
      if (! output_layer)
        {
          g_set_error_literal (&error, GIMP_PLUG_IN_ERROR, 0,
                               "Could not create output image or layer.");
          goto execution_error;
        }

      src_buffer = gimp_drawable_get_buffer (source_drawable);
      dst_buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (output_layer));
    }
  else if (output_mode == OUTPUT_NEW_LAYER)
    {
      output_layer = create_output_layer (image,
                                          source_drawable,
                                          output_name,
                                          dst_width,
                                          dst_height);
      if (! output_layer)
        {
          g_set_error_literal (&error, GIMP_PLUG_IN_ERROR, 0,
                               "Could not create output layer.");
          goto execution_error;
        }

      if (! insert_layer_near_source (image,
                                      output_layer,
                                      selected_drawable))
        {
          g_set_error_literal (&error, GIMP_PLUG_IN_ERROR, 0,
                               "Could not insert output layer.");
          goto execution_error;
        }

      src_buffer = gimp_drawable_get_buffer (source_drawable);
      dst_buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (output_layer));
    }
  else
    {
      src_buffer = copy_source_buffer (source_drawable, src_width, src_height);

      gimp_image_undo_group_start (image);
      undo_started = TRUE;

      if (! gimp_layer_resize (GIMP_LAYER (source_drawable),
                               dst_width,
                               dst_height,
                               0,
                               0))
        {
          g_set_error_literal (&error, GIMP_PLUG_IN_ERROR, 0,
                               "Could not resize selected layer.");
          goto execution_error;
        }

      if (! gimp_layer_set_offsets (GIMP_LAYER (source_drawable), 0, 0))
        {
          g_set_error_literal (&error, GIMP_PLUG_IN_ERROR, 0,
                               "Could not move selected layer to the image origin.");
          goto execution_error;
        }

      if (! gimp_image_resize (image, dst_width, dst_height, 0, 0))
        {
          g_set_error_literal (&error, GIMP_PLUG_IN_ERROR, 0,
                               "Could not resize image canvas.");
          goto execution_error;
        }

      output_layer = GIMP_LAYER (source_drawable);
      dst_buffer = gimp_drawable_get_buffer (source_drawable);
    }

  if (! lanczos_gegl_resample (src_buffer,
                               dst_buffer,
                               src_width,
                               src_height,
                               dst_width,
                               dst_height,
                               &format_info,
                               kernel,
                               progress_cb,
                               NULL,
                               &error))
    goto execution_error;

  gimp_drawable_update (GIMP_DRAWABLE (output_layer),
                        0, 0, dst_width, dst_height);

  if (undo_started)
    {
      gimp_image_undo_group_end (image);
      undo_started = FALSE;
    }

  if (output_image)
    {
      gimp_image_undo_enable (output_image);

      if (run_mode != GIMP_RUN_NONINTERACTIVE)
        gimp_display_new (output_image);
    }
  else if (run_mode != GIMP_RUN_NONINTERACTIVE)
    {
      gimp_displays_flush ();
    }

  gimp_progress_end ();

  g_clear_object (&src_buffer);
  g_clear_object (&dst_buffer);

  if (visible_layer)
    gimp_item_delete (GIMP_ITEM (visible_layer));

  g_free (output_name);

  return return_with_outputs (procedure,
                              GIMP_PDB_SUCCESS,
                              NULL,
                              output_image ? output_image : image,
                              output_layer);

execution_error:
  if (undo_started)
    gimp_image_undo_group_end (image);

  g_clear_object (&src_buffer);
  g_clear_object (&dst_buffer);

  delete_failed_output (output_mode, output_image, output_layer);

  if (visible_layer)
    gimp_item_delete (GIMP_ITEM (visible_layer));

  gimp_progress_end ();
  g_free (output_name);

  return return_with_outputs (procedure,
                              GIMP_PDB_EXECUTION_ERROR,
                              error,
                              NULL,
                              NULL);
}
