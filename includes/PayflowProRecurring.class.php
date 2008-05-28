<?php

class PayflowProRecurring {
  protected $pid = null;
  protected $profile_id = null;
  protected $profile_name = null;
  protected $profile_term = null;
  protected $profile_start = null;
  protected $profile_end = null;
  protected $profile_payperiod = null;
  protected $profile_amt = null;
  protected $profile_tender = null;
  protected $profile_email = null;
  protected $profile_company_name = null;
  protected $profile_billto = null;
  protected $profile_shipto = null;
  protected $profile_payments_left = null;
  protected $profile_aggregate_amt = null;
  protected $profile_aggregate_trans_amt = null;
  protected $profile_num_failed_payments = null;
  protected $profile_retry_num_days = null;
  protected $profile_next_payment = null;
  protected $profile_exp_data = array();
  protected $profile_status = 'N/A';
  
  protected $payment_history = array();
  protected $auth = array();
  protected $mode = null;
  
  protected $return_code = '';
  protected $return_msg = '';
  
  private $last_payment_status = '';
  
  protected $update_items = array();
  protected $create_new = false;
  protected $loaded = false;
  /**
   * Takes a profile ID to load
   */
  function __construct($pid = null, $auth = array()) {
    // Empty constructor
    if($pid != null) {
      $this->profile_id = $pid;
      $this->auth = $auth;
      if(substr($pid,0,2) == 'RT') {
        $this->mode = 'test';
      }
      else {
        $this->mode = 'live';
      }
    }
  }
  
  function createNew() {
    if($this->loaded) {
      throw new Exception('Attempt to CREATE NEW PROFILE on and already loaded PROFILE');
    }
    $this->create_new = true;
  }
  
  function setMode($mode) {
    $this->mode = $mode;
  }
  
  public function refresh() {
    $this->load();
  }
  /**
   * Inquiries about the profile and loads the inforamtion
   * needed about the profile.
   */
  private function load() {
    $profile = $this->runInquiry();
    $profile = (object)$profile->RecurringProfileResult;
    $this->loaded = true;
    
    // Set the varts
    $this->profile_name = (string)$profile->Name;
    $this->profile_start = (string)$profile->Start;
    $this->profile_end = (string)$profile->End;
    $this->profile_term = (string)$profile->Term;
    $this->profile_status = (string)$profile->Status;
    $this->profile_payperiod = (string)$profile->PayPeriod;
    $this->profile_retry_num_days = (string)$profile->RetryNumDays;
    $this->profile_email = (string)$profile->EMail;
    $this->profile_company_name = (string)$profile->CompanyName;
    $this->profile_amt = (string)$profile->Amt['Currency']; // Hack for bug from PFP
    $this->profile_payments_left = (string)$profile->PaymentsLeft;
    $this->profile_next_payment = (string)$profile->NextPayment;
    $this->profile_aggregate_amt = (string)$profile->AggregateAmt;
    $this->profile_aggregate_trans_amt = (string)$profile->AggregateOptionalTransAmt;
    $this->profile_num_failed_payments = (string)$profile->NumFailedPayments;
    $this->profile_tender = (array)$profile->Tender;
    $this->profile_billto = (array)$profile->BillTo->Address;
    $this->profile_shipto = (array)$profile->ShipTo->Address;
    $this->profile_exp_data = (array)$profile->ExpData;
    
    // Load Payment History
    $history = $this->runInquiry(true); // True to get the history
    
    $history = (object)$history->RecurringProfileResult;
    $history = array($history);
    foreach($history as $k => $payment) {
      if(!isset($payment->RPPaymentResult)) {
        continue;
      }
      $payment = (object)$payment->RPPaymentResult;
      $payment->Amount = (float)$payment->Amt['Currency'];  
      $this->payment_history[] = (array)$payment;
    }
    if(count($this->payment_history) === 0) {
      $this->last_payment_status = null;
    }
    else {
      $lp_index = count($this->payment_history) - 1;
      $this->last_payment_status = isset($this->payment_history[$lp_index]['Result']) ? (int)$this->payment_history[$lp_index]['Result'] : null;
    }
    
    return;
  }
  
