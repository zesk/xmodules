<?php

/**
 * $URL: https://code.marketacumen.com/zesk/trunk/modules/commerce/classes/account.inc $
 * @package zesk
 * @subpackage objects
 */
namespace zesk;

/**
 * Account
 *
 * @author kent
 */
class Account extends Object {
	public function compute_balance() {
		$query = $this->application->class_query("Account_Transaction", "X")->what("*Balance", "SUM(X.Amount)")->where("Account", $this);
		
		$this->Balance = doubleval($query->where("Reconciled", true)->one("Balance", 0));
		$this->NewBalance = doubleval($query->where("Reconciled", false)->one("Balance", 0));
		if ($this->changed()) {
			$this->store();
		}
	}
	function balance_as_of(Timestamp $dt, $includingDate = true) {
		$balance = $this->member("CurrentBalance");
		$balance = $balance->copyObject($this);
		$where = $balance->option_array("Where");
		$cmp = $includingDate ? "le" : "lt";
		$where["EffectiveDate:$cmp"] = $dt->__toString();
		$balance->set_option("Where", $where);
		return $balance->get();
	}
	function payment() {
		return $this->Payment;
	}
	function payment_account() {
		if ($this->member_is_empty("PaymentAccount")) {
			$this->PaymentAccount = $this;
			$this->store();
		}
		if ($this->is_self_paid()) {
			return $this;
		}
		return $this->PaymentAccount;
	}
	function is_self_paid() {
		return $this->payment_account()->id() === $this->id();
	}
	function payment_is_valid() {
		if ($this->is_self_paid()) {
			$payment = $this->find_valid_payment();
			return $payment instanceof Payment;
		}
		return $this->payment_account()->payment_is_valid();
	}
	function find_valid_payment() {
		if ($this->payment_is_valid()) {
			return $this->payment();
		}
		$payments = $this->application->query_select("Payment")
			->where("IsValid", true)
			->where("Account", $this)
			->order_by("-Created")
			->object_iterator();
		foreach ($payments as $payment) {
			/* @var $payment Payment */
			$result = $payment->checkValid();
			if ($result === true) {
				$this->set_member("Payment", $payment);
				$this->store();
				return $payment;
			}
		}
		return false;
	}
	function has_valid_payment() {
		$paymentAccount = $this->payment_account();
		if ($paymentAccount->member_is_empty("Payment")) {
			return false;
		}
		$payment = $paymentAccount->payment();
		if (!$payment instanceof Payment) {
			return false;
		}
		if ($payment->is_valid()) {
			return true;
		}
		return false;
	}
	function referrerAccount() {
		$ref = $this->memberGet("Referrer");
		if ($ref) {
			$account = $ref->account();
			if ($account) {
				return $account;
			}
		}
		return false;
	}
	function manager_account() {
		return $this->memberGet("ManagerAccount");
	}
	function update_next_bill_date() {
		$lastBillDate = $this->LastBillDate;
		if (!$lastBillDate) {
			return false;
		}
		$this->NextBillDate = $this->next_bill_date_from($lastBillDate);
		return $this->store();
	}
	function bill_time_untis() {
		$units = $this->BillTimeUnit;
		if (empty($units)) {
			$units = $this->option("bill_time_unit", "month");
			$this->BillTimeUnit = $units;
			$unitCount = $this->option_integer("bill_time_unit_count", 1);
			$this->BillTimeUnitCount = $unitCount;
			$this->store();
		} else {
			$unitCount = $this->BillTimeUnitCount;
		}
		return array(
			$units,
			$unitCount
		);
	}
	private static function next_bill_date_from(Timestamp $here) {
		$here = clone $here;
		list($units, $unitCount) = $this->billTimeUnits();
		$here->addUnit($units, $unitCount);
		return $here;
	}
	function billedRecently(Timestamp $afterDate) {
		$query = $this->application->query_select("Payment_Transaction")->what("*LastDate", "MAX(X.ProcessDate)")->where("Account", $this);
		$query->where("Authorized", true)->where("ProcessDate|>=", $afterDate);
		$result = $query->one("LastDate");
		if ($result) {
			return Timestamp::factory($result);
		}
		return null;
	}
	
