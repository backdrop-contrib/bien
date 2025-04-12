<?php

/**
 * @file
 * Import form to import content from another Backdrop or WordPress site.
 */

/**
 * Form callback: Provides the content import form.
 *
 * @param array $form
 *   The form array.
 * @param array $form_state
 *   The form state array.
 *
 * @return array
 *   The complete form array.
 */
function bien_import_settings($form, &$form_state) {
  $form = array();
  $form['import'] = array(
    '#type' => 'fieldset',
    '#title' => t('Import Content'),
  );
  // @todo Implement auto-detection of import source type.
  $form['import']['import-file'] = array(
    '#type' => 'file',
    '#title' => t('Upload a ZIP file'),
    '#description' => t('Upload a ZIP file containing content export from a Backdrop site.'),
    '#upload_validators' => array(
      'file_validate_extensions' => array('zip'),
      'file_validate_size' => array(file_upload_max_size()),
    ),
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
 * Validate handler for the content import form.
 *
 * @param array $form
 *   The form array.
 * @param array $form_state
 *   The form state array.
 */
function bien_import_settings_validate($form, &$form_state) {
  $validators = array(
    'file_validate_extensions' => array('zip'),
    'file_validate_size' => array(file_upload_max_size()),
  );

  $file = file_save_upload('import-file', $validators);

  if ($file) {
    if ($file = file_move($file, 'public://')) {
      $form_state['storage']['import-file'] = $file;
    }
    else {
      form_set_error(
        'import-file',
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

        // Check if we already have a mapping for this file ID
        if (isset($form_state['storage']['file_map'][$fid])) {
          continue;
        }

        // Check if file with this name already exists
        $existing_files = file_load_multiple(array(), array('filename' => $filename));
        if (!empty($existing_files)) {
          // Use the first matching file
          $existing_file = reset($existing_files);
          $form_state['storage']['file_map'][$fid] = $existing_file->fid;
          continue;
        }
        
        // Create a new file entry
        $new_file = new File();
        // Generate a unique filename by adding a timestamp
        $unique_filename = time() . '_' . $filename;
        $new_file->uri = 'public://' . $unique_filename;
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
    // First import taxonomy vocabularies and terms
    if (isset($content['taxonomy'])) {
      // Create a mapping of old term IDs to new term IDs
      $term_id_map = array();
      
      // Import vocabularies first
      if (isset($content['taxonomy']['vocabularies'])) {
        foreach ($content['taxonomy']['vocabularies'] as $machine_name => $vocab_data) {
          // Check if vocabulary exists
          $existing_vocab = taxonomy_vocabulary_machine_name_load($machine_name);
          if (!$existing_vocab) {
            // Create new vocabulary using the proper class
            $vocabulary = new TaxonomyVocabulary();
            $vocabulary->name = $vocab_data['name'];
            $vocabulary->machine_name = $vocab_data['machine_name'];
            $vocabulary->description = $vocab_data['description'];
            $vocabulary->hierarchy = $vocab_data['hierarchy'];
            $vocabulary->weight = $vocab_data['weight'];
            if (isset($vocab_data['language'])) {
              $vocabulary->language = $vocab_data['language'];
            }
            taxonomy_vocabulary_save($vocabulary);
            backdrop_set_message(t('Created new vocabulary: @name', array('@name' => $vocabulary->name)));
          }
        }
      }

      // Import terms
      if (isset($content['taxonomy']['terms'])) {
        // Create a mapping of old term IDs to new term IDs
        $term_id_map = array();
        $term_count = 0;
        
        // First pass: Create all terms without parents
        foreach ($content['taxonomy']['terms'] as $vocabulary_machine_name => $terms) {
          foreach ($terms as $old_tid => $term_data) {
            if (!isset($term_data['parent']) || $term_data['parent'] == 0) {
              $vocabulary = taxonomy_vocabulary_machine_name_load($vocabulary_machine_name);
              if ($vocabulary) {
                $term = new TaxonomyTerm();
                $term->name = $term_data['name'];
                $term->description = $term_data['description'];
                $term->format = $term_data['format'];
                $term->weight = $term_data['weight'];
                $term->vocabulary = $vocabulary->machine_name;
                $term->parent = array(0);
                if (isset($term_data['language'])) {
                  $term->language = $term_data['language'];
                }
                taxonomy_term_save($term);
                $term_id_map[$old_tid] = $term->tid;
                $term_count++;
              }
            }
          }
        }

        // Second pass: Create terms with parents
        foreach ($content['taxonomy']['terms'] as $vocabulary_machine_name => $terms) {
          foreach ($terms as $old_tid => $term_data) {
            if (isset($term_data['parent']) && $term_data['parent'] != 0 && !isset($term_id_map[$old_tid])) {
              $vocabulary = taxonomy_vocabulary_machine_name_load($vocabulary_machine_name);
              if ($vocabulary) {
                $term = new TaxonomyTerm();
                $term->name = $term_data['name'];
                $term->description = $term_data['description'];
                $term->format = $term_data['format'];
                $term->weight = $term_data['weight'];
                $term->vocabulary = $vocabulary->machine_name;
                $term->parent = array(isset($term_id_map[$term_data['parent']]) ? $term_id_map[$term_data['parent']] : 0);
                if (isset($term_data['language'])) {
                  $term->language = $term_data['language'];
                }
                taxonomy_term_save($term);
                $term_id_map[$old_tid] = $term->tid;
                $term_count++;
              }
            }
          }
        }
        
        if ($term_count > 0) {
          backdrop_set_message(t('Created @count new taxonomy terms.', array('@count' => $term_count)));
        }
      }
    }

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

        // Add UUID field if it doesn't exist
        if (!field_info_field('field_uuid')) {
          $uuid_field = array(
            'field_name' => 'field_uuid',
            'type' => 'text',
            'cardinality' => 1,
            'locked' => TRUE,
            'settings' => array(
              'max_length' => 36,
            ),
          );
          field_create_field($uuid_field);
        }

        // Import field instances
        if (isset($type_data['field_instances'])) {
          foreach ($type_data['field_instances'] as $field_name => $instance) {
            if (!field_info_instance('node', $field_name, $type)) {
              field_create_instance($instance);
            }
          }
        }

        // Add UUID field instance if it doesn't exist
        if (!field_info_instance('node', 'field_uuid', $type)) {
          $uuid_instance = array(
            'field_name' => 'field_uuid',
            'entity_type' => 'node',
            'bundle' => $type,
            'label' => 'UUID',
            'description' => 'Unique identifier for this content',
            'required' => FALSE,
            'widget' => array(
              'type' => 'text_textfield',
              'settings' => array(
                'size' => 60,
              ),
            ),
            'display' => array(
              'default' => array(
                'label' => 'above',
                'type' => 'text_default',
              ),
            ),
          );
          field_create_instance($uuid_instance);
        }

        // Clear the menu cache to make the new content type available
        menu_rebuild();
      }
    }

    // Now import the nodes
    $content = $content['nodes'];
  }

  // First pass: Create a mapping of UUIDs to new NIDs
  $uuid_to_nid = array();
  foreach ($content as $c) {
    if (isset($c['uuid'])) {
      $uuid_to_nid[$c['uuid']] = NULL; // Will be populated after node creation
    }
  }

  // Second pass: Import nodes and update the mapping
  $node_count = 0;
  $skipped_count = 0;
  foreach ($content as $c) {
    if(isset($c['nid'])) {
      unset($c['nid']);
    }
    if (isset($c['vid'])) {
      unset($c['vid']);
    }

    // Check if node with this UUID already exists
    if (isset($c['uuid']) && bien_entity_exists_by_uuid('node', $c['uuid'])) {
      $skipped_count++;
      continue;
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
          // Handle node reference fields
          elseif ($field_info && $field_info['type'] === 'node_reference') {
            if (is_array($value)) {
              $valid_items = array();
              foreach ($value as $lang => $items) {
                if (is_array($items)) {
                  $valid_items[$lang] = array();
                  foreach ($items as $delta => $item) {
                    if (isset($item['nid'])) {
                      // Find the original node to get its UUID
                      $original_node = NULL;
                      foreach ($content as $original) {
                        if (isset($original['nid']) && $original['nid'] == $item['nid']) {
                          $original_node = $original;
                          break;
                        }
                      }
                      
                      if ($original_node && isset($original_node['uuid']) && isset($uuid_to_nid[$original_node['uuid']])) {
                        $valid_items[$lang][] = array('nid' => $uuid_to_nid[$original_node['uuid']]);
                      }
                    }
                  }
                }
              }
              if (!empty($valid_items)) {
                $value = $valid_items;
              } else {
                continue;
              }
            }
          }
          // Handle taxonomy term reference fields
          elseif ($field_info && $field_info['type'] === 'taxonomy_term_reference') {
            if (is_array($value)) {
              $valid_items = array();
              foreach ($value as $lang => $items) {
                if (is_array($items)) {
                  $valid_items[$lang] = array();
                  foreach ($items as $delta => $item) {
                    if (isset($item['tid'])) {
                      // Update term ID using our mapping
                      if (isset($term_id_map[$item['tid']])) {
                        $valid_items[$lang][] = array('tid' => $term_id_map[$item['tid']]);
                      }
                    }
                  }
                }
              }
              if (!empty($valid_items)) {
                $value = $valid_items;
              } else {
                continue;
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
      $node_count++;
      // Update the UUID to NID mapping
      if (isset($c['uuid'])) {
        $uuid_to_nid[$c['uuid']] = $node->nid;
        // Store the UUID in the field
        $node->field_uuid[LANGUAGE_NONE][0]['value'] = $c['uuid'];
        $node->save();
      }
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
      'Import completed successfully. @node_count new nodes were imported (@skipped_count skipped as duplicates) with @file_count associated files. Visit the <a href="@content_url">content listing</a> to view the imported content.',
      array(
        '@node_count' => $node_count,
        '@skipped_count' => $skipped_count,
        '@file_count' => count($form_state['storage']['file_map']),
        '@content_url' => url('admin/content')
      )
    )
  );
}

/**
 * Check if an entity with the given UUID already exists.
 *
 * @param string $entity_type
 *   The type of entity to check (e.g., 'node', 'file').
 * @param string $uuid
 *   The UUID to check for.
 *
 * @return bool
 *   TRUE if an entity with the UUID exists, FALSE otherwise.
 */
function bien_entity_exists_by_uuid($entity_type, $uuid) {
  $query = new EntityFieldQuery();
  $query->entityCondition('entity_type', $entity_type)
        ->fieldCondition('field_uuid', 'value', $uuid);
  $result = $query->execute();
  return !empty($result[$entity_type]);
}