  public function getLastPaymentStatus() {
    return $this->last_payment_status;
  }
  public function getLastPayment() {
    $lp_index = count($this->payment_history) - 1;
    if($lp_index >= 0) {
      return $this->payment_history[$lp_index];
    }
    else {
      return null;
    }
  }
  
  /**
   * Function to abstract the recurring functions methods
   * of the xml
   */
  private function runInquiry($history = false) {
    $profile_id = $this->getProfileID();
    $options = array();
    
    # Build XML
    $transaction = '';
    // Wrap tine inquirty
    $transaction .= $this->getActionInquiry($history);
    // Wrap in a profile
    //$options['custref'] = $this->getProfileID();
    $transaction = $this->profileWrap($transaction, $options);
    
    // Final wrap
    $xml = $this->recurringXMLPayWrap($transaction);
    
    // Send XML
    $response = $this->sendTransaction($xml);
    
    // Return the SimpleXml Object
    return $response;
  }
  
  /**
   * Cancels this subscription
   */
  public function cancel() {
    $profile_id = $this->getProfileID();
    $options = array();
    
    # Build XML
    $transaction = '';
    // Get the cancel transaction xml
    $action = $this->getCancelXML();
    // Wrap the cancel transction in a profile
    $transaction = $this->profileWrap($action);
    
    // Final Wrap
    $xml = $this->recurringXMLPayWrap($transaction);
    
    // Send XML
    $response = $this->sendTransaction(trim($xml));
    
    if($response->RecurringProfileResult->Result == 0) {
      return true;
    }
    else {
      $this->return_code = (int)$response->RecurringProfileResult->Result;
      $this->return_msg = _payflowpro_code_to_string($this->returnCode);
      return false;
    }
    return $response;
  }
  
  public function getReturnCode() {
    return $this->return_code;
  }
  public function getReturnMsg() {
    return $this->return_msg;
  }
  
  /**
   * Saves/Updates this profiles
   * This is only used when creating a new
   * profile.
   */
  public function save() {
    $this->isCreateNew();
    # Build XML
    $transaction = '';
    
    // Fetch the RPData
    $rpdataxml .= $this->getAddXML();
    
    // Wrap in a profile
    //$options['custref'] = $this->getProfileID();
    $transaction = $this->profileWrap($rpdataxml, $options);
    
    // Final wrap
    $xml = $this->recurringXMLPayWrap($transaction);
    
    // Send XML
    $response = $this->sendTransaction($xml);
    // Setup the xml
    
    return $response;
  }
  
  private function parsePFPResponse($response, $type = '') {
  }
  
  function payflowproRecurringFactory($args = array()) {
  }
  
  private function sendTransaction($xml_request) {
    
    $xml_request = trim($xml_request);
    
    if(module_exists('payment_payflowpro')) {
      $response = _payflowpro_send_transaction($xml_request, $this->mode);
    }
    else if(module_exists('uc_payflowpro')) {
      $response = _uc_payflowpro_submit_xml($xml_request, $this->mode);
    }

    return $response;
  }
  
  function findProfile() {
  }
  
  private function profileWrap($action, $options = array()) {
    
    if($options['id']) {
      $id_opt = ' ID=' . $options['id'];
    }
    if($options['custref']) {
      $custref_opt = ' CustRef="' . $options['custref'] . '"';
    }
    
    $xml = '<RecurringProfile' . $id_opt . $custref_opt . '>' . $action . '</RecurringProfile>';
    return $xml;
  }
  
  private function getActionInquiry($history = false) {
    if($history) {
      $xml = '<Inquiry>
                <ProfileID>' . $this->getProfileID() . '</ProfileID>
                <PaymentHistory>Y</PaymentHistory>
              </Inquiry>';
    }
    else {
      $xml = '<Inquiry>
                <ProfileID>' . $this->getProfileID() . '</ProfileID>
              </Inquiry>';
    }
    return $xml;
  }
  
  private function getCancelXML() {
    $xml = '<Cancel><ProfileID>' . $this->getProfileID() . '</ProfileID></Cancel>';
    return $xml;
  }
  
