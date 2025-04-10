<?php

/**
 * @file
 *  Export form to export content from a Backdrop site.
 */

/**
 * Form callback: Provides the content export form.
 *
 * @param array $form
 *   The form array.
 * @param array $form_state
 *   The form state array.
 *
 * @return array
 *   The complete form array.
 */
function bien_export_settings($form, &$form_state) {
  $form = array();
  $form['export'] = array(
    '#type' => 'fieldset',
    '#title' => t('Export Content'),
  );
  
  $types = node_type_get_types();
  $type_options = array();
  
  foreach ($types as $type) {
    $type_options[$type->type] = $type->name;
  }

  $form['export']['content'] = array(
    '#type' => 'checkboxes',
    '#title' => t('Content Types'),
    '#options' => $type_options,
    '#default_value' => array_keys($type_options),
    '#description' => t('Select the content types you would like to export.'),
    '#required' => TRUE,
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
 * Submit handler for the content export form.
 *
 * @param array $form
 *   The form array.
 * @param array $form_state
 *   The form state array.
 */
function bien_export_settings_submit(array $form, array &$form_state) {
  $export_data = array(
    'content_types' => array(),
    'nodes' => array(),
  );

  // Get selected content types
  $selected_types = array();
  if ($form_state['values']['content']) {
    foreach ($form_state['values']['content'] as $type) {
      if ($type) {
        $selected_types[] = $type;
      }
    }
  }

  // Export content type definitions
  foreach ($selected_types as $type) {
    $content_type = node_type_get_type($type);
    if ($content_type) {
      $export_data['content_types'][$type] = array(
        'name' => $content_type->name,
        'type' => $content_type->type,
        'description' => $content_type->description,
        'help' => $content_type->help,
        'title_label' => $content_type->title_label,
        'base' => $content_type->base,
        'custom' => isset($content_type->custom) ? $content_type->custom : FALSE,
        'modified' => $content_type->modified,
        'locked' => isset($content_type->locked) ? $content_type->locked : FALSE,
        'disabled' => $content_type->disabled,
        'module' => $content_type->module,
        'node_preview' => $content_type->node_preview,
        'orig_type' => $content_type->orig_type,
      );

      // Export field instances for this content type
      $field_instances = field_info_instances('node', $type);
      $export_data['content_types'][$type]['field_instances'] = $field_instances;

      // Export field configurations
      $field_configs = array();
      foreach ($field_instances as $field_name => $instance) {
        $field_config = field_info_field($field_name);
        if ($field_config) {
          $field_configs[$field_name] = $field_config;
        }
      }
      $export_data['content_types'][$type]['field_configs'] = $field_configs;
    }
  }

  // Export nodes
  $query = db_select('node', 'n')
    ->fields('n', array('nid'));
  $or = db_or();
  foreach ($selected_types as $type) {
    $or->condition('type', $type);
  }
  $query->condition($or);
  $result = $query->orderBy('nid')->execute();
  $nids = [];
  foreach($result as $r) {
    $nids[] = $r->nid;
  }
  $nodes = node_load_multiple($nids);
  $export_data['nodes'] = $nodes;

  // Create a temporary directory for the export
  $temp_dir = 'temporary://bien_export_' . time();
  file_prepare_directory($temp_dir, FILE_CREATE_DIRECTORY);

  // Save the JSON data
  $json_file = $temp_dir . '/content.json';
  file_put_contents($json_file, json_encode($export_data, JSON_PRETTY_PRINT));

  // Create a files directory
  $files_dir = $temp_dir . '/files';
  file_prepare_directory($files_dir, FILE_CREATE_DIRECTORY);

  // Track exported files to avoid duplicates
  $exported_files = array();
  $file_count = 0;
  $error_count = 0;

  // Export associated files
  foreach ($nodes as $node) {
    $fields = field_info_instances('node', $node->type);
    foreach ($fields as $field_name => $instance) {
      $field_info = field_info_field($field_name);
      if ($field_info['type'] === 'file' || $field_info['type'] === 'image') {
        $items = field_get_items('node', $node, $field_name);
        if ($items) {
          foreach ($items as $item) {
            if (isset($item['fid'])) {
              // Skip if we've already exported this file
              if (isset($exported_files[$item['fid']])) {
                continue;
              }

              $file = file_load($item['fid']);
              if ($file) {
                // Validate file exists and is readable
                $source = backdrop_realpath($file->uri);
                if ($source && file_exists($source) && is_readable($source)) {
                  $dest = $files_dir . '/' . $file->fid . '_' . $file->filename;
                  
                  // Copy the file
                  if (copy($source, $dest)) {
                    // Store file metadata
                    $exported_files[$item['fid']] = array(
                      'fid' => $file->fid,
                      'filename' => $file->filename,
                      'uri' => $file->uri,
                      'filemime' => $file->filemime,
                      'filesize' => $file->filesize,
                      'status' => $file->status,
                      'timestamp' => $file->timestamp,
                    );
                    $file_count++;
                  } else {
                    backdrop_set_message(t('Warning: Could not copy file @file.', array('@file' => $file->filename)), 'warning');
                    $error_count++;
                  }
                } else {
                  $error_count++;
                }
              }
            }
          }
        }
      }
    }
  }

  // Save file metadata to a separate JSON file
  $files_json = $temp_dir . '/files.json';
  file_put_contents($files_json, json_encode($exported_files, JSON_PRETTY_PRINT));

  // Create a zip file
  $zip_file = 'public://content-export.zip';
  $zip = new ZipArchive();
  if ($zip->open(backdrop_realpath($zip_file), ZipArchive::CREATE | ZipArchive::OVERWRITE) === TRUE) {
    // Add the JSON files
    $json_realpath = backdrop_realpath($json_file);
    $files_json_realpath = backdrop_realpath($files_json);
    
    if (file_exists($json_realpath)) {
      $zip->addFile($json_realpath, 'content.json');
    } else {
      backdrop_set_message(t('Error: Could not create content.json file.'), 'error');
    }
    
    if (file_exists($files_json_realpath)) {
      $zip->addFile($files_json_realpath, 'files.json');
    }
    
    // Add all files from the files directory
    $files = file_scan_directory($files_dir, '/.*/');
    foreach ($files as $file) {
      $file_realpath = backdrop_realpath($file->uri);
      if (file_exists($file_realpath)) {
        $zip->addFile($file_realpath, 'files/' . basename($file->uri));
      } else {
        $error_count++;
      }
    }
    
    $zip->close();

    // Show summary message
    if ($error_count > 0) {
      backdrop_set_message(t('Export completed with @errors error(s). Some files may be missing from the export.', array(
        '@errors' => $error_count
      )), 'warning');
    } else {
      backdrop_set_message(t('Export completed successfully. @count files included.', array(
        '@count' => $file_count
      )));
    }
  } else {
    backdrop_set_message(t('Error: Could not create ZIP file.'), 'error');
  }

  // Clean up temporary directory
  file_unmanaged_delete_recursive($temp_dir);

  $form_state['redirect'] = 'admin/config/system/content/export-content-download';
}

/**
 * Downloads the exported content.
 */
function content_download_full_export() {
  $headers = array(
    'Content-Disposition' => 'attachment; filename=content-export.zip',
    'Content-type' => 'application/zip',
  );
  file_transfer('public://content-export.zip', $headers);
  file_unmanaged_delete('public://content-export.zip');
}
