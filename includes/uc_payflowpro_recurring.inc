<?php
// $Id: uc_payflowpro_recurring.inc,v 1.2.2.18 2011/01/26 00:43:50 kwinters Exp $

/***********************************
 *
 * Main function that process recurring payments
 * post payment of the sale payment.
 *
 */
function uc_payflowpro_recurring_charge_process($order) {
  // Take the order in and process the recurring items by creating profiles for
  // them, and linking the profile ID to a table in UC so we can look up their
  // profile information later.

  $cuser = user_load(array('uid' => $order->uid));
  // User name is empty for anon checkout, so make one up the same way as uc_cart_complete_sale.
  if ($cuser->uid < 1) {
    $cuser->name = uc_store_email_to_username($order->primary_email);
  }
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
      $profile->setCompanyName( (($order->billing_company) ? $order->billing_company : $order->delivery_company));
      $profile->setBillTo(_uc_payflowpro_format_address($order, 'billto'));
      $profile->setShipTo(_uc_payflowpro_format_address($order, 'shipto'));
      $profile->setEmail($order->primary_email);

      // Tender
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
      $profile->setTerm($info['terms']); // Terms can come from the user
      $profile->setPayPeriod(_uc_to_pfp_format_period($schedule->period)); // Period can NOT come from the user

      $result = $profile->save();
      $result = $result->RecurringProfileResult;

      // Place the profile result into our database
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

        $message = t('Recurring payment processed successfully with profile ID @pfp_profile_id.', array(
          '@pfp_profile_id' => $result->ProfileID,
        ));
      } else {
        $message = t('Recurring payment declined for !amount with error code @return_code (@message).', array(
          '!amount' => uc_currency_format($item->price),
          '@return_code' => $result->Result,
          '@message' => $result->Message,
        ));
      }

      // Add an admin note to the order reflecting the result of the recurring profile save attempt.
      uc_order_comment_save($order->order_id, $order->uid, $message, 'admin');
    }
  }

}

