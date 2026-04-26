#include <string.h>

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
lanczos_scale_init (LanczosScale *self)
{
  (void) self;
}

static GList *
lanczos_scale_query_procedures (GimpPlugIn *plug_in)
{
  (void) plug_in;

  return g_list_append (NULL, g_strdup (PLUG_IN_PROC));
}

static gboolean
lanczos_scale_set_i18n (GimpPlugIn  *plug_in,
                        const gchar *procedure_name,
                        gchar      **gettext_domain,
                        gchar      **catalog_dir)
{
  (void) plug_in;
  (void) procedure_name;
  (void) gettext_domain;
  (void) catalog_dir;

  return FALSE;
}

static GimpProcedure *
lanczos_scale_create_procedure (GimpPlugIn  *plug_in,
                                const gchar *name)
{
  GimpProcedure *procedure = NULL;

  if (strcmp (name, PLUG_IN_PROC) == 0)
    {
      procedure = gimp_image_procedure_new (plug_in,
                                            name,
                                            GIMP_PDB_PROC_TYPE_PLUGIN,
                                            lanczos_scale_run,
                                            NULL,
                                            NULL);

      gimp_procedure_set_image_types (procedure, "RGB*, GRAY*");
      gimp_procedure_set_sensitivity_mask (procedure,
                                           GIMP_PROCEDURE_SENSITIVE_DRAWABLE |
                                           GIMP_PROCEDURE_SENSITIVE_NO_DRAWABLES);
      gimp_procedure_set_menu_label (procedure, "_Lanczos Scale...");
      gimp_procedure_add_menu_path (procedure, "<Image>/Image/[Scale]");
      gimp_procedure_set_documentation (procedure,
                                        "Scale a drawable or visible image with a custom Lanczos resampler",
                                        "Scales the selected layer drawable or the visible image projection using a separable Lanczos2 or Lanczos3 filter.",
                                        PLUG_IN_PROC);
      gimp_procedure_set_attribution (procedure,
                                      "OpenAI Codex",
                                      "OpenAI Codex",
                                      "2026");

      gimp_procedure_add_choice_argument (procedure, "target",
                                          "Target",
                                          "Pixels to scale",
                                          gimp_choice_new_with_values ("selected-drawable", TARGET_SELECTED_DRAWABLE, "Selected drawable", NULL,
                                                                       "visible-image",     TARGET_VISIBLE_IMAGE,     "Visible image",     NULL,
                                                                       NULL),
                                          "selected-drawable",
                                          G_PARAM_READWRITE);

      gimp_procedure_add_int_argument (procedure, "new-width",
                                       "New width",
                                       "Scaled output width in pixels",
                                       1, GIMP_MAX_IMAGE_SIZE, 1,
                                       G_PARAM_READWRITE);

      gimp_procedure_add_int_argument (procedure, "new-height",
                                       "New height",
                                       "Scaled output height in pixels",
                                       1, GIMP_MAX_IMAGE_SIZE, 1,
                                       G_PARAM_READWRITE);

      gimp_procedure_add_unit_aux_argument (procedure, "size-unit",
                                            "Size unit",
                                            "Output size unit of measure",
                                            TRUE, TRUE, gimp_unit_pixel (),
                                            G_PARAM_READWRITE);

      gimp_procedure_add_choice_argument (procedure, "kernel",
                                          "Kernel",
                                          "Lanczos kernel radius",
                                          gimp_choice_new_with_values ("lanczos3", LANCZOS_KERNEL_3, "Lanczos3", NULL,
                                                                       "lanczos2", LANCZOS_KERNEL_2, "Lanczos2", NULL,
                                                                       NULL),
                                          "lanczos3",
                                          G_PARAM_READWRITE);

      gimp_procedure_add_choice_argument (procedure, "output-mode",
                                          "Output",
                                          "Where to write the scaled result",
                                          gimp_choice_new_with_values ("new-layer",        OUTPUT_NEW_LAYER,        "New layer",        NULL,
                                                                       "replace-drawable", OUTPUT_REPLACE_DRAWABLE, "Replace drawable", NULL,
                                                                       "new-image",        OUTPUT_NEW_IMAGE,        "New image",        NULL,
                                                                       NULL),
                                          "new-layer",
                                          G_PARAM_READWRITE);

      gimp_procedure_add_boolean_argument (procedure, "linear-light",
                                           "Linear light",
                                           "Resample in linear-light floating point",
                                           TRUE,
                                           G_PARAM_READWRITE);

      gimp_procedure_add_string_argument (procedure, "name",
                                          "Name",
                                          "Output layer name",
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
      g_object_unref (profile);
    }

  if (gimp_image_get_resolution (src, &xres, &yres))
    gimp_image_set_resolution (dst, xres, yres);

  unit = gimp_image_get_unit (src);
  if (unit)
    gimp_image_set_unit (dst, unit);

  return TRUE;
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
progress_cb (double fraction,
             void  *data)
{
  (void) data;

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

  g_object_unref (src);

  return copy;
}

static gboolean
run_dialog (GimpProcedure       *procedure,
            GimpProcedureConfig *config,
            GimpImage           *image,
            GimpDrawable        *drawable,
            gboolean             has_drawable)
{
  GtkWidget *dialog;
  gint       width;
  gint       height;
  gdouble    xres;
  gdouble    yres;
  gboolean   run;

  gimp_ui_init (PLUG_IN_BINARY);

  if (has_drawable)
    {
      width = gimp_drawable_get_width (drawable);
      height = gimp_drawable_get_height (drawable);

      g_object_set (config,
                    "target", "selected-drawable",
                    "output-mode", "new-layer",
                    NULL);
    }
  else
    {
      width = gimp_image_get_width (image);
      height = gimp_image_get_height (image);

      g_object_set (config,
                    "target", "visible-image",
                    "output-mode", "new-image",
                    NULL);
    }

  gimp_image_get_resolution (image, &xres, &yres);
  g_object_set (config,
                "new-width", width,
                "new-height", height,
                NULL);

  dialog = gimp_procedure_dialog_new (procedure,
                                      config,
                                      "Lanczos Scale");

  gimp_procedure_dialog_get_label (GIMP_PROCEDURE_DIALOG (dialog),
                                   "size-label",
                                   "Output Size",
                                   FALSE,
                                   FALSE);

  gimp_procedure_dialog_get_coordinates (GIMP_PROCEDURE_DIALOG (dialog),
                                         "coordinates",
                                         "new-width",
                                         "new-height",
                                         "size-unit",
                                         "%a",
                                         GIMP_SIZE_ENTRY_UPDATE_SIZE,
                                         xres,
                                         yres);

  gimp_procedure_dialog_fill_frame (GIMP_PROCEDURE_DIALOG (dialog),
                                    "size-frame",
                                    "size-label",
                                    FALSE,
                                    "coordinates");

  gimp_procedure_dialog_fill (GIMP_PROCEDURE_DIALOG (dialog),
                              "target",
                              "size-frame",
                              "kernel",
                              "output-mode",
                              "linear-light",
                              "name",
                              NULL);

  run = gimp_procedure_dialog_run (GIMP_PROCEDURE_DIALOG (dialog));

  gtk_widget_destroy (dialog);

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
                   gpointer              run_data)
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

  (void) run_data;

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

  if (src_buffer)
    g_object_unref (src_buffer);
  if (dst_buffer)
    g_object_unref (dst_buffer);

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

  if (src_buffer)
    g_object_unref (src_buffer);
  if (dst_buffer)
    g_object_unref (dst_buffer);

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
