/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/boxes/kotato_fonts_box.h"

#include "kotato/kotato_lang.h"
#include "base/platform/base_platform_info.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/wrap.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "ui/boxes/confirm_box.h"
#include "kotato/json_settings.h"
#include "lang/lang_keys.h"
#include "app.h"

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
			const QStringList &fontList,
			const QString &defaultValue) {
		_view->model()->setStringList(fontList);
		resize(0, _view->sizeHintForRow(0) * 10);
		_view->highlighted(
		) | rpl::start_with_next([=](QString fontName) {
			if (!field->hasFocus()) {
				field->setText(fontName);
			}
		}, _view->lifetime());
		QObject::connect(field, &Ui::InputField::changed, [=] {
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
		});
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
{
}

void FontsBox::prepare() {
	setTitle(rktr("ktg_fonts_title"));

	addButton(tr::lng_settings_save(), [=] { save(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	addLeftButton(rktr("ktg_fonts_reset"), [=] { resetToDefault(); });

	_useSystemFont = _content->add(
		object_ptr<Ui::Checkbox>(_content, ktr("ktg_fonts_use_system_font"), cUseSystemFont()),
		QMargins(
			st::boxPadding.left(),
			0,
			st::boxPadding.right(),
			st::boxPadding.bottom()));
	_useOriginalMetrics = _content->add(
		object_ptr<Ui::Checkbox>(_content, ktr("ktg_fonts_use_original_metrics"), cUseOriginalMetrics()),
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
		object_ptr<Ui::Checkbox>(_content, ktr("ktg_fonts_semibold_is_bold"), cSemiboldFontIsBold()),
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
	_content->add(
		object_ptr<Ui::FlatLabel>(_content, rktr("ktg_fonts_about"), st::boxDividerLabel),
		QMargins(
			st::boxPadding.left(),
			0,
			st::boxPadding.right(),
			st::boxPadding.bottom()));

	if (!cMainFont().isEmpty()) {
		_mainFontName->setText(cMainFont());
	}

	if (!cSemiboldFont().isEmpty()) {
		_semiboldFontName->setText(cSemiboldFont());
	}

	if (!cMonospaceFont().isEmpty()) {
		_monospacedFontName->setText(cMonospaceFont());
	}

	const auto fontNames = QFontDatabase().families();
	_mainFontList->prepare(_mainFontName, fontNames, cMainFont());
	_semiboldFontList->prepare(_semiboldFontName, fontNames, cSemiboldFont());
	_monospacedFontList->prepare(_monospacedFontName, fontNames, cMonospaceFont());

	auto wrap = object_ptr<Ui::OverrideMargins>(this, std::move(_owned));
	setDimensionsToContent(st::boxWidth, wrap.data());
	setInnerWidget(std::move(wrap));
}


void FontsBox::setInnerFocus() {
	_mainFontName->setFocusFast();
}

void FontsBox::save() {
	const auto useSystemFont = _useSystemFont->checked();
	const auto useOriginalMetrics = _useOriginalMetrics->checked();
	const auto mainFont = _mainFontName->getLastText().trimmed();
	const auto semiboldFont = _semiboldFontName->getLastText().trimmed();
	const auto semiboldIsBold = _semiboldIsBold->checked();
	const auto monospacedFont = _monospacedFontName->getLastText().trimmed();

	const auto changeFonts = [=] {
		cSetUseSystemFont(useSystemFont);
		cSetUseOriginalMetrics(useOriginalMetrics);
		cSetMainFont(mainFont);
		cSetSemiboldFont(semiboldFont);
		cSetSemiboldFontIsBold(semiboldIsBold);
		cSetMonospaceFont(monospacedFont);
		Kotato::JsonSettings::Write();
		App::restart();
	};

	const auto box = std::make_shared<QPointer<BoxContent>>();

	*box = getDelegate()->show(
		Box<Ui::ConfirmBox>(
			tr::lng_settings_need_restart(tr::now),
			tr::lng_settings_restart_now(tr::now),
			tr::lng_cancel(tr::now),
			changeFonts));
}

void FontsBox::resetToDefault() {
	const auto resetFonts = [=] {
		cSetMainFont(QString());
		cSetSemiboldFont(QString());
		cSetSemiboldFontIsBold(false);
		cSetMonospaceFont(QString());
#ifdef DESKTOP_APP_USE_PACKAGED_FONTS
		cSetUseSystemFont(true);
#else
		cSetUseSystemFont(Platform::IsLinux());
#endif
		cSetUseOriginalMetrics(false);
		Kotato::JsonSettings::Write();
		App::restart();
	};

	const auto box = std::make_shared<QPointer<BoxContent>>();

	*box = getDelegate()->show(
		Box<Ui::ConfirmBox>(
			tr::lng_settings_need_restart(tr::now),
			tr::lng_settings_restart_now(tr::now),
			tr::lng_cancel(tr::now),
			resetFonts));
}
