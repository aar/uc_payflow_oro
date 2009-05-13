<?php
// $Id$

/***********************************
 *
 * Main function that process recurring payments
 * post payment of the sale payment.
 *
 */
function uc_payflowpro_recurring_charge_process($order, $result) {
  // So, we're going to take the order in and process the recurring
  // items by creating profiles for them, and linking the profile
  // ID to a table in UC so we can look up their profile information.

  //TODO: determine if this if block from the old HEAD branch is useful and
  //add it back in if it is.  wasn't in the version being used for D6 port.
  /*if($result['success'] != true) {
    return false;
  }*/
  
  $cuser = user_load(array('uid' => $order->uid));
  $payment = $order->payment_details;
  $site_key = variable_get('uc_payflowpro_recurring_site_key', 'drupal_uc');
  foreach($order->products as $k => $item) {
    
    if(isset($item->data['pfp_renew']) && $item->data['pfp_renew']['renew']) {
      
      $info = $item->data['pfp_renew'];
      // Get the current schedule for this product
      $schedule = _uc_payflowpro_recurring_get_schedule(array('nid' => $item->nid));
      $full_item_render = node_load($item->nid);
      $profile = null;
      $profile = new PayflowProRecurring();
      $profile->setAuth(_uc_payflowpro_get_auth());
      
      // Call the create new function to set the
      // object up to create a new profile.
      $profile->createNew();
      $profile->setName($site_key . '_' . $cuser->name);
      $profile->setBillTo(_uc_payflowpro_format_address($order, 'billto'));
      if($can_ship) {
        $profile->setShipTo(_uc_payflowpro_format_address($order, 'shipto'));
      }
      $profile->setEmail($cuser->mail);
      
      // Tedner
      $tender['Type'] = 'C';
      $tender['CardNum'] = $payment['cc_number'];
      $tender['NameOnCard'] = $payment['cc_owner'];
      $tender['ExpDate'] = $payment['cc_exp_year'] . sprintf('%02d', $payment['cc_exp_month']);
      $tender['CVNum'] = $payment['cc_cvv'];
      $profile->setTender($tender);
      
      // Terms
      $profile->setAmt($item->price);
      # Start date is calcuated as term + 1, meaning if weekly, start next week; monthly start next month
      $start_date = _uc_pfp_calc_start_date($schedule->period);
      $profile->setStartDate(format_date($start_date, 'custom', 'mdY'));
      $profile->setTerm($info['terms']); // Terms come from the user, not the schedule
      $profile->setPayPeriod(_uc_to_pfp_format_period($schedule->period)); // Period can NOT come from the user
      
      $result = $profile->save();
      $result = $result->RecurringProfileResult;
      // Place the profile result into our database
      // Should probabley store the model
      if($result->Message == 'Approved') {
        db_query("INSERT INTO {uc_payflowpro_recurring_profiles}
                 (order_id, nid, vid, pfp_uid, pfp_profile_id,
                 pfp_rpref, pfp_origid,
                 pfp_status, uc_model)
                 VALUES(%d, %d, %d, %d, '%s',
                 '%s', '%s', '%s', '%s');",
                 $order->order_id, $item->nid, $full_item_render->vid,
                 $cuser->uid, $result->ProfileID, $result->RPRef,
                 $result->RPRef,
                 $result->Result, $item->model);
      }
      
    }
  }
  
}

// Helpers to be moved
// Returns the auth information for PFP
function _uc_payflowpro_get_auth() {
  $auth = array();
  $auth['vendor'] = variable_get('uc_payflowpro_vendor', '');
  $auth['partner'] = variable_get('uc_payflowpro_partner', '');
  $auth['user'] = variable_get('uc_payflowpro_user', '');
  $auth['password'] = variable_get('uc_payflowpro_password', '');
  return $auth;
}

function _uc_payflowpro_format_address($order, $type) {
  $address = array();
  return $address;
}
function _uc_to_pfp_format_period($period) {
  $map = _uc_pfp_period_map();
  return $map[$period];
}

function _uc_pfp_period_map() {
  static $map;
  if($map) {
    return $map;
  }
  $map = array(
    'W' => 'Weekly',
    'B' => 'Bi-weekly',
    'SM' => 'Semi-monthly',
    '4W' => 'Every four weeks',
    'M' => 'Monthly',
    'Q' => 'Quaterly',
    'SA' => 'Semi-yearly',
    'Y' => 'Yearly',
  );
  
  return $map;
}


