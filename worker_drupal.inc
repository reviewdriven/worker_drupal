<?php
/**
 * @file
 * Common functions used by Drupal plugins.
 *
 * @author Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/**
 * Install Drupal on the primary database.
 *
 * @param $properties
 *   Associative array of properties defining the job.
 * @return
 *   TRUE if successful, otherwise FALSE.
 *
 * @see worker_perform() for a description of the properties array.
 */
function worker_drupal_install(array $properties) {
  worker_log('Prepare primary database.');
  if (!($connection = worker_database('drupal', $properties['database']))) {
    worker_log('Failed to prepare primary database.');
    return FALSE;
  }

  worker_log('Begin install of Drupal ' . $properties['version'] . '.');

  // Copy the default.settings.php file as settings.php
  $default_settings = JOB_ROOT . '/sites/default/default.settings.php';
  $settings = JOB_ROOT . '/sites/default/settings.php';
  copy($default_settings, $settings);

  // Invoke either the Drupal 6 installation function or the Drupal 7 and above
  // installation function and wait until after chmod() before checking result.
  $function = 'worker_drupal_install_' . min($properties['version'], 7);
  $installed = $function($properties);

  // Ensure that both the default sites directory and settings.php are writable
  // so they can be removed later.
  chmod(JOB_ROOT . '/sites/default', 0755);
  chmod(JOB_ROOT . '/sites/default/settings.php', 0644);

  // If the installation was not a success then $installed will be FALSE or
  // contain the error message associated with the failure.
  if ($installed !== TRUE) {
    worker_log('FAILURE: Install of Drupal ' . $properties['version'] . '.');
    return $installed;
  }

  // Since Drupal installation has completed then go ahead and override/set the
  // specified list of variables.
  if (!worker_drupal_install_variables($properties['variables'], $connection)) {
    worker_log('Failed to set Drupal variable(s).');
    return FALSE;
  }

  worker_log('SUCCESS: Install of Drupal ' . $properties['version'] . '.');
  return TRUE;
}

/**
 * Perform Drupal 7 installation using non-interactive script.
 *
 * For Drupal 7 and above we can use the non-interactive installation method
 * which requires a file to be generated with the site configuration
 * information and placed in the root of the Drupal install.
 *
 * @param $properties
 *   Associative array of properties defining the job.
 * @return
 *   TRUE if successful, otherwise an error message or FALSE.
 *
 * @see worker_perform() for a description of the properties array.
 */
function worker_drupal_install_7(array $properties) {
  // Generate array of settings information required for installation and dump
  // array in installation script template to a file in the root of job site.
  $settings = array(
    'parameters' => array(
      'profile' => 'minimal',
      'locale' => 'en',
    ),
    'forms' => array(
      'install_settings_form' => worker_database_info('drupal', $properties['database']),
      'install_configure_form' => array(
        'site_name' => 'Worker generated installation',
        'site_mail' => 'worker@example.com',
        'account' => array(
          'name' => 'admin',
          'mail' => 'admin@example.com',
          'pass' => array(
            'pass1' => worker_drupal_password(),
            'pass2' => worker_drupal_password(),
          ),
        ),
        'update_status_module' => array(
          1 => TRUE,
          2 => TRUE,
        ),
        'clean_url' => TRUE,
      ),
    ),
  );
  $out = <<<'EOT'
<?php
define('MAINTENANCE_MODE', 'install');
define('DRUPAL_ROOT', getcwd());
require DRUPAL_ROOT . '/includes/install.core.inc';
$settings = [settings];
install_drupal($settings);

EOT;
  $out = str_replace('[settings]', var_export($settings, TRUE), $out);
  file_put_contents(JOB_ROOT . '/install.worker.php', $out);

  // To compelete the installation the generated file needs to be invoked
  // using the URL through which the site will be accessed.
  $browser = new Browser();
  sdp($browser, '$browser');
  sdp(JOB_URL, 'JOB_URL');
  $result = $browser->get(JOB_URL . '/install.worker.php');
  sdp($result, '$result');
  if ($result['code'] != 200) {
    return worker_drupal_install_error($browser);
  }
  return TRUE;
}

/**
 * Perform Drupal 6 installation using Browser class.
 *
 * @param $properties
 *   Associative array of properties defining the job.
 * @return
 *   TRUE if successful, otherwise an error message or FALSE.
 *
 * @see worker_perform() for a description of the properties array.
 */
function worker_drupal_install_6(array $properties) {
  $browser = new Browser();

  // Step: Select an installation profile.
  // Step: Choose language.
  $result = $browser->get(JOB_URL . '/install.php?profile=default&locale=en');
  if ($result['code'] != 200) {
    worker_log('> Failed to start installation process.');
    return worker_drupal_install_error($browser);
  }

  // Step: Database configuration.
  $database = worker_database_info('drupal', $properties['database']);
  $post = array();
  if ($browser->getPage()->xpath('//input[@name="driver"]')) {
    $post['driver'] = $properties['database'];
  }
  $post['db_path'] = $database['database'];
  $post['db_user'] = $database['username'];
  $post['db_pass'] = $database['password'];
  $post['db_host'] = $database['host'];
  $result = $browser->post(NULL, $post, 'Save and continue');
  if ($result['code'] != 302) {
    worker_log('> Failed to submit database information.');
    return worker_drupal_install_error($browser);
  }

  // Step: Site configuration.
  $post = array();
  $post['site_name'] = 'Conduit generated installation';
  $post['site_mail'] = 'worker@example.com';
  $post['account[name]'] = 'admin';
  $post['account[mail]'] = 'admin@example.com';
  $post['account[pass][pass1]'] = $post['account[pass][pass2]'] = worker_drupal_password();
  $post['update_status_module[1]'] = FALSE;
  $result = $browser->post(NULL, $post, 'Save and continue');
  if ($result['code'] != 200 || !$browser->getPage()->xpath('//h2[text()="Drupal installation complete"]')) {
    worker_log('> Failed to complete site configuration.');
    return worker_drupal_install_error($browser);
  }
  return TRUE;
}

