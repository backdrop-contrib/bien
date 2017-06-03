<?php

/**
 * @file
 *  Export form to export content from a Backdrop site.
 */

/**
 * Callback function for /admin/config/import.
 */
function bien_export_settings($form, &$form_state) {
  $form = array();
  $form['export'] = array(
    '#type' => 'fieldset',
    '#title' => t('Export Content'),
  );
  $types = node_type_get_types();

  // Prepare options array.
  foreach ($types as $type) {
    $type_options[$type->type] = $type->name;
  }

  $form['export']['content'] = array(
    '#type' => 'checkboxes',
    '#options' => $type_options,
    '#attributes' => array(
      'checked' => 'checked',
    ),
    '#default_value' => array_keys($type_options),
    '#description' => t('Check the content types you would like to export.'),
  );
  $form['export']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Export Content'),
    '#attributes' => array(
      'class' => array('button-primary'),
    ),
    '#submit' => array('bien_export_settings_submit'),
  );

  return $form;
}

/**
 * Submit handler for config_export_full_form().
 */
function bien_export_settings_submit(array $form, array &$form_state) {
  $query = db_select('node', 'n')
    ->fields('n', array('nid'));
  $or = db_or();
  if ($form_state['values']['content']) {
    foreach ($form_state['values']['content'] as $type) {
      if ($type) {
        $or->condition('type', $type);
      }
    }
  }
  $query->condition($or);
  $result = $query->orderBy('nid')->execute();
  $nids = [];
  foreach($result as $r) {
    $nids[] = $r->nid;
  }
  $nodes = node_load_multiple($nids);
  $content = json_encode($nodes);

  file_unmanaged_save_data($content, 'public://content-export.json', FILE_EXISTS_REPLACE);

  $form_state['redirect'] = 'admin/config/system/content/export-content-download';
}

/**
 * Downloads a tarball of the site configuration.
 */
function content_download_full_export() {
  $headers = array(
    'Content-Disposition' => 'attachment; filename=content-export.json',
    'Content-type' => 'application/json',
  );
  file_transfer('public://content-export.json', $headers);
  file_unmanaged_delete('public://content-export.json');
}
