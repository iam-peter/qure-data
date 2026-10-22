#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QtQml/qqmlregistration.h>

#include <deque>

namespace qure::ui {

/// The Pd window's console: print output, errors, compile warnings.
class ConsoleModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("use Qure.console")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Level { Error, Warning, Info, Debug };
    Q_ENUM(Level)
    enum Role { TextRole = Qt::UserRole + 1, LevelRole, TimeRole };

    using QAbstractListModel::QAbstractListModel;

    void append(Level level, const QString& text);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QString allText() const;

    [[nodiscard]] int count() const { return static_cast<int>(lines_.size()); }
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    struct Line {
        Level level;
        QString text;
        QTime time;
    };
    static constexpr std::size_t kMaxLines = 5000;
    std::deque<Line> lines_;
};

} // namespace qure::ui