/**
 * Attempt to scrape an error message from the current browser page.
 *
 * @param $browser
 *   Browser class object after performing a request.
 * @return
 *   The first error message on the page, otherwise FALSE.
 */
function worker_drupal_install_error(Browser $browser) {
  // Find the first list item in the error messages div or if only one error
  // and thus no list then just grab the only message.
  if ($page = $browser->getPage()) {
    if (($error = current($page->xpath('//div[@class="messages error"]//li'))) ||
      ($error = current($page->xpath('//div[@class="messages error"]')))) {
      // Remove any HTML tags the message may contain.
      $error = trim(preg_replace('/^Error message/m', '', strip_tags((string) $error->asXML())));
      worker_log('> ' . $error);
      return $error;
    }
  }
  return FALSE;
}

/**
 * Set a list of Drupal variables.
 *
 * @param $variables
 *   List of Drupal variables.
 * @param $connection
 *   Database connection to set the variables in.
 * @return
 *   TRUE if successful, otherwise FALSE.
 */
function worker_drupal_install_variables(array $variables, $connection) {
  worker_log('Set Drupal variable(s).');
  $success = TRUE;
  foreach ($variables as $name => $value) {
    // If the variable was not set successfully then log the event and set
    // success to false, but continue so that if the variable set had an error
    // for some reason other then database connectivity that may be noticed.
    if (!worker_drupal_variable_set($name, $value, $connection)) {
      worker_log('> Failed to set variable ' . $name . '.');
      $success = FALSE;
    }
  }
  return $success;
}

/**
 * Set a Drupal variable.
 *
 * @param $name
 *   Name of the variable.
 * @param $value
 *   Value of the variable.
 * @param $connection
 *   Database connection to set the variable in.
 * @return
 *   TRUE if successful, otherwise FALSE.
 */
function worker_drupal_variable_set($name, $value, $connection) {
  // Clear entire cache to ensure that variable changes are loaded.
  $connection->delete('cache')->execute();

  // Remove any pre-existing value for the variable.
  $connection
    ->delete('variable')
    ->condition('name', $name)
    ->execute();

  // Insert the variable and serialize the value. If the return value is zero
  // then the insert was successful since the variables table has no ID field.
  return (int) $connection
    ->insert('variable')
    ->fields(array(
      'name' => $name,
      'value' => serialize($value),
    ))
    ->execute() === 0;
}

/**
 * Get the admin password for the Drupal installation.
 *
 * @return
 *   The admin password for the Drupal installation.
 */
function worker_drupal_password() {
  return drupal_static(__FUNCTION__, mt_rand(1000, 1000000));
}

/**
 * Enable the module or modules on the job site.
 *
 * @param $modules
 *   A single module or list of modules.
 * @param $version
 *   Drupal core version.
 * @return
 *  TRUE if successful, otherwise FALSE.
 */
function worker_drupal_module_enable($modules, $version) {
  // If a single module was specified then create an array.
  if (!is_array($modules)) {
    $modules = array($modules);
  }

  // Login to the job site.
  $browser = new Browser();
  $post = array(
    'name' => 'admin',
    'pass' => worker_drupal_password(),
  );
  $browser->post(JOB_URL . '/user/login', $post, 'Log in');

  // Load the modules page, scan for the appropriate checkboxes, build up a
  // post array, and submit.
  $browser->get(JOB_URL . ($version == 6 ? '/admin/build/modules' : '/admin/modules'));
  $post = array();
  $found = TRUE;
  foreach ($modules as $module) {
    $name = $version == 6 ? 'status[' . $module . ']' : '[' . $module . '][enable]';
    if ($checkbox = current($browser->getPage()->xpath('//input[contains(@name,"' . $name . '")]'))) {
      $post[(string) $checkbox['name']] = TRUE;
    }
    else {
      worker_log('Failed to enable ' . $module . '.');
      $found = FALSE;
    }
  }
  $result = $browser->post(NULL, $post, 'Save configuration');
  return $found && $result['code'] == 302;
}

/**
 * Get the path to a module relative to the JOB_ROOT.
 *
 * @param $module
 *   Name of the module.
 * @return
 *   Relative path to the module, otherwise FALSE.
 */
function worker_drupal_module_path($module) {
  $modules = &drupal_static(__FUNCTION__);

  // If the module paths have not been loaded the scan the job directory for
  // all modules and store the directories in which they are found.
  if (!$modules) {
    worker_log('Scan for modules.');
    foreach (array('modules', 'sites/all', 'sites/default') as $directory) {
      $files = file_scan_directory(JOB_ROOT . '/' . $directory, '/\.module$/m');
      foreach ($files as $file) {
        $modules[$file->name] = str_replace(array(JOB_ROOT . '/', '/' . $file->filename), '', $file->uri);
      }
    }
    worker_log('> Found ' . count($modules) . '.');
  }

  return !empty($modules[$module]) ? $modules[$module] : FALSE;
}