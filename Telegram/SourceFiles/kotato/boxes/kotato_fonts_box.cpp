/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/boxes/kotato_fonts_box.h"

#include "kotato/kotato_lang.h"
#include "kotato/kotato_settings.h"
#include "base/platform/base_platform_info.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/wrap.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/continuous_sliders.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_calls.h"
#include "styles/style_settings.h"
#include "ui/boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "core/application.h"

#include <QFontDatabase>
#include <QListView>
#include <QStringListModel>
#include <QVBoxLayout>

class FontListView : public QListView {
public:
	FontListView(QWidget *parent)
	: QListView(parent) {
		setModel(new QStringListModel(parent));
		setEditTriggers(NoEditTriggers);
		setFont(st::normalFont);
	}

	inline QStringListModel *model() const {
		return static_cast<QStringListModel *>(QListView::model());
	}
	inline void setCurrentItem(int item) {
		QListView::setCurrentIndex(static_cast<QAbstractListModel*>(model())->index(item));
	}
	inline int currentItem() const {
		return QListView::currentIndex().row();
	}
	inline int count() const {
		return model()->rowCount();
	}
	inline QString currentText() const {
		int row = QListView::currentIndex().row();
		return row < 0 ? QString() : model()->stringList().at(row);
	}
	void currentChanged(const QModelIndex &current, const QModelIndex &previous) override {
		QListView::currentChanged(current, previous);
		if (current.isValid())
			_highlighted.fire_copy(model()->stringList().at(current.row()));
	}
	QString text(int i) const {
		return model()->stringList().at(i);
	}
	rpl::producer<QString> highlighted() {
		return _highlighted.events();
	}
	rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	rpl::event_stream<QString> _highlighted;
	rpl::lifetime _lifetime;
};

class RpFontListView : public Ui::RpWidget {
public:
	RpFontListView(QWidget *parent)
	: Ui::RpWidget(parent)
	, _layout(this)
	, _view(this) {
		_layout->addWidget(_view);
	}

	void prepare(
			Ui::InputField *field,
			const QStringList &fontList) {
		_view->model()->setStringList(fontList);
		resize(0, _view->sizeHintForRow(0) * 10);
		_view->highlighted(
		) | rpl::start_with_next([=](QString fontName) {
			if (!field->hasFocus()) {
				field->setText(fontName);
			}
		}, _view->lifetime());
		field->changes(
		) | rpl::start_with_next([=] {
			if (field->getLastText().isEmpty()) {
				_view->setCurrentItem(-1);
				return;
			}
			_view->setCurrentItem(
				std::distance(fontList.begin(), ranges::find_if(
					fontList,
					[&](const auto &fontName) {
						return fontName.startsWith(field->getLastText());
					})));
		}, field->lifetime());
		const auto defaultValue = field->getLastText().trimmed();
		if (!defaultValue.isEmpty()) {
			_view->setCurrentItem(fontList.indexOf(defaultValue));
		}
	}

private:
	object_ptr<QVBoxLayout> _layout;
	object_ptr<FontListView> _view;
};

FontsBox::FontsBox(QWidget* parent)
: _owned(this)
, _content(_owned.data())
, _fontSize(::Kotato::JsonSettings::GetIntWithPending("fonts/size"))
{
}

