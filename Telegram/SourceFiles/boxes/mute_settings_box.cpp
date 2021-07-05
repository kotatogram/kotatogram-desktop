/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

This code is in Public Domain, see license terms in .github/CONTRIBUTING.md
Copyright (C) 2017, Nicholas Guriev <guriev-ns@ya.ru>
*/
#include "boxes/mute_settings_box.h"

#include "lang/lang_keys.h"
#include "base/event_filter.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "ui/special_buttons.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "app.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace {

constexpr auto kForeverHours = 24 * 365;
constexpr auto kCustomFor = kForeverHours - 1;

} // namespace

MuteSettingsBox::MuteSettingsBox(QWidget *parent, not_null<PeerData*> peer)
: _peer(peer)
, _forNumberInput(this, st::scheduleDateField)
, _forPeriodInput(this, st::scheduleDateField) {
}

void MuteSettingsBox::prepare() {
	setTitle(tr::lng_disable_notifications_from_tray());
	auto y = 0;

	object_ptr<Ui::FlatLabel> info(this, st::boxLabel);
	info->setText(tr::lng_mute_box_tip(tr::now));
	info->moveToLeft(st::boxPadding.left(), y);
	y += info->height() + st::boxLittleSkip;

	const auto icon = object_ptr<Ui::UserpicButton>(
		this,
		_peer,
		Ui::UserpicButton::Role::Custom,
		st::mutePhotoButton);
	icon->setPointerCursor(false);
	icon->moveToLeft(st::boxPadding.left(), y);

	object_ptr<Ui::FlatLabel> title(this, st::muteChatTitle);
	title->setText(_peer->name);
	title->moveToLeft(
		st::boxPadding.left() + st::muteChatTitleLeft,
		y + (icon->height() / 2) - (title->height() / 2));
	// the icon is always higher than this chat title
	y += icon->height() + st::boxMediumSkip;

	// in fact, this is mute only for 1 year
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(kForeverHours);
	y += st::boxOptionListPadding.top();

	const auto trimLangStr = [] (const QString &numberStr, const QString &langStr) {
		return langStr.mid(numberStr.length()).trimmed();
	};

	const auto makePeriodText = [=, this] (Period period) {
		const auto currentValue = _forNumberInput->getLastText().toInt();
		const auto currentStrValue = QString::number(currentValue); // re-converting to get rid of the invalid symbols
		switch (period) {
			case Period::Second:
				return trimLangStr(currentStrValue, tr::lng_group_call_duration_seconds(tr::now, lt_count, currentValue));

			case Period::Minute:
				return trimLangStr(currentStrValue, tr::lng_group_call_duration_minutes(tr::now, lt_count, currentValue));

			case Period::Hour:
				return trimLangStr(currentStrValue, tr::lng_group_call_duration_hours(tr::now, lt_count, currentValue));

			case Period::Day:
				return trimLangStr(currentStrValue, tr::lng_group_call_duration_days(tr::now, lt_count, currentValue));

			default:
				return QString();
		}
	};

	// Prefill input values, default is 1 hour
	_forNumberInput->setText("1");
	_forPeriodInput->setText(makePeriodText(_period));

	for (const auto hours : { kCustomFor, kForeverHours }) {
		const auto text = [&] {
			if (hours == kCustomFor) {
				return tr::ktg_mute_for_selected_time(tr::now);
			} else {
				return tr::lng_mute_duration_forever(tr::now);
			}
		}();
		object_ptr<Ui::Radiobutton> option(this, group, hours, text);
		option->moveToLeft(st::boxPadding.left(), y);
		y += option->heightNoMargins() + st::boxOptionListSkip;

		if (hours == kCustomFor) {
			const auto fieldLeft = st::boxPadding.left()
				+ st::autolockButton.margin.left()
				+ st::autolockButton.margin.right()
				+ st::defaultToggle.width
				+ st::defaultToggle.border * 2;

			_forNumberInput->resizeToWidth(st::scheduleTimeWidth);
			_forNumberInput->moveToLeft(fieldLeft, y);

			_forPeriodInput->resizeToWidth(st::scheduleDateWidth);
			_forPeriodInput->moveToLeft(fieldLeft + st::scheduleTimeWidth + st::scheduleAtSkip, y);

			y += _forNumberInput->heightNoMargins() + st::boxOptionListSkip;
		}
	}
	group->setChangedCallback([this] (int hours) {
		if (hours == kCustomFor) {
			_forNumberInput->setFocus();
		} else {
			_forNumberInput->clearFocus();
		}
	});
	y += st::boxOptionListPadding.bottom()
		- st::boxOptionListSkip
		+ st::defaultCheckbox.margin.bottom();

	_forNumberInput->customTab(true);

	_forNumberInput->documentContentsChanges(
	) | rpl::start_with_next([=](const auto &value) {
		_forNumberInput->hideError();
		_forPeriodInput->setText(makePeriodText(_period));
	}, _lifetime);

	QObject::connect(_forNumberInput, &Ui::InputField::focused, [=] {
		if (group->value() != kCustomFor) {
			group->setValue(kCustomFor);
		}
	});

	_forPeriodInput->rawTextEdit()->setTextInteractionFlags(Qt::NoTextInteraction);

	const auto &forPeriodViewport = _forPeriodInput->rawTextEdit()->viewport();
	base::install_event_filter(forPeriodViewport, [=](not_null<QEvent*> event) {
		switch (event->type()) {
			case QEvent::Leave:
				if (_menu) {
					_menu->hideAnimated();
				}
				return base::EventFilterResult::Cancel;

			case QEvent::ContextMenu:
			case QEvent::MouseButtonDblClick:
				return base::EventFilterResult::Cancel;			

			case QEvent::MouseButtonPress:
				if (group->value() != kCustomFor) {
					group->setValue(kCustomFor);
				}

				_forNumberInput->setFocus();

				if (_menu) {
					_menu->hideAnimated(Ui::InnerDropdown::HideOption::IgnoreShow);
					return base::EventFilterResult::Cancel;
				}

				_menu = base::make_unique_q<Ui::DropdownMenu>(window());
				const auto weak = _menu.get();
				_menu->setHiddenCallback([=] {
					weak->deleteLater();
				});

				for (const auto period : { Period::Second, Period::Minute, Period::Hour, Period::Day }) {
					const auto periodStr = makePeriodText(period);
					_menu->addAction(periodStr, [=] {
						_period = period;
						_forPeriodInput->setText(periodStr);
					});
				}

				const auto parentTopLeft = window()->mapToGlobal({ 0, 0 });
				const auto inputTopLeft = _forPeriodInput->mapToGlobal({ 0, 0 });
				const auto parentRect = QRect(parentTopLeft, window()->size());
				const auto inputRect = QRect(inputTopLeft, _forPeriodInput->size());
				_menu->move(
					inputRect.x() + inputRect.width() + st::boxPadding.left() - _menu->width() - parentRect.x(),
					inputRect.y() + inputRect.height() - parentRect.y());
				_menu->showAnimated(Ui::PanelAnimation::Origin::TopRight);
				return base::EventFilterResult::Cancel;
		}

		return base::EventFilterResult::Continue;
	});

	_save = [=] {
		const auto muteForSeconds = (group->value() == kCustomFor)
			? _forNumberInput->getLastText().toInt() * int(_period)
			: group->value() * 3600;
		if (muteForSeconds <= 0 || muteForSeconds > kForeverHours) {
			_forNumberInput->showError();
		} else {
			_peer->owner().updateNotifySettings(
				_peer,
				muteForSeconds);
			closeBox();
		}
	};
	addButton(tr::lng_box_ok(), _save);
	addButton(tr::lng_cancel(), [this] { closeBox(); });

	setDimensions(st::boxWidth, y);
}

void MuteSettingsBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_save) {
			_save();
		}
	}
}

// vi: ts=4 tw=80
