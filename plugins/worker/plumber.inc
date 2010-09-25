<?php
/**
 * @file
 * Provide Drupal plumber test runner plugin.
 *
 * - install
 * - run tests
 * - code coverage (later)
 *
 * The following Drupal variables are expected to be set as shown.
 * - simpletest_clear_results: 0
 * - simpletest_verbose: 0
 *
 * @author Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

$plugin = array(
  'title' => t('Drupal plumber'),
  'description' => t('Provide Drupal plumber test runner plugin.'),
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

  if (!($log = plumber_plumb($properties))) {
    // Unrecoverable error related to configuration occurred.
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
 *   Log of the length of time each testcase took to complete, otherwise FALSE.
 * @see worker_execute_concurrent()
 */
function plumber_plumb(array $properties) {
  // Determine the location of the PHP executable from whereis command output.
  if (!(($php = current(worker_execute_output('whereis php'))) && ($php = explode(' ', $php)) && !empty($php[1]))) {
    worker_log('Could not determine the location of the PHP executable.');
    return FALSE;
  }

  // Escape arguments.
  $url = escapeshellarg(JOB_URL);
  $php = escapeshellarg($php[1]);

  // Generate list of commands to run all test cases.
  $commands = array();
  $command = "php scripts/run-tests.sh --php $php --url $url --class ";
  foreach ($properties['classes'] as $class) {
    $commands[$class] = $command . $class;
  }

  // Run tests using manual concurrency.
  worker_log('Run ' . count($properties['classes']) . ' test case(s).');
  worker_chdir();
  $log = worker_execute_concurrent($commands);
  chdir(DRUPAL_ROOT);

  return $log;
}