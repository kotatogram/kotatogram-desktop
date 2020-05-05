/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/boxes/radio_box.h"

#include "lang/lang_keys.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "boxes/confirm_box.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "kotato/json_settings.h"
#include "app.h"

namespace Kotato {

RadioBox::RadioBox(
	QWidget*,
	const QString &title,
	int currentValue,
	const QMap<int, QString> &options,
	Fn<void(int)> saveCallback,
	bool warnRestart)
: _title(title)
, _startValue(currentValue)
, _options(options)
, _saveCallback(std::move(saveCallback))
, _warnRestart(warnRestart) {
}

RadioBox::RadioBox(
	QWidget*,
	const QString &title,
	const QString &description,
	int currentValue,
	const QMap<int, QString> &options,
	Fn<void(int)> saveCallback,
	bool warnRestart)
: _title(title)
, _description(description)
, _startValue(currentValue)
, _options(options)
, _saveCallback(std::move(saveCallback))
, _warnRestart(warnRestart) {
}

void RadioBox::prepare() {
	setTitle(rpl::single(_title));

	addButton(tr::lng_settings_save(), [=] { save(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	if (!_description.isEmpty()) {
		content->add(
			object_ptr<Ui::FlatLabel>(this, _description, st::boxLabel),
			style::margins(
				st::boxPadding.left(),
				st::boxPadding.bottom(),
				st::boxPadding.right(),
				st::boxPadding.bottom()));
	}

	_group = std::make_shared<Ui::RadiobuttonGroup>(_startValue);

	for (auto i = _options.constBegin(); i != _options.constEnd(); ++i) {
		content->add(
			object_ptr<Ui::Radiobutton>(
				this,
				_group,
				i.key(),
				i.value(),
				st::autolockButton),
			style::margins(
				st::boxPadding.left(),
				st::boxPadding.bottom(),
				st::boxPadding.right(),
				st::boxPadding.bottom()));
	}

	setDimensionsToContent(st::boxWidth, content);
}

void RadioBox::save() {
	if (_warnRestart) {
		const auto saveAfterWarn = [=] {
			_saveCallback(_group->value());
			App::restart();
		};

		const auto box = std::make_shared<QPointer<BoxContent>>();

		*box = getDelegate()->show(
			Box<ConfirmBox>(
				tr::lng_settings_need_restart(tr::now),
				tr::lng_settings_restart_now(tr::now),
				tr::lng_cancel(tr::now),
				saveAfterWarn));
	} else {
		_saveCallback(_group->value());
		closeBox();
	}
}

} // namespace Kotato