/**
 *
 * Reference this information about payment dates:
 * 
 * -- WEEK: Weekly - Every week on the same day of the week as the first payment.
 * -- BIWK: Every Two Weeks - Every other week on the same day of the week as the first payment.
 * -- SMMO: Twice Every Month - The 1st and 15th of the month. Results in 24 payments per year. SMMO can start on 1st to 15th of the month, second payment 15 days later or on the last day of the month.
 * -- FRWK: Every Four Weeks - Every 28 days from the previous payment date beginning with the first payment date. Results in 13 payments per year.
 * -- MONT: Monthly - Every month on the same date as the first payment. Results in 12 payments per year.
 * -- QTER: Quarterly - Every three months on the same date as the first payment.
 * -- SMYR: Twice Every Year - Every six months on the same date as the first payment.
 * -- YEAR: Yearly - Every 12 months on the same date as the first payment.
 *
 * //TODO Correct the start dates for the other types
 * 
 */
function _uc_pfp_calc_start_date($period) {
  $today = time();
  switch($period) {
    case 'W':
      $delta = 60 * 60 * 24 * 7;
      break;
    case 'B':
      $delta = 60 * 60 * 24 * 7 * 2;
      break;
    // Semi-monthly starts on the 1st or 15th
    // So, if we're > 15th, then start on the 1st,
    // else start on the 15th
    // -- NOT IMPL
    case 'SM':
      $delta = 60 * 60 * 24 * 7;
      break;
    // Every 28 days
    case '4W':
      $delta = 60 * 60 * 24 * 28;
      break;
    // Same date every month
    // On the next month, arbitrariy the
    // 15th of that month to avoid the 31st of a month
    // when other months only have 30, and Feb with 28/29
    // if D == 30, D = 28
    // -- NOT IMPL
    case 'M':
      $delta = 60 * 60 * 24 * 7;
      break;
    // Same date every 3 months, again, if D == 30, D = 28
    // -- NOT IMPL
    case 'Q':
      $delta = 60 * 60 * 24 * 7;
      break;
    // Every 6 months
    // if D == 30, D = 28
    // -- NOT IMPL
    case 'SA':
      $delta = 60 * 60 * 24 * 7;
      break;
    // Same daate eady year
    // if D == 30, D = 28
    case 'Y':
      // What about leap years?
      $delta = (int)60 * 60 * 24 * 365.25;
      break;
  }
  $start_date = $today + $delta;
  return $start_date;
}
/*******************************************************************************
 * 
 * Recurring Billing Uberbart hooks
 *  
 ******************************************************************************/
function uc_payflowpro_checkout_pane() {
  if(variable_get('uc_payflowpro_enable_recurring', false) == false) {
    return;
  }
  $panes[] = array(
    'id' => 'pfp_recurring_co_pane',
    'callback' => 'uc_payflowpro_recurring_checkout_pane_cart',
    'title' => t('Renewable Products Options'),
    'desc' => t("Display options to renew/repurchase certain products on a schedule."),
    'weight' => 2,
    'process' => true,
    'collapsible' => FALSE,
  );
  return $panes;
}

