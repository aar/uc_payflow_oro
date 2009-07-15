<?php
// $Id$

/**
 * @file
 * Contains Conditional Actions hooks for recurring profiles.
 *
 * Because uc_cart_complete_sale can potentially create a user, and the
 * recurring profile entry references the uid, the recurring profile must
 * be created after uc_cart_complete_sale has completed.
 *
 * @author kwinters (kwinters@coalmarch.com)
 */

/******************************************************************************
 * Conditional Actions Hooks                                                  *
 ******************************************************************************/

/**
 * Implementation of hook_ca_predicate().
 */
function uc_payflowpro_ca_predicate() {
  $predicates = array();

  // Setup a default predicate for customer checkout notifications.
  $predicates['uc_checkout_complete_payflowpro'] = array(
    '#title' => t('Order Complete Recurring Profiles'),
    '#description' => t('Create recurring profiles when the checkout is complete.'),
    '#class' => 'payment',
    '#status' => 1,
    '#trigger' => 'uc_checkout_complete',
    '#actions' => array(
      array(
        '#name' => 'uc_payflowpro_process_recurring',
        '#title' => t('Create recurring profiles for the order.'),
        '#argument_map' => array(
          'order' => 'order',
        ),
      ),
    ),
  );

  return $predicates;
}

/**
 * Implementation of hook_ca_action().
 */
function uc_payflowpro_ca_action() {
  $order_arg = array(
    '#entity' => 'uc_order',
    '#title' => t('Order'),
  );

  $actions['uc_payflowpro_process_recurring'] = array(
    '#title' => t('Create recurring profiles for the order.'),
    '#category' => t('Payment'),
    '#callback' => 'uc_payflowpro_recurring_charge_process',
    '#arguments' => array(
      'order' => $order_arg,
    ),
  );

  return $actions;
}