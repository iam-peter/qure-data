#pragma once

#include <QHash>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <QUrl>

#include <vector>

namespace qure {
class ObjectRegistry;
struct Box;
} // namespace qure

namespace qure::ui {

/// Describes a QML front end for a class of boxes. Loaded from
/// `nodes.json` manifests: the built-in one (qrc) and any found in the
/// user's node directory, so new nodes can be added without recompiling.
struct FrontendInfo {
    QString className;
    QUrl qml;
    QSizeF size{60, 30};      ///< default size in canvas units
    int widthArg = -1;        ///< take the width from this argument...
    double widthScale = 1;    ///< ...multiplied by this
    int heightArg = -1;
    double heightScale = 1;
    int inlets = -1;          ///< >= 0: the class is defined by the front end alone
    int outlets = -1;
    bool loadArgs = false;    ///< front-end-only: send the creation args out of outlet 0 on load
    QString help;
    bool snapshots = false;   ///< polls the object's SnapshotBuffer (scopes, arrays)
};

class FrontendRegistry {
public:
    /// Loads a manifest; relative "qml" paths resolve against its location.
    /// A manifest that was already loaded is skipped.
    bool loadManifest(const QUrl& manifest, QString* error = nullptr);
    /// Loads every `*/nodes.json` below a directory (user node packs).
    void loadDirectory(const QString& dir);
    /// How many front ends are registered; with registerEngineClasses(from)
    /// this registers only what a later manifest added.
    [[nodiscard]] std::size_t size() const { return infos_.size(); }

    [[nodiscard]] const FrontendInfo* find(const QString& className) const;
    /// The front end for a box, if it has one (also for atom boxes and
    /// array graphs, which have internal class names).
    [[nodiscard]] const FrontendInfo* find(const Box& box) const;
    [[nodiscard]] const std::vector<FrontendInfo>& all() const { return infos_; }

    /// Registers engine objects for front-end-only classes (those added
    /// from index `from` on).
    void registerEngineClasses(ObjectRegistry& registry, std::size_t from = 0) const;

private:
    std::vector<FrontendInfo> infos_;
    QHash<QString, std::size_t> byName_;
    QSet<QUrl> manifests_;
};

/// Name used to look a box up in the registry ("tgl", "@floatatom", "@graph").
QString frontendKey(const Box& box);

} // namespace qure::ui
