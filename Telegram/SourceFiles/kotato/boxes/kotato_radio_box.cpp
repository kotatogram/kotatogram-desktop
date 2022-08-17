/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/boxes/kotato_radio_box.h"

#include "kotato/kotato_settings.h"
#include "lang/lang_keys.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/wrap.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/boxes/confirm_box.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "core/application.h"

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
, _warnRestart(warnRestart)
, _owned(this)
, _content(_owned.data()) {
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
, _warnRestart(warnRestart)
, _owned(this)
, _content(_owned.data()) {
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
, _warnRestart(warnRestart)
, _owned(this)
, _content(_owned.data()) {
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
, _warnRestart(warnRestart)
, _owned(this)
, _content(_owned.data()) {
}

void RadioBox::prepare() {
	setTitle(rpl::single(_title));

	addButton(tr::lng_settings_save(), [=] { save(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	if (!_description.isEmpty()) {
		_content->add(
			object_ptr<Ui::FlatLabel>(_content, _description, st::boxDividerLabel),
			style::margins(
				st::boxPadding.left(),
				0,
				st::boxPadding.right(),
				st::boxPadding.bottom()));
	}

	_group = std::make_shared<Ui::RadiobuttonGroup>(_startValue);

	for (auto i = 0; i != _valueCount; ++i) {
		const auto description = _descriptionGetter
			? _descriptionGetter(i)
			: QString();

		_content->add(
			object_ptr<Ui::Radiobutton>(
				_content,
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
			_content->add(
				object_ptr<Ui::FlatLabel>(_content, description, st::boxDividerLabel),
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

	auto wrap = object_ptr<Ui::OverrideMargins>(this, std::move(_owned));
	setDimensionsToContent(st::boxWidth, wrap.data());
	setInnerWidget(std::move(wrap));
}

void RadioBox::save() {
	_saveCallback(_group->current());
	if (_warnRestart) {
		const auto box = std::make_shared<QPointer<BoxContent>>();

		*box = getDelegate()->show(
			Ui::MakeConfirmBox({
				.text = tr::lng_settings_need_restart(),
				.confirmed = [] { Core::Restart(); },
				.cancelled = crl::guard(this, [=] { closeBox(); box->data()->closeBox(); }),
				.confirmText = tr::lng_settings_restart_now(),
				.cancelText = tr::lng_settings_restart_later(),
			}));
	} else {
		closeBox();
	}
}

} // namespace Kotato