  /**
   * Call this function to add someone.
   * The getRPDataXML is a HELPER function for this one.
   */
  private function getAddXML($optional_tran = null) {
    $xml = '';
    $xml = '<Add>' .
      $this->getTenderXML() .
      $this->getRPDataXML($optional_tran) .
      '</Add>';
    return $xml;
  }
  
  /**
   * Do NOT call this function directly. This is a helper function
   * to other functions.
   */
  private function getRPDataXML($optional_tran = null) {
    $optionstrans = '';
    if($optional_tran != null) {
      $optionstrans = '<OptionalTrans>Sale</OptionalTrans>
              <OptionalTransAmt>' . $optional_tran['amt'] . '</OptionalTransAmt>';
    }
    $xml = '
            <RPData>
              <Name>' . $this->getName() . '</Name>
              <TotalAmt>' . $this->getAmt() . '</TotalAmt>
              <Start>' . $this->getStartDate() . '</Start>
              <Term>' . $this->getTerm() . '</Term>
              <PayPeriod>' . $this->getPayPeriod() . '</PayPeriod>
              <EMail>' . $this->getEmail() . '</EMail>
              ' . $optionstrans . 
              $this->getAddressXML('billto') .
              $this->getAddressXML('shipto') . 
            '</RPData>';
    return $xml;
  }
  private function getTenderXML() {
    $xml = '';
    $tender = $this->profile_tender;
    if($tender['CVNum']) {
      $cvnum = '<CVNum>' . $tender['CVNum'] . '</CVNum>';
    }
    $xml = '<Tender>
              <Card>
              <CardNum>' . $tender['CardNum'] . '</CardNum>
              <ExpDate>' . $tender['ExpDate'] . '</ExpDate>
              <NameOnCard>' . $tender['NameOnCard'] . '</NameOnCard>
              ' . $cvnum . '
              </Card>
              </Tender>';
    return $xml;
  }
  
  /**
   *
   * This function runs through the items that have been set
   * to be updated, and then sends the information to PFP
   */
  public function update() {
    return;
  }
  
  public function setUpdate($item, $val) {
    $this->update_items[$item] = $val;
    return;
  }
  public function clearUpdate() {
    $this->update_items = array();
  }
  /**
   * Returns the array of the update states that will be updated
   * upon running the 'save' function.
   */
  public function getCurrentUpdateState() {
    return $this->update_items;
  }
  
  public function getProfileID() {
    return $this->profile_id;
  }
  
  public function parseResults(SimpleXMLElement $result) {
    $status_code = (int)$result->Result;
    $status_msg = (string)$result->Message;
    $this->setStatus((string)$result->Status);
  }
  
  private function setStatus($status) {
    $this->profile_status = $status;
  }
  
  public function getName() {
    return $this->profile_name;
  }
  public function setName($name) {
    $this->profile_name = $name;
  }
  
  public function getTerm() {
    return $this->profile_term;  
  }
  // TODO: Create new only
  public function setTerm($term) {
    $this->isCreateNew();
    $this->profile_term = $term;
  }
  
  private function isCreateNew() {
    if(!$this->create_new) {
      throw new Exception('ERROR: NOT IN CORRECT STATE');
    }
    return;
  }
  
  public function setAuth($auth) {
    $this->auth = (array)$auth;
  }
  public function getEndDate($format = 'raw') {
    if( $this->getStatus() == 'ACTIVE' && $this->getTerm() == 0 && $this->profile_end == '' ) {
      return 'Renewed until user cancels';
    }
    if($format != 'raw') {
      $date = $this->parsePayFlowDate($this->profile_end);
      $formatted_date = format_date($date, 'custom', $format);
    }
    else {
      $formatted_date = $this->profile_end;
    }
    return $formatted_date;
  }
  public function setEnd($end) {
    $this->profile_end = $end;
  }
  public function setStartDate($date) {
    $this->isCreateNew();
    $this->profile_start = $date;
  }
  
  public function getPayPeriod() {
    return $this->profile_payperiod;
  }
  public function setPayPeriod($period) {
    $this->profile_payperiod = $period;
  }
  
  public function getAmt() {
    return $this->profile_amt;
  }
  public function setAmt($amt) {
    $this->profile_amt = $amt;
  }
  