function uc_payflowpro_checkout_complete_processing($order) {
  // Anonymous users automatically have a user created for them on checkout
  // complete. However, that happens after the CC info has already been
  // truncated, so it's now too late to create the profile.  Since the only
  // thing the uid is needed for is some user panel lookups, etc. we can make
  // the profile earlier and then just fix the uid after the user has been created.
  if ($order->uid > 0 && $order->order_id > 0) {
    db_query(
    	"UPDATE {uc_payflowpro_recurring_profiles} SET pfp_uid = %d WHERE order_id = %d",
      $order->uid, $order->order_id
   );
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

/**
 * Returns a billing or shipping address from the order in the format
 * that is needed by the recurring class.
 */
function _uc_payflowpro_format_address($order, $type = 'billto') {
  //TODO: This function is somewhat redundant with the address XML generation
  // block used for normal transactions, condense them.

  if ($type == 'shipto') {
    $prefix = 'delivery';
  } else {
    $prefix = 'billing';
  }

  $sourceData = array(
    'FirstName' => 'billing_first_name',
    'LastName' => 'billing_last_name',
    'Phone' => 'billing_phone',
    'EMail' => 'primary_email',
    'CustCode' => 'uid',
    'Street' => 'billing_street1',
    'Street2' => 'billing_street2',
    'City' => 'billing_city',
    'State' => 'billing_zone',
    'Zip' => 'billing_postal_code',
    'Country' => 'billing_country',
  );

  $address = array();

  foreach ($sourceData as $newKey => $orderKey)
  {
    $orderKey = str_replace('billing',$prefix,$orderKey);
    $address[$newKey] = $order->$orderKey;
  }

  // Special field handling for state and country codes.
  $country = uc_get_country_data(array('country_id' => $address['Country']));
  if ($country === FALSE) {
    $country = array(0 => array('country_iso_code_2' => 'US'));
  }
  $address['State'] = uc_get_zone_code($address['State']);
  $address['Country'] = $country[0]['country_iso_code_2'];

  return $address;
}

/**
 * Returns the PFP format needed for requests for a given internal period type.
 */
function _uc_to_pfp_format_period($period) {
  $map = _uc_pfp_period_map();
  return $map[$period];
}

/**
 * Returns a map from the internal period type to the PFP request type.
 */
function _uc_pfp_period_map() {
  static $uc_pfp_period_map;
  if($uc_pfp_period_map) {
    return $uc_pfp_period_map;
  }
  $uc_pfp_period_map = array(
    'W'  => 'Weekly',
    'B'  => 'Bi-weekly',
    'SM' => 'Semi-monthly',
    '4W' => 'Every four weeks',
    'M'  => 'Monthly',
    'Q'  => 'Quarterly',
    'SA' => 'Semi-yearly',
    'Y'  => 'Yearly',
  );

  return $uc_pfp_period_map;
}

/**
 * Returns display text for a given internal period type.
 */
function _uc_to_display_format_period($period) {
  $map = _uc_pfp_display_map();
  return $map[$period];
}

/**
 * Returns a map from the internal period type to its display text.
 */
function _uc_display_period_map() {
  static $uc_display_period_map;
  if($uc_display_period_map) {
    return $uc_display_period_map;
  }
  $uc_display_period_map = array(
    'W' => 'Weekly',
    'B' => 'Every Two Weeks',
    'SM' => 'Twice a Month',
    '4W' => 'Every Four Weeks',
    'M' => 'Monthly',
    'Q' => 'Quaterly',
    'SA' => 'Twice a Year',
    'Y' => 'Yearly',
  );

  return $uc_display_period_map;
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
 */
function _uc_pfp_calc_start_date($period, $fromTime = -1) {
  // Monthly / yearly periods need to have consistent billing dates across months with varying numbers of days.
  // For monthly, round the 29/30/31st up to the first of the next month so that the buyer doesn't get short-changed.
  // Apply the same rule to yearly / semi-yearly for consistency and so that leap years don't have issues.
  $noEndOfMonthDays = TRUE;
  $fromTime = intval($fromTime);
  if ($fromTime < 1)
  {
    $fromTime = time();
  }

  switch($period) {
    case 'W': // Weekly
      $start_time = strtotime('+1 week', $fromTime);
      $noEndOfMonthDays = FALSE;
      break;
    case 'B': // Every Two Weeks
      $start_time = strtotime('+2 weeks', $fromTime);
      $noEndOfMonthDays = FALSE;
      break;
    case 'SM': // Twice Every Month
      // Need the 15th or 1st of the month, at least 15 days from now (avoid cutting any pay period short).
      // Special case: orders placed exactly on the 1st or 15th of the month don't have to be rounded up.
      // Rounding them up would result in a full month before the first recurrence instead of semi-monthly.
      $currentDayOfMonth = intval( date('j', $fromTime) );
      if ($currentDayOfMonth == 1) {
        // Next charge = 15th of current month
        $start_time = strtotime( date('Y-m-15 H:i:s', $fromTime) );
      } elseif ($currentDayOfMonth == 15) {
        // Charge on 1st of next month (advance to next month then set day to 1st).
        // Note: +1 month will advance the month counter by 2 when next month is too short, so use 16 < x < 28 days.
        $start_time = strtotime( date('Y-m-1 H:i:s', strtotime('+20 days', $fromTime)) );
      } elseif ($currentDayOfMonth > 15) {
        // At least 15 days = 15th of next month.
        $start_time = strtotime( date('Y-m-15 H:i:s', strtotime('+20 days', $fromTime)) );
      } else { // $currentDayOfMonth < 15 and > 1
        // At least 15 days = 1st of next month.
        // Use +1 month here, because it will never overflow into +2 months since no month has < 15 days.
        $start_time = strtotime( date('Y-m-1 H:i:s', strtotime('+1 month', $fromTime)) );
      }
      break;
    case '4W': // Every Four Weeks
      $start_time = strtotime('+4 weeks', $fromTime);
      $noEndOfMonthDays = FALSE;
      break;
    case 'M': // Monthly
      $start_time = strtotime('+1 month', $fromTime);
      break;
    case 'Q': // Quarterly
      $start_time = strtotime('+3 months', $fromTime);
      break;
    case 'SA': // Twice Every Year
      $start_time = strtotime('+6 months', $fromTime);
      break;
    case 'Y': // Yearly
      $start_time = strtotime('+1 year', $fromTime);
      break;
  }

  if ($noEndOfMonthDays && intval( date('j', $start_time) ) > 28) {
    // Add one week to increase the month counter while automatically handling any year increase.
    // Then set the day of the month to the first, retaining time of day for consistency (YYYY-MM-DD HH:MM:SS).
    // Basing this off of the start date calculated above is for periods > 1 month.
    $monthAfterOldStart = strtotime('+1 week', $start_time);
    $start_time = strtotime(date('Y-m-1 H:i:s', $monthAfterOldStart));
  }

  return $start_time;
}
/*******************************************************************************
 *
 * Recurring Billing Uberbart hooks
 *
 ******************************************************************************/
function uc_payflowpro_checkout_pane() {
  // Hide the panel if the recurring feature is not enabled.
  if(variable_get('uc_payflowpro_enable_recurring', false) == false) {
    return;
  }
  // Also hide the panel entirely if no items in the cart are renewable.
  $rs = uc_payflowpro_recurring_get_renewable_cart_products_rs();
  $row = db_fetch_array($rs);
  if (empty($row)) {
    return;
  }
  $panes[] = array(
    'id' => 'pfp_recurring_co_pane',
    'callback' => 'uc_payflowpro_recurring_checkout_pane_cart',
    'title' => t('Renewal Information'),
    'desc' => t('Details and options for renewable products (required if renewal is to be used).'),
    'weight' => 2,
    'process' => true,
    'collapsible' => FALSE,
  );
  return $panes;
}

function uc_payflowpro_recurring_checkout_pane_cart($op, &$arg1, $arg2) {
  switch($op) {
    case 'view':
      $description = t('Please review this information about your renewable products.');
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

function uc_payflowpro_recurring_get_renewable_cart_products_rs() {
  $items = uc_cart_get_contents();

  if (empty($items)) {
    return FALSE;
  }

  $vids = array();
  foreach($items as $k => $item) {
    $vids[] =  $item->vid;
  }

  if (empty($vids)) {
    return FALSE;
  }

  return db_query("SELECT n.title, model, terms, period, urp.vid FROM {uc_payflowpro_recurring_products} urp INNER JOIN {uc_products} p ON urp.vid = p.vid
                 INNER JOIN {uc_payflowpro_recurring_schedules} urs ON urp.sid = urs.sid INNER JOIN {node} n on n.nid = p.nid
                 WHERE urp.vid IN(%s)", implode(',', $vids));
}

function uc_payflowpro_recurring_co_pane_contents($order = '') {
  $period_map = _uc_payflowpro_recurring_periods();
  $rs = uc_payflowpro_recurring_get_renewable_cart_products_rs();
  $form = array();
  while($row = db_fetch_array($rs)) {
    // Store some information in the form that will be used to create the recurring profile.
    $form['renewable_products'][$row['vid']]['vid'] = array(
      '#type' => 'value',
      '#value' => $row['vid']);
    $form['renewable_products'][$row['vid']]['model'] = array(
      '#type' => 'value',
      '#value' => $row['model'],
    );
    $form['renewable_products'][$row['vid']]['period'] = array(
      '#type' => 'value',
      '#value' => $row['period'],
    );

    /*
     * If the schedule is set to zero terms (unlimited), display a note but no options.
     * Eventually, we may need to add a new term option (or new setting) that lets
     * a site admin indicate that users can choose whether or not to auto-renew, but
     * the only term available is unlimited.
     */
    $scheduleMaxTerms = intval($row['terms']);
    if ($scheduleMaxTerms < 1) {
      $form['renewable_products'][$row['vid']]['note'] = array(
        '#type' => 'markup',
        '#value' => '<p>' . t('@product will automatically renew @period.', array(
          '@product' => $row['title'],
          '@period' => $period_map[$row['period']],
        )) . '</p>',
      );
      $form['renewable_products'][$row['vid']]['renew'] = array(
        '#type' => 'value',
        '#value' => 1,
      );
      $form['renewable_products'][$row['vid']]['terms'] = array(
        '#type' => 'value',
        '#value' => 0, // Unlimited renewals value.
      );
    } else { // User can configure, let them choose
      $termOptions = array_merge(
        array( 0 => t('Until I Cancel') ) ,
        range(1,$scheduleMaxTerms)
      );
      $form['renewable_products'][$row['vid']]['renew'] = array(
        '#type' => 'checkbox',
        '#title' => t('Automatically renew @product @period', array(
          '@product' => $row['title'],
          '@period' => $period_map[$row['period']],
        )),
      );
      $form['renewable_products'][$row['vid']]['terms'] = array(
        '#type' => 'select',
        '#title' => t('Maximum number of times to renew @product', array(
          '@product' => $row['title'],
        )),
        '#options' => drupal_map_assoc($termOptions),
      );
    }
  }

  return $form;
}

/*******************************************************************************
 *
 * Recurring Billing settings/forms/functions
 *
 ******************************************************************************/
function uc_payflowpro_recurring_settings() {
  _uc_payflowpro_recurring_required();

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
  _uc_payflowpro_recurring_required();

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
    $tr[] = "Renewed " . $period_map[$row->period] . " for $row->terms periods.";
    $links = array();
    $links[] = array('title' => t('edit'), 'href' => 'admin/store/settings/uc_recurring/schedules/' . $row->sid . '/edit');
    $links[] = array('title' => t('delete'), 'href' => 'admin/store/settings/uc_recurring/schedules/' . $row->sid . '/delete');
    $tr[] = theme('links', $links);
    $rows[] = $tr;
  }
  $page .= theme('table', $header, $rows);
  return $page;
}

function uc_payflowpro_recurring_schedule_edit_form($form_state, $sid = '') {
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
  $terms = drupal_map_assoc( array_merge(
    array( 0 => 'Renew Forever' ),
    range( 1, 36 )
  ));
  $form['terms'] = array(
    '#type' => 'select',
    '#title' => t('Terms'),
    '#options' => $terms,
    '#description' => t('If Renew Forever, the shopper will be forced to automatically renew in the cart.'),
    '#default_value' => $s->terms,
  );
  $form['period'] = array(
    '#type' => 'select',
    '#title' => t('Period'),
    '#options' => _uc_payflowpro_recurring_periods(),
    '#default_value' => $s->period,
    '#description' => t('Monthly and yearly periods will advance the recurring date for the 29/30/31 of the month to the first of the next month.'),
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
    db_query("INSERT INTO {uc_payflowpro_recurring_schedules} (title, description, terms, period)
           VALUES('%s', '%s', '%s', '%s');",
           $form_state['values']['title'], $form_state['values']['description'],
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

function uc_payflowpro_recurring_schedule_delete_form($form_state, $sid) {
  $form = array();
  $form['sid'] = array('#type' => 'hidden', '#value' => $sid);
  return confirm_form($form, t('Are you sure you wish to remove this schedule?'), 'admin/store/settings/uc_recurring/schedules');
}

function uc_payflowpro_recurring_schedule_delete_form_submit($form, &$form_state) {
  db_query("DELETE FROM {uc_payflowpro_recurring_schedules} WHERE sid = %d", $form_state['values']['sid']);
  drupal_set_message(t('The schedule has been removed from the system.'));
  $form_state['redirect'] = 'admin/store/settings/uc_recurring/schedules';
}

/**
 * Recurring Products for
 */
function uc_payflowpro_recurring_products() {
  _uc_payflowpro_recurring_required();

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
  $rs = db_query("SELECT uc_pfp_rpid, urp.sid, model, period, terms, urp.vid FROM {uc_payflowpro_recurring_products} urp
                  INNER JOIN {uc_products} up ON urp.vid = up.vid
                  INNER JOIN {uc_payflowpro_recurring_schedules} urs ON urp.sid = urs.sid");
  $period_map = _uc_payflowpro_recurring_periods();
  while($row = db_fetch_object($rs)) {
    $tr = array();
    $tr[] = $row->model;
    if($row->terms == 0) {
      $row->terms = t('Unlimited');
    }
    $tr[] = "Renewed " . $period_map[$row->period] . " for $row->terms periods.";
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
function uc_payflowpro_recurring_products_edit_form($form_state, $uc_pfp_rpid = '') {
  $form = array();

  $products = array();
  if(!is_numeric($uc_pfp_rpid)) {
    $rs = db_query('SELECT nid, vid, model FROM {uc_products}
                   WHERE vid NOT IN (
                    SELECT vid FROM {uc_payflowpro_recurring_products}
                    )');
  }
  else {
    //TODO: this block is a hacky way to prevent you from changing the product ID, either make it no longer needed or make the field type='value' on edit instead.
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
  if (empty($schedules))
  {
    drupal_set_message(t('You must create a schedule first'), 'error');
    $schedules = array(-1 => t('You must create a schedule first'));
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
// Submit Recurring Products
function uc_payflowpro_recurring_products_edit_form_submit($form, &$form_state) {
  if(!isset($form_state['values']['uc_pfp_rpid'])) {
    $try = db_query("INSERT INTO {uc_payflowpro_recurring_products}(vid, sid)
                    VALUES(%d, %d)",
                    $form_state['values']['vid'], $form_state['values']['sid']);
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
                    WHERE uc_pfp_rpid = %d",
                    $form_state['values']['sid'], $form_state['values']['uc_pfp_rpid']);
    if(!$try) {
      drupal_set_message(t('There was an error adding your products.'), 'error');
    }
    else {
      drupal_set_message(t('Your recurring product option has been updaed.'));
      $form_state['redirect'] = 'admin/store/settings/uc_recurring/products';
    }
  }
  return;
}

// Delete
function uc_payflowpro_recurring_products_delete_form($form_state, $uc_pfp_rpid) {
  $form = array();
  $form['uc_pfp_rpid'] = array('#type' => 'hidden', '#value' => $uc_pfp_rpid);
  return confirm_form($form, t('Are you sure you wish to remove this schedule from this product?'), 'admin/store/settings/uc_recurring/products');
}
function uc_payflowpro_recurring_products_delete_form_submit($form, &$form_state) {
  db_query("DELETE FROM {uc_payflowpro_recurring_products} WHERE uc_pfp_rpid = %d", $form_state['values']['uc_pfp_rpid']);
  drupal_set_message(t('The product has had the selected schedule removed.'));
  $form_state['redirect'] = 'admin/store/settings/uc_recurring/products';
}

/*************
 * Helper function
 */

/**
 * For functions that must have recurring invoices enabled to work correctly,
 * warn the user if it is not.
 */
function _uc_payflowpro_recurring_required() {
  if( ! variable_get('uc_payflowpro_enable_recurring', 0) ) {
    trigger_error(t('Warning: You need to enable recurring products in the <a href="@gateway-settings">gateway settings</a> to use this tool.', array('@gateway-settings' => url('admin/store/settings/payment/edit/gateways'))));
  }
}

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

  _uc_payflowpro_recurring_required();

  $page = '';
  $page .= '<h2>' . t('Recurring Payments Panel') . '</h2>';

  if (arg(0) == 'admin') { // Admin entry point.
    // Admin version only accessible to admins, error out entirely if not one.
    if( ! user_access('administer store') ) {
      return drupal_access_denied();
    }

    $query = "SELECT pfp_pid pid, pfp_profile_id as profile_id, pfp_uid as uid, pfp_origid as pn_ref
                         FROM {uc_payflowpro_recurring_profiles}";
    $base_url = 'admin/store/settings/uc_recurring/administer';
  }
  else { // User account entry point.
    // If it's not your UID in user/%/pfp_cpanel and you're not an admin, denied.
    $user_id = intval(arg(1));
    if( $user_id != $user->uid && ! user_access('administer store') ) {
      return drupal_access_denied();
    }

    $query = "SELECT pfp_pid pid, pfp_profile_id as profile_id, pfp_uid as uid, pfp_origid as pn_ref
                         FROM {uc_payflowpro_recurring_profiles} WHERE pfp_uid = " . $user_id;
    $base_url = 'user/' .$user_id . '/pfp_cpanel';
  }

  // Dispatcher
  $profile_id = arg(0) == 'user' ? arg(3) : arg(5);
  if(arg(4) && ( strstr($profile_id, 'RP') || strstr($profile_id, 'RT') ) ) {
    if(arg(6) == 'cancel' || arg(4) == 'cancel') {
      return drupal_get_form('uc_payflowpro_cancel_form', $profile_id, $base_url);
    }
    elseif(arg(6) == 'cc_update' || arg(4) == 'cc_update') {
      return drupal_get_form('uc_payflowpro_cc_update_form', $profile_id, $base_url);
    }
    else {
      return uc_payflowpro_cpanel_view_profile($profile_id);
    }
  }

  $rs = pager_query($query);

  $page .= theme('pager');
  $header = array(t('Username'), t('Profile name'), t('Profile ID'), t('PN Ref'), 'Details');
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
    $row[] = l('View', $base_url . '/' . $crow->profile_id . '/view');
    $rows[] = $row;
  }
  $page .= theme('table', $header, $rows);
  $page .= theme('pager');
  return $page;
}

function uc_payflowpro_cpanel_view_profile($pid) {
  global $user;
  $mode = substr($pid,0,2) == 'RT' ? 'test' : 'live';

  $pidWhereClause = ''; // If no PID is provided, check for any results before showing the panel.
  if ($pid != '') {
    // PID provided, check perms specifically on that profile.
    $pidWhereClause = " AND pfp_profile_id='%s'";
  }
  $check = db_result(db_query("SELECT count(*) FROM {uc_payflowpro_recurring_profiles} WHERE pfp_uid = '%d' $pidWhereClause", $user->uid, $pid));
  if($check < 1 && !user_access('administer store')) {
    return drupal_access_denied();
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
  $update_link = $profile->getStatus() == 'ACTIVE' ? l(t('update credit card info'), $base_url . '/cc_update') : '';
  $page .= theme('payflowpro_tender', $profile->getTender(), $update_link);

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

function theme_payflowpro_tender($tender, $update_link = NULL) {
  $output = '';

  if(isset($tender['Card'])) {
    $tender = $tender['Card'];
    $output .= '<strong>' . t('Card') . "</strong>: {$tender->CardNum} ";
    if (!empty($update_link)) {
    	$output .= '&nbsp;&nbsp;&nbsp;' . $update_link;
    }
    $output .= '<br />';
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

function uc_payflowpro_cancel_form($form_state, $profile_id, $cancel_path) {
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
function uc_payflowpro_cancel_form_submit($form, &$form_state) {
  global $user;
	$pid = $form_state['values']['profile_id'];
  $order_id = $form_state['values']['order_id'];

  // Load up the profile
  $profile = new PayflowProRecurring($pid, _uc_payflowpro_get_auth());
  $profile->refresh();

  // Cancel
  $try = $profile->cancel();
  if(!$try) { // Failed
    $message = $profile->getReturnMsg();
    drupal_set_message(t('Unable to cancel your subscription at this time.  Error: @error_msg',
      array('@error_msg' => $message)), 'error');
    if (!empty($order_id)) {
      uc_order_comment_save($order_id, $user->uid,
        t('payflow: Recurring profile @pid cancel error: @error_msg.',
           array('@pid' => $pid, '@error_msg' => $message)
         ), 'admin');
    }
  }
  else { // Success
    drupal_set_message(t('Your subscription has been canceled.'));
  	if (!empty($order_id)) {
      // Leave an order comment
      uc_order_comment_save($order_id, $user->uid,
        t('payflow: Recurring profile @pid canceled.',
           array('@pid' => $pid)
         ), 'admin');
    }
  }

  // There could potentially be a hook here for better integration with uc_roles, etc.

  $form_state['redirect'] = $form_state['values']['post_dest'];
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

function uc_payflowpro_cc_update_form($form_state, $profile_id, $cc_update_path) {
  drupal_set_title(t('Update Credit Card Info'));

  $order_id = db_result(db_query("SELECT order_id FROM {uc_payflowpro_recurring_profiles} WHERE pfp_profile_id = '%s'", $profile_id));
  $order = uc_order_load($order_id);

	$form = array();
  $form['info'] = array('#value' => '<p>'.t('Complete the form below to change the credit card billed for this recurring order.') . '</p>');
  $form['profile_id'] = array('#type' => 'hidden', '#value' => $profile_id);
  $form['order_id'] = array('#type' => 'hidden', '#value' => $order_id);
  $form['#destination'] = $cc_update_path;
  $form['post_dest'] = array('#type' => 'hidden', '#value' => $cc_update_path);

  // Add the cc fields to the form.
  $form['cc_data'] = array(
    '#type' => 'fieldset',
    '#title' => t('Credit card details'),
    '#theme' => 'uc_payment_method_credit_form',
    '#tree' => TRUE,
  );
  $form['cc_data'] += uc_payment_method_credit_form(array(), $order);

  // The CC number will be returned in the format (Last 4) 4321, which might confuse the customer.
  unset($form['cc_data']['cc_number']['#default_value']);

  // The CC policy only makes sense in the cart context.
  unset($form['cc_data']['cc_policy']);

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Update'),
  );

  return $form;
}

function uc_payflowpro_cc_update_form_submit($form, &$form_state) {
  global $user;
  $pid = $form_state['values']['profile_id'];
  $order_id = $form_state['values']['order_id'];
  if (!empty($order_id)) {
    $order = uc_order_load($order_id);
  }

  // Load up the profile
  $profile = new PayflowProRecurring($pid, _uc_payflowpro_get_auth());
  $profile->refresh();

  // Set up the items to modify (currently only CC info supported).
  $profile->clearUpdate(); // Make sure nothing else gets changed.
  $payment = $form_state['values']['cc_data'];
  $tender['Type'] = 'C';
  $tender['CardNum'] = $payment['cc_number'];
  $tender['NameOnCard'] = $payment['cc_owner'];
  $tender['ExpDate'] = $payment['cc_exp_year'] . sprintf('%02d', $payment['cc_exp_month']);
  $tender['CVNum'] = $payment['cc_cvv'];
  $profile->setTender($tender);

  // Update remote data
  $try = $profile->update();
  if(!$try) { // Failed
    $message = $profile->getReturnMsg();
    drupal_set_message(t('Unable to update your subscription at this time.  Error: @error_msg',
      array('@error_msg' => $message)), 'error');
    if (!empty($order_id)) {
      uc_order_comment_save($order_id, $user->uid,
        t('payflow: Recurring profile @pid update error: @error_msg.',
           array('@pid' => $pid, '@error_msg' => $message)
         ), 'admin');
    }
  }
  else { // Success
    drupal_set_message(t('Your credit card information has been updated.'));
    if (!empty($order_id)) {
    	// Update the CC info listed in the order row.
    	foreach($form_state['values']['cc_data'] as $key => $value) {
    		$order->payment_details[$key] = $value;
    	}
    	uc_order_save($order);

    	// And leave a comment
    	uc_order_comment_save($order_id, $user->uid,
	      t('payflow: CC into for recurring profile @pid updated.',
	         array('@pid' => $pid)
	       ), 'admin');
    }
  }

  // We can add more code here to un-subscribe, deactivate certain features

  $form_state['redirect'] = $form_state['values']['post_dest'];
}