	//	// TODO: Prior to billing, update IsBillable in Account
	//	function referred_total_accounts($asOfDate=false)
	//	{
	//		if (!$asOfDate instanceof Timestamp) {
	//			$asOfDate = Timestamp::instance();
	//		}
	//		/*
	//		 * Count Accounts referred
	//		 * Added "Invoice" which means an account is not "counted" unless it's invoiced.
	//		 */
	//		$q = $this->query_select("X");
	//		$q->what("*Total", "COUNT(DISTINCT Account.ID)");
	//
	//		//		$q->addLink("StatsSite");
	//		$q->addLink("AccountReferrer", "Account.Referrer");
	//		$q->addJoin("Invoice", "inner", array("Account.ID:" => array("Invoice.Account", "Invoice.PaymentAccount")));
	//		$q->setWhere(
	//		array(
	//		"AccountReferrer.Account" => $this,
	//		"Account.ActivateDateTime:le" => array(null,$asOfDate),
	//		"Account.Cancelled:ge" => array(null, $asOfDate),
	//		)
	//		);
	//
	///*		$sql =
	//		"SELECT COUNT(DISTINCT A.ID) as Total
	//			FROM zeskAccount A
	//			INNER JOIN zeskStatsSite S ON S.Account=A.ID
	//			INNER JOIN zeskAccountReferrer R ON R.ID=A.Referrer
	//			INNER JOIN zeskInvoice I ON (I.Account=A.ID OR I.PaymentAccount=A.ID)
	//			WHERE
	//				R.Account=$this->ID
	//				AND S.ActivateDateTime <= '".$asOfDate->__toString()."'
	//				AND (S.Deactivated IS null OR S.Deactivated >= '".$asOfDate->__toString()."')
	//			";
	//*/
	//		$result = $q->oneQuery("Total", 0);
	//
	////		echo "\n\n$result = \n\t";
	////		echo $q->toSQL();
	////		echo "\n\n";
	//
	//		return $result;
	//	}
	function managed_total_accounts($asOfDate = false) {
		if (!$asOfDate instanceof Timestamp) {
			$asOfDate = Timestamp::now();
		}
		$q = $this->query_select()->where("ManagerAccount", $this)->where("Cancelled|<=", array(
			$asOfDate,
			null
		));
		$q->what("*Total", "COUNT(ID)");
		return $q->one_integer("Total");
	}
	private function determine_commission() {
		$commission = $this->Commission;
		if ($commission) {
			return $commission;
		}
		$accountType = $this->AccountType;
		if (!$accountType) {
			return null;
		}
		return $accountType->DefaultCommission;
	}
	function commission() {
		$commission = $this->_commission();
		if (!$commission) {
			return null;
		}
		if (!$commission->isDiscount()) {
			return $commission;
		}
		return null;
	}
	function discount() {
		$commission = $this->_commission();
		if (!$commission) {
			return null;
		}
		if ($commission->isDiscount()) {
			return $commission;
		}
		return null;
	}
	function referrerCommission() {
		$referrer = $this->referrerAccount();
		if (!$referrer) {
			return false;
		}
		return $referrer->commission();
	}
	
	//	private function _lastInvoiceQuery(Timestamp $fromDate=null)
	//	{
	//		$q = CModelClass::queryFromType("Invoice");
	//		$where["Account"] = $this;
	//		if ($fromDate instanceof Timestamp && !$fromDate->isEmpty()) {
	//			$where["BillDate:lt"] = $fromDate;
	//		}
	//		$q->setWhere($where);
	//		$q->setOrderBy("-BillDate");
	//		$q->setLimit(1);
	//		return $q;
	//	}
	//
	//	function lastInvoice(Timestamp $fromDate)
	//	{
	//		$q = $this->_lastInvoiceQuery($fromDate);
	//		return $q->classQueryOne();
	//	}
	//
	//	function lastInvoiceDate(/*Timestamp */$fromDate=null, $create=false)
	//	{
	//		$q = $this->_lastInvoiceQuery($fromDate);
	//		$q->setWhat("BillDate");
	//		$result = $q->oneQueryDateTime("BillDate");
	//		if ($result instanceof Timestamp) {
	//			return $result;
	//		}
	//		if (!$create) {
	//			return $result;
	//		}
	//		return new Timestamp();
	//	}
	function killAccount() {
		$this->application->query_update(__CLASS__)->values(array(
			"Cancelled" => "now",
			"IsBillable" => false,
			"NextBillDate" => null,
			"PaymentAccount" => null,
			"ManagerAccount" => null,
			"Payment" => null
		))->where(array(
			"id" => $this->id()
		));
	}
	
