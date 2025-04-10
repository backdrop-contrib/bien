<?php

/**
 * @file
 * Import form to import content from another Backdrop or Wordpress site.
 */

/**
 * Callback function for /admin/config/import.
 */
function bien_import_settings($form, &$form_state) {
  $form = array();
  $form['import'] = array(
    '#type' => 'fieldset',
    '#title' => t('Import Content'),
  );
  // TODO: Figure out if we can autodetect the type of impoort.
  // $form['import']['type-of-import'] = array(
  //   '#type' => 'select',
  //   '#title' => 'Source of import data:',
  //   '#description' => 'Select the source of the import i.e. the data came from a Backdrop site.',
  //   '#options' => array(
  //     'backdrop' => 'Backdrop CMS',
  //     'wordrpress' => 'WordPress',
  //   ),
  // );
  $form['import']['import-file'] = array(
    '#type' => 'file',
    '#title' => 'Upload a ZIP file:',
    '#description' => 'Upload a ZIP file containing content export from a Backdrop site.',
  );
  $form['import']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Import Content'),
    '#attributes' => array(
      'class' => array('button-primary'),
    ),
    '#submit' => array('bien_import_settings_submit'),
  );

  return $form;
}

/**
 * Validate handler for bien_import_settings().
 */
function bien_import_settings_validate($form, &$form_state) {
  $file = file_save_upload('import-file', array(
    // Validate extensions.
    'file_validate_extensions' => array('zip'),
  ));

  // If the file passed validation:
  if ($file) {
    // Move the file into the backdrop file system.
    if ($file = file_move($file, 'public://')) {
      // Save the file for use in the submit handler.
      $form_state['storage']['import-file'] = $file;
    }
    else {
      form_set_error('import-file',
        t("Failed to write the uploaded file to the site's file folder.")
      );
    }
  }
  else {
    form_set_error('import-file', t('The import could not be completed.'));
  }
}

/**
 * Submit handler for the bien_import_settings form.
 */
