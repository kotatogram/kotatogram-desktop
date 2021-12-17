/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_phone.h"

#include "lang/lang_keys.h"
#include "intro/intro_code.h"
#include "intro/intro_qr.h"
#include "styles/style_intro.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/special_fields.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "data/data_user.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/phone_banned_box.h"
#include "core/application.h"
#include "countries/countries_instance.h" // Countries::Groups

namespace Intro {
namespace details {
namespace {

bool AllowPhoneAttempt(const QString &phone) {
	const auto digits = ranges::count_if(
		phone,
		[](QChar ch) { return ch.isNumber(); });
	return (digits > 1);
}

} // namespace

PhoneWidget::PhoneWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data)
, _country(this, st::introCountry)
, _code(this, st::introCountryCode)
, _phone(
	this,
	st::introPhone,
	[](const QString &s) { return Countries::Groups(s); })
, _checkRequestTimer([=] { checkRequest(); }) {
	_phone->frontBackspaceEvent(
	) | rpl::start_with_next([=](not_null<QKeyEvent*> e) {
		_code->startErasing(e);
	}, _code->lifetime());

	_country->codeChanged(
	) | rpl::start_with_next([=](const QString &code) {
		_code->codeSelected(code);
		_phone->chooseCode(code);
	}, _country->lifetime());
	_code->codeChanged(
	) | rpl::start_with_next([=](const QString &code) {
		_country->onChooseCode(code);
		_phone->chooseCode(code);
	}, _code->lifetime());
	_code->addedToNumber(
	) | rpl::start_with_next([=](const QString &added) {
		_phone->addedToNumber(added);
	}, _phone->lifetime());
	connect(_phone, &Ui::PhonePartInput::changed, [=] { phoneChanged(); });
	connect(_code, &Ui::CountryCodeInput::changed, [=] { phoneChanged(); });

	setTitleText(tr::lng_phone_title());
	setDescriptionText(tr::lng_phone_desc());
	getData()->updated.events(
	) | rpl::start_with_next([=] {
		countryChanged();
	}, lifetime());
	setErrorCentered(true);
	setupQrLogin();

	if (!_country->chooseCountry(getData()->country)) {
		_country->chooseCountry(qsl("US"));
	}
	_changed = false;
}

void PhoneWidget::setupQrLogin() {
	const auto qrLogin = Ui::CreateChild<Ui::LinkButton>(
		this,
		tr::lng_phone_to_qr(tr::now));
	qrLogin->show();

	DEBUG_LOG(("PhoneWidget.qrLogin link created and shown."));

	rpl::combine(
		sizeValue(),
		qrLogin->widthValue()
	) | rpl::start_with_next([=](QSize size, int qrLoginWidth) {
		qrLogin->moveToLeft(
			(size.width() - qrLoginWidth) / 2,
			contentTop() + st::introQrLoginLinkTop);
	}, qrLogin->lifetime());

	qrLogin->setClickedCallback([=] {
		goReplace<QrWidget>(Animate::Forward);
	});
}

void PhoneWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	_country->moveToLeft(contentLeft(), contentTop() + st::introStepFieldTop);
	auto phoneTop = _country->y() + _country->height() + st::introPhoneTop;
	_code->moveToLeft(contentLeft(), phoneTop);
	_phone->moveToLeft(contentLeft() + _country->width() - st::introPhone.width, phoneTop);
}

void PhoneWidget::showPhoneError(rpl::producer<QString> text) {
	_phone->showError();
	showError(std::move(text));
}

void PhoneWidget::hidePhoneError() {
	hideError();
}

void PhoneWidget::countryChanged() {
	if (!_changed) {
		selectCountry(getData()->country);
	}
}

void PhoneWidget::phoneChanged() {
	_changed = true;
	hidePhoneError();
}

