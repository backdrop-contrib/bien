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
    '#title' => 'Upload an XML or JSON file:',
    '#description' => 'Upload a file export from a Backdrop or WordPress content export.',
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
    'file_validate_extensions' => array('json', 'xml'),
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
  // Make the storage of the file permanent.
  $file->status = FILE_STATUS_PERMANENT;
  // Save file status.
  file_save($file);

  $realpath = backdrop_realpath($file->uri);

  // TODO: figure out if the file is an XML WordPress export or a JSON Backdrop
  // export.
  $content = file_get_contents($realpath);
  $content = backdrop_json_decode($content);

  // Save as new content nodes.
  foreach ($content as $c) {
    if(isset($c['nid'])) {
      unset($c['nid']);
    }
    if (isset($c['vid'])) {
      unset($c['vid']);
    }

    $node = new Node($c);
    $node->save();
  }

  // Gie a message to the user.
  backdrop_set_message(
    t(
      'The import has been saved, filename: @filename. Check the /admin/content
      page for a listing of content',
      array('@filename' => $file->filename)
    )
  );
}
