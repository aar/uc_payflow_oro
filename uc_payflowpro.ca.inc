<?php
// $Id: uc_payflowpro.ca.inc,v 1.1.2.2 2010/04/08 20:18:34 kwinters Exp $

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
    '#description' => t('Payflow Pro recurring order post-completion processing.'),
    '#class' => 'payment',
    '#status' => 1,
    '#trigger' => 'uc_checkout_complete',
    '#actions' => array(
      array(
        '#name' => 'uc_payflowpro_process_recurring',
        '#title' => t('Assign automatically created users to the correct recurring profiles.'),
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
    '#title' => t('Assign automatically created users to the correct recurring profiles.'),
    '#category' => t('Payment'),
    '#callback' => 'uc_payflowpro_checkout_complete_processing',
    '#arguments' => array(
      'order' => $order_arg,
    ),
  );

  return $actions;
}