  public function getEmail() {
    return $this->profile_email;
  }
  public function setEmail($email) {
    $this->profile_email = $email;
  }
  
  public function getCompanyName() {
    return $this->profile_company_name;
  }
  public function setCompanyName($name) {
    $this->profile_company_name = $name;
  }
  
  // Pehraps return these as classes
  public function getBillTo() {
    return $this->profile_billto;
  }
  public function setBillTo($billing) {
    if(!$this->create_new) {
      throw new Exception('Attempt to set billing information out of CREATE NEW mode.');
    }
    $this->profile_billto = $billing;
  }
  public function getShipTo() {
    return $this->profile_shipto;
  }
  public function setShipTo($shipping) {
    if(!$this->create_new) {
      throw new Exception('Attempt to set shippin information out of CREATE NEW mode.');
    }
    $this->profile_shipto = $shipping;
  }
  
  // Read-only vars
  public function getPaymentsLeft() {
    return $this->profile_payments_left;
  }
  
  public function getNextPaymentDate($format = 'raw') {
    switch($format) {
      case 'raw':
        return $this->profile_next_payment;
      break;
      case 'unix':
        return $this->parsePayFlowDate($this->profile_next_payment);
      break;
      default:
        $date = $this->parsePayFlowDate($this->profile_next_payment);
        return format_date($date, 'custom', $format);
        break;
    }
    
    return $this->profile_next_payment;
  }
  public function getAggregateAmt() {
    return $this->profile_aggregate_amt;
  }
  public function getAggregateOptionalTransAmt() {
    return $this->profile_aggregate_trans_amt;
  }
  public function getNumFailedPayments() {
    return $this->profile_num_failed_payments;
  }
  public function getTender() {
    return $this->profile_tender;
  }
  public function setTender($tender) {
    $this->profile_tender = $tender;
  }
  public function getStatus() {
    return $this->profile_status;
  }
  public function getPaymentHistory() {
    return $this->payment_history;
  }
  
  public function getStartDate($format = 'raw') {
    if($format != 'raw') {
      $date = $this->parsePayFlowDate($this->profile_start);
      $formated_date = format_date($date, 'custom', $format);
    }
    else {
      $formated_date = $this->profile_start;
    }
    return $formated_date;
  }
  
  // Helpers
  /**
   * Format a date into Payflow time
   * Types are:
   * - pfp
   * - cc
   */
  private function parsePayFlowDate($date) {
    $date = strtotime(substr($date,0,2) . '/' . substr($date,2,2) . '/' . substr($date,4,4));
    return $date;
  }
  private function getAddressXML($type) {
    if($type == 'billto') {
      $type = 'BillTo';
      $addy = $this->getBillTo();
    }
    if($type == 'shipto') {
      $type = 'ShipTo';
      $addy = $this->getShipTo();
    }
    
    if(count($addy) == 0) {
      return ;
    }
    
    $xml = '<' . $type . '>
            <Address>
              <Street>' . $addy['Street'] . '</Street>
              <City>' . $addy['City'] . '</City>
              <State>' .  $addy['State'] . '</State>
              <Zip>' . $addy['Zip'] . '</Zip>
            </Address>
          </' . $type . '>';
    return $xml;
  }
  
  
  /************************************************************
   * 
   * Static functions
   *  
   ************************************************************ 
   */
  /**
   * Auth array in the args:
   * - vendor
   * - partner
   * - user
   * - password
   */
  private function recurringXMLPayWrap($transaction) {
     $auth = $this->auth;
     $vendor = $auth['vendor'];
     $partner = $auth['partner'];
     $user = $auth['user'];
     $password = $auth['password'];
     
     $xml = '<?xml version="1.0" encoding="UTF-8"?>
      <XMLPayRequest Timeout="30" version = "2.0" xmlns="http://www.paypal.com/XMLPay">
        <RequestData>
          <Vendor>' . $vendor . '</Vendor>
          <Partner>' . $partner . '</Partner>
          <RecurringProfiles>
          ' . $transaction . '
          </RecurringProfiles>
        </RequestData>
        <RequestAuth>
          <UserPass>
            <User>' . $user . '</User>
            <Password>' . $password . '</Password>
          </UserPass>
        </RequestAuth>
      </XMLPayRequest>';
      
      return $xml;
  }
  
}