	/**
	 *
	 * @param
	 *        	sdate is the start date of this invoice
	 * @param
	 *        	edate returns the resulting end date of the clickthroughs
	 * @return array of CInvoiceParameter
	 */
	function generateInvoiceParameters(Timestamp $billDate, $forceBillUnit = false) {
		$p = new Invoice_Parameters($this, $billDate, $forceBillUnit);
		
		$now = Timestamp::now();
		if ($billDate->difference($now, kTimeUnit_Day) >= 21) {
			$p->messageAdd(__("Your account is behind in our billing, please note the effective date of this invoice. We appreciate your understanding!"));
			$p->messageAdd(__("Your account will be billed weekly until it is up to date, at which point it will be billed monthly."));
		}
		$links = $this->find_billable_items();
		if (!$links) {
			return false;
		}
		foreach ($links as $name) {
			$member = $this->member($name);
			$object = $member->linkedInstance(ID_NOOP);
			if (!$object) {
				continue;
			}
			$object->invoiceParameters($p);
		}
		return $p;
	}
	function generate_discounts(Invoice_Parameters $p) {
		$result = array();
		$links = $this->find_billable_items();
		if (!$links) {
			return $result;
		}
		foreach ($links as $name) {
			$member = $this->member($name);
			$object = $member->linkedInstance(ID_NOOP);
			$object->invoiceDiscount($p);
		}
		return true;
	}
	private function find_billable_items() {
		$c = $this->modelClass();
		return $c->linkFindByOption("Billable");
	}
	
	/*
	 * Commissions and Invoices
	 * @todo Fix this, rewrite
	 */
	private function _transactionQuery($invoiceID, $endDate) {
		$accountID = $this->id();
		
		$q = Object::class_query("Account_Transaction");
		$q->link("Account_Transaction_Type");
		$q->link("Invoice");
		$q->link("Invoice_Item");
		$q->where(array(
			"Account" => $this,
			"EffectiveDate|<=" => $endDate,
			//				"InvoiceItem.Invoice" => array($invoiceID, null), /* OR */
			array(
						/* Things already linked to this invoice */
						array(
					"Invoice.PaymentInvoice" => $invoiceID
				),	 /* OR */
						array(
					"Invoice.PaymentInvoice" => null,
					"*Invoice.PaymentAccount" => "AccountTransaction.Account",
					"*Invoice.Account|!=" => "AccountTransaction.Account"
				),	 /* OR */
						array(
					"InvoiceItem.Invoice" => $invoiceID
				),	 /* OR */
						/* Unreconciled transactions paid by us */
						array(
					"Reconciled" => false,
					"Invoice.PaymentAccount" => $accountID,
					"AccountTransactionType.InvoiceShow" => true,
					"InvoiceItem.ID" => null
				),	 /* OR */
						/* Reconciled transactions paid to us, not appearing on other invoices */
						array(
					"Reconciled" => true,
					"Invoice.PaymentAccount:ne" => $accountID,
					"AccountTransactionType.InvoiceShow" => true,
					"InvoiceItem.ID" => null
				),	 /* OR */
						/* Reconciled transactions paid to us, not appearing on other invoices */
						array(
					"Reconciled" => false,
					"Invoice.ID:ne" => $invoiceID,
					"Invoice.PaymentAccount" => $accountID
				),	 /* OR */
						array(
					"Reconciled" => false,
					"AccountTransaction.Invoice" => NULL,
					"AccountTransaction.InvoiceItem" => NULL,
					"AccountTransaction.PaymentTransaction" => NULL,
					"AccountTransactionType.InvoiceShow" => true
				) /* OR */
				//TODO: When PaymentAccount=CommissionAccount, copy the commissions into this invoice
			)
		));
		$q->order_by(array(
			"EffectiveDate",
			"OrderIndex"
		));
		//		CDebug::dump($q->toSQL()); exit();
		return $q;
	}
	function update_balance_all() {
		$sql = explode(";", Template::instance(ZESK_COMMERCE_ROOT . "theme/account/update-balances.sql.tpl"));
		return $this->database()->query($sql) ? true : false;
	}
	function issue_check($checkNumber, Timestamp $when, $amount, $description = "") {
		$fields = array(
			"TransactionGroup" => $checkNumber,
			"Account" => $this,
			"TransactionType" => "check",
			"EffectiveDate" => $when,
			"Description" => $description,
			"Amount" => -abs($amount),
			"Reconciled" => true
		);
		return Account_Transaction::register_transaction($fields);
	}
	function is_cancelled() {
		return !$this->member_is_empty("Cancelled");
	}
	protected function hook_permission(User $user, $action) {
		if ($user->member_integer("Account") === $this->id()) {
			return true;
		}
		return false;
	}
}
