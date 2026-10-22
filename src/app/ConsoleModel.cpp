#include "ConsoleModel.hpp"

namespace qure::ui {

void ConsoleModel::append(Level level, const QString& text) {
    if (lines_.size() >= kMaxLines) {
        beginRemoveRows({}, 0, 0);
        lines_.pop_front();
        endRemoveRows();
    }
    const int row = static_cast<int>(lines_.size());
    beginInsertRows({}, row, row);
    lines_.push_back({level, text, QTime::currentTime()});
    endInsertRows();
    emit countChanged();
}

void ConsoleModel::clear() {
    beginResetModel();
    lines_.clear();
    endResetModel();
    emit countChanged();
}

QString ConsoleModel::allText() const {
    QStringList out;
    for (const auto& l : lines_)
        out << l.text;
    return out.join(QLatin1Char('\n'));
}

int ConsoleModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(lines_.size());
}

QVariant ConsoleModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(lines_.size()))
        return {};
    const auto& l = lines_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Qt::DisplayRole:
    case TextRole: return l.text;
    case LevelRole: return static_cast<int>(l.level);
    case TimeRole: return l.time.toString(QStringLiteral("HH:mm:ss"));
    default: return {};
    }
}

QHash<int, QByteArray> ConsoleModel::roleNames() const {
    return {{TextRole, "text"}, {LevelRole, "level"}, {TimeRole, "time"}};
}

} // namespace qure::ui
