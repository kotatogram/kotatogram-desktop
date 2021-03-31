/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/customboxes/radio_box.h"

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
	int valueCount,
	Fn<QString(int)> labelGetter,
	Fn<void(int)> saveCallback,
	bool warnRestart)
: _title(title)
, _startValue(currentValue)
, _valueCount(valueCount)
, _labelGetter(labelGetter)
, _saveCallback(std::move(saveCallback))
, _warnRestart(warnRestart) {
}

RadioBox::RadioBox(
	QWidget*,
	const QString &title,
	const QString &description,
	int currentValue,
	int valueCount,
	Fn<QString(int)> labelGetter,
	Fn<void(int)> saveCallback,
	bool warnRestart)
: _title(title)
, _description(description)
, _startValue(currentValue)
, _valueCount(valueCount)
, _labelGetter(labelGetter)
, _saveCallback(std::move(saveCallback))
, _warnRestart(warnRestart) {
}

RadioBox::RadioBox(
	QWidget*,
	const QString &title,
	int currentValue,
	int valueCount,
	Fn<QString(int)> labelGetter,
	Fn<QString(int)> descriptionGetter,
	Fn<void(int)> saveCallback,
	bool warnRestart)
: _title(title)
, _startValue(currentValue)
, _valueCount(valueCount)
, _labelGetter(labelGetter)
, _descriptionGetter(descriptionGetter)
, _saveCallback(std::move(saveCallback))
, _warnRestart(warnRestart) {
}

RadioBox::RadioBox(
	QWidget*,
	const QString &title,
	const QString &description,
	int currentValue,
	int valueCount,
	Fn<QString(int)> labelGetter,
	Fn<QString(int)> descriptionGetter,
	Fn<void(int)> saveCallback,
	bool warnRestart)
: _title(title)
, _description(description)
, _startValue(currentValue)
, _valueCount(valueCount)
, _labelGetter(labelGetter)
, _descriptionGetter(descriptionGetter)
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

	for (auto i = 0; i != _valueCount; ++i) {
		const auto description = _descriptionGetter
			? _descriptionGetter(i)
			: QString();

		content->add(
			object_ptr<Ui::Radiobutton>(
				this,
				_group,
				i,
				_labelGetter(i),
				st::autolockButton),
			style::margins(
				st::boxPadding.left(),
				st::boxPadding.bottom(),
				st::boxPadding.right(),
				description.isEmpty() ? st::boxPadding.bottom() : 0));
		if (!description.isEmpty()) {
			content->add(
				object_ptr<Ui::FlatLabel>(this, description, st::boxDividerLabel),
				style::margins(
					st::boxPadding.left()
						+ st::autolockButton.margin.left()
						+ st::autolockButton.margin.right()
						+ st::defaultToggle.width
						+ st::defaultToggle.border * 2,
					0,
					st::boxPadding.right(),
					st::boxPadding.bottom()));
		}
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