<?php
/**
 * @file
 * 'Provide Drupal plumbing test runner plugin.
 *
 * - install
 * - run tests
 * - code coverage (later)
 *
 * @author Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

$plugin = array(
  'title' => t('Drupal plumbing'),
  'description' => t('Provide Drupal plumbing test runner plugin.'),
  'perform' => 'plumber_perform',
);

/**
 * Perform the job.
 *
 * @param $properties
 *   Associative array of properties defining the job.
 * @return
 *   Associative array containing the 'type' of result being returned and the
 *   associated value that goes along with it, either 'errors' or 'classes'.
 */
function plumber_perform(array $properties) {
  // Load common Drupal conduit functions.
  require drupal_get_path('module', 'conduit_drupal') . '/drupal.inc';

  if (($error = worker_drupal_install($properties)) !== TRUE) {
    return array(
      'type' => 'install',
      'error' => $error,
    );
  }

  worker_log('Enable simpletest.');
  if (!worker_drupal_module_enable('simpletest', $properties['version'])) {
    return array(
      'type' => 'enable',
      'error' => 'Failed to enable simpletest.',
    );
  }

  if (!plumbing_run_tests($properties)) {
    // Unrecoverable error related to configuration occured.
    return FALSE;
  }

  return TRUE;
}

/**
 * Plumb the plumbing (ie. run tests).
 *
 * @param $properties
 *   Associative array of properties defining the job.
 * @return
 *   TRUE if successfully, otherwise FALSE.
 */
function plumbing_plumb(array $properties) {
  // Determine the location of the php executable by using the first entry from
  // whereis and escape the value.
  if (!(($php = current(worker_execute_output('whereis php'))) && ($php = explode(' ', $php)) && !empty($php[1]))) {
    worker_log('Could not determine the location of the PHP executable.');
    return FALSE;
  }
  $php = escapeshellarg($php[1]);

  // Escape arguments.
  $url = escapeshellarg(JOB_URL);
  $classes = implode(',', $properties['classes']);

  // Run tests.
  worker_log('Run ' . count($properties['classes']) . ' test cases.');
  worker_chdir();
  dpm(worker_execute_output("php scripts/run-tests.sh --php $php --url $url --class $classes"));
  chdir(DRUPAL_ROOT);

  return TRUE;
}