void PhoneWidget::submit() {
	if (_sentRequest || isHidden()) {
		return;
	}

	const auto phone = fullNumber();
	if (!AllowPhoneAttempt(phone)) {
		showPhoneError(tr::lng_bad_phone());
		_phone->setFocus();
		return;
	}

	cancelNearestDcRequest();

	// Check if such account is authorized already.
	const auto digitsOnly = [](QString value) {
		return value.replace(QRegularExpression("[^0-9]"), QString());
	};
	const auto phoneDigits = digitsOnly(phone);
	for (const auto &[index, existing] : Core::App().domain().accounts()) {
		const auto raw = existing.get();
		if (const auto session = raw->maybeSession()) {
			if (raw->mtp().environment() == account().mtp().environment()
				&& digitsOnly(session->user()->phone()) == phoneDigits) {
				crl::on_main(raw, [=] {
					Core::App().domain().activate(raw);
				});
				return;
			}
		}
	}

	hidePhoneError();

	_checkRequestTimer.callEach(1000);

	_sentPhone = phone;
	api().instance().setUserPhone(_sentPhone);
	_sentRequest = api().request(MTPauth_SendCode(
		MTP_string(_sentPhone),
		MTP_int(cApiId()),
		MTP_string(cApiHash()),
		MTP_codeSettings(MTP_flags(0), MTP_vector<MTPbytes>())
	)).done([=](const MTPauth_SentCode &result) {
		phoneSubmitDone(result);
	}).fail([=](const MTP::Error &error) {
		phoneSubmitFail(error);
	}).handleFloodErrors().send();
}

void PhoneWidget::stopCheck() {
	_checkRequestTimer.cancel();
}

void PhoneWidget::checkRequest() {
	auto status = api().instance().state(_sentRequest);
	if (status < 0) {
		auto leftms = -status;
		if (leftms >= 1000) {
			api().request(base::take(_sentRequest)).cancel();
		}
	}
	if (!_sentRequest && status == MTP::RequestSent) {
		stopCheck();
	}
}

void PhoneWidget::phoneSubmitDone(const MTPauth_SentCode &result) {
	stopCheck();
	_sentRequest = 0;

	if (result.type() != mtpc_auth_sentCode) {
		showPhoneError(rpl::single(Lang::Hard::ServerError()));
		return;
	}

	const auto &d = result.c_auth_sentCode();
	fillSentCodeData(d);
	getData()->phone = _sentPhone;
	getData()->phoneHash = qba(d.vphone_code_hash());
	const auto next = d.vnext_type();
	if (next && next->type() == mtpc_auth_codeTypeCall) {
		getData()->callStatus = CallStatus::Waiting;
		getData()->callTimeout = d.vtimeout().value_or(60);
	} else {
		getData()->callStatus = CallStatus::Disabled;
		getData()->callTimeout = 0;
	}
	goNext<CodeWidget>();
}

void PhoneWidget::phoneSubmitFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		stopCheck();
		_sentRequest = 0;
		showPhoneError(tr::lng_flood_error());
		return;
	}

	stopCheck();
	_sentRequest = 0;
	auto &err = error.type();
	if (err == qstr("PHONE_NUMBER_FLOOD")) {
		Ui::show(Box<Ui::InformBox>(tr::lng_error_phone_flood(tr::now)));
	} else if (err == qstr("PHONE_NUMBER_INVALID")) { // show error
		showPhoneError(tr::lng_bad_phone());
	} else if (err == qstr("PHONE_NUMBER_BANNED")) {
		Ui::ShowPhoneBannedError(getData()->controller, _sentPhone);
	} else if (Logs::DebugEnabled()) { // internal server error
		showPhoneError(rpl::single(err + ": " + error.description()));
	} else {
		showPhoneError(rpl::single(Lang::Hard::ServerError()));
	}
}

QString PhoneWidget::fullNumber() const {
	return _code->getLastText() + _phone->getLastText();
}

void PhoneWidget::selectCountry(const QString &country) {
	_country->chooseCountry(country);
}

void PhoneWidget::setInnerFocus() {
	_phone->setFocusFast();
}

void PhoneWidget::activate() {
	Step::activate();
	showChildren();
	setInnerFocus();
}

void PhoneWidget::finished() {
	Step::finished();
	_checkRequestTimer.cancel();
	apiClear();

	cancelled();
}

void PhoneWidget::cancelled() {
	api().request(base::take(_sentRequest)).cancel();
}

} // namespace details
} // namespace Intro