void FontsBox::prepare() {
	setTitle(rktr("ktg_fonts_title"));

	addButton(tr::lng_settings_save(), [=] { save(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	addLeftButton(rktr("ktg_fonts_reset"), [=] { resetToDefault(); });

	_useSystemFont = _content->add(
		object_ptr<Ui::Checkbox>(_content,
			ktr("ktg_fonts_use_system_font"),
			::Kotato::JsonSettings::GetBoolWithPending("fonts/use_system_font")),
		QMargins(
			st::boxPadding.left(),
			0,
			st::boxPadding.right(),
			st::boxPadding.bottom()));
	_useOriginalMetrics = _content->add(
		object_ptr<Ui::Checkbox>(_content,
			ktr("ktg_fonts_use_original_metrics"),
			::Kotato::JsonSettings::GetBoolWithPending("fonts/use_original_metrics")),
		QMargins(
			st::boxPadding.left(),
			st::boxPadding.bottom(),
			st::boxPadding.right(),
			st::boxPadding.bottom()));
	_mainFontName = _content->add(
		object_ptr<Ui::InputField>(_content, st::defaultInputField, rktr("ktg_fonts_main")),
		QMargins(
			st::boxPadding.left(),
			0,
			st::boxPadding.right(),
			st::boxPadding.bottom()));
	_mainFontList = _content->add(
		object_ptr<RpFontListView>(_content),
		QMargins(
			st::boxPadding.left(),
			st::boxPadding.bottom(),
			st::boxPadding.right(),
			st::boxPadding.bottom()));
	_semiboldFontName = _content->add(
		object_ptr<Ui::InputField>(_content, st::defaultInputField, rktr("ktg_fonts_semibold")),
		QMargins(
			st::boxPadding.left(),
			0,
			st::boxPadding.right(),
			st::boxPadding.bottom()));
	_semiboldFontList = _content->add(
		object_ptr<RpFontListView>(_content),
		QMargins(
			st::boxPadding.left(),
			st::boxPadding.bottom(),
			st::boxPadding.right(),
			st::boxPadding.bottom()));
	_semiboldIsBold = _content->add(
		object_ptr<Ui::Checkbox>(_content,
			ktr("ktg_fonts_semibold_is_bold"),
			::Kotato::JsonSettings::GetBoolWithPending("fonts/semibold_is_bold")),
		QMargins(
			st::boxPadding.left(),
			0,
			st::boxPadding.right(),
			st::boxPadding.bottom()));
	_monospacedFontName = _content->add(
		object_ptr<Ui::InputField>(_content, st::defaultInputField, rktr("ktg_fonts_monospaced")),
		QMargins(
			st::boxPadding.left(),
			0,
			st::boxPadding.right(),
			st::boxPadding.bottom()));
	_monospacedFontList = _content->add(
		object_ptr<RpFontListView>(_content),
		QMargins(
			st::boxPadding.left(),
			st::boxPadding.bottom(),
			st::boxPadding.right(),
			st::boxPadding.bottom()));
	_fontSizeLabel = _content->add(
		object_ptr<Ui::LabelSimple>(
			_content,
			st::ktgSettingsSliderLabel),
		st::groupCallDelayLabelMargin);
	_fontSizeSlider = _content->add(
		object_ptr<Ui::MediaSlider>(
			_content,
			st::defaultContinuousSlider),
		st::localStorageLimitMargin);
	const auto updateFontSizeLabel = [=](int value) {
		const auto prefix = (value >= 0) ? qsl("+") : QString();
		const auto pixels = prefix + QString::number(value);
		_fontSizeLabel->setText(
			ktr("ktg_fonts_size", { "pixels", pixels }));
	};
	const auto updateFontSize = [=](int value) {
		updateFontSizeLabel(value);
		_fontSize = value;
	};
	_fontSizeSlider->resize(st::defaultContinuousSlider.seekSize);
	_fontSizeSlider->setPseudoDiscrete(
		21,
		[](int val) { return val - 10; },
		_fontSize,
		updateFontSize);
	updateFontSizeLabel(_fontSize);
	_content->add(
		object_ptr<Ui::FlatLabel>(_content, rktr("ktg_fonts_about"), st::boxDividerLabel),
		QMargins(
			st::boxPadding.left(),
			0,
			st::boxPadding.right(),
			st::boxPadding.bottom()));

	_mainFontName->setText(::Kotato::JsonSettings::GetStringWithPending("fonts/main"));
	_semiboldFontName->setText(::Kotato::JsonSettings::GetStringWithPending("fonts/semibold"));
	_monospacedFontName->setText(::Kotato::JsonSettings::GetStringWithPending("fonts/monospaced"));

	const auto fontNames = QFontDatabase().families();
	_mainFontList->prepare(_mainFontName, fontNames);
	_semiboldFontList->prepare(_semiboldFontName, fontNames);
	_monospacedFontList->prepare(_monospacedFontName, fontNames);

	auto wrap = object_ptr<Ui::OverrideMargins>(this, std::move(_owned));
	setDimensionsToContent(st::boxWidth, wrap.data());
	setInnerWidget(std::move(wrap));
}


void FontsBox::setInnerFocus() {
	_mainFontName->setFocusFast();
}

void FontsBox::save() {
	::Kotato::JsonSettings::SetAfterRestart("fonts/main", _mainFontName->getLastText().trimmed());
	::Kotato::JsonSettings::SetAfterRestart("fonts/semibold", _semiboldFontName->getLastText().trimmed());
	::Kotato::JsonSettings::SetAfterRestart("fonts/monospaced", _monospacedFontName->getLastText().trimmed());
	::Kotato::JsonSettings::SetAfterRestart("fonts/semibold_is_bold", _semiboldIsBold->checked());
	::Kotato::JsonSettings::SetAfterRestart("fonts/use_system_font", _useSystemFont->checked());
	::Kotato::JsonSettings::SetAfterRestart("fonts/use_original_metrics", _useOriginalMetrics->checked());
	::Kotato::JsonSettings::SetAfterRestart("fonts/size", _fontSize);
	::Kotato::JsonSettings::Write();

	const auto box = std::make_shared<QPointer<BoxContent>>();

	*box = getDelegate()->show(
		Ui::MakeConfirmBox({
			.text = tr::lng_settings_need_restart(),
			.confirmed = [] { Core::Restart(); },
			.cancelled = crl::guard(this, [=] { closeBox(); box->data()->closeBox(); }),
			.confirmText = tr::lng_settings_restart_now(),
			.cancelText = tr::lng_settings_restart_later(),
		}));
}

void FontsBox::resetToDefault() {
	::Kotato::JsonSettings::ResetAfterRestart("fonts/main");
	::Kotato::JsonSettings::ResetAfterRestart("fonts/semibold");
	::Kotato::JsonSettings::ResetAfterRestart("fonts/monospaced");
	::Kotato::JsonSettings::ResetAfterRestart("fonts/semibold_is_bold");
	::Kotato::JsonSettings::ResetAfterRestart("fonts/size");
	::Kotato::JsonSettings::ResetAfterRestart("fonts/use_system_font");
	::Kotato::JsonSettings::ResetAfterRestart("fonts/use_original_metrics");
	::Kotato::JsonSettings::Write();

	const auto box = std::make_shared<QPointer<BoxContent>>();

	*box = getDelegate()->show(
		Ui::MakeConfirmBox({
			.text = tr::lng_settings_need_restart(),
			.confirmed = [] { Core::Restart(); },
			.cancelled = crl::guard(this, [=] { closeBox(); box->data()->closeBox(); }),
			.confirmText = tr::lng_settings_restart_now(),
			.cancelText = tr::lng_settings_restart_later(),
		}));
}
