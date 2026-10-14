#pragma once

#include <QString>
#include <QStringList>

#include <string>
#include <vector>

///
/// The UI is Qt and the core is plain C++: names, messages and paths cross between
/// them here. Core strings are UTF-8 std::string.
///

inline QString qs(const std::string &s) { return QString::fromStdString(s); }

inline std::string ss(const QString &s) { return s.toStdString(); }

inline QStringList qsl(const std::vector<std::string> &list)
{
    QStringList out;
    out.reserve(static_cast<qsizetype>(list.size()));
    for (const std::string &s : list) out << QString::fromStdString(s);
    return out;
}

inline std::vector<std::string> ssl(const QStringList &list)
{
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(list.size()));
    for (const QString &s : list) out.push_back(s.toStdString());
    return out;
}