class PayflowProResult {
  function __construct() {
  }
  
  function setStatusCode($status) {
  }
}


class PayflowProClient {
// Submit request to PayFlow
  
  private $mode = null;
  
  public function __construct() {
    $this->mode = variable_get('ec_payflowpro_tx_mode', 'test');
  }
  
  public function setMode($mode) {
    $this->mode = $mode;
  }
  
  public static function submitTransaction($xml_request, $mode = 'test') {
    // DEBUG: Return false to not submit
    
    // Info
    $certpath = variable_get('ec_payflowpro_sdk_cert_path', '');
    
    if ($mode == 'test') {
      $url = 'https://pilot-payflowpro.verisign.com/transaction:443';
    }
    else {
      $url = 'https://payflowpro.verisign.com/transaction:443';
      
    }
  
    $request_id = sha1($xml_request . time());
    $user_agent = 'Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0)';
  
    $headers[] = "Content-Type: text/xml"; // either text/namevalue or text/xml
    $headers[] = "X-VPS-Timeout: 30";
    $headers[] = "X-VPS-VIT-OS-Name: Linux";  // Name of your Operating System (OS)
    $headers[] = "X-VPS-VIT-OS-Version: RHEL 4";  // OS Version
    $headers[] = "X-VPS-VIT-Client-Type: PHP/cURL";  // Language you are using
    $headers[] = "X-VPS-VIT-Client-Version: 1.0";  // For your info
    $headers[] = "X-VPS-VIT-Client-Architecture: x86";  // For your info
    $headers[] = "X-VPS-VIT-Client-Certification-Id: 33baf5893fc2123d8b191d2d011b7fdc"; // This header requirement will be removed
    $headers[] = "X-VPS-VIT-Integration-Product: Ubercart";  // For your info, would populate with application name
    $headers[] = "X-VPS-VIT-Integration-Version: 2.0"; // Application version
    $headers[] = "X-VPS-Request-ID: " . $request_id;
  
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_HTTPHEADER, $headers);
    curl_setopt($ch, CURLOPT_USERAGENT, $user_agent);
    curl_setopt($ch, CURLOPT_HEADER, FALSE);
    curl_setopt($ch, CURLOPT_POST, TRUE);
    curl_setopt($ch, CURLOPT_POSTFIELDS, $xml_request);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, TRUE);
    curl_setopt($ch, CURLOPT_CAPATH, $certpath);
    //curl_setopt($ch, CURLOPT_SSL_VERIFYPEER, FALSE);
    curl_setopt($ch, CURLOPT_SSL_VERIFYHOST, FALSE);
    curl_setopt($ch, CURLOPT_FORBID_REUSE, TRUE);
    curl_setopt($ch, CURLOPT_TIMEOUT, 15);
    curl_setopt($ch, CURLOPT_FOLLOWLOCATION, FALSE);
    $curl_response = curl_exec($ch);
    if (!$curl_response) {
      watchdog('payment_payflowpro', 'Connecting to PayFlow server failed: ' . curl_error($ch), WATCHDOG_ERROR);
    }
    curl_close($ch);
  
    if ($curl_response == '') {
      $response = FALSE;
    }
    else {
      $xml_response = simplexml_load_string($curl_response);
      $response = $xml_response->ResponseData->TransactionResults->TransactionResult;
    }
    return $response;
  }
  

}


