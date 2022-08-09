/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#pragma once

namespace Kotato {
namespace Lang {

struct Var {
	Var() {};
	Var(const QString &k, const QString &v) {
		key = k;
		value = v;
	}

	QString key;
	QString value;
};

struct EntVar {
	EntVar() {};
	EntVar(const QString &k, TextWithEntities v) {
		key = k;
		value = v;
	}

	QString key;
	TextWithEntities value;
};

void Load(const QString &baseLangCode, const QString &langCode);

QString Translate(
	const QString &key,
	Var var1 = Var(),
	Var var2 = Var(),
	Var var3 = Var(),
	Var var4 = Var());
QString Translate(
	const QString &key,
	float64 value,
	Var var1 = Var(),
	Var var2 = Var(),
	Var var3 = Var(),
	Var var4 = Var());

TextWithEntities TranslateWithEntities(
	const QString &key,
	EntVar var1 = EntVar(),
	EntVar var2 = EntVar(),
	EntVar var3 = EntVar(),
	EntVar var4 = EntVar());
TextWithEntities TranslateWithEntities(
	const QString &key,
	float64 value,
	EntVar var1 = EntVar(),
	EntVar var2 = EntVar(),
	EntVar var3 = EntVar(),
	EntVar var4 = EntVar());

rpl::producer<> Events();

} // namespace Lang
} // namespace Kotato

// Shorthands

inline QString ktr(
	const QString &key,
	::Kotato::Lang::Var var1 = ::Kotato::Lang::Var(),
	::Kotato::Lang::Var var2 = ::Kotato::Lang::Var(),
	::Kotato::Lang::Var var3 = ::Kotato::Lang::Var(),
	::Kotato::Lang::Var var4 = ::Kotato::Lang::Var()) {
	return ::Kotato::Lang::Translate(key, var1, var2, var3, var4);
}

inline QString ktr(
	const QString &key,
	float64 value,
	::Kotato::Lang::Var var1 = ::Kotato::Lang::Var(),
	::Kotato::Lang::Var var2 = ::Kotato::Lang::Var(),
	::Kotato::Lang::Var var3 = ::Kotato::Lang::Var(),
	::Kotato::Lang::Var var4 = ::Kotato::Lang::Var()) {
	return ::Kotato::Lang::Translate(key, value, var1, var2, var3, var4);
}

inline TextWithEntities ktre(
	const QString &key,
	::Kotato::Lang::EntVar var1 = ::Kotato::Lang::EntVar(),
	::Kotato::Lang::EntVar var2 = ::Kotato::Lang::EntVar(),
	::Kotato::Lang::EntVar var3 = ::Kotato::Lang::EntVar(),
	::Kotato::Lang::EntVar var4 = ::Kotato::Lang::EntVar()) {
	return ::Kotato::Lang::TranslateWithEntities(key, var1, var2, var3, var4);
}

inline TextWithEntities ktre(
	const QString &key,
	float64 value,
	::Kotato::Lang::EntVar var1 = ::Kotato::Lang::EntVar(),
	::Kotato::Lang::EntVar var2 = ::Kotato::Lang::EntVar(),
	::Kotato::Lang::EntVar var3 = ::Kotato::Lang::EntVar(),
	::Kotato::Lang::EntVar var4 = ::Kotato::Lang::EntVar()) {
	return ::Kotato::Lang::TranslateWithEntities(key, value, var1, var2, var3, var4);
}

inline rpl::producer<QString> rktr(
	const QString &key,
	::Kotato::Lang::Var var1 = ::Kotato::Lang::Var(),
	::Kotato::Lang::Var var2 = ::Kotato::Lang::Var(),
	::Kotato::Lang::Var var3 = ::Kotato::Lang::Var(),
	::Kotato::Lang::Var var4 = ::Kotato::Lang::Var()) {
	return rpl::single(
			::Kotato::Lang::Translate(key, var1, var2, var3, var4)
		) | rpl::then(
			::Kotato::Lang::Events() | rpl::map(
				[=]{ return ::Kotato::Lang::Translate(key, var1, var2, var3, var4); })
		);
}

inline rpl::producer<QString> rktr(
	const QString &key,
	float64 value,
	::Kotato::Lang::Var var1 = ::Kotato::Lang::Var(),
	::Kotato::Lang::Var var2 = ::Kotato::Lang::Var(),
	::Kotato::Lang::Var var3 = ::Kotato::Lang::Var(),
	::Kotato::Lang::Var var4 = ::Kotato::Lang::Var()) {
	return rpl::single(
			::Kotato::Lang::Translate(key, value, var1, var2, var3, var4)
		) | rpl::then(
			::Kotato::Lang::Events() | rpl::map(
				[=]{ return ::Kotato::Lang::Translate(key, value, var1, var2, var3, var4); })
		);
}

inline rpl::producer<TextWithEntities> rktre(
	const QString &key,
	::Kotato::Lang::EntVar var1 = ::Kotato::Lang::EntVar(),
	::Kotato::Lang::EntVar var2 = ::Kotato::Lang::EntVar(),
	::Kotato::Lang::EntVar var3 = ::Kotato::Lang::EntVar(),
	::Kotato::Lang::EntVar var4 = ::Kotato::Lang::EntVar()) {
	return rpl::single(
			::Kotato::Lang::TranslateWithEntities(key, var1, var2, var3, var4)
		) | rpl::then(
			::Kotato::Lang::Events() | rpl::map(
				[=]{ return ::Kotato::Lang::TranslateWithEntities(key, var1, var2, var3, var4); })
		);
}

inline rpl::producer<TextWithEntities> rktre(
	const QString &key,
	float64 value,
	::Kotato::Lang::EntVar var1 = ::Kotato::Lang::EntVar(),
	::Kotato::Lang::EntVar var2 = ::Kotato::Lang::EntVar(),
	::Kotato::Lang::EntVar var3 = ::Kotato::Lang::EntVar(),
	::Kotato::Lang::EntVar var4 = ::Kotato::Lang::EntVar()) {
	return rpl::single(
			::Kotato::Lang::TranslateWithEntities(key, value, var1, var2, var3, var4)
		) | rpl::then(
			::Kotato::Lang::Events() | rpl::map(
				[=]{ return ::Kotato::Lang::TranslateWithEntities(key, value, var1, var2, var3, var4); })
		);
}
