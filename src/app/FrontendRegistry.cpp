#include "FrontendRegistry.hpp"

#include "ObjectRegistry.hpp"
#include "Patch.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace qure::ui {

QString frontendKey(const Box& box) {
    switch (box.kind) {
    case BoxKind::FloatAtom: return QStringLiteral("@floatatom");
    case BoxKind::SymbolAtom: return QStringLiteral("@symbolatom");
    case BoxKind::ListAtom: return QStringLiteral("@listbox");
    case BoxKind::Object:
        if (box.child && box.className().str() == "graph")
            return QStringLiteral("@graph");
        if (box.object || box.text.empty())
            return QString::fromUtf8(box.className().str());
        return {};
    default: return {};
    }
}

bool FrontendRegistry::loadManifest(const QUrl& manifest, QString* error) {
    if (manifests_.contains(manifest))
        return true;
    const QString path = manifest.scheme() == QLatin1String("qrc") ? QLatin1Char(':') + manifest.path()
                                                                     : manifest.toLocalFile();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot read %1").arg(path);
        return false;
    }
    QJsonParseError pe;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, pe.errorString());
        return false;
    }
    manifests_.insert(manifest);
    const QUrl base = manifest.adjusted(QUrl::RemoveFilename);
    for (const auto& v : doc.object().value(QStringLiteral("nodes")).toArray()) {
        const auto o = v.toObject();
        FrontendInfo info;
        info.className = o.value(QStringLiteral("class")).toString();
        if (info.className.isEmpty())
            continue;
        info.qml = base.resolved(QUrl(o.value(QStringLiteral("qml")).toString()));
        if (const auto size = o.value(QStringLiteral("size")).toArray(); size.size() == 2)
            info.size = QSizeF(size[0].toDouble(), size[1].toDouble());
        info.widthArg = o.value(QStringLiteral("widthArg")).toInt(-1);
        info.widthScale = o.value(QStringLiteral("widthScale")).toDouble(1);
        info.heightArg = o.value(QStringLiteral("heightArg")).toInt(-1);
        info.heightScale = o.value(QStringLiteral("heightScale")).toDouble(1);
        info.inlets = o.value(QStringLiteral("inlets")).toInt(-1);
        info.outlets = o.value(QStringLiteral("outlets")).toInt(-1);
        info.loadArgs = o.value(QStringLiteral("loadArgs")).toBool(false);
        info.help = o.value(QStringLiteral("help")).toString();
        info.snapshots = o.value(QStringLiteral("snapshots")).toBool(false);

        QStringList names{info.className};
        for (const auto& a : o.value(QStringLiteral("aliases")).toArray())
            names << a.toString();
        const auto index = infos_.size();
        infos_.push_back(info);
        for (const auto& n : names)
            byName_.insert(n, index);
    }
    return true;
}

void FrontendRegistry::loadDirectory(const QString& dir) {
    QDirIterator it(dir, {QStringLiteral("nodes.json")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        loadManifest(QUrl::fromLocalFile(it.next()));
}

const FrontendInfo* FrontendRegistry::find(const QString& className) const {
    const auto it = byName_.constFind(className);
    return it == byName_.constEnd() ? nullptr : &infos_[*it];
}

const FrontendInfo* FrontendRegistry::find(const Box& box) const {
    if (box.broken())
        return nullptr;
    const auto key = frontendKey(box);
    return key.isEmpty() ? nullptr : find(key);
}

void FrontendRegistry::registerEngineClasses(ObjectRegistry& registry, std::size_t from) const {
    for (std::size_t i = from; i < infos_.size(); ++i) {
        const auto& info = infos_[i];
        if (info.inlets < 0 && info.outlets < 0)
            continue;
        const int ins = std::max(0, info.inlets), outs = std::max(0, info.outlets);
        const bool loadArgs = info.loadArgs;
        registry.add(info.className.toStdString(),
                     [ins, outs, loadArgs](const ObjectArgs& a) { return makeFrontendObject(a, ins, outs, loadArgs); },
                     info.help.toStdString());
    }
}

} // namespace qure::ui