function bien_import_settings_submit(&$form, &$form_state) {
  $file = $form_state['storage']['import-file'];
  // We are done with the file, remove it from storage.
  unset($form_state['storage']['import-file']);
  
  // Validate file exists and is readable
  if (!isset($file->uri) || !file_exists($file->uri)) {
    backdrop_set_message(t('Error: Import file not found or not readable.'), 'error');
    return;
  }

  // Create a temporary directory for extraction
  $temp_dir = 'temporary://bien_import_' . time();
  file_prepare_directory($temp_dir, FILE_CREATE_DIRECTORY);

  // Extract the zip file
  $zip = new ZipArchive();
  if ($zip->open(backdrop_realpath($file->uri)) === TRUE) {
    $zip->extractTo(backdrop_realpath($temp_dir));
    $zip->close();
  }
  else {
    backdrop_set_message(t('Error: Could not extract the ZIP file.'), 'error');
    return;
  }

  // Read the JSON file
  $json_file = $temp_dir . '/content.json';
  if (!file_exists($json_file)) {
    backdrop_set_message(t('Error: content.json not found in the ZIP file.'), 'error');
    return;
  }

  $content = file_get_contents($json_file);
  if ($content === FALSE) {
    backdrop_set_message(t('Error: Could not read the content.json file.'), 'error');
    return;
  }

  $content = backdrop_json_decode($content);
  if ($content === NULL) {
    backdrop_set_message(t('Error: The content.json file is not valid JSON.'), 'error');
    return;
  }

  // Process files directory if it exists
  $files_dir = $temp_dir . '/files';
  if (file_exists($files_dir)) {
    $files = file_scan_directory($files_dir, '/.*/');
    foreach ($files as $file) {
      // Ensure file URI exists and is not null
      if (!isset($file->uri) || empty($file->uri)) {
        backdrop_set_message(t('Warning: Skipping file with invalid URI.'), 'warning');
        continue;
      }

      // Extract file ID from filename (format: fid_filename)
      $basename = basename($file->uri);
      if (empty($basename)) {
        backdrop_set_message(t('Warning: Skipping file with invalid basename.'), 'warning');
        continue;
      }

      $parts = explode('_', $basename, 2);
      if (count($parts) === 2) {
        $fid = $parts[0];
        $filename = $parts[1];
        
        // Validate filename
        if (empty($filename)) {
          backdrop_set_message(t('Warning: Skipping file with empty filename.'), 'warning');
          continue;
        }
        
        // Create a new file entry
        $new_file = new File();
        $new_file->uri = 'public://' . $filename;
        $new_file->filename = $filename;
        $new_file->filemime = file_get_mimetype($filename);
        $new_file->uid = $GLOBALS['user']->uid;
        $new_file->status = FILE_STATUS_PERMANENT;
        
        // Ensure source file exists and is readable
        if (!file_exists($file->uri) || !is_readable($file->uri)) {
          backdrop_set_message(t('Warning: Source file @file does not exist or is not readable.', array('@file' => $file->uri)), 'warning');
          continue;
        }

        // Ensure destination directory exists
        $dest_dir = dirname(backdrop_realpath($new_file->uri));
        if (!file_exists($dest_dir)) {
          file_prepare_directory($dest_dir, FILE_CREATE_DIRECTORY);
        }
        
        // Copy the file to the public directory
        if (copy($file->uri, backdrop_realpath($new_file->uri))) {
          file_save($new_file);
          // Store the mapping of old FID to new FID
          $form_state['storage']['file_map'][$fid] = $new_file->fid;
        } else {
          backdrop_set_message(t('Warning: Failed to copy file @file.', array('@file' => $filename)), 'warning');
        }
      }
    }
  }

  // Check if this is a new format export with content types
  if (isset($content['content_types']) && isset($content['nodes'])) {
    // Import content types first
    foreach ($content['content_types'] as $type => $type_data) {
      // Check if content type exists
      if (!node_type_get_type($type)) {
        // Create the content type
        $content_type = new stdClass();
        foreach ($type_data as $key => $value) {
          if ($key !== 'field_instances' && $key !== 'field_configs') {
            $content_type->$key = $value;
          }
        }
        node_type_save($content_type);

        // Inform about the new content type creation
        backdrop_set_message(t('Created new content type: @name (@type)', array(
          '@name' => $content_type->name,
          '@type' => $content_type->type
        )));

        // Grant admin role all permissions for this content type
        $admin_rid = config_get('system.core', 'user_admin_role');
        if ($admin_rid) {
          $permissions = array(
            "create {$type} content" => TRUE,
            "edit any {$type} content" => TRUE,
            "edit own {$type} content" => TRUE,
            "delete any {$type} content" => TRUE,
            "delete own {$type} content" => TRUE,
          );
          user_role_change_permissions($admin_rid, $permissions);
          backdrop_set_message(t('Granted administrator permissions for content type: @type', array('@type' => $content_type->name)));
        }

        // Import field configurations
        if (isset($type_data['field_configs'])) {
          foreach ($type_data['field_configs'] as $field_name => $field_config) {
            if (!field_info_field($field_name)) {
              field_create_field($field_config);
            }
          }
        }

        // Import field instances
        if (isset($type_data['field_instances'])) {
          foreach ($type_data['field_instances'] as $field_name => $instance) {
            if (!field_info_instance('node', $field_name, $type)) {
              field_create_instance($instance);
            }
          }
        }

        // Clear the menu cache to make the new content type available
        menu_rebuild();
      }
    }

    // Now import the nodes
    $content = $content['nodes'];
  }

  // Save as new content nodes.
  foreach ($content as $c) {
    if(isset($c['nid'])) {
      unset($c['nid']);
    }
    if (isset($c['vid'])) {
      unset($c['vid']);
    }

    // Validate that the content type exists
    if (!isset($c['type']) || !node_type_get_type($c['type'])) {
      backdrop_set_message(t('Skipping node: Content type @type does not exist.', array('@type' => isset($c['type']) ? $c['type'] : 'undefined')), 'error');
      continue;
    }

    // Create a new node using the proper Node class
    $node = entity_create('node', array(
      'type' => $c['type'],
      'title' => isset($c['title']) ? $c['title'] : '',
      'language' => LANGUAGE_NONE,
      'uid' => $GLOBALS['user']->uid,
      'status' => 1,
      'path' => array(
        'alias' => isset($c['path']['alias']) ? $c['path']['alias'] : '',
      ),
    ));
    
    // Get the content type settings
    $content_type = node_type_get_type($node->type);
    if (!$content_type) {
      backdrop_set_message(t('Skipping node: Could not load content type settings for @type', array('@type' => $node->type)), 'error');
      continue;
    }
    
    // Copy all other properties from the imported content
    foreach ($c as $key => $value) {
      if (!in_array($key, array('nid', 'vid', 'type', 'title', 'language', 'uid', 'status', 'path'))) {
        // Handle file fields specially
        if (strpos($key, 'field_') === 0) {
          $field_info = field_info_field($key);
          if ($field_info && ($field_info['type'] === 'file' || $field_info['type'] === 'image')) {
            // Skip if value is null or empty
            if (empty($value)) {
              continue;
            }

            // Update file IDs using our mapping
            if (is_array($value)) {
              $valid_items = array();
              foreach ($value as $lang => $items) {
                if (is_array($items)) {
                  $valid_items[$lang] = array();
                  foreach ($items as $delta => $item) {
                    // Skip if item is null or doesn't have required properties
                    if (empty($item) || !isset($item['fid'])) {
                      continue;
                    }

                    // Skip if we don't have a mapping for this file
                    if (!isset($form_state['storage']['file_map'][$item['fid']])) {
                      backdrop_set_message(t('Warning: File ID @fid in field @field was not included in the import.', array('@fid' => $item['fid'], '@field' => $key)), 'warning');
                      continue;
                    }

                    // Create a new file item with only the necessary properties
                    $new_item = array(
                      'fid' => $form_state['storage']['file_map'][$item['fid']],
                      'display' => isset($item['display']) ? $item['display'] : 1,
                      'description' => isset($item['description']) ? $item['description'] : '',
                    );

                    // Only add URI if we have a valid file
                    if (isset($item['uri']) && !empty($item['uri'])) {
                      $new_item['uri'] = $item['uri'];
                    }

                    $valid_items[$lang][] = $new_item;
                  }
                }
              }
              // Only set the field value if we have valid items
              if (!empty($valid_items)) {
                $value = $valid_items;
              } else {
                continue; // Skip this field entirely if no valid items
              }
            }
          }
        }
        $node->$key = $value;
      }
    }
    
    // Ensure required fields are present
    $required_fields = field_info_instances('node', $node->type);
    $missing_fields = array();
    foreach ($required_fields as $field_name => $instance) {
      if ($instance['required'] && (!isset($node->$field_name) || empty($node->$field_name))) {
        $missing_fields[] = $field_name;
      }
    }
    
    if (!empty($missing_fields)) {
      backdrop_set_message(t('Skipping node: Missing required fields: @fields', array('@fields' => implode(', ', $missing_fields))), 'error');
      continue;
    }
    
    try {
      $node->save();
    }
    catch (Exception $e) {
      backdrop_set_message(t('Error saving node: @error', array('@error' => $e->getMessage())), 'error');
    }
  }

  // Clean up temporary directory
  file_unmanaged_delete_recursive($temp_dir);

  // Give a message to the user.
  backdrop_set_message(
    t(
      'Import completed successfully. @node_count nodes were imported with @file_count associated files. Visit the <a href="@content_url">content listing</a> to view the imported content.',
      array(
        '@node_count' => count($content),
        '@file_count' => count($form_state['storage']['file_map']),
        '@content_url' => url('admin/content')
      )
    )
  );
}