// REG FUN
function _test_xml($id = 1) {
    $xml[1] = '
<?xml version="1.0" encoding="UTF-8"?>
    <XMLPayRequest Timeout="30" version = "2.0">
      <RequestData>
        <Vendor>wbrnewmedia</Vendor>
        <Partner>PayPal</Partner>
        <RecurringProfiles>
          <RecurringProfile>
            <Profile>
              <Inquiry>
                <ProfileID>RP0000000012</ProfileID>
              </Inquiry>
            </Profile>
          </RecurringProfile>
        </RecurringProfiles>
      </RequestData>
      <RequestAuth>
        <UserPass>
          <User>WebApp</User>
          <Password>WebApp123</Password>
        </UserPass>
      </RequestAuth>
    </XMLPayRequest>';
    
    $xml[2] = '
<?xml version="1.0" encoding="UTF-8"?>
<XMLPayRequest Timeout="40" version="2.0" xmlns="http://www.verisign.com/XMLPay">
 <RequestData>
        <Vendor>wbrnewmedia</Vendor>
        <Partner>PayPal</Partner>
  <Transactions>
<Transaction>
<Sale>
<PayData>
<Invoice>
<NationalTaxIncl>false</NationalTaxIncl>
<TotalAmt>24.97</TotalAmt>
</Invoice>
<Tender>
<Card>
<CardType>visa</CardType>
<CardNum>5105105105105100</CardNum>
<ExpDate>200911</ExpDate>
<NameOnCard/>
</Card>
</Tender>
</PayData>
</Sale>
</Transaction>
  </Transactions>
 </RequestData>
      <RequestAuth>
        <UserPass>
          <User>WebApp</User>
          <Password>WebApp123</Password>
        </UserPass>
      </RequestAuth>
</XMLPayRequest>';

    return $xml[$id];
  }

function _my_submit($xml_request, $mode = 'test') {
    // DEBUG: Return false to not submit
    
    // Info
    $certpath = variable_get('ec_payflowpro_sdk_cert_path', '');
    
    if ($mode == 'test') {
      $url = 'https://pilot-payflowpro.verisign.com/transaction:443';
    }
    else {
      $url = 'https://payflowpro.verisign.com/transaction:443';
      
    }
  
    $request_id = sha1($xml_request . time());
    $user_agent = 'Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0)';
  
    $headers[] = "Content-Type: text/xml"; // either text/namevalue or text/xml
    $headers[] = "X-VPS-Timeout: 30";
    $headers[] = "X-VPS-VIT-OS-Name: Linux";  // Name of your Operating System (OS)
    $headers[] = "X-VPS-VIT-OS-Version: RHEL 4";  // OS Version
    $headers[] = "X-VPS-VIT-Client-Type: PHP/cURL";  // Language you are using
    $headers[] = "X-VPS-VIT-Client-Version: 1.0";  // For your info
    $headers[] = "X-VPS-VIT-Client-Architecture: x86";  // For your info
    $headers[] = "X-VPS-VIT-Client-Certification-Id: 33baf5893fc2123d8b191d2d011b7fdc"; // This header requirement will be removed
    $headers[] = "X-VPS-VIT-Integration-Product: Ubercart";  // For your info, would populate with application name
    $headers[] = "X-VPS-VIT-Integration-Version: 2.0"; // Application version
    $headers[] = "X-VPS-Request-ID: " . $request_id;
  
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_HTTPHEADER, $headers);
    curl_setopt($ch, CURLOPT_USERAGENT, $user_agent);
    curl_setopt($ch, CURLOPT_HEADER, FALSE);
    curl_setopt($ch, CURLOPT_POST, TRUE);
    curl_setopt($ch, CURLOPT_POSTFIELDS, $xml_request);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, TRUE);
    curl_setopt($ch, CURLOPT_CAPATH, $certpath);
    //curl_setopt($ch, CURLOPT_SSL_VERIFYPEER, FALSE);
    curl_setopt($ch, CURLOPT_SSL_VERIFYHOST, FALSE);
    curl_setopt($ch, CURLOPT_FORBID_REUSE, TRUE);
    curl_setopt($ch, CURLOPT_TIMEOUT, 15);
    curl_setopt($ch, CURLOPT_FOLLOWLOCATION, FALSE);
    $curl_response = curl_exec($ch);
    if (!$curl_response) {
      watchdog('payment_payflowpro', 'Connecting to PayFlow server failed: ' . curl_error($ch), WATCHDOG_ERROR);
    }
    curl_close($ch);
  
    if ($curl_response == '') {
      $response = FALSE;
    }
    else {
      $xml_response = simplexml_load_string($curl_response);
      $response = $xml_response->ResponseData->TransactionResults->TransactionResult;
    }
    return $response;
  }

?>