function uc_payflowpro_recurring_checkout_pane_cart($op, &$arg1, $arg2) {
  
  switch($op) {
    case 'view':
      $description = t('You may select which products would like to auto
                       renew.');
      $contents = uc_payflowpro_recurring_co_pane_contents();
      return array('description' => $description,
                   'contents' => $contents);
      break;
    case 'process':
      $renew_products = $arg2['renewable_products'];
      foreach($arg1->products as $k => $product) {
        if(isset($renew_products[$product->vid]) && $renew_products[$product->vid]['renew']) {
          $arg1->products[$k]->data['pfp_renew'] = $renew_products[$product->vid];
        }
      }
      return true;
      break;
  }
  return;
}

function uc_payflowpro_recurring_co_pane_contents($order = '') {
  $items = uc_cart_get_contents();
  $vids = array();
  foreach($items as $k => $item) {
    $vids[] =  $item->vid;
  }
  
  $rs = db_query("SELECT model, terms, period, urp.vid FROM {uc_payflowpro_recurring_products} urp INNER JOIN {uc_products} p ON urp.vid = p.vid
                 INNER JOIN {uc_payflowpro_recurring_schedules} urs ON urp.sid = urp.sid
                 WHERE urp.vid IN('%s')", implode(',', $vids));
  $form = array();
  $form['renewable_products']['#tree'] = true;
  $form['renewable_products']['#theme'] = 'uc_pfp_recurring_co_pane_table';
  while($row = db_fetch_array($rs)) {
    $form['renewable_products'][$row['vid']]['renew'] = array(
      '#type' => 'checkbox',
      '#title' => 'renew',
    );
    $form['renewable_products'][$row['vid']]['vid'] = array(
      '#type' => 'hidden',
      '#value' => $row['vid']);
    $form['renewable_products'][$row['vid']]['model'] = array(
      '#value' => $row['model'],
    );
    $form['renewable_products'][$row['vid']]['period'] = array(
      '#value' => $row['period'],
    );
    $form['renewable_products'][$row['vid']]['terms'] = array(
      '#type' => 'select',
      '#title' => t('Terms'),
      '#options' => drupal_map_assoc(range(0,12)),
    );
  }
  
  return $form;
}

function theme_uc_pfp_recurring_co_pane_table($form_values) {
  $header = array(t('Renew?'), t('Cart Item'), t('Auto-renew'), t('How many times would you like to renew?'));
  foreach(element_children($form_values) as $id => $item_vid) {
    $row = array();
    $item = $form_values[$item_vid];
    unset($item['renew']['#title']);
    unset($item['model']['#title']);
    unset($item['period']['#title']);
    unset($item['terms']['#title']);
    $row[] = drupal_render($item['renew']);
    $row[] = drupal_render($item['model']);
    $row[] = drupal_render($item['period']);
    $row[] = drupal_render($item['terms']);
    $output .= drupal_render($item['vid']);
    $rows[] = $row;
    unset($form_values[$id]);
  }
  if( count($rows) == 0 ) {
    return '<div><em>' . t('You have no renewable items in your cart.') . '</em></div>';;
  }
  $output .= theme('table', $header, $rows);
  
  return $output;
}



/*******************************************************************************
 * 
 * Recurring Billing settings/forms/functions
 *  
 ******************************************************************************/
function uc_payflowpro_recurring_settings() {
  $page = '';
  
  
  $page .= '<h2>' . t('Recurring Stats') . '</h2>';
  $header = array(' ', ' ');
  $rows = array();
  $row = array();
  $total_profiles = 0;
  //TODO: figure out what is going on with this.  
  //current version looks much worse than pre-D6 HEAD
  //$total_profiles = db_result(db_query('SELECT COUNT(*) FROM {uc_payflowpro_recurring_profiles}'));
  //$total_user_profiles = db_result(db_query('SELECT COUNT(DISTINCT pfp_uid) FROM {uc_payflowpro_recurring_profiles}'));
  $total_products = db_result(db_query('SELECT COUNT(*) FROM {uc_payflowpro_recurring_products}'));
  $rows[] = array(t('Total Profiles') . ':', $total_profiles);
  //TODO: this line also from old HEAD
  //$rows[] = array(t('Total Users with Profiles') . ':', $total_user_profiles);
  $rows[] = array(t('Total Recurring Products') . ':', $total_products);
  $page .= theme('table', $header, $rows);
  return $page;
}

function uc_payflowpro_recurring_schedules() {
  $page = '';
  
  if(arg(6) == 'edit') {
    $page = drupal_get_form('uc_payflowpro_recurring_schedule_edit_form', arg(5));
    return $page;
  }
  else if (arg(6) == 'delete') {
    $page = drupal_get_form('uc_payflowpro_recurring_schedule_delete_form', arg(5));
    return $page;
  }
  // Add section
  $block = array();
  $block['n'] = array(
    '#type' => 'fieldset',
    '#title' => t('Add Schedule'),
    '#collapsible' => true,
    '#collapsed' => true,
    '#value' => drupal_get_form('uc_payflowpro_recurring_schedule_edit_form'),
  );
  $page .= drupal_render($block);
  
  // Current Schedules
  $header = array(t('Name'), t('Terms'), '');
  $rows = array();
  $tr = array();
  $rs = db_query("SELECT * FROM {uc_payflowpro_recurring_schedules}");
  $period_map = _uc_payflowpro_recurring_periods();
  while($row = db_fetch_object($rs)) {
    $tr = array();
    $tr[] = $row->title;
    if($row->terms == 0) {
      $row->terms = t('Unlimited');
    }
    $tr[] = "Renewed each " . $period_map[$row->period] . " for $row->terms";
    $links = array();
    $links[] = array('title' => t('edit'), 'href' => 'admin/store/settings/uc_recurring/schedules/' . $row->sid . '/edit');
    $links[] = array('title' => t('delete'), 'href' => 'admin/store/settings/uc_recurring/schedules/' . $row->sid . '/delete');
    $tr[] = theme('links', $links);
    $rows[] = $tr;
  }
  $page .= theme('table', $header, $rows);
  return $page;
}

function uc_payflowpro_recurring_schedule_edit_form($sid = '') {
  $form = array();
  $s = db_fetch_object(db_query("SELECT * FROM {uc_payflowpro_recurring_schedules} WHERE sid = %d", $sid));
  $form['title'] = array(
    '#type' => 'textfield',
    '#title' => t('Title'),
    '#default_value' => $s->title,
  );
  $form['description'] = array(
    '#type' => 'textarea',
    '#title' => t('Description'),
    '#default_value' => $s->description,
  );
  $terms = drupal_map_assoc(range(0,12));
  $form['terms'] = array(
    '#type' => 'select',
    '#title' => t('Terms'),
    '#options' => $terms,
    '#description' => t('Set to 0 for an unlimited number of renewals until the user cancels.'),
    '#default_value' => $s->terms,
  );
  $form['period'] = array(
    '#type' => 'select',
    '#title' => t('Period'),
    '#options' => _uc_payflowpro_recurring_periods(),
    '#default_value' => $s->period,
  );
  if(is_numeric($sid)) {
    $form['sid'] = array('#type' => 'hidden', '#value' => $sid);
    $form['update'] = array('#type' => 'submit', '#value' => t('Update Schedule'));
  }
  else {
    $form['add'] = array('#type' => 'submit', '#value' => t('Add Schedule'));
  }
  
  
  return $form;
}

function uc_payflowpro_recurring_schedule_edit_form_submit($form, &$form_state) {
  if(!isset($form_state['values']['sid'])) {
    $sid = db_next_id('{uc_payflowpro_recurring_schedules}_sid');
    db_query("INSERT INTO {uc_payflowpro_recurring_schedules}(sid, title, description, terms, period)
           VALUES(%d, '%s', '%s', '%s', '%s');",
           $sid, $form_state['values']['title'], $form_state['values']['description'],
           $form_state['values']['terms'], $form_state['values']['period']);
    drupal_set_message(t('Schedule added.'));
  }
  else {
    db_query("UPDATE {uc_payflowpro_recurring_schedules}
             SET title = '%s', description = '%s',
             terms = '%s', period = '%s'
             WHERE sid = %d",
             $form_state['values']['title'], $form_state['values']['description'],
             $form_state['values']['terms'], $form_state['values']['period'],
             $form_state['values']['sid']);
    drupal_set_message(t('Schedule updated.'));
  }
             
  return;
}

function uc_payflowpro_recurring_schedule_delete_form($sid) {
  $form = array();
  $form['sid'] = array('#type' => 'hidden', '#value' => $sid);
  return confirm_form($form, t('Are you sure you wish to remove this schedule?'), 'admin/store/settings/uc_recurring/schedules');
}
function uc_payflowpro_recurring_schedule_delete_form_submit($form_id, $form_values) {
  db_query("DELETE FROM {uc_payflowpro_recurring_schedules} WHERE sid = %d", $form_values['sid']);
  drupal_set_message(t('The schedule has been removed from the system.'));
  return 'admin/store/settings/uc_recurring/schedules';
}

/**
 * Recurring Products for
 */
function uc_payflowpro_recurring_products() {
  $page = '';
  
  if(arg(6) == 'edit') {
    $page = drupal_get_form('uc_payflowpro_recurring_products_edit_form', arg(5));
    return $page;
  }
  else if(arg(6) == 'delete') {
    $page = drupal_get_form('uc_payflowpro_recurring_products_delete_form', arg(5));
    return $page;
  }
  // Add section
  $block = array();
  $block['n'] = array(
    '#type' => 'fieldset',
    '#title' => t('Add Recurring Product'),
    '#collapsible' => true,
    '#collapsed' => true,
    '#value' => drupal_get_form('uc_payflowpro_recurring_products_edit_form'),
  );
  $page .= drupal_render($block);
  
  // Current Products
  $header = array(t('Name/Model'), t('Terms'), '');
  $rows = array();
  $tr = array();
  $rs = db_query("SELECT uc_pfp_rpid, urp.sid, model, period, urp.vid FROM {uc_payflowpro_recurring_products} urp
                  INNER JOIN {uc_products} up ON urp.vid = up.vid
                  INNER JOIN {uc_payflowpro_recurring_schedules} urs ON urp.sid = urs.sid");
  $period_map = _uc_payflowpro_recurring_periods();
  while($row = db_fetch_object($rs)) {
    $tr = array();
    $tr[] = $row->model;
    if($row->terms == 0) {
      $row->terms = t('Unlimited');
    }
    $tr[] = "Renewed each " . $period_map[$row->period] . " for $row->terms";
    $links = array();
    $links[] = array('title' => t('edit'), 'href' => 'admin/store/settings/uc_recurring/products/' . $row->uc_pfp_rpid . '/edit');
    $links[] = array('title' => t('delete'), 'href' => 'admin/store/settings/uc_recurring/products/' . $row->uc_pfp_rpid . '/delete');
    $tr[] = theme('links', $links);
    $rows[] = $tr;
  }
  $page .= theme('table', $header, $rows);
  
  return $page;
}

// Recurring products edit form
function uc_payflowpro_recurring_products_edit_form($uc_pfp_rpid = '') {
  $form = array();
  
  $products = array();
  if(!is_numeric($uc_pfp_rpid)) {
    $rs = db_query('SELECT nid, vid, model FROM {uc_products}
                   WHERE vid NOT IN (
                    SELECT vid FROM {uc_payflowpro_recurring_products}
                    )');
  }
  else {
    $rs = db_query('SELECT nid, up.vid, model FROM {uc_products} up INNER JOIN {uc_payflowpro_recurring_products} urp ON up.vid = urp.vid WHERE urp.uc_pfp_rpid = %d', $uc_pfp_rpid);
  }
  while($row = db_fetch_object($rs)) {
    $products[$row->vid] = $row->model;
  }
  $form['vid'] = array(
    '#type' => 'select',
    '#title' => t('Product'),
    '#default_value' => $s->pid,
    '#options' => $products,
  );
  $rs = db_query('SELECT title, sid FROM {uc_payflowpro_recurring_schedules}');
  while($row = db_fetch_object($rs)) {
    $schedules[$row->sid] = $row->title;
  }
  $form['sid'] = array(
    '#type' => 'select',
    '#title' => t('Schedule'),
    '#default_value' => $p->sid,
    '#options' => $schedules,
  );
  
  if(is_numeric($uc_pfp_rpid)) {
    $form['uc_pfp_rpid'] = array('#type' => 'hidden', '#value' => $uc_pfp_rpid);
    $form['update'] = array('#type' => 'submit',
                            '#value' => t('Update Product'));
  }
  else {
    $form['add'] = array('#type' => 'submit',
                         '#value' => t('Add Product'),
                        );
  }
  return $form;
}
// Submit a Recurring PRoducts
function uc_payflowpro_recurring_products_edit_form_submit($form_id, $form_values) {
  if(!isset($form_values['uc_pfp_rpid'])) {
    $try = db_query("INSERT INTO {uc_payflowpro_recurring_products}(vid, sid)
                    VALUES(%d, %d)",
                    $form_values['vid'], $form_values['sid']);
    if(!$try) {
      drupal_set_message(t('There was an error adding your products.'), 'error');
    }
    else {
      drupal_set_message(t('Your recurring product option has been added.'));
    }
  }
  else {
    $try = db_query("UPDATE {uc_payflowpro_recurring_products}
                    SET sid = %d
                    WHERE uc_pfp_id = %d",
                    $form_values['sid'], $form_values['uc_pfp_id']);
    if(!$try) {
      drupal_set_message(t('There was an error adding your products.'), 'error');
    }
    else {
      drupal_set_message(t('Your recurring product option has been updaed.'));
      return 'admin/store/settings/uc_recurring/products';
    }
  }
  return;
}

// Delete
function uc_payflowpro_recurring_products_delete_form($uc_pfp_rpid) {
  $form = array();
  $form['uc_pfp_rpid'] = array('#type' => 'hidden', '#value' => $uc_pfp_rpid);
  return confirm_form($form, t('Are you sure you wish to remove this schedule from this product?'), 'admin/store/settings/uc_recurring/products');
}
function uc_payflowpro_recurring_products_delete_form_submit($form_id, $form_values) {
  db_query("DELETE FROM {uc_payflowpro_recurring_products} WHERE uc_pfp_rpid = %d", $form_values['uc_pfp_rpid']);
  drupal_set_message(t('The product has had the selected schedule removed.'));
  return 'admin/store/settings/uc_recurring/products';
}

/*************
 * Helper function
 */
function _uc_payflowpro_recurring_get_schedule($search) {
  if(!$search['vid']) {
    $item = node_load($search['nid']);
  }
  $query = "SELECT * FROM {uc_payflowpro_recurring_schedules} urs
            INNER JOIN {uc_payflowpro_recurring_products} urp ON urs.sid = urp.sid
            WHERE urp.vid = %d";
  $schedule = db_fetch_object(db_query($query, $item->vid));
  return $schedule;
}











/*******************************************************************************
 * 
 * Genearl Control Panel Section
 *  
 ******************************************************************************/

function uc_payflowpro_user_panel() {
  global $user;
  
  $page = '';
  $page .= '<h2>' . t('Recurring Payments Panel') . '</h2>';
  
  if(user_access('administer store') && arg(0) == 'admin') { 
    $query = "SELECT pfp_pid pid, pfp_profile_id as profile_id, pfp_uid as uid, pfp_origid as pn_ref 
                         FROM {uc_payflowpro_recurring_profiles}";
    $base_url = 'admin/store/settings/uc_recurring/administer';
  }
  else {
    $query = "SELECT pfp_pid pid, pfp_profile_id as profile_id, pfp_uid as uid, pfp_origid as pn_ref 
                         FROM {uc_payflowpro_recurring_profiles} WHERE pfp_uid = " . $user->uid;
    $base_url = 'user/' . arg(1) . '/pfp_cpanel';
    //TODO: investigate old HEAD version:
    /*if(user_access('administer store')) {
      $user_id = arg(1);
    }
    else {
      $user_id = $user->uid;
    }
    $query = "SELECT pfp_pid pid, pfp_profile_id as profile_id, pfp_uid as uid, pfp_origid as pn_ref 
                         FROM {uc_payflowpro_recurring_profiles} WHERE pfp_uid = " . $user_id;
    $base_url = 'user/' . arg(1) . '/pfp_cpanel';*/
  }
  
  // Dispatcher
  $profile_id = arg(0) == 'user' ? arg(3) : arg(5);
  if(arg(4) && ( strstr($profile_id, 'RP') || strstr($profile_id, 'RT') ) ) {
    if(arg(6) == 'cancel' || arg(4) == 'cancel') {
      return drupal_get_form('uc_payflowpro_cancel_form', $profile_id, $base_url);
    }
    else {
      return uc_payflowpro_cpanel_view_profile($profile_id);
    }
  }
  
  $rs = pager_query($query);
  
  $page .= theme('pager');
  $header = array(t('Username'), t('Profile name'), t('Profile ID'), t('PN Ref'), '-');
  $rows = array();
  $row = array();
  while($crow = db_fetch_object($rs)) {
    $row = array();
    $ul = null;
    if($crow->uid != '') {
      $ul = user_load(array('uid' => $crow->uid));
    }
    
    $row[] = theme('username', $ul);
    $row[] = $ul->name;
    $row[] = $crow->profile_id;
    $row[] = $crow->pn_ref;
    $row[] = l('view', $base_url . '/' . $crow->profile_id . '/view');
    $rows[] = $row;
  }
  $page .= theme('table', $header, $rows);
  $page .= theme('pager');
  return $page;
}

function uc_payflowpro_cpanel_view_profile($pid) {
  global $user;
  $mode = substr($pid,0,2) == 'RT' ? 'test' : 'live';
  $check = db_num_rows(db_query("SELECT pfp_pid, pfp_profile_id, pfp_uid, pfp_origid, pfp_profile_id FROM {uc_payflowpro_recurring_profiles} WHERE pfp_uid = %d", $user->uid));
  if($check < 1 && !user_access('administer store')) {
    return t('You are not authorized to view this information.');
  }
  
  if(arg(0) == 'admin') {
    $base_url = 'admin/' . arg(1) . '/pfp_cpanel/' . $pid;
  }
  else {
    $base_url = 'user/' . arg(1) . '/pfp_cpanel/' . $pid;
  }
  
  $page = '';
  $page .= '<h2>' . t('Viewing profile %profile_id', array('%profile_id' => $pid)) . '</h2>';
  if($mode == 'test') {
    $page .= '<h2><font color="red">' . t('THIS IS A TEST PROFILE. TRANSACTION WILL BE RUN IN TEST MODE') . '</font></h2>';
    drupal_set_message(t('THIS IS A TEST PROFILE. TRANSACTION WILL BE RUN IN TEST MODE'), 'error');
  }
  
  #$response = PayflowProClient::submitTransaction(trim(_text_xml()));
  //$response = _payflowpro_send_transaction(trim(_test_xml(1)));
  //$page .= '<pre>' . print_r($response,1) . '</pre>';
  
  $profile = new PayflowProRecurring($pid, _uc_payflowpro_get_auth());
  $profile->setMode($mode);
  $try = $profile->refresh();
  if($try == false) {
    '<h2>There was an error loading your information at this time. Please try again later.</h2>';
  }
  // Billin information
  $page .= '<table><tr><td>' . theme('payflow_address', t('Bill to'), $profile->getBillTo()) .
    '</td><td>&nbsp;</td><td>' . theme('payflow_address', t('Ship to'), $profile->getShipTo()) .
    '</td></tr></table>';
    
  // Payment information
  // Commenting out the fieldset, width is too wide
  /*$form = array();
  $form['tender'] = array(
    '#type' => 'fieldset',
    '#title' => t('Tender'),
    '#value' => theme('payflowpro_tender', $profile->getTender()),
  );*/
  $page .= theme('payflowpro_tender', $profile->getTender());
  
  if($profile->getStatus() == 'ACTIVE' && $profile->getLastPaymentStatus() != 0) {
    drupal_set_message(t(strtoupper('This profile is scheduled to be deactived due to non-payment.')), 'error');
  }
  
  // Status
  $order_id = db_result(db_query("SELECT order_id FROM {uc_payflowpro_recurring_profiles} WHERE pfp_profile_id = '%s'", $profile->getProfileID()));
  $vid = db_result(db_query("SELECT vid FROM {uc_payflowpro_recurring_profiles} WHERE order_id = %d", $order_id));
  $product = node_load(array('vid' => $vid));
  
  $form = array();
  $status = array();
  
  $cancel = $profile->getStatus() == 'ACTIVE' ? l(t('cancel'), $base_url . '/cancel') : '';
  $status['Product Name'] = $product ? '<strong>' . l($product->title, 'node/' . $product->nid) . '</strong>' : '<strong><font color="red">' . t('Unable to find associated product') . '</font></strong>';
  $status['Status'] = $profile->getStatus() . ' ' . $cancel;
  // The following will only be avail if this is an active profile
  if($cancel) {
    $status['Name'] = $profile->getName();
    $status['Email'] = $profile->getEmail();
    $status['Start'] = $profile->getStartDate('M d, Y');
    $status['End'] = $profile->getEndDate('M d, Y');
    $status['Payment Periods'] = $profile->getPayPeriod();
    $status['Payment Amounts'] = $profile->getAmt();
    $status['Payments Left'] = $profile->getPaymentsLeft('M d, Y');
    $status['Next Payment'] = $profile->getNextPaymentDate('M d, Y');
  }
  $header = array('', '');
  foreach($status as $label => $value) {
    $row=  array();
    $row[] = $label;
    $row[] = $value;
    $rows[] = $row;
  }
  $status = theme('table', $header, $rows);
  $form['status'] = array(
    '#type' => 'fieldset',
    '#title' => t('Payment status'),
    '#value' => $status,
  );
  $page .= drupal_render($form);  
  
  
  // Payment History
  if($profile->getLastPaymentStatus() !== null) {
    $history = $profile->getPaymentHistory();
    $header = array(t('ID'), t('Payment Date'), t('Tender'), t('Amt'), t('Status'));
    $rows = array();
    $row = array();
    foreach($history as $k => $payment) {
      $row = array();
      $row[] = $payment['PaymentNum'];
      $row[] = $payment['TransTime'];
      $row[] = $payment['Tender'];
      $row[] = $payment['Amount'];
      $row[] = _payflowpro_transaction_code_to_string($payment['Result']);
      $rows[] = $row;
    }
    $status = theme('table', $header, $rows);
  }
  else {
    if($profile->getStatus() == 'ACTIVE') {
      $status = '<h2>' . t('No payments have been made') . '</h2>';
    }
    else {
      $status = '<h2>' . t('Unable to pull the payment history on a non-active subscription') . '</h2>';
    }
  }
  $form = array();
  $form['history'] = array(
    '#type' => 'fieldset',
    '#title' => t('Payment History'),
    '#value' => $status);

  $page .= drupal_render($form);
  
  return $page;
}

function theme_payflowpro_tender($tender) {
  $output = '';
  
  if(isset($tender['Card'])) {
    $tender = $tender['Card'];
    $output .= '<strong>' . t('Card') . "</strong>: {$tender->CardNum}<br />";
    $output .= '<strong>' . t('Expiration') . "</strong>: " . substr($tender->ExpDate,0,2) . '/' . substr($tender->ExpDate,2,2) . '<br />';
  }
  
  return $output;
}

function theme_payflow_address($title = 'Address', $address) {
  $output = '';
  
  $output .= t('Address') . ":{$address['Street']}<br/>";
  $output .= t('City') . ":{$address['City']}<br/>";
  $output .= t('State') . ":{$address['State']}<br/>";
  $output .= t('Zip') . ":{$address['Zip']}<br/>";
  $output .= t('Country') . ":{$address['Country']}<br/>";
  
  $render = array('#type' => 'fieldset',
                  '#title' => $title,
                  '#value' => $output);
  
  return drupal_render($render);
}
function _payment_payflowpro_auth_info() {
  static $auth;
  if(!isset($auth)) {
    $auth['vendor'] = variable_get('ec_payflowpro_vendor', '');
    $auth['partner'] = variable_get('ec_payflowpro_partner', '');
    $auth['user'] = variable_get('ec_payflowpro_user', '');
    $auth['password'] = variable_get('ec_payflowpro_password', '');
  }
  return $auth;
}

function uc_payflowpro_cancel_form($profile_id, $cancel_path) {
  $form = array();
  $form['profile_id'] = array('#type' => 'hidden', '#value' => $profile_id);
  $order_id = db_result(db_query("SELECT order_id FROM {uc_payflowpro_recurring_profiles} WHERE pfp_profile_id = '%s'", $profile_id));
  $form['order_id'] = array('#type' => 'hidden', '#value' => $order_id);
  $form['#destination'] = $cancel_path;
  $form['post_dest'] = array('#type' => 'hidden', '#value' => $cancel_path);
  
  return confirm_form($form, t('Are you sure you wish to cancel your subscription?'), $cancel_path,
    t('This will stop the renewal of payments and remove you from the
      subscribed to service.'),
    t('Cancel Subscription'));
}

/**
 * Submit form for a profile from the cpanel
 */
function uc_payflowpro_cancel_form_submit($form_id, $form_values) {
  
  // Load up the profile
  $profile = new PayflowProRecurring($form_values['profile_id'], _uc_payflowpro_get_auth());
  $profile->refresh();
  // Cancel
  $try = $profile->cancel();
  if(!$try) {
    if($profile->getReturnCode() != '33') {
      drupal_set_message(t('Unable to cancel your subscription at this time.'), 'error');
      drupal_set_message(t('Error: %error_msg', array('%error_msg' => $profile->getReturnMsg())));
    }
  }
  // We can add more code here to un-subscribe, deactivate certain features
  
  drupal_set_message(t('Your subscription has been canceled.'));
  return $form_values['post_dest'];
}


function payment_payflowpro_cancel($txnid = '') {
  $page = drupal_get_form('payment_payflowpro_confirm_cancel', $txnid);
  return $page;
}

function payment_payflowpro_confirm_cancel($txnid = '') {
  $form = array();
  $form['txnid'] = array(
    '#type' => 'hidden',
    '#value' => $txnid,
  );
  $form['#redirect'] = 'store/subscriptions';
  return confirm_form($form, t('Are you sure you wish to cancel your subscription?'));
}

function uc_payflowpro_confirm_cancel_submit($form, &$form_state) {
  _payment_payflowpro_ec_subscription_cancel($form_state['values']['txnid']);
  drupal_set_message(t('Your subscription has been canceled.'));
}

function _uc_payflowpro_ec_subscription_cancel($txnid) {
  // Update the status to expired
  if(module_exists('ec_recurring')) {
    db_query("UPDATE {ec_recurring_expiration} SET expiry = %d WHERE txnid = %d", time() - 10, $txnid);
    // Run the expiration sequence
    ec_recurring_process_expiries();
  }
  // Ubercart
  
  return;
